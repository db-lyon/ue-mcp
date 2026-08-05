// Split from AssetHandlers.cpp to keep that file under 3k lines.
// FAssetHandlers::BulkSetAssetProperties lives here; registration stays in
// AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"

namespace
{
	constexpr int32 MaxBulkPropertyAssets = 500;

	// Per-item outcome vocabulary. Every submitted item gets exactly one of
	// these back, so a caller can always account for all N entries it sent.
	const TCHAR* const BulkStatusOk           = TEXT("ok");
	const TCHAR* const BulkStatusInvalid      = TEXT("invalid");
	const TCHAR* const BulkStatusProtected    = TEXT("protected");
	const TCHAR* const BulkStatusDuplicate    = TEXT("duplicate");
	const TCHAR* const BulkStatusNotFound     = TEXT("not_found");
	const TCHAR* const BulkStatusUpdated      = TEXT("updated");
	const TCHAR* const BulkStatusUnchanged    = TEXT("unchanged");
	const TCHAR* const BulkStatusPartial      = TEXT("partial");
	const TCHAR* const BulkStatusFailed       = TEXT("failed");
	const TCHAR* const BulkStatusSkipped      = TEXT("skipped");

	UObject* ResolveBulkPropertyTarget(UObject* Asset)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			if (UClass* GeneratedClass = Blueprint->GeneratedClass)
			{
				if (UObject* CDO = GeneratedClass->GetDefaultObject()) return CDO;
			}
		}
		return Asset;
	}

	struct FPreparedPropertyWrite
	{
		FString PropertyName;
		TSharedPtr<FJsonValue> RequestedValue;
		TSharedPtr<FJsonValue> PreviousValue;
		FString PreviousText;
		FString ProposedText;
		/** Non-empty once the property is rejected in preflight or fails to apply. */
		FString Error;
		bool bApplied = false;
		bool bChanged = false;
	};

	struct FPreparedAssetWrite
	{
		int32 ItemIndex = 0;
		FString AssetPath;
		UObject* Asset = nullptr;
		TArray<FPreparedPropertyWrite> Properties;
		/** Preflight verdict. Empty error means the item is eligible to apply. */
		FString Status;
		FString Error;
		bool PassedPreflight() const { return Error.IsEmpty(); }
	};

	// Mirrors IsProtectedAssetPath in AssetHandlers.cpp, which is file-local to
	// that translation unit. Keep the two rule sets identical: a write path that
	// enforces a weaker rule than its neighbours is how engine content gets
	// mutated through the bridge.
	bool IsProtectedBulkAssetPath(const FString& Path)
	{
		FString Normalized = Path;
		Normalized.TrimStartAndEndInline();
		if (Normalized.IsEmpty()) return false;
		if (!Normalized.StartsWith(TEXT("/"))) Normalized = TEXT("/") + Normalized;
		const FString Lower = Normalized.ToLower();
		if (Lower.StartsWith(TEXT("/engine/"))) return true;
		if (Lower.StartsWith(TEXT("/script/"))) return true;
		if (Lower.StartsWith(TEXT("/memory/"))) return true;
		if (Lower.StartsWith(TEXT("/temp/"))) return true;
		// Verse runtime objects surface as /Script/CoreUObject.* etc.
		if (Lower.Contains(TEXT("/script/"))) return true;
		return false;
	}

	TSharedPtr<FJsonObject> MakePropertyReadback(
		const FPreparedPropertyWrite& Prepared,
		const TSharedPtr<FJsonValue>& ActualValue,
		const FString& ActualText)
	{
		TSharedPtr<FJsonObject> Readback = MakeShared<FJsonObject>();
		Readback->SetStringField(TEXT("propertyName"), Prepared.PropertyName);
		Readback->SetBoolField(TEXT("ok"), Prepared.Error.IsEmpty());
		if (!Prepared.Error.IsEmpty())
		{
			Readback->SetStringField(TEXT("error"), Prepared.Error);
		}
		Readback->SetField(TEXT("previousValue"), Prepared.PreviousValue);
		Readback->SetField(TEXT("value"), ActualValue);
		Readback->SetStringField(TEXT("previousValueText"), Prepared.PreviousText);
		Readback->SetStringField(TEXT("valueText"), ActualText);
		Readback->SetBoolField(TEXT("changed"), Prepared.PreviousText != ActualText);
		return Readback;
	}

	/** Readback for a property that never got written (preflight reject, or the
	 *  whole item was skipped). Reports the reason instead of pretending a value. */
	TSharedPtr<FJsonObject> MakeUnwrittenReadback(const FPreparedPropertyWrite& Prepared)
	{
		TSharedPtr<FJsonObject> Readback = MakeShared<FJsonObject>();
		Readback->SetStringField(TEXT("propertyName"), Prepared.PropertyName);
		Readback->SetBoolField(TEXT("ok"), false);
		Readback->SetStringField(TEXT("error"), Prepared.Error);
		Readback->SetBoolField(TEXT("changed"), false);
		return Readback;
	}
}

