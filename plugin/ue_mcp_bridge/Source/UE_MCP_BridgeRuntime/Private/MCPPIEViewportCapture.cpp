#include "MCPPIEViewportCapture.h"
#include "UE_MCP_BridgeRuntimeModule.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "PixelFormat.h"
#include "Math/Float16Color.h"

namespace MCPPIE
{

BEGIN_SHADER_PARAMETER_STRUCT(FMCPCapturePassParameters, )
END_SHADER_PARAMETER_STRUCT()

// Encode a BGRA FColor buffer to disk as JPEG or PNG. Safe on a background
// thread: FImageUtils PNG compression is thread-safe, and IImageWrapper is fine
// once the ImageWrapper module is loaded (it is a module dependency, loaded at
// startup). Falls back to PNG if the JPEG wrapper is unavailable.
static bool EncodeColorsToFile(const TArray<FColor>& Pixels, int32 W, int32 H, bool bJpeg, int32 Quality, const FString& Path)
{
	if (bJpeg)
	{
		IImageWrapperModule* IWM = FModuleManager::Get().GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (IWM)
		{
			TSharedPtr<IImageWrapper> Wrapper = IWM->CreateImageWrapper(EImageFormat::JPEG);
			if (Wrapper.IsValid() &&
				Wrapper->SetRaw(Pixels.GetData(), static_cast<int64>(Pixels.Num()) * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
			{
				const TArray64<uint8>& Data = Wrapper->GetCompressed(FMath::Clamp(Quality, 1, 100));
				if (Data.Num() > 0)
				{
					return FFileHelper::SaveArrayToFile(Data, *Path);
				}
			}
		}
		// fall through to PNG on any failure
	}
	TArray64<uint8> PNG;
	FImageUtils::PNGCompressImageArray(W, H, Pixels, PNG);
	return FFileHelper::SaveArrayToFile(PNG, *Path);
}

FPIEViewportCapture::FPIEViewportCapture(const FAutoRegister& AutoReg)
	: FSceneViewExtensionBase(AutoReg)
{
}

FPIEViewportCapture::~FPIEViewportCapture()
{
	// InFlight should already be empty: SetEnabled(false) drains it. This is
	// only here so the TUniquePtr<FRHIGPUTextureReadback> destructor sees the
	// complete type (the header forward-declares it).
}

bool FPIEViewportCapture::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!bEnabled.load(std::memory_order_acquire)) return false;
	// A targeted capture reads exactly that viewport; one PIE process can hold several players' viewports.
	if (const FViewport* Target = TargetViewport.load(std::memory_order_acquire))
	{
		return Context.Viewport == Target;
	}
	if (!GEngine || !GEngine->GameViewport) return false;
	return Context.Viewport == GEngine->GameViewport->Viewport;
}

void FPIEViewportCapture::SetTargetViewport(const FViewport* Viewport)
{
	TargetViewport.store(Viewport, std::memory_order_release);
}

void FPIEViewportCapture::SetEnabled(bool bEnable)
{
	bEnabled.store(bEnable, std::memory_order_release);
	if (!bEnable)
	{
		TArray<FPendingRequest> Dropped;
		{
			FScopeLock SL(&Lock);
			Dropped = MoveTemp(Pending);
		}
		for (FPendingRequest& Request : Dropped)
		{
			if (Request.OnWritten) Request.OnWritten(false);
		}
		// Drain outstanding GPU copies so their PNGs get written and the RHI
		// resources are released on the render thread before we're torn down.
		FlushPending();
	}
}

void FPIEViewportCapture::RequestCapture(const FString& OutputPath, FOnCaptureWritten OnWritten)
{
	FScopeLock SL(&Lock);
	Pending.Add({ OutputPath, MoveTemp(OnWritten) });
}

namespace
{
	void NotifyCaptureWritten(FPIEViewportCapture::FOnCaptureWritten OnWritten, bool bWritten)
	{
		if (OnWritten)
		{
			AsyncTask(ENamedThreads::GameThread, [OnWritten = MoveTemp(OnWritten), bWritten]() { OnWritten(bWritten); });
		}
	}
}

void FPIEViewportCapture::SetOutputFormat(bool bInUseJpeg, int32 InQuality)
{
	bUseJpeg.store(bInUseJpeg, std::memory_order_release);
	JpegQuality.store(FMath::Clamp(InQuality, 1, 100), std::memory_order_release);
}

int32 FPIEViewportCapture::GetCapturedCount() const
{
	return CapturedCount.load(std::memory_order_acquire);
}

// The viewport's render target is 8-bit BGRA in a window, but an offscreen client (-RenderOffscreen) renders into
// r.DefaultBackBufferPixelFormat, 10-bit by default. Decode each into 8-bit BGRA; anything else is refused, never
// reinterpreted.
static bool DecodePixels(const void* Data, int32 RowPitchInPixels, int32 W, int32 H, EPixelFormat Format, TArray<FColor>& Out)
{
	Out.SetNumUninitialized(W * H);
	switch (Format)
	{
	case PF_B8G8R8A8:
	case PF_R8G8B8A8:
		for (int32 y = 0; y < H; ++y)
		{
			FMemory::Memcpy(&Out[y * W], static_cast<const FColor*>(Data) + y * RowPitchInPixels, W * sizeof(FColor));
		}
		if (Format == PF_R8G8B8A8)
		{
			for (FColor& C : Out) { Swap(C.R, C.B); }
		}
		return true;
	case PF_A2B10G10R10:
		for (int32 y = 0; y < H; ++y)
		{
			const uint32* Row = static_cast<const uint32*>(Data) + y * RowPitchInPixels;
			for (int32 x = 0; x < W; ++x)
			{
				const uint32 P = Row[x];
				Out[y * W + x] = FColor(static_cast<uint8>((P & 0x3FF) >> 2), static_cast<uint8>(((P >> 10) & 0x3FF) >> 2), static_cast<uint8>(((P >> 20) & 0x3FF) >> 2), 255);
			}
		}
		return true;
	case PF_FloatRGBA:
		for (int32 y = 0; y < H; ++y)
		{
			const FFloat16Color* Row = static_cast<const FFloat16Color*>(Data) + y * RowPitchInPixels;
			for (int32 x = 0; x < W; ++x)
			{
				Out[y * W + x] = FLinearColor(Row[x]).ToFColorSRGB();
			}
		}
		return true;
	case PF_A32B32G32R32F:
		for (int32 y = 0; y < H; ++y)
		{
			const FLinearColor* Row = static_cast<const FLinearColor*>(Data) + y * RowPitchInPixels;
			for (int32 x = 0; x < W; ++x)
			{
				Out[y * W + x] = Row[x].ToFColorSRGB();
			}
		}
		return true;
	default:
		Out.Reset();
		return false;
	}
}

void FPIEViewportCapture::ProcessReadbacks_RenderThread(FRHICommandListImmediate& RHICmdList, bool bDrainAll)
{
	// A full drain (teardown) blocks once so every enqueued copy is guaranteed
	// ready; the per-frame path never blocks and just skips copies still in
	// flight, picking them up on a later frame.
	if (bDrainAll && InFlight.Num() > 0)
	{
		RHICmdList.SubmitAndBlockUntilGPUIdle();
	}

	for (int32 i = 0; i < InFlight.Num(); )
	{
		FInFlightReadback& R = InFlight[i];
		if (!bDrainAll && !R.Readback->IsReady())
		{
			++i;
			continue;
		}

		const int32 W = R.Width;
		const int32 H = R.Height;
		int32 RowPitchInPixels = 0;
		void* Data = R.Readback->Lock(RowPitchInPixels);
		TArray<FColor> Pixels;
		const bool bDecoded = Data && RowPitchInPixels >= W && DecodePixels(Data, RowPitchInPixels, W, H, R.Format, Pixels);
		if (Data)
		{
			R.Readback->Unlock();
		}
		if (bDecoded)
		{
			CapturedCount.fetch_add(1, std::memory_order_relaxed);
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
				[Pix = MoveTemp(Pixels), W, H, Path = R.Path, bJpeg = R.bJpeg, Quality = R.Quality, OnWritten = MoveTemp(R.OnWritten)]() mutable
				{
					NotifyCaptureWritten(MoveTemp(OnWritten), EncodeColorsToFile(Pix, W, H, bJpeg, Quality, Path));
				});
		}
		else
		{
			if (Data && RowPitchInPixels >= W)
			{
				UE_LOG(LogMCPPIERuntime, Warning, TEXT("Viewport capture: pixel format %s is not supported; %s not written"),
					GPixelFormats[R.Format].Name, *R.Path);
			}
			NotifyCaptureWritten(MoveTemp(R.OnWritten), false);
		}

		InFlight.RemoveAt(i);
	}
}

