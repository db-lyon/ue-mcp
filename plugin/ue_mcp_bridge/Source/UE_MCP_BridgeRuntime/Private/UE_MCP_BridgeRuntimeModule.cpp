#include "UE_MCP_BridgeRuntimeModule.h"
#include "Modules/ModuleManager.h"
#include "MCPPIEInputInjector.h"

DEFINE_LOG_CATEGORY(LogMCPPIERuntime);
IMPLEMENT_MODULE(FUE_MCP_BridgeRuntimeModule, UE_MCP_BridgeRuntime)

void FUE_MCP_BridgeRuntimeModule::StartupModule()
{
	MCPPIE::FPIEInputInjector::Init();
}

void FUE_MCP_BridgeRuntimeModule::ShutdownModule()
{
	MCPPIE::FPIEInputInjector::Shutdown();
}
