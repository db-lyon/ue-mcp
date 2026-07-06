#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// #685 — ChooserTable (UChooserTable) row authoring. Chooser tables are the
// data-driven selection layer behind Motion Matching (a chooser maps character
// state to which PoseSearchDatabase to search). The engine stores columns/rows
// as instanced structs with no scripting entry point, so extending a chooser
// (adding a stance branch, retargeting an output) previously meant hand-editing
// in the Chooser Table editor. These handlers introspect and author rows.
class FChooserHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> Create(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Describe(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddColumn(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListRows(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteRow(const TSharedPtr<FJsonObject>& Params);
};
