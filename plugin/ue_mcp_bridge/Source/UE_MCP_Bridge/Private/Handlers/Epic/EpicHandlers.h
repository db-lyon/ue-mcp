#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Wraps Epic's native 5.8 AI Toolset Registry (the plugin behind Unreal's
// experimental MCP server) as first-class ue-mcp actions. We reach the registry
// in-process by reflection over UToolsetRegistry's static entry points
// (/Script/ToolsetRegistry.ToolsetRegistry), so there is no Build.cs link
// dependency and the handlers degrade gracefully to "unavailable" on engine
// versions or projects where the plugin is not present.
//
// Design: mirror Epic's own discovery pattern (list -> describe -> call) so the
// ue-mcp surface stays tiny regardless of how many toolsets ship. 5.8 already
// exposes 50+ toolsets; flattening every tool into a static schema would be
// unworkable.
class FEpicHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Availability + toolset count. Never errors: reports available=false with a
	// reason when the registry plugin is not loaded.
	static TSharedPtr<FJsonValue> Status(const TSharedPtr<FJsonObject>& Params);

	// Lightweight catalog: per-toolset name/version/description/toolCount + tool
	// names. Strips the verbose per-tool input/output schemas to stay small.
	// Optional nameFilter (case-sensitive substring on the qualified name).
	static TSharedPtr<FJsonValue> ListToolsets(const TSharedPtr<FJsonObject>& Params);

	// Full schema for one toolset (tools with input/output schemas). Params: toolset.
	static TSharedPtr<FJsonValue> DescribeToolset(const TSharedPtr<FJsonObject>& Params);

	// Execute a registered tool as an AI agent would. Params: toolset, tool,
	// input? (object) | inputJson? (string). Returns the tool's JSON result.
	static TSharedPtr<FJsonValue> CallTool(const TSharedPtr<FJsonObject>& Params);
};
