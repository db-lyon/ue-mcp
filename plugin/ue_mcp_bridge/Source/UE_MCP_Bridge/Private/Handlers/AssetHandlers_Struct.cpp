// Split from AssetHandlers.cpp. All functions below are still members of
// FAssetHandlers - this file is a translation-unit partition, not a new class.
// Handler registration stays in AssetHandlers.cpp::RegisterHandlers.
//
// #735 - UserDefinedStruct authoring. UUserDefinedStruct has no scripting entry
// point for creating, listing, renaming, retyping, or removing members; the
// Struct editor does that through FStructureEditorUtils (editor-internal). The
// key requirement is that renaming a field preserve the member GUID so existing
// Blueprint pins and DataTable rows keyed off that field survive the rename -
// FStructureEditorUtils::RenameVariable does exactly that (it only rewrites the
// friendly name, not the GUID). These handlers wrap it so struct-backed systems
// (DataTables, Blueprint variables) can be authored end to end from the bridge.

#include "AssetHandlers.h"
#include "BlueprintHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "MCPEngineCompat.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Human-readable type label for one member (e.g. "int", "Vector (struct)",
// "Actor (object)", "int[]"). Best-effort; agents mainly key off name/guid.
static FString StructFieldTypeLabel(const FStructVariableDescription& Desc)
{
	FString Base = Desc.Category.ToString();
	if (!Desc.SubCategoryObject.IsNull())
	{
		Base = FString::Printf(TEXT("%s (%s)"), *Desc.SubCategoryObject.GetAssetName(), *Base);
	}
	switch (Desc.ContainerType)
	{
	case EPinContainerType::Array: return Base + TEXT("[]");
	case EPinContainerType::Set:   return TEXT("Set<") + Base + TEXT(">");
	case EPinContainerType::Map:   return TEXT("Map<") + Base + TEXT(">");
	default:                       return Base;
	}
}

// Serialize one member to JSON: index, internal VarName, friendly (display) name,
// GUID string, type label.
static TSharedPtr<FJsonObject> StructFieldToJson(const FStructVariableDescription& Desc, int32 Index)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
	V->SetNumberField(TEXT("index"), Index);
	V->SetStringField(TEXT("name"), Desc.VarName.ToString());
	V->SetStringField(TEXT("friendlyName"), Desc.FriendlyName);
	V->SetStringField(TEXT("guid"), Desc.VarGuid.ToString());
	V->SetStringField(TEXT("type"), StructFieldTypeLabel(Desc));
	return V;
}

