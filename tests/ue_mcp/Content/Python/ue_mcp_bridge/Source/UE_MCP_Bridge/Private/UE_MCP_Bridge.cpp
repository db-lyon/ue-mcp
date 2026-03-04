#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "Misc/ConfigCacheIni.h"

IMPLEMENT_MODULE(FUE_MCP_BridgeModule, UE_MCP_Bridge)

static TSharedPtr<FMCPBridgeServer> G_BridgeServer;

void FUE_MCP_BridgeModule::StartupModule()
{
	// Create and start bridge server
	G_BridgeServer = MakeShared<FMCPBridgeServer>(9877);
	if (G_BridgeServer->Start())
	{
		UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Bridge server started on port 9877"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[UE-MCP] Failed to start bridge server"));
	}
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	// Stop bridge server
	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}
