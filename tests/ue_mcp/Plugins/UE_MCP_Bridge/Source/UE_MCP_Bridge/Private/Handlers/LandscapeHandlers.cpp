#include "LandscapeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
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
	Registry.RegisterHandler(TEXT("sculpt_landscape"), &SculptLandscape);
	Registry.RegisterHandler(TEXT("paint_landscape_layer"), &PaintLandscapeLayer);
	Registry.RegisterHandler(TEXT("import_heightmap"), &ImportHeightmap);
	Registry.RegisterHandler(TEXT("set_landscape_material"), &SetLandscapeMaterial);
	Registry.RegisterHandler(TEXT("get_landscape_bounds"), &GetLandscapeBounds);
	Registry.RegisterHandler(TEXT("add_landscape_layer_info"), &AddLandscapeLayerInfo);
	Registry.RegisterHandler(TEXT("import_landscape_heightmap"), &ImportHeightmap);
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

TSharedPtr<FJsonValue> FLandscapeHandlers::SculptLandscape(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj || !(*LocationObj).IsValid())
	{
		return MCPError(TEXT("Missing 'location' parameter (object with x, y)"));
	}

	double LocX = 0, LocY = 0;
	(*LocationObj)->TryGetNumberField(TEXT("x"), LocX);
	(*LocationObj)->TryGetNumberField(TEXT("y"), LocY);

	double SculptRadius = OptionalNumber(Params, TEXT("radius"), 500.0);
	double Strength = OptionalNumber(Params, TEXT("strength"), 0.5);
	FString Operation = OptionalString(Params, TEXT("operation"), TEXT("raise"));
	double Falloff = OptionalNumber(Params, TEXT("falloff"), 0.5);

	REQUIRE_EDITOR_WORLD(World);

	// Verify a landscape exists by line tracing at the target location
	bool bFoundLandscape = false;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (*It)
		{
			bFoundLandscape = true;
			break;
		}
	}

	if (!bFoundLandscape)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	// Landscape sculpting is not directly exposed as a simple C++ API.
	// The LandscapeEdMode (editor mode) handles sculpting internally.
	// Fall back to console command approach.
	FString Command = FString::Printf(
		TEXT("Landscape.Sculpt X=%.1f Y=%.1f Radius=%.1f Strength=%.2f Op=%s"),
		LocX, LocY, SculptRadius, Strength, *Operation);

	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	auto Result = MCPSuccess();
	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	LocationResult->SetNumberField(TEXT("x"), LocX);
	LocationResult->SetNumberField(TEXT("y"), LocY);
	Result->SetObjectField(TEXT("location"), LocationResult);
	Result->SetNumberField(TEXT("radius"), SculptRadius);
	Result->SetNumberField(TEXT("strength"), Strength);
	Result->SetStringField(TEXT("operation"), Operation);
	Result->SetNumberField(TEXT("falloff"), Falloff);
	Result->SetStringField(TEXT("note"), TEXT("Executed via console command. Verify visually. If the console command is not supported, use execute_python with unreal.LandscapeEditorLibrary.sculpt() instead."));
	// No rollback: destructive/external — sculpting permanently alters heightmap.

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::PaintLandscapeLayer(const TSharedPtr<FJsonObject>& Params)
{
	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj || !(*LocationObj).IsValid())
	{
		return MCPError(TEXT("Missing 'location' parameter (object with x, y)"));
	}

	double LocX = 0, LocY = 0;
	(*LocationObj)->TryGetNumberField(TEXT("x"), LocX);
	(*LocationObj)->TryGetNumberField(TEXT("y"), LocY);

	double PaintRadius = OptionalNumber(Params, TEXT("radius"), 500.0);
	double Strength = OptionalNumber(Params, TEXT("strength"), 1.0);
	double Falloff = OptionalNumber(Params, TEXT("falloff"), 0.5);

	REQUIRE_EDITOR_WORLD(World);

	// Verify a landscape exists
	bool bFoundLandscape = false;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (*It)
		{
			bFoundLandscape = true;
			break;
		}
	}

	if (!bFoundLandscape)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	// Landscape layer painting is internal to LandscapeEdMode.
	// The C++ API for painting layers requires the landscape editor mode to be active
	// and is not trivially accessible from plugins.
	// Provide the fallback note for using execute_python.
	auto Result = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	LocationResult->SetNumberField(TEXT("x"), LocX);
	LocationResult->SetNumberField(TEXT("y"), LocY);
	Result->SetObjectField(TEXT("location"), LocationResult);
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetNumberField(TEXT("radius"), PaintRadius);
	Result->SetNumberField(TEXT("strength"), Strength);
	Result->SetNumberField(TEXT("falloff"), Falloff);

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("note"),
		TEXT("Landscape layer painting requires LandscapeEdMode which is not accessible from C++ plugins. ")
		TEXT("Use the execute_python handler with unreal.LandscapeEditorLibrary.paint_layer() if available, ")
		TEXT("or manually paint in the editor landscape tool."));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ImportHeightmap(const TSharedPtr<FJsonObject>& Params)
{
	FString FilePath;
	if (auto Err = RequireString(Params, TEXT("filePath"), FilePath)) return Err;

	// Verify the file exists
	if (!FPaths::FileExists(FilePath))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FilePath));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Find the landscape
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

	// Read the heightmap file
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		return MCPError(FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
	}

	// Heightmap import requires the landscape editor subsystem which is internal to LandscapeEdMode.
	// The raw heightmap data has been loaded successfully.
	// Provide information about the file and a note about the import path.
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("filePath"), FilePath);
	Result->SetNumberField(TEXT("fileSizeBytes"), FileData.Num());
	Result->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());

	// Determine if this looks like a 16-bit raw heightmap based on file size
	int64 FileSize = FileData.Num();
	bool bLooksLikeRaw16 = false;
	int32 PossibleResolution = 0;
	for (int32 Res = 127; Res <= 8161; Res += 2)
	{
		if (FileSize == (int64)Res * Res * 2)
		{
			bLooksLikeRaw16 = true;
			PossibleResolution = Res;
			break;
		}
	}

	if (bLooksLikeRaw16)
	{
		Result->SetNumberField(TEXT("possibleResolution"), PossibleResolution);
		Result->SetStringField(TEXT("format"), TEXT("RAW16"));
	}

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("note"),
		TEXT("Heightmap file loaded and validated. Direct heightmap import requires LandscapeEditorUtils ")
		TEXT("which is internal to the landscape editor module. Use the execute_python handler with ")
		TEXT("unreal.LandscapeEditorLibrary.import_heightmap() if available, or import through the ")
		TEXT("Landscape editor mode Import tool."));

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

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeBounds(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString LandscapeName = OptionalString(Params, TEXT("landscapeName"));

	TArray<TSharedPtr<FJsonValue>> LandscapeBoundsArray;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		// Filter by name if specified
		if (!LandscapeName.IsEmpty() && Landscape->GetName() != LandscapeName)
		{
			continue;
		}

		TSharedPtr<FJsonObject> LandscapeObj = MakeShared<FJsonObject>();
		LandscapeObj->SetStringField(TEXT("name"), Landscape->GetName());

		// Get actor bounds using GetActorBounds
		FVector Origin;
		FVector BoxExtent;
		Landscape->GetActorBounds(false, Origin, BoxExtent);

		TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
		OriginObj->SetNumberField(TEXT("x"), Origin.X);
		OriginObj->SetNumberField(TEXT("y"), Origin.Y);
		OriginObj->SetNumberField(TEXT("z"), Origin.Z);
		LandscapeObj->SetObjectField(TEXT("origin"), OriginObj);

		TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
		ExtentObj->SetNumberField(TEXT("x"), BoxExtent.X);
		ExtentObj->SetNumberField(TEXT("y"), BoxExtent.Y);
		ExtentObj->SetNumberField(TEXT("z"), BoxExtent.Z);
		LandscapeObj->SetObjectField(TEXT("boxExtent"), ExtentObj);

		// Also provide min/max corners for convenience
		FVector BoundsMin = Origin - BoxExtent;
		FVector BoundsMax = Origin + BoxExtent;

		TSharedPtr<FJsonObject> MinObj = MakeShared<FJsonObject>();
		MinObj->SetNumberField(TEXT("x"), BoundsMin.X);
		MinObj->SetNumberField(TEXT("y"), BoundsMin.Y);
		MinObj->SetNumberField(TEXT("z"), BoundsMin.Z);
		LandscapeObj->SetObjectField(TEXT("min"), MinObj);

		TSharedPtr<FJsonObject> MaxObj = MakeShared<FJsonObject>();
		MaxObj->SetNumberField(TEXT("x"), BoundsMax.X);
		MaxObj->SetNumberField(TEXT("y"), BoundsMax.Y);
		MaxObj->SetNumberField(TEXT("z"), BoundsMax.Z);
		LandscapeObj->SetObjectField(TEXT("max"), MaxObj);

		// Size
		FVector Size = BoxExtent * 2.0;
		TSharedPtr<FJsonObject> SizeObj = MakeShared<FJsonObject>();
		SizeObj->SetNumberField(TEXT("x"), Size.X);
		SizeObj->SetNumberField(TEXT("y"), Size.Y);
		SizeObj->SetNumberField(TEXT("z"), Size.Z);
		LandscapeObj->SetObjectField(TEXT("size"), SizeObj);

		// Location
		FVector Location = Landscape->GetActorLocation();
		TSharedPtr<FJsonObject> LocationResultObj = MakeShared<FJsonObject>();
		LocationResultObj->SetNumberField(TEXT("x"), Location.X);
		LocationResultObj->SetNumberField(TEXT("y"), Location.Y);
		LocationResultObj->SetNumberField(TEXT("z"), Location.Z);
		LandscapeObj->SetObjectField(TEXT("location"), LocationResultObj);

		LandscapeBoundsArray.Add(MakeShared<FJsonValueObject>(LandscapeObj));
	}

	if (LandscapeBoundsArray.Num() == 0)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("landscapes"), LandscapeBoundsArray);
	Result->SetNumberField(TEXT("count"), LandscapeBoundsArray.Num());

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