void FPIEViewportCapture::FlushPending()
{
	FPIEViewportCapture* Self = this;
	ENQUEUE_RENDER_COMMAND(MCPViewportCaptureDrain)(
		[Self](FRHICommandListImmediate& RHICmdList)
		{
			Self->ProcessReadbacks_RenderThread(RHICmdList, /*bDrainAll=*/true);
		});
	// Block the game thread until the drain has run and released its resources.
	FlushRenderingCommands();
}

void FPIEViewportCapture::PostRenderViewFamily_RenderThread(
	FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Retire any completed copies from earlier frames first. Never blocks.
	ProcessReadbacks_RenderThread(GraphBuilder.RHICmdList, /*bDrainAll=*/false);

	// Every request queued since the last frame reads this frame.
	TArray<FPendingRequest> Requests;
	{
		FScopeLock SL(&Lock);
		Requests = MoveTemp(Pending);
	}
	if (Requests.Num() == 0) return;

	const FRenderTarget* RT = InViewFamily.RenderTarget;
	FTextureRHIRef Texture = RT ? RT->GetRenderTargetTexture() : FTextureRHIRef();
	const FIntPoint Size = RT ? RT->GetSizeXY() : FIntPoint::ZeroValue;
	if (!Texture.IsValid() || Size.X <= 0 || Size.Y <= 0)
	{
		for (FPendingRequest& Request : Requests)
		{
			NotifyCaptureWritten(MoveTemp(Request.OnWritten), false);
		}
		return;
	}

	const EPixelFormat Format = Texture->GetFormat();
	for (FPendingRequest& Request : Requests)
	{
		EnqueueReadback_RenderThread(GraphBuilder, Texture, Size, Format, Request.Path, MoveTemp(Request.OnWritten));
	}
}

