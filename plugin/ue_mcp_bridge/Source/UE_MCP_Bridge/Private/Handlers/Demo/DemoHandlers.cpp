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
#include "HAL/FileManager.h"

// Assets / Packages
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace DemoConstants
{
	static const FString MAT_DIR     = TEXT("/Game/Demo");
	static const FString DEMO_LEVEL  = TEXT("/Game/Demo/DemoLevel");
	static const FString HOME_LEVEL  = TEXT("/Game/MCP_Home");
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
// The steps themselves are the server's demo_step_N flows (src/tools/demo-steps.ts);
// what stays here is the step list, the cleanup and the home level.
void FDemoHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("demo"));

	Registry.RegisterHandler(TEXT("demo_get_steps"), &DemoGetSteps, {});
	Registry.RegisterHandler(TEXT("demo_cleanup"), &DemoCleanup, TArray<FMCPParamSpec>(),
		MCPSpec::ContractExempt(TEXT("Takes no parameters and acts unconditionally: switches levels and deletes the demo scene")));
	Registry.RegisterHandler(TEXT("demo_go_home"), &DemoGoHome, TArray<FMCPParamSpec>(),
		MCPSpec::ContractExempt(TEXT("Takes no parameters and acts unconditionally: creates the home level if missing and opens it")));
}

// Ensures /Game/MCP_Home exists on disk and loads it. Idempotent.
bool FDemoHandlers::EnsureHomeLevelLoaded(FString& OutError)
{
	ULevelEditorSubsystem* LevelSub = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LevelSub) { OutError = TEXT("LevelEditorSubsystem not available"); return false; }

	if (!UEditorAssetLibrary::DoesAssetExist(DemoConstants::HOME_LEVEL))
	{
		// Create + save a blank level on disk so subsequent loads have a
		// real package to anchor on (no Untitled state).
		if (!LevelSub->NewLevel(DemoConstants::HOME_LEVEL))
		{
			OutError = FString::Printf(TEXT("NewLevel failed for %s"), *DemoConstants::HOME_LEVEL);
			return false;
		}
		if (!LevelSub->SaveCurrentLevel())
		{
			OutError = FString::Printf(TEXT("%s was created but could not be saved"), *DemoConstants::HOME_LEVEL);
			return false;
		}
	}
	else
	{
		LevelSub->LoadLevel(DemoConstants::HOME_LEVEL);
	}
	return true;
}

