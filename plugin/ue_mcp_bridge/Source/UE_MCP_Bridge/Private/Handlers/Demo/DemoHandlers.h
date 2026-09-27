#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FMCPHandlerRegistry;

/**
 * Neon Shrine demo handlers. The nineteen steps are the server's demo_step_N
 * flows (src/tools/demo-steps.ts); these are what the flows cannot be.
 *
 * Registers 3 handlers:
 *   demo_get_steps  - Return ordered step list.
 *   demo_cleanup    - Remove all demo actors and assets.
 *   demo_go_home    - Open /Game/MCP_Home, creating it on first use.
 */
class FDemoHandlers
{
public:
	static void RegisterHandlers(FMCPHandlerRegistry& Registry);

private:
	// Top-level handlers
	static TSharedPtr<FJsonValue> DemoGetSteps(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DemoCleanup(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DemoGoHome(const TSharedPtr<FJsonObject>& Params);

	// Ensures /Game/MCP_Home exists on disk and loads it. Used by cleanup +
	// go_home to keep the editor anchored to a saved level instead of an
	// auto-generated Untitled (which traps the editor in a save dialog).
	static bool EnsureHomeLevelLoaded(FString& OutError);

	// Build the ordered step list (id, name, description)
	struct FDemoStep
	{
		int32 Index;
		FString Id;
		FString Description;
	};
	static TArray<FDemoStep> GetStepDefinitions();
};
