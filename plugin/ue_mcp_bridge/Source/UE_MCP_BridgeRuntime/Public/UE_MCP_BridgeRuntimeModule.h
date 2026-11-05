#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

UE_MCP_BRIDGERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogMCPPIERuntime, Log, All);

class FUE_MCP_BridgeRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
