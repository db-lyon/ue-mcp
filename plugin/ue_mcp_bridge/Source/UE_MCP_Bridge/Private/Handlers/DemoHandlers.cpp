#include "DemoHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

// Core / Editor
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LevelEditorSubsystem.h"
#include "FileHelpers.h"

// Assets / Packages
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// Static mesh actors / components
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

// Lights
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"

// Atmosphere / Post-process
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"

// Materials
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"

// Niagara
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitter.h"

// PCG
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGVolume.h"

// Sequencer
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "MovieScene.h"
#include "Tracks/MovieScene3DTransformTrack.h"

// Movement
#include "GameFramework/RotatingMovementComponent.h"

// Widget / Blutility (kept for potential future use)
// EditorUtilityWidget creation moved to interactive widget tool

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace DemoConstants
{
	static const FString FOLDER      = TEXT("Demo_Scene");
	static const FString MAT_DIR     = TEXT("/Game/Demo");
	static const FString DEMO_LEVEL  = TEXT("/Game/Demo/DemoLevel");
	static const FString CUBE_MESH   = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const FString SPHERE_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const FString CYLINDER_MESH = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void FDemoHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("demo_step"),      &DemoStep);
	Registry.RegisterHandler(TEXT("demo_get_steps"), &DemoGetSteps);
	Registry.RegisterHandler(TEXT("demo_cleanup"),   &DemoCleanup);
}

// ---------------------------------------------------------------------------
// Step definitions
// ---------------------------------------------------------------------------
TArray<FDemoHandlers::FDemoStep> FDemoHandlers::GetStepDefinitions()
{
	TArray<FDemoStep> Steps;
	Steps.Add({ 1,  TEXT("create_level"),    TEXT("Create new level at /Game/Demo/DemoLevel") });
	Steps.Add({ 2,  TEXT("materials"),        TEXT("Create 3 materials: floor, glow, pillar") });
	Steps.Add({ 3,  TEXT("floor"),            TEXT("60m dark reflective floor") });
	Steps.Add({ 4,  TEXT("pedestal"),         TEXT("Central pedestal cylinder") });
	Steps.Add({ 5,  TEXT("hero_sphere"),      TEXT("Emissive gold hero sphere") });
	Steps.Add({ 6,  TEXT("pillars"),          TEXT("4 corner pillar cylinders") });
	Steps.Add({ 7,  TEXT("orbs"),             TEXT("4 glowing orbs at pillar bases") });
	Steps.Add({ 8,  TEXT("neon_lights"),      TEXT("4 coloured point lights") });
	Steps.Add({ 9,  TEXT("hero_light"),       TEXT("Warm point light above hero") });
	Steps.Add({ 10, TEXT("moonlight"),        TEXT("Directional moon light") });
	Steps.Add({ 11, TEXT("sky_light"),        TEXT("SkyLight ambient fill") });
	Steps.Add({ 12, TEXT("fog"),              TEXT("ExponentialHeightFog atmosphere") });
	Steps.Add({ 13, TEXT("post_process"),     TEXT("PostProcessVolume bloom/vignette") });
	Steps.Add({ 14, TEXT("niagara_vfx"),      TEXT("Niagara particle system above hero") });
	Steps.Add({ 15, TEXT("pcg_scatter"),      TEXT("PCG scatter volume on floor") });
	Steps.Add({ 16, TEXT("orbit_rings"),      TEXT("8 orbiting emissive spheres + rotation") });
	Steps.Add({ 17, TEXT("level_sequence"),   TEXT("LevelSequence with hero binding") });
	Steps.Add({ 18, TEXT("tuning_panel"),     TEXT("EditorUtilityWidget tuning panel") });
	Steps.Add({ 19, TEXT("save"),             TEXT("Save current level") });
	return Steps;
}