void FPIEViewportCapture::EnqueueReadback_RenderThread(FRDGBuilder& GraphBuilder, FTextureRHIRef Texture, FIntPoint Size,
	EPixelFormat Format, const FString& Path, FOnCaptureWritten OnWritten)
{
	// Enqueue an async GPU->CPU copy inside an RDG pass so it runs after the
	// scene has finished rendering. Unlike ReadSurfaceData this does not stall
	// the render thread; we poll for completion on a later frame.
	TUniquePtr<FRHIGPUTextureReadback> Readback =
		MakeUnique<FRHIGPUTextureReadback>(TEXT("MCPViewportCapture"));
	FRHIGPUTextureReadback* ReadbackPtr = Readback.Get();

	FMCPCapturePassParameters* Params = GraphBuilder.AllocParameters<FMCPCapturePassParameters>();
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MCPViewportCapture"),
		Params,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[ReadbackPtr, RHITex = Texture.GetReference()](FRHICommandListImmediate& RHICmdList)
		{
			ReadbackPtr->EnqueueCopy(RHICmdList, RHITex);
		});

	FInFlightReadback Entry;
	Entry.Readback = MoveTemp(Readback);
	Entry.Path = Path;
	Entry.Width = Size.X;
	Entry.Height = Size.Y;
	Entry.Format = Format;
	Entry.bJpeg = bUseJpeg.load(std::memory_order_acquire);
	Entry.Quality = JpegQuality.load(std::memory_order_acquire);
	Entry.OnWritten = MoveTemp(OnWritten);
	InFlight.Add(MoveTemp(Entry));
}

} // namespace MCPPIE
