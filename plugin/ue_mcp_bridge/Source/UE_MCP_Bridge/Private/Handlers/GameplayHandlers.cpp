#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/TopLevelAssetPath.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Editor.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/HUD.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "Factories/BlueprintFactory.h"

void FGameplayHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_smart_object_definition"), &CreateSmartObjectDefinition);
	Registry.RegisterHandler(TEXT("get_navmesh_info"), &GetNavmeshInfo);
	Registry.RegisterHandler(TEXT("get_game_framework_info"), &GetGameFrameworkInfo);
	Registry.RegisterHandler(TEXT("list_input_assets"), &ListInputAssets);
	Registry.RegisterHandler(TEXT("list_behavior_trees"), &ListBehaviorTrees);
	Registry.RegisterHandler(TEXT("list_eqs_queries"), &ListEqsQueries);
	Registry.RegisterHandler(TEXT("list_state_trees"), &ListStateTrees);
	Registry.RegisterHandler(TEXT("project_point_to_navigation"), &ProjectPointToNavigation);
	Registry.RegisterHandler(TEXT("create_input_action"), &CreateInputAction);
	Registry.RegisterHandler(TEXT("create_input_mapping_context"), &CreateInputMappingContext);
	Registry.RegisterHandler(TEXT("create_blackboard"), &CreateBlackboard);
	Registry.RegisterHandler(TEXT("create_behavior_tree"), &CreateBehaviorTree);
	Registry.RegisterHandler(TEXT("create_eqs_query"), &CreateEqsQuery);
	Registry.RegisterHandler(TEXT("create_state_tree"), &CreateStateTree);
	Registry.RegisterHandler(TEXT("create_game_mode"), &CreateGameMode);
	Registry.RegisterHandler(TEXT("create_game_state"), &CreateGameState);
	Registry.RegisterHandler(TEXT("create_player_controller"), &CreatePlayerController);
	Registry.RegisterHandler(TEXT("create_player_state"), &CreatePlayerState);
	Registry.RegisterHandler(TEXT("create_hud"), &CreateHud);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateSmartObjectDefinition(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/AI/SmartObjects");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Delete existing asset if it exists
	FString AssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(AssetPath);

	// Find SmartObjectDefinition class
	UClass* SmartObjectDefClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectDefinition"));
	if (!SmartObjectDefClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("SmartObjectDefinition class not found. Enable SmartObjects plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create asset
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	PackagePath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		AssetName = Name;
	}
	else
	{
		PackageName = PackagePath;
		AssetName = Name;
	}
	PackageName = PackageName.LeftChop(1); // Remove trailing /

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackageName, SmartObjectDefClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create SmartObjectDefinition"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetNavmeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		Result->SetStringField(TEXT("status"), TEXT("no_navigation_system"));
		Result->SetBoolField(TEXT("success"), true);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("status"), TEXT("active"));

	// Get nav data info
	TArray<TSharedPtr<FJsonValue>> NavDataArray;
	for (ANavigationData* NavData : NavSys->NavDataSet)
	{
		if (NavData)
		{
			TSharedPtr<FJsonObject> NavDataObj = MakeShared<FJsonObject>();
			NavDataObj->SetStringField(TEXT("name"), NavData->GetName());
			NavDataObj->SetStringField(TEXT("class"), NavData->GetClass()->GetName());

			NavDataArray.Add(MakeShared<FJsonValueObject>(NavDataObj));
		}
	}
	Result->SetArrayField(TEXT("navData"), NavDataArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetGameFrameworkInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Game mode
	AGameModeBase* GameMode = World->GetAuthGameMode();
	if (GameMode)
	{
		Result->SetStringField(TEXT("gameMode"), GameMode->GetClass()->GetName());
	}
	else
	{
		Result->SetStringField(TEXT("gameMode"), TEXT("none"));
	}

	// Game state
	AGameStateBase* GameState = World->GetGameState();
	if (GameState)
	{
		Result->SetStringField(TEXT("gameState"), GameState->GetClass()->GetName());
	}
	else
	{
		Result->SetStringField(TEXT("gameState"), TEXT("none"));
	}

	// Default player controller class
	if (GameMode)
	{
		TSubclassOf<APlayerController> PCClass = GameMode->PlayerControllerClass;
		if (PCClass)
		{
			Result->SetStringField(TEXT("playerControllerClass"), PCClass->GetName());
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListInputAssets(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// List InputAction assets
	TArray<FAssetData> InputActions;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), TEXT("InputAction")), InputActions, true);

	TArray<TSharedPtr<FJsonValue>> InputActionArray;
	for (const FAssetData& Asset : InputActions)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		InputActionArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}
	Result->SetArrayField(TEXT("inputActions"), InputActionArray);

	// List InputMappingContext assets
	TArray<FAssetData> MappingContexts;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), TEXT("InputMappingContext")), MappingContexts, true);

	TArray<TSharedPtr<FJsonValue>> MappingContextArray;
	for (const FAssetData& Asset : MappingContexts)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		MappingContextArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}
	Result->SetArrayField(TEXT("inputMappingContexts"), MappingContextArray);

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListBehaviorTrees(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("BehaviorTree")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}
	Result->SetArrayField(TEXT("behaviorTrees"), AssetArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListEqsQueries(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("EnvironmentQuery")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}
	Result->SetArrayField(TEXT("eqsQueries"), AssetArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListStateTrees(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/StateTreeModule"), TEXT("StateTree")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}
	Result->SetArrayField(TEXT("stateTrees"), AssetArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ProjectPointToNavigation(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'location' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FVector Point;
	Point.X = (*LocationObj)->GetNumberField(TEXT("x"));
	Point.Y = (*LocationObj)->GetNumberField(TEXT("y"));
	Point.Z = (*LocationObj)->GetNumberField(TEXT("z"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		Result->SetStringField(TEXT("error"), TEXT("No navigation system available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FNavLocation NavLocation;
	bool bProjected = NavSys->ProjectPointToNavigation(Point, NavLocation);

	Result->SetBoolField(TEXT("projected"), bProjected);
	if (bProjected)
	{
		TSharedPtr<FJsonObject> ProjectedPoint = MakeShared<FJsonObject>();
		ProjectedPoint->SetNumberField(TEXT("x"), NavLocation.Location.X);
		ProjectedPoint->SetNumberField(TEXT("y"), NavLocation.Location.Y);
		ProjectedPoint->SetNumberField(TEXT("z"), NavLocation.Location.Z);
		Result->SetObjectField(TEXT("projectedLocation"), ProjectedPoint);
	}
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateInputAction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Input");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* InputActionClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputAction"));
	if (!InputActionClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("InputAction class not found. Enable EnhancedInput plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, InputActionClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create InputAction"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateInputMappingContext(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Input");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* IMCClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputMappingContext"));
	if (!IMCClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("InputMappingContext class not found. Enable EnhancedInput plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, IMCClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create InputMappingContext"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/AI");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* BlackboardClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BlackboardData"));
	if (!BlackboardClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("BlackboardData class not found."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, BlackboardClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create BlackboardData"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/AI");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* BTClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BehaviorTree"));
	if (!BTClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("BehaviorTree class not found."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, BTClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create BehaviorTree"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/AI/EQS");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* EQSClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvironmentQuery"));
	if (!EQSClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("EnvironmentQuery class not found."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, EQSClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create EnvironmentQuery"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/AI");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* STClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
	if (!STClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("StateTree class not found. Enable StateTree plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, STClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create StateTree"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("%s class not found: %s"), *FriendlyTypeName, *ParentClassPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Delete existing asset if it exists
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create %s Blueprint"), *FriendlyTypeName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = ParentClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	// Save
	UPackage* Package = NewBlueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("type"), FriendlyTypeName);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameMode(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Blueprints/GameFramework");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.GameModeBase"), TEXT("GameMode"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameState(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Blueprints/GameFramework");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.GameStateBase"), TEXT("GameState"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerController(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Blueprints/GameFramework");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.PlayerController"), TEXT("PlayerController"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerState(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Blueprints/GameFramework");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.PlayerState"), TEXT("PlayerState"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateHud(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Blueprints/GameFramework");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.HUD"), TEXT("HUD"));
}
