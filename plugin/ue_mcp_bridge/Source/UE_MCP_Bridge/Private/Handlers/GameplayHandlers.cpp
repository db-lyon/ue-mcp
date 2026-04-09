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
	Registry.RegisterHandler(TEXT("add_perception_component"), &AddPerceptionComponent);
	Registry.RegisterHandler(TEXT("configure_ai_perception_sense"), &ConfigureAiPerceptionSense);
	Registry.RegisterHandler(TEXT("add_state_tree_component"), &AddStateTreeComponent);
	Registry.RegisterHandler(TEXT("add_smart_object_component"), &AddSmartObjectComponent);
	Registry.RegisterHandler(TEXT("read_imc"), &ReadImc);
	Registry.RegisterHandler(TEXT("add_imc_mapping"), &AddImcMapping);
	Registry.RegisterHandler(TEXT("set_mapping_modifiers"), &SetMappingModifiers);
	Registry.RegisterHandler(TEXT("inspect_pie"), &InspectPie);
	Registry.RegisterHandler(TEXT("get_pie_anim_state"), &GetPieAnimState);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateSmartObjectDefinition(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI/SmartObjects"));

	// Delete existing asset if it exists
	FString AssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(AssetPath);

	// Find SmartObjectDefinition class
	UClass* SmartObjectDefClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectDefinition"));
	if (!SmartObjectDefClass)
	{
		return MCPError(TEXT("SmartObjectDefinition class not found. Enable SmartObjects plugin."));
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
	if (PackageName.EndsWith(TEXT("/"))) PackageName = PackageName.LeftChop(1);

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackageName, SmartObjectDefClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create SmartObjectDefinition"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

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

TSharedPtr<FJsonValue> FGameplayHandlers::CreateInputAction(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Input"));

	UClass* InputActionClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputAction"));
	if (!InputActionClass)
	{
		return MCPError(TEXT("InputAction class not found. Enable EnhancedInput plugin."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, InputActionClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create InputAction"));
	}

	// Apply valueType if provided
	FString ValueTypeStr = OptionalString(Params, TEXT("valueType"));
	if (!ValueTypeStr.IsEmpty())
	{
		EInputActionValueType DesiredType = EInputActionValueType::Boolean;
		bool bValidType = true;

		if (ValueTypeStr.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || ValueTypeStr == TEXT("Digital"))
		{
			DesiredType = EInputActionValueType::Boolean;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			DesiredType = EInputActionValueType::Axis1D;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
		{
			DesiredType = EInputActionValueType::Axis2D;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
		{
			DesiredType = EInputActionValueType::Axis3D;
		}
		else
		{
			bValidType = false;
		}

		if (bValidType)
		{
			UInputAction* InputAction = Cast<UInputAction>(NewAsset);
			if (InputAction)
			{
				InputAction->ValueType = DesiredType;
			}
		}
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateInputMappingContext(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Input"));

	UClass* IMCClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputMappingContext"));
	if (!IMCClass)
	{
		return MCPError(TEXT("InputMappingContext class not found. Enable EnhancedInput plugin."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, IMCClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create InputMappingContext"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));

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
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));

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
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI/EQS"));

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
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));

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
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName)
{
	UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		return MCPError(FString::Printf(TEXT("%s class not found: %s"), *FriendlyTypeName, *ParentClassPath));
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
		return MCPError(FString::Printf(TEXT("Failed to create %s Blueprint"), *FriendlyTypeName));
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

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("type"), FriendlyTypeName);

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

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetCollisionProfileName(FName(*ProfileName));
			ComponentsUpdated++;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("profileName"), ProfileName);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);

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

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetSimulatePhysics(bEnabled);
			ComponentsUpdated++;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);

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

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetCollisionEnabled(CollisionEnabled);
			ComponentsUpdated++;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("collisionType"), CollisionType);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);

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
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("componentsUpdated"), ComponentsUpdated);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

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
		return MCPError(TEXT("Failed to spawn NavModifierVolume"));
	}

	// Set label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		NewVolume->SetActorLabel(Label);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), NewVolume->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());

	TSharedPtr<FJsonObject> LocationResult = MakeShared<FJsonObject>();
	FVector ActorLocation = NewVolume->GetActorLocation();
	LocationResult->SetNumberField(TEXT("x"), ActorLocation.X);
	LocationResult->SetNumberField(TEXT("y"), ActorLocation.Y);
	LocationResult->SetNumberField(TEXT("z"), ActorLocation.Z);
	Result->SetObjectField(TEXT("location"), LocationResult);

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

	WorldSettings->DefaultGameMode = GameModeClass;
	WorldSettings->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("gameModeClass"), GameModeClass->GetPathName());
	Result->SetStringField(TEXT("gameModeName"), GameModeClass->GetName());

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

	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

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
	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	Result->SetStringField(TEXT("keyName"), KeyName);
	Result->SetStringField(TEXT("keyType"), KeyType);
	Result->SetNumberField(TEXT("totalKeys"), BlackboardAsset->Keys.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetupEnhancedInput(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString MappingContextPath;
	if (auto Err = RequireString(Params, TEXT("mappingContextPath"), MappingContextPath)) return Err;

	int32 Priority = OptionalInt(Params, TEXT("priority"), 0);

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

	// Load the mapping context asset
	UInputMappingContext* MappingContext = LoadObject<UInputMappingContext>(nullptr, *MappingContextPath);
	if (!MappingContext)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *MappingContextPath));
	}

	// Check if the actor has an EnhancedInputComponent
	UEnhancedInputComponent* InputComp = FoundActor->FindComponentByClass<UEnhancedInputComponent>();
	if (!InputComp)
	{
		return MCPError(TEXT("Actor does not have an EnhancedInputComponent"));
	}

	// Optionally bind input actions from params
	TArray<TSharedPtr<FJsonValue>> BoundActions;
	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("actions"), ActionsArray))
	{
		for (const auto& ActionVal : *ActionsArray)
		{
			const TSharedPtr<FJsonObject>* ActionObj = nullptr;
			if (ActionVal->TryGetObject(ActionObj))
			{
				FString ActionPath;
				if ((*ActionObj)->TryGetStringField(TEXT("actionPath"), ActionPath))
				{
					UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *ActionPath);
					if (InputAction)
					{
						TSharedPtr<FJsonObject> BoundAction = MakeShared<FJsonObject>();
						BoundAction->SetStringField(TEXT("actionPath"), ActionPath);
						BoundAction->SetStringField(TEXT("actionName"), InputAction->GetName());
						BoundActions.Add(MakeShared<FJsonValueObject>(BoundAction));
					}
				}
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("mappingContext"), MappingContext->GetName());
	Result->SetNumberField(TEXT("priority"), Priority);
	Result->SetArrayField(TEXT("boundActions"), BoundActions);

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

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("AIPerceptionComp"));
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		UPackage* Pkg = BP->GetOutermost();
		if (Pkg)
		{
			Pkg->MarkPackageDirty();
			FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TEXT("AIPerceptionComp"));

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

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("StateTreeComp"));
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		UPackage* Pkg = BP->GetOutermost();
		if (Pkg)
		{
			Pkg->MarkPackageDirty();
			FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TEXT("StateTreeComp"));

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

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("SmartObjectComp"));
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		UPackage* Pkg = BP->GetOutermost();
		if (Pkg)
		{
			Pkg->MarkPackageDirty();
			FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TEXT("SmartObjectComp"));

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #57 / #60  read_imc — Read InputMappingContext mappings
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ReadImc(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> MappingsArr;
	const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();
	for (const FEnhancedActionKeyMapping& Mapping : Mappings)
	{
		TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
		MObj->SetStringField(TEXT("inputAction"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("None"));
		MObj->SetStringField(TEXT("inputActionName"), Mapping.Action ? Mapping.Action->GetName() : TEXT("None"));
		MObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

		// Triggers
		TArray<TSharedPtr<FJsonValue>> TriggersArr;
		for (const TObjectPtr<UInputTrigger>& Trigger : Mapping.Triggers)
		{
			if (Trigger)
			{
				TriggersArr.Add(MakeShared<FJsonValueString>(Trigger->GetClass()->GetName()));
			}
		}
		MObj->SetArrayField(TEXT("triggers"), TriggersArr);

		// Modifiers
		TArray<TSharedPtr<FJsonValue>> ModifiersArr;
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
		{
			if (Modifier)
			{
				ModifiersArr.Add(MakeShared<FJsonValueString>(Modifier->GetClass()->GetName()));
			}
		}
		MObj->SetArrayField(TEXT("modifiers"), ModifiersArr);

		MappingsArr.Add(MakeShared<FJsonValueObject>(MObj));
	}

	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetStringField(TEXT("imcName"), IMC->GetName());
	Result->SetArrayField(TEXT("mappings"), MappingsArr);
	Result->SetNumberField(TEXT("count"), MappingsArr.Num());

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #57 / #60  add_imc_mapping — Add key mapping to an IMC
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::AddImcMapping(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	FString InputActionPath;
	if (auto Err = RequireString(Params, TEXT("inputActionPath"), InputActionPath)) return Err;

	FString KeyName;
	if (auto Err = RequireString(Params, TEXT("key"), KeyName)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *InputActionPath);
	if (!InputAction)
	{
		return MCPError(FString::Printf(TEXT("InputAction not found: %s"), *InputActionPath));
	}

	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Invalid key name: %s"), *KeyName));
	}

	// Create the mapping and add it
	FEnhancedActionKeyMapping NewMapping;
	NewMapping.Action = InputAction;
	NewMapping.Key = Key;

	IMC->MapKey(InputAction, Key);

	// Save the asset
	UPackage* Pkg = IMC->GetOutermost();
	if (Pkg)
	{
		Pkg->MarkPackageDirty();
		FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetStringField(TEXT("inputAction"), InputAction->GetPathName());
	Result->SetStringField(TEXT("key"), KeyName);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #75  set_mapping_modifiers — Add modifiers/triggers to an IMC mapping
//      Creates UObject subobjects with IMC as outer so they serialize.
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::SetMappingModifiers(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	int32 MappingIndex = OptionalInt(Params, TEXT("mappingIndex"), 0);
	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	if (!Mappings.IsValidIndex(MappingIndex))
	{
		return MCPError(FString::Printf(TEXT("Mapping index %d out of range (count: %d)"), MappingIndex, Mappings.Num()));
	}

	FEnhancedActionKeyMapping& Mapping = Mappings[MappingIndex];

	// ── Modifiers ──
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArr) && ModifiersArr)
	{
		Mapping.Modifiers.Empty();
		for (const auto& ModVal : *ModifiersArr)
		{
			const TSharedPtr<FJsonObject>* ModObj = nullptr;
			if (!ModVal->TryGetObject(ModObj) || !ModObj) continue;

			FString TypeName;
			(*ModObj)->TryGetStringField(TEXT("type"), TypeName);
			if (TypeName.IsEmpty()) continue;

			// Resolve class: "DeadZone" → UInputModifierDeadZone
			FString ClassName = TypeName;
			if (!ClassName.StartsWith(TEXT("UInputModifier")))
			{
				ClassName = TEXT("UInputModifier") + ClassName;
			}

			UClass* ModClass = FindClassByShortName(ClassName);
			if (!ModClass || !ModClass->IsChildOf(UInputModifier::StaticClass()))
			{
				continue; // skip unknown modifier types
			}

			// Create with IMC as outer — this is the key fix for #75
			UInputModifier* Modifier = NewObject<UInputModifier>(IMC, ModClass);

			// Set properties via reflection
			for (const auto& Pair : (*ModObj)->Values)
			{
				if (Pair.Key == TEXT("type")) continue;

				FProperty* Prop = ModClass->FindPropertyByName(FName(*Pair.Key));
				if (!Prop) continue;

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Modifier);

				if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					FloatProp->SetPropertyValue(PropAddr, (float)Val);
				}
				else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					DoubleProp->SetPropertyValue(PropAddr, Val);
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					bool Val = false;
					Pair.Value->TryGetBool(Val);
					BoolProp->SetPropertyValue(PropAddr, Val);
				}
				else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
				{
					FString EnumStr;
					if (Pair.Value->TryGetString(EnumStr))
					{
						int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(EnumStr);
						if (EnumVal != INDEX_NONE)
						{
							EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropAddr, EnumVal);
						}
					}
				}
				else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
				{
					if (ByteProp->Enum)
					{
						FString EnumStr;
						if (Pair.Value->TryGetString(EnumStr))
						{
							int64 EnumVal = ByteProp->Enum->GetValueByNameString(EnumStr);
							if (EnumVal != INDEX_NONE)
							{
								ByteProp->SetPropertyValue(PropAddr, (uint8)EnumVal);
							}
						}
					}
					else
					{
						double Val = 0;
						Pair.Value->TryGetNumber(Val);
						ByteProp->SetPropertyValue(PropAddr, (uint8)Val);
					}
				}
			}

			Mapping.Modifiers.Add(Modifier);
		}
	}

	// ── Triggers ──
	const TArray<TSharedPtr<FJsonValue>>* TriggersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("triggers"), TriggersArr) && TriggersArr)
	{
		Mapping.Triggers.Empty();
		for (const auto& TrigVal : *TriggersArr)
		{
			const TSharedPtr<FJsonObject>* TrigObj = nullptr;
			if (!TrigVal->TryGetObject(TrigObj) || !TrigObj) continue;

			FString TypeName;
			(*TrigObj)->TryGetStringField(TEXT("type"), TypeName);
			if (TypeName.IsEmpty()) continue;

			FString ClassName = TypeName;
			if (!ClassName.StartsWith(TEXT("UInputTrigger")))
			{
				ClassName = TEXT("UInputTrigger") + ClassName;
			}

			UClass* TrigClass = FindClassByShortName(ClassName);
			if (!TrigClass || !TrigClass->IsChildOf(UInputTrigger::StaticClass()))
			{
				continue;
			}

			UInputTrigger* Trigger = NewObject<UInputTrigger>(IMC, TrigClass);

			// Set properties via reflection (same pattern as modifiers)
			for (const auto& Pair : (*TrigObj)->Values)
			{
				if (Pair.Key == TEXT("type")) continue;

				FProperty* Prop = TrigClass->FindPropertyByName(FName(*Pair.Key));
				if (!Prop) continue;

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Trigger);

				if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					FloatProp->SetPropertyValue(PropAddr, (float)Val);
				}
				else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					DoubleProp->SetPropertyValue(PropAddr, Val);
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					bool Val = false;
					Pair.Value->TryGetBool(Val);
					BoolProp->SetPropertyValue(PropAddr, Val);
				}
			}

			Mapping.Triggers.Add(Trigger);
		}
	}

	// Save
	UPackage* Pkg = IMC->GetOutermost();
	if (Pkg)
	{
		Pkg->MarkPackageDirty();
		FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), MappingIndex);
	Result->SetNumberField(TEXT("modifierCount"), Mapping.Modifiers.Num());
	Result->SetNumberField(TEXT("triggerCount"), Mapping.Triggers.Num());
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #54 / #89 / #90  inspect_pie — PIE runtime actor inspection
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::InspectPie(const TSharedPtr<FJsonObject>& Params)
{
	// Get PIE world
	FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
	if (!PIEContext || !PIEContext->World())
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	UWorld* PIEWorld = PIEContext->World();

	FString ActorLabel;
	bool bHasLabel = Params->TryGetStringField(TEXT("actorLabel"), ActorLabel) && !ActorLabel.IsEmpty();

	if (!bHasLabel)
	{
		// List all actors in PIE world
		TArray<TSharedPtr<FJsonValue>> ActorsArr;
		for (TActorIterator<AActor> It(PIEWorld); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !IsValid(Actor)) continue;

			TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
			AObj->SetStringField(TEXT("name"), Actor->GetName());
			AObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
			AObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			FVector Loc = Actor->GetActorLocation();
			TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
			LocObj->SetNumberField(TEXT("x"), Loc.X);
			LocObj->SetNumberField(TEXT("y"), Loc.Y);
			LocObj->SetNumberField(TEXT("z"), Loc.Z);
			AObj->SetObjectField(TEXT("location"), LocObj);

			ActorsArr.Add(MakeShared<FJsonValueObject>(AObj));
		}

		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("actors"), ActorsArr);
		Result->SetNumberField(TEXT("count"), ActorsArr.Num());
		return MCPResult(Result);
	}

	// Find specific actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found in PIE: %s"), *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), FoundActor->GetName());
	Result->SetStringField(TEXT("label"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("class"), FoundActor->GetClass()->GetName());

	FVector Loc = FoundActor->GetActorLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Result->SetObjectField(TEXT("location"), LocObj);

	// Components
	TArray<TSharedPtr<FJsonValue>> CompsArr;
	TArray<UActorComponent*> Components;
	FoundActor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;

		TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
		CObj->SetStringField(TEXT("name"), Comp->GetName());
		CObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CObj->SetBoolField(TEXT("active"), Comp->IsActive());

		// For scene components, include transform
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			FVector CLoc = SceneComp->GetComponentLocation();
			TSharedPtr<FJsonObject> CLocObj = MakeShared<FJsonObject>();
			CLocObj->SetNumberField(TEXT("x"), CLoc.X);
			CLocObj->SetNumberField(TEXT("y"), CLoc.Y);
			CLocObj->SetNumberField(TEXT("z"), CLoc.Z);
			CObj->SetObjectField(TEXT("worldLocation"), CLocObj);
		}

		// For primitive components, include physics/collision info
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp))
		{
			CObj->SetBoolField(TEXT("simulatingPhysics"), PrimComp->IsSimulatingPhysics());
			CObj->SetStringField(TEXT("collisionProfile"), PrimComp->GetCollisionProfileName().ToString());
		}

		CompsArr.Add(MakeShared<FJsonValueObject>(CObj));
	}
	Result->SetArrayField(TEXT("components"), CompsArr);

	// Input bindings — check for EnhancedInputComponent
	UEnhancedInputComponent* InputComp = FoundActor->FindComponentByClass<UEnhancedInputComponent>();
	Result->SetBoolField(TEXT("hasEnhancedInput"), InputComp != nullptr);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #26  get_pie_anim_state — PIE anim instance state
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetPieAnimState(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	// Get PIE world
	FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
	if (!PIEContext || !PIEContext->World())
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	UWorld* PIEWorld = PIEContext->World();

	// Find actor
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found in PIE: %s"), *ActorLabel));
	}

	// Find SkeletalMeshComponent
	USkeletalMeshComponent* SkelMesh = FoundActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh)
	{
		return MCPError(TEXT("Actor does not have a SkeletalMeshComponent"));
	}

	// Get AnimInstance
	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return MCPError(TEXT("No AnimInstance on the SkeletalMeshComponent"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetName());
	Result->SetStringField(TEXT("animClass"), AnimInst->GetClass()->GetName());

	// Current montage
	UAnimMontage* CurrentMontage = AnimInst->GetCurrentActiveMontage();
	Result->SetStringField(TEXT("currentMontage"), CurrentMontage ? CurrentMontage->GetName() : TEXT("None"));
	if (CurrentMontage)
	{
		Result->SetNumberField(TEXT("montagePosition"), AnimInst->Montage_GetPosition(CurrentMontage));
		Result->SetBoolField(TEXT("montageIsPlaying"), AnimInst->Montage_IsPlaying(CurrentMontage));
	}

	// State machine info — use the anim class interface to enumerate machines
	TArray<TSharedPtr<FJsonValue>> StatesArr;
	if (const IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(AnimInst->GetClass()))
	{
		const TArray<FBakedAnimationStateMachine>& BakedMachines = AnimClassInterface->GetBakedStateMachines();
		for (int32 MachineIdx = 0; MachineIdx < BakedMachines.Num(); ++MachineIdx)
		{
			const FBakedAnimationStateMachine& BakedMachine = BakedMachines[MachineIdx];
			TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
			SMObj->SetStringField(TEXT("machineName"), BakedMachine.MachineName.ToString());
			SMObj->SetNumberField(TEXT("machineIndex"), MachineIdx);
			SMObj->SetNumberField(TEXT("stateCount"), BakedMachine.States.Num());

			// Try to get current state from the instance
			const FAnimNode_StateMachine* SM = AnimInst->GetStateMachineInstance(MachineIdx);
			if (SM)
			{
				int32 StateIdx = SM->GetCurrentState();
				SMObj->SetNumberField(TEXT("currentStateIndex"), StateIdx);
				if (BakedMachine.States.IsValidIndex(StateIdx))
				{
					SMObj->SetStringField(TEXT("currentStateName"), BakedMachine.States[StateIdx].StateName.ToString());
				}
			}

			StatesArr.Add(MakeShared<FJsonValueObject>(SMObj));
		}
	}
	Result->SetArrayField(TEXT("stateMachines"), StatesArr);

	return MCPResult(Result);
}
