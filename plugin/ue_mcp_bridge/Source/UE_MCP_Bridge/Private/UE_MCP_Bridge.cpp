#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "Handlers/DialogHandlers.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogMCPBridge);
IMPLEMENT_MODULE(FUE_MCP_BridgeModule, UE_MCP_Bridge)

static TSharedPtr<FMCPBridgeServer> G_BridgeServer;
static bool G_BridgeRestartScheduled = false;
static bool G_BridgeModuleShuttingDown = false;

namespace
{
	constexpr int32 DefaultBridgePort = 9877;

	void MarkEditorReadyIfPossible()
	{
		if (!G_BridgeServer.IsValid() || !GEditor || !GEngine)
		{
			return;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				G_BridgeServer->GetGameThreadExecutor().SetEditorReady();
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Editor ready - accepting requests"));
				return;
			}
		}
	}

	bool StartBridgeServer(int32 Port)
	{
		G_BridgeServer = MakeShared<FMCPBridgeServer>(Port);
		if (!G_BridgeServer->Start())
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to start bridge server"));
			return false;
		}

		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server started on port %d"), Port);
		MarkEditorReadyIfPossible();
		return true;
	}
}

void FUE_MCP_BridgeModule::StartupModule()
{
	G_BridgeModuleShuttingDown = false;

	FDialogHandlers::InstallDialogHook();
	// Safety net: auto-decline overwrite dialogs to prevent game thread blocking.
	// Handlers should check for existing assets before creating, but if a dialog
	// slips through, decline it rather than blocking the game thread forever.
	FDialogHandlers::AddDefaultPolicy(TEXT("already exists"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Overwrite"), EAppReturnType::No);
	// Safety-net for the editor's auto "save level / save unsaved" prompts.
	// When an agent session ends or the editor closes, these would otherwise
	// block the main thread waiting on a human. Default to "Discard".
	// Agents that want to persist changes still call project(build), level(save),
	// or asset(save) explicitly.
	FDialogHandlers::AddDefaultPolicy(TEXT("Save Changes"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Save Content"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Unsaved"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Untitled"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("save your changes"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("save the level"), EAppReturnType::No);

	StartBridgeServer(DefaultBridgePort);

	// Defer the editor-ready signal until GEditor is available and has at least one world.
	// GetEditorWorldContext(false) can fail if no editor world context exists yet,
	// so we iterate all world contexts instead (#162).
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor || !GEngine)
			{
				return true; // keep ticking - not ready yet
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

			MarkEditorReadyIfPossible();
			return false; // done
		})
	);

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (G_BridgeModuleShuttingDown)
			{
				return false;
			}

			if (!G_BridgeRestartScheduled && (!G_BridgeServer.IsValid() || !G_BridgeServer->IsRunning()))
			{
				const int32 Port = G_BridgeServer.IsValid() && G_BridgeServer->GetPort() > 0
					? G_BridgeServer->GetPort()
					: DefaultBridgePort;

				UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Bridge watchdog detected stopped server; restarting"));
				if (G_BridgeServer.IsValid())
				{
					G_BridgeServer->Shutdown();
					G_BridgeServer.Reset();
				}
				StartBridgeServer(Port);
			}

			return true;
		}),
		5.0f
	);
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	G_BridgeModuleShuttingDown = true;
	FDialogHandlers::RemoveDialogHook();

	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}

bool FUE_MCP_BridgeModule::IsBridgeServerRunning()
{
	return G_BridgeServer.IsValid() && G_BridgeServer->IsRunning();
}

int32 FUE_MCP_BridgeModule::GetBridgeServerPort()
{
	return G_BridgeServer.IsValid() ? G_BridgeServer->GetPort() : 0;
}

bool FUE_MCP_BridgeModule::RestartBridgeServer(FString& OutMessage)
{
	const int32 PreviousPort = GetBridgeServerPort() > 0 ? GetBridgeServerPort() : DefaultBridgePort;

	if (G_BridgeRestartScheduled)
	{
		OutMessage = TEXT("Bridge server restart is already scheduled");
		return true;
	}

	G_BridgeRestartScheduled = true;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([PreviousPort](float) -> bool
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Restarting bridge server on request"));

			if (G_BridgeServer.IsValid())
			{
				G_BridgeServer->Shutdown();
				G_BridgeServer.Reset();
			}

			StartBridgeServer(PreviousPort);
			G_BridgeRestartScheduled = false;
			return false;
		}),
		1.0f
	);

	OutMessage = FString::Printf(TEXT("Bridge server restart scheduled from port %d"), PreviousPort);
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] %s"), *OutMessage);
	return true;
}
