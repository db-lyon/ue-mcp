#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Project-level editor operations: native C++ authoring (create class,
 * live coding compile, module discovery).
 *
 * These handlers wrap GameProjectGeneration / LiveCoding modules — the
 * same APIs the editor's own "File -> New C++ Class" and "Live Coding"
 * menus use — so agents can build native tools end-to-end without
 * escape-hatching through Python or the UI.
 */
class FProjectHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// v0.7.13 — native C++ authoring (#129)
	static TSharedPtr<FJsonValue> CreateCppClass(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListProjectModules(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LiveCodingCompile(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LiveCodingStatus(const TSharedPtr<FJsonObject>& Params);
};
