#include "HandlerJsonProperty.h"
#include "HandlerUtils.h"
#include "JsonSerializer.h"
#include "MCPHandlerRegistration.h"

#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/DataAsset.h"
#include "Factories/DataAssetFactory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace
{
constexpr int32 MaxBulkUpsertItems = 500;
constexpr float BulkUpsertTimeoutSeconds = 120.0f;

struct FPreparedProperty
{
	FString Name;
	TSharedPtr<FJsonValue> RequestedValue;
	TSharedPtr<FJsonValue> PreviousValue;
	FString PreviousText;
	FString ProposedText;
};

struct FPreparedUpsertItem
{
	FString Name;
	FString PackagePath;
	FString AssetPath;
	FString ClassName;
	UClass* DataClass = nullptr;
	UObject* ExistingAsset = nullptr;
	bool bSkip = false;
	TArray<FPreparedProperty> Properties;
};

bool IsProtectedAssetPath(const FString& Path)
{
	FString Normalized = Path;
	Normalized.TrimStartAndEndInline();
	if (!Normalized.StartsWith(TEXT("/")))
	{
		Normalized = TEXT("/") + Normalized;
	}
	const FString Lower = Normalized.ToLower();
	return Lower.StartsWith(TEXT("/engine/"))
		|| Lower.StartsWith(TEXT("/script/"))
		|| Lower.StartsWith(TEXT("/memory/"))
		|| Lower.StartsWith(TEXT("/temp/"));
}

UClass* ResolveDataAssetClass(const FString& ClassName)
{
	UClass* DataClass = nullptr;
	if (ClassName.StartsWith(TEXT("/")))
	{
		DataClass = LoadClass<UObject>(nullptr, *ClassName);
		if (!DataClass)
		{
			DataClass = LoadObject<UClass>(nullptr, *ClassName);
		}
	}

	if (!DataClass)
	{
		FString Trimmed = ClassName;
		Trimmed.RemoveFromEnd(TEXT("_C"));
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == Trimmed || It->GetName() == ClassName)
			{
				DataClass = *It;
				break;
			}
		}
	}
	return DataClass;
}

bool ParseAssetIdentity(
	const TSharedPtr<FJsonObject>& Item,
	const int32 ItemIndex,
	FPreparedUpsertItem& OutItem,
	FString& OutError)
{
	if (!Item->TryGetStringField(TEXT("name"), OutItem.Name) || OutItem.Name.IsEmpty())
	{
		OutError = FString::Printf(TEXT("items[%d].name must be a non-empty string"), ItemIndex);
		return false;
	}
	if (!Item->TryGetStringField(TEXT("packagePath"), OutItem.PackagePath) || OutItem.PackagePath.IsEmpty())
	{
		OutError = FString::Printf(TEXT("items[%d].packagePath must be a non-empty string"), ItemIndex);
		return false;
	}
	if (!Item->TryGetStringField(TEXT("className"), OutItem.ClassName) || OutItem.ClassName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("items[%d].className must be a non-empty string"), ItemIndex);
		return false;
	}

	OutItem.Name.TrimStartAndEndInline();
	OutItem.PackagePath.TrimStartAndEndInline();
	OutItem.PackagePath.RemoveFromEnd(TEXT("/"));
	OutItem.ClassName.TrimStartAndEndInline();

	FText InvalidNameReason;
	const FName ObjectName(*OutItem.Name);
	if (ObjectName.IsNone() || !ObjectName.IsValidObjectName(InvalidNameReason))
	{
		OutError = FString::Printf(
			TEXT("items[%d].name '%s' is invalid: %s"),
			ItemIndex,
			*OutItem.Name,
			*InvalidNameReason.ToString());
		return false;
	}

	FText InvalidPackageReason;
	if (!FPackageName::IsValidLongPackageName(OutItem.PackagePath, true, &InvalidPackageReason))
	{
		OutError = FString::Printf(
			TEXT("items[%d].packagePath '%s' is invalid: %s"),
			ItemIndex,
			*OutItem.PackagePath,
			*InvalidPackageReason.ToString());
		return false;
	}

	const FString LongPackageName = OutItem.PackagePath + TEXT("/") + OutItem.Name;
	if (IsProtectedAssetPath(LongPackageName))
	{
		OutError = FString::Printf(TEXT("Refusing to mutate protected package '%s'"), *LongPackageName);
		return false;
	}
	OutItem.AssetPath = LongPackageName + TEXT(".") + OutItem.Name;
	return true;
}

