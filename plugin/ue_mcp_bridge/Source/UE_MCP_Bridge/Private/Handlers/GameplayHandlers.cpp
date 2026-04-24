#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
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
#include "GameFramework/WorldSettings.h"
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
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Engine/SCS_Node.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/NavMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "EnhancedActionKeyMapping.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimNode_StateMachine.h"
#include "NavMesh/RecastNavMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"

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
	// Aliases
	Registry.RegisterHandler(TEXT("rebuild_navigation"), &RebuildNavmesh);
	// New handlers
	Registry.RegisterHandler(TEXT("get_behavior_tree_info"), &GetBehaviorTreeInfo);
	Registry.RegisterHandler(TEXT("read_behavior_tree_graph"), &ReadBehaviorTreeGraph);
	Registry.RegisterHandler(TEXT("add_perception_component"), &AddPerceptionComponent);
	Registry.RegisterHandler(TEXT("configure_ai_perception_sense"), &ConfigureAiPerceptionSense);
	Registry.RegisterHandler(TEXT("add_state_tree_component"), &AddStateTreeComponent);
	Registry.RegisterHandler(TEXT("add_smart_object_component"), &AddSmartObjectComponent);
	Registry.RegisterHandler(TEXT("read_imc"), &ReadImc);
	Registry.RegisterHandler(TEXT("list_imc_mappings"), &ReadImc);
	Registry.RegisterHandler(TEXT("add_imc_mapping"), &AddImcMapping);
	Registry.RegisterHandler(TEXT("set_mapping_modifiers"), &SetMappingModifiers);
	Registry.RegisterHandler(TEXT("remove_imc_mapping"), &RemoveImcMapping);
	Registry.RegisterHandler(TEXT("set_imc_mapping_key"), &SetImcMappingKey);
	Registry.RegisterHandler(TEXT("set_imc_mapping_action"), &SetImcMappingAction);
	Registry.RegisterHandler(TEXT("inspect_pie"), &InspectPie);
	Registry.RegisterHandler(TEXT("get_pie_anim_state"), &GetPieAnimState);
	Registry.RegisterHandler(TEXT("get_pie_anim_properties"), &GetPieAnimProperties);
	Registry.RegisterHandler(TEXT("get_pie_subsystem_state"), &GetPieSubsystemState);
	Registry.RegisterHandler(TEXT("get_navmesh_details"), &GetNavmeshDetails);
	Registry.RegisterHandler(TEXT("apply_damage_in_pie"), &ApplyDamageInPie);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateSmartObjectDefinition(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI/SmartObjects"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("SmartObjectDefinition")))
	{
		return Existing;
	}

	UClass* SmartObjectDefClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectDefinition"));
	if (!SmartObjectDefClass)
	{
		return MCPError(TEXT("SmartObjectDefinition class not found. Enable SmartObjects plugin."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, SmartObjectDefClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create SmartObjectDefinition"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetNavmeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("status"), TEXT("no_navigation_system"));
		return MCPResult(Result);
	}

	auto Result = MCPSuccess();
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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetGameFrameworkInfo(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	auto Result = MCPSuccess();

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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListInputAssets(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListBehaviorTrees(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListEqsQueries(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListStateTrees(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ProjectPointToNavigation(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		return MCPError(TEXT("Missing 'location' parameter"));
	}

	FVector Point;
	Point.X = (*LocationObj)->GetNumberField(TEXT("x"));
	Point.Y = (*LocationObj)->GetNumberField(TEXT("y"));
	Point.Z = (*LocationObj)->GetNumberField(TEXT("z"));

	REQUIRE_EDITOR_WORLD(World);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return MCPError(TEXT("No navigation system available"));
	}

	FNavLocation NavLocation;
	bool bProjected = NavSys->ProjectPointToNavigation(Point, NavLocation);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("projected"), bProjected);
	if (bProjected)
	{
		TSharedPtr<FJsonObject> ProjectedPoint = MakeShared<FJsonObject>();
		ProjectedPoint->SetNumberField(TEXT("x"), NavLocation.Location.X);
		ProjectedPoint->SetNumberField(TEXT("y"), NavLocation.Location.Y);
		ProjectedPoint->SetNumberField(TEXT("z"), NavLocation.Location.Z);
		Result->SetObjectField(TEXT("projectedLocation"), ProjectedPoint);
	}

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("BlackboardData")))
	{
		return Existing;
	}

	UClass* BlackboardClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BlackboardData"));
	if (!BlackboardClass)
	{
		return MCPError(TEXT("BlackboardData class not found."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, BlackboardClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create BlackboardData"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("BehaviorTree")))
	{
		return Existing;
	}

	UClass* BTClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BehaviorTree"));
	if (!BTClass)
	{
		return MCPError(TEXT("BehaviorTree class not found."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, BTClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create BehaviorTree"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI/EQS"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("EnvironmentQuery")))
	{
		return Existing;
	}

	UClass* EQSClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvironmentQuery"));
	if (!EQSClass)
	{
		return MCPError(TEXT("EnvironmentQuery class not found."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, EQSClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create EnvironmentQuery"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("StateTree")))
	{
		return Existing;
	}

	UClass* STClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
	if (!STClass)
	{
		return MCPError(TEXT("StateTree class not found. Enable StateTree plugin."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, STClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create StateTree"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName)
{
	UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		return MCPError(FString::Printf(TEXT("%s class not found: %s"), *FriendlyTypeName, *ParentClassPath));
	}

	// Idempotency: check if the blueprint already exists.
	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *ProbePath))
	{
		auto Res = MCPSuccess();
		MCPSetExisted(Res);
		Res->SetStringField(TEXT("path"), Existing->GetPathName());
		Res->SetStringField(TEXT("name"), Name);
		Res->SetStringField(TEXT("type"), FriendlyTypeName);
		return MCPResult(Res);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		return MCPError(FString::Printf(TEXT("Failed to create %s Blueprint"), *FriendlyTypeName));
	}

	NewBlueprint->ParentClass = ParentClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	SaveAssetPackage(NewBlueprint);

	const FString CreatedPath = NewBlueprint->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), CreatedPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("type"), FriendlyTypeName);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), CreatedPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameMode(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.GameModeBase"), TEXT("GameMode"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameState(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.GameStateBase"), TEXT("GameState"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerController(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.PlayerController"), TEXT("PlayerController"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerState(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.PlayerState"), TEXT("PlayerState"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateHud(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	return CreateBlueprintWithParent(Name, PackagePath, TEXT("/Script/Engine.HUD"), TEXT("HUD"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetCollisionProfile(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString ProfileName;
	if (auto Err = RequireString(Params, TEXT("profileName"), ProfileName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Set collision profile on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	// Idempotency: if every prim already matches, short-circuit
	const FName ProfileFName(*ProfileName);
	bool bAllMatch = PrimitiveComponents.Num() > 0;
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp || PrimComp->GetCollisionProfileName() != ProfileFName)
		{
			bAllMatch = false;
			break;
		}
	}
	if (bAllMatch)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("actorLabel"), ActorLabel);
		Noop->SetStringField(TEXT("profileName"), ProfileName);
		return MCPResult(Noop);
	}

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetCollisionProfileName(ProfileFName);
			ComponentsUpdated++;
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("profileName"), ProfileName);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	// No rollback: multi-component previous state capture not yet implemented.

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);

	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Set physics simulation on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	// Idempotency: if every prim already matches, short-circuit
	bool bAllMatch = PrimitiveComponents.Num() > 0;
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp || PrimComp->IsSimulatingPhysics() != bEnabled)
		{
			bAllMatch = false;
			break;
		}
	}
	if (bAllMatch)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("actorLabel"), ActorLabel);
		Noop->SetBoolField(TEXT("enabled"), bEnabled);
		return MCPResult(Noop);
	}

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetSimulatePhysics(bEnabled);
			ComponentsUpdated++;
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);

	// Rollback: self-inverse with flipped flag (approximation: prior state assumed uniform)
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetBoolField(TEXT("enabled"), !bEnabled);
	MCPSetRollback(Result, TEXT("set_physics_enabled"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetCollisionType(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString CollisionType;
	if (auto Err = RequireString(Params, TEXT("collisionType"), CollisionType)) return Err;

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
		return MCPError(FString::Printf(TEXT("Invalid collisionType: %s. Use NoCollision, QueryOnly, PhysicsOnly, or QueryAndPhysics"), *CollisionType));
	}

	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Set collision enabled on all primitive components
	int32 ComponentsUpdated = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	// Idempotency: all prims already match?
	bool bAllMatch = PrimitiveComponents.Num() > 0;
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp || PrimComp->GetCollisionEnabled() != CollisionEnabled)
		{
			bAllMatch = false;
			break;
		}
	}
	if (bAllMatch)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("actorLabel"), ActorLabel);
		Noop->SetStringField(TEXT("collisionType"), CollisionType);
		return MCPResult(Noop);
	}

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetCollisionEnabled(CollisionEnabled);
			ComponentsUpdated++;
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("collisionType"), CollisionType);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	// No rollback: multi-component previous state capture not yet implemented.

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetBodyProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
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

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);
	// No rollback: multi-component previous state capture not yet implemented.

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("NavModifierVolume '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("actorLabel"), Label);
				return MCPResult(Existing);
			}
		}
	}

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
		return MCPError(TEXT("Failed to spawn NavModifierVolume"));
	}

	if (!Label.IsEmpty())
	{
		NewVolume->SetActorLabel(Label);
	}

	const FString FinalLabel = NewVolume->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());

	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	FVector ActorLocation = NewVolume->GetActorLocation();
	LocationResult->SetNumberField(TEXT("x"), ActorLocation.X);
	LocationResult->SetNumberField(TEXT("y"), ActorLocation.Y);
	LocationResult->SetNumberField(TEXT("z"), ActorLocation.Z);
	Result->SetObjectField(TEXT("location"), LocationResult);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RebuildNavmesh(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// Trigger navmesh rebuild via console command
	GEditor->Exec(World, TEXT("RebuildNavigation"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("status"), TEXT("rebuild_triggered"));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetCdoDefaults(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

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
		return MCPError(FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	UObject* CDO = FoundClass->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(FString::Printf(TEXT("Could not get CDO for class: %s"), *ClassName));
	}

	auto Result = MCPSuccess();
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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetWorldGameMode(const TSharedPtr<FJsonObject>& Params)
{
	FString GameModeClassPath;
	if (auto Err = RequireString(Params, TEXT("gameModeClass"), GameModeClassPath)) return Err;

	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(FString::Printf(TEXT("GameMode class not found: %s"), *GameModeClassPath));
	}

	if (!GameModeClass->IsChildOf(AGameModeBase::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Class '%s' is not a GameModeBase subclass"), *GameModeClassPath));
	}

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (!WorldSettings)
	{
		return MCPError(TEXT("Could not get WorldSettings"));
	}

	// Idempotency: capture previous value, bail if already matching
	UClass* PrevGameMode = WorldSettings->DefaultGameMode;
	if (PrevGameMode == GameModeClass)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("gameModeClass"), GameModeClass->GetPathName());
		return MCPResult(Noop);
	}

	WorldSettings->DefaultGameMode = GameModeClass;
	WorldSettings->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("gameModeClass"), GameModeClass->GetPathName());
	Result->SetStringField(TEXT("gameModeName"), GameModeClass->GetName());

	// Rollback: self-inverse with previous game mode
	if (PrevGameMode)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("gameModeClass"), PrevGameMode->GetPathName());
		MCPSetRollback(Result, TEXT("set_world_game_mode"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateAiPerceptionConfig(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
	}

	// The blueprint must be an Actor-based blueprint to add components
	if (!Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
	{
		return MCPError(TEXT("Blueprint must be based on AActor to add perception component"));
	}

	// Read which senses to configure
	bool bAddSight = OptionalBool(Params, TEXT("addSight"), true);
	bool bAddHearing = OptionalBool(Params, TEXT("addHearing"), false);
	bool bAddDamage = OptionalBool(Params, TEXT("addDamage"), false);

	// Add AIPerceptionComponent via the SCS (SimpleConstructionScript)
	USCS_Node* PerceptionNode = Blueprint->SimpleConstructionScript->CreateNode(UAIPerceptionComponent::StaticClass(), TEXT("AIPerceptionComp"));
	if (!PerceptionNode)
	{
		return MCPError(TEXT("Failed to create AIPerceptionComponent node"));
	}

	Blueprint->SimpleConstructionScript->AddNode(PerceptionNode);

	UAIPerceptionComponent* PerceptionComp = Cast<UAIPerceptionComponent>(PerceptionNode->ComponentTemplate);
	if (!PerceptionComp)
	{
		return MCPError(TEXT("Failed to get AIPerceptionComponent template"));
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

	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("componentName"), TEXT("AIPerceptionComp"));
	Result->SetArrayField(TEXT("sensesConfigured"), SensesAdded);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddBlackboardKey(const TSharedPtr<FJsonObject>& Params)
{
	FString BlackboardPath;
	if (auto Err = RequireString(Params, TEXT("blackboardPath"), BlackboardPath)) return Err;

	FString KeyName;
	if (auto Err = RequireString(Params, TEXT("keyName"), KeyName)) return Err;

	FString KeyType = OptionalString(Params, TEXT("keyType"), TEXT("Bool"));

	UBlackboardData* BlackboardAsset = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!BlackboardAsset)
	{
		return MCPError(FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));
	}

	// Idempotency: key with this name already present?
	const FName KeyFName(*KeyName);
	for (const FBlackboardEntry& E : BlackboardAsset->Keys)
	{
		if (E.EntryName == KeyFName)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("blackboardPath"), BlackboardPath);
			Existed->SetStringField(TEXT("keyName"), KeyName);
			return MCPResult(Existed);
		}
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
		return MCPError(FString::Printf(TEXT("Unknown key type: %s. Supported: Bool, Int, Float, String, Name, Object, Class, Enum, Vector, Rotator"), *KeyType));
	}

	// Add the new key entry
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);
	NewEntry.KeyType = KeyTypeInstance;

	BlackboardAsset->Keys.Add(NewEntry);
	BlackboardAsset->MarkPackageDirty();

	// Save
	UEditorAssetLibrary::SaveAsset(BlackboardAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	Result->SetStringField(TEXT("keyName"), KeyName);
	Result->SetStringField(TEXT("keyType"), KeyType);
	Result->SetNumberField(TEXT("totalKeys"), BlackboardAsset->Keys.Num());
	// No rollback: no paired remove_blackboard_key handler.

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FGameplayHandlers::ConfigureBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString BehaviorTreePath;
	if (auto Err = RequireString(Params, TEXT("behaviorTreePath"), BehaviorTreePath)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label - should be an AI-controlled pawn/character
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
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Load the behavior tree asset
	UBehaviorTree* BehaviorTree = LoadObject<UBehaviorTree>(nullptr, *BehaviorTreePath);
	if (!BehaviorTree)
	{
		return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *BehaviorTreePath));
	}

	// Optionally load blackboard
	FString BlackboardPath;
	UBlackboardData* BlackboardAsset = nullptr;
	if (Params->TryGetStringField(TEXT("blackboardPath"), BlackboardPath))
	{
		BlackboardAsset = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
		if (!BlackboardAsset)
		{
			return MCPError(FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));
		}
	}

	// Find or get the AI controller for this actor
	APawn* Pawn = Cast<APawn>(FoundActor);
	if (!Pawn)
	{
		return MCPError(TEXT("Actor is not a Pawn. BehaviorTree requires an AI-controlled Pawn."));
	}

	AAIController* AIController = Cast<AAIController>(Pawn->GetController());
	if (!AIController)
	{
		return MCPError(TEXT("Pawn does not have an AAIController. Assign an AI controller first."));
	}

	// In UE 5.7, use RunBehaviorTree() on the AI controller rather than
	// SetDefaultTree()/SetDefaultBlackboard() on BehaviorTreeComponent (which don't exist).
	// RunBehaviorTree() handles creating/configuring the BehaviorTreeComponent internally
	// and also initializes the blackboard from the tree's BlackboardAsset if set.
	bool bSuccess = AIController->RunBehaviorTree(BehaviorTree);
	if (!bSuccess)
	{
		return MCPError(TEXT("Failed to run behavior tree on AI controller"));
	}

	// If a separate blackboard was specified, use the tree's component to apply it
	if (BlackboardAsset)
	{
		UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(AIController->GetBrainComponent());
		if (BTComp)
		{
			// The blackboard is initialized via the tree asset's BlackboardAsset property.
			// If a custom blackboard was provided, we can set it on the tree asset itself
			// before starting, or use the blackboard component on the controller.
			UBlackboardComponent* BBComp = AIController->GetBlackboardComponent();
			if (BBComp)
			{
				BBComp->InitializeBlackboard(*BlackboardAsset);
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("behaviorTree"), BehaviorTree->GetName());
	if (BlackboardAsset)
	{
		Result->SetStringField(TEXT("blackboard"), BlackboardAsset->GetName());
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetupPathFollowing(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Actor must be a Pawn with an AI controller
	APawn* Pawn = Cast<APawn>(FoundActor);
	if (!Pawn)
	{
		return MCPError(TEXT("Actor is not a Pawn"));
	}

	AAIController* AIController = Cast<AAIController>(Pawn->GetController());
	if (!AIController)
	{
		return MCPError(TEXT("Pawn does not have an AAIController"));
	}

	// Get the PathFollowingComponent from the AI controller
	UPathFollowingComponent* PathFollowComp = AIController->GetPathFollowingComponent();
	if (!PathFollowComp)
	{
		return MCPError(TEXT("AI Controller does not have a PathFollowingComponent"));
	}

	auto Result = MCPSuccess();

	// SetMovementComponent is deprecated but SetNavMoveInterface doesn't exist yet in 5.7
	UNavMovementComponent* NavMoveComp = Pawn->FindComponentByClass<UNavMovementComponent>();
	if (NavMoveComp)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		PathFollowComp->SetMovementComponent(NavMoveComp);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	else
	{
		Result->SetStringField(TEXT("warning"), TEXT("No UNavMovementComponent found on pawn; path following may not work correctly"));
	}

	// Read optional acceptance radius
	double AcceptanceRadius = -1.0;
	if (Params->TryGetNumberField(TEXT("acceptanceRadius"), AcceptanceRadius) && AcceptanceRadius >= 0.0)
	{
		// acceptance radius is typically set per-request via MoveToLocation, not on the component
	}

	// Optionally trigger a move-to if target location is specified
	const TSharedPtr<FJsonObject>* TargetObj = nullptr;
	if (Params->TryGetObjectField(TEXT("targetLocation"), TargetObj))
	{
		FVector TargetLocation;
		TargetLocation.X = (*TargetObj)->GetNumberField(TEXT("x"));
		TargetLocation.Y = (*TargetObj)->GetNumberField(TEXT("y"));
		TargetLocation.Z = (*TargetObj)->GetNumberField(TEXT("z"));

		FAIMoveRequest MoveRequest;
		MoveRequest.SetGoalLocation(TargetLocation);
		if (AcceptanceRadius >= 0.0)
		{
			MoveRequest.SetAcceptanceRadius(AcceptanceRadius);
		}
		MoveRequest.SetUsePathfinding(true);

		AIController->MoveTo(MoveRequest);

		TSharedPtr<FJsonObject> TargetResult = MakeShared<FJsonObject>();
		TargetResult->SetNumberField(TEXT("x"), TargetLocation.X);
		TargetResult->SetNumberField(TEXT("y"), TargetLocation.Y);
		TargetResult->SetNumberField(TEXT("z"), TargetLocation.Z);
		Result->SetObjectField(TEXT("targetLocation"), TargetResult);
		Result->SetStringField(TEXT("moveStatus"), TEXT("move_requested"));
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("hasNavMovementComponent"), NavMoveComp != nullptr);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RunEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;

	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Load the EQS query asset
	UEnvQuery* EnvQuery = LoadObject<UEnvQuery>(nullptr, *QueryPath);
	if (!EnvQuery)
	{
		return MCPError(FString::Printf(TEXT("EnvQuery not found: %s"), *QueryPath));
	}

	// Find the querier actor
	AActor* QuerierActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			QuerierActor = *ActorIt;
			break;
		}
	}

	if (!QuerierActor)
	{
		return MCPError(FString::Printf(TEXT("Querier actor not found: %s"), *ActorLabel));
	}

	// In UE 5.7, run EQS queries via UEnvQueryManager::RunQuery() with FEnvQueryRequest.
	// FEnvQueryRequest and FEQSParametrizedQueryExecutionRequest do not exist as standalone types.
	// Instead, use UEnvQueryManager directly with RunEQSQuery or the instance-based API.
	UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(World);
	if (!EQSManager)
	{
		return MCPError(TEXT("EnvQueryManager not available in current world"));
	}

	// Run the query synchronously-ish: we trigger it and report that it was started.
	// EQS queries in UE are async by nature; we start the query and return its ID.
	UEnvQueryInstanceBlueprintWrapper* QueryInstance = EQSManager->RunEQSQuery(World, EnvQuery, QuerierActor, EEnvQueryRunMode::AllMatching, nullptr);

	if (!QueryInstance)
	{
		return MCPError(TEXT("Failed to start EQS query"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("queryPath"), QueryPath);
	Result->SetStringField(TEXT("queryName"), EnvQuery->GetName());
	Result->SetStringField(TEXT("querierActor"), ActorLabel);
	Result->SetNumberField(TEXT("queryId"), QueryInstance->GetUniqueID());
	Result->SetStringField(TEXT("status"), TEXT("query_started"));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetBehaviorTreeInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), Asset->GetName());
	Result->SetStringField(TEXT("className"), Asset->GetClass()->GetName());

	// Try to read blackboard asset
	FProperty* BBProp = Asset->GetClass()->FindPropertyByName(TEXT("BlackboardAsset"));
	if (BBProp)
	{
		FObjectProperty* ObjProp = CastField<FObjectProperty>(BBProp);
		if (ObjProp)
		{
			UObject* BB = ObjProp->GetObjectPropertyValue(BBProp->ContainerPtrToValuePtr<void>(Asset));
			if (BB)
			{
				Result->SetStringField(TEXT("blackboardAsset"), BB->GetPathName());

				// Try to read blackboard keys
				TArray<TSharedPtr<FJsonValue>> KeysArray;
				FProperty* KeysProp = BB->GetClass()->FindPropertyByName(TEXT("Keys"));
				if (KeysProp)
				{
					FArrayProperty* ArrProp = CastField<FArrayProperty>(KeysProp);
					if (ArrProp)
					{
						FScriptArrayHelper ArrayHelper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(BB));
						for (int32 i = 0; i < ArrayHelper.Num(); i++)
						{
							TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
							UObject* KeyEntry = *reinterpret_cast<UObject**>(ArrayHelper.GetRawPtr(i));
							if (KeyEntry)
							{
								FProperty* NameProp = KeyEntry->GetClass()->FindPropertyByName(TEXT("EntryName"));
								if (NameProp)
								{
									FString EntryName;
									NameProp->ExportTextItem_Direct(EntryName, NameProp->ContainerPtrToValuePtr<void>(KeyEntry), nullptr, KeyEntry, PPF_None);
									KeyObj->SetStringField(TEXT("name"), EntryName);
								}
								else
								{
									KeyObj->SetStringField(TEXT("name"), KeyEntry->GetName());
								}
							}
							KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
						}
					}
				}
				Result->SetArrayField(TEXT("blackboardKeys"), KeysArray);
			}
		}
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddPerceptionComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
	}

	UClass* CompClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.AIPerceptionComponent"));
	if (!CompClass)
	{
		return MCPError(TEXT("AIPerceptionComponent not found. Enable AIModule."));
	}

	// Idempotency: existing AIPerceptionComponent on the SCS?
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N->ComponentTemplate && N->ComponentTemplate->GetClass() == CompClass)
			{
				auto Existed = MCPSuccess();
				MCPSetExisted(Existed);
				Existed->SetStringField(TEXT("blueprintPath"), BPPath);
				Existed->SetStringField(TEXT("component"), N->GetVariableName().ToString());
				return MCPResult(Existed);
			}
		}
	}

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("AIPerceptionComp"));
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		SaveAssetPackage(BP);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TEXT("AIPerceptionComp"));

	// Rollback: remove_component
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), BPPath);
	Payload->SetStringField(TEXT("componentName"), TEXT("AIPerceptionComp"));
	MCPSetRollback(Result, TEXT("remove_component"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ConfigureAiPerceptionSense(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;

	FString SenseType = OptionalString(Params, TEXT("senseType"), TEXT("Sight"));

	TMap<FString, FString> SenseMap;
	SenseMap.Add(TEXT("Sight"), TEXT("AISenseConfig_Sight"));
	SenseMap.Add(TEXT("Hearing"), TEXT("AISenseConfig_Hearing"));
	SenseMap.Add(TEXT("Damage"), TEXT("AISenseConfig_Damage"));
	SenseMap.Add(TEXT("Touch"), TEXT("AISenseConfig_Touch"));
	SenseMap.Add(TEXT("Team"), TEXT("AISenseConfig_Team"));

	FString* SenseClassName = SenseMap.Find(SenseType);
	if (!SenseClassName)
	{
		return MCPError(FString::Printf(TEXT("Unknown sense type: %s. Available: Sight, Hearing, Damage, Touch, Team"), *SenseType));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("senseType"), SenseType);
	Result->SetStringField(TEXT("senseClass"), *SenseClassName);
	Result->SetStringField(TEXT("note"), FString::Printf(TEXT("Use editor.execute_python to fully configure %s properties."), **SenseClassName));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddStateTreeComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
	}

	UClass* CompClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTreeComponent"));
	if (!CompClass)
	{
		return MCPError(TEXT("StateTreeComponent not found. Enable StateTree plugin."));
	}

	// Idempotency: check for existing component by name/class on the SCS
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N->ComponentTemplate && N->ComponentTemplate->GetClass() == CompClass)
			{
				auto Existed = MCPSuccess();
				MCPSetExisted(Existed);
				Existed->SetStringField(TEXT("blueprintPath"), BPPath);
				Existed->SetStringField(TEXT("component"), N->GetVariableName().ToString());
				return MCPResult(Existed);
			}
		}
	}

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("StateTreeComp"));
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		SaveAssetPackage(BP);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TEXT("StateTreeComp"));

	// Rollback: remove_component handler
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), BPPath);
	Payload->SetStringField(TEXT("componentName"), TEXT("StateTreeComp"));
	MCPSetRollback(Result, TEXT("remove_component"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddSmartObjectComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
	}

	UClass* CompClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectComponent"));
	if (!CompClass)
	{
		return MCPError(TEXT("SmartObjectComponent not found. Enable SmartObjects plugin."));
	}

	// Idempotency: existing component of this class already on the SCS?
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N->ComponentTemplate && N->ComponentTemplate->GetClass() == CompClass)
			{
				auto Existed = MCPSuccess();
				MCPSetExisted(Existed);
				Existed->SetStringField(TEXT("blueprintPath"), BPPath);
				Existed->SetStringField(TEXT("component"), N->GetVariableName().ToString());
				return MCPResult(Existed);
			}
		}
	}

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("SmartObjectComp"));
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		SaveAssetPackage(BP);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TEXT("SmartObjectComp"));

	// Rollback: remove_component
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), BPPath);
	Payload->SetStringField(TEXT("componentName"), TEXT("SmartObjectComp"));
	MCPSetRollback(Result, TEXT("remove_component"), Payload);

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FGameplayHandlers::ReadBehaviorTreeGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	TFunction<TSharedPtr<FJsonObject>(UBTNode*)> Walk;
	Walk = [&](UBTNode* Node) -> TSharedPtr<FJsonObject>
	{
		if (!Node) return nullptr;
		TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
		NObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NObj->SetStringField(TEXT("name"), Node->GetName());
		FProperty* NameProp = Node->GetClass()->FindPropertyByName(TEXT("NodeName"));
		if (NameProp)
		{
			FString NodeDisplay;
			NameProp->ExportText_Direct(NodeDisplay, NameProp->ContainerPtrToValuePtr<void>(Node), nullptr, Node, PPF_None);
			NObj->SetStringField(TEXT("nodeName"), NodeDisplay);
		}

		if (UBTCompositeNode* Comp = Cast<UBTCompositeNode>(Node))
		{
			NObj->SetStringField(TEXT("kind"), TEXT("composite"));

			TArray<TSharedPtr<FJsonValue>> ChildrenArr;
			for (const FBTCompositeChild& Child : Comp->Children)
			{
				TSharedPtr<FJsonObject> ChildEntry = MakeShared<FJsonObject>();
				if (Child.ChildComposite)
					ChildEntry->SetObjectField(TEXT("child"), Walk(Child.ChildComposite));
				else if (Child.ChildTask)
					ChildEntry->SetObjectField(TEXT("child"), Walk(Child.ChildTask));

				TArray<TSharedPtr<FJsonValue>> Decs;
				for (UBTDecorator* D : Child.Decorators)
				{
					if (!D) continue;
					TSharedPtr<FJsonObject> DObj = MakeShared<FJsonObject>();
					DObj->SetStringField(TEXT("class"), D->GetClass()->GetName());
					DObj->SetStringField(TEXT("name"), D->GetName());
					Decs.Add(MakeShared<FJsonValueObject>(DObj));
				}
				ChildEntry->SetArrayField(TEXT("decorators"), Decs);
				ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildEntry));
			}
			NObj->SetArrayField(TEXT("children"), ChildrenArr);

			TArray<TSharedPtr<FJsonValue>> Services;
			for (UBTService* S : Comp->Services)
			{
				if (!S) continue;
				TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
				SObj->SetStringField(TEXT("class"), S->GetClass()->GetName());
				SObj->SetStringField(TEXT("name"), S->GetName());
				Services.Add(MakeShared<FJsonValueObject>(SObj));
			}
			NObj->SetArrayField(TEXT("services"), Services);
		}
		else if (Cast<UBTTaskNode>(Node))
		{
			NObj->SetStringField(TEXT("kind"), TEXT("task"));
		}
		return NObj;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), BT->GetName());
	if (BT->BlackboardAsset)
	{
		Result->SetStringField(TEXT("blackboardAsset"), BT->BlackboardAsset->GetPathName());
	}
	if (BT->RootNode)
	{
		Result->SetObjectField(TEXT("root"), Walk(BT->RootNode));
	}
	TArray<TSharedPtr<FJsonValue>> RootDecs;
	for (UBTDecorator* D : BT->RootDecorators)
	{
		if (!D) continue;
		TSharedPtr<FJsonObject> DObj = MakeShared<FJsonObject>();
		DObj->SetStringField(TEXT("class"), D->GetClass()->GetName());
		DObj->SetStringField(TEXT("name"), D->GetName());
		RootDecs.Add(MakeShared<FJsonValueObject>(DObj));
	}
	Result->SetArrayField(TEXT("rootDecorators"), RootDecs);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #163  get_navmesh_details — Detailed ARecastNavMesh configuration
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetNavmeshDetails(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return MCPError(TEXT("No navigation system found in editor world."));
	}

	// Find the first ARecastNavMesh in the nav data set
	ARecastNavMesh* RecastNav = nullptr;
	for (ANavigationData* NavData : NavSys->NavDataSet)
	{
		RecastNav = Cast<ARecastNavMesh>(NavData);
		if (RecastNav) break;
	}

	// Fallback: iterate world actors
	if (!RecastNav)
	{
		for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
		{
			RecastNav = *It;
			break;
		}
	}

	if (!RecastNav)
	{
		return MCPError(TEXT("No ARecastNavMesh found. Add a NavMeshBoundsVolume and build navigation."));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), RecastNav->GetName());
	Result->SetStringField(TEXT("class"), RecastNav->GetClass()->GetName());

	// Cell / voxelization
	Result->SetNumberField(TEXT("cellSize"), RecastNav->CellSize);
	Result->SetNumberField(TEXT("cellHeight"), RecastNav->CellHeight);

	// Agent
	Result->SetNumberField(TEXT("agentRadius"), RecastNav->AgentRadius);
	Result->SetNumberField(TEXT("agentHeight"), RecastNav->AgentHeight);
	Result->SetNumberField(TEXT("agentMaxSlope"), RecastNav->AgentMaxSlope);
	Result->SetNumberField(TEXT("agentMaxStepHeight"), RecastNav->AgentMaxStepHeight);

	// Tile / region
	Result->SetNumberField(TEXT("tileSize"), static_cast<double>(RecastNav->TileSizeUU));
	Result->SetNumberField(TEXT("minRegionArea"), RecastNav->MinRegionArea);
	Result->SetNumberField(TEXT("mergingRegionSize"), RecastNav->MergeRegionSize);

	// Additional useful fields
	Result->SetNumberField(TEXT("maxSimplificationError"), RecastNav->MaxSimplificationError);
	Result->SetBoolField(TEXT("fixedTilePoolSize"), RecastNav->bFixedTilePoolSize);
	Result->SetNumberField(TEXT("tilePoolSize"), static_cast<double>(RecastNav->TilePoolSize));
	Result->SetBoolField(TEXT("drawFilledPolys"), RecastNav->bDrawFilledPolys);

	// Nav bounds volumes count
	int32 BoundsCount = 0;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		++BoundsCount;
	}
	Result->SetNumberField(TEXT("navMeshBoundsVolumeCount"), BoundsCount);

	return MCPResult(Result);
}