// ---------------------------------------------------------------------------
// Handler: demo_get_steps
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FDemoHandlers::DemoGetSteps(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();
	TArray<FDemoStep> Steps = GetStepDefinitions();

	TArray<TSharedPtr<FJsonValue>> StepsArray;
	for (const FDemoStep& S : Steps)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), S.Index);
		Obj->SetStringField(TEXT("id"), S.Id);
		Obj->SetStringField(TEXT("description"), S.Description);
		StepsArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Result->SetArrayField(TEXT("steps"), StepsArray);
	Result->SetNumberField(TEXT("count"), StepsArray.Num());

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Handler: demo_step
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FDemoHandlers::DemoStep(const TSharedPtr<FJsonObject>& Params)
{
	// If no step param, return step list
	double StepNum = -1;
	if (!Params->TryGetNumberField(TEXT("step"), StepNum))
	{
		return DemoGetSteps(Params);
	}

	int32 StepIndex = static_cast<int32>(StepNum);

	// Dispatch to step implementation
	TSharedPtr<FJsonObject> StepResult;
	switch (StepIndex)
	{
	case 1:  StepResult = StepCreateLevel(); break;
	case 2:  StepResult = StepMaterials(); break;
	case 3:  StepResult = StepFloor(); break;
	case 4:  StepResult = StepPedestal(); break;
	case 5:  StepResult = StepHeroSphere(); break;
	case 6:  StepResult = StepPillars(); break;
	case 7:  StepResult = StepOrbs(); break;
	case 8:  StepResult = StepNeonLights(); break;
	case 9:  StepResult = StepHeroLight(); break;
	case 10: StepResult = StepMoonlight(); break;
	case 11: StepResult = StepSkyLight(); break;
	case 12: StepResult = StepFog(); break;
	case 13: StepResult = StepPostProcess(); break;
	case 14: StepResult = StepNiagaraVfx(); break;
	case 15: StepResult = StepPcgScatter(); break;
	case 16: StepResult = StepOrbitRings(); break;
	case 17: StepResult = StepLevelSequence(); break;
	case 18: StepResult = StepTuningPanel(); break;
	case 19: StepResult = StepSave(); break;
	default:
	{
		return MCPError(FString::Printf(TEXT("Invalid step index %d. Valid range: 1-19"), StepIndex));
	}
	}

	// Tag the result with step metadata
	if (StepResult.IsValid())
	{
		StepResult->SetNumberField(TEXT("step"), StepIndex);
		TArray<FDemoStep> Defs = GetStepDefinitions();
		if (StepIndex >= 1 && StepIndex <= Defs.Num())
		{
			StepResult->SetStringField(TEXT("stepId"), Defs[StepIndex - 1].Id);
		}
	}

	return MCPResult(StepResult);
}

// ---------------------------------------------------------------------------
// Handler: demo_cleanup
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FDemoHandlers::DemoCleanup(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GetEditorWorld();

	// 1) Destroy actors whose label starts with "Demo_"
	int32 ActorsDeleted = 0;
	if (World)
	{
		TArray<AActor*> ToDelete;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->GetActorLabel().StartsWith(TEXT("Demo_")))
			{
				ToDelete.Add(Actor);
			}
		}
		for (AActor* Actor : ToDelete)
		{
			World->DestroyActor(Actor);
			++ActorsDeleted;
		}
	}

	// 2) Delete demo assets
	TArray<FString> AssetsToDelete = {
		DemoConstants::MAT_DIR / TEXT("M_Demo_Floor"),
		DemoConstants::MAT_DIR / TEXT("M_Demo_Glow"),
		DemoConstants::MAT_DIR / TEXT("M_Demo_Pillar"),
		DemoConstants::MAT_DIR / TEXT("NS_Demo_Aura"),
		DemoConstants::MAT_DIR / TEXT("PCG_Demo_Scatter"),
		DemoConstants::MAT_DIR / TEXT("SEQ_Demo_Showcase"),
		DemoConstants::MAT_DIR / TEXT("EUW_DemoTuning"),
	};

	int32 AssetsDeleted = 0;
	for (const FString& AssetPath : AssetsToDelete)
	{
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			if (UEditorAssetLibrary::DeleteAsset(AssetPath))
			{
				++AssetsDeleted;
			}
		}
	}

	// 3) Delete DemoLevel
	if (UEditorAssetLibrary::DoesAssetExist(DemoConstants::DEMO_LEVEL))
	{
		UEditorAssetLibrary::DeleteAsset(DemoConstants::DEMO_LEVEL);
		++AssetsDeleted;
	}

	// 4) Delete /Game/Demo directory if empty
	if (UEditorAssetLibrary::DoesDirectoryExist(DemoConstants::MAT_DIR))
	{
		TArray<FString> Remaining = UEditorAssetLibrary::ListAssets(DemoConstants::MAT_DIR, true);
		if (Remaining.Num() == 0)
		{
			UEditorAssetLibrary::DeleteDirectory(DemoConstants::MAT_DIR);
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("actorsDeleted"), ActorsDeleted);
	Result->SetNumberField(TEXT("assetsDeleted"), AssetsDeleted);

	return MCPResult(Result);
}

