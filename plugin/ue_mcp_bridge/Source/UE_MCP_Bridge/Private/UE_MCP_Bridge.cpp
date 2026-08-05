#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "EngineStatusHooks.h"
#include "MCPEngineStatus.h"
#include "Handlers/DialogHandlers.h"
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
	// Create and start bridge server. The base port is derived per-worktree
	// from the project root path (or an explicit -MCPPort / UE_MCP_PORT
	// override) so multiple checkouts can run side-by-side without colliding;
	// the probe loop in Run() resolves the rare clash and publishes the actual
	// bound port to the per-project lockfile.
	const int32 BasePort = FMCPBridgeServer::ResolveConfiguredPort();
	G_BridgeServer = MakeShared<FMCPBridgeServer>(BasePort);

	// The snapshot has been publishing since PostConfigInit, from the
	// UE_MCP_BridgeStatus module. Now that Slate, the shader compiler and the
	// asset compiler exist, hand it the sensors that need them.
	FMCPEngineStatusHooks::Install();
	FMCPEngineStatus::Get().SetPhase(TEXT("bridge starting"));

	FDialogHandlers::InstallDialogHook();
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
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server starting on base port %d"), BasePort);
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
			FMCPEngineStatus::Get().SetPhase(TEXT("ready"));

			return false; // done
		})
	);
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	FDialogHandlers::RemoveDialogHook();
	// The snapshot itself outlives this module (its own module owns it and
	// keeps publishing until PostConfigInit teardown); only the Slate and
	// Engine sensors go away with us.
	FMCPEngineStatusHooks::Remove();

	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}
