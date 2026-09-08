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
		// Also here, not only on the modal-loop tick. A modal that has just
		// gone up is answered a frame sooner, and this is the tick that runs
		// with no modal at all, which is what clears the "already answered
		// this window" bookkeeping. The call is a null check when nothing is
		// modal.
		FDialogHandlers::ApplyPolicyToActiveModal();

		// The modal-safe queue is only emptied by the modal-loop tick below,
		// which never fires while no dialog is up. This is the tick that does
		// fire then, so it is where a finished entry is dropped. On an idle
		// bridge the queue is empty and the call is a single load.
		FMCPGameThreadExecutor::SweepModalSafeQueue();
	});

	GModalLoopHandle = FSlateApplication::Get().GetOnModalLoopTickEvent().AddLambda([](float)
	{
		FMCPEngineStatus::Get().CaptureNow();
		// Seeing the dialog is only half of it: the call that could answer it
		// is queued behind the same blocked game thread. Modal-safe handlers
		// run here so a dialog can be cleared from the outside.
		FMCPGameThreadExecutor::DrainModalSafeQueue();

		// Drain first, then apply: a set_dialog_policy that arrived on this
		// same tick is registered by the drain above and gets its chance here.
		//
		// This is the only place an armed policy can answer a Slate modal
		// WINDOW. FCoreDelegates::ModalMessageDialog carries FMessageDialog
		// prompts and nothing else, so the editor's own modals - the shutdown
		// "Save Content" prompt above all - were never offered to the policy
		// list at all, and an automated stop hung on one every time.
		FDialogHandlers::ApplyPolicyToActiveModal();
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
