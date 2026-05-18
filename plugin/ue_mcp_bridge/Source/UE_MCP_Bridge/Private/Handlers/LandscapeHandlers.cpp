#include "LandscapeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeEditTypes.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeSplineActor.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Materials/MaterialInterface.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

void FLandscapeHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_landscape_info"), &GetLandscapeInfo);
	Registry.RegisterHandler(TEXT("list_landscape_layers"), &ListLandscapeLayers);
	Registry.RegisterHandler(TEXT("sample_landscape"), &SampleLandscape);
	Registry.RegisterHandler(TEXT("list_landscape_splines"), &ListLandscapeSplines);
	Registry.RegisterHandler(TEXT("get_landscape_component"), &GetLandscapeComponent);
	Registry.RegisterHandler(TEXT("set_landscape_material"), &SetLandscapeMaterial);
	Registry.RegisterHandler(TEXT("add_landscape_layer_info"), &AddLandscapeLayerInfo);
	Registry.RegisterHandler(TEXT("create_landscape"), &CreateLandscape);
	Registry.RegisterHandler(TEXT("create_landscape_layer_info"), &CreateLandscapeLayerInfo);
	Registry.RegisterHandler(TEXT("get_landscape_material_usage_summary"), &GetMaterialUsageSummary);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeInfo(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// Find landscape proxies in the world
	TArray<TSharedPtr<FJsonValue>> LandscapeArray;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		TSharedPtr<FJsonObject> LandscapeObj = MakeShared<FJsonObject>();
		LandscapeObj->SetStringField(TEXT("name"), Landscape->GetName());
		LandscapeObj->SetStringField(TEXT("class"), Landscape->GetClass()->GetName());

		// Get component count
		TArray<ULandscapeComponent*> LandscapeComponents;
		Landscape->GetComponents<ULandscapeComponent>(LandscapeComponents);
		LandscapeObj->SetNumberField(TEXT("componentCount"), LandscapeComponents.Num());

		// Get bounds
		FBox Bounds = Landscape->GetComponentsBoundingBox();
		if (Bounds.IsValid)
		{
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("minX"), Bounds.Min.X);
			BoundsObj->SetNumberField(TEXT("minY"), Bounds.Min.Y);
			BoundsObj->SetNumberField(TEXT("minZ"), Bounds.Min.Z);
			BoundsObj->SetNumberField(TEXT("maxX"), Bounds.Max.X);
			BoundsObj->SetNumberField(TEXT("maxY"), Bounds.Max.Y);
			BoundsObj->SetNumberField(TEXT("maxZ"), Bounds.Max.Z);

			FVector Size = Bounds.GetSize();
			BoundsObj->SetNumberField(TEXT("sizeX"), Size.X);
			BoundsObj->SetNumberField(TEXT("sizeY"), Size.Y);
			BoundsObj->SetNumberField(TEXT("sizeZ"), Size.Z);
			LandscapeObj->SetObjectField(TEXT("bounds"), BoundsObj);
		}

		// Get location
		FVector Location = Landscape->GetActorLocation();
		LandscapeObj->SetNumberField(TEXT("locationX"), Location.X);
		LandscapeObj->SetNumberField(TEXT("locationY"), Location.Y);
		LandscapeObj->SetNumberField(TEXT("locationZ"), Location.Z);

		LandscapeArray.Add(MakeShared<FJsonValueObject>(LandscapeObj));
	}

	auto Result = MCPSuccess();
	if (LandscapeArray.Num() == 0)
	{
		Result->SetStringField(TEXT("landscape"), TEXT("none"));
	}
	else
	{
		Result->SetArrayField(TEXT("landscapes"), LandscapeArray);
	}

	Result->SetNumberField(TEXT("count"), LandscapeArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeLayers(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> LayerArray;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
		if (LandscapeInfo)
		{
			for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
			{
				if (LayerSettings.LayerInfoObj)
				{
					TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
					LayerObj->SetStringField(TEXT("name"), LayerSettings.GetLayerName().ToString());
					LayerObj->SetStringField(TEXT("landscapeName"), Landscape->GetName());
					LayerArray.Add(MakeShared<FJsonValueObject>(LayerObj));
				}
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("layers"), LayerArray);
	Result->SetNumberField(TEXT("count"), LayerArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SampleLandscape(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* PointObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("point"), PointObj))
	{
		return MCPError(TEXT("Missing 'point' parameter"));
	}

	FVector Point;
	Point.X = (*PointObj)->GetNumberField(TEXT("x"));
	Point.Y = (*PointObj)->GetNumberField(TEXT("y"));
	Point.Z = (*PointObj)->GetNumberField(TEXT("z"));

	REQUIRE_EDITOR_WORLD(World);

	// Find the first landscape and sample height
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		// Use line trace to get the landscape height at the given point
		FVector TraceStart(Point.X, Point.Y, Point.Z + 100000.0f);
		FVector TraceEnd(Point.X, Point.Y, Point.Z - 100000.0f);

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.bTraceComplex = true;

		if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
		{
			if (HitResult.GetActor() && HitResult.GetActor()->IsA(ALandscapeProxy::StaticClass()))
			{
				auto Result = MCPSuccess();
				Result->SetNumberField(TEXT("height"), HitResult.Location.Z);
				TSharedPtr<FJsonObject> HitPoint = MakeShared<FJsonObject>();
				HitPoint->SetNumberField(TEXT("x"), HitResult.Location.X);
				HitPoint->SetNumberField(TEXT("y"), HitResult.Location.Y);
				HitPoint->SetNumberField(TEXT("z"), HitResult.Location.Z);
				Result->SetObjectField(TEXT("hitLocation"), HitPoint);

				TSharedPtr<FJsonObject> Normal = MakeShared<FJsonObject>();
				Normal->SetNumberField(TEXT("x"), HitResult.Normal.X);
				Normal->SetNumberField(TEXT("y"), HitResult.Normal.Y);
				Normal->SetNumberField(TEXT("z"), HitResult.Normal.Z);
				Result->SetObjectField(TEXT("normal"), Normal);

				Result->SetBoolField(TEXT("hit"), true);
				return MCPResult(Result);
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hit"), false);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeSplines(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> SplineArray;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		ULandscapeSplinesComponent* SplinesComp = Landscape->GetSplinesComponent();
		if (!SplinesComp) continue;

		const TArray<TObjectPtr<ULandscapeSplineControlPoint>>& ControlPoints = SplinesComp->GetControlPoints();
		for (const TObjectPtr<ULandscapeSplineControlPoint>& CP : ControlPoints)
		{
			if (!CP) continue;

			TSharedPtr<FJsonObject> PointObj = MakeShared<FJsonObject>();
			FVector Location = CP->Location;
			PointObj->SetNumberField(TEXT("x"), Location.X);
			PointObj->SetNumberField(TEXT("y"), Location.Y);
			PointObj->SetNumberField(TEXT("z"), Location.Z);
			PointObj->SetStringField(TEXT("landscapeName"), Landscape->GetName());
			SplineArray.Add(MakeShared<FJsonValueObject>(PointObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("controlPoints"), SplineArray);
	Result->SetNumberField(TEXT("count"), SplineArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeComponent(const TSharedPtr<FJsonObject>& Params)
{
	int32 ComponentIndex = 0;
	if (Params->HasField(TEXT("componentIndex")))
	{
		ComponentIndex = static_cast<int32>(Params->GetNumberField(TEXT("componentIndex")));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Collect all landscape components across all landscape proxies
	TArray<ULandscapeComponent*> AllComponents;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		TArray<ULandscapeComponent*> LandscapeComponents;
		Landscape->GetComponents<ULandscapeComponent>(LandscapeComponents);
		AllComponents.Append(LandscapeComponents);
	}

	if (ComponentIndex < 0 || ComponentIndex >= AllComponents.Num())
	{
		return MCPError(FString::Printf(TEXT("Component index %d out of range (0-%d)"), ComponentIndex, AllComponents.Num() - 1));
	}

	ULandscapeComponent* Comp = AllComponents[ComponentIndex];
	if (!Comp)
	{
		return MCPError(TEXT("Component is null"));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("componentIndex"), ComponentIndex);
	Result->SetStringField(TEXT("name"), Comp->GetName());

	FVector CompLocation = Comp->GetComponentLocation();
	Result->SetNumberField(TEXT("locationX"), CompLocation.X);
	Result->SetNumberField(TEXT("locationY"), CompLocation.Y);
	Result->SetNumberField(TEXT("locationZ"), CompLocation.Z);

	Result->SetNumberField(TEXT("sectionBaseX"), Comp->SectionBaseX);
	Result->SetNumberField(TEXT("sectionBaseY"), Comp->SectionBaseY);
	Result->SetNumberField(TEXT("componentSizeQuads"), Comp->ComponentSizeQuads);
	Result->SetNumberField(TEXT("subSections"), Comp->NumSubsections);

	FBox CompBounds = Comp->Bounds.GetBox();
	if (CompBounds.IsValid)
	{
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("minX"), CompBounds.Min.X);
		BoundsObj->SetNumberField(TEXT("minY"), CompBounds.Min.Y);
		BoundsObj->SetNumberField(TEXT("minZ"), CompBounds.Min.Z);
		BoundsObj->SetNumberField(TEXT("maxX"), CompBounds.Max.X);
		BoundsObj->SetNumberField(TEXT("maxY"), CompBounds.Max.Y);
		BoundsObj->SetNumberField(TEXT("maxZ"), CompBounds.Max.Z);
		Result->SetObjectField(TEXT("bounds"), BoundsObj);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) && !Params->TryGetStringField(TEXT("path"), MaterialPath) && !Params->TryGetStringField(TEXT("assetPath"), MaterialPath))
	{
		return MCPError(TEXT("Missing 'materialPath', 'path', or 'assetPath' parameter"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Find the target landscape
	ALandscapeProxy* TargetLandscape = nullptr;
	FString LandscapeName = OptionalString(Params, TEXT("landscapeName"));

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		if (LandscapeName.IsEmpty() || Landscape->GetName() == LandscapeName)
		{
			TargetLandscape = Landscape;
			break;
		}
	}

	if (!TargetLandscape)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	// Load the material
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Capture previous material for rollback and idempotency
	UMaterialInterface* PrevMaterial = TargetLandscape->LandscapeMaterial;
	if (PrevMaterial == Material)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
		Noop->SetStringField(TEXT("materialPath"), MaterialPath);
		return MCPResult(Noop);
	}

	// Set the landscape material
	TargetLandscape->LandscapeMaterial = Material;

	// Update all landscape components to use the new material
	TArray<ULandscapeComponent*> LandscapeComponents;
	TargetLandscape->GetComponents<ULandscapeComponent>(LandscapeComponents);
	for (ULandscapeComponent* Comp : LandscapeComponents)
	{
		if (Comp)
		{
			Comp->SetMaterial(0, Material);
			Comp->MarkRenderStateDirty();
		}
	}

	// Mark the landscape as modified
	TargetLandscape->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetStringField(TEXT("materialName"), Material->GetName());
	Result->SetNumberField(TEXT("componentsUpdated"), LandscapeComponents.Num());

	// Rollback: restore previous material path if any
	if (PrevMaterial)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
		Payload->SetStringField(TEXT("materialPath"), PrevMaterial->GetPathName());
		MCPSetRollback(Result, TEXT("set_landscape_material"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::AddLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find the target landscape
	ALandscapeProxy* TargetLandscape = nullptr;
	FString LandscapeName = OptionalString(Params, TEXT("landscapeName"));

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		if (LandscapeName.IsEmpty() || Landscape->GetName() == LandscapeName)
		{
			TargetLandscape = Landscape;
			break;
		}
	}

	if (!TargetLandscape)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	ULandscapeInfo* LandscapeInfo = TargetLandscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MCPError(TEXT("Failed to get landscape info"));
	}

	// Check if a layer with this name already exists
	for (const FLandscapeInfoLayerSettings& ExistingLayer : LandscapeInfo->Layers)
	{
		if (ExistingLayer.LayerInfoObj && ExistingLayer.GetLayerName().ToString() == LayerName)
		{
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("layerName"), LayerName);
			Result->SetStringField(TEXT("path"), ExistingLayer.LayerInfoObj->GetPathName());
			Result->SetStringField(TEXT("note"), TEXT("Layer already exists on this landscape"));
			return MCPResult(Result);
		}
	}

	// Create a new ULandscapeLayerInfoObject asset
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Landscape/LayerInfos"));

	FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	FString PackageFullPath = PackagePath / AssetName;

	// Check if the asset already exists
	ULandscapeLayerInfoObject* LayerInfoObj = LoadObject<ULandscapeLayerInfoObject>(nullptr, *(PackageFullPath + TEXT(".") + AssetName));
	if (!LayerInfoObj)
	{
		UPackage* Package = CreatePackage(*PackageFullPath);
		if (!Package)
		{
			return MCPError(FString::Printf(TEXT("Failed to create package: %s"), *PackageFullPath));
		}

		LayerInfoObj = NewObject<ULandscapeLayerInfoObject>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!LayerInfoObj)
		{
			return MCPError(TEXT("Failed to create LandscapeLayerInfoObject"));
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LayerInfoObj->LayerName = FName(*LayerName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Set optional properties
		bool bIsWeightBlended = OptionalBool(Params, TEXT("weightBlended"), true);
		// bNoWeightBlend removed in UE 5.7 — weight blending is now controlled per-layer via landscape settings

		// Notify asset registry and save
		FAssetRegistryModule::AssetCreated(LayerInfoObj);
		Package->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(PackageFullPath, false);
	}

	// Register the layer info with the landscape
	int32 LayerIndex = LandscapeInfo->Layers.Num();
	FLandscapeInfoLayerSettings NewLayerSettings(LayerInfoObj, TargetLandscape);
	LandscapeInfo->Layers.Add(NewLayerSettings);

	// Mark the landscape as dirty
	TargetLandscape->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetStringField(TEXT("path"), LayerInfoObj->GetPathName());
	Result->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
	Result->SetNumberField(TEXT("layerIndex"), LayerIndex);
	Result->SetBoolField(TEXT("weightBlended"), true);

	return MCPResult(Result);
}

// ─── #150 get_landscape_material_usage_summary ──────────────────────
// Compact per-proxy dump: class, label, material paths, grass / Nanite /
// landscape component counts. Avoids the big "get all components" blob
// get_actor_details produces when you only need materials + counts.
// #303: spawn an ALandscape with a default flat heightmap at mid-elevation
// (uint16 32768 = no offset). Section/quad defaults match the Editor's
// Landscape Mode "create new" form: 63 quads/subsection, 2 subsections/component
// = 127 quads/component. ComponentCount X/Y default to 8x8 producing a
// 1016x1016 quad landscape (~1 km at default actor scale 100,100,100).
TSharedPtr<FJsonValue> FLandscapeHandlers::CreateLandscape(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const int32 SubsectionSizeQuads = OptionalInt(Params, TEXT("subsectionSizeQuads"), 63);
	const int32 NumSubsections = OptionalInt(Params, TEXT("numSubsections"), 2);
	const int32 ComponentCountX = OptionalInt(Params, TEXT("componentCountX"), 8);
	const int32 ComponentCountY = OptionalInt(Params, TEXT("componentCountY"), 8);

	// Bounds checks: SubsectionSizeQuads must be one of the engine's supported
	// values (7, 15, 31, 63, 127, 255), NumSubsections is 1 or 2, and the
	// component grid has to be at least 1x1.
	auto IsPowOf2Minus1 = [](int32 v) {
		const int32 p = v + 1;
		return v >= 7 && v <= 255 && (p & (p - 1)) == 0;
	};
	if (!IsPowOf2Minus1(SubsectionSizeQuads))
	{
		return MCPError(FString::Printf(
			TEXT("subsectionSizeQuads must be one of 7, 15, 31, 63, 127, 255 (got %d)"),
			SubsectionSizeQuads));
	}
	if (NumSubsections != 1 && NumSubsections != 2)
	{
		return MCPError(FString::Printf(TEXT("numSubsections must be 1 or 2 (got %d)"), NumSubsections));
	}
	if (ComponentCountX < 1 || ComponentCountY < 1)
	{
		return MCPError(TEXT("componentCountX and componentCountY must be >= 1"));
	}

	const int32 ComponentSizeQuads = SubsectionSizeQuads * NumSubsections;
	const int32 SizeX = (ComponentCountX * ComponentSizeQuads) + 1;
	const int32 SizeY = (ComponentCountY * ComponentSizeQuads) + 1;

	const int32 HeightOffset = OptionalInt(Params, TEXT("heightOffset"), 32768);
	if (HeightOffset < 0 || HeightOffset > 65535)
	{
		return MCPError(TEXT("heightOffset must be in [0, 65535] (uint16 elevation)"));
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FVector Scale = OptionalVec3(Params, TEXT("scale"), FVector(100.0, 100.0, 100.0));

	const FString Label = OptionalString(Params, TEXT("label"));

	// Idempotency by label.
	if (auto Existing = MCPCheckActorLabelExists(World, Label, TEXT("skip"), TEXT("Landscape")))
	{
		return Existing;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Landscape)
	{
		return MCPError(TEXT("Failed to spawn ALandscape actor"));
	}
	Landscape->SetActorScale3D(Scale);

	// Allocate a flat heightmap. Layer 0 (FGuid()) is the only edit layer for a
	// non-edit-layer landscape, which is what gets created by the Editor's
	// "create new landscape" defaults.
	TArray<uint16> HeightData;
	HeightData.SetNumUninitialized(SizeX * SizeY);
	for (int32 i = 0; i < HeightData.Num(); ++i)
	{
		HeightData[i] = static_cast<uint16>(HeightOffset);
	}

	TMap<FGuid, TArray<uint16>> ImportHeightData;
	ImportHeightData.Add(FGuid(), MoveTemp(HeightData));

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> ImportLayerInfo;
	ImportLayerInfo.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	TArray<FLandscapeLayer> EmptyLayers;
	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		SizeX - 1, SizeY - 1,
		NumSubsections,
		SubsectionSizeQuads,
		ImportHeightData,
		nullptr,
		ImportLayerInfo,
		ELandscapeImportAlphamapType::Additive,
#if UE_MCP_HAS_5_5_API
		MakeArrayView(EmptyLayers)
#else
		// 5.4: last arg is const TArray<FLandscapeLayer>* (TArrayView signature added in 5.5).
		&EmptyLayers
#endif
	);

	if (!Label.IsEmpty())
	{
		Landscape->SetActorLabel(Label);
	}

	// Register so subsequent get_landscape_info / sample_landscape calls find it.
	if (ULandscapeInfo* LI = Landscape->GetLandscapeInfo())
	{
		LI->FixupProxiesTransform();
		LI->RecreateCollisionComponents();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetNumberField(TEXT("componentCountX"), ComponentCountX);
	Result->SetNumberField(TEXT("componentCountY"), ComponentCountY);
	Result->SetNumberField(TEXT("componentSizeQuads"), ComponentSizeQuads);
	Result->SetNumberField(TEXT("subsectionSizeQuads"), SubsectionSizeQuads);
	Result->SetNumberField(TEXT("numSubsections"), NumSubsections);
	Result->SetNumberField(TEXT("sizeX"), SizeX);
	Result->SetNumberField(TEXT("sizeY"), SizeY);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

// #251: standalone LayerInfo asset creation. Unlike add_landscape_layer_info
// (which requires a landscape in the world to register the layer against),
// this creates the ULandscapeLayerInfoObject asset in the content browser
// so paint workflows can pre-author layer assets before the landscape exists.
TSharedPtr<FJsonValue> FLandscapeHandlers::CreateLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	const FString Name = OptionalString(Params, TEXT("name"), FString::Printf(TEXT("LI_%s"), *LayerName));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Landscape/LayerInfos"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	TSharedPtr<FJsonValue> Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("LandscapeLayerInfoObject"));
	if (Existing.IsValid()) return Existing;

	const FString PackageFullPath = PackagePath / Name;
	UPackage* Package = CreatePackage(*PackageFullPath);
	if (!Package)
	{
		return MCPError(FString::Printf(TEXT("Failed to create package: %s"), *PackageFullPath));
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, *Name, RF_Public | RF_Standalone);
	if (!LayerInfo)
	{
		return MCPError(TEXT("Failed to create LandscapeLayerInfoObject"));
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	LayerInfo->LayerName = FName(*LayerName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// Optional hardness; physics material is settable via set_actor_property
	// against the asset path (PhysicsCore is not a hard dep of this module).
	double Hardness = 0.0;
	if (Params->TryGetNumberField(TEXT("hardness"), Hardness))
	{
		LayerInfo->Hardness = static_cast<float>(Hardness);
	}

	FAssetRegistryModule::AssetCreated(LayerInfo);
	Package->MarkPackageDirty();
	SaveAssetPackage(LayerInfo);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), LayerInfo->GetPathName());
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	MCPSetDeleteAssetRollback(Result, LayerInfo->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> ProxyArray;
	TSet<FString> UniqueMaterials;
	int32 TotalComponents = 0, TotalGrass = 0, TotalNanite = 0;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		if (!Proxy) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), Proxy->GetActorLabel());
		Obj->SetStringField(TEXT("name"), Proxy->GetName());
		Obj->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
		Obj->SetStringField(TEXT("path"), Proxy->GetPathName());

		if (UMaterialInterface* Mat = Proxy->LandscapeMaterial)
		{
			Obj->SetStringField(TEXT("landscapeMaterial"), Mat->GetPathName());
			UniqueMaterials.Add(Mat->GetPathName());
		}
		if (UMaterialInterface* HoleMat = Proxy->LandscapeHoleMaterial)
		{
			Obj->SetStringField(TEXT("landscapeHoleMaterial"), HoleMat->GetPathName());
		}

		// Histogram components by class (grass / Nanite / regular landscape comps)
		int32 LandscapeComps = 0, GrassComps = 0, NaniteComps = 0;
		TArray<UActorComponent*> Comps;
		Proxy->GetComponents(Comps);
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			const FString CName = C->GetClass()->GetName();
			if (CName == TEXT("LandscapeComponent")) LandscapeComps++;
			else if (CName == TEXT("GrassInstancedStaticMeshComponent")) GrassComps++;
			else if (CName == TEXT("LandscapeNaniteComponent")) NaniteComps++;
		}
		Obj->SetNumberField(TEXT("landscapeComponentCount"), LandscapeComps);
		Obj->SetNumberField(TEXT("grassComponentCount"), GrassComps);
		Obj->SetNumberField(TEXT("naniteComponentCount"), NaniteComps);
		TotalComponents += LandscapeComps;
		TotalGrass += GrassComps;
		TotalNanite += NaniteComps;

		const FVector Loc = Proxy->GetActorLocation();
		const FVector Scale = Proxy->GetActorScale3D();
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Loc.X);
		LocObj->SetNumberField(TEXT("y"), Loc.Y);
		LocObj->SetNumberField(TEXT("z"), Loc.Z);
		Obj->SetObjectField(TEXT("location"), LocObj);
		TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
		ScaleObj->SetNumberField(TEXT("x"), Scale.X);
		ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
		ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
		Obj->SetObjectField(TEXT("scale"), ScaleObj);

		ProxyArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TArray<TSharedPtr<FJsonValue>> UniqueMatsArr;
	for (const FString& M : UniqueMaterials) UniqueMatsArr.Add(MakeShared<FJsonValueString>(M));

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("proxies"), ProxyArray);
	Result->SetNumberField(TEXT("proxyCount"), ProxyArray.Num());
	Result->SetArrayField(TEXT("uniqueLandscapeMaterials"), UniqueMatsArr);
	Result->SetNumberField(TEXT("totalLandscapeComponents"), TotalComponents);
	Result->SetNumberField(TEXT("totalGrassComponents"), TotalGrass);
	Result->SetNumberField(TEXT("totalNaniteComponents"), TotalNanite);
	return MCPResult(Result);
}
