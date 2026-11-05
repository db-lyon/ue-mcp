#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class FRHIGPUTextureReadback;
class FViewport;

namespace MCPPIE
{

class UE_MCP_BRIDGERUNTIME_API FPIEViewportCapture : public FSceneViewExtensionBase
{
public:
	FPIEViewportCapture(const FAutoRegister& AutoReg);
	virtual ~FPIEViewportCapture() override;

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	/** Called on the game thread once the image is written (true) or could not be (false). */
	using FOnCaptureWritten = TFunction<void(bool bWritten)>;

	void SetEnabled(bool bEnable);
	/** Read this viewport only. Unset, the capture reads GEngine's game viewport, the only one in a -game process. */
	void SetTargetViewport(const FViewport* Viewport);
	void RequestCapture(const FString& OutputPath, FOnCaptureWritten OnWritten = nullptr);
	int32 GetCapturedCount() const;

	// Output encoding for subsequent captures. JPEG (default) is far smaller and
	// is what a vision model reads well; PNG is lossless for pixel-exact work.
	// The path extension passed to RequestCapture should match.
	void SetOutputFormat(bool bInUseJpeg, int32 InQuality);

	// Block-drain any in-flight GPU readbacks and dispatch their PNG writes.
	// Call from the game thread at teardown before releasing this extension.
	void FlushPending();

private:
	// A GPU->CPU copy that has been enqueued but not yet completed. All fields
	// are touched only on the render thread.
	struct FInFlightReadback
	{
		TUniquePtr<FRHIGPUTextureReadback> Readback;
		FString Path;
		int32 Width = 0;
		int32 Height = 0;
		EPixelFormat Format = PF_B8G8R8A8;
		bool bJpeg = true;
		int32 Quality = 80;
		FOnCaptureWritten OnWritten;
	};

	// Poll (or, when bDrainAll, block on) in-flight readbacks and hand each
	// completed one off to a background PNG write. Render thread only.
	void ProcessReadbacks_RenderThread(FRHICommandListImmediate& RHICmdList, bool bDrainAll);

	void EnqueueReadback_RenderThread(FRDGBuilder& GraphBuilder, FTextureRHIRef Texture, FIntPoint Size,
		EPixelFormat Format, const FString& Path, FOnCaptureWritten OnWritten);

	TArray<FInFlightReadback> InFlight;

	std::atomic<bool> bEnabled{false};
	std::atomic<bool> bUseJpeg{true};
	std::atomic<int32> JpegQuality{80};
	mutable FCriticalSection Lock;
	struct FPendingRequest { FString Path; FOnCaptureWritten OnWritten; };
	TArray<FPendingRequest> Pending;
	std::atomic<const FViewport*> TargetViewport{nullptr};
	std::atomic<int32> CapturedCount{0};
};

} // namespace MCPPIE
