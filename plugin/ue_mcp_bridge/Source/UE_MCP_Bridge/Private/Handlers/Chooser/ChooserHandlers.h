#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// #685 - ChooserTable (UChooserTable) row authoring. Chooser tables are the
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
	// #754: read and repoint object references inside nested chooser tables.
	static TSharedPtr<FJsonValue> ListObjectReferences(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemapObjectReferences(const TSharedPtr<FJsonObject>& Params);

	// #1828: the rest of what the Chooser Table editor can do, in
	// ChooserHandlers_Authoring.cpp. Implemented against the engine's own
	// chooser data structures so every engine from 5.4 to 5.9 takes one path.
	static TSharedPtr<FJsonValue> ListTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSignature(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSignature(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteColumn(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveColumn(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DuplicateColumn(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CopyColumn(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetColumn(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteRows(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveRows(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DuplicateRows(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CopyRows(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCell(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCell(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetFallback(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AutoPopulate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNested(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListNested(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteNested(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameNested(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveDisabled(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RestoreSnapshot(const TSharedPtr<FJsonObject>& Params);
};