// Resolve a member's GUID from an explicit "fieldGuid" param, or by matching a
// "fieldName" param against either the friendly (display) name or the internal
// VarName. Returns false (invalid Guid) if unresolved.
static bool ResolveStructFieldGuid(const TSharedPtr<FJsonObject>& Params, UUserDefinedStruct* Struct, FGuid& OutGuid)
{
	const FString GuidStr = OptionalString(Params, TEXT("fieldGuid"));
	if (!GuidStr.IsEmpty())
	{
		return FGuid::Parse(GuidStr, OutGuid);
	}
	const FString FieldName = OptionalString(Params, TEXT("fieldName"));
	if (FieldName.IsEmpty()) return false;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		if (Desc.FriendlyName == FieldName || Desc.VarName.ToString() == FieldName)
		{
			OutGuid = Desc.VarGuid;
			return true;
		}
	}
	return false;
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateUserDefinedStruct(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("UserDefinedStruct")))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*(PackagePath + TEXT("/") + Name));
	if (!Package) return MCPError(FString::Printf(TEXT("Failed to create package for %s"), *Name));

	UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*Name), RF_Public | RF_Standalone);
	if (!Struct) return MCPError(TEXT("Failed to create UserDefinedStruct"));

	// CreateUserDefinedStruct seeds one default member. Capture its GUID so we can
	// strip it once the caller's own fields are in (a struct must never hit zero
	// members, so we add first and remove the seed last).
	TArray<FGuid> SeedGuids;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		SeedGuids.Add(Desc.VarGuid);
	}

	const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
	int32 AddedCount = 0;
	if (Params->TryGetArrayField(TEXT("fields"), Fields) && Fields)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Fields)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Entry->TryGetObject(Obj) || !Obj) continue;
			const FString FieldName = OptionalString(*Obj, TEXT("name"));
			const FString FieldType = OptionalString(*Obj, TEXT("type"), TEXT("bool"));
			if (FieldName.IsEmpty()) continue;

			// Snapshot GUIDs so we can identify the member AddVariable appends.
			TSet<FGuid> Before;
			for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(Struct)) Before.Add(D.VarGuid);

			if (!FStructureEditorUtils::AddVariable(Struct, FBlueprintHandlers::MakePinType(FieldType))) continue;

			for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(Struct))
			{
				if (!Before.Contains(D.VarGuid))
				{
					FStructureEditorUtils::RenameVariable(Struct, D.VarGuid, FieldName);
					++AddedCount;
					break;
				}
			}
		}
	}

	// Only drop the seed member if the caller supplied replacements; otherwise the
	// struct keeps the default so it never has zero members.
	if (AddedCount > 0)
	{
		for (const FGuid& Seed : SeedGuids)
		{
			FStructureEditorUtils::RemoveVariable(Struct, Seed);
		}
	}

	FAssetRegistryModule::AssetCreated(Struct);
	UEditorAssetLibrary::SaveLoadedAsset(Struct);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Struct->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	TArray<TSharedPtr<FJsonValue>> FieldList;
	int32 Idx = 0;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		FieldList.Add(MakeShared<FJsonValueObject>(StructFieldToJson(Desc, Idx++)));
	}
	Res->SetArrayField(TEXT("fields"), FieldList);
	Res->SetNumberField(TEXT("count"), FieldList.Num());
	MCPSetDeleteAssetRollback(Res, Struct->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListStructFields(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedStruct* Struct = LoadAssetByPath<UUserDefinedStruct>(AssetPath);
	if (!Struct) return MCPError(FString::Printf(TEXT("UserDefinedStruct not found (native structs are not editable): %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("name"), Struct->GetName());

	TArray<TSharedPtr<FJsonValue>> FieldList;
	int32 Idx = 0;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		FieldList.Add(MakeShared<FJsonValueObject>(StructFieldToJson(Desc, Idx++)));
	}
	Res->SetArrayField(TEXT("fields"), FieldList);
	Res->SetNumberField(TEXT("count"), FieldList.Num());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAssetHandlers::EditUserDefinedStruct(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString Op;
	if (auto Err = RequireString(Params, TEXT("op"), Op)) return Err;

	UUserDefinedStruct* Struct = LoadAssetByPath<UUserDefinedStruct>(AssetPath);
	if (!Struct) return MCPError(FString::Printf(TEXT("UserDefinedStruct not found (native structs are not editable): %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("op"), Op);

	if (Op == TEXT("add_field"))
	{
		const FString FieldType = OptionalString(Params, TEXT("type"), TEXT("bool"));
		FString FieldName = OptionalString(Params, TEXT("fieldName"));
		if (FieldName.IsEmpty()) FieldName = OptionalString(Params, TEXT("newDisplayName"));

		TSet<FGuid> Before;
		for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(Struct)) Before.Add(D.VarGuid);

		if (!FStructureEditorUtils::AddVariable(Struct, FBlueprintHandlers::MakePinType(FieldType)))
		{
			return MCPError(FString::Printf(TEXT("Failed to add field of type '%s'"), *FieldType));
		}

		FGuid NewGuid;
		for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(Struct))
		{
			if (!Before.Contains(D.VarGuid)) { NewGuid = D.VarGuid; break; }
		}
		if (NewGuid.IsValid() && !FieldName.IsEmpty())
		{
			FStructureEditorUtils::RenameVariable(Struct, NewGuid, FieldName);
		}

		UEditorAssetLibrary::SaveLoadedAsset(Struct);
		MCPSetUpdated(Res);
		if (const FStructVariableDescription* Desc = FStructureEditorUtils::GetVarDescByGuid(Struct, NewGuid))
		{
			Res->SetObjectField(TEXT("field"), StructFieldToJson(*Desc, INDEX_NONE));
		}
		Res->SetNumberField(TEXT("count"), FStructureEditorUtils::GetVarDesc(Struct).Num());

		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("assetPath"), AssetPath);
		RbPayload->SetStringField(TEXT("op"), TEXT("remove_field"));
		RbPayload->SetStringField(TEXT("fieldGuid"), NewGuid.ToString());
		MCPSetRollback(Res, TEXT("edit_user_defined_struct"), RbPayload);
		return MCPResult(Res);
	}
	else if (Op == TEXT("rename_field"))
	{
		FString NewDisplayName;
		if (auto Err = RequireString(Params, TEXT("newDisplayName"), NewDisplayName)) return Err;
		FGuid Guid;
		if (!ResolveStructFieldGuid(Params, Struct, Guid))
			return MCPError(TEXT("Could not resolve field (pass 'fieldGuid', or 'fieldName' matching a friendly or internal name)"));

		const FStructVariableDescription* Before = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid);
		const FString PrevName = Before ? Before->FriendlyName : FString();

		// RenameVariable rewrites only the friendly name; the member GUID is
		// preserved, so Blueprint pins and DataTable rows keyed off it survive.
		if (!FStructureEditorUtils::RenameVariable(Struct, Guid, NewDisplayName))
		{
			return MCPError(FString::Printf(TEXT("Rename to '%s' failed (name invalid or duplicate)"), *NewDisplayName));
		}

		UEditorAssetLibrary::SaveLoadedAsset(Struct);
		MCPSetUpdated(Res);
		if (const FStructVariableDescription* After = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid))
		{
			Res->SetObjectField(TEXT("field"), StructFieldToJson(*After, INDEX_NONE));
		}
		Res->SetStringField(TEXT("previousDisplayName"), PrevName);

		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("assetPath"), AssetPath);
		RbPayload->SetStringField(TEXT("op"), TEXT("rename_field"));
		RbPayload->SetStringField(TEXT("fieldGuid"), Guid.ToString());
		RbPayload->SetStringField(TEXT("newDisplayName"), PrevName);
		MCPSetRollback(Res, TEXT("edit_user_defined_struct"), RbPayload);
		return MCPResult(Res);
	}
	else if (Op == TEXT("set_field_type"))
	{
		FString FieldType;
		if (auto Err = RequireString(Params, TEXT("type"), FieldType)) return Err;
		FGuid Guid;
		if (!ResolveStructFieldGuid(Params, Struct, Guid))
			return MCPError(TEXT("Could not resolve field (pass 'fieldGuid', or 'fieldName' matching a friendly or internal name)"));

		if (!FStructureEditorUtils::ChangeVariableType(Struct, Guid, FBlueprintHandlers::MakePinType(FieldType)))
		{
			return MCPError(FString::Printf(TEXT("Failed to change field type to '%s'"), *FieldType));
		}

		UEditorAssetLibrary::SaveLoadedAsset(Struct);
		MCPSetUpdated(Res);
		if (const FStructVariableDescription* After = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid))
		{
			Res->SetObjectField(TEXT("field"), StructFieldToJson(*After, INDEX_NONE));
		}
		return MCPResult(Res);
	}
	else if (Op == TEXT("remove_field"))
	{
		FGuid Guid;
		if (!ResolveStructFieldGuid(Params, Struct, Guid))
			return MCPError(TEXT("Could not resolve field (pass 'fieldGuid', or 'fieldName' matching a friendly or internal name)"));
		if (FStructureEditorUtils::GetVarDesc(Struct).Num() <= 1)
			return MCPError(TEXT("Cannot remove the last field; a UserDefinedStruct must keep at least one member"));

		TSharedPtr<FJsonObject> Removed;
		if (const FStructVariableDescription* Desc = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid))
		{
			Removed = StructFieldToJson(*Desc, INDEX_NONE);
		}
		if (!FStructureEditorUtils::RemoveVariable(Struct, Guid))
		{
			return MCPError(TEXT("RemoveVariable failed"));
		}

		UEditorAssetLibrary::SaveLoadedAsset(Struct);
		MCPSetUpdated(Res);
		if (Removed.IsValid()) Res->SetObjectField(TEXT("removed"), Removed);
		Res->SetNumberField(TEXT("count"), FStructureEditorUtils::GetVarDesc(Struct).Num());
		return MCPResult(Res);
	}

	return MCPError(FString::Printf(TEXT("Unknown op '%s' (expected add_field|rename_field|set_field_type|remove_field)"), *Op));
}
