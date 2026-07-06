// Split from AssetHandlers.cpp. All functions below are still members of
// FAssetHandlers - this file is a translation-unit partition, not a new class.
// Handler registration stays in AssetHandlers.cpp::RegisterHandlers.
//
// #686 — UserDefinedEnum authoring. UUserDefinedEnum exposes its DisplayNameMap
// for reading but has no scripting entry point for adding/renaming/removing
// enumerators; the editor does that through FEnumEditorUtils (editor-internal).
// These handlers wrap FEnumEditorUtils so enum-keyed systems (Chooser tables,
// gameplay state branches) can be authored end to end without hand-editing.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// True for indices that are the implicit trailing "_MAX" sentinel UEnum keeps.
static bool IsEnumMaxSentinel(const UEnum* Enum, int32 Index)
{
	return Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX"));
}

// Count of authored enumerators (excludes the trailing _MAX sentinel).
static int32 CountRealEnumerators(const UEnum* Enum)
{
	int32 Count = 0;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (!IsEnumMaxSentinel(Enum, i)) ++Count;
	}
	return Count;
}

// Serialize one enumerator to JSON: index, authored short name, display name, value.
static TSharedPtr<FJsonObject> EnumeratorToJson(const UEnum* Enum, int32 Index)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
	V->SetNumberField(TEXT("index"), Index);
	// GetNameStringByIndex returns the short authored name (e.g. "NewEnumerator0").
	V->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
	V->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(Index).ToString());
	V->SetNumberField(TEXT("value"), (double)Enum->GetValueByIndex(Index));
	return V;
}

// Resolve an enumerator index from an explicit "index" param, or by matching a
// "name" param against either the authored short name or the display name.
// Returns INDEX_NONE if unresolved.
static int32 ResolveEnumeratorIndex(const TSharedPtr<FJsonObject>& Params, const UEnum* Enum)
{
	int32 Index = INDEX_NONE;
	if (Params->TryGetNumberField(TEXT("index"), Index))
	{
		return Index;
	}
	const FString Name = OptionalString(Params, TEXT("name"));
	if (Name.IsEmpty()) return INDEX_NONE;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (IsEnumMaxSentinel(Enum, i)) continue;
		if (Enum->GetNameStringByIndex(i) == Name ||
			Enum->GetDisplayNameTextByIndex(i).ToString() == Name)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

TSharedPtr<FJsonValue> FAssetHandlers::ListEnumValues(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UEnum* Enum = Cast<UEnum>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Enum) return MCPError(FString::Printf(TEXT("Enum not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("name"), Enum->GetName());
	Res->SetBoolField(TEXT("isUserDefined"), Enum->IsA<UUserDefinedEnum>());

	TArray<TSharedPtr<FJsonValue>> Values;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (IsEnumMaxSentinel(Enum, i)) continue;
		Values.Add(MakeShared<FJsonValueObject>(EnumeratorToJson(Enum, i)));
	}
	Res->SetArrayField(TEXT("values"), Values);
	Res->SetNumberField(TEXT("count"), Values.Num());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAssetHandlers::EditUserDefinedEnum(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString Op;
	if (auto Err = RequireString(Params, TEXT("op"), Op)) return Err;

	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Enum) return MCPError(FString::Printf(TEXT("UserDefinedEnum not found (native UEnums cannot be edited): %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("op"), Op);

	if (Op == TEXT("add_value"))
	{
		// AddNew appends an enumerator with an auto-generated authored name
		// (e.g. "NewEnumerator3"). The authored name is not user-settable via the
		// public API; the display name is, and that is what UIs and choosers show.
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);

		// The new enumerator sits just before the trailing _MAX sentinel.
		const int32 NewIndex = Enum->NumEnums() - 2;
		if (NewIndex < 0) return MCPError(TEXT("Failed to add enumerator"));

		// Prefer an explicit displayName; fall back to `name` so a single param works.
		FString DisplayName = OptionalString(Params, TEXT("displayName"));
		if (DisplayName.IsEmpty()) DisplayName = OptionalString(Params, TEXT("name"));
		if (!DisplayName.IsEmpty())
		{
			if (!FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName)))
			{
				return MCPError(FString::Printf(TEXT("Enumerator added but display name '%s' is invalid or duplicate"), *DisplayName));
			}
		}

		UEditorAssetLibrary::SaveLoadedAsset(Enum);
		MCPSetUpdated(Res);
		Res->SetObjectField(TEXT("value"), EnumeratorToJson(Enum, NewIndex));
		Res->SetNumberField(TEXT("count"), CountRealEnumerators(Enum));

		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("assetPath"), AssetPath);
		RbPayload->SetStringField(TEXT("op"), TEXT("remove_value"));
		RbPayload->SetNumberField(TEXT("index"), NewIndex);
		MCPSetRollback(Res, TEXT("edit_user_defined_enum"), RbPayload);
		return MCPResult(Res);
	}
	else if (Op == TEXT("rename_value"))
	{
		FString DisplayName;
		if (auto Err = RequireString(Params, TEXT("displayName"), DisplayName)) return Err;
		const int32 Index = ResolveEnumeratorIndex(Params, Enum);
		if (Index == INDEX_NONE || IsEnumMaxSentinel(Enum, Index))
			return MCPError(TEXT("Could not resolve enumerator (pass 'index', or 'name' matching a short or display name)"));

		const FString PrevDisplay = Enum->GetDisplayNameTextByIndex(Index).ToString();
		if (!FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, FText::FromString(DisplayName)))
		{
			return MCPError(FString::Printf(TEXT("Display name '%s' is invalid or duplicate"), *DisplayName));
		}

		UEditorAssetLibrary::SaveLoadedAsset(Enum);
		MCPSetUpdated(Res);
		Res->SetObjectField(TEXT("value"), EnumeratorToJson(Enum, Index));
		Res->SetStringField(TEXT("previousDisplayName"), PrevDisplay);
		return MCPResult(Res);
	}
	else if (Op == TEXT("remove_value"))
	{
		const int32 Index = ResolveEnumeratorIndex(Params, Enum);
		if (Index == INDEX_NONE || IsEnumMaxSentinel(Enum, Index))
			return MCPError(TEXT("Could not resolve enumerator (pass 'index', or 'name' matching a short or display name)"));
		if (CountRealEnumerators(Enum) <= 1)
			return MCPError(TEXT("Cannot remove the last enumerator; a UserDefinedEnum must keep at least one value"));

		TSharedPtr<FJsonObject> Removed = EnumeratorToJson(Enum, Index);
		FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Index);

		UEditorAssetLibrary::SaveLoadedAsset(Enum);
		MCPSetUpdated(Res);
		Res->SetObjectField(TEXT("removed"), Removed);
		Res->SetNumberField(TEXT("count"), CountRealEnumerators(Enum));
		return MCPResult(Res);
	}

	return MCPError(FString::Printf(TEXT("Unknown op '%s' (expected add_value|rename_value|remove_value)"), *Op));
}
