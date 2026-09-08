#include "FoliageHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "LandscapeGrassType.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

void FFoliageHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_foliage_types"), &ListFoliageTypes);
	Registry.RegisterHandler(TEXT("sample_foliage"), &SampleFoliage);
	Registry.RegisterHandler(TEXT("get_foliage_type_settings"), &GetFoliageSettings);
	Registry.RegisterHandler(TEXT("set_foliage_type_settings"), &SetFoliageTypeSettings);
	Registry.RegisterHandler(TEXT("create_foliage_type"), &CreateFoliageType);
	// #988: predicate-driven batch. Scanning and saving hundreds of FoliageType
	// assets outlives the default handler timeout.
	Registry.RegisterHandlerWithTimeout(
		TEXT("batch_set_foliage_settings_where"), &BatchSetFoliageSettingsWhere, 300.0f);

	// V12 depth (FoliageHandlers_Depth.cpp): the surface above could create and
	// configure a FoliageType and count instances inside a sphere, but could not
	// place one, remove one, say where any of them are, or put a type into the
	// level's palette at all.
	Registry.RegisterHandler(TEXT("add_foliage_instances"), &AddFoliageInstances);
	Registry.RegisterHandler(TEXT("remove_foliage_instances"), &RemoveFoliageInstances);
	Registry.RegisterHandler(TEXT("get_foliage_instances"), &GetFoliageInstances);
	Registry.RegisterHandler(TEXT("add_foliage_type_to_level"), &AddFoliageTypeToLevel);
	Registry.RegisterHandler(TEXT("remove_foliage_type_from_level"), &RemoveFoliageTypeFromLevel);
	Registry.RegisterHandler(TEXT("read_procedural_foliage_spawner"), &ReadProceduralFoliageSpawner);
	Registry.RegisterHandler(TEXT("set_procedural_foliage_spawner_types"), &SetProceduralFoliageSpawnerTypes);
	// A tile simulation over a large volume plus a world trace per generated
	// point outlives the default handler timeout on anything but a small volume.
	Registry.RegisterHandlerWithTimeout(
		TEXT("simulate_procedural_foliage"), &SimulateProceduralFoliage, 600.0f);
	Registry.RegisterHandler(TEXT("clear_procedural_foliage"), &ClearProceduralFoliage);
}