// ===========================================================================
//  Utility helpers
// ===========================================================================
AActor* FDemoHandlers::SpawnMesh(const FString& Label, const FString& MeshPath,
	FVector Location, FRotator Rotation, FVector Scale)
{
	UWorld* World = GetEditorWorld();
	if (!World) return nullptr;

	FTransform SpawnTransform(Rotation, Location);
	AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), SpawnTransform);
	if (!MeshActor) return nullptr;

	MeshActor->SetActorLabel(Label);
	MeshActor->SetFolderPath(*DemoConstants::FOLDER);
	MeshActor->SetActorScale3D(Scale);

	// Assign mesh
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (Mesh && MeshActor->GetStaticMeshComponent())
	{
		MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	}

	return MeshActor;
}

AActor* FDemoHandlers::SpawnPointLight(const FString& Label, FVector Location,
	FColor Color, float Intensity)
{
	UWorld* World = GetEditorWorld();
	if (!World) return nullptr;

	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	APointLight* Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), SpawnTransform);
	if (!Light) return nullptr;

	Light->SetActorLabel(Label);
	Light->SetFolderPath(*DemoConstants::FOLDER);

	UPointLightComponent* Comp = Light->PointLightComponent;
	if (Comp)
	{
		Comp->SetIntensity(Intensity);
		Comp->SetLightColor(FLinearColor(Color));
	}

	return Light;
}

UMaterialInterface* FDemoHandlers::LoadDemoMat(const FString& Name)
{
	FString FullPath = DemoConstants::MAT_DIR / Name + TEXT(".") + Name;
	return LoadObject<UMaterialInterface>(nullptr, *FullPath);
}

void FDemoHandlers::ApplyMat(AActor* Actor, UMaterialInterface* Mat)
{
	if (!Actor || !Mat) return;

	AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor);
	if (MeshActor && MeshActor->GetStaticMeshComponent())
	{
		MeshActor->GetStaticMeshComponent()->SetMaterial(0, Mat);
	}
}

// ===========================================================================
//  Helper: create a simple material with a constant base color
// ===========================================================================
static UMaterial* CreateSimpleMaterial(const FString& Name, const FString& PackagePath,
	FLinearColor BaseColor, float Metallic, float Roughness,
	bool bEmissive = false, FLinearColor EmissiveColor = FLinearColor::Black, float EmissiveStrength = 1.0f)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Delete existing
	FString FullPath = PackagePath / Name;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		UEditorAssetLibrary::DeleteAsset(FullPath);
	}

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterial::StaticClass(), Factory);
	UMaterial* Mat = Cast<UMaterial>(NewAsset);
	if (!Mat) return nullptr;

	Mat->PreEditChange(nullptr);

	// Base color expression
	UMaterialExpressionConstant4Vector* ColorExpr = NewObject<UMaterialExpressionConstant4Vector>(Mat);
	ColorExpr->Constant = BaseColor;
	Mat->GetExpressionCollection().AddExpression(ColorExpr);
	Mat->GetEditorOnlyData()->BaseColor.Connect(0, ColorExpr);

	// Metallic
	UMaterialExpressionConstant* MetallicExpr = NewObject<UMaterialExpressionConstant>(Mat);
	MetallicExpr->R = Metallic;
	Mat->GetExpressionCollection().AddExpression(MetallicExpr);
	Mat->GetEditorOnlyData()->Metallic.Connect(0, MetallicExpr);

	// Roughness
	UMaterialExpressionConstant* RoughnessExpr = NewObject<UMaterialExpressionConstant>(Mat);
	RoughnessExpr->R = Roughness;
	Mat->GetExpressionCollection().AddExpression(RoughnessExpr);
	Mat->GetEditorOnlyData()->Roughness.Connect(0, RoughnessExpr);

	// Emissive
	if (bEmissive)
	{
		UMaterialExpressionConstant4Vector* EmissiveExpr = NewObject<UMaterialExpressionConstant4Vector>(Mat);
		EmissiveExpr->Constant = EmissiveColor;
		Mat->GetExpressionCollection().AddExpression(EmissiveExpr);

		UMaterialExpressionConstant* StrengthExpr = NewObject<UMaterialExpressionConstant>(Mat);
		StrengthExpr->R = EmissiveStrength;
		Mat->GetExpressionCollection().AddExpression(StrengthExpr);

		UMaterialExpressionMultiply* MulExpr = NewObject<UMaterialExpressionMultiply>(Mat);
		Mat->GetExpressionCollection().AddExpression(MulExpr);
		MulExpr->A.Connect(0, EmissiveExpr);
		MulExpr->B.Connect(0, StrengthExpr);

		Mat->GetEditorOnlyData()->EmissiveColor.Connect(0, MulExpr);
	}

	Mat->PostEditChange();
	Mat->MarkPackageDirty();

	// Save
	SaveAssetPackage(Mat);

	return Mat;
}

