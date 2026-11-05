#include "AssetHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"
#include "UObject/Package.h"

namespace AssetCollectionEdit
{
	static FProperty* FindField(UStruct* Type, const FString& Name)
	{
		for (TFieldIterator<FProperty> It(Type); It; ++It)
			if (It->GetName() == Name || It->GetAuthoredName() == Name) return *It;
		return nullptr;
	}

	// Walk structs only: patching a copied object pointer must not edit its source.
	static bool Patch(UStruct* Type, void* Data, const TSharedPtr<FJsonObject>& Fields, FString& Error)
	{
		for (const auto& Pair : Fields->Values)
		{
			if (Pair.Key.IsEmpty()) { Error = TEXT("Override field names must not be empty"); return false; }
			TArray<FString> Segments;
			FString(*Pair.Key).ParseIntoArray(Segments, TEXT("."), false);
			UStruct* CurrentType = Type;
			void* CurrentData = Data;
			for (int32 Index = 0; Index < Segments.Num(); ++Index)
			{
				FProperty* Property = FindField(CurrentType, Segments[Index]);
				if (!Property)
				{
					Error = FString::Printf(TEXT("Unknown override field '%s'"), *Pair.Key);
					return false;
				}
				void* Address = Property->ContainerPtrToValuePtr<void>(CurrentData);
				if (Index == Segments.Num() - 1)
				{
					if (!MCPJsonProperty::SetJsonOnProperty(Property, Address, Pair.Value, Error)) return false;
				}
				else
				{
					FStructProperty* Nested = CastField<FStructProperty>(Property);
					if (!Nested) { Error = TEXT("Dotted overrides may traverse structs only"); return false; }
					CurrentType = Nested->Struct;
					CurrentData = Address;
				}
			}
		}
		return true;
	}

