#include "MCPEngineStatus.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"

/**
 * Loads at PostConfigInit, which is the earliest phase where a plugin module
 * can run and still have config, paths and the feedback context available.
 * Everything the editor does between here and PostEngineInit - asset registry
 * scan, startup shader compilation, map load, and any prompt raised during
 * them - was previously invisible to ue-mcp because nothing of ours existed
 * yet. From this point on it is published to Saved/UE_MCP_Bridge/status.json.
 */
class FUE_MCP_BridgeStatusModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FMCPEngineStatus::Get().Install();
		FMCPEngineStatus::Get().SetPhase(TEXT("config init"));

		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda([]()
		{
			FMCPEngineStatus::Get().SetPhase(TEXT("modules loaded"));
		});
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
		{
			FMCPEngineStatus::Get().SetPhase(TEXT("engine loop initialized"));
		});
	}

	virtual void ShutdownModule() override
	{
		FMCPEngineStatus::Get().Shutdown();
	}
};

IMPLEMENT_MODULE(FUE_MCP_BridgeStatusModule, UE_MCP_BridgeStatus)
