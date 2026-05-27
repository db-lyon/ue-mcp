#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "HAL/CriticalSection.h"
#include <atomic>

namespace UEMCPPIE
{

class FPIEViewportCapture : public FSceneViewExtensionBase
{
public:
	FPIEViewportCapture(const FAutoRegister& AutoReg);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	void SetEnabled(bool bEnable);
	void RequestCapture(const FString& OutputPath);
	int32 GetCapturedCount() const;

private:
	std::atomic<bool> bEnabled{false};
	mutable FCriticalSection Lock;
	FString PendingPath;
	std::atomic<int32> CapturedCount{0};
};

} // namespace UEMCPPIE
