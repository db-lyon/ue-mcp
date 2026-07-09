#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMCPBridge, Log, All);

class FUE_MCP_BridgeModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static bool IsBridgeServerRunning();
	static int32 GetBridgeServerPort();
	static bool RestartBridgeServer(FString& OutMessage);
};