// ---------------------------------------------------------------------------
// bulk_set_asset_properties -- the batch counterpart to set_asset_property.
//
// Every submitted item is preflighted (load the asset, resolve each dotted
// path, deserialize each value into a scratch copy) and every item gets a
// status back, including the ones that were rejected. A bad path in item 300
// no longer hides the other 499 verdicts behind a single error string.
//
// By default a failed preflight aborts before any UObject is touched, so the
// batch stays all-or-nothing. Pass continueOnError to apply the items that did
// pass and keep the rejects reported alongside them. The apply pass never
// early-returns: a mid-batch failure is recorded on its item and the remaining
// items still run, so the response always describes what actually landed.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::BulkSetAssetProperties(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Params->TryGetArrayField(TEXT("items"), Items) || !Items)
	{
		return MCPError(TEXT("Missing 'items' array"));
	}
	if (Items->Num() == 0)
	{
		return MCPError(TEXT("'items' must contain at least one asset update"));
	}
	if (Items->Num() > MaxBulkPropertyAssets)
	{
		return MCPError(FString::Printf(
			TEXT("'items' exceeds the maximum batch size of %d (received %d)"),
			MaxBulkPropertyAssets, Items->Num()));
	}

	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);
	bool bDryRun = false;
	Params->TryGetBoolField(TEXT("dryRun"), bDryRun);
	bool bContinueOnError = false;
	Params->TryGetBoolField(TEXT("continueOnError"), bContinueOnError);

	TArray<FPreparedAssetWrite> PreparedAssets;
	PreparedAssets.Reserve(Items->Num());
	TSet<FString> SeenAssetPaths;
	int32 RequestedPropertyCount = 0;
	int32 PreflightFailedCount = 0;
	FString FirstPreflightError;

	// Preflight: validate every descriptor, load every target, resolve every
	// dotted path, and deserialize every proposed value into temporary property
	// storage. No UObject is modified in this pass. Rejections are recorded on
	// the item rather than returned, so the caller sees all of them at once.
	for (int32 ItemIndex = 0; ItemIndex < Items->Num(); ++ItemIndex)
	{
		FPreparedAssetWrite PreparedAsset;
		PreparedAsset.ItemIndex = ItemIndex;

		auto RejectItem = [&PreparedAsset, &PreflightFailedCount, &FirstPreflightError]
			(const TCHAR* Status, const FString& Message)
		{
			PreparedAsset.Status = Status;
			PreparedAsset.Error = Message;
			++PreflightFailedCount;
			if (FirstPreflightError.IsEmpty()) FirstPreflightError = Message;
		};

		const TSharedPtr<FJsonObject>* ItemObject = nullptr;
		if (!(*Items)[ItemIndex].IsValid() || !(*Items)[ItemIndex]->TryGetObject(ItemObject) || !ItemObject || !(*ItemObject).IsValid())
		{
			RejectItem(BulkStatusInvalid, FString::Printf(TEXT("items[%d] must be an object"), ItemIndex));
			PreparedAssets.Add(MoveTemp(PreparedAsset));
			continue;
		}

		FString AssetPath;
		if (!(*ItemObject)->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			RejectItem(BulkStatusInvalid, FString::Printf(TEXT("items[%d].assetPath must be a non-empty string"), ItemIndex));
			PreparedAssets.Add(MoveTemp(PreparedAsset));
			continue;
		}
		PreparedAsset.AssetPath = AssetPath;

		if (IsProtectedBulkAssetPath(AssetPath))
		{
			RejectItem(BulkStatusProtected, FString::Printf(
				TEXT("Refusing to mutate protected mount: %s. Engine, /Script/, /Memory/, /Temp/ are read-only via the bridge."),
				*AssetPath));
			PreparedAssets.Add(MoveTemp(PreparedAsset));
			continue;
		}
		if (SeenAssetPaths.Contains(AssetPath))
		{
			RejectItem(BulkStatusDuplicate, FString::Printf(
				TEXT("Duplicate assetPath in batch: %s. Merge the properties into a single item so the write order is unambiguous."),
				*AssetPath));
			PreparedAssets.Add(MoveTemp(PreparedAsset));
			continue;
		}
		SeenAssetPaths.Add(AssetPath);

		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		if (!(*ItemObject)->TryGetObjectField(TEXT("properties"), PropertiesObject)
			|| !PropertiesObject || !(*PropertiesObject).IsValid() || (*PropertiesObject)->Values.Num() == 0)
		{
			RejectItem(BulkStatusInvalid, FString::Printf(TEXT("items[%d].properties must be a non-empty object"), ItemIndex));
			PreparedAssets.Add(MoveTemp(PreparedAsset));
			continue;
		}

		UObject* LoadedAsset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!LoadedAsset)
		{
			RejectItem(BulkStatusNotFound, FString::Printf(TEXT("Preflight failed: could not load asset '%s'"), *AssetPath));
			PreparedAssets.Add(MoveTemp(PreparedAsset));
			continue;
		}
		PreparedAsset.Asset = ResolveBulkPropertyTarget(LoadedAsset);
		PreparedAsset.Properties.Reserve((*PropertiesObject)->Values.Num());

		FString ItemPropertyError;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertiesObject)->Values)
		{
			FPreparedPropertyWrite PreparedProperty;
			PreparedProperty.PropertyName = Pair.Key;
			PreparedProperty.RequestedValue = Pair.Value;
			++RequestedPropertyCount;

			if (Pair.Key.IsEmpty() || !Pair.Value.IsValid())
			{
				PreparedProperty.Error = FString::Printf(
					TEXT("Preflight failed for '%s': property names and values must be non-empty"), *AssetPath);
			}
			else
			{
				FProperty* Property = nullptr;
				void* ValueAddress = nullptr;
				UObject* LeafOwner = nullptr;
				FString ResolveError;
				if (!MCPJsonProperty::ResolveDottedPath(PreparedAsset.Asset, Pair.Key, Property, ValueAddress, LeafOwner, ResolveError))
				{
					PreparedProperty.Error = FString::Printf(
						TEXT("Preflight failed for '%s.%s': %s"), *AssetPath, *Pair.Key, *ResolveError);
				}
				else
				{
					// Dry-fit the value against a scratch copy of the property so a
					// bad payload is caught without touching the live asset.
					void* TemporaryValue = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
					Property->InitializeValue(TemporaryValue);
					Property->CopyCompleteValue(TemporaryValue, ValueAddress);
					FString SetError;
					const bool bValidValue = MCPJsonProperty::SetJsonOnProperty(Property, TemporaryValue, Pair.Value, SetError);
					FString ProposedText;
					if (bValidValue)
					{
						Property->ExportText_Direct(ProposedText, TemporaryValue, TemporaryValue, LeafOwner, PPF_None);
					}
					Property->DestroyValue(TemporaryValue);
					FMemory::Free(TemporaryValue);

					if (bValidValue)
					{
						PreparedProperty.PreviousValue = FMCPJsonSerializer::SerializeValue(ValueAddress, Property);
						Property->ExportText_Direct(PreparedProperty.PreviousText, ValueAddress, ValueAddress, LeafOwner, PPF_None);
						PreparedProperty.ProposedText = MoveTemp(ProposedText);
					}
					else
					{
						PreparedProperty.Error = FString::Printf(
							TEXT("Preflight failed for '%s.%s': %s"), *AssetPath, *Pair.Key, *SetError);
					}
				}
			}

			if (!PreparedProperty.Error.IsEmpty() && ItemPropertyError.IsEmpty())
			{
				ItemPropertyError = PreparedProperty.Error;
			}
			PreparedAsset.Properties.Add(MoveTemp(PreparedProperty));
		}

		if (!ItemPropertyError.IsEmpty())
		{
			RejectItem(BulkStatusInvalid, ItemPropertyError);
		}
		PreparedAssets.Add(MoveTemp(PreparedAsset));
	}

	// All-or-nothing default: report every verdict, mutate nothing.
	if (PreflightFailedCount > 0 && !bContinueOnError)
	{
		TArray<TSharedPtr<FJsonValue>> RejectResults;
		RejectResults.Reserve(PreparedAssets.Num());
		for (const FPreparedAssetWrite& PreparedAsset : PreparedAssets)
		{
			TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
			ItemResult->SetNumberField(TEXT("index"), PreparedAsset.ItemIndex);
			ItemResult->SetStringField(TEXT("assetPath"), PreparedAsset.AssetPath);
			const bool bPassed = PreparedAsset.PassedPreflight();
			ItemResult->SetBoolField(TEXT("ok"), bPassed);
			ItemResult->SetStringField(TEXT("status"), bPassed ? BulkStatusSkipped : *PreparedAsset.Status);
			if (!bPassed) ItemResult->SetStringField(TEXT("error"), PreparedAsset.Error);
			ItemResult->SetBoolField(TEXT("changed"), false);
			ItemResult->SetBoolField(TEXT("saved"), false);

			TArray<TSharedPtr<FJsonValue>> PropertyResults;
			for (const FPreparedPropertyWrite& PreparedProperty : PreparedAsset.Properties)
			{
				PropertyResults.Add(MakeShared<FJsonValueObject>(PreparedProperty.Error.IsEmpty()
					? MakePropertyReadback(PreparedProperty, PreparedProperty.RequestedValue, PreparedProperty.ProposedText)
					: MakeUnwrittenReadback(PreparedProperty)));
			}
			ItemResult->SetArrayField(TEXT("properties"), PropertyResults);
			RejectResults.Add(MakeShared<FJsonValueObject>(ItemResult));
		}

		auto Rejected = MCPSuccess();
		Rejected->SetBoolField(TEXT("success"), false);
		Rejected->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Preflight failed for %d of %d items; no assets were modified. First failure: %s. Pass continueOnError=true to apply the items that did pass."),
			PreflightFailedCount, PreparedAssets.Num(), *FirstPreflightError));
		Rejected->SetBoolField(TEXT("dryRun"), bDryRun);
		Rejected->SetBoolField(TEXT("continueOnError"), false);
		Rejected->SetBoolField(TEXT("preflightPassed"), false);
		Rejected->SetNumberField(TEXT("requestedAssetCount"), PreparedAssets.Num());
		Rejected->SetNumberField(TEXT("requestedPropertyCount"), RequestedPropertyCount);
		Rejected->SetNumberField(TEXT("preflightFailedCount"), PreflightFailedCount);
		Rejected->SetNumberField(TEXT("updatedAssetCount"), 0);
		Rejected->SetNumberField(TEXT("updatedPropertyCount"), 0);
		Rejected->SetNumberField(TEXT("savedAssetCount"), 0);
		Rejected->SetNumberField(TEXT("saveFailedCount"), 0);
		Rejected->SetNumberField(TEXT("skippedAssetCount"), PreparedAssets.Num() - PreflightFailedCount);
		Rejected->SetArrayField(TEXT("items"), RejectResults);
		return MCPResult(Rejected);
	}

	TArray<TSharedPtr<FJsonValue>> ItemResults;
	TArray<TSharedPtr<FJsonValue>> RollbackItems;
	ItemResults.Reserve(PreparedAssets.Num());
	RollbackItems.Reserve(PreparedAssets.Num());
	int32 UpdatedAssetCount = 0;
	int32 UpdatedPropertyCount = 0;
	int32 SavedAssetCount = 0;
	int32 SaveFailedCount = 0;
	int32 FailedAssetCount = 0;
	int32 FailedPropertyCount = 0;

	for (FPreparedAssetWrite& PreparedAsset : PreparedAssets)
	{
		TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetNumberField(TEXT("index"), PreparedAsset.ItemIndex);
		ItemResult->SetStringField(TEXT("assetPath"), PreparedAsset.AssetPath);

		// Rejected in preflight and continueOnError is on: report it, skip it,
		// never drop it silently.
		if (!PreparedAsset.PassedPreflight())
		{
			TArray<TSharedPtr<FJsonValue>> PropertyResults;
			for (const FPreparedPropertyWrite& PreparedProperty : PreparedAsset.Properties)
			{
				PropertyResults.Add(MakeShared<FJsonValueObject>(PreparedProperty.Error.IsEmpty()
					? MakePropertyReadback(PreparedProperty, PreparedProperty.RequestedValue, PreparedProperty.ProposedText)
					: MakeUnwrittenReadback(PreparedProperty)));
			}
			ItemResult->SetBoolField(TEXT("ok"), false);
			ItemResult->SetStringField(TEXT("status"), PreparedAsset.Status);
			ItemResult->SetStringField(TEXT("error"), PreparedAsset.Error);
			ItemResult->SetBoolField(TEXT("changed"), false);
			ItemResult->SetBoolField(TEXT("wouldChange"), false);
			ItemResult->SetBoolField(TEXT("saved"), false);
			ItemResult->SetArrayField(TEXT("properties"), PropertyResults);
			ItemResults.Add(MakeShared<FJsonValueObject>(ItemResult));
			++FailedAssetCount;
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> PropertyResults;
		TSharedPtr<FJsonObject> RollbackProperties = MakeShared<FJsonObject>();
		bool bAssetChanged = false;
		bool bAssetModified = false;
		FString FirstApplyError;
		int32 ItemFailedProperties = 0;

		for (FPreparedPropertyWrite& PreparedProperty : PreparedAsset.Properties)
		{
			if (bDryRun)
			{
				PropertyResults.Add(MakeShared<FJsonValueObject>(MakePropertyReadback(
					PreparedProperty, PreparedProperty.RequestedValue, PreparedProperty.ProposedText)));
				continue;
			}

			FProperty* Property = nullptr;
			void* ValueAddress = nullptr;
			UObject* LeafOwner = nullptr;
			FString ResolveError;
			if (!MCPJsonProperty::ResolveDottedPath(PreparedAsset.Asset, PreparedProperty.PropertyName, Property, ValueAddress, LeafOwner, ResolveError))
			{
				PreparedProperty.Error = FString::Printf(TEXT("Apply failed after preflight for '%s.%s': %s"),
					*PreparedAsset.AssetPath, *PreparedProperty.PropertyName, *ResolveError);
			}
			else
			{
				if (!bAssetModified)
				{
					PreparedAsset.Asset->Modify();
					bAssetModified = true;
				}
				if (LeafOwner && LeafOwner != PreparedAsset.Asset) LeafOwner->Modify();

				FString SetError;
				if (!MCPJsonProperty::SetJsonOnProperty(Property, ValueAddress, PreparedProperty.RequestedValue, SetError))
				{
					PreparedProperty.Error = FString::Printf(TEXT("Apply failed after preflight for '%s.%s': %s"),
						*PreparedAsset.AssetPath, *PreparedProperty.PropertyName, *SetError);
				}
				else
				{
					if (LeafOwner) LeafOwner->PostEditChange();

					FString ActualText;
					Property->ExportText_Direct(ActualText, ValueAddress, ValueAddress, LeafOwner, PPF_None);
					PreparedProperty.bApplied = true;
					PreparedProperty.bChanged = PreparedProperty.PreviousText != ActualText;
					bAssetChanged |= PreparedProperty.bChanged;
					if (PreparedProperty.bChanged) ++UpdatedPropertyCount;
					// Only properties that actually landed belong in the rollback
					// payload; replaying a value we never wrote would corrupt state.
					RollbackProperties->SetField(PreparedProperty.PropertyName, PreparedProperty.PreviousValue);
					PropertyResults.Add(MakeShared<FJsonValueObject>(MakePropertyReadback(
						PreparedProperty, FMCPJsonSerializer::SerializeValue(ValueAddress, Property), ActualText)));
					continue;
				}
			}

			// Reached only on an apply failure. Record it and keep going so the
			// rest of the batch still runs and still reports.
			++ItemFailedProperties;
			++FailedPropertyCount;
			if (FirstApplyError.IsEmpty()) FirstApplyError = PreparedProperty.Error;
			PropertyResults.Add(MakeShared<FJsonValueObject>(MakeUnwrittenReadback(PreparedProperty)));
		}

		if (!bDryRun && bAssetChanged)
		{
			PreparedAsset.Asset->PostEditChange();
			PreparedAsset.Asset->MarkPackageDirty();
			++UpdatedAssetCount;
		}

		bool bSaved = false;
		if (!bDryRun && bSave && bAssetChanged)
		{
			bSaved = UEditorAssetLibrary::SaveAsset(PreparedAsset.AssetPath, false);
			if (bSaved) ++SavedAssetCount;
			else ++SaveFailedCount;
		}

		const bool bItemOk = ItemFailedProperties == 0;
		const TCHAR* ItemStatus = BulkStatusUnchanged;
		if (bDryRun)              ItemStatus = BulkStatusOk;
		else if (!bItemOk)        ItemStatus = bAssetChanged ? BulkStatusPartial : BulkStatusFailed;
		else if (bAssetChanged)   ItemStatus = BulkStatusUpdated;

		if (!bItemOk) ++FailedAssetCount;

		ItemResult->SetBoolField(TEXT("ok"), bItemOk);
		ItemResult->SetStringField(TEXT("status"), ItemStatus);
		if (!bItemOk) ItemResult->SetStringField(TEXT("error"), FirstApplyError);
		ItemResult->SetNumberField(TEXT("failedPropertyCount"), ItemFailedProperties);
		ItemResult->SetBoolField(TEXT("changed"), !bDryRun && bAssetChanged);
		ItemResult->SetBoolField(TEXT("wouldChange"), bDryRun && PreparedAsset.Properties.ContainsByPredicate(
			[](const FPreparedPropertyWrite& Property) { return Property.PreviousText != Property.ProposedText; }));
		ItemResult->SetBoolField(TEXT("saved"), bSaved);
		ItemResult->SetArrayField(TEXT("properties"), PropertyResults);
		ItemResults.Add(MakeShared<FJsonValueObject>(ItemResult));

		if (RollbackProperties->Values.Num() > 0)
		{
			TSharedPtr<FJsonObject> RollbackItem = MakeShared<FJsonObject>();
			RollbackItem->SetStringField(TEXT("assetPath"), PreparedAsset.AssetPath);
			RollbackItem->SetObjectField(TEXT("properties"), RollbackProperties);
			RollbackItems.Add(MakeShared<FJsonValueObject>(RollbackItem));
		}
	}

	const bool bAllOk = FailedAssetCount == 0;

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("success"), bAllOk);
	if (!bAllOk)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d items failed; see items[] for the per-item status."),
			FailedAssetCount, PreparedAssets.Num()));
	}
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("continueOnError"), bContinueOnError);
	Result->SetBoolField(TEXT("preflightPassed"), PreflightFailedCount == 0);
	Result->SetNumberField(TEXT("requestedAssetCount"), PreparedAssets.Num());
	Result->SetNumberField(TEXT("requestedPropertyCount"), RequestedPropertyCount);
	Result->SetNumberField(TEXT("preflightFailedCount"), PreflightFailedCount);
	Result->SetNumberField(TEXT("updatedAssetCount"), UpdatedAssetCount);
	Result->SetNumberField(TEXT("updatedPropertyCount"), UpdatedPropertyCount);
	Result->SetNumberField(TEXT("failedAssetCount"), FailedAssetCount);
	Result->SetNumberField(TEXT("failedPropertyCount"), FailedPropertyCount);
	Result->SetNumberField(TEXT("savedAssetCount"), SavedAssetCount);
	Result->SetNumberField(TEXT("saveFailedCount"), SaveFailedCount);
	Result->SetArrayField(TEXT("items"), ItemResults);
	if (!bDryRun)
	{
		if (UpdatedAssetCount > 0) MCPSetUpdated(Result);
		if (RollbackItems.Num() > 0)
		{
			TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
			RollbackPayload->SetArrayField(TEXT("items"), RollbackItems);
			RollbackPayload->SetBoolField(TEXT("save"), bSave);
			RollbackPayload->SetBoolField(TEXT("dryRun"), false);
			RollbackPayload->SetBoolField(TEXT("continueOnError"), true);
			MCPSetRollback(Result, TEXT("bulk_set_asset_properties"), RollbackPayload);
		}
	}
	return MCPResult(Result);
}
