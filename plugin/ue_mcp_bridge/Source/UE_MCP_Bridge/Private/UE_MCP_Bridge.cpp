#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "Handlers/DialogHandlers.h"
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
	// Safety net: auto-decline overwrite dialogs to prevent game thread blocking.
	// Handlers should check for existing assets before creating, but if a dialog
	// slips through, decline it rather than blocking the game thread forever.
	FDialogHandlers::AddDefaultPolicy(TEXT("already exists"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Overwrite"), EAppReturnType::No);

	if (G_BridgeServer->Start())
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server started on port 9877"));
	}
	else
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to start bridge server"));
	}

	// Defer the editor-ready signal until GEditor and the editor world are available.
	// Use a repeating ticker that checks each frame until ready.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor || !GEditor->GetEditorWorldContext(false).World())
			{
				return true; // keep ticking — not ready yet
			}
			if (G_BridgeServer.IsValid())
			{
				G_BridgeServer->GetGameThreadExecutor().SetEditorReady();
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Editor ready — accepting requests"));
			}
			return false; // done
		})
	);
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	// Stop bridge server
	FDialogHandlers::RemoveDialogHook();

	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}
