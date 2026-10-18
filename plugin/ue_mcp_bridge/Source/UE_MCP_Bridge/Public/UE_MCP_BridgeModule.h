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

private:
	/** -RenderOffScreen turned the background CPU throttle off in this session. */
	bool bThrottleTurnedOff = false;
	/** The throttle value as read from config before this session turned it off. */
	bool bThrottleUserValue = false;
};