// demo_go_home: switch the editor to /Game/MCP_Home (creating it on first use).
TSharedPtr<FJsonValue> FDemoHandlers::DemoGoHome(const TSharedPtr<FJsonObject>& Params)
{
	// What was open before the swap, and whether the home level had to be
	// created. Both are read here rather than inside EnsureHomeLevelLoaded,
	// which demo_cleanup also calls and which must keep reporting nothing.
	const bool bHomeExisted = UEditorAssetLibrary::DoesAssetExist(DemoConstants::HOME_LEVEL);
	FString PreviousLevelPath;
	if (UWorld* PreviousWorld = GetEditorWorld())
	{
		if (UPackage* PreviousPackage = PreviousWorld->GetOutermost())
		{
			const FString PreviousName = PreviousPackage->GetName();
			// A world under /Temp/ is an unsaved map that no path can reopen,
			// which is the same rule level(load) applies to its own inverse.
			if (PreviousName.StartsWith(TEXT("/Game/")) || PreviousName.StartsWith(TEXT("/Engine/")))
			{
				PreviousLevelPath = PreviousName;
			}
		}
	}

	FString Err;
	if (!EnsureHomeLevelLoaded(Err))
	{
		return MCPError(Err);
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelPath"), DemoConstants::HOME_LEVEL);
	Result->SetStringField(TEXT("previousLevelPath"), PreviousLevelPath);
	Result->SetBoolField(TEXT("homeLevelCreated"), !bHomeExisted);
	// Idempotency: already standing in the home level means this opened nothing.
	const bool bAlreadyHome = PreviousLevelPath == DemoConstants::HOME_LEVEL;
	Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyHome);

	if (!bAlreadyHome && !PreviousLevelPath.IsEmpty())
	{
		// "Open the home level" inverts to opening the level that was open,
		// which is exactly what level(load) does and what its own rollback
		// record carries.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelPath"), PreviousLevelPath);
		MCPSetRollback(Result, TEXT("load_level"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), !bHomeExisted);
		if (!bHomeExisted)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The previous level is reopened, but '%s' was created on disk by this call and stays there. "
					 "Delete it with asset(delete) as well if the rollback has to leave no trace."),
				*DemoConstants::HOME_LEVEL));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bAlreadyHome
			? TEXT("The home level was already open, so nothing changed and there is nothing to undo.")
			: TEXT("The level that was open has no content path to reopen - an unsaved or Untitled map - so no "
				   "inverse can name it."));
	}
	return MCPResult(Result);
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
// Handler: demo_cleanup
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FDemoHandlers::DemoCleanup(const TSharedPtr<FJsonObject>& Params)
{
	// Anchor the editor to the saved home level FIRST. Otherwise deleting
	// the demo level under it leaves the editor on an Untitled map and
	// every subsequent action triggers a "save Untitled?" dialog.
	//
	// That anchoring is itself a change to the project and to the editor, so it
	// is measured here: a cleanup that created the home level and moved the
	// editor into it did something, whatever the delete counts say.
	const bool bHomeExisted = UEditorAssetLibrary::DoesAssetExist(DemoConstants::HOME_LEVEL);
	FString PreviousLevelPath;
	if (UWorld* PreviousWorld = GetEditorWorld())
	{
		if (UPackage* PreviousPackage = PreviousWorld->GetOutermost())
		{
			PreviousLevelPath = PreviousPackage->GetName();
		}
	}
	const bool bLevelSwitched = PreviousLevelPath != DemoConstants::HOME_LEVEL;
	// Deleting the demo level while the editor still stands in it is the
	// Untitled-map trap described above, so a failed anchor stops the cleanup.
	{
		FString HomeErr;
		if (!EnsureHomeLevelLoaded(HomeErr))
		{
			return MCPError(FString::Printf(
				TEXT("Cleanup stopped before deleting anything: the editor could not be moved to %s (%s)."),
				*DemoConstants::HOME_LEVEL, *HomeErr));
		}
	}
	// The demo world stays in memory after the switch until a collection runs,
	// and a loaded world cannot be deleted. Collect twice, as load_level does.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);

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

	// 3) The DemoLevel goes last, after the assets placed in it.
	AssetsToDelete.Add(DemoConstants::DEMO_LEVEL);

	int32 AssetsDeleted = 0;
	TArray<FString> FailedDeletes;
	for (const FString& AssetPath : AssetsToDelete)
	{
		if (!UEditorAssetLibrary::DoesAssetExist(AssetPath)) continue;
		if (UEditorAssetLibrary::DeleteAsset(AssetPath))
		{
			++AssetsDeleted;
		}
		else
		{
			FailedDeletes.Add(AssetPath);
		}
	}
	// DeleteAsset reports success for the level once it is gone from memory,
	// but the .umap can stay on disk, and the next run's step 1 reopens it.
	const FString DemoLevelFile = FPaths::ConvertRelativePathToFull(
		FPackageName::LongPackageNameToFilename(DemoConstants::DEMO_LEVEL, FPackageName::GetMapPackageExtension()));
	if (IFileManager::Get().FileExists(*DemoLevelFile))
	{
		if (UPackage* Loaded = FindPackage(nullptr, *DemoConstants::DEMO_LEVEL))
		{
			ResetLoaders(Loaded);
		}
		if (IFileManager::Get().Delete(*DemoLevelFile, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true))
		{
			FAssetRegistryModule::GetRegistry().ScanModifiedAssetFiles({ DemoLevelFile });
		}
		else
		{
			FailedDeletes.AddUnique(DemoConstants::DEMO_LEVEL);
		}
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
	Result->SetStringField(TEXT("previousLevelPath"), PreviousLevelPath);
	Result->SetBoolField(TEXT("homeLevelCreated"), !bHomeExisted);
	Result->SetBoolField(TEXT("levelSwitched"), bLevelSwitched);
	// A demo level left on disk is reopened by the next run's step 1, which
	// would then spawn a second set of Demo_ actors beside the first.
	Result->SetBoolField(TEXT("demoLevelRemoved"), !UEditorAssetLibrary::DoesAssetExist(DemoConstants::DEMO_LEVEL));
	if (FailedDeletes.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetArrayField(TEXT("failedDeletes"), MCPStringListToJson(FailedDeletes));
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d demo asset(s) could not be deleted: %s. They may be referenced or read-only."),
			FailedDeletes.Num(), *FString::Join(FailedDeletes, TEXT(", "))));
	}
	// Idempotency, counting the anchoring as well as the deletes: cleaning an
	// already-clean project while already standing in the home level does
	// nothing at all. Creating that level, or moving the editor into it, is a
	// change even when nothing was deleted.
	Result->SetBoolField(TEXT("unchanged"),
		ActorsDeleted == 0 && AssetsDeleted == 0 && bHomeExisted && !bLevelSwitched);

	// No inverse. The demo scene comes back by running demo(step) 1 through 19
	// again, which is nineteen calls rather than one, and nothing restores the
	// deleted packages in place - they are rebuilt from scratch.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Cleanup deletes the demo level, its assets and its actors. The scene is rebuilt by running demo(step) "
			 "1 through 19 again, which is nineteen calls rather than one inverse, and nothing restores the deleted "
			 "packages themselves."));

	return MCPResult(Result);
}