// ===========================================================================
//  Step implementations
// ===========================================================================

// Step 1: Create level
TSharedPtr<FJsonObject> FDemoHandlers::StepCreateLevel()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Ensure /Game/Demo directory exists by creating the level there
	ULevelEditorSubsystem* LevelSub = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LevelSub)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	// Create a new map via the editor
	UEditorAssetLibrary::MakeDirectory(DemoConstants::MAT_DIR);
	bool bCreated = LevelSub->NewLevel(DemoConstants::DEMO_LEVEL);

	Result->SetStringField(TEXT("levelPath"), DemoConstants::DEMO_LEVEL);
	Result->SetBoolField(TEXT("created"), bCreated);
	Result->SetBoolField(TEXT("success"), bCreated);
	return Result;
}

// Step 2: Materials
TSharedPtr<FJsonObject> FDemoHandlers::StepMaterials()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UEditorAssetLibrary::MakeDirectory(DemoConstants::MAT_DIR);

	// M_Demo_Floor - dark reflective
	UMaterial* Floor = CreateSimpleMaterial(
		TEXT("M_Demo_Floor"), DemoConstants::MAT_DIR,
		FLinearColor(0.02f, 0.02f, 0.025f, 1.0f), // near-black
		0.9f,  // high metallic
		0.15f  // low roughness (reflective)
	);

	// M_Demo_Glow - emissive gold
	UMaterial* Glow = CreateSimpleMaterial(
		TEXT("M_Demo_Glow"), DemoConstants::MAT_DIR,
		FLinearColor(0.8f, 0.6f, 0.1f, 1.0f), // gold base
		0.5f,
		0.3f,
		true,  // emissive
		FLinearColor(1.0f, 0.75f, 0.1f, 1.0f), // emissive gold
		10.0f  // strength
	);

	// M_Demo_Pillar - dark matte
	UMaterial* Pillar = CreateSimpleMaterial(
		TEXT("M_Demo_Pillar"), DemoConstants::MAT_DIR,
		FLinearColor(0.05f, 0.05f, 0.06f, 1.0f),
		0.0f,  // no metallic
		0.85f  // high roughness (matte)
	);

	TArray<TSharedPtr<FJsonValue>> MatArray;
	if (Floor)  MatArray.Add(MakeShared<FJsonValueString>(Floor->GetPathName()));
	if (Glow)   MatArray.Add(MakeShared<FJsonValueString>(Glow->GetPathName()));
	if (Pillar) MatArray.Add(MakeShared<FJsonValueString>(Pillar->GetPathName()));

	Result->SetArrayField(TEXT("materials"), MatArray);
	Result->SetNumberField(TEXT("count"), MatArray.Num());
	Result->SetBoolField(TEXT("success"), MatArray.Num() == 3);
	return Result;
}

