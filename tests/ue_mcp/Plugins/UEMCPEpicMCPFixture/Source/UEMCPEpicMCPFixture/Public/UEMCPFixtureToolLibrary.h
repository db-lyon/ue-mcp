// A legacy MCP Tool Library, the deprecated way Epic's MCP plugin still
// registers tools: every public function becomes a tool on module load.

#pragma once

#include "CoreMinimal.h"
#include "ModelContextProtocolToolLibrary.h"
#include "UEMCPFixtureToolLibrary.generated.h"

UCLASS()
class UUEMCPFixtureToolLibrary : public UModelContextProtocolToolLibrary
{
	GENERATED_BODY()

public:
	/** Greets a name. Test fixture for ue-mcp's epic(list_mcp_tools) and epic(call_mcp_tool). */
	UFUNCTION(BlueprintCallable, Category = "UE-MCP Fixture")
	static FString UeMcpFixtureLibraryGreet(const FString& Name);
};
