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
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "NavModifierVolume.h"
#include "Engine/WorldSettings.h"
#include "UObject/UnrealType.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "EnhancedInput/EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/NavMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"

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
	Registry.RegisterHandler(TEXT("set_collision_profile"), &SetCollisionProfile);
	Registry.RegisterHandler(TEXT("set_physics_enabled"), &SetPhysicsEnabled);
	Registry.RegisterHandler(TEXT("set_collision_type"), &SetCollisionType);
	Registry.RegisterHandler(TEXT("set_body_properties"), &SetBodyProperties);
	Registry.RegisterHandler(TEXT("spawn_nav_modifier_volume"), &SpawnNavModifierVolume);
	Registry.RegisterHandler(TEXT("rebuild_navmesh"), &RebuildNavmesh);
	Registry.RegisterHandler(TEXT("get_cdo_defaults"), &GetCdoDefaults);
	Registry.RegisterHandler(TEXT("set_world_game_mode"), &SetWorldGameMode);
	Registry.RegisterHandler(TEXT("create_ai_perception_config"), &CreateAiPerceptionConfig);
	Registry.RegisterHandler(TEXT("add_blackboard_key"), &AddBlackboardKey);
	Registry.RegisterHandler(TEXT("setup_enhanced_input"), &SetupEnhancedInput);
	Registry.RegisterHandler(TEXT("configure_behavior_tree"), &ConfigureBehaviorTree);
	Registry.RegisterHandler(TEXT("setup_path_following"), &SetupPathFollowing);
	Registry.RegisterHandler(TEXT("run_eqs_query"), &RunEqsQuery);
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