bool PrepareProperties(
	const TSharedPtr<FJsonObject>& PropertiesObject,
	UObject* StagingAsset,
	const FString& AssetPath,
	const bool bCapturePreviousValues,
	TArray<FPreparedProperty>& OutProperties,
	FString& OutError)
{
	if (!PropertiesObject.IsValid())
	{
		return true;
	}

	TArray<FString> PropertyNames;
	for (const auto& Pair : PropertiesObject->Values)
	{
		PropertyNames.Add(FString(*Pair.Key));
	}
	PropertyNames.Sort();
	OutProperties.Reserve(PropertyNames.Num());

	for (const FString& PropertyName : PropertyNames)
	{
		const TSharedPtr<FJsonValue> RequestedValue = PropertiesObject->TryGetField(PropertyName);
		if (PropertyName.IsEmpty() || !RequestedValue.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Preflight failed for '%s': property names and values must be non-empty"),
				*AssetPath);
			return false;
		}

		FProperty* Property = nullptr;
		void* ValueAddress = nullptr;
		UObject* LeafOwner = nullptr;
		FString ResolveError;
		if (!MCPJsonProperty::ResolveDottedPath(
			StagingAsset,
			PropertyName,
			Property,
			ValueAddress,
			LeafOwner,
			ResolveError))
		{
			OutError = FString::Printf(
				TEXT("Preflight failed for '%s.%s': %s"),
				*AssetPath,
				*PropertyName,
				*ResolveError);
			return false;
		}

		FPreparedProperty Prepared;
		Prepared.Name = PropertyName;
		Prepared.RequestedValue = RequestedValue;
		if (bCapturePreviousValues)
		{
			Prepared.PreviousValue = FMCPJsonSerializer::SerializeValue(ValueAddress, Property);
			Property->ExportText_Direct(
				Prepared.PreviousText,
				ValueAddress,
				ValueAddress,
				LeafOwner,
				PPF_None);
		}

		FString SetError;
		if (!MCPJsonProperty::SetJsonOnProperty(Property, ValueAddress, RequestedValue, SetError))
		{
			OutError = FString::Printf(
				TEXT("Preflight failed for '%s.%s': %s"),
				*AssetPath,
				*PropertyName,
				*SetError);
			return false;
		}
		Property->ExportText_Direct(
			Prepared.ProposedText,
			ValueAddress,
			ValueAddress,
			LeafOwner,
			PPF_None);
		OutProperties.Add(MoveTemp(Prepared));
	}
	return true;
}

bool ApplyPreparedProperties(
	UObject* Asset,
	const TArray<FPreparedProperty>& Properties,
	int32& OutChangedPropertyCount,
	FString& OutError)
{
	OutChangedPropertyCount = 0;
	for (const FPreparedProperty& Prepared : Properties)
	{
		FProperty* Property = nullptr;
		void* ValueAddress = nullptr;
		UObject* LeafOwner = nullptr;
		FString ResolveError;
		if (!MCPJsonProperty::ResolveDottedPath(
			Asset,
			Prepared.Name,
			Property,
			ValueAddress,
			LeafOwner,
			ResolveError))
		{
			OutError = FString::Printf(TEXT("Failed to resolve '%s': %s"), *Prepared.Name, *ResolveError);
			return false;
		}

		FString BeforeText;
		Property->ExportText_Direct(BeforeText, ValueAddress, ValueAddress, LeafOwner, PPF_None);
		if (LeafOwner)
		{
			LeafOwner->Modify();
		}
		FString SetError;
		if (!MCPJsonProperty::SetJsonOnProperty(
			Property,
			ValueAddress,
			Prepared.RequestedValue,
			SetError))
		{
			OutError = FString::Printf(TEXT("Failed to set '%s': %s"), *Prepared.Name, *SetError);
			return false;
		}
		if (LeafOwner)
		{
			LeafOwner->PostEditChange();
		}

		FString AfterText;
		Property->ExportText_Direct(AfterText, ValueAddress, ValueAddress, LeafOwner, PPF_None);
		if (BeforeText != AfterText)
		{
			++OutChangedPropertyCount;
		}
	}
	return true;
}