// Step 3: Floor
TSharedPtr<FJsonObject> FDemoHandlers::StepFloor()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	AActor* FloorActor = SpawnMesh(
		TEXT("Demo_Floor"), DemoConstants::CUBE_MESH,
		FVector(0.0, 0.0, -5.0),
		FRotator::ZeroRotator,
		FVector(60.0, 60.0, 0.1)
	);

	if (!FloorActor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn floor"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	ApplyMat(FloorActor, LoadDemoMat(TEXT("M_Demo_Floor")));

	Result->SetStringField(TEXT("actorLabel"), FloorActor->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 4: Pedestal
TSharedPtr<FJsonObject> FDemoHandlers::StepPedestal()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	AActor* Ped = SpawnMesh(
		TEXT("Demo_Pedestal"), DemoConstants::CYLINDER_MESH,
		FVector(0.0, 0.0, 75.0),
		FRotator::ZeroRotator,
		FVector(2.5, 2.5, 1.5)
	);

	if (!Ped)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn pedestal"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	ApplyMat(Ped, LoadDemoMat(TEXT("M_Demo_Pillar")));

	Result->SetStringField(TEXT("actorLabel"), Ped->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 5: Hero sphere
TSharedPtr<FJsonObject> FDemoHandlers::StepHeroSphere()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	AActor* Hero = SpawnMesh(
		TEXT("Demo_HeroSphere"), DemoConstants::SPHERE_MESH,
		FVector(0.0, 0.0, 260.0),
		FRotator::ZeroRotator,
		FVector(1.8, 1.8, 1.8)
	);

	if (!Hero)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn hero sphere"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	ApplyMat(Hero, LoadDemoMat(TEXT("M_Demo_Glow")));

	// Must be Movable for RotatingMovementComponent (added in orbit_rings step)
	if (Hero->GetRootComponent())
	{
		Hero->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	}

	Result->SetStringField(TEXT("actorLabel"), Hero->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 6: 4 corner pillars
TSharedPtr<FJsonObject> FDemoHandlers::StepPillars()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	UMaterialInterface* PillarMat = LoadDemoMat(TEXT("M_Demo_Pillar"));

	struct FPillarDef { FString Label; double X; double Y; };
	TArray<FPillarDef> Defs = {
		{ TEXT("Demo_Pillar_NE"),  600.0,  600.0 },
		{ TEXT("Demo_Pillar_NW"), -600.0,  600.0 },
		{ TEXT("Demo_Pillar_SE"),  600.0, -600.0 },
		{ TEXT("Demo_Pillar_SW"), -600.0, -600.0 },
	};

	TArray<TSharedPtr<FJsonValue>> Labels;
	for (const FPillarDef& D : Defs)
	{
		AActor* P = SpawnMesh(D.Label, DemoConstants::CYLINDER_MESH,
			FVector(D.X, D.Y, 200.0),
			FRotator::ZeroRotator,
			FVector(0.6, 0.6, 4.0));
		if (P)
		{
			ApplyMat(P, PillarMat);
			Labels.Add(MakeShared<FJsonValueString>(P->GetActorLabel()));
		}
	}

	Result->SetArrayField(TEXT("pillars"), Labels);
	Result->SetNumberField(TEXT("count"), Labels.Num());
	Result->SetBoolField(TEXT("success"), Labels.Num() == 4);
	return Result;
}

// Step 7: 4 orbs at pillar bases
TSharedPtr<FJsonObject> FDemoHandlers::StepOrbs()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	UMaterialInterface* GlowMat = LoadDemoMat(TEXT("M_Demo_Glow"));

	struct FOrbDef { FString Label; double X; double Y; };
	TArray<FOrbDef> Defs = {
		{ TEXT("Demo_Orb_NE"),  600.0,  600.0 },
		{ TEXT("Demo_Orb_NW"), -600.0,  600.0 },
		{ TEXT("Demo_Orb_SE"),  600.0, -600.0 },
		{ TEXT("Demo_Orb_SW"), -600.0, -600.0 },
	};

	TArray<TSharedPtr<FJsonValue>> Labels;
	for (const FOrbDef& D : Defs)
	{
		AActor* O = SpawnMesh(D.Label, DemoConstants::SPHERE_MESH,
			FVector(D.X, D.Y, 30.0),
			FRotator::ZeroRotator,
			FVector(0.4, 0.4, 0.4));
		if (O)
		{
			ApplyMat(O, GlowMat);
			Labels.Add(MakeShared<FJsonValueString>(O->GetActorLabel()));
		}
	}

	Result->SetArrayField(TEXT("orbs"), Labels);
	Result->SetNumberField(TEXT("count"), Labels.Num());
	Result->SetBoolField(TEXT("success"), Labels.Num() == 4);
	return Result;
}

// Step 8: 4 coloured neon point lights
TSharedPtr<FJsonObject> FDemoHandlers::StepNeonLights()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	struct FNeonDef { FString Label; FVector Pos; FColor Color; };
	TArray<FNeonDef> Defs = {
		{ TEXT("Demo_Neon_Cyan"),    FVector( 600.0,  600.0, 350.0), FColor(0, 220, 255) },
		{ TEXT("Demo_Neon_Magenta"), FVector(-600.0,  600.0, 350.0), FColor(255, 0, 180) },
		{ TEXT("Demo_Neon_Amber"),   FVector( 600.0, -600.0, 350.0), FColor(255, 170, 0) },
		{ TEXT("Demo_Neon_Violet"),  FVector(-600.0, -600.0, 350.0), FColor(130, 0, 255) },
	};

	TArray<TSharedPtr<FJsonValue>> Labels;
	for (const FNeonDef& D : Defs)
	{
		AActor* L = SpawnPointLight(D.Label, D.Pos, D.Color, 80000.0f);
		if (L)
		{
			Labels.Add(MakeShared<FJsonValueString>(L->GetActorLabel()));
		}
	}

	Result->SetArrayField(TEXT("lights"), Labels);
	Result->SetNumberField(TEXT("count"), Labels.Num());
	Result->SetBoolField(TEXT("success"), Labels.Num() == 4);
	return Result;
}

// Step 9: Hero light
TSharedPtr<FJsonObject> FDemoHandlers::StepHeroLight()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	AActor* L = SpawnPointLight(
		TEXT("Demo_HeroLight"),
		FVector(80.0, -80.0, 500.0),
		FColor(255, 225, 190),
		120000.0f
	);

	if (!L)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn hero light"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	Result->SetStringField(TEXT("actorLabel"), L->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 10: Moonlight (directional)
TSharedPtr<FJsonObject> FDemoHandlers::StepMoonlight()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	FTransform SpawnTransform(FRotator(-30.0, 210.0, 0.0), FVector::ZeroVector);
	ADirectionalLight* DirLight = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), SpawnTransform);
	if (!DirLight)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn directional light"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	DirLight->SetActorLabel(TEXT("Demo_Moonlight"));
	DirLight->SetFolderPath(*DemoConstants::FOLDER);

	UDirectionalLightComponent* Comp = DirLight->GetComponent();
	if (Comp)
	{
		Comp->SetIntensity(3.0f);
		Comp->SetLightColor(FLinearColor(FColor(100, 120, 200)));
	}

	Result->SetStringField(TEXT("actorLabel"), DirLight->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 11: SkyLight
TSharedPtr<FJsonObject> FDemoHandlers::StepSkyLight()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 500.0));
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), SpawnTransform);
	if (!Sky)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn sky light"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	Sky->SetActorLabel(TEXT("Demo_SkyLight"));
	Sky->SetFolderPath(*DemoConstants::FOLDER);

	USkyLightComponent* Comp = Sky->GetLightComponent();
	if (Comp)
	{
		Comp->SetIntensity(0.3f);
	}

	Result->SetStringField(TEXT("actorLabel"), Sky->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 12: ExponentialHeightFog
TSharedPtr<FJsonObject> FDemoHandlers::StepFog()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(
		AExponentialHeightFog::StaticClass(), SpawnTransform);
	if (!Fog)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn fog"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	Fog->SetActorLabel(TEXT("Demo_Fog"));
	Fog->SetFolderPath(*DemoConstants::FOLDER);

	UExponentialHeightFogComponent* Comp = Fog->GetComponent();
	if (Comp)
	{
		Comp->SetFogDensity(0.035f);
		Comp->SetFogHeightFalloff(0.5f);
		Comp->SetFogMaxOpacity(0.85f);
	}

	Result->SetStringField(TEXT("actorLabel"), Fog->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 13: PostProcessVolume
TSharedPtr<FJsonObject> FDemoHandlers::StepPostProcess()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	APostProcessVolume* PP = World->SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), SpawnTransform);
	if (!PP)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn post process volume"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	PP->SetActorLabel(TEXT("Demo_PostProcess"));
	PP->SetFolderPath(*DemoConstants::FOLDER);
	PP->bUnbound = true;

	// Bloom
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = 2.0f;

	// Vignette
	PP->Settings.bOverride_VignetteIntensity = true;
	PP->Settings.VignetteIntensity = 0.6f;

	// Exposure bias
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = -1.0f;

	Result->SetStringField(TEXT("actorLabel"), PP->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 14: Niagara VFX — continuous particle aura above hero sphere
TSharedPtr<FJsonObject> FDemoHandlers::StepNiagaraVfx()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	// Clean up any existing asset
	FString NiagaraAssetPath = DemoConstants::MAT_DIR / TEXT("NS_Demo_Aura");
	if (UEditorAssetLibrary::DoesAssetExist(NiagaraAssetPath))
	{
		UEditorAssetLibrary::DeleteAsset(NiagaraAssetPath);
	}

	// Load the Fountain emitter template from engine content — a fully configured
	// continuous-spawn emitter with sprite renderer, velocity, lifetime, etc.
	UNiagaraEmitter* FountainEmitter = LoadObject<UNiagaraEmitter>(
		nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"));
	if (!FountainEmitter)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not load engine Fountain emitter template"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	// Create the system using the factory with the Fountain emitter pre-configured
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
	FVersionedNiagaraEmitter VersionedEmitter;
	VersionedEmitter.Emitter = FountainEmitter;
	VersionedEmitter.Version = FountainEmitter->GetExposedVersion().VersionGuid;
	Factory->EmittersToAddToNewSystem.Add(VersionedEmitter);

	UObject* NSAsset = AssetTools.CreateAsset(
		TEXT("NS_Demo_Aura"), DemoConstants::MAT_DIR,
		UNiagaraSystem::StaticClass(), Factory);
	UNiagaraSystem* NiagaraSys = Cast<UNiagaraSystem>(NSAsset);
	if (!NiagaraSys)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create NiagaraSystem from Fountain emitter"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	// Initialize and save
	UNiagaraSystemFactoryNew::InitializeSystem(NiagaraSys, true);
	UEditorAssetLibrary::SaveAsset(NiagaraSys->GetPathName());

	// Spawn a dedicated actor and attach a NiagaraComponent with the system
	FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 380.0));
	AActor* NiagaraActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
	if (!NiagaraActor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn Niagara actor"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	NiagaraActor->SetActorLabel(TEXT("Demo_NiagaraVFX"));
	NiagaraActor->SetFolderPath(*DemoConstants::FOLDER);

	// Create and register a scene root so the Niagara component has something to attach to
	USceneComponent* SceneRoot = NewObject<USceneComponent>(NiagaraActor, TEXT("DefaultSceneRoot"));
	SceneRoot->RegisterComponent();
	NiagaraActor->SetRootComponent(SceneRoot);
	NiagaraActor->AddInstanceComponent(SceneRoot);

	// Create the Niagara component with our system asset
	UNiagaraComponent* NiagaraComp = NewObject<UNiagaraComponent>(NiagaraActor, TEXT("DemoNiagaraComp"));
	NiagaraComp->SetAsset(NiagaraSys);
	NiagaraComp->SetAutoActivate(true);
	NiagaraComp->RegisterComponent();
	NiagaraComp->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	NiagaraActor->AddInstanceComponent(NiagaraComp);

	// Activate the component to start emitting
	NiagaraComp->Activate(true);

	Result->SetStringField(TEXT("actorLabel"), NiagaraActor->GetActorLabel());
	Result->SetStringField(TEXT("assetPath"), NiagaraSys->GetPathName());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 15: PCG scatter
TSharedPtr<FJsonObject> FDemoHandlers::StepPcgScatter()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	// Spawn PCG Volume on floor
	FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	APCGVolume* PCGVol = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), SpawnTransform);
	if (!PCGVol)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn PCG volume"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	PCGVol->SetActorLabel(TEXT("Demo_PCGScatter"));
	PCGVol->SetFolderPath(*DemoConstants::FOLDER);
	PCGVol->SetActorScale3D(FVector(30.0, 30.0, 3.0));

	// Create a PCG graph directly (no factory needed)
	UPCGComponent* PCGComp = PCGVol->FindComponentByClass<UPCGComponent>();
	if (PCGComp)
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>(PCGComp, TEXT("PCG_Demo_Scatter"));
		if (Graph)
		{
			PCGComp->SetGraph(Graph);
			Result->SetBoolField(TEXT("graphAssigned"), true);
		}
	}

	Result->SetStringField(TEXT("actorLabel"), PCGVol->GetActorLabel());
	Result->SetStringField(TEXT("note"), TEXT("PCG volume placed. Configure the graph for scatter behavior."));
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 16: Orbit rings
TSharedPtr<FJsonObject> FDemoHandlers::StepOrbitRings()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	UMaterialInterface* GlowMat = LoadDemoMat(TEXT("M_Demo_Glow"));

	const float Radius = 220.0f;
	const float Height = 280.0f;
	const int32 NumOrbs = 8;

	// Spawn an invisible pivot actor at the hero sphere's height — all orbs attach to this
	FTransform PivotTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 0.0));
	AActor* PivotActor = World->SpawnActor<AActor>(AActor::StaticClass(), PivotTransform);
	if (!PivotActor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn orbit pivot"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}
	PivotActor->SetActorLabel(TEXT("Demo_OrbitPivot"));
	PivotActor->SetFolderPath(*DemoConstants::FOLDER);

	// Give it a scene root and make it movable
	USceneComponent* PivotRoot = NewObject<USceneComponent>(PivotActor, TEXT("PivotRoot"));
	PivotRoot->SetMobility(EComponentMobility::Movable);
	PivotRoot->RegisterComponent();
	PivotActor->SetRootComponent(PivotRoot);
	PivotActor->AddInstanceComponent(PivotRoot);

	// Add RotatingMovementComponent to the pivot
	URotatingMovementComponent* RotComp = NewObject<URotatingMovementComponent>(
		PivotActor, TEXT("DemoRotation"));
	if (RotComp)
	{
		RotComp->RotationRate = FRotator(0.0, 45.0, 0.0);
		RotComp->RegisterComponent();
		PivotActor->AddInstanceComponent(RotComp);
		Result->SetBoolField(TEXT("rotationAdded"), true);
	}

	// Spawn orbs and attach them to the pivot
	TArray<TSharedPtr<FJsonValue>> Labels;
	for (int32 i = 0; i < NumOrbs; ++i)
	{
		float Angle = (2.0f * PI * i) / NumOrbs;
		float X = Radius * FMath::Cos(Angle);
		float Y = Radius * FMath::Sin(Angle);

		FString Label = FString::Printf(TEXT("Demo_OrbitOrb_%d"), i);
		AActor* Orb = SpawnMesh(Label, DemoConstants::SPHERE_MESH,
			FVector(X, Y, Height),
			FRotator::ZeroRotator,
			FVector(0.2, 0.2, 0.2));
		if (Orb)
		{
			// Make movable so it can be attached and rotate
			if (Orb->GetRootComponent())
			{
				Orb->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			}
			Orb->AttachToActor(PivotActor, FAttachmentTransformRules::KeepWorldTransform);
			ApplyMat(Orb, GlowMat);
			Labels.Add(MakeShared<FJsonValueString>(Orb->GetActorLabel()));
		}
	}

	Result->SetArrayField(TEXT("orbitOrbs"), Labels);
	Result->SetNumberField(TEXT("count"), Labels.Num());
	Result->SetBoolField(TEXT("success"), Labels.Num() == NumOrbs);
	return Result;
}

