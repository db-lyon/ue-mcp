#include "UE_MCP_ReplayModule.h"
#include "Modules/ModuleManager.h"
#include "MCPHandlerRegistration.h"
#include "Handlers/GameplayHandlers.h"
#include "PIE/PIEInputInjector.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIEObserver.h"
#include "UI/SMCPPIEPanel.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogMCPReplay);
IMPLEMENT_MODULE(FUE_MCP_ReplayModule, UE_MCP_Replay)

void FUE_MCP_ReplayModule::StartupModule()
{
	UEMCPPIE::FPIEInputInjector::Init();
	UEMCPPIE::FPIEInputRecorder::Get().Init();
	UEMCPPIE::FPIEInputReplayer::Get().Init();
	UEMCPPIE::FPIEObserver::Get().Init();
	SMCPPIEPanel::RegisterTab();
	SMCPPIEPanel::RegisterToolbarButton();

	FEditorDelegates::EndPIE.AddLambda([](bool)
	{
		UEMCPPIE::FPIEInputInjector::OnPIEEnded();
	});

	// Register all PIE handlers on the bridge via the external API.
	// Input injection
	UEMCP::RegisterExternalHandler(TEXT("inject_input"), &FGameplayHandlers::InjectInput);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_start"), &FGameplayHandlers::InjectInputStart);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_update"), &FGameplayHandlers::InjectInputUpdate);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_stop"), &FGameplayHandlers::InjectInputStop);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_tape"), &FGameplayHandlers::InjectInputTape);

	// Recording
	UEMCP::RegisterExternalHandler(TEXT("pie_record_arm"), &FGameplayHandlers::PieRecordArm);
	UEMCP::RegisterExternalHandler(TEXT("pie_record_disarm"), &FGameplayHandlers::PieRecordDisarm);
	UEMCP::RegisterExternalHandler(TEXT("pie_record_stop"), &FGameplayHandlers::PieRecordStop);
	UEMCP::RegisterExternalHandler(TEXT("pie_record_status"), &FGameplayHandlers::PieRecordStatus);
	UEMCP::RegisterExternalHandler(TEXT("pie_record_list"), &FGameplayHandlers::PieRecordList);
	UEMCP::RegisterExternalHandler(TEXT("pie_record_read"), &FGameplayHandlers::PieRecordRead);
	UEMCP::RegisterExternalHandler(TEXT("pie_record_delete"), &FGameplayHandlers::PieRecordDelete);
	UEMCP::RegisterExternalHandler(TEXT("pie_mark"), &FGameplayHandlers::PieMark);

	// Replay
	UEMCP::RegisterExternalHandler(TEXT("pie_replay_arm"), &FGameplayHandlers::PieReplayArm);
	UEMCP::RegisterExternalHandler(TEXT("pie_replay_disarm"), &FGameplayHandlers::PieReplayDisarm);
	UEMCP::RegisterExternalHandler(TEXT("pie_replay_stop"), &FGameplayHandlers::PieReplayStop);
	UEMCP::RegisterExternalHandler(TEXT("pie_replay_status"), &FGameplayHandlers::PieReplayStatus);

	// Diff / Snapshot
	UEMCP::RegisterExternalHandler(TEXT("pie_record_diff"), &FGameplayHandlers::PieRecordDiff);
	UEMCP::RegisterExternalHandler(TEXT("pie_snapshot"), &FGameplayHandlers::PieSnapshot);

	// Observation profiles
	UEMCP::RegisterExternalHandler(TEXT("pie_profile_create"), &FGameplayHandlers::PieProfileCreate);
	UEMCP::RegisterExternalHandler(TEXT("pie_profile_read"), &FGameplayHandlers::PieProfileRead);
	UEMCP::RegisterExternalHandler(TEXT("pie_profile_update"), &FGameplayHandlers::PieProfileUpdate);
	UEMCP::RegisterExternalHandler(TEXT("pie_profile_delete"), &FGameplayHandlers::PieProfileDelete);
	UEMCP::RegisterExternalHandler(TEXT("pie_profile_list"), &FGameplayHandlers::PieProfileList);

	// Observer
	UEMCP::RegisterExternalHandler(TEXT("pie_observe_arm"), &FGameplayHandlers::PieObserveArm);
	UEMCP::RegisterExternalHandler(TEXT("pie_observe_disarm"), &FGameplayHandlers::PieObserveDisarm);
	UEMCP::RegisterExternalHandler(TEXT("pie_observe_stop"), &FGameplayHandlers::PieObserveStop);
	UEMCP::RegisterExternalHandler(TEXT("pie_observe_status"), &FGameplayHandlers::PieObserveStatus);
	UEMCP::RegisterExternalHandler(TEXT("pie_observe_list"), &FGameplayHandlers::PieObserveList);
	UEMCP::RegisterExternalHandler(TEXT("pie_observe_read"), &FGameplayHandlers::PieObserveRead);

	// PIE inspection
	UEMCP::RegisterExternalHandler(TEXT("get_pie_anim_state"), &FGameplayHandlers::GetPieAnimState);
	UEMCP::RegisterExternalHandler(TEXT("get_pie_anim_properties"), &FGameplayHandlers::GetPieAnimProperties);
	UEMCP::RegisterExternalHandler(TEXT("get_pie_subsystem_state"), &FGameplayHandlers::GetPieSubsystemState);

	// CPU throttle suppression while recording/replaying/observing
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor) return true;
			bool bHasWorld = false;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World()) { bHasWorld = true; break; }
			}
			if (!bHasWorld) return true;

			UEditorEngine::FShouldDisableCPUThrottling Suppress;
			Suppress.BindLambda([]() -> bool
			{
				return UEMCPPIE::FPIEInputRecorder::Get().IsActive()
				    || UEMCPPIE::FPIEInputReplayer::Get().IsActive()
				    || UEMCPPIE::FPIEObserver::Get().IsActive();
			});
			GEditor->ShouldDisableCPUThrottlingDelegates.Add(Suppress);
			return false;
		})
	);

	UE_LOG(LogMCPReplay, Log, TEXT("[ue-mcp-replay] Registered %d handlers"), 33);
}

void FUE_MCP_ReplayModule::ShutdownModule()
{
	SMCPPIEPanel::UnregisterToolbarButton();
	SMCPPIEPanel::UnregisterTab();

	// Unregister all external handlers
	UEMCP::UnregisterExternalHandler(TEXT("inject_input"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_start"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_update"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_tape"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_status"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_list"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_read"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_delete"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_mark"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_replay_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_replay_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_replay_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_replay_status"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_record_diff"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_snapshot"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_profile_create"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_profile_read"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_profile_update"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_profile_delete"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_profile_list"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_observe_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_observe_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_observe_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_observe_status"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_observe_list"));
	UEMCP::UnregisterExternalHandler(TEXT("pie_observe_read"));
	UEMCP::UnregisterExternalHandler(TEXT("get_pie_anim_state"));
	UEMCP::UnregisterExternalHandler(TEXT("get_pie_anim_properties"));
	UEMCP::UnregisterExternalHandler(TEXT("get_pie_subsystem_state"));

	UEMCPPIE::FPIEObserver::Get().Shutdown();
	UEMCPPIE::FPIEInputReplayer::Get().Shutdown();
	UEMCPPIE::FPIEInputRecorder::Get().Shutdown();
	UEMCPPIE::FPIEInputInjector::Shutdown();
}