TSharedPtr<FJsonValue> FGameplayHandlers::SetCollisionProfile(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ProfileName;
	if (!Params->TryGetStringField(TEXT("profileName"), ProfileName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'profileName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			FoundActor = *ActorIt;
			break;
		}
	}

	if (!FoundActor)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set collision profile on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetCollisionProfileName(FName(*ProfileName));
			ComponentsUpdated++;
		}
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("profileName"), ProfileName);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bEnabled = true;
	Params->TryGetBoolField(TEXT("enabled"), bEnabled);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			FoundActor = *ActorIt;
			break;
		}
	}

	if (!FoundActor)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set physics simulation on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetSimulatePhysics(bEnabled);
			ComponentsUpdated++;
		}
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetCollisionType(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString CollisionType;
	if (!Params->TryGetStringField(TEXT("collisionType"), CollisionType))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'collisionType' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Map string to ECollisionEnabled::Type
	ECollisionEnabled::Type CollisionEnabled;
	if (CollisionType == TEXT("NoCollision"))
	{
		CollisionEnabled = ECollisionEnabled::NoCollision;
	}
	else if (CollisionType == TEXT("QueryOnly"))
	{
		CollisionEnabled = ECollisionEnabled::QueryOnly;
	}
	else if (CollisionType == TEXT("PhysicsOnly"))
	{
		CollisionEnabled = ECollisionEnabled::PhysicsOnly;
	}
	else if (CollisionType == TEXT("QueryAndPhysics"))
	{
		CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid collisionType: %s. Use NoCollision, QueryOnly, PhysicsOnly, or QueryAndPhysics"), *CollisionType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			FoundActor = *ActorIt;
			break;
		}
	}

	if (!FoundActor)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set collision enabled on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetCollisionEnabled(CollisionEnabled);
			ComponentsUpdated++;
		}
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("collisionType"), CollisionType);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetBodyProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			FoundActor = *ActorIt;
			break;
		}
	}

	if (!FoundActor)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set body properties on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	double Mass = -1.0;
	double LinearDamping = -1.0;
	double AngularDamping = -1.0;
	bool bHasGravityParam = false;
	bool bEnableGravity = true;

	Params->TryGetNumberField(TEXT("mass"), Mass);
	Params->TryGetNumberField(TEXT("linearDamping"), LinearDamping);
	Params->TryGetNumberField(TEXT("angularDamping"), AngularDamping);
	bHasGravityParam = Params->TryGetBoolField(TEXT("enableGravity"), bEnableGravity);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			if (Mass >= 0.0)
			{
				PrimComp->BodyInstance.SetMassOverride(Mass);
			}
			if (LinearDamping >= 0.0)
			{
				PrimComp->SetLinearDamping(LinearDamping);
			}
			if (AngularDamping >= 0.0)
			{
				PrimComp->SetAngularDamping(AngularDamping);
			}
			if (bHasGravityParam)
			{
				PrimComp->SetEnableGravity(bEnableGravity);
			}
			ComponentsUpdated++;
		}
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get location
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		Location.X = (*LocationObj)->GetNumberField(TEXT("x"));
		Location.Y = (*LocationObj)->GetNumberField(TEXT("y"));
		Location.Z = (*LocationObj)->GetNumberField(TEXT("z"));
	}

	// Get scale
	FVector Scale = FVector::OneVector;
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
	{
		Scale.X = (*ScaleObj)->GetNumberField(TEXT("x"));
		Scale.Y = (*ScaleObj)->GetNumberField(TEXT("y"));
		Scale.Z = (*ScaleObj)->GetNumberField(TEXT("z"));
	}

	FTransform SpawnTransform;
	SpawnTransform.SetLocation(Location);
	SpawnTransform.SetScale3D(Scale);

	ANavModifierVolume* NewVolume = World->SpawnActor<ANavModifierVolume>(ANavModifierVolume::StaticClass(), SpawnTransform);
	if (!NewVolume)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn NavModifierVolume"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		NewVolume->SetActorLabel(Label);
	}

	Result->SetStringField(TEXT("actorLabel"), NewVolume->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());

	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	FVector ActorLocation = NewVolume->GetActorLocation();
	LocationResult->SetNumberField(TEXT("x"), ActorLocation.X);
	LocationResult->SetNumberField(TEXT("y"), ActorLocation.Y);
	LocationResult->SetNumberField(TEXT("z"), ActorLocation.Z);
	Result->SetObjectField(TEXT("location"), LocationResult);

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RebuildNavmesh(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Trigger navmesh rebuild via console command
	GEditor->Exec(World, TEXT("RebuildNavigation"));

	Result->SetStringField(TEXT("status"), TEXT("rebuild_triggered"));
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetCdoDefaults(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ClassName;
	if (!Params->TryGetStringField(TEXT("className"), ClassName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'className' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Try to find the class by name - support both short names and full paths
	UClass* FoundClass = nullptr;

	// Try full path first (e.g. "/Script/Engine.Actor")
	FoundClass = FindObject<UClass>(nullptr, *ClassName);

	// If not found, search by short name
	if (!FoundClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == ClassName)
			{
				FoundClass = *It;
				break;
			}
		}
	}

	if (!FoundClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Class not found: %s"), *ClassName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* CDO = FoundClass->GetDefaultObject();
	if (!CDO)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Could not get CDO for class: %s"), *ClassName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("className"), FoundClass->GetName());
	Result->SetStringField(TEXT("classPath"), FoundClass->GetPathName());

	// Iterate properties and collect their default values
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;
	for (TFieldIterator<FProperty> PropIt(FoundClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Property->GetName());
		PropObj->SetStringField(TEXT("type"), Property->GetCPPType());
		PropObj->SetStringField(TEXT("class"), Property->GetOwnerClass() ? Property->GetOwnerClass()->GetName() : TEXT("Unknown"));

		// Get string representation of the default value
		FString ValueStr;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
		Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
		PropObj->SetStringField(TEXT("defaultValue"), ValueStr);

		PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("propertyCount"), PropertiesArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetWorldGameMode(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString GameModeClassPath;
	if (!Params->TryGetStringField(TEXT("gameModeClass"), GameModeClassPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'gameModeClass' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Try to find the game mode class - support blueprint paths ending with _C
	UClass* GameModeClass = nullptr;

	// Try loading as a blueprint class first (common case for user BPs)
	GameModeClass = LoadObject<UClass>(nullptr, *GameModeClassPath);

	// If not found, try appending _C for blueprint paths
	if (!GameModeClass && !GameModeClassPath.EndsWith(TEXT("_C")))
	{
		FString BlueprintClassPath = GameModeClassPath + TEXT("_C");
		GameModeClass = LoadObject<UClass>(nullptr, *BlueprintClassPath);
	}

	// Try FindObject as fallback
	if (!GameModeClass)
	{
		GameModeClass = FindObject<UClass>(nullptr, *GameModeClassPath);
	}

	if (!GameModeClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("GameMode class not found: %s"), *GameModeClassPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!GameModeClass->IsChildOf(AGameModeBase::StaticClass()))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' is not a GameModeBase subclass"), *GameModeClassPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (!WorldSettings)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not get WorldSettings"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	WorldSettings->DefaultGameMode = GameModeClass;
	WorldSettings->MarkPackageDirty();

	Result->SetStringField(TEXT("gameModeClass"), GameModeClass->GetPathName());
	Result->SetStringField(TEXT("gameModeName"), GameModeClass->GetName());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateAiPerceptionConfig(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// The blueprint must be an Actor-based blueprint to add components
	if (!Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
	{
		Result->SetStringField(TEXT("error"), TEXT("Blueprint must be based on AActor to add perception component"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Read which senses to configure
	bool bAddSight = true;
	bool bAddHearing = false;
	bool bAddDamage = false;
	Params->TryGetBoolField(TEXT("addSight"), bAddSight);
	Params->TryGetBoolField(TEXT("addHearing"), bAddHearing);
	Params->TryGetBoolField(TEXT("addDamage"), bAddDamage);

	// Add AIPerceptionComponent via the SCS (SimpleConstructionScript)
	USCS_Node* PerceptionNode = Blueprint->SimpleConstructionScript->CreateNode(UAIPerceptionComponent::StaticClass(), TEXT("AIPerceptionComp"));
	if (!PerceptionNode)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create AIPerceptionComponent node"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Blueprint->SimpleConstructionScript->AddNode(PerceptionNode);

	UAIPerceptionComponent* PerceptionComp = Cast<UAIPerceptionComponent>(PerceptionNode->ComponentTemplate);
	if (!PerceptionComp)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to get AIPerceptionComponent template"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<TSharedPtr<FJsonValue>> SensesAdded;

	// Configure sight sense
	if (bAddSight)
	{
		UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(PerceptionComp, TEXT("SightConfig"));
		SightConfig->SightRadius = 3000.0f;
		SightConfig->LoseSightRadius = 3500.0f;
		SightConfig->PeripheralVisionAngleDegrees = 90.0f;
		PerceptionComp->ConfigureSense(*SightConfig);
		SensesAdded.Add(MakeShared<FJsonValueString>(TEXT("Sight")));
	}

	// Configure hearing sense
	if (bAddHearing)
	{
		UAISenseConfig_Hearing* HearingConfig = NewObject<UAISenseConfig_Hearing>(PerceptionComp, TEXT("HearingConfig"));
		HearingConfig->HearingRange = 3000.0f;
		PerceptionComp->ConfigureSense(*HearingConfig);
		SensesAdded.Add(MakeShared<FJsonValueString>(TEXT("Hearing")));
	}

	// Configure damage sense
	if (bAddDamage)
	{
		UAISenseConfig_Damage* DamageConfig = NewObject<UAISenseConfig_Damage>(PerceptionComp, TEXT("DamageConfig"));
		PerceptionComp->ConfigureSense(*DamageConfig);
		SensesAdded.Add(MakeShared<FJsonValueString>(TEXT("Damage")));
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("componentName"), TEXT("AIPerceptionComp"));
	Result->SetArrayField(TEXT("sensesConfigured"), SensesAdded);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddBlackboardKey(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlackboardPath;
	if (!Params->TryGetStringField(TEXT("blackboardPath"), BlackboardPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blackboardPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString KeyName;
	if (!Params->TryGetStringField(TEXT("keyName"), KeyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'keyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString KeyType = TEXT("Bool");
	Params->TryGetStringField(TEXT("keyType"), KeyType);

	UBlackboardData* BlackboardAsset = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!BlackboardAsset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Determine the key type class
	UBlackboardKeyType* KeyTypeInstance = nullptr;
	if (KeyType == TEXT("Bool"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Bool>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Int"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Int>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Float"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Float>(BlackboardAsset);
	}
	else if (KeyType == TEXT("String"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_String>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Name"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Name>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Object"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Object>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Class"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Class>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Enum"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Enum>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Vector"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Vector>(BlackboardAsset);
	}
	else if (KeyType == TEXT("Rotator"))
	{
		KeyTypeInstance = NewObject<UBlackboardKeyType_Rotator>(BlackboardAsset);
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown key type: %s. Supported: Bool, Int, Float, String, Name, Object, Class, Enum, Vector, Rotator"), *KeyType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Add the new key entry
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);
	NewEntry.KeyType = KeyTypeInstance;

	BlackboardAsset->Keys.Add(NewEntry);
	BlackboardAsset->MarkPackageDirty();

	// Save
	UEditorAssetLibrary::SaveAsset(BlackboardAsset->GetPathName());

	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	Result->SetStringField(TEXT("keyName"), KeyName);
	Result->SetStringField(TEXT("keyType"), KeyType);
	Result->SetNumberField(TEXT("totalKeys"), BlackboardAsset->Keys.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}
