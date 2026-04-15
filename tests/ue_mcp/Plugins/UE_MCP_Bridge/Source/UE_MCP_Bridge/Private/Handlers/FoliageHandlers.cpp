#include "FoliageHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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
	Registry.RegisterHandler(TEXT("get_foliage_settings"), &GetFoliageSettings);
	Registry.RegisterHandler(TEXT("paint_foliage"), &PaintFoliage);
	Registry.RegisterHandler(TEXT("erase_foliage"), &EraseFoliage);
	Registry.RegisterHandler(TEXT("sample_foliage_instances"), &SampleFoliageInstances);
	Registry.RegisterHandler(TEXT("create_foliage_layer"), &CreateFoliageLayer);
	Registry.RegisterHandler(TEXT("get_foliage_type_settings"), &GetFoliageSettings);
	Registry.RegisterHandler(TEXT("set_foliage_type_settings"), &SetFoliageTypeSettings);
	Registry.RegisterHandler(TEXT("create_foliage_type"), &CreateFoliageType);
}

TSharedPtr<FJsonValue> FFoliageHandlers::ListFoliageTypes(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> FoliageTypesArray;

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

			FoliageTypesArray.Add(MakeShared<FJsonValueObject>(TypeObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("foliageTypes"), FoliageTypesArray);
	Result->SetNumberField(TEXT("count"), FoliageTypesArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SampleFoliage(const TSharedPtr<FJsonObject>& Params)
{
	// Parse center point
	const TSharedPtr<FJsonObject>* CenterObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("center"), CenterObj) || !CenterObj || !(*CenterObj).IsValid())
	{
		return MCPError(TEXT("Missing 'center' parameter (object with x, y, z)"));
	}

	double CenterX = 0, CenterY = 0, CenterZ = 0;
	(*CenterObj)->TryGetNumberField(TEXT("x"), CenterX);
	(*CenterObj)->TryGetNumberField(TEXT("y"), CenterY);
	(*CenterObj)->TryGetNumberField(TEXT("z"), CenterZ);
	FVector Center(CenterX, CenterY, CenterZ);

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

	TSharedPtr<FJsonObject> CenterResult = MakeShared<FJsonObject>();
	CenterResult->SetNumberField(TEXT("x"), CenterX);
	CenterResult->SetNumberField(TEXT("y"), CenterY);
	CenterResult->SetNumberField(TEXT("z"), CenterZ);

	Result->SetObjectField(TEXT("center"), CenterResult);
	Result->SetNumberField(TEXT("radius"), Radius);
	Result->SetNumberField(TEXT("totalCount"), TotalCount);
	Result->SetArrayField(TEXT("types"), TypesArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::GetFoliageSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString FoliageTypePath;
	if (auto Err = RequireString(Params, TEXT("foliageTypePath"), FoliageTypePath)) return Err;

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

TSharedPtr<FJsonValue> FFoliageHandlers::PaintFoliage(const TSharedPtr<FJsonObject>& Params)
{
	FString FoliageTypePath;
	if (auto Err = RequireString(Params, TEXT("foliageType"), FoliageTypePath)) return Err;

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj || !(*LocationObj).IsValid())
	{
		return MCPError(TEXT("Missing 'location' parameter (object with x, y, z)"));
	}

	double LocX = 0, LocY = 0, LocZ = 0;
	(*LocationObj)->TryGetNumberField(TEXT("x"), LocX);
	(*LocationObj)->TryGetNumberField(TEXT("y"), LocY);
	(*LocationObj)->TryGetNumberField(TEXT("z"), LocZ);

	double PaintRadius = OptionalNumber(Params, TEXT("radius"), 500.0);
	double PaintDensity = OptionalNumber(Params, TEXT("density"), 100.0);

	// Foliage painting is not directly exposed as a C++ editor API.
	// The FoliageEdMode / FoliageEditorLibrary is internal to the foliage editor mode
	// and cannot be easily called programmatically from a plugin.
	// Use execute_python as a fallback to invoke the Python FoliageEditorLibrary if available.
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("foliageType"), FoliageTypePath);

	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	LocationResult->SetNumberField(TEXT("x"), LocX);
	LocationResult->SetNumberField(TEXT("y"), LocY);
	LocationResult->SetNumberField(TEXT("z"), LocZ);
	Result->SetObjectField(TEXT("location"), LocationResult);
	Result->SetNumberField(TEXT("radius"), PaintRadius);
	Result->SetNumberField(TEXT("density"), PaintDensity);

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("note"),
		TEXT("Foliage painting requires FoliageEdMode which is not accessible from C++ plugins. ")
		TEXT("Use the execute_python handler with unreal.FoliageEditorLibrary.paint_foliage() if available, ")
		TEXT("or manually paint in the editor foliage tool."));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::EraseFoliage(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj || !(*LocationObj).IsValid())
	{
		return MCPError(TEXT("Missing 'location' parameter (object with x, y, z)"));
	}

	double LocX = 0, LocY = 0, LocZ = 0;
	(*LocationObj)->TryGetNumberField(TEXT("x"), LocX);
	(*LocationObj)->TryGetNumberField(TEXT("y"), LocY);
	(*LocationObj)->TryGetNumberField(TEXT("z"), LocZ);

	double EraseRadius = OptionalNumber(Params, TEXT("radius"), 500.0);

	FString FoliageTypeFilter = OptionalString(Params, TEXT("foliageType"));

	// Foliage erasure, like painting, is internal to FoliageEdMode.
	// Provide the same execute_python fallback note.
	auto Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	LocationResult->SetNumberField(TEXT("x"), LocX);
	LocationResult->SetNumberField(TEXT("y"), LocY);
	LocationResult->SetNumberField(TEXT("z"), LocZ);
	Result->SetObjectField(TEXT("location"), LocationResult);
	Result->SetNumberField(TEXT("radius"), EraseRadius);

	if (!FoliageTypeFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("foliageTypeFilter"), FoliageTypeFilter);
	}

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("note"),
		TEXT("Foliage erasure requires FoliageEdMode which is not accessible from C++ plugins. ")
		TEXT("Use the execute_python handler with unreal.FoliageEditorLibrary.erase_foliage() if available, ")
		TEXT("or manually erase in the editor foliage tool."));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SampleFoliageInstances(const TSharedPtr<FJsonObject>& Params)
{
	// Parse center location
	const TSharedPtr<FJsonObject>* CenterObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("center"), CenterObj) || !CenterObj || !(*CenterObj).IsValid())
	{
		return MCPError(TEXT("Missing 'center' parameter (object with x, y, z)"));
	}

	double CenterX = 0, CenterY = 0, CenterZ = 0;
	(*CenterObj)->TryGetNumberField(TEXT("x"), CenterX);
	(*CenterObj)->TryGetNumberField(TEXT("y"), CenterY);
	(*CenterObj)->TryGetNumberField(TEXT("z"), CenterZ);
	FVector Center(CenterX, CenterY, CenterZ);

	double Radius = OptionalNumber(Params, TEXT("radius"), 1000.0);
	double RadiusSq = Radius * Radius;

	int32 Limit = 100;
	if (Params->HasField(TEXT("limit")))
	{
		Limit = static_cast<int32>(Params->GetNumberField(TEXT("limit")));
	}

	FString FoliageTypeFilter = OptionalString(Params, TEXT("foliageType"));

	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> InstancesArray;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* FoliageActor = *It;
		if (!FoliageActor) continue;
		if (InstancesArray.Num() >= Limit) break;

		const auto& FoliageInfoMap = FoliageActor->GetFoliageInfos();
		for (const auto& Pair : FoliageInfoMap)
		{
			if (InstancesArray.Num() >= Limit) break;

			UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;

			if (!FoliageType) continue;

			FString TypeName = FoliageType->GetName();

			// Apply foliage type filter if specified
			if (!FoliageTypeFilter.IsEmpty() && !TypeName.Contains(FoliageTypeFilter))
			{
				continue;
			}

			// Get the instanced static mesh component for mesh info
			FString MeshName = TEXT("Unknown");
			UFoliageType_InstancedStaticMesh* ISMType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
			if (ISMType && ISMType->Mesh)
			{
				MeshName = ISMType->Mesh->GetName();
			}

			// UE 5.7: Instances array is private; use the HISM component for transforms
			UHierarchicalInstancedStaticMeshComponent* HISMComp = FoliageInfo.GetComponent();
			if (!HISMComp) continue;

			int32 NumInstances = HISMComp->GetInstanceCount();
			for (int32 i = 0; i < NumInstances; ++i)
			{
				if (InstancesArray.Num() >= Limit) break;

				FTransform InstanceTransform;
				HISMComp->GetInstanceTransform(i, InstanceTransform, /*bWorldSpace=*/ true);
				FVector InstanceLocation = InstanceTransform.GetLocation();
				double DistSq = FVector::DistSquared(Center, InstanceLocation);

				if (DistSq <= RadiusSq)
				{
					double Distance = FMath::Sqrt(DistSq);

					TSharedPtr<FJsonObject> InstanceObj = MakeShared<FJsonObject>();
					InstanceObj->SetStringField(TEXT("foliageType"), TypeName);
					InstanceObj->SetStringField(TEXT("mesh"), MeshName);

					TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
					LocObj->SetNumberField(TEXT("x"), InstanceLocation.X);
					LocObj->SetNumberField(TEXT("y"), InstanceLocation.Y);
					LocObj->SetNumberField(TEXT("z"), InstanceLocation.Z);
					InstanceObj->SetObjectField(TEXT("location"), LocObj);

					InstanceObj->SetNumberField(TEXT("distance"), FMath::RoundToFloat(Distance * 10.0f) / 10.0f);

					// Extract rotation and scale from the instance transform
					FRotator InstanceRotation = InstanceTransform.Rotator();
					TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
					RotObj->SetNumberField(TEXT("pitch"), InstanceRotation.Pitch);
					RotObj->SetNumberField(TEXT("yaw"), InstanceRotation.Yaw);
					RotObj->SetNumberField(TEXT("roll"), InstanceRotation.Roll);
					InstanceObj->SetObjectField(TEXT("rotation"), RotObj);

					FVector InstanceScale = InstanceTransform.GetScale3D();
					TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
					ScaleObj->SetNumberField(TEXT("x"), InstanceScale.X);
					ScaleObj->SetNumberField(TEXT("y"), InstanceScale.Y);
					ScaleObj->SetNumberField(TEXT("z"), InstanceScale.Z);
					InstanceObj->SetObjectField(TEXT("scale"), ScaleObj);

					InstancesArray.Add(MakeShared<FJsonValueObject>(InstanceObj));
				}
			}
		}
	}

	auto Result = MCPSuccess();

	TSharedPtr<FJsonObject> CenterResult = MakeShared<FJsonObject>();
	CenterResult->SetNumberField(TEXT("x"), CenterX);
	CenterResult->SetNumberField(TEXT("y"), CenterY);
	CenterResult->SetNumberField(TEXT("z"), CenterZ);

	Result->SetObjectField(TEXT("center"), CenterResult);
	Result->SetNumberField(TEXT("radius"), Radius);
	Result->SetNumberField(TEXT("instanceCount"), InstancesArray.Num());
	Result->SetNumberField(TEXT("limit"), Limit);
	Result->SetArrayField(TEXT("instances"), InstancesArray);

	if (!FoliageTypeFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("foliageTypeFilter"), FoliageTypeFilter);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::CreateFoliageLayer(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetName;
	if (auto Err = RequireString(Params, TEXT("name"), AssetName)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Foliage"));
	FString MeshPath = OptionalString(Params, TEXT("meshPath"));
	FString AssetType = OptionalString(Params, TEXT("assetType"), TEXT("FoliageType"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Hit = MCPCheckAssetExists(PackagePath, AssetName, OnConflict, *AssetType))
	{
		return Hit;
	}

	FString FullPath = PackagePath / AssetName;

	auto Result = MakeShared<FJsonObject>();

	if (AssetType == TEXT("LandscapeGrassType"))
	{
		// Create a LandscapeGrassType asset
		FString PackageFullPath = PackagePath / AssetName;
		UPackage* Package = CreatePackage(*PackageFullPath);
		if (!Package)
		{
			return MCPError(FString::Printf(TEXT("Failed to create package: %s"), *PackageFullPath));
		}

		ULandscapeGrassType* GrassType = NewObject<ULandscapeGrassType>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!GrassType)
		{
			return MCPError(TEXT("Failed to create LandscapeGrassType object"));
		}

		// If a mesh path is provided, add it as a grass variety
		if (!MeshPath.IsEmpty())
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (Mesh)
			{
				FGrassVariety Variety;
				Variety.GrassMesh = Mesh;
				GrassType->GrassVarieties.Add(Variety);
			}
		}

		// Notify asset registry
		FAssetRegistryModule::AssetCreated(GrassType);
		Package->MarkPackageDirty();

		// Save the asset
		UEditorAssetLibrary::SaveAsset(PackageFullPath, false);

		Result->SetStringField(TEXT("path"), PackageFullPath);
		Result->SetStringField(TEXT("name"), GrassType->GetName());
		Result->SetStringField(TEXT("className"), GrassType->GetClass()->GetName());
		Result->SetStringField(TEXT("assetType"), TEXT("LandscapeGrassType"));
		if (!MeshPath.IsEmpty())
		{
			Result->SetStringField(TEXT("meshPath"), MeshPath);
		}
		Result->SetBoolField(TEXT("success"), true);
		MCPSetCreated(Result);
		MCPSetDeleteAssetRollback(Result, GrassType->GetPathName());
	}
	else
	{
		// Create a FoliageType_InstancedStaticMesh asset (default)
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

		// Set mesh if provided
		if (!MeshPath.IsEmpty())
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (Mesh)
			{
				FoliageType->Mesh = Mesh;
			}
		}

		// Notify asset registry
		FAssetRegistryModule::AssetCreated(FoliageType);
		Package->MarkPackageDirty();

		// Save the asset
		UEditorAssetLibrary::SaveAsset(PackageFullPath, false);

		Result->SetStringField(TEXT("path"), PackageFullPath);
		Result->SetStringField(TEXT("name"), FoliageType->GetName());
		Result->SetStringField(TEXT("className"), FoliageType->GetClass()->GetName());
		Result->SetStringField(TEXT("assetType"), TEXT("FoliageType"));
		if (!MeshPath.IsEmpty())
		{
			Result->SetStringField(TEXT("meshPath"), MeshPath);
		}
		Result->SetBoolField(TEXT("success"), true);
		MCPSetCreated(Result);
		MCPSetDeleteAssetRollback(Result, FoliageType->GetPathName());
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
		return MCPError(TEXT("Missing 'foliageTypePath' or 'foliageTypeName' parameter"));
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

	for (const auto& KV : (*SettingsObj)->Values)
	{
		FString PropertyName = KV.Key;
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
		const TCHAR* ImportResult = Property->ImportText_Direct(*PropertyValue, PropertyAddr, FoliageType, PPF_None);
		if (ImportResult == nullptr)
		{
			FailedSettings.Add(FString::Printf(TEXT("%s: failed to set value '%s'"), *PropertyName, *PropertyValue));
		}
		else
		{
			AppliedSettings.Add(PropertyName);
		}
	}

	// Mark the foliage type as dirty
	FoliageType->MarkPackageDirty();

	// Save the asset if it has a valid package path
	FString PackagePath = FoliageType->GetPathName();
	if (PackagePath.Contains(TEXT("/Game/")))
	{
		UEditorAssetLibrary::SaveAsset(FoliageType->GetOutermost()->GetName(), false);
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
			FString PropertyName = KV.Key;
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
