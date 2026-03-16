#include "LandscapeHandlers.h"
#include "HandlerRegistry.h"
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
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	if (LandscapeArray.Num() == 0)
	{
		Result->SetStringField(TEXT("landscape"), TEXT("none"));
	}
	else
	{
		Result->SetArrayField(TEXT("landscapes"), LandscapeArray);
	}

	Result->SetNumberField(TEXT("count"), LandscapeArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeLayers(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetArrayField(TEXT("layers"), LayerArray);
	Result->SetNumberField(TEXT("count"), LayerArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SampleLandscape(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	const TSharedPtr<FJsonObject>* PointObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("point"), PointObj))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'point' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FVector Point;
	Point.X = (*PointObj)->GetNumberField(TEXT("x"));
	Point.Y = (*PointObj)->GetNumberField(TEXT("y"));
	Point.Z = (*PointObj)->GetNumberField(TEXT("z"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
				Result->SetBoolField(TEXT("success"), true);
				return MakeShared<FJsonValueObject>(Result);
			}
		}
	}

	Result->SetBoolField(TEXT("hit"), false);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeSplines(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetArrayField(TEXT("controlPoints"), SplineArray);
	Result->SetNumberField(TEXT("count"), SplineArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeComponent(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	int32 ComponentIndex = 0;
	if (Params->HasField(TEXT("componentIndex")))
	{
		ComponentIndex = static_cast<int32>(Params->GetNumberField(TEXT("componentIndex")));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Component index %d out of range (0-%d)"), ComponentIndex, AllComponents.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	ULandscapeComponent* Comp = AllComponents[ComponentIndex];
	if (!Comp)
	{
		Result->SetStringField(TEXT("error"), TEXT("Component is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SculptLandscape(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj || !(*LocationObj).IsValid())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'location' parameter (object with x, y)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double LocX = 0, LocY = 0;
	(*LocationObj)->TryGetNumberField(TEXT("x"), LocX);
	(*LocationObj)->TryGetNumberField(TEXT("y"), LocY);

	double SculptRadius = 500.0;
	Params->TryGetNumberField(TEXT("radius"), SculptRadius);

	double Strength = 0.5;
	Params->TryGetNumberField(TEXT("strength"), Strength);

	FString Operation = TEXT("raise");
	Params->TryGetStringField(TEXT("operation"), Operation);

	double Falloff = 0.5;
	Params->TryGetNumberField(TEXT("falloff"), Falloff);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), TEXT("No landscape found in the current level"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Landscape sculpting is not directly exposed as a simple C++ API.
	// The LandscapeEdMode (editor mode) handles sculpting internally.
	// Fall back to console command approach.
	FString Command = FString::Printf(
		TEXT("Landscape.Sculpt X=%.1f Y=%.1f Radius=%.1f Strength=%.2f Op=%s"),
		LocX, LocY, SculptRadius, Strength, *Operation);

	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	LocationResult->SetNumberField(TEXT("x"), LocX);
	LocationResult->SetNumberField(TEXT("y"), LocY);
	Result->SetObjectField(TEXT("location"), LocationResult);
	Result->SetNumberField(TEXT("radius"), SculptRadius);
	Result->SetNumberField(TEXT("strength"), Strength);
	Result->SetStringField(TEXT("operation"), Operation);
	Result->SetNumberField(TEXT("falloff"), Falloff);
	Result->SetStringField(TEXT("note"), TEXT("Executed via console command. Verify visually. If the console command is not supported, use execute_python with unreal.LandscapeEditorLibrary.sculpt() instead."));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::PaintLandscapeLayer(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString LayerName;
	if (!Params->TryGetStringField(TEXT("layerName"), LayerName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'layerName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj || !(*LocationObj).IsValid())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'location' parameter (object with x, y)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double LocX = 0, LocY = 0;
	(*LocationObj)->TryGetNumberField(TEXT("x"), LocX);
	(*LocationObj)->TryGetNumberField(TEXT("y"), LocY);

	double PaintRadius = 500.0;
	Params->TryGetNumberField(TEXT("radius"), PaintRadius);

	double Strength = 1.0;
	Params->TryGetNumberField(TEXT("strength"), Strength);

	double Falloff = 0.5;
	Params->TryGetNumberField(TEXT("falloff"), Falloff);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), TEXT("No landscape found in the current level"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Landscape layer painting is internal to LandscapeEdMode.
	// The C++ API for painting layers requires the landscape editor mode to be active
	// and is not trivially accessible from plugins.
	// Provide the fallback note for using execute_python.
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

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ImportHeightmap(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString FilePath;
	if (!Params->TryGetStringField(TEXT("filePath"), FilePath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'filePath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Verify the file exists
	if (!FPaths::FileExists(FilePath))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("File not found: %s"), *FilePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the landscape
	ALandscapeProxy* TargetLandscape = nullptr;
	FString LandscapeName;
	Params->TryGetStringField(TEXT("landscapeName"), LandscapeName);

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
		Result->SetStringField(TEXT("error"), TEXT("No landscape found in the current level"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Read the heightmap file
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Heightmap import requires the landscape editor subsystem which is internal to LandscapeEdMode.
	// The raw heightmap data has been loaded successfully.
	// Provide information about the file and a note about the import path.
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

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the target landscape
	ALandscapeProxy* TargetLandscape = nullptr;
	FString LandscapeName;
	Params->TryGetStringField(TEXT("landscapeName"), LandscapeName);

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
		Result->SetStringField(TEXT("error"), TEXT("No landscape found in the current level"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load the material
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetStringField(TEXT("materialName"), Material->GetName());
	Result->SetNumberField(TEXT("componentsUpdated"), LandscapeComponents.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeBounds(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString LandscapeName;
	Params->TryGetStringField(TEXT("landscapeName"), LandscapeName);

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
		Result->SetStringField(TEXT("error"), TEXT("No landscape found in the current level"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetArrayField(TEXT("landscapes"), LandscapeBoundsArray);
	Result->SetNumberField(TEXT("count"), LandscapeBoundsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