TSharedPtr<FJsonValue> FFoliageHandlers::ListFoliageTypes(const TSharedPtr<FJsonObject>& Params)
{
	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_foliage_types"), /*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	REQUIRE_EDITOR_WORLD(World);

	TArray<MCPPagination::FPageRow> Rows;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* FoliageActor = *It;
		if (!FoliageActor) continue;

		const auto& FoliageInfoMap = FoliageActor->GetFoliageInfos();
		for (const auto& Pair : FoliageInfoMap)
		{
			UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;

			if (!FoliageType) continue;

			TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
			TypeObj->SetStringField(TEXT("name"), FoliageType->GetName());
			TypeObj->SetStringField(TEXT("path"), FoliageType->GetPathName());

			// UE 5.7: Instances array is private; use the HISM component for instance count
			int32 InstanceCount = 0;
			UHierarchicalInstancedStaticMeshComponent* HISMComp = FoliageInfo.GetComponent();
			if (HISMComp)
			{
				InstanceCount = HISMComp->GetInstanceCount();
			}
			TypeObj->SetNumberField(TEXT("instanceCount"), InstanceCount);

			// Get source info
			TypeObj->SetStringField(TEXT("className"), FoliageType->GetClass()->GetName());
			// Which InstancedFoliageActor holds this type. One world holds one
			// per level or grid cell, and the same foliage type appears under
			// each, so the row says which one it came from.
			TypeObj->SetStringField(TEXT("foliageActorPath"), FoliageActor->GetPathName());

			// The page anchor is the owning actor plus the type, because the
			// type alone repeats across actors and an anchor has to name one
			// row.
			Rows.Add({
				FString::Printf(TEXT("%s|%s"), *FoliageActor->GetPathName(), *FoliageType->GetPathName()),
				MakeShared<FJsonValueObject>(TypeObj) });
		}
	}

	// TActorIterator order and TMap iteration order are both unspecified, so
	// the rows are sorted before paging. A cursor over an unordered enumeration
	// is not resumable.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	MCPPagination::EmitPage(Page, Rows, TEXT("foliageTypes"), Result);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SampleFoliage(const TSharedPtr<FJsonObject>& Params)
{
	FVector Center;
	if (auto Err = RequireVec3(Params, TEXT("center"), Center)) return Err;

	double Radius = OptionalNumber(Params, TEXT("radius"), 1000.0);
	double RadiusSq = Radius * Radius;

	REQUIRE_EDITOR_WORLD(World);

	TMap<FString, int32> TypeCounts;
	int32 TotalCount = 0;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* FoliageActor = *It;
		if (!FoliageActor) continue;

		const auto& FoliageInfoMap = FoliageActor->GetFoliageInfos();
		for (const auto& Pair : FoliageInfoMap)
		{
			UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;

			if (!FoliageType) continue;

			FString TypeName = FoliageType->GetName();
			int32 MatchCount = 0;

			// UE 5.7: Instances array is private; use the HISM component for transforms
			UHierarchicalInstancedStaticMeshComponent* HISMComp = FoliageInfo.GetComponent();
			if (HISMComp)
			{
				int32 NumInstances = HISMComp->GetInstanceCount();
				for (int32 i = 0; i < NumInstances; ++i)
				{
					FTransform InstanceTransform;
					HISMComp->GetInstanceTransform(i, InstanceTransform, /*bWorldSpace=*/ true);
					FVector InstanceLocation = InstanceTransform.GetLocation();
					double DistSq = FVector::DistSquared(Center, InstanceLocation);
					if (DistSq <= RadiusSq)
					{
						MatchCount++;
					}
				}
			}

			if (MatchCount > 0)
			{
				TypeCounts.FindOrAdd(TypeName) += MatchCount;
				TotalCount += MatchCount;
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	for (const auto& Pair : TypeCounts)
	{
		TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
		TypeObj->SetStringField(TEXT("type"), Pair.Key);
		TypeObj->SetNumberField(TEXT("count"), Pair.Value);
		TypesArray.Add(MakeShared<FJsonValueObject>(TypeObj));
	}

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("center"), MCPVec3ToJsonObject(Center));
	Result->SetNumberField(TEXT("radius"), Radius);
	Result->SetNumberField(TEXT("totalCount"), TotalCount);
	Result->SetArrayField(TEXT("types"), TypesArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::GetFoliageSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString FoliageTypePath;
	if (!Params->TryGetStringField(TEXT("foliageTypePath"), FoliageTypePath) || FoliageTypePath.IsEmpty())
	{
		// #988: a caller who reaches this error usually wants the whole set,
		// not one asset, so the error names the action that answers that.
		return MCPError(TEXT("Missing required parameter 'foliageTypePath'. To read or filter many types at once use foliage(list_types) or asset(bulk_read_properties); to WRITE settings to every type matching a predicate use foliage(batch_set_settings_where)."));
	}

	UFoliageType* FoliageType = LoadObject<UFoliageType>(nullptr, *FoliageTypePath);
	if (!FoliageType)
	{
		return MCPError(FString::Printf(TEXT("Foliage type not found: %s"), *FoliageTypePath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), FoliageTypePath);
	Result->SetStringField(TEXT("name"), FoliageType->GetName());
	Result->SetStringField(TEXT("className"), FoliageType->GetClass()->GetName());

	// Density settings
	Result->SetNumberField(TEXT("density"), FoliageType->Density);
	Result->SetNumberField(TEXT("densityAdjustmentFactor"), FoliageType->DensityAdjustmentFactor);
	Result->SetNumberField(TEXT("radius"), FoliageType->Radius);

	// Scaling settings
	TSharedPtr<FJsonObject> ScalingObj = MakeShared<FJsonObject>();
	ScalingObj->SetNumberField(TEXT("scaleMinX"), FoliageType->ScaleX.Min);
	ScalingObj->SetNumberField(TEXT("scaleMaxX"), FoliageType->ScaleX.Max);
	ScalingObj->SetNumberField(TEXT("scaleMinY"), FoliageType->ScaleY.Min);
	ScalingObj->SetNumberField(TEXT("scaleMaxY"), FoliageType->ScaleY.Max);
	ScalingObj->SetNumberField(TEXT("scaleMinZ"), FoliageType->ScaleZ.Min);
	ScalingObj->SetNumberField(TEXT("scaleMaxZ"), FoliageType->ScaleZ.Max);
	Result->SetObjectField(TEXT("scaling"), ScalingObj);

	// Placement settings
	Result->SetBoolField(TEXT("alignToNormal"), FoliageType->AlignToNormal);
	Result->SetNumberField(TEXT("alignMaxAngle"), FoliageType->AlignMaxAngle);
	Result->SetBoolField(TEXT("randomYaw"), FoliageType->RandomYaw);
	Result->SetNumberField(TEXT("randomPitchAngle"), FoliageType->RandomPitchAngle);
	Result->SetNumberField(TEXT("groundSlopeAngle"), FoliageType->GroundSlopeAngle.Max);

	// Height range
	Result->SetNumberField(TEXT("heightMin"), FoliageType->Height.Min);
	Result->SetNumberField(TEXT("heightMax"), FoliageType->Height.Max);

	// Collision settings
	TSharedPtr<FJsonObject> CollisionObj = MakeShared<FJsonObject>();
	CollisionObj->SetBoolField(TEXT("collisionWithWorld"), FoliageType->CollisionWithWorld);
	CollisionObj->SetNumberField(TEXT("collisionRadius"), FoliageType->CollisionRadius);
	CollisionObj->SetNumberField(TEXT("collisionScale"), FoliageType->CollisionScale.X);
	Result->SetObjectField(TEXT("collision"), CollisionObj);

	// LOD settings
	TSharedPtr<FJsonObject> LodObj = MakeShared<FJsonObject>();
	LodObj->SetNumberField(TEXT("cullDistanceMin"), FoliageType->CullDistance.Min);
	LodObj->SetNumberField(TEXT("cullDistanceMax"), FoliageType->CullDistance.Max);
	Result->SetObjectField(TEXT("lod"), LodObj);

	// Rendering settings
	Result->SetBoolField(TEXT("castShadow"), FoliageType->CastShadow);
	Result->SetBoolField(TEXT("receivesDecals"), FoliageType->bReceivesDecals);

	// Mesh reference (for InstancedStaticMesh types)
	UFoliageType_InstancedStaticMesh* ISMType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
	if (ISMType && ISMType->Mesh)
	{
		Result->SetStringField(TEXT("meshPath"), ISMType->Mesh->GetPathName());
		Result->SetStringField(TEXT("meshName"), ISMType->Mesh->GetName());
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SetFoliageTypeSettings(const TSharedPtr<FJsonObject>& Params)
{
	// Accept either foliageTypePath or foliageTypeName for lookup
	FString FoliageTypePath;
	if (!Params->TryGetStringField(TEXT("foliageTypePath"), FoliageTypePath))
	{
		Params->TryGetStringField(TEXT("foliageTypeName"), FoliageTypePath);
	}
	if (FoliageTypePath.IsEmpty())
	{
		return MCPError(TEXT("Missing 'foliageTypePath' or 'foliageTypeName' parameter. To write the same settings to every type matching a predicate on an existing value, use foliage(batch_set_settings_where) instead of calling this once per asset (#988)."));
	}

	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("settings"), SettingsObj) || !SettingsObj || !(*SettingsObj).IsValid())
	{
		return MCPError(TEXT("Missing 'settings' parameter (object with property name/value pairs)"));
	}

	// Try to load by path first; if not found, search by name in the world
	UFoliageType* FoliageType = LoadObject<UFoliageType>(nullptr, *FoliageTypePath);

	if (!FoliageType)
	{
		// Search by name in world foliage actors
		UWorld* World = GetEditorWorld();
		if (World)
		{
			for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
			{
				AInstancedFoliageActor* FoliageActor = *It;
				if (!FoliageActor) continue;

				const auto& FoliageInfoMap = FoliageActor->GetFoliageInfos();
				for (const auto& Pair : FoliageInfoMap)
				{
					if (Pair.Key && (Pair.Key->GetName() == FoliageTypePath || Pair.Key->GetPathName() == FoliageTypePath))
					{
						FoliageType = Pair.Key;
						break;
					}
				}
				if (FoliageType) break;
			}
		}
	}

	if (!FoliageType)
	{
		return MCPError(FString::Printf(TEXT("Foliage type not found: %s"), *FoliageTypePath));
	}

	// Apply settings via property reflection
	TArray<FString> AppliedSettings;
	TArray<FString> FailedSettings;
	// Settings whose property already held the requested value. Writing one of
	// those changed nothing, and a result that counted it as applied would
	// report a no-op replay as a state change.
	TArray<FString> UnchangedSettings;
	// The previous values, captured BEFORE each write and in the same text form
	// the settings parameter takes, so the record below is a call that restores
	// them. Reading the properties back afterwards would only ever recover the
	// values this call just installed.
	TSharedPtr<FJsonObject> PreviousSettings = MakeShared<FJsonObject>();
	int32 ChangedCount = 0;

	for (const auto& KV : (*SettingsObj)->Values)
	{
		FString PropertyName(*KV.Key);
		FString PropertyValue;

		// Convert the JSON value to a string for ImportText
		if (KV.Value->Type == EJson::String)
		{
			PropertyValue = KV.Value->AsString();
		}
		else if (KV.Value->Type == EJson::Number)
		{
			PropertyValue = FString::SanitizeFloat(KV.Value->AsNumber());
		}
		else if (KV.Value->Type == EJson::Boolean)
		{
			PropertyValue = KV.Value->AsBool() ? TEXT("True") : TEXT("False");
		}
		else
		{
			// For complex types, try serializing as string
			TSharedPtr<FJsonValue> Val = KV.Value;
			if (Val.IsValid())
			{
				PropertyValue = KV.Value->AsString();
			}
		}

		FProperty* Property = FoliageType->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Property)
		{
			FailedSettings.Add(FString::Printf(TEXT("%s: property not found"), *PropertyName));
			continue;
		}

		void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(FoliageType);
		// Exported with no default to compare against, so a value that happens
		// to equal the class default still comes back in full rather than as
		// the empty delta a struct property would otherwise write.
		FString OldText;
		Property->ExportText_Direct(OldText, PropertyAddr, nullptr, FoliageType, PPF_None);

		const TCHAR* ImportResult = Property->ImportText_Direct(*PropertyValue, PropertyAddr, FoliageType, PPF_None);
		if (ImportResult == nullptr)
		{
			FailedSettings.Add(FString::Printf(TEXT("%s: failed to set value '%s'"), *PropertyName, *PropertyValue));
		}
		else
		{
			AppliedSettings.Add(PropertyName);
			// Compare the exported forms rather than the caller's string: "1"
			// and "1.000000" are the same float, and a text comparison against
			// what was asked for would call that a change.
			FString NewText;
			Property->ExportText_Direct(NewText, PropertyAddr, nullptr, FoliageType, PPF_None);
			if (NewText == OldText)
			{
				UnchangedSettings.Add(PropertyName);
			}
			else
			{
				++ChangedCount;
				PreviousSettings->SetStringField(PropertyName, OldText);
			}
		}
	}

	// Dirty and save only when a value actually moved. A call that wrote the
	// values the asset already held has nothing to persist, and dirtying the
	// package anyway would hand the user an unsaved asset for a no-op.
	if (ChangedCount > 0)
	{
		// Mark the foliage type as dirty
		FoliageType->MarkPackageDirty();

		// Save the asset if it has a valid package path
		FString PackagePath = FoliageType->GetPathName();
		if (PackagePath.Contains(TEXT("/Game/")))
		{
			UEditorAssetLibrary::SaveAsset(FoliageType->GetOutermost()->GetName(), false);
		}
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("foliageType"), FoliageType->GetName());
	Result->SetStringField(TEXT("path"), FoliageType->GetPathName());

	TArray<TSharedPtr<FJsonValue>> AppliedArray;
	for (const FString& S : AppliedSettings)
	{
		AppliedArray.Add(MakeShared<FJsonValueString>(S));
	}
	Result->SetArrayField(TEXT("appliedSettings"), AppliedArray);

	if (FailedSettings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FString& S : FailedSettings)
		{
			FailedArray.Add(MakeShared<FJsonValueString>(S));
		}
		Result->SetArrayField(TEXT("failedSettings"), FailedArray);
	}

	if (UnchangedSettings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> UnchangedArray;
		for (const FString& S : UnchangedSettings)
		{
			UnchangedArray.Add(MakeShared<FJsonValueString>(S));
		}
		Result->SetArrayField(TEXT("unchangedSettings"), UnchangedArray);
	}

	if (ChangedCount > 0) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), ChangedCount == 0);
	Result->SetNumberField(TEXT("changedCount"), ChangedCount);

	if (ChangedCount > 0)
	{
		// The inverse is this same action with the values that were there
		// before. Only the properties that actually moved are listed: replaying
		// the ones that did not would be a second no-op write, and a property
		// whose import failed was never touched.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("foliageTypePath"), FoliageType->GetPathName());
		Payload->SetObjectField(TEXT("settings"), PreviousSettings);
		MCPSetRollback(Result, TEXT("set_foliage_type_settings"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT(
			"No property value moved, so nothing was written to the foliage type and there is nothing to undo. "
			"Anything listed in failedSettings was rejected before the write and never reached the asset."));
	}

	Result->SetBoolField(TEXT("success"), FailedSettings.Num() == 0);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::CreateFoliageType(const TSharedPtr<FJsonObject>& Params)
{
	FString MeshPath;
	if (auto Err = RequireString(Params, TEXT("meshPath"), MeshPath)) return Err;

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return MCPError(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));
	}

	FString AssetName = OptionalString(Params, TEXT("name"));
	if (AssetName.IsEmpty())
	{
		AssetName = FString::Printf(TEXT("FT_%s"), *Mesh->GetName());
	}

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Foliage"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, AssetName, OnConflict, TEXT("FoliageType")))
	{
		return Existing;
	}

	FString PackageFullPath = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*PackageFullPath);
	if (!Package)
	{
		return MCPError(FString::Printf(TEXT("Failed to create package: %s"), *PackageFullPath));
	}

	UFoliageType_InstancedStaticMesh* FoliageType = NewObject<UFoliageType_InstancedStaticMesh>(
		Package, *AssetName, RF_Public | RF_Standalone);
	if (!FoliageType)
	{
		return MCPError(TEXT("Failed to create FoliageType object"));
	}

	FoliageType->Mesh = Mesh;

	// Apply optional settings if provided
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid())
	{
		for (const auto& KV : (*SettingsObj)->Values)
		{
			FString PropertyName(*KV.Key);
			FString PropertyValue;

			if (KV.Value->Type == EJson::String)
			{
				PropertyValue = KV.Value->AsString();
			}
			else if (KV.Value->Type == EJson::Number)
			{
				PropertyValue = FString::SanitizeFloat(KV.Value->AsNumber());
			}
			else if (KV.Value->Type == EJson::Boolean)
			{
				PropertyValue = KV.Value->AsBool() ? TEXT("True") : TEXT("False");
			}

			FProperty* Property = FoliageType->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (Property)
			{
				void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(FoliageType);
				Property->ImportText_Direct(*PropertyValue, PropertyAddr, FoliageType, PPF_None);
			}
		}
	}

	// Notify asset registry and save
	FAssetRegistryModule::AssetCreated(FoliageType);
	Package->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(PackageFullPath, false);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), PackageFullPath);
	Result->SetStringField(TEXT("name"), FoliageType->GetName());
	Result->SetStringField(TEXT("className"), FoliageType->GetClass()->GetName());
	Result->SetStringField(TEXT("meshPath"), MeshPath);
	Result->SetStringField(TEXT("meshName"), Mesh->GetName());
	MCPSetDeleteAssetRollback(Result, FoliageType->GetPathName());

	return MCPResult(Result);
}
