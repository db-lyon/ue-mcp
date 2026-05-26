#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "Handlers/DialogHandlers.h"
#include "PIE/PIEInputInjector.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIEObserver.h"
#include "UI/SMCPPIEPanel.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogMCPBridge);
IMPLEMENT_MODULE(FUE_MCP_BridgeModule, UE_MCP_Bridge)

static TSharedPtr<FMCPBridgeServer> G_BridgeServer;

void FUE_MCP_BridgeModule::StartupModule()
{
	// Create and start bridge server
	G_BridgeServer = MakeShared<FMCPBridgeServer>(9877);
	FDialogHandlers::InstallDialogHook();
	UEMCPPIE::FPIEInputInjector::Init();
	UEMCPPIE::FPIEInputRecorder::Get().Init();
	UEMCPPIE::FPIEInputReplayer::Get().Init();
	UEMCPPIE::FPIEObserver::Get().Init();
	SMCPPIEPanel::RegisterTab();
	SMCPPIEPanel::RegisterToolbarButton();
	// Clear any leftover injections from a previous PIE session so a fresh
	// EndPIE-BeginPIE pair starts with no ghost holds in the queue.
	FEditorDelegates::EndPIE.AddLambda([](bool /*bIsSimulating*/)
	{
		UEMCPPIE::FPIEInputInjector::OnPIEEnded();
	});
	// Safety net: auto-decline overwrite dialogs to prevent game thread blocking.
	// Handlers should check for existing assets before creating, but if a dialog
	// slips through, decline it rather than blocking the game thread forever.
	FDialogHandlers::AddDefaultPolicy(TEXT("already exists"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Overwrite"), EAppReturnType::No);
	// Safety-net for the editor's auto "save level / save unsaved" prompts —
	// when an agent session ends or the editor closes, these would otherwise
	// block the main thread waiting on a human. Default to "Discard".
	// (Agents that actually want to persist changes still call project(build)
	//  / level(save) / asset(save) explicitly.)
	FDialogHandlers::AddDefaultPolicy(TEXT("Save Changes"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Save Content"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Unsaved"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Untitled"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("save your changes"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("save the level"), EAppReturnType::No);

	if (G_BridgeServer->Start())
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server started on port 9877"));
	}
	else
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to start bridge server"));
	}

	// Defer the editor-ready signal until GEditor is available and has at least one world.
	// GetEditorWorldContext(false) can fail if no editor world context exists yet,
	// so we iterate all world contexts instead (#162).
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor)
			{
				return true; // keep ticking — not ready yet
			}

			// Accept any world context (editor or PIE) as proof the editor is usable.
			bool bHasWorld = false;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World())
				{
					bHasWorld = true;
					break;
				}
			}
			if (!bHasWorld)
			{
				return true; // keep ticking
			}

			if (G_BridgeServer.IsValid())
			{
				G_BridgeServer->GetGameThreadExecutor().SetEditorReady();
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Editor ready — accepting requests"));
			}

			// Opt out of the editor's unfocused-CPU throttle while a PIE
			// recording or replay is in flight. UEditorEngine::GetMaxTickRate
			// hard-codes 3 fps when ShouldThrottleCPUUsage() is true, which
			// makes scripted PIE sessions (where the editor window doesn't
			// have OS focus) sample at ~3 Hz regardless of sample_hz / pin_fps.
			// The engine exposes ShouldDisableCPUThrottlingDelegates for
			// exactly this case; returning true suppresses the throttle
			// without mutating the user's project settings.
			UEditorEngine::FShouldDisableCPUThrottling Suppress;
			Suppress.BindLambda([]() -> bool
			{
				return UEMCPPIE::FPIEInputRecorder::Get().IsActive()
				    || UEMCPPIE::FPIEInputReplayer::Get().IsActive()
				    || UEMCPPIE::FPIEObserver::Get().IsActive();
			});
			GEditor->ShouldDisableCPUThrottlingDelegates.Add(Suppress);

			return false; // done
		})
	);
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	SMCPPIEPanel::UnregisterToolbarButton();
	SMCPPIEPanel::UnregisterTab();
	// Stop bridge server
	FDialogHandlers::RemoveDialogHook();
	UEMCPPIE::FPIEObserver::Get().Shutdown();
	UEMCPPIE::FPIEInputReplayer::Get().Shutdown();
	UEMCPPIE::FPIEInputRecorder::Get().Shutdown();
	UEMCPPIE::FPIEInputInjector::Shutdown();

	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}