TSharedPtr<FJsonObject> BuildItemResult(
	const FPreparedUpsertItem& Prepared,
	const FString& Status,
	const bool bSaved,
	const int32 ChangedPropertyCount)
{
	TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
	ItemResult->SetStringField(TEXT("assetPath"), Prepared.AssetPath);
	ItemResult->SetStringField(TEXT("name"), Prepared.Name);
	ItemResult->SetStringField(TEXT("packagePath"), Prepared.PackagePath);
	ItemResult->SetStringField(TEXT("className"), Prepared.DataClass
		? Prepared.DataClass->GetPathName()
		: Prepared.ClassName);
	ItemResult->SetStringField(TEXT("status"), Status);
	ItemResult->SetBoolField(TEXT("created"), Status == TEXT("created"));
	ItemResult->SetBoolField(TEXT("updated"), Status == TEXT("updated"));
	ItemResult->SetBoolField(TEXT("skipped"), Status == TEXT("skipped") || Status == TEXT("wouldSkip"));
	ItemResult->SetBoolField(TEXT("saved"), bSaved);
	ItemResult->SetNumberField(TEXT("propertyCount"), Prepared.Properties.Num());
	ItemResult->SetNumberField(TEXT("changedPropertyCount"), ChangedPropertyCount);
	return ItemResult;
}