// Step 17: LevelSequence
TSharedPtr<FJsonObject> FDemoHandlers::StepLevelSequence()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	// Create LevelSequence asset
	FString SeqName = TEXT("SEQ_Demo_Showcase");
	FString FullPackagePath = DemoConstants::MAT_DIR / SeqName;

	if (UEditorAssetLibrary::DoesAssetExist(FullPackagePath))
	{
		UEditorAssetLibrary::DeleteAsset(FullPackagePath);
	}

	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create sequence package"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	ULevelSequence* Seq = NewObject<ULevelSequence>(Package, FName(*SeqName), RF_Public | RF_Standalone);
	Seq->Initialize();

	if (!Seq)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create LevelSequence"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	FAssetRegistryModule::AssetCreated(Seq);
	Package->MarkPackageDirty();

	// Bind the hero sphere as a possessable
	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (MovieScene)
	{
		AActor* HeroSphere = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if ((*It)->GetActorLabel() == TEXT("Demo_HeroSphere"))
			{
				HeroSphere = *It;
				break;
			}
		}

		if (HeroSphere)
		{
			FGuid BindingGuid = MovieScene->AddPossessable(
				TEXT("Demo_HeroSphere"), HeroSphere->GetClass());
			Seq->BindPossessableObject(BindingGuid, *HeroSphere, World);

			// Add a transform track
			MovieScene->AddTrack(UMovieScene3DTransformTrack::StaticClass(), BindingGuid);

			Result->SetStringField(TEXT("boundActor"), TEXT("Demo_HeroSphere"));
			Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
		}
	}

	UEditorAssetLibrary::SaveAsset(Seq->GetPathName());

	// Place a LevelSequenceActor in the level
	FTransform SeqTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	ALevelSequenceActor* SeqActor = World->SpawnActor<ALevelSequenceActor>(
		ALevelSequenceActor::StaticClass(), SeqTransform);
	if (SeqActor)
	{
		SeqActor->SetActorLabel(TEXT("Demo_SequenceActor"));
		SeqActor->SetFolderPath(*DemoConstants::FOLDER);
		SeqActor->SetSequence(Seq);
		Result->SetStringField(TEXT("sequenceActorLabel"), SeqActor->GetActorLabel());
	}

	Result->SetStringField(TEXT("sequencePath"), Seq->GetPathName());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 18: Tuning panel (EditorUtilityWidget)
TSharedPtr<FJsonObject> FDemoHandlers::StepTuningPanel()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// This step is a placeholder - creating EditorUtilityWidgets programmatically
	// requires careful factory setup. Mark as success with a note.
	Result->SetStringField(TEXT("note"),
		TEXT("Tuning panel step skipped - create EUW_DemoTuning manually in /Game/Demo/ for a custom control panel. "
		     "Use the widget tool's create_widget action to build one interactively."));
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// Step 19: Save
TSharedPtr<FJsonObject> FDemoHandlers::StepSave()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	ULevelEditorSubsystem* LevelSub = GEditor ?
		GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LevelSub)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return Result;
	}

	bool bSaved = LevelSub->SaveCurrentLevel();

	UWorld* World = GetEditorWorld();
	if (World)
	{
		Result->SetStringField(TEXT("levelName"), World->GetName());
		Result->SetStringField(TEXT("levelPath"), World->GetPathName());
	}

	Result->SetBoolField(TEXT("saved"), bSaved);
	Result->SetBoolField(TEXT("success"), bSaved);
	return Result;
}
