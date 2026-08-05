#include "EngineStatusHooks.h"
#include "MCPEngineStatus.h"
#include "UE_MCP_BridgeModule.h"
#include "GameThreadExecutor.h"
#include "Handlers/DialogHandlers.h"

#include "Framework/Application/SlateApplication.h"

#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"

namespace
{
	FDelegateHandle GPreTickHandle;
	FDelegateHandle GModalLoopHandle;
}

void FMCPEngineStatusHooks::Install()
{
	FMCPEngineStatus& Status = FMCPEngineStatus::Get();

	Status.SetModalProvider([](FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons)
	{
		return FDialogHandlers::DescribeActiveModal(OutTitle, OutMessage, OutButtons);
	});

	Status.SetCompileProvider([](int32& OutShaderJobs, int32& OutAssetCompiles)
	{
		OutShaderJobs = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		OutAssetCompiles = FAssetCompilingManager::Get().GetNumRemainingAssets();
	});

	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Slate not initialized at bridge startup - engine status keeps its core-only hooks"));
		return;
	}

	// Slate's pre-tick keeps firing while a long operation pumps the UI to draw
	// its progress bar, which is exactly the window where the core ticker is
	// suspended and every bridge request times out.
	GPreTickHandle = FSlateApplication::Get().OnPreTick().AddLambda([](float)
	{
		FMCPEngineStatus::Get().CaptureNow();
	});

	GModalLoopHandle = FSlateApplication::Get().GetOnModalLoopTickEvent().AddLambda([](float)
	{
		FMCPEngineStatus::Get().CaptureNow();
		// Seeing the dialog is only half of it: the call that could answer it
		// is queued behind the same blocked game thread. Modal-safe handlers
		// run here so a dialog can be cleared from the outside.
		FMCPGameThreadExecutor::DrainModalSafeQueue();
	});
}

void FMCPEngineStatusHooks::Remove()
{
	FMCPEngineStatus& Status = FMCPEngineStatus::Get();
	Status.SetModalProvider(nullptr);
	Status.SetCompileProvider(nullptr);

	if (FSlateApplication::IsInitialized())
	{
		if (GPreTickHandle.IsValid())
		{
			FSlateApplication::Get().OnPreTick().Remove(GPreTickHandle);
			GPreTickHandle.Reset();
		}
		if (GModalLoopHandle.IsValid())
		{
			FSlateApplication::Get().GetOnModalLoopTickEvent().Remove(GModalLoopHandle);
			GModalLoopHandle.Reset();
		}
	}
}