TSharedPtr<FJsonValue> BulkRestoreDataAssets(const TSharedPtr<FJsonObject>& Params)
{
	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);

	const TArray<TSharedPtr<FJsonValue>>* UpdatedItems = nullptr;
	Params->TryGetArrayField(TEXT("updatedItems"), UpdatedItems);
	const TArray<TSharedPtr<FJsonValue>>* CreatedAssetPaths = nullptr;
	Params->TryGetArrayField(TEXT("createdAssetPaths"), CreatedAssetPaths);

	int32 RestoredAssetCount = 0;
	int32 DeletedAssetCount = 0;
	TArray<TSharedPtr<FJsonValue>> Errors;

	if (UpdatedItems)
	{
		for (int32 ItemIndex = 0; ItemIndex < UpdatedItems->Num(); ++ItemIndex)
		{
			const TSharedPtr<FJsonObject>* Item = nullptr;
			if (!(*UpdatedItems)[ItemIndex].IsValid()
				|| !(*UpdatedItems)[ItemIndex]->TryGetObject(Item)
				|| !Item
				|| !Item->IsValid())
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("updatedItems[%d] is invalid"), ItemIndex)));
				continue;
			}

			FString AssetPath;
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			if (!(*Item)->TryGetStringField(TEXT("assetPath"), AssetPath)
				|| IsProtectedAssetPath(AssetPath)
				|| !(*Item)->TryGetObjectField(TEXT("properties"), Properties)
				|| !Properties
				|| !Properties->IsValid())
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("updatedItems[%d] has invalid fields"), ItemIndex)));
				continue;
			}

			UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
			if (!Asset)
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Unable to load '%s' for rollback"), *AssetPath)));
				continue;
			}

			TArray<FPreparedProperty> RestoreProperties;
			TArray<FString> PropertyNames;
			for (const auto& Pair : (*Properties)->Values)
			{
				PropertyNames.Add(FString(*Pair.Key));
			}
			PropertyNames.Sort();
			for (const FString& PropertyName : PropertyNames)
			{
				FPreparedProperty& Restore = RestoreProperties.AddDefaulted_GetRef();
				Restore.Name = PropertyName;
				Restore.RequestedValue = (*Properties)->TryGetField(PropertyName);
			}

			Asset->Modify();
			int32 ChangedPropertyCount = 0;
			FString ApplyError;
			if (!ApplyPreparedProperties(Asset, RestoreProperties, ChangedPropertyCount, ApplyError))
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
					TEXT("Failed to restore '%s': %s"), *AssetPath, *ApplyError)));
				continue;
			}
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
			if (bSave && !UEditorAssetLibrary::SaveAsset(AssetPath, false))
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Failed to save restored asset '%s'"), *AssetPath)));
				continue;
			}
			++RestoredAssetCount;
		}
	}

	if (CreatedAssetPaths)
	{
		for (int32 PathIndex = CreatedAssetPaths->Num() - 1; PathIndex >= 0; --PathIndex)
		{
			FString AssetPath;
			if (!(*CreatedAssetPaths)[PathIndex].IsValid()
				|| !(*CreatedAssetPaths)[PathIndex]->TryGetString(AssetPath)
				|| IsProtectedAssetPath(AssetPath))
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("createdAssetPaths[%d] is invalid"), PathIndex)));
				continue;
			}
			if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
			{
				continue;
			}
			if (UEditorAssetLibrary::DeleteAsset(AssetPath))
			{
				++DeletedAssetCount;
			}
			else
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Failed to delete created asset '%s'"), *AssetPath)));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("success"), Errors.IsEmpty());
	Result->SetNumberField(TEXT("restoredAssetCount"), RestoredAssetCount);
	Result->SetNumberField(TEXT("deletedAssetCount"), DeletedAssetCount);
	Result->SetArrayField(TEXT("errors"), Errors);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> BulkUpsertDataAssets(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Params->TryGetArrayField(TEXT("items"), Items) || !Items)
	{
		return MCPError(TEXT("Missing 'items' array"));
	}
	if (Items->IsEmpty())
	{
		return MCPError(TEXT("'items' must contain at least one DataAsset descriptor"));
	}
	if (Items->Num() > MaxBulkUpsertItems)
	{
		return MCPError(FString::Printf(
			TEXT("'items' exceeds the maximum batch size of %d (received %d)"),
			MaxBulkUpsertItems,
			Items->Num()));
	}

	bool bDryRun = false;
	Params->TryGetBoolField(TEXT("dryRun"), bDryRun);
	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);
	FString OnConflict = TEXT("update");
	Params->TryGetStringField(TEXT("onConflict"), OnConflict);
	OnConflict.TrimStartAndEndInline();
	OnConflict.ToLowerInline();
	if (OnConflict != TEXT("update") && OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("onConflict must be 'update', 'skip', or 'error'"));
	}

	TArray<FPreparedUpsertItem> PreparedItems;
	PreparedItems.Reserve(Items->Num());
	TSet<FString> SeenAssetPaths;
	int32 RequestedPropertyCount = 0;

	for (int32 ItemIndex = 0; ItemIndex < Items->Num(); ++ItemIndex)
	{
		const TSharedPtr<FJsonObject>* ItemObject = nullptr;
		if (!(*Items)[ItemIndex].IsValid()
			|| !(*Items)[ItemIndex]->TryGetObject(ItemObject)
			|| !ItemObject
			|| !ItemObject->IsValid())
		{
			return MCPError(FString::Printf(TEXT("items[%d] must be an object"), ItemIndex));
		}

		FPreparedUpsertItem Prepared;
		FString IdentityError;
		if (!ParseAssetIdentity(*ItemObject, ItemIndex, Prepared, IdentityError))
		{
			return MCPError(IdentityError);
		}
		if (SeenAssetPaths.Contains(Prepared.AssetPath))
		{
			return MCPError(FString::Printf(TEXT("Duplicate DataAsset in batch: %s"), *Prepared.AssetPath));
		}
		SeenAssetPaths.Add(Prepared.AssetPath);

		Prepared.DataClass = ResolveDataAssetClass(Prepared.ClassName);
		if (!Prepared.DataClass)
		{
			return MCPError(FString::Printf(
				TEXT("items[%d].className could not be resolved: %s"),
				ItemIndex,
				*Prepared.ClassName));
		}
		if (!Prepared.DataClass->IsChildOf(UDataAsset::StaticClass()))
		{
			return MCPError(FString::Printf(
				TEXT("items[%d].className is not a UDataAsset subclass: %s"),
				ItemIndex,
				*Prepared.DataClass->GetPathName()));
		}
		if (Prepared.DataClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return MCPError(FString::Printf(
				TEXT("items[%d].className cannot be instantiated: %s"),
				ItemIndex,
				*Prepared.DataClass->GetPathName()));
		}

		Prepared.ExistingAsset = LoadObject<UObject>(nullptr, *Prepared.AssetPath);
		if (Prepared.ExistingAsset)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("DataAsset already exists: %s"), *Prepared.AssetPath));
			}
			if (Prepared.ExistingAsset->GetClass() != Prepared.DataClass)
			{
				return MCPError(FString::Printf(
					TEXT("Existing asset '%s' has class %s, expected %s"),
					*Prepared.AssetPath,
					*Prepared.ExistingAsset->GetClass()->GetPathName(),
					*Prepared.DataClass->GetPathName()));
			}
			Prepared.bSkip = OnConflict == TEXT("skip");
		}

		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		TSharedPtr<FJsonObject> Properties;
		if ((*ItemObject)->TryGetObjectField(TEXT("properties"), PropertiesObject)
			&& PropertiesObject
			&& PropertiesObject->IsValid())
		{
			Properties = *PropertiesObject;
		}
		else if ((*ItemObject)->HasField(TEXT("properties")))
		{
			return MCPError(FString::Printf(TEXT("items[%d].properties must be an object"), ItemIndex));
		}

		if (!Prepared.bSkip)
		{
			TStrongObjectPtr<UObject> StagingAsset;
			if (Prepared.ExistingAsset)
			{
				const FName StagingName = MakeUniqueObjectName(
					GetTransientPackage(),
					Prepared.DataClass,
					FName(TEXT("UEMCP_BulkUpsertExisting")));
				StagingAsset.Reset(DuplicateObject<UObject>(
					Prepared.ExistingAsset,
					GetTransientPackage(),
					StagingName));
			}
			else
			{
				const FName StagingName = MakeUniqueObjectName(
					GetTransientPackage(),
					Prepared.DataClass,
					FName(TEXT("UEMCP_BulkUpsertNew")));
				StagingAsset.Reset(NewObject<UObject>(
					GetTransientPackage(),
					Prepared.DataClass,
					StagingName));
			}
			if (!StagingAsset.IsValid())
			{
				return MCPError(FString::Printf(TEXT("Unable to create preflight object for '%s'"), *Prepared.AssetPath));
			}

			FString PropertyError;
			if (!PrepareProperties(
				Properties,
				StagingAsset.Get(),
				Prepared.AssetPath,
				Prepared.ExistingAsset != nullptr,
				Prepared.Properties,
				PropertyError))
			{
				return MCPError(PropertyError);
			}
			RequestedPropertyCount += Prepared.Properties.Num();
		}

		PreparedItems.Add(MoveTemp(Prepared));
	}

	TArray<TSharedPtr<FJsonValue>> ItemResults;
	ItemResults.Reserve(PreparedItems.Num());
	TArray<TSharedPtr<FJsonValue>> CreatedAssetPaths;
	TArray<TSharedPtr<FJsonValue>> UpdatedRollbackItems;
	int32 CreatedAssetCount = 0;
	int32 UpdatedAssetCount = 0;
	int32 UnchangedAssetCount = 0;
	int32 SkippedAssetCount = 0;
	int32 SavedAssetCount = 0;
	int32 SaveFailedCount = 0;
	int32 ChangedPropertyCount = 0;

	for (FPreparedUpsertItem& Prepared : PreparedItems)
	{
		if (Prepared.bSkip)
		{
			++SkippedAssetCount;
			ItemResults.Add(MakeShared<FJsonValueObject>(BuildItemResult(
				Prepared,
				bDryRun ? TEXT("wouldSkip") : TEXT("skipped"),
				false,
				0)));
			continue;
		}

		const bool bWouldCreate = Prepared.ExistingAsset == nullptr;
		int32 ItemChangedPropertyCount = 0;
		for (const FPreparedProperty& Property : Prepared.Properties)
		{
			if (bWouldCreate || Property.PreviousText != Property.ProposedText)
			{
				++ItemChangedPropertyCount;
			}
		}

		if (bDryRun)
		{
			const FString Status = bWouldCreate
				? TEXT("wouldCreate")
				: (ItemChangedPropertyCount > 0 ? TEXT("wouldUpdate") : TEXT("wouldRemainUnchanged"));
			ItemResults.Add(MakeShared<FJsonValueObject>(BuildItemResult(
				Prepared,
				Status,
				false,
				ItemChangedPropertyCount)));
			continue;
		}

		UObject* Asset = Prepared.ExistingAsset;
		if (bWouldCreate)
		{
			FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
			Factory->DataAssetClass = Prepared.DataClass;
			Asset = AssetTools.Get().CreateAsset(
				Prepared.Name,
				Prepared.PackagePath,
				Prepared.DataClass,
				Factory);
			if (!Asset)
			{
				return MCPError(FString::Printf(TEXT("Failed to create DataAsset '%s'"), *Prepared.AssetPath));
			}
			CreatedAssetPaths.Add(MakeShared<FJsonValueString>(Prepared.AssetPath));
		}

		Asset->Modify();
		FString ApplyError;
		int32 AppliedChangedPropertyCount = 0;
		if (!ApplyPreparedProperties(Asset, Prepared.Properties, AppliedChangedPropertyCount, ApplyError))
		{
			return MCPError(FString::Printf(
				TEXT("Apply failed after preflight for '%s': %s"),
				*Prepared.AssetPath,
				*ApplyError));
		}

		const bool bChanged = bWouldCreate || AppliedChangedPropertyCount > 0;
		if (bChanged)
		{
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
		}

		bool bSaved = false;
		if (bChanged && bSave)
		{
			bSaved = UEditorAssetLibrary::SaveAsset(Prepared.AssetPath, false);
			if (bSaved)
			{
				++SavedAssetCount;
			}
			else
			{
				++SaveFailedCount;
			}
		}

		if (bWouldCreate)
		{
			++CreatedAssetCount;
		}
		else if (bChanged)
		{
			++UpdatedAssetCount;
			TSharedPtr<FJsonObject> PreviousProperties = MakeShared<FJsonObject>();
			for (const FPreparedProperty& Property : Prepared.Properties)
			{
				if (Property.PreviousValue.IsValid())
				{
					PreviousProperties->SetField(Property.Name, Property.PreviousValue);
				}
			}
			TSharedPtr<FJsonObject> RollbackItem = MakeShared<FJsonObject>();
			RollbackItem->SetStringField(TEXT("assetPath"), Prepared.AssetPath);
			RollbackItem->SetObjectField(TEXT("properties"), PreviousProperties);
			UpdatedRollbackItems.Add(MakeShared<FJsonValueObject>(RollbackItem));
		}
		else
		{
			++UnchangedAssetCount;
		}
		ChangedPropertyCount += AppliedChangedPropertyCount;

		const FString Status = bWouldCreate
			? TEXT("created")
			: (bChanged ? TEXT("updated") : TEXT("unchanged"));
		ItemResults.Add(MakeShared<FJsonValueObject>(BuildItemResult(
			Prepared,
			Status,
			bSaved,
			AppliedChangedPropertyCount)));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("success"), SaveFailedCount == 0);
	if (SaveFailedCount > 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("One or more changed DataAssets could not be saved"));
	}
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("preflightPassed"), true);
	Result->SetBoolField(TEXT("mutationPerformed"), !bDryRun && (CreatedAssetCount + UpdatedAssetCount) > 0);
	Result->SetStringField(TEXT("onConflict"), OnConflict);
	Result->SetNumberField(TEXT("requestedAssetCount"), PreparedItems.Num());
	Result->SetNumberField(TEXT("requestedPropertyCount"), RequestedPropertyCount);
	Result->SetNumberField(TEXT("createdAssetCount"), CreatedAssetCount);
	Result->SetNumberField(TEXT("updatedAssetCount"), UpdatedAssetCount);
	Result->SetNumberField(TEXT("unchangedAssetCount"), UnchangedAssetCount);
	Result->SetNumberField(TEXT("skippedAssetCount"), SkippedAssetCount);
	Result->SetNumberField(TEXT("changedPropertyCount"), ChangedPropertyCount);
	Result->SetNumberField(TEXT("savedAssetCount"), SavedAssetCount);
	Result->SetNumberField(TEXT("saveFailedCount"), SaveFailedCount);
	Result->SetArrayField(TEXT("items"), ItemResults);

	if (!bDryRun && (CreatedAssetPaths.Num() > 0 || UpdatedRollbackItems.Num() > 0))
	{
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetArrayField(TEXT("createdAssetPaths"), CreatedAssetPaths);
		RollbackPayload->SetArrayField(TEXT("updatedItems"), UpdatedRollbackItems);
		RollbackPayload->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("bulk_restore_data_assets"), RollbackPayload);
	}
	return MCPResult(Result);
}

struct FRegisterBulkDataAssetHandlers
{
	FRegisterBulkDataAssetHandlers()
	{
		UEMCP::RegisterExternalHandlerWithTimeout(
			TEXT("bulk_upsert_data_assets"),
			&BulkUpsertDataAssets,
			BulkUpsertTimeoutSeconds);
		UEMCP::RegisterExternalHandlerWithTimeout(
			TEXT("asset.bulk_upsert_data_assets"),
			&BulkUpsertDataAssets,
			BulkUpsertTimeoutSeconds);
		UEMCP::RegisterExternalHandlerWithTimeout(
			TEXT("bulk_restore_data_assets"),
			&BulkRestoreDataAssets,
			BulkUpsertTimeoutSeconds);
	}
};

FRegisterBulkDataAssetHandlers RegisterBulkDataAssetHandlers;
}
