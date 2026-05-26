#include "PIEViewportCapture.h"
#include "UE_MCP_BridgeModule.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"

namespace UEMCPPIE
{

BEGIN_SHADER_PARAMETER_STRUCT(FMCPCapturePassParameters, )
END_SHADER_PARAMETER_STRUCT()

FPIEViewportCapture::FPIEViewportCapture(const FAutoRegister& AutoReg)
	: FSceneViewExtensionBase(AutoReg)
{
}

bool FPIEViewportCapture::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!bEnabled.load(std::memory_order_acquire)) return false;
	if (!GEngine || !GEngine->GameViewport) return false;
	return Context.Viewport == GEngine->GameViewport->Viewport;
}

void FPIEViewportCapture::SetEnabled(bool bEnable)
{
	bEnabled.store(bEnable, std::memory_order_release);
	if (!bEnable)
	{
		FScopeLock SL(&Lock);
		PendingPath.Reset();
	}
}

void FPIEViewportCapture::RequestCapture(const FString& OutputPath)
{
	FScopeLock SL(&Lock);
	PendingPath = OutputPath;
}

int32 FPIEViewportCapture::GetCapturedCount() const
{
	return CapturedCount.load(std::memory_order_acquire);
}

void FPIEViewportCapture::PostRenderViewFamily_RenderThread(
	FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	FString Path;
	{
		FScopeLock SL(&Lock);
		if (PendingPath.IsEmpty()) return;
		Path = PendingPath;
		PendingPath.Reset();
	}

	const FRenderTarget* RT = InViewFamily.RenderTarget;
	if (!RT) return;

	FTextureRHIRef Texture = RT->GetRenderTargetTexture();
	if (!Texture.IsValid()) return;

	const FIntPoint Size = RT->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0) return;

	const int32 W = Size.X;
	const int32 H = Size.Y;
	std::atomic<int32>* Counter = &CapturedCount;

	FMCPCapturePassParameters* Params = GraphBuilder.AllocParameters<FMCPCapturePassParameters>();
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MCPViewportCapture"),
		Params,
		ERDGPassFlags::NeverCull,
		[RHITex = Texture.GetReference(), W, H, Path, Counter](FRHICommandListImmediate& RHICmdList)
		{
			TArray<FColor> Pixels;
			RHICmdList.ReadSurfaceData(
				RHITex, FIntRect(0, 0, W, H),
				Pixels, FReadSurfaceDataFlags(RCM_UNorm));

			if (Pixels.Num() == W * H)
			{
				Counter->fetch_add(1, std::memory_order_relaxed);
				AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
					[Pix = MoveTemp(Pixels), W, H, Path]()
					{
						TArray64<uint8> PNG;
						FImageUtils::PNGCompressImageArray(W, H, Pix, PNG);
						FFileHelper::SaveArrayToFile(PNG, *Path);
					});
			}
		});
}

} // namespace UEMCPPIE