	static TSharedPtr<FJsonObject> ReadStruct(const UStruct* Type, const void* Data)
	{
		auto Fields = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Type); It; ++It)
			Fields->SetField(It->GetAuthoredName(), FMCPJsonSerializer::SerializeValue(It->ContainerPtrToValuePtr<void>(Data), *It));
		return Fields;
	}

	static void Persist(UObject* Asset, bool bSave, const TSharedPtr<FJsonObject>& Result)
	{
		FString Reason = TEXT("save=false; changes remain in memory");
		const bool bSaved = bSave && SaveAssetPackageChecked(Asset, Reason);
		Result->SetBoolField(TEXT("saved"), bSaved);
		Result->SetBoolField(TEXT("persisted"), bSaved);
		Result->SetBoolField(TEXT("packageDirty"), Asset->GetOutermost()->IsDirty());
		Result->SetStringField(TEXT("packageName"), Asset->GetOutermost()->GetName());
		if (!bSaved) Result->SetStringField(TEXT("persistError"), Reason);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::EditCollectionEntry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, PropertyName;
	const auto MissingAsset = RequireString(Params, TEXT("assetPath"), AssetPath);
	const auto MissingProperty = RequireString(Params, TEXT("propertyName"), PropertyName);
	const FString KeyField = OptionalString(Params, TEXT("keyField"));
	const auto EntryKey = TryGetParam(Params, TEXT("entryKey"));
	const auto NewKey = TryGetParam(Params, TEXT("newKey"));
	const TSharedPtr<FJsonObject>* Overrides = nullptr;
	TryGetObjectParam(Params, TEXT("overrides"), Overrides);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	if (MissingAsset) return MissingAsset;
	if (MissingProperty) return MissingProperty;
	if (!EntryKey.IsValid()) return MCPError(TEXT("entryKey is required"));
	if (!NewKey.IsValid() && (!Overrides || (*Overrides)->Values.IsEmpty())) return MCPError(TEXT("Supply overrides to update, or newKey to clone"));
	if (MCPIsProtectedAssetPath(AssetPath)) return MCPError(TEXT("Cannot edit a protected asset"));
	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	if (Cast<UBlueprint>(Asset)) return MCPError(TEXT("Use a data asset; Blueprint defaults are edited with set_property"));
	FProperty* Collection = nullptr;
	void* Address = nullptr;
	UObject* Owner = nullptr;
	FString Error;
	if (!MCPJsonProperty::ResolveDottedPath(Asset, PropertyName, Collection, Address, Owner, Error)) return MCPError(Error);
	if (Owner && Owner != Asset && !Owner->IsIn(Asset))
		return MCPError(TEXT("propertyName must stay within this asset and its owned subobjects"));
	FArrayProperty* Array = CastField<FArrayProperty>(Collection);
	FMapProperty* Map = CastField<FMapProperty>(Collection);
	FStructProperty* Element = CastField<FStructProperty>(Array ? Array->Inner : Map ? Map->ValueProp : nullptr);
	if (!Element) return MCPError(TEXT("propertyName must address a TArray of structs or a TMap with struct values"));
	FProperty* KeyProperty = Map ? Map->KeyProp : AssetCollectionEdit::FindField(Element->Struct, KeyField);
	if (!KeyProperty) return MCPError(TEXT("keyField must name a field on the array element"));
	if (NewKey.IsValid() && Element->HasAnyPropertyFlags(CPF_ContainsInstancedReference))
		return MCPError(TEXT("Cloning structs containing instanced subobjects is not supported"));
	FDefaultConstructedPropertyElement SourceKey(KeyProperty), DestinationKey(KeyProperty);
	if (!MCPJsonProperty::SetJsonOnProperty(KeyProperty, SourceKey.GetObjAddress(), EntryKey, Error)) return MCPError(Error);
	if (NewKey.IsValid() && !MCPJsonProperty::SetJsonOnProperty(KeyProperty, DestinationKey.GetObjAddress(), NewKey, Error)) return MCPError(Error);
	int32 SourceIndex = INDEX_NONE, Matches = 0;
	bool bDestinationExists = false;
	void* Source = nullptr;
	auto Match = [&](int32 Index, void* Key, void* Value)
	{
		if (KeyProperty->Identical(Key, SourceKey.GetObjAddress())) { ++Matches; SourceIndex = Index; Source = Value; }
		if (NewKey.IsValid() && KeyProperty->Identical(Key, DestinationKey.GetObjAddress())) bDestinationExists = true;
	};
	if (Array)
	{
		FScriptArrayHelper Helper(Array, Address);
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
			Match(Index, KeyProperty->ContainerPtrToValuePtr<void>(Helper.GetRawPtr(Index)), Helper.GetRawPtr(Index));
	}
	else
	{
		FScriptMapHelper Helper(Map, Address);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			if (Helper.IsValidIndex(Index)) Match(Index, Helper.GetKeyPtr(Index), Helper.GetValuePtr(Index));
	}
	if (Matches != 1) return MCPError(FString::Printf(TEXT("entryKey must match exactly one entry; matched %d"), Matches));
	if (bDestinationExists) return MCPError(TEXT("newKey already exists; no entry was changed"));
	FDefaultConstructedPropertyElement Staged(Element);
	Element->CopyCompleteValue(Staged.GetObjAddress(), Source);
	if (Overrides && !AssetCollectionEdit::Patch(Element->Struct, Staged.GetObjAddress(), *Overrides, Error)) return MCPError(Error);
	if (Array)
	{
		void* StagedKey = KeyProperty->ContainerPtrToValuePtr<void>(Staged.GetObjAddress());
		if (!KeyProperty->Identical(StagedKey, SourceKey.GetObjAddress())) return MCPError(TEXT("overrides must not change keyField; use newKey to clone"));
		if (NewKey.IsValid()) KeyProperty->CopyCompleteValue(StagedKey, DestinationKey.GetObjAddress());
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetField(TEXT("entryKey"), NewKey.IsValid() ? NewKey : EntryKey);
	Result->SetObjectField(TEXT("previousValue"), AssetCollectionEdit::ReadStruct(Element->Struct, Source));
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	if (bDryRun)
	{
		Result->SetObjectField(TEXT("proposedValue"), AssetCollectionEdit::ReadStruct(Element->Struct, Staged.GetObjAddress()));
		MCPSetNoRollback(Result, TEXT("Dry run; nothing changed"));
		return MCPResult(Result);
	}
	if (bSave) if (auto Blocked = MCPAssetWriteBlockedError(Asset, AssetPath, TEXT("edit collection entry"))) return Blocked;
	FString PreviousText;
	Collection->ExportText_Direct(PreviousText, Address, nullptr, Owner, PPF_None);
	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "EditCollectionEntry", "Edit collection entry"));
	Asset->Modify();
	if (Owner && Owner != Asset) Owner->Modify();
	int32 WrittenIndex = SourceIndex;
	if (Array)
	{
		FScriptArrayHelper Helper(Array, Address);
		if (NewKey.IsValid()) WrittenIndex = Helper.AddValue();
		Element->CopyCompleteValue(Helper.GetRawPtr(WrittenIndex), Staged.GetObjAddress());
	}
	else
	{
		FScriptMapHelper Helper(Map, Address);
		if (NewKey.IsValid())
		{
			WrittenIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
			KeyProperty->CopyCompleteValue(Helper.GetKeyPtr(WrittenIndex), DestinationKey.GetObjAddress());
		}
		Element->CopyCompleteValue(Helper.GetValuePtr(WrittenIndex), Staged.GetObjAddress());
		if (NewKey.IsValid()) Helper.Rehash();
	}
	if (Owner && Owner != Asset) Owner->PostEditChange();
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	// Re-resolve after notifications, which may reallocate the collection.
	bool bReadBack = false;
	if (MCPJsonProperty::ResolveDottedPath(Asset, PropertyName, Collection, Address, Owner, Error))
	{
		Array = CastField<FArrayProperty>(Collection);
		Map = CastField<FMapProperty>(Collection);
		void* Stored = nullptr;
		if (Array)
		{
			FScriptArrayHelper Helper(Array, Address);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				if (KeyProperty->Identical(KeyProperty->ContainerPtrToValuePtr<void>(Helper.GetRawPtr(Index)), NewKey.IsValid() ? DestinationKey.GetObjAddress() : SourceKey.GetObjAddress()))
				{
					Stored = Helper.GetRawPtr(Index);
					WrittenIndex = Index;
					break;
				}
			}
			Result->SetNumberField(TEXT("index"), WrittenIndex);
			Result->SetNumberField(TEXT("count"), Helper.Num());
		}
		else if (Map)
		{
			FScriptMapHelper Helper(Map, Address);
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (Helper.IsValidIndex(Index) && KeyProperty->Identical(Helper.GetKeyPtr(Index), NewKey.IsValid() ? DestinationKey.GetObjAddress() : SourceKey.GetObjAddress()))
				{
					Stored = Helper.GetValuePtr(Index);
					break;
				}
			}
			Result->SetNumberField(TEXT("count"), Helper.Num());
		}
		if (Stored)
		{
			Result->SetObjectField(TEXT("value"), AssetCollectionEdit::ReadStruct(Element->Struct, Stored));
			bReadBack = true;
		}
	}
	if (NewKey.IsValid()) MCPSetCreated(Result); else MCPSetUpdated(Result);
	auto Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("assetPath"), AssetPath);
	Rollback->SetStringField(TEXT("propertyName"), PropertyName);
	Rollback->SetStringField(TEXT("value"), PreviousText);
	Rollback->SetBoolField(TEXT("save"), bSave);
	MCPSetRollback(Result, TEXT("set_asset_property"), Rollback);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("Restores the whole collection; replay before subsequent collection edits"));
	if (bReadBack) AssetCollectionEdit::Persist(Asset, bSave, Result);
	else
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Entry missing after edit notification; nothing was saved"));
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetBoolField(TEXT("persisted"), false);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::CloneDataTableRows(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	const auto MissingAsset = RequireString(Params, TEXT("assetPath"), AssetPath);
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	TryGetArrayParam(Params, TEXT("clones"), Rows);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	if (MissingAsset) return MissingAsset;
	if (!Rows || Rows->IsEmpty() || Rows->Num() > 500) return MCPError(TEXT("clones must contain 1-500 clone requests"));
	if (MCPIsProtectedAssetPath(AssetPath)) return MCPError(TEXT("Cannot edit a protected asset"));
	UDataTable* Table = LoadAssetByPath<UDataTable>(AssetPath);
	if (!Table || !Table->GetRowStruct()) return MCPError(TEXT("DataTable with a row struct required"));
	// UDataTable exposes its row type as const; the reflection helpers below only read it.
	UScriptStruct* RowStruct = const_cast<UScriptStruct*>(Table->GetRowStruct());
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference))
			return MCPError(TEXT("Cloning rows containing instanced subobjects is not supported"));
	TArray<TSharedPtr<FStructOnScope>> Staged;
	TArray<FName> Names;
	TSet<FName> Destinations;
	for (const auto& Request : *Rows)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Request->TryGetObject(Object)) return MCPError(TEXT("Each clones entry must be an object"));
		FString SourceName, RowName;
		if (!(*Object)->TryGetStringField(TEXT("sourceRow"), SourceName) || !(*Object)->TryGetStringField(TEXT("rowName"), RowName)
			|| FName(*SourceName).IsNone() || FName(*RowName).IsNone()) return MCPError(TEXT("Each clone requires non-empty sourceRow and rowName"));
		const FName Name(*RowName);
		if (Table->GetRowMap().Contains(Name) || Destinations.Contains(Name)) return MCPError(FString::Printf(TEXT("Destination row already exists: %s"), *RowName));
		const uint8* const* Source = Table->GetRowMap().Find(FName(*SourceName));
		if (!Source) return MCPError(FString::Printf(TEXT("Source row not found: %s"), *SourceName));
		auto Copy = MakeShared<FStructOnScope>(RowStruct);
		RowStruct->CopyScriptStruct(Copy->GetStructMemory(), *Source);
		const TSharedPtr<FJsonObject>* Overrides = nullptr;
		if ((*Object)->HasField(TEXT("overrides")) && !(*Object)->TryGetObjectField(TEXT("overrides"), Overrides)) return MCPError(TEXT("overrides must be an object"));
		FString Error;
		if (Overrides && !AssetCollectionEdit::Patch(RowStruct, Copy->GetStructMemory(), *Overrides, Error)) return MCPError(Error);
		Staged.Add(Copy);
		Names.Add(Name);
		Destinations.Add(Name);
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	if (!bDryRun)
	{
		if (bSave) if (auto Blocked = MCPAssetWriteBlockedError(Table, AssetPath, TEXT("clone table rows"))) return Blocked;
		FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "CloneDataTableRows", "Clone table rows"));
		Table->Modify();
		for (int32 Index = 0; Index < Names.Num(); ++Index)
			Table->AddRow(Names[Index], *reinterpret_cast<FTableRowBase*>(Staged[Index]->GetStructMemory()));
		Table->MarkPackageDirty();
		MCPSetCreated(Result);
	}
	TArray<TSharedPtr<FJsonValue>> Readback;
	bool bReadBack = true;
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("rowName"), Names[Index].ToString());
		const uint8* const* Stored = Table->GetRowMap().Find(Names[Index]);
		if (bDryRun || (Stored && *Stored))
			Row->SetObjectField(TEXT("fields"), AssetCollectionEdit::ReadStruct(RowStruct, bDryRun ? Staged[Index]->GetStructMemory() : *Stored));
		else
		{
			Row->SetStringField(TEXT("error"), TEXT("Row missing after change notification"));
			bReadBack = false;
		}
		Readback.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(bDryRun ? TEXT("proposedRows") : TEXT("rows"), Readback);
	Result->SetNumberField(TEXT("clonedCount"), bDryRun ? 0 : Names.Num());
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	if (bDryRun) MCPSetNoRollback(Result, TEXT("Dry run; nothing changed"));
	else
	{
		if (bReadBack) AssetCollectionEdit::Persist(Table, bSave, Result);
		else
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("Rows disappeared during change notifications; nothing was saved"));
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetBoolField(TEXT("persisted"), false);
		}
		auto Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("assetPath"), AssetPath);
		TArray<TSharedPtr<FJsonValue>> RowNames;
		for (const FName Name : Names) RowNames.Add(MakeShared<FJsonValueString>(Name.ToString()));
		Rollback->SetArrayField(TEXT("rowNames"), RowNames);
		Rollback->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("remove_datatable_row"), Rollback);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("Removes the cloned rows with the same save policy. Row change hook side effects are not reversed; replay before further edits to these rows."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RemoveDataTableRows(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	const auto MissingAsset = RequireString(Params, TEXT("assetPath"), AssetPath);
	const FString RowName = OptionalString(Params, TEXT("rowName"));
	const TArray<TSharedPtr<FJsonValue>>* RequestedNames = nullptr;
	TryGetArrayParam(Params, TEXT("rowNames"), RequestedNames);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	if (MissingAsset) return MissingAsset;
	if (RequestedNames && !RowName.IsEmpty()) return MCPError(TEXT("Supply rowName or rowNames, not both"));
	if (RequestedNames && (RequestedNames->IsEmpty() || RequestedNames->Num() > 500)) return MCPError(TEXT("rowNames must contain 1-500 names"));
	TArray<FName> Names;
	if (RequestedNames)
	{
		for (const auto& Value : *RequestedNames)
		{
			FString Name;
			if (!Value->TryGetString(Name) || FName(*Name).IsNone()) return MCPError(TEXT("rowNames must contain non-empty names"));
			Names.AddUnique(FName(*Name));
		}
	}
	else if (!FName(*RowName).IsNone()) Names.Add(FName(*RowName));
	else return MCPError(TEXT("A row name is required"));
	if (MCPIsProtectedAssetPath(AssetPath)) return MCPError(TEXT("Cannot edit a protected table"));
	UDataTable* Table = LoadAssetByPath<UDataTable>(AssetPath);
	if (!Table) return MCPAssetLoadError(AssetPath, TEXT("DataTable"));
	if (bSave) if (auto Blocked = MCPAssetWriteBlockedError(Table, AssetPath, TEXT("remove table rows"))) return Blocked;
	TArray<TSharedPtr<FJsonValue>> Removed;
	bool bHasRows = false;
	for (const FName Name : Names) bHasRows |= Table->GetRowMap().Contains(Name);
	if (bHasRows)
	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "RemoveTableRows", "Remove table rows"));
		Table->Modify();
		for (const FName Name : Names)
		{
			if (!Table->GetRowMap().Contains(Name)) continue;
			Table->RemoveRow(Name);
			Removed.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		Table->MarkPackageDirty();
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("removedRows"), Removed);
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	Result->SetBoolField(TEXT("alreadyDeleted"), Removed.IsEmpty());
	Result->SetBoolField(TEXT("unchanged"), Removed.IsEmpty());
	if (!Removed.IsEmpty()) MCPSetUpdated(Result);
	MCPSetNoRollback(Result, TEXT("Use editor undo to restore removed rows and their values; row change hook side effects may not be reversible"));
	AssetCollectionEdit::Persist(Table, bSave, Result);
	return MCPResult(Result);
}
