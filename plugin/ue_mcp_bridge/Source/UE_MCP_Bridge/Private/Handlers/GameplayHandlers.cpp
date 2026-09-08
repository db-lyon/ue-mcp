#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerJsonProperty.h"
#include "HandlerAssetCreate.h"
#include "JsonSerializer.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Modules/ModuleManager.h"
#include "StateTree.h"
#include "StateTreeReference.h"
#include "StateTreeInstanceData.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeEditorData.h"
#include "StateTreeSchema.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeCompilerLog.h"
#include "HandlerStateTreeSchema.h"
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
#include "NavigationPath.h"
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
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig.h"
#include "NavAreas/NavArea.h"
#include "VolumeHelpers_Internal.h"
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
	// Enhanced Input asset authoring stays here. pie-studio owns PIE-time
	// inject/record/replay; authoring InputAction / InputMappingContext
	// assets and editing IMC mappings is core ue-mcp.
	Registry.RegisterHandler(TEXT("create_input_action"), &CreateInputAction);
	Registry.RegisterHandler(TEXT("create_input_mapping_context"), &CreateInputMappingContext);
	Registry.RegisterHandler(TEXT("read_imc"), &ReadImc);
	// #778: superseded by GetInputMappingContexts, which covers every PIE world
	// rather than only the primary one. The old name stays registered so it does
	// not become "Unknown method", but the RESPONSE SHAPE changed: results are
	// now nested under worlds[].players[] and the context array is
	// mappingContexts (was appliedContexts, with imc -> path). A caller reading
	// the old shape must be updated - this is name compatibility, not contract
	// compatibility.
	Registry.RegisterHandler(TEXT("get_applied_imcs"), &GetInputMappingContexts);
	Registry.RegisterHandler(TEXT("list_imc_mappings"), &ReadImc);
	Registry.RegisterHandler(TEXT("add_imc_mapping"), &AddImcMapping);
	Registry.RegisterHandler(TEXT("set_mapping_modifiers"), &SetMappingModifiers);
	Registry.RegisterHandler(TEXT("remove_imc_mapping"), &RemoveImcMapping);
	Registry.RegisterHandler(TEXT("set_imc_mapping_key"), &SetImcMappingKey);
	Registry.RegisterHandler(TEXT("set_imc_mapping_action"), &SetImcMappingAction);
	// V11 Enhanced Input depth: the read half authoring never had, the
	// action's own instanced trigger/modifier arrays, live apply/remove of a
	// mapping context, the live action value, and the audit.
	Registry.RegisterHandler(TEXT("read_input_action"), &ReadInputAction);
	Registry.RegisterHandler(TEXT("set_action_triggers"), &SetActionTriggers);
	Registry.RegisterHandler(TEXT("apply_mapping_context"), &ApplyMappingContext);
	Registry.RegisterHandler(TEXT("remove_mapping_context"), &RemoveMappingContext);
	Registry.RegisterHandler(TEXT("get_action_value"), &GetActionValue);
	Registry.RegisterHandler(TEXT("validate_input"), &ValidateInput);
	// T18 Mass Entity and Zone Graph. ensure_mass_entity_config and
	// read_mass_entity_config already ship from MassHandlers.cpp.
	Registry.RegisterHandler(TEXT("list_mass_types"), &ListMassTypes);
	Registry.RegisterHandler(TEXT("remove_mass_trait"), &RemoveMassTrait);
	Registry.RegisterHandler(TEXT("reorder_mass_traits"), &ReorderMassTraits);
	Registry.RegisterHandler(TEXT("validate_mass_entity_config"), &ValidateMassEntityConfig);
	Registry.RegisterHandler(TEXT("query_zone_graph"), &QueryZoneGraph);
	Registry.RegisterHandler(TEXT("create_blackboard"), &CreateBlackboard);
	Registry.RegisterHandler(TEXT("create_behavior_tree"), &CreateBehaviorTree);
	Registry.RegisterHandler(TEXT("create_eqs_query"), &CreateEqsQuery);
	Registry.RegisterHandler(TEXT("list_eqs_types"), &ListEqsTypes);
	Registry.RegisterHandler(TEXT("read_eqs_query"), &ReadEqsQuery);
	Registry.RegisterHandler(TEXT("add_eqs_generator"), &AddEqsGenerator);
	Registry.RegisterHandler(TEXT("add_eqs_test"), &AddEqsTest);
	Registry.RegisterHandler(TEXT("remove_eqs_test"), &RemoveEqsTest);
	Registry.RegisterHandler(TEXT("remove_eqs_option"), &RemoveEqsOption);
	Registry.RegisterHandler(TEXT("get_bt_runtime"), &GetBtRuntime);
	Registry.RegisterHandler(TEXT("get_live_blackboard"), &GetLiveBlackboard);
	Registry.RegisterHandler(TEXT("set_live_blackboard"), &SetLiveBlackboard);
	Registry.RegisterHandler(TEXT("run_behavior_tree"), &RunBehaviorTree);
	Registry.RegisterHandler(TEXT("stop_behavior_tree"), &StopBehaviorTree);
	Registry.RegisterHandler(TEXT("list_ai_agents"), &ListAiAgents);
	Registry.RegisterHandler(TEXT("read_perception"), &ReadPerception);
	Registry.RegisterHandler(TEXT("remove_sense"), &RemoveSense);
	Registry.RegisterHandler(TEXT("get_perceived_actors"), &GetPerceivedActors);
	Registry.RegisterHandler(TEXT("check_perception"), &CheckPerception);
	Registry.RegisterHandler(TEXT("report_noise_event"), &ReportNoiseEvent);
	Registry.RegisterHandler(TEXT("reorder_eqs_tests"), &ReorderEqsTests);
	Registry.RegisterHandler(TEXT("run_eqs_query"), &RunEqsQuery);
	Registry.RegisterHandler(TEXT("create_state_tree"), &CreateStateTree);
	Registry.RegisterHandler(TEXT("get_input_mapping_contexts"), &GetInputMappingContexts);
	Registry.RegisterHandler(TEXT("get_state_tree_runtime"), &GetStateTreeRuntime);
	Registry.RegisterHandler(TEXT("create_game_mode"), &CreateGameMode);
	Registry.RegisterHandler(TEXT("create_game_state"), &CreateGameState);
	Registry.RegisterHandler(TEXT("create_player_controller"), &CreatePlayerController);
	Registry.RegisterHandler(TEXT("create_player_state"), &CreatePlayerState);
	Registry.RegisterHandler(TEXT("create_hud"), &CreateHud);
	Registry.RegisterHandler(TEXT("spawn_nav_modifier_volume"), &SpawnNavModifierVolume);
	Registry.RegisterHandler(TEXT("set_world_game_mode"), &SetWorldGameMode);
	Registry.RegisterHandler(TEXT("add_blackboard_key"), &AddBlackboardKey);
	// #469: set parent on BlackboardData so a child Blackboard can extend the
	// parent's keys (canonical UE pattern for extending third-party AI assets).
	Registry.RegisterHandler(TEXT("set_blackboard_parent"), &SetBlackboardParent);
	Registry.RegisterHandler(TEXT("remove_blackboard_key"), &RemoveBlackboardKey);
	Registry.RegisterHandler(TEXT("read_blackboard"), &ReadBlackboard);
	// #494: discover available BT node classes (composites, tasks, decorators, services).
	Registry.RegisterHandler(TEXT("list_bt_node_classes"), &ListBTNodeClasses);
	Registry.RegisterHandler(TEXT("set_behavior_tree_blackboard"), &SetBehaviorTreeBlackboard);
	Registry.RegisterHandler(TEXT("rebuild_navigation"), &RebuildNavmesh);
	Registry.RegisterHandler(TEXT("find_nav_path"), &FindNavPath);
	Registry.RegisterHandler(TEXT("list_nav_invokers"), &ListNavInvokers);
	// New handlers
	Registry.RegisterHandler(TEXT("get_behavior_tree_info"), &GetBehaviorTreeInfo);
	Registry.RegisterHandler(TEXT("read_behavior_tree_graph"), &ReadBehaviorTreeGraph);
	// #919: pick BT nodes and read only their own UPROPERTY values.
	Registry.RegisterHandler(TEXT("read_bt_node_properties"), &ReadBTNodeProperties);
	// #940: inventory BTTask nodes, FilterClass included.
	Registry.RegisterHandler(TEXT("list_bt_tasks"), &ListBTTasks);
	// #919/#940: one scoped write onto an owned BT node subobject. Registered
	// under both names because a caller reaching for the task-specific one
	// should not have to know it is the general node setter.
	Registry.RegisterHandler(TEXT("set_bt_node_property"), &SetBTNodeProperty);
	Registry.RegisterHandler(TEXT("set_bt_task_property"), &SetBTNodeProperty);
	// #889/#947: author the BT editor graph and recompile it into the runnable
	// tree. Reading and writing node properties only reaches nodes that already
	// exist; these are what put nodes there, reconnect them and reorder them.
	Registry.RegisterHandler(TEXT("list_bt_graph_nodes"), &ListBTGraphNodes);
	Registry.RegisterHandler(TEXT("add_bt_node"), &AddBTNode);
	Registry.RegisterHandler(TEXT("move_bt_node"), &MoveBTNode);
	Registry.RegisterHandler(TEXT("remove_bt_node"), &RemoveBTNode);
	Registry.RegisterHandler(TEXT("add_perception_component"), &AddPerceptionComponent);
	Registry.RegisterHandler(TEXT("configure_ai_perception_sense"), &ConfigureAiPerceptionSense);
	Registry.RegisterHandler(TEXT("add_state_tree_component"), &AddStateTreeComponent);
	Registry.RegisterHandler(TEXT("add_smart_object_component"), &AddSmartObjectComponent);
	Registry.RegisterHandler(TEXT("add_smart_object_slot"), &AddSmartObjectSlot);
	Registry.RegisterHandler(TEXT("set_smart_object_slot"), &SetSmartObjectSlot);
	Registry.RegisterHandler(TEXT("remove_smart_object_slot"), &RemoveSmartObjectSlot);
	Registry.RegisterHandler(TEXT("list_smart_object_slots"), &ListSmartObjectSlots);
	Registry.RegisterHandler(TEXT("add_smart_object_slot_behavior"), &AddSmartObjectSlotBehavior);
	Registry.RegisterHandler(TEXT("add_smart_object_default_behavior"), &AddSmartObjectDefaultBehavior);
	// read_imc through get_pie_subsystem_state moved to pie-studio
	Registry.RegisterHandler(TEXT("get_navmesh_details"), &GetNavmeshDetails);
	// apply_damage_in_pie moved to pie-studio
}

// ── #416: SmartObject slot authoring (reflection-only) ────────────────

namespace
{
	// Load a USmartObjectDefinition by asset path and locate its Slots TArray
	// property. Returns: the asset, the array property, and a writable script
	// array helper. Uses pure reflection so we don't have to depend on the
	// SmartObjectsModule at build time.
	struct FSlotsAccess
	{
		UObject* Asset = nullptr;
		FArrayProperty* SlotsProp = nullptr;
		FStructProperty* SlotStruct = nullptr;
		void* ArrayAddr = nullptr;
	};

	static TSharedPtr<FJsonValue> ResolveSlots(const TSharedPtr<FJsonObject>& Params, FSlotsAccess& Out)
	{
		FString AssetPath;
		if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset) return MCPError(FString::Printf(TEXT("SmartObjectDefinition not found: %s"), *AssetPath));
		UClass* Cls = Asset->GetClass();
		if (Cls->GetName() != TEXT("SmartObjectDefinition"))
		{
			return MCPError(FString::Printf(TEXT("Asset '%s' is %s, not a SmartObjectDefinition"), *AssetPath, *Cls->GetName()));
		}
		FProperty* Prop = Cls->FindPropertyByName(FName(TEXT("Slots")));
		FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop);
		if (!ArrProp)
		{
			return MCPError(TEXT("SmartObjectDefinition has no 'Slots' TArray property (engine layout changed?)"));
		}
		FStructProperty* SlotStruct = CastField<FStructProperty>(ArrProp->Inner);
		if (!SlotStruct)
		{
			return MCPError(TEXT("Slots inner is not a struct"));
		}
		Out.Asset = Asset;
		Out.SlotsProp = ArrProp;
		Out.SlotStruct = SlotStruct;
		Out.ArrayAddr = ArrProp->ContainerPtrToValuePtr<void>(Asset);
		return nullptr;
	}

	// Apply optional offset/rotation/tags JSON fields onto a slot struct in place.
	static FString ApplySlotFieldsFromJson(FStructProperty* SlotStruct, void* SlotAddr, const TSharedPtr<FJsonObject>& Src)
	{
		auto SetField = [&](const TCHAR* PropName, const TSharedPtr<FJsonValue>& Val) -> FString
		{
			FProperty* P = SlotStruct->Struct->FindPropertyByName(FName(PropName));
			if (!P) return FString::Printf(TEXT("Slot has no '%s'"), PropName);
			void* PV = P->ContainerPtrToValuePtr<void>(SlotAddr);
			FString E;
			if (!MCPJsonProperty::SetJsonOnProperty(P, PV, Val, E))
			{
				return FString::Printf(TEXT("Failed to set '%s': %s"), PropName, *E);
			}
			return FString();
		};
		const TSharedPtr<FJsonObject>* SubObj = nullptr;
		FString Err;
		if (Src->TryGetObjectField(TEXT("offset"), SubObj))
		{
			Err = SetField(TEXT("Offset"), MakeShared<FJsonValueObject>(*SubObj));
			if (!Err.IsEmpty()) return Err;
		}
		if (Src->TryGetObjectField(TEXT("rotation"), SubObj))
		{
			Err = SetField(TEXT("Rotation"), MakeShared<FJsonValueObject>(*SubObj));
			if (!Err.IsEmpty()) return Err;
		}
		const TArray<TSharedPtr<FJsonValue>>* TagArr = nullptr;
		if (Src->TryGetArrayField(TEXT("tags"), TagArr) && TagArr)
		{
			Err = SetField(TEXT("RuntimeTags"), MakeShared<FJsonValueArray>(*TagArr));
			if (!Err.IsEmpty()) return Err;
		}
		FString NameStr;
		if (Src->TryGetStringField(TEXT("name"), NameStr))
		{
			FProperty* P = SlotStruct->Struct->FindPropertyByName(FName(TEXT("Name")));
			if (P)
			{
				FNameProperty* NP = CastField<FNameProperty>(P);
				if (NP) NP->SetPropertyValue(NP->ContainerPtrToValuePtr<void>(SlotAddr), FName(*NameStr));
			}
		}
		return FString();
	}

	// ── #833: behavior definitions ────────────────────────────────────────
	//
	// USmartObjectDefinition::Validate refuses a definition whose slot has no
	// behavior definition when the definition carries no default either:
	//   "Slot at index N needs to provide a behavior definition since there is
	//    no default one in the SmartObject definition"
	// Slots were addable and behaviors were not settable at create time, and
	// DefaultBehaviorDefinitions had no route at all, so every definition the
	// bridge built with slots failed the editor's own asset check.

	static UClass* BehaviorDefinitionBaseClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectBehaviorDefinition"));
	}

	// Accepts a behavior-definition asset path or a class spelling, and
	// instances the class under the definition (the arrays are Instanced).
	static UObject* ResolveBehaviorDefinition(UObject* Outer, const FString& Spec, FString& OutError)
	{
		if (UObject* Existing = LoadObject<UObject>(nullptr, *Spec))
		{
			if (!Existing->IsA<UClass>()) return Existing;
		}
		UClass* Base = BehaviorDefinitionBaseClass();
		UClass* BehaviorClass = Base
			? MCPResolveClassOfType(Spec, Base)
			: MCPResolveClass(Spec);
		if (!BehaviorClass)
		{
			OutError = FString::Printf(
				TEXT("Behavior definition class not found: '%s'. Pass a USmartObjectBehaviorDefinition subclass ")
				TEXT("(/Script/<Module>.<Class>) or the path of an existing behavior asset. ")
				TEXT("The engine's concrete behavior definitions ship in optional plugins: ")
				TEXT("GameplayBehaviorSmartObjects (GameplayBehaviorSmartObjectBehaviorDefinition) and ")
				TEXT("MassGameplay (SmartObjectMassBehaviorDefinition). Enable one with project(enable_plugin) if this ")
				TEXT("project has none. reflection(list_classes, parentFilter=\"SmartObjectBehaviorDefinition\") lists ")
				TEXT("what is loaded right now."),
				*Spec);
			return nullptr;
		}
		if (BehaviorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = FString::Printf(
				TEXT("'%s' resolves to %s, which is abstract and cannot be instanced. Name a concrete subclass."),
				*Spec, *BehaviorClass->GetPathName());
			return nullptr;
		}
		return NewObject<UObject>(Outer, BehaviorClass);
	}

	// Append one behavior instance to a TArray<TObjectPtr<USmartObjectBehaviorDefinition>>
	// reached by reflection - the slot's BehaviorDefinitions or the definition's
	// DefaultBehaviorDefinitions, which are the same shape.
	static FString AppendBehaviorDefinition(
		UObject* Asset,
		UStruct* Owner,
		void* Container,
		const TCHAR* PropertyName,
		const FString& Spec,
		const TSharedPtr<FJsonObject>* InstanceProperties,
		UObject*& OutInstance,
		int32& OutIndex)
	{
		FArrayProperty* Arr = CastField<FArrayProperty>(Owner->FindPropertyByName(FName(PropertyName)));
		if (!Arr) return FString::Printf(TEXT("No '%s' TArray property (engine layout changed?)"), PropertyName);
		FObjectProperty* Inner = CastField<FObjectProperty>(Arr->Inner);
		if (!Inner) return FString::Printf(TEXT("'%s' inner is not a UObject*"), PropertyName);

		FString ResolveError;
		OutInstance = ResolveBehaviorDefinition(Asset, Spec, ResolveError);
		if (!OutInstance) return ResolveError;

		if (InstanceProperties && (*InstanceProperties).IsValid())
		{
			for (const auto& Pair : (*InstanceProperties)->Values)
			{
				FProperty* P = OutInstance->GetClass()->FindPropertyByName(FName(*Pair.Key));
				if (!P) continue;
				FString E;
				MCPJsonProperty::SetJsonOnProperty(P, P->ContainerPtrToValuePtr<void>(OutInstance), Pair.Value, E);
			}
		}

		FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Container));
		OutIndex = Helper.AddValue();
		Inner->SetObjectPropertyValue(Helper.GetRawPtr(OutIndex), OutInstance);
		return FString();
	}

	static int32 CountDefaultBehaviorDefinitions(UObject* Asset)
	{
		FArrayProperty* Arr = CastField<FArrayProperty>(
			Asset->GetClass()->FindPropertyByName(FName(TEXT("DefaultBehaviorDefinitions"))));
		if (!Arr) return 0;
		FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Asset));
		return Helper.Num();
	}

	// The engine's rule, restated where the bridge can act on it: a slot with
	// no behavior definition is only legal when the definition has a default.
	// Reported on every write so a caller learns the asset is unusable at the
	// call that made it so, rather than from the editor's asset check later.
	static void ReportDefinitionValidity(const FSlotsAccess& SA, TSharedPtr<FJsonObject>& Result)
	{
		const int32 DefaultCount = CountDefaultBehaviorDefinitions(SA.Asset);
		FScriptArrayHelper Slots(SA.SlotsProp, SA.ArrayAddr);
		FArrayProperty* SlotBehaviors = CastField<FArrayProperty>(
			SA.SlotStruct->Struct->FindPropertyByName(FName(TEXT("BehaviorDefinitions"))));

		TArray<TSharedPtr<FJsonValue>> Offenders;
		if (DefaultCount == 0 && SlotBehaviors)
		{
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				FScriptArrayHelper Behaviors(
					SlotBehaviors, SlotBehaviors->ContainerPtrToValuePtr<void>(Slots.GetRawPtr(i)));
				if (Behaviors.Num() == 0) Offenders.Add(MakeShared<FJsonValueNumber>(i));
			}
		}

		Result->SetNumberField(TEXT("defaultBehaviorCount"), DefaultCount);
		Result->SetBoolField(TEXT("definitionValid"), Offenders.Num() == 0);
		if (Offenders.Num() > 0)
		{
			Result->SetArrayField(TEXT("slotsMissingBehavior"), Offenders);
			Result->SetStringField(TEXT("validationError"), FString::Printf(
				TEXT("%d slot(s) provide no behavior definition and the definition has no default one, so the editor's ")
				TEXT("asset check rejects this asset (\"Slot at index N needs to provide a behavior definition since ")
				TEXT("there is no default one in the SmartObject definition\"). Fix it either way: ")
				TEXT("gameplay(add_smart_object_slot_behavior, slotIndex, behaviorClass) per slot, or ")
				TEXT("gameplay(add_smart_object_default_behavior, behaviorClass) once for the whole definition."),
				Offenders.Num()));
		}
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateSmartObjectDefinition(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI/SmartObjects"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* SmartObjectDefClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectDefinition"));
	if (!SmartObjectDefClass)
	{
		return MCPError(TEXT("SmartObjectDefinition class not found. Enable SmartObjects plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("SmartObjectDefinition"), SmartObjectDefClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	auto Result = MCPSuccess();
	MCPSetCreated(Result);

	// #833: a definition with slots and no behavior definition anywhere fails
	// the editor's asset check. Setting the default here is the one-call way to
	// make every slot added later legal, and it had no route before.
	const FString DefaultBehavior = OptionalString(Params, TEXT("defaultBehaviorClass"));
	if (!DefaultBehavior.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* InstanceProps = nullptr;
		Params->TryGetObjectField(TEXT("instanceProperties"), InstanceProps);
		UObject* Instance = nullptr;
		int32 Index = INDEX_NONE;
		Created.Asset->Modify();
		const FString Err = AppendBehaviorDefinition(
			Created.Asset, Created.Asset->GetClass(), Created.Asset,
			TEXT("DefaultBehaviorDefinitions"), DefaultBehavior, InstanceProps, Instance, Index);
		if (!Err.IsEmpty())
		{
			// Nothing usable was written, so do not leave the asset behind: a
			// definition whose default silently failed is the broken shape.
			UEditorAssetLibrary::DeleteAsset(Created.Asset->GetPathName());
			return MCPError(Err);
		}
		Created.Asset->PostEditChange();
		Result->SetStringField(TEXT("defaultBehavior"), Instance->GetClass()->GetPathName());
		Result->SetNumberField(TEXT("defaultBehaviorIndex"), Index);
	}

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetNumberField(TEXT("defaultBehaviorCount"), CountDefaultBehaviorDefinitions(Created.Asset));
	// No slots yet, so the definition is valid either way; the field is set so
	// a caller reads the same key on create as on every slot write.
	Result->SetBoolField(TEXT("definitionValid"), true);
	if (DefaultBehavior.IsEmpty())
	{
		Result->SetStringField(TEXT("note"),
			TEXT("This definition has no default behavior definition. That is legal while it has no slots, but the ")
			TEXT("editor's asset check rejects any slot added later that carries no behavior of its own. Pass ")
			TEXT("defaultBehaviorClass here, or behaviorClass on gameplay(add_smart_object_slot)."));
	}
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

// #833: add a default behavior definition to an EXISTING definition. The
// per-slot route (add_smart_object_slot_behavior) shipped; the definition-wide
// default it falls back to had no route, so a definition already carrying
// slots could not be made valid without editing it by hand.
TSharedPtr<FJsonValue> FGameplayHandlers::AddSmartObjectDefaultBehavior(const TSharedPtr<FJsonObject>& Params)
{
	FSlotsAccess SA;
	if (auto Err = ResolveSlots(Params, SA)) return Err;
	FString BehaviorSpec;
	if (auto Err = RequireString(Params, TEXT("behaviorClass"), BehaviorSpec)) return Err;

	const TSharedPtr<FJsonObject>* InstanceProps = nullptr;
	Params->TryGetObjectField(TEXT("instanceProperties"), InstanceProps);

	SA.Asset->Modify();
	UObject* Instance = nullptr;
	int32 Index = INDEX_NONE;
	const FString Err = AppendBehaviorDefinition(
		SA.Asset, SA.Asset->GetClass(), SA.Asset,
		TEXT("DefaultBehaviorDefinitions"), BehaviorSpec, InstanceProps, Instance, Index);
	if (!Err.IsEmpty()) return MCPError(Err);

	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetStringField(TEXT("behavior"), Instance->GetClass()->GetPathName());
	Result->SetNumberField(TEXT("behaviorIndex"), Index);
	ReportDefinitionValidity(SA, Result);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("Nothing in this surface removes an entry from DefaultBehaviorDefinitions, so adding one has no inverse ")
		TEXT("call. The entry added here is at index %d if it has to be cleared by hand."), Index));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FGameplayHandlers::AddSmartObjectSlot(const TSharedPtr<FJsonObject>& Params)
{
	FSlotsAccess SA;
	if (auto Err = ResolveSlots(Params, SA)) return Err;
	SA.Asset->Modify();
	FScriptArrayHelper Helper(SA.SlotsProp, SA.ArrayAddr);
	const int32 NewIdx = Helper.AddValue();
	void* SlotAddr = Helper.GetRawPtr(NewIdx);
	const FString ApplyErr = ApplySlotFieldsFromJson(SA.SlotStruct, SlotAddr, Params);
	if (!ApplyErr.IsEmpty())
	{
		Helper.RemoveValues(NewIdx, 1);
		return MCPError(ApplyErr);
	}

	// #833: a slot with no behavior definition is what the editor's asset check
	// rejects, so the slot can be born with one instead of being added and then
	// repaired by a second call.
	const FString BehaviorSpec = OptionalString(Params, TEXT("behaviorClass"));
	FString AddedBehavior;
	if (!BehaviorSpec.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* InstanceProps = nullptr;
		Params->TryGetObjectField(TEXT("instanceProperties"), InstanceProps);
		UObject* Instance = nullptr;
		int32 BehaviorIndex = INDEX_NONE;
		const FString BehaviorErr = AppendBehaviorDefinition(
			SA.Asset, SA.SlotStruct->Struct, SlotAddr,
			TEXT("BehaviorDefinitions"), BehaviorSpec, InstanceProps, Instance, BehaviorIndex);
		if (!BehaviorErr.IsEmpty())
		{
			Helper.RemoveValues(NewIdx, 1);
			return MCPError(BehaviorErr);
		}
		AddedBehavior = Instance->GetClass()->GetPathName();
	}

	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), NewIdx);
	Result->SetNumberField(TEXT("slotCount"), Helper.Num());
	if (!AddedBehavior.IsEmpty()) Result->SetStringField(TEXT("behavior"), AddedBehavior);
	ReportDefinitionValidity(SA, Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Payload->SetNumberField(TEXT("slotIndex"), NewIdx);
	MCPSetRollback(Result, TEXT("remove_smart_object_slot"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetSmartObjectSlot(const TSharedPtr<FJsonObject>& Params)
{
	FSlotsAccess SA;
	if (auto Err = ResolveSlots(Params, SA)) return Err;
	int32 SlotIdx = -1;
	if (!Params->TryGetNumberField(TEXT("slotIndex"), SlotIdx) || SlotIdx < 0)
	{
		return MCPError(TEXT("Missing 'slotIndex' (non-negative integer)"));
	}
	FScriptArrayHelper Helper(SA.SlotsProp, SA.ArrayAddr);
	if (SlotIdx >= Helper.Num())
	{
		return MCPError(FString::Printf(TEXT("slotIndex %d out of range (0-%d)"), SlotIdx, Helper.Num() - 1));
	}
	SA.Asset->Modify();
	void* SlotAddr = Helper.GetRawPtr(SlotIdx);

	// The slot's current values for exactly the fields this call is about to
	// overwrite, read before the write and in the JSON shape this same action
	// takes back. FMCPJsonSerializer::SerializeValue is the read half of the
	// MCPJsonProperty::SetJsonOnProperty that ApplySlotFieldsFromJson uses, so
	// the round trip is the pair rather than two hand-written encodings.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	int32 CapturedFields = 0;
	{
		auto CapturePrevious = [&](const TCHAR* ParamName, const TCHAR* PropName)
		{
			FProperty* P = SA.SlotStruct->Struct->FindPropertyByName(FName(PropName));
			if (!P) return;
			TSharedPtr<FJsonValue> Previous =
				FMCPJsonSerializer::SerializeValue(P->ContainerPtrToValuePtr<void>(SlotAddr), P);
			if (!Previous.IsValid()) return;
			RollbackPayload->SetField(ParamName, Previous);
			++CapturedFields;
		};
		// The SAME type-checked accessors ApplySlotFieldsFromJson writes
		// through. Probing with HasField instead would capture - and emit a
		// record for - a field the writer skipped because its JSON was the
		// wrong type, e.g. offset given as a string.
		const TSharedPtr<FJsonObject>* ObjProbe = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ArrProbe = nullptr;
		FString StrProbe;
		if (Params->TryGetObjectField(TEXT("offset"), ObjProbe))   CapturePrevious(TEXT("offset"),   TEXT("Offset"));
		if (Params->TryGetObjectField(TEXT("rotation"), ObjProbe)) CapturePrevious(TEXT("rotation"), TEXT("Rotation"));
		if (Params->TryGetArrayField(TEXT("tags"), ArrProbe))      CapturePrevious(TEXT("tags"),     TEXT("RuntimeTags"));
		if (Params->TryGetStringField(TEXT("name"), StrProbe))     CapturePrevious(TEXT("name"),     TEXT("Name"));
	}

	// The whole slot as text, before and after. Exact, and it covers every
	// field the writer touches without a second per-field comparison.
	FString SlotTextBefore;
	SA.SlotStruct->ExportTextItem_Direct(SlotTextBefore, SlotAddr, nullptr, nullptr, PPF_None);

	const FString ApplyErr = ApplySlotFieldsFromJson(SA.SlotStruct, SlotAddr, Params);
	if (!ApplyErr.IsEmpty()) return MCPError(ApplyErr);

	FString SlotTextAfter;
	SA.SlotStruct->ExportTextItem_Direct(SlotTextAfter, SlotAddr, nullptr, nullptr, PPF_None);
	const bool bSlotChanged = SlotTextAfter != SlotTextBefore;
	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	if (bSlotChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bSlotChanged);
	Result->SetNumberField(TEXT("fieldsWritten"), CapturedFields);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), SlotIdx);
	if (CapturedFields > 0 && bSlotChanged)
	{
		RollbackPayload->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
		RollbackPayload->SetNumberField(TEXT("slotIndex"), SlotIdx);
		MCPSetRollback(Result, TEXT("set_smart_object_slot"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), CapturedFields == 0
			? TEXT("None of offset, rotation, tags or name was passed in a form this action writes, so nothing on the "
				   "slot was written and there is nothing to undo.")
			: TEXT("The slot already held the values this call wrote, so nothing changed and there is nothing to "
				   "undo."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveSmartObjectSlot(const TSharedPtr<FJsonObject>& Params)
{
	FSlotsAccess SA;
	if (auto Err = ResolveSlots(Params, SA)) return Err;
	int32 SlotIdx = -1;
	if (!Params->TryGetNumberField(TEXT("slotIndex"), SlotIdx) || SlotIdx < 0)
	{
		return MCPError(TEXT("Missing 'slotIndex' (non-negative integer)"));
	}
	FScriptArrayHelper Helper(SA.SlotsProp, SA.ArrayAddr);
	if (SlotIdx >= Helper.Num())
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
		Noop->SetNumberField(TEXT("slotIndex"), SlotIdx);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}
	// Everything about the slot, read before it goes. The exported text is for
	// the human reading the response; the per-field JSON below is the only part
	// gameplay(add_smart_object_slot) can actually take back.
	void* DoomedSlot = Helper.GetRawPtr(SlotIdx);
	FString RemovedSlotText;
	SA.SlotStruct->ExportTextItem_Direct(RemovedSlotText, DoomedSlot, nullptr, nullptr, PPF_None);

	// Re-adding APPENDS, so the index only survives when the slot being removed
	// is the last one. Anywhere else, restoring would shift every later slot and
	// hand the caller a differently indexed slot than it had.
	const bool bWasLastSlot = (SlotIdx == Helper.Num() - 1);
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	int32 RemovedBehaviorCount = 0;
	if (bWasLastSlot)
	{
		const TCHAR* const Fields[][2] = {
			{ TEXT("offset"),   TEXT("Offset") },
			{ TEXT("rotation"), TEXT("Rotation") },
			{ TEXT("tags"),     TEXT("RuntimeTags") },
			{ TEXT("name"),     TEXT("Name") },
		};
		for (const auto& Field : Fields)
		{
			FProperty* P = SA.SlotStruct->Struct->FindPropertyByName(FName(Field[1]));
			if (!P) continue;
			TSharedPtr<FJsonValue> Previous =
				FMCPJsonSerializer::SerializeValue(P->ContainerPtrToValuePtr<void>(DoomedSlot), P);
			if (Previous.IsValid()) RollbackPayload->SetField(Field[0], Previous);
		}
		if (FArrayProperty* BDArr = CastField<FArrayProperty>(
			SA.SlotStruct->Struct->FindPropertyByName(FName(TEXT("BehaviorDefinitions")))))
		{
			FScriptArrayHelper BDHelper(BDArr, BDArr->ContainerPtrToValuePtr<void>(DoomedSlot));
			RemovedBehaviorCount = BDHelper.Num();
		}
	}

	SA.Asset->Modify();
	Helper.RemoveValues(SlotIdx, 1);
	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), SlotIdx);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetNumberField(TEXT("slotCount"), Helper.Num());
	Result->SetStringField(TEXT("removedSlot"), RemovedSlotText);
	ReportDefinitionValidity(SA, Result);
	Result->SetBoolField(TEXT("wasLastSlot"), bWasLastSlot);

	if (bWasLastSlot)
	{
		// Appending puts the slot back at the index it had, with the fields
		// add_smart_object_slot accepts. Its BehaviorDefinitions do not come
		// back, because that action has no parameter for them.
		RollbackPayload->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
		MCPSetRollback(Result, TEXT("add_smart_object_slot"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), RemovedBehaviorCount > 0);
		if (RemovedBehaviorCount > 0)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The slot comes back at the same index with its offset, rotation, tags and name, but the %d "
					 "BehaviorDefinition(s) it carried do NOT: gameplay(add_smart_object_slot) takes at most one "
					 "behaviorClass and cannot describe an instanced behavior's own property values. Re-add them with "
					 "gameplay(add_smart_object_slot_behavior); 'removedSlot' names what was there."),
				RemovedBehaviorCount));
		}
	}
	else
	{
		// No inverse for a slot in the middle. Re-adding APPENDS, so restoring
		// would hand back a slot at a different index while every later slot
		// stays shifted - a caller addressing slots by index would then edit the
		// wrong one, which is worse than reporting that nothing can undo this.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This slot was not the last one, so removing it shifted every later slotIndex down by one. "
				 "gameplay(add_smart_object_slot) appends, which would put the slot back at the END rather than "
				 "where it was, leaving every index wrong; no inverse is emitted rather than one that silently "
				 "renumbers the slots. The removed slot is reported in full as 'removedSlot' for manual recovery."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListSmartObjectSlots(const TSharedPtr<FJsonObject>& Params)
{
	FSlotsAccess SA;
	if (auto Err = ResolveSlots(Params, SA)) return Err;
	FScriptArrayHelper Helper(SA.SlotsProp, SA.ArrayAddr);
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("index"), i);
		void* SlotAddr = Helper.GetRawPtr(i);
		// Export the whole struct as text - generic but always readable. Callers
		// who want structured offset/rotation can call set_smart_object_slot to
		// mutate or asset.set_property for typed reads.
		FString Exported;
		SA.SlotStruct->ExportTextItem_Direct(Exported, SlotAddr, nullptr, nullptr, PPF_None);
		S->SetStringField(TEXT("raw"), Exported);
		// Pull out common fields explicitly for ergonomics.
		if (FProperty* Off = SA.SlotStruct->Struct->FindPropertyByName(FName(TEXT("Offset"))))
		{
			if (CastField<FStructProperty>(Off))
			{
				const FVector* V = reinterpret_cast<const FVector*>(Off->ContainerPtrToValuePtr<void>(SlotAddr));
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("x"), V->X); O->SetNumberField(TEXT("y"), V->Y); O->SetNumberField(TEXT("z"), V->Z);
				S->SetObjectField(TEXT("offset"), O);
			}
		}
		if (FProperty* Rot = SA.SlotStruct->Struct->FindPropertyByName(FName(TEXT("Rotation"))))
		{
			if (CastField<FStructProperty>(Rot))
			{
				const FRotator* R = reinterpret_cast<const FRotator*>(Rot->ContainerPtrToValuePtr<void>(SlotAddr));
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("pitch"), R->Pitch); O->SetNumberField(TEXT("yaw"), R->Yaw); O->SetNumberField(TEXT("roll"), R->Roll);
				S->SetObjectField(TEXT("rotation"), O);
			}
		}
		Slots.Add(MakeShared<FJsonValueObject>(S));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotCount"), Helper.Num());
	Result->SetArrayField(TEXT("slots"), Slots);
	ReportDefinitionValidity(SA, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddSmartObjectSlotBehavior(const TSharedPtr<FJsonObject>& Params)
{
	FSlotsAccess SA;
	if (auto Err = ResolveSlots(Params, SA)) return Err;
	int32 SlotIdx = -1;
	if (!Params->TryGetNumberField(TEXT("slotIndex"), SlotIdx) || SlotIdx < 0)
	{
		return MCPError(TEXT("Missing 'slotIndex' (non-negative integer)"));
	}
	FString BehaviorClassPath;
	if (auto Err2 = RequireString(Params, TEXT("behaviorClass"), BehaviorClassPath)) return Err2;

	FScriptArrayHelper Helper(SA.SlotsProp, SA.ArrayAddr);
	if (SlotIdx >= Helper.Num()) return MCPError(FString::Printf(TEXT("slotIndex %d out of range"), SlotIdx));
	void* SlotAddr = Helper.GetRawPtr(SlotIdx);

	// Resolution, instancing and the instanceProperties write are shared with
	// add_smart_object_slot and add_smart_object_default_behavior: one route to
	// a behavior definition, so a name that works on one works on all three.
	const TSharedPtr<FJsonObject>* InstObj = nullptr;
	Params->TryGetObjectField(TEXT("instanceProperties"), InstObj);

	SA.Asset->Modify();
	UObject* BehaviorAsset = nullptr;
	int32 NewBDIdx = INDEX_NONE;
	const FString BehaviorErr = AppendBehaviorDefinition(
		SA.Asset, SA.SlotStruct->Struct, SlotAddr,
		TEXT("BehaviorDefinitions"), BehaviorClassPath, InstObj, BehaviorAsset, NewBDIdx);
	if (!BehaviorErr.IsEmpty()) return MCPError(BehaviorErr);

	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), SlotIdx);
	Result->SetNumberField(TEXT("behaviorIndex"), NewBDIdx);
	Result->SetStringField(TEXT("behavior"), BehaviorAsset->GetClass()->GetPathName());
	ReportDefinitionValidity(SA, Result);
	// No inverse. Nothing in this surface removes an entry from a slot's
	// BehaviorDefinitions: the array is reachable only through this call, which
	// appends. gameplay(remove_smart_object_slot) would delete the whole slot,
	// which undoes far more than adding one behaviour to it.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("No action removes an entry from a SmartObject slot's BehaviorDefinitions, so adding one has no inverse "
			 "call. Removing the slot would undo more than this did. The entry added here is at behaviorIndex %d of "
			 "slot %d if it has to be cleared by hand."),
		NewBDIdx, SlotIdx));
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
	// T3: paged. Two asset classes come back here, so ONE cursor pages ONE
	// collection: every row, each tagged with its `kind`, under `assets`. The
	// two familiar arrays are still emitted and hold this page's rows of that
	// kind, so an existing reader keeps working while the paging fields
	// (count, total, hasMore, nextCursor) describe the whole listing.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_input_assets"), /*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// The registry returns assets in whatever order it scanned them, which is
	// not a contract, so each group is sorted by object path before paging.
	const auto Collect = [&AR](const TCHAR* ClassName, const TCHAR* Kind)
	{
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), ClassName), Assets, true);

		TArray<MCPPagination::FPageRow> Out;
		Out.Reserve(Assets.Num());
		for (const FAssetData& Asset : Assets)
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			AssetObj->SetStringField(TEXT("kind"), Kind);
			// The asset's object path is the page anchor: unique, and stable
			// across two scans of the registry.
			Out.Add({ Asset.GetObjectPathString(), MakeShared<FJsonValueObject>(AssetObj) });
		}
		Out.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
			{ return A.Id < B.Id; });
		return Out;
	};

	TArray<MCPPagination::FPageRow> Rows = Collect(TEXT("InputAction"), TEXT("inputAction"));
	const int32 ActionCount = Rows.Num();
	Rows.Append(Collect(TEXT("InputMappingContext"), TEXT("inputMappingContext")));

	Result->SetNumberField(TEXT("inputActionCount"), ActionCount);
	Result->SetNumberField(TEXT("inputMappingContextCount"), Rows.Num() - ActionCount);
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);

	// This page's rows, regrouped the way this action always reported them.
	TArray<TSharedPtr<FJsonValue>> InputActionArray;
	TArray<TSharedPtr<FJsonValue>> MappingContextArray;
	for (const TSharedPtr<FJsonValue>& Row : Result->GetArrayField(TEXT("assets")))
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Row.IsValid() || !Row->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;
		FString Kind;
		(*Obj)->TryGetStringField(TEXT("kind"), Kind);
		if (Kind == TEXT("inputAction")) InputActionArray.Add(Row);
		else MappingContextArray.Add(Row);
	}
	Result->SetArrayField(TEXT("inputActions"), InputActionArray);
	Result->SetArrayField(TEXT("inputMappingContexts"), MappingContextArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListBehaviorTrees(const TSharedPtr<FJsonObject>& Params)
{
	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_behavior_trees"), /*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("BehaviorTree")), Assets, true);

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		// The asset's object path is the page anchor. Two folders can each hold
		// a BT_Enemy, and a page boundary has to name exactly one of them.
		Rows.Add({ Asset.GetObjectPathString(), MakeShared<FJsonValueObject>(AssetObj) });
	}
	// The registry returns assets in scan order, which is not a contract.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	MCPPagination::EmitPage(Page, Rows, TEXT("behaviorTrees"), Result);
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
	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_state_trees"), /*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/StateTreeModule"), TEXT("StateTree")), Assets, true);

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		// The asset's object path is the page anchor.
		Rows.Add({ Asset.GetObjectPathString(), MakeShared<FJsonValueObject>(AssetObj) });
	}
	// The registry returns assets in scan order, which is not a contract.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	MCPPagination::EmitPage(Page, Rows, TEXT("stateTrees"), Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ProjectPointToNavigation(const TSharedPtr<FJsonObject>& Params)
{
	FVector Point;
	if (auto Err = RequireVec3(Params, TEXT("location"), Point)) return Err;

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

	UClass* BlackboardClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BlackboardData"));
	if (!BlackboardClass)
	{
		return MCPError(TEXT("BlackboardData class not found."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("BlackboardData"), BlackboardClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* BTClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BehaviorTree"));
	if (!BTClass)
	{
		return MCPError(TEXT("BehaviorTree class not found."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("BehaviorTree"), BTClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI/EQS"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// The asset type reads as "Environment Query" in the editor, but the UClass
	// is UEnvQuery. Looking up the display name found nothing, so this action
	// refused every call it was ever given.
	UClass* EQSClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvQuery"));
	if (!EQSClass)
	{
		return MCPError(TEXT(
			"UEnvQuery class not found: the AIModule is not loaded in this editor. "
			"EQS lives in AIModule, which a project with no AI content may never load."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("EnvironmentQuery"), EQSClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/AI"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* STClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
	if (!STClass)
	{
		return MCPError(TEXT("StateTree class not found. Enable StateTree plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("StateTree"), STClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	// #653/#681: a bare UStateTree has no EditorData, so every statetree(*)
	// authoring action failed with "EditorData not found" and nothing could be
	// persisted. Mirror UStateTreeFactory: attach a UStateTreeEditorData with a
	// concrete schema and a root state, then compile - so states/tasks are
	// authorable AND serialize with the asset (survive save/load).
	UStateTree* StateTree = Cast<UStateTree>(Created.Asset);
	if (!StateTree)
	{
		return MCPError(TEXT("Created asset is not a UStateTree"));
	}

	// #833: the schema is not optional dressing. Without one the compiler bails
	// with "does not have a schema" and the asset is written dead, so the
	// resolution failing is a failed create, not a silent substitution.
	MCPStateTreeSchema::FResolution Schema = MCPStateTreeSchema::Resolve(
		OptionalString(Params, TEXT("schema")));
	if (!Schema.SchemaClass)
	{
		// The asset exists at this point. Delete it rather than leave a
		// schema-less tree behind, which is the exact shape being fixed.
		const FString OrphanPath = StateTree->GetPathName();
		UEditorAssetLibrary::DeleteAsset(OrphanPath);
		return MCPStateTreeSchema::UnresolvedError(Schema);
	}

	const MCPStateTreeSchema::FAttachOutcome Attach =
		MCPStateTreeSchema::AttachSchema(StateTree, Schema.SchemaClass);

	FStateTreeCompilerLog Log;
	const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);
	StateTree->MarkPackageDirty();

	SaveAssetPackage(StateTree);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), StateTree->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("schema"), Schema.SchemaClass->GetPathName());
	Result->SetStringField(TEXT("schemaSource"), Schema.Source);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetBoolField(TEXT("createdEditorData"), Attach.bCreatedEditorData);
	if (!Schema.Note.IsEmpty())
	{
		Result->SetStringField(TEXT("schemaNote"), Schema.Note);
		Result->SetArrayField(TEXT("availableSchemas"), MCPStateTreeSchema::ConcreteSchemaPathsJson());
	}
	MCPSetDeleteAssetRollback(Result, StateTree->GetPathName());

	return MCPResult(Result);
}

// #654: read the active state names of a running StateTreeComponent in PIE.
// The component (UStateTreeComponent / UStateTreeAIComponent) keeps its runtime
// data in an FStateTreeInstanceData 'InstanceData' member and its asset in a
// 'StateTreeRef' FStateTreeReference. Access both via reflection (no hard
// GameplayStateTreeModule dependency) and build an execution context to read
// the active states.
TSharedPtr<FJsonValue> FGameplayHandlers::GetStateTreeRuntime(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("pie"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Find a component that carries StateTree runtime data (by having an
	// InstanceData property of type FStateTreeInstanceData + a StateTreeRef).
	UActorComponent* STComp = nullptr;
	FStructProperty* InstanceProp = nullptr;
	FStructProperty* RefProp = nullptr;
	const FString CompName = OptionalString(Params, TEXT("componentName"));
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;
		if (!CompName.IsEmpty() && Comp->GetName() != CompName) continue;
		FStructProperty* IP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("InstanceData")));
		FStructProperty* RP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("StateTreeRef")));
		if (IP && IP->Struct == FStateTreeInstanceData::StaticStruct() &&
			RP && RP->Struct == FStateTreeReference::StaticStruct())
		{
			STComp = Comp; InstanceProp = IP; RefProp = RP; break;
		}
	}
	if (!STComp) return MCPError(FString::Printf(TEXT("No StateTree component found on '%s'"), *ActorLabel));

	FStateTreeReference* Ref = RefProp->ContainerPtrToValuePtr<FStateTreeReference>(STComp);
	const UStateTree* StateTree = Ref ? Ref->GetStateTree() : nullptr;
	FStateTreeInstanceData* InstanceData = InstanceProp->ContainerPtrToValuePtr<FStateTreeInstanceData>(STComp);
	if (!StateTree || !InstanceData) return MCPError(TEXT("StateTree asset or instance data unavailable"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), STComp->GetName());
	Result->SetStringField(TEXT("stateTree"), StateTree->GetPathName());

	if (InstanceData->Num() == 0)
	{
		Result->SetBoolField(TEXT("running"), false);
		Result->SetStringField(TEXT("note"), TEXT("StateTree instance data not initialized (component not running yet). Start PIE / the component's logic first."));
		return MCPResult(Result);
	}

	FStateTreeExecutionContext Context(*STComp, *StateTree, *InstanceData);
	const TArray<FName> ActiveNames = Context.GetActiveStateNames();
	TArray<TSharedPtr<FJsonValue>> Names;
	for (const FName& N : ActiveNames) Names.Add(MakeShared<FJsonValueString>(N.ToString()));

	Result->SetBoolField(TEXT("running"), true);
	Result->SetStringField(TEXT("activeState"), Context.GetActiveStateName());
	Result->SetArrayField(TEXT("activeStates"), Names);
	return MCPResult(Result);
}

// Resolve a class from a short name, a /Script path, or a Blueprint ASSET path.
// A Blueprint's generated class lives at "<path>.<AssetName>_C" - appending a
// bare "_C" to the package path (which is what this used to do) never resolves,
// so every documented "or a Blueprint asset path" call failed.
static UClass* ResolveClassFlexible(const FString& Requested)
{
	if (Requested.IsEmpty()) return nullptr;
	if (UClass* Direct = LoadObject<UClass>(nullptr, *Requested)) return Direct;
	if (UClass* ByName = FindClassByShortName(Requested)) return ByName;
	if (Requested.StartsWith(TEXT("/")))
	{
		FString AssetName = Requested;
		if (!Requested.Contains(TEXT(".")))
		{
			Requested.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			const FString Generated = Requested + TEXT(".") + AssetName + TEXT("_C");
			if (UClass* Gen = LoadObject<UClass>(nullptr, *Generated)) return Gen;
			// Also try the plain object path, for a non-Blueprint asset.
			if (UClass* Obj = LoadObject<UClass>(nullptr, *(Requested + TEXT(".") + AssetName))) return Obj;
		}
		else if (!Requested.EndsWith(TEXT("_C")))
		{
			if (UClass* Gen = LoadObject<UClass>(nullptr, *(Requested + TEXT("_C")))) return Gen;
		}
	}
	return nullptr;
}

// Resolve an optional caller-supplied parentClass, requiring it to derive from
// the framework base the action is for. Returning the engine default silently
// when the caller named a class is how you end up debugging a Blueprint that
// simply is not the thing you asked for.
static bool ResolveFrameworkParent(const TSharedPtr<FJsonObject>& Params, const FString& DefaultPath,
	const TCHAR* FriendlyTypeName, FString& OutPath, TSharedPtr<FJsonValue>& OutError)
{
	OutPath = DefaultPath;
	const FString Requested = OptionalString(Params, TEXT("parentClass"));
	if (Requested.IsEmpty()) return true;

	UClass* Base = FindObject<UClass>(nullptr, *DefaultPath);
	UClass* Resolved = ResolveClassFlexible(Requested);
	if (!Resolved)
	{
		OutError = MCPError(FString::Printf(TEXT("parentClass not found: %s"), *Requested));
		return false;
	}
	if (Base && !Resolved->IsChildOf(Base))
	{
		OutError = MCPError(FString::Printf(
			TEXT("parentClass '%s' does not derive from %s, so it cannot be used as a %s."),
			*Resolved->GetPathName(), *Base->GetName(), FriendlyTypeName));
		return false;
	}
	OutPath = Resolved->GetPathName();
	return true;
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName)
{
	UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		return MCPError(FString::Printf(TEXT("%s class not found: %s"), *FriendlyTypeName, *ParentClassPath));
	}

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	auto Created = MCPCreateAssetIdempotent<UBlueprint>(Name, PackagePath, TEXT("skip"), FriendlyTypeName, BlueprintFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UBlueprint* NewBlueprint = Created.Asset;

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

	FString ParentPath;
	TSharedPtr<FJsonValue> ParentErr;
	if (!ResolveFrameworkParent(Params, TEXT("/Script/Engine.GameModeBase"), TEXT("GameMode"), ParentPath, ParentErr)) return ParentErr;

	return CreateBlueprintWithParent(Name, PackagePath, ParentPath, TEXT("GameMode"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameState(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	FString ParentPath;
	TSharedPtr<FJsonValue> ParentErr;
	if (!ResolveFrameworkParent(Params, TEXT("/Script/Engine.GameStateBase"), TEXT("GameState"), ParentPath, ParentErr)) return ParentErr;

	return CreateBlueprintWithParent(Name, PackagePath, ParentPath, TEXT("GameState"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerController(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	FString ParentPath;
	TSharedPtr<FJsonValue> ParentErr;
	if (!ResolveFrameworkParent(Params, TEXT("/Script/Engine.PlayerController"), TEXT("PlayerController"), ParentPath, ParentErr)) return ParentErr;

	return CreateBlueprintWithParent(Name, PackagePath, ParentPath, TEXT("PlayerController"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerState(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	FString ParentPath;
	TSharedPtr<FJsonValue> ParentErr;
	if (!ResolveFrameworkParent(Params, TEXT("/Script/Engine.PlayerState"), TEXT("PlayerState"), ParentPath, ParentErr)) return ParentErr;

	return CreateBlueprintWithParent(Name, PackagePath, ParentPath, TEXT("PlayerState"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::CreateHud(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Blueprints/GameFramework"));

	FString ParentPath;
	TSharedPtr<FJsonValue> ParentErr;
	if (!ResolveFrameworkParent(Params, TEXT("/Script/Engine.HUD"), TEXT("HUD"), ParentPath, ParentErr)) return ParentErr;

	return CreateBlueprintWithParent(Name, PackagePath, ParentPath, TEXT("HUD"));
}

TSharedPtr<FJsonValue> FGameplayHandlers::SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("NavModifierVolume")))
	{
		return Existing;
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FVector Scale = OptionalVec3(Params, TEXT("scale"), FVector::OneVector);

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

	// areaClass and extent were documented and accepted but never applied.
	// The default AreaClass is UNavArea_Null (see ANavModifierVolume's
	// constructor), which cuts a navmesh hole, so a default volume is useful -
	// what made every one of these inert was the missing brush below.
	FString AreaClassPath = OptionalString(Params, TEXT("areaClass"));
	if (!AreaClassPath.IsEmpty())
	{
		UClass* AreaClass = ResolveClassFlexible(AreaClassPath);
		if (!AreaClass || !AreaClass->IsChildOf(UNavArea::StaticClass()))
		{
			World->DestroyActor(NewVolume);
			return MCPError(FString::Printf(
				TEXT("areaClass '%s' is not a UNavArea subclass (try NavArea_Null, NavArea_Obstacle, or a /Script/... path)."),
				*AreaClassPath));
		}
		NewVolume->SetAreaClass(AreaClass);
	}

	// A bare SpawnActor leaves an AVolume with Brush == nullptr, so the volume
	// has NO geometry at all - it bounds nothing and modifies nothing no matter
	// what area class it carries. Every call this action has ever served
	// produced one of those and reported created: true. Building the brush is
	// therefore unconditional, not gated on the caller passing extent.
	//
	// extent is a half-size in world units, defaulting to a 200-unit box.
	// Reuses the shared helper spawn_volume has used since #238 rather than a
	// hand-rolled UCubeBuilder::Build - the model, polys, brush-component
	// wiring and csgPrepMovingBrush all have to happen, and doing a subset
	// silently yields the same inert actor.
	const FVector Extent = Params->HasField(TEXT("extent"))
		? OptionalVec3(Params, TEXT("extent"), FVector(100.f, 100.f, 100.f))
		: FVector(100.f, 100.f, 100.f);
	if (Extent.X <= 0.f || Extent.Y <= 0.f || Extent.Z <= 0.f)
	{
		World->DestroyActor(NewVolume);
		return MCPError(TEXT("extent must be positive on every axis (it is a half-size, not a corner)."));
	}
	UEMCP::BuildVolumeAsCube(World, NewVolume, Extent);
	// The helper resets scale to one (the brush carries the size), so a caller's
	// scale has to be re-applied after it or it is silently discarded.
	if (!Scale.Equals(FVector::OneVector))
	{
		NewVolume->SetActorScale3D(Scale);
	}

	const FString FinalLabel = NewVolume->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());
	Result->SetStringField(TEXT("areaClass"), NewVolume->GetAreaClass()
		? NewVolume->GetAreaClass()->GetName() : TEXT("(default)"));
	// Read the built bounds back rather than echoing the request: a volume that
	// reports created with a zero extent is the exact failure being fixed here.
	FVector BoundsOrigin, BoundsExtent;
	NewVolume->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);
	TSharedPtr<FJsonObject> ExtentJson = MakeShared<FJsonObject>();
	ExtentJson->SetNumberField(TEXT("x"), BoundsExtent.X);
	ExtentJson->SetNumberField(TEXT("y"), BoundsExtent.Y);
	ExtentJson->SetNumberField(TEXT("z"), BoundsExtent.Z);
	Result->SetObjectField(TEXT("extent"), ExtentJson);

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

	// No inverse. The navmesh is DERIVED data: this recomputes it from the
	// level's geometry and nav bounds, which is a build step rather than an
	// authored change, and the thing it replaces was the stale output of the
	// same computation. Nothing restores a previous navmesh, and nothing would
	// want to - editing the level and rebuilding again is the only way the
	// result changes.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Rebuilding navigation recomputes derived data from the level. There is no call that restores the "
			 "previous navmesh, and none is needed: the result is decided by the level, so a rebuild on an unchanged "
			 "level produces the same navmesh again."));
	return MCPResult(Result);
}

// #424: synchronous path query between two world points. Returns the polyline
// (if any), partial flag, and total length. The standard "why doesn't my AI
// move?" diagnostic.
TSharedPtr<FJsonValue> FGameplayHandlers::FindNavPath(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FVector Start = OptionalVec3(Params, TEXT("start"));
	const FVector End = OptionalVec3(Params, TEXT("end"));

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) return MCPError(TEXT("Navigation system unavailable"));

	// Optional pathfindingContext (actor label) so the path query uses the
	// matching navigation filter / agent.
	AActor* Context = nullptr;
	FString ContextLabel = OptionalString(Params, TEXT("pathfindingContext"));
	if (!ContextLabel.IsEmpty() || Params->HasField(TEXT("pathfindingContextPath")))
	{
		// #983: the context actor decides which navigation filter and agent
		// answer the query, so picking the wrong namesake changes the path.
		FMCPActorSelector ContextSel;
		ContextSel.LabelKey = TEXT("pathfindingContext");
		ContextSel.PathKey = TEXT("pathfindingContextPath");
		ContextSel.Match = EMCPActorMatch::LabelNameOrPath;
		TSharedPtr<FJsonValue> ContextErr;
		Context = MCPResolveActor(World, Params, ContextErr, ContextSel);
		if (!Context && MCPIsAmbiguousActorError(ContextErr)) return ContextErr;
	}

	UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, Start, End, Context);
	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(Start));
	Result->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(End));
	if (!Path)
	{
		Result->SetBoolField(TEXT("valid"), false);
		Result->SetBoolField(TEXT("partial"), false);
		Result->SetNumberField(TEXT("length"), 0.0);
		Result->SetArrayField(TEXT("points"), {});
		return MCPResult(Result);
	}
	Result->SetBoolField(TEXT("valid"), Path->IsValid());
	Result->SetBoolField(TEXT("partial"), Path->IsPartial());
	Result->SetNumberField(TEXT("length"), Path->GetPathLength());
	TArray<TSharedPtr<FJsonValue>> Points;
	for (const FVector& P : Path->PathPoints)
	{
		Points.Add(MakeShared<FJsonValueObject>(MCPVec3ToJsonObject(P)));
	}
	Result->SetArrayField(TEXT("points"), Points);
	return MCPResult(Result);
}

// #424: enumerate every actor in the world carrying a NavigationInvokerComponent
// plus its tile-generation radius. Useful for diagnosing "AI doesn't move
// because there's no nav data tiled here".
TSharedPtr<FJsonValue> FGameplayHandlers::ListNavInvokers(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	UClass* InvokerClass = FindObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavigationInvokerComponent"));
	if (!InvokerClass) InvokerClass = LoadObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavigationInvokerComponent"));
	if (!InvokerClass) return MCPError(TEXT("NavigationInvokerComponent class not found"));

	TArray<TSharedPtr<FJsonValue>> Out;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		TArray<UActorComponent*> Comps;
		A->GetComponents(InvokerClass, Comps);
		for (UActorComponent* Comp : Comps)
		{
			if (!Comp) continue;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("actorLabel"), A->GetActorLabel());
			Entry->SetStringField(TEXT("componentName"), Comp->GetName());
			Entry->SetStringField(TEXT("componentClass"), Comp->GetClass()->GetName());
			// Read TileGenerationRadius + TileRemovalRadius via reflection so we
			// don't link against the NavigationSystem editor module just for
			// these two properties.
			auto ReadFloat = [&](const TCHAR* PropName) -> double
			{
				if (FFloatProperty* FP = CastField<FFloatProperty>(Comp->GetClass()->FindPropertyByName(PropName)))
					return FP->GetPropertyValue_InContainer(Comp);
				if (FDoubleProperty* DP = CastField<FDoubleProperty>(Comp->GetClass()->FindPropertyByName(PropName)))
					return DP->GetPropertyValue_InContainer(Comp);
				return 0.0;
			};
			Entry->SetNumberField(TEXT("tileGenerationRadius"), ReadFloat(TEXT("TileGenerationRadius")));
			Entry->SetNumberField(TEXT("tileRemovalRadius"), ReadFloat(TEXT("TileRemovalRadius")));
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetArrayField(TEXT("invokers"), Out);
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

// Resolve an optional `baseClass` for an Object/Class blackboard key. Leaving
// it at UObject is legal but useless: the BT nodes that consume the key filter
// on this, so an untyped key silently will not bind.
static TSharedPtr<FJsonValue> ResolveBlackboardBaseClass(const TSharedPtr<FJsonObject>& Params,
	UClass* DefaultClass, TObjectPtr<UClass>& OutClass)
{
	OutClass = DefaultClass;
	const FString Requested = OptionalString(Params, TEXT("baseClass"));
	if (Requested.IsEmpty()) return nullptr;

	UClass* Resolved = ResolveClassFlexible(Requested);
	if (!Resolved)
	{
		return MCPError(FString::Printf(TEXT("baseClass not found: %s"), *Requested));
	}
	OutClass = Resolved;
	return nullptr;
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
			// Nothing was declared, so no record: an inverse here would remove a
			// key this call did not create. That is the whole reason a replayed
			// add has to be told apart from a first one.
			Existed->SetBoolField(TEXT("unchanged"), true);
			Existed->SetBoolField(TEXT("rollbackPossible"), false);
			Existed->SetStringField(TEXT("rollbackNote"),
				TEXT("A key of this name was already on the Blackboard, so nothing was added. No inverse is emitted, "
					 "because removing it would delete a key this call did not create."));
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
		// baseClass was accepted and ignored, so Object/Class keys were created
		// untyped (BaseClass = UObject). A Behaviour Tree node that expects a
		// specific class then refuses to bind to the key, with nothing in the
		// creation response hinting why.
		UBlackboardKeyType_Object* ObjKey = NewObject<UBlackboardKeyType_Object>(BlackboardAsset);
		if (auto Err = ResolveBlackboardBaseClass(Params, UObject::StaticClass(), ObjKey->BaseClass)) return Err;
		KeyTypeInstance = ObjKey;
	}
	else if (KeyType == TEXT("Class"))
	{
		UBlackboardKeyType_Class* ClassKey = NewObject<UBlackboardKeyType_Class>(BlackboardAsset);
		if (auto Err = ResolveBlackboardBaseClass(Params, UObject::StaticClass(), ClassKey->BaseClass)) return Err;
		KeyTypeInstance = ClassKey;
	}
	else if (KeyType == TEXT("Enum"))
	{
		UBlackboardKeyType_Enum* EnumKey = NewObject<UBlackboardKeyType_Enum>(BlackboardAsset);
		const FString EnumName = OptionalString(Params, TEXT("enumType"), OptionalString(Params, TEXT("baseClass")));
		if (!EnumName.IsEmpty())
		{
			UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumName);
			if (!Enum && !EnumName.Contains(TEXT(".")))
			{
				Enum = FindObject<UEnum>(nullptr, *(FString(TEXT("/Script/Engine.")) + EnumName));
			}
			if (!Enum)
			{
				for (TObjectIterator<UEnum> It; It; ++It)
				{
					if (It->GetName() == EnumName) { Enum = *It; break; }
				}
			}
			if (!Enum)
			{
				return MCPError(FString::Printf(TEXT("Enum not found for blackboard key: %s"), *EnumName));
			}
			// UBlackboardKeyType_Enum stores the value in a uint8 and the editor
			// refuses an out-of-range enum via ValidateEnum. Setting EnumType
			// directly bypassed that, producing a key that truncates silently
			// at runtime.
			for (int32 EnumIdx = 0; EnumIdx < Enum->NumEnums(); ++EnumIdx)
			{
				// NumEnums() counts the synthetic trailing _MAX entry, whose
				// value is one past the last real one. An enum whose largest
				// value is 255 would therefore be rejected for a 256 nobody can
				// select. It is not a usable key value, so skip it.
				if (Enum->GetNameStringByIndex(EnumIdx).EndsWith(TEXT("_MAX"))) continue;
				if (Enum->HasMetaData(TEXT("Hidden"), EnumIdx)) continue;
				const int64 EnumValue = Enum->GetValueByIndex(EnumIdx);
				if (EnumValue < 0 || EnumValue > 255)
				{
					return MCPError(FString::Printf(
						TEXT("Enum '%s' has a value out of the 0-255 range a Blackboard enum key can hold (%s = %lld), so the key would truncate silently."),
						*Enum->GetName(), *Enum->GetNameStringByIndex(EnumIdx), EnumValue));
				}
			}
			EnumKey->EnumType = Enum;
			EnumKey->EnumName = Enum->GetName();
		}
		KeyTypeInstance = EnumKey;
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
	if (UBlackboardKeyType_Object* AsObj = Cast<UBlackboardKeyType_Object>(KeyTypeInstance))
	{
		Result->SetStringField(TEXT("baseClass"), AsObj->BaseClass ? AsObj->BaseClass->GetPathName() : TEXT("Object"));
	}
	else if (UBlackboardKeyType_Class* AsCls = Cast<UBlackboardKeyType_Class>(KeyTypeInstance))
	{
		Result->SetStringField(TEXT("baseClass"), AsCls->BaseClass ? AsCls->BaseClass->GetPathName() : TEXT("Object"));
	}
	Result->SetNumberField(TEXT("totalKeys"), BlackboardAsset->Keys.Num());
	// #469: rollback via remove_blackboard_key. Exact: the key did not exist
	// before this call - the loop above returned early if it did - so removing
	// it by name restores the Blackboard to what it was. That action is
	// idempotent on a name it cannot find, so a replayed rollback is safe.
	Result->SetBoolField(TEXT("unchanged"), false);
	TSharedPtr<FJsonObject> RollPayload = MakeShared<FJsonObject>();
	RollPayload->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	RollPayload->SetStringField(TEXT("keyName"), KeyName);
	MCPSetRollback(Result, TEXT("remove_blackboard_key"), RollPayload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);

	return MCPResult(Result);
}

// #469: set Parent on BlackboardData. Canonical UE pattern for extending a
// third-party blackboard (e.g. plugin's ACFAIBB) without duplicating its
// keys. Optionally prune duplicate own-keys that the parent already defines.
TSharedPtr<FJsonValue> FGameplayHandlers::SetBlackboardParent(const TSharedPtr<FJsonObject>& Params)
{
	FString BlackboardPath;
	if (auto Err = RequireString(Params, TEXT("blackboardPath"), BlackboardPath)) return Err;

	FString ParentPath;
	const bool bHasParent = Params->TryGetStringField(TEXT("parentPath"), ParentPath);

	const bool bAutoPrune = OptionalBool(Params, TEXT("autoPruneDuplicateKeys"), true);

	UBlackboardData* Child = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!Child) return MCPError(FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));

	const FString PrevParentPath = Child->Parent ? Child->Parent->GetPathName() : TEXT("None");

	UBlackboardData* Parent = nullptr;
	if (bHasParent && !ParentPath.IsEmpty() && !ParentPath.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		Parent = LoadObject<UBlackboardData>(nullptr, *ParentPath);
		if (!Parent) return MCPError(FString::Printf(TEXT("Parent BlackboardData not found: %s"), *ParentPath));
		if (Parent == Child) return MCPError(TEXT("Cannot set blackboard parent to itself"));
		// Walk parent chain to guard against cycles.
		for (UBlackboardData* Walk = Parent->Parent; Walk; Walk = Walk->Parent)
		{
			if (Walk == Child) return MCPError(TEXT("Cycle detected in blackboard parent chain"));
		}
	}

	Child->Modify();
	Child->Parent = Parent;

	TArray<TSharedPtr<FJsonValue>> Pruned;
	if (bAutoPrune && Parent)
	{
		// Collect parent keys for set-membership.
		TSet<FName> ParentKeyNames;
		for (UBlackboardData* Walk = Parent; Walk; Walk = Walk->Parent)
		{
			for (const FBlackboardEntry& E : Walk->Keys)
			{
				ParentKeyNames.Add(E.EntryName);
			}
		}
		for (int32 i = Child->Keys.Num() - 1; i >= 0; --i)
		{
			if (ParentKeyNames.Contains(Child->Keys[i].EntryName))
			{
				Pruned.Add(MakeShared<FJsonValueString>(Child->Keys[i].EntryName.ToString()));
				Child->Keys.RemoveAt(i);
			}
		}
	}

	// Refresh runtime key index cache.
	Child->UpdateKeyIDs();

	Child->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Child->GetPathName());

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	Result->SetStringField(TEXT("parentPath"), Parent ? Parent->GetPathName() : TEXT("None"));
	Result->SetArrayField(TEXT("prunedDuplicates"), Pruned);
	Result->SetNumberField(TEXT("ownKeyCount"), Child->Keys.Num());

	TSharedPtr<FJsonObject> RollPayload = MakeShared<FJsonObject>();
	RollPayload->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	RollPayload->SetStringField(TEXT("parentPath"), PrevParentPath);
	RollPayload->SetBoolField(TEXT("autoPruneDuplicateKeys"), false);
	MCPSetRollback(Result, TEXT("set_blackboard_parent"), RollPayload);

	return MCPResult(Result);
}

// #469: remove a single key from a Blackboard by name. Idempotent.
TSharedPtr<FJsonValue> FGameplayHandlers::RemoveBlackboardKey(const TSharedPtr<FJsonObject>& Params)
{
	FString BlackboardPath;
	if (auto Err = RequireString(Params, TEXT("blackboardPath"), BlackboardPath)) return Err;
	FString KeyName;
	if (auto Err = RequireString(Params, TEXT("keyName"), KeyName)) return Err;

	UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!BB) return MCPError(FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));

	const FName KeyFName(*KeyName);
	int32 RemovedIdx = INDEX_NONE;
	FString RemovedType;
	// What add_blackboard_key needs to put this key back: its keyType in the
	// short spelling that action takes, and the base class or enum an Object,
	// Class or Enum key carries. Read here, before the entry is destroyed.
	FString RollbackKeyType;
	FString RollbackBaseClass;
	for (int32 i = 0; i < BB->Keys.Num(); ++i)
	{
		if (BB->Keys[i].EntryName == KeyFName)
		{
			RemovedIdx = i;
			UBlackboardKeyType* KeyType = BB->Keys[i].KeyType;
			RemovedType = KeyType ? KeyType->GetClass()->GetName() : TEXT("Unknown");
			if (KeyType)
			{
				// "BlackboardKeyType_Bool" is how the class is named; "Bool" is
				// what add_blackboard_key's keyType parameter takes. The list is
				// exactly what that action accepts, checked rather than derived:
				// UBlackboardKeyType_NativeEnum derives straight from
				// UBlackboardKeyType and would otherwise strip to "NativeEnum",
				// and so would any project-defined subclass, producing a record
				// the adder rejects at replay time.
				static const TCHAR* const AdderKeyTypes[] = {
					TEXT("Bool"), TEXT("Int"), TEXT("Float"), TEXT("String"), TEXT("Name"),
					TEXT("Object"), TEXT("Class"), TEXT("Enum"), TEXT("Vector"), TEXT("Rotator"),
				};
				FString ShortType = RemovedType;
				ShortType.RemoveFromStart(TEXT("BlackboardKeyType_"));
				for (const TCHAR* Supported : AdderKeyTypes)
				{
					if (ShortType.Equals(Supported, ESearchCase::CaseSensitive))
					{
						RollbackKeyType = ShortType;
						break;
					}
				}
				if (UBlackboardKeyType_Object* AsObj = Cast<UBlackboardKeyType_Object>(KeyType))
				{
					if (AsObj->BaseClass) RollbackBaseClass = AsObj->BaseClass->GetPathName();
				}
				else if (UBlackboardKeyType_Class* AsCls = Cast<UBlackboardKeyType_Class>(KeyType))
				{
					if (AsCls->BaseClass) RollbackBaseClass = AsCls->BaseClass->GetPathName();
				}
				else if (UBlackboardKeyType_Enum* AsEnum = Cast<UBlackboardKeyType_Enum>(KeyType))
				{
					if (AsEnum->EnumType) RollbackBaseClass = AsEnum->EnumType->GetPathName();
				}
			}
			break;
		}
	}
	if (RemovedIdx == INDEX_NONE)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetBoolField(TEXT("updated"), false);
		Noop->SetStringField(TEXT("blackboardPath"), BlackboardPath);
		Noop->SetStringField(TEXT("keyName"), KeyName);
		Noop->SetNumberField(TEXT("remainingKeys"), BB->Keys.Num());
		Noop->SetBoolField(TEXT("rollbackPossible"), false);
		Noop->SetStringField(TEXT("rollbackNote"),
			TEXT("No key of that name is on this Blackboard, so nothing was removed and there is nothing to put "
				 "back."));
		return MCPResult(Noop);
	}
	BB->Modify();
	BB->Keys.RemoveAt(RemovedIdx);
	BB->UpdateKeyIDs();
	BB->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BB->GetPathName());

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	Result->SetStringField(TEXT("keyName"), KeyName);
	Result->SetStringField(TEXT("removedKeyType"), RemovedType);
	Result->SetNumberField(TEXT("removedIndex"), RemovedIdx);
	Result->SetNumberField(TEXT("remainingKeys"), BB->Keys.Num());

	if (!RollbackKeyType.IsEmpty())
	{
		// The inverse re-declares the key with the same name and type.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("blackboardPath"), BlackboardPath);
		Payload->SetStringField(TEXT("keyName"), KeyName);
		Payload->SetStringField(TEXT("keyType"), RollbackKeyType);
		// add_blackboard_key reads the base class and the enum from the same
		// 'baseClass' parameter, which is why one field covers both.
		if (!RollbackBaseClass.IsEmpty()) Payload->SetStringField(TEXT("baseClass"), RollbackBaseClass);
		MCPSetRollback(Result, TEXT("add_blackboard_key"), Payload);
		// Keys are appended, and UpdateKeyIDs renumbers on every edit, so a
		// restored key lands at the END of the list with a new key ID.
		// Always lossy: add_blackboard_key declares a name and a type and
		// nothing else, so the entry's instance-sync flag and its editor-only
		// description and category are not carried whatever the index does.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The key comes back with the same name and type%s. gameplay(add_blackboard_key) takes no other "
				 "parameters, so the entry's bInstanceSynced flag and its editor-only description and category are "
				 "NOT restored.%s"),
			RollbackBaseClass.IsEmpty() ? TEXT("") : TEXT(", and its base class or enum"),
			RemovedIdx == BB->Keys.Num()
				? TEXT("")
				: *FString::Printf(
					TEXT(" It is also appended at index %d rather than the %d it held, because add_blackboard_key "
						 "appends and UpdateKeyIDs renumbers. Behaviour Tree nodes bind by key NAME, so they still "
						 "resolve; anything reading key IDs directly does not."),
					BB->Keys.Num(), RemovedIdx)));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The removed key's type was '%s', which gameplay(add_blackboard_key) cannot re-declare: it accepts "
				 "Bool, Int, Float, String, Name, Object, Class, Enum, Vector and Rotator, and anything else - a "
				 "NativeEnum key, a project-defined UBlackboardKeyType subclass, or an entry with no KeyType object "
				 "at all - has no spelling in that action. No inverse is emitted rather than one that would be "
				 "refused at replay time."),
			*RemovedType));
	}
	return MCPResult(Result);
}

// #469: read parent + own keys + inherited keys for a Blackboard.
TSharedPtr<FJsonValue> FGameplayHandlers::ReadBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString BlackboardPath;
	if (auto Err = RequireStringAlt(Params, TEXT("blackboardPath"), TEXT("assetPath"), BlackboardPath)) return Err;

	UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!BB) return MCPError(FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));

	auto KeyArrayFor = [](UBlackboardData* From) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FBlackboardEntry& E : From->Keys)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), E.EntryName.ToString());
			Obj->SetStringField(TEXT("type"), E.KeyType ? E.KeyType->GetClass()->GetName() : TEXT("Unknown"));
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	};

	TArray<TSharedPtr<FJsonValue>> InheritedKeys;
	for (UBlackboardData* Walk = BB->Parent; Walk; Walk = Walk->Parent)
	{
		for (const FBlackboardEntry& E : Walk->Keys)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), E.EntryName.ToString());
			Obj->SetStringField(TEXT("type"), E.KeyType ? E.KeyType->GetClass()->GetName() : TEXT("Unknown"));
			Obj->SetStringField(TEXT("from"), Walk->GetPathName());
			InheritedKeys.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	Result->SetStringField(TEXT("parentPath"), BB->Parent ? BB->Parent->GetPathName() : TEXT("None"));
	Result->SetArrayField(TEXT("ownKeys"), KeyArrayFor(BB));
	Result->SetArrayField(TEXT("inheritedKeys"), InheritedKeys);
	Result->SetNumberField(TEXT("ownKeyCount"), BB->Keys.Num());
	Result->SetNumberField(TEXT("inheritedKeyCount"), InheritedKeys.Num());
	return MCPResult(Result);
}

// #494: enumerate every concrete BT node class (composite, task, decorator,
// service). Gives authoring scripts a discoverable list of node classes to
// pass to a future add_bt_node handler, and lets them resolve plugin-supplied
// custom decorators (UBTDecorator_*) without grepping engine + plugin source.
//
// Params: kind? ("composite"|"task"|"decorator"|"service" - default: all)
TSharedPtr<FJsonValue> FGameplayHandlers::ListBTNodeClasses(const TSharedPtr<FJsonObject>& Params)
{
	const FString KindFilter = OptionalString(Params, TEXT("kind"), TEXT("")).ToLower();
	const bool bAll = KindFilter.IsEmpty();
	if (!bAll
		&& KindFilter != TEXT("composite") && KindFilter != TEXT("task")
		&& KindFilter != TEXT("decorator") && KindFilter != TEXT("service"))
	{
		// An unrecognised kind used to fall through every test and return four
		// counts with no arrays at all, which reads as an empty node palette.
		return MCPError(FString::Printf(
			TEXT("'kind' must be one of composite, task, decorator, service, got '%s'. Omit it for every kind."),
			*KindFilter));
	}

	// T3: paged. Four class groups come back here, so ONE cursor pages ONE
	// collection: every row, each tagged with its `kind`, under `classes`. The
	// four familiar arrays are still emitted and hold this page's rows of that
	// kind, while the counts stay counts of the WHOLE listing.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_bt_node_classes|kind=%s"), *KindFilter),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	auto PushClass = [](TArray<MCPPagination::FPageRow>& Out, UClass* C, const TCHAR* Kind)
	{
		if (!C || C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), C->GetName());
		Obj->SetStringField(TEXT("path"), C->GetPathName());
		Obj->SetStringField(TEXT("kind"), Kind);
		// The class PATH is the page anchor, not the short name: two modules can
		// each declare a BTTask_MoveTo, and a page boundary has to name one.
		Out.Add({ C->GetPathName(), MakeShared<FJsonValueObject>(Obj) });
	};

	TArray<MCPPagination::FPageRow> Composites, Tasks, Decorators, Services;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (C->IsChildOf(UBTCompositeNode::StaticClass())) PushClass(Composites, C, TEXT("composite"));
		else if (C->IsChildOf(UBTTaskNode::StaticClass())) PushClass(Tasks, C, TEXT("task"));
		else if (C->IsChildOf(UBTDecorator::StaticClass())) PushClass(Decorators, C, TEXT("decorator"));
		else if (C->IsChildOf(UBTService::StaticClass())) PushClass(Services, C, TEXT("service"));
	}

	// TObjectIterator walks the object hash, whose order is not a contract, so
	// each group is sorted by class path and the groups are then concatenated
	// in a fixed order. Without both, the same page comes back reshuffled and
	// a cursor cannot resume into it.
	const auto ByPath = [](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; };
	Composites.Sort(ByPath);
	Tasks.Sort(ByPath);
	Decorators.Sort(ByPath);
	Services.Sort(ByPath);

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("compositeCount"), Composites.Num());
	Result->SetNumberField(TEXT("taskCount"), Tasks.Num());
	Result->SetNumberField(TEXT("decoratorCount"), Decorators.Num());
	Result->SetNumberField(TEXT("serviceCount"), Services.Num());

	TArray<MCPPagination::FPageRow> Rows;
	if (bAll || KindFilter == TEXT("composite")) Rows.Append(Composites);
	if (bAll || KindFilter == TEXT("task")) Rows.Append(Tasks);
	if (bAll || KindFilter == TEXT("decorator")) Rows.Append(Decorators);
	if (bAll || KindFilter == TEXT("service")) Rows.Append(Services);
	MCPPagination::EmitPage(Page, Rows, TEXT("classes"), Result);

	// This page's rows, regrouped the way this action always reported them.
	TArray<TSharedPtr<FJsonValue>> PageComposites, PageTasks, PageDecorators, PageServices;
	for (const TSharedPtr<FJsonValue>& Row : Result->GetArrayField(TEXT("classes")))
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Row.IsValid() || !Row->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;
		FString Kind;
		(*Obj)->TryGetStringField(TEXT("kind"), Kind);
		if (Kind == TEXT("composite")) PageComposites.Add(Row);
		else if (Kind == TEXT("task")) PageTasks.Add(Row);
		else if (Kind == TEXT("decorator")) PageDecorators.Add(Row);
		else if (Kind == TEXT("service")) PageServices.Add(Row);
	}
	if (bAll || KindFilter == TEXT("composite")) Result->SetArrayField(TEXT("composites"), PageComposites);
	if (bAll || KindFilter == TEXT("task")) Result->SetArrayField(TEXT("tasks"), PageTasks);
	if (bAll || KindFilter == TEXT("decorator")) Result->SetArrayField(TEXT("decorators"), PageDecorators);
	if (bAll || KindFilter == TEXT("service")) Result->SetArrayField(TEXT("services"), PageServices);
	return MCPResult(Result);
}

// #250: rebind a BehaviorTree asset's BlackboardAsset reference. The field is
// `protected` in C++ so direct writes need reflection; Python set_editor_property
// also can't reach it because the UPROPERTY is BlueprintReadOnly.
TSharedPtr<FJsonValue> FGameplayHandlers::SetBehaviorTreeBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString BehaviorTreePath;
	if (auto Err = RequireString(Params, TEXT("behaviorTreePath"), BehaviorTreePath)) return Err;

	FString BlackboardPath;
	if (auto Err = RequireString(Params, TEXT("blackboardPath"), BlackboardPath)) return Err;

	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BehaviorTreePath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *BehaviorTreePath));

	UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!BB) return MCPError(FString::Printf(TEXT("BlackboardData not found: %s"), *BlackboardPath));

	FObjectProperty* BBProp = CastField<FObjectProperty>(BT->GetClass()->FindPropertyByName(TEXT("BlackboardAsset")));
	if (!BBProp)
	{
		return MCPError(TEXT("BehaviorTree class has no BlackboardAsset property - engine version drift?"));
	}

	UBlackboardData* Previous = Cast<UBlackboardData>(BBProp->GetObjectPropertyValue_InContainer(BT));

	BT->Modify();
	BBProp->SetObjectPropertyValue_InContainer(BT, BB);
	BT->PostEditChange();
	SaveAssetPackage(BT);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("behaviorTreePath"), BehaviorTreePath);
	Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
	if (Previous)
	{
		Result->SetStringField(TEXT("previousBlackboard"), Previous->GetPathName());

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("behaviorTreePath"), BehaviorTreePath);
		Payload->SetStringField(TEXT("blackboardPath"), Previous->GetPathName());
		MCPSetRollback(Result, TEXT("set_behavior_tree_blackboard"), Payload);
	}
	return MCPResult(Result);
}

// get_behavior_tree_info and read_behavior_tree_graph moved to
// GameplayHandlers_BehaviorTree.cpp, alongside the node-level BT reads and
// writes they now share their walker and property reflection with.

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

	// Resolve every requested sense BEFORE touching the Blueprint. The previous
	// order added the SCS node first and returned an error mid-loop on a bad
	// sense name, leaving the component behind - and the idempotency check
	// below then made every retry return existed: true, so the action could
	// never configure it. Nothing is mutated until this pass succeeds.
	TArray<UClass*> SenseClasses;
	const TArray<TSharedPtr<FJsonValue>>* SenseArray = nullptr;
	if (Params->TryGetArrayField(TEXT("senses"), SenseArray) && SenseArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *SenseArray)
		{
			FString SenseName;
			if (!Entry.IsValid() || Entry->Type != EJson::String || !Entry->TryGetString(SenseName))
			{
				// Flow YAML steps bypass the TS schema, so a non-string entry
				// reaches here. Skipping it silently returned senses: [] with
				// created: true - a component that perceives nothing.
				return MCPError(TEXT("senses entries must be strings (e.g. \"Sight\"), not objects or numbers."));
			}
			SenseName.TrimStartAndEndInline();
			if (SenseName.IsEmpty())
			{
				return MCPError(TEXT("senses contains an empty entry."));
			}
			// Accept "Sight", "AISenseConfig_Sight", or a full class path.
			UClass* ConfigClass = LoadObject<UClass>(nullptr, *SenseName);
			if (!ConfigClass) ConfigClass = FindClassByShortName(SenseName);
			if (!ConfigClass) ConfigClass = FindClassByShortName(TEXT("AISenseConfig_") + SenseName);
			if (!ConfigClass || !ConfigClass->IsChildOf(UAISenseConfig::StaticClass()))
			{
				return MCPError(FString::Printf(
					TEXT("Unknown sense '%s'. Expected a UAISenseConfig subclass - e.g. Sight, Hearing, Damage, Touch, Team, Prediction."),
					*SenseName));
			}
			SenseClasses.Add(ConfigClass);
		}
	}

	// Idempotency: an existing AIPerceptionComponent is reused rather than
	// duplicated, and requested senses are applied to it - returning
	// existed: true without configuring them made "add a sense" impossible
	// on any Blueprint that already had the component.
	USCS_Node* TargetNode = nullptr;
	bool bCreated = false;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N->ComponentTemplate && N->ComponentTemplate->GetClass() == CompClass)
			{
				TargetNode = N;
				break;
			}
		}
	}
	if (!TargetNode)
	{
		TargetNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("AIPerceptionComp"));
		if (!TargetNode)
		{
			return MCPError(TEXT("Failed to create the AIPerceptionComponent SCS node."));
		}
		BP->SimpleConstructionScript->AddNode(TargetNode);
		bCreated = true;
	}

	UAIPerceptionComponent* Template = Cast<UAIPerceptionComponent>(TargetNode->ComponentTemplate);
	if (!Template)
	{
		return MCPError(TEXT("AIPerceptionComponent template unavailable on the SCS node."));
	}

	// ConfigureSense appends, so re-running with the same sense would stack
	// duplicate configs on the component. Skip the ones already present and
	// report them separately rather than silently doing nothing about them.
	TArray<TSharedPtr<FJsonValue>> ConfiguredSenses;
	TArray<TSharedPtr<FJsonValue>> AlreadyConfigured;
	for (UClass* ConfigClass : SenseClasses)
	{
		bool bPresent = false;
		for (auto It = Template->GetSensesConfigIterator(); It; ++It)
		{
			if (*It && (*It)->GetClass() == ConfigClass) { bPresent = true; break; }
		}
		if (bPresent)
		{
			AlreadyConfigured.Add(MakeShared<FJsonValueString>(ConfigClass->GetName()));
			continue;
		}
		UAISenseConfig* Config = NewObject<UAISenseConfig>(Template, ConfigClass);
		Template->ConfigureSense(*Config);
		ConfiguredSenses.Add(MakeShared<FJsonValueString>(ConfigClass->GetName()));
	}

	if (bCreated || ConfiguredSenses.Num() > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		SaveAssetPackage(BP);
	}

	auto Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TargetNode->GetVariableName().ToString());
	Result->SetArrayField(TEXT("senses"), ConfiguredSenses);
	if (AlreadyConfigured.Num() > 0)
	{
		Result->SetArrayField(TEXT("sensesAlreadyPresent"), AlreadyConfigured);
	}

	// Only offer the remove-component inverse when this call actually created
	// it. Removing a component that already existed is not an undo of adding
	// senses to it - it destroys something the caller did not ask us to touch.
	if (bCreated)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), BPPath);
		Payload->SetStringField(TEXT("componentName"), TargetNode->GetVariableName().ToString());
		MCPSetRollback(Result, TEXT("remove_component"), Payload);
	}

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
	SenseMap.Add(TEXT("Prediction"), TEXT("AISenseConfig_Prediction"));
	SenseMap.Add(TEXT("Blueprint"), TEXT("AISenseConfig_Blueprint"));

	FString* SenseClassName = SenseMap.Find(SenseType);
	if (!SenseClassName)
	{
		return MCPError(FString::Printf(TEXT("Unknown sense type: %s. Available: Sight, Hearing, Damage, Touch, Team, Prediction, Blueprint"), *SenseType));
	}

	UClass* SenseCfgClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), **SenseClassName));
	if (!SenseCfgClass)
	{
		return MCPError(FString::Printf(TEXT("Sense config class not found: %s. Enable AIModule."), **SenseClassName));
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	// Locate the AIPerceptionComponent template on the construction script.
	UClass* PercClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.AIPerceptionComponent"));
	const FString CompName = OptionalString(Params, TEXT("componentName"));
	UObject* PercTemplate = nullptr;
	FString ResolvedComp;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (!N || !N->ComponentTemplate) continue;
			const bool bIsPerc = PercClass
				? N->ComponentTemplate->IsA(PercClass)
				: N->ComponentTemplate->GetClass()->GetName().Contains(TEXT("AIPerception"));
			if (!bIsPerc) continue;
			if (!CompName.IsEmpty() && N->GetVariableName() != FName(*CompName)) continue;
			PercTemplate = N->ComponentTemplate;
			ResolvedComp = N->GetVariableName().ToString();
			break;
		}
	}
	if (!PercTemplate)
	{
		return MCPError(TEXT("No AIPerceptionComponent on the blueprint - run add_perception_component first"));
	}

	// Add the sense config to SensesConfig via reflection (avoids linking AIModule
	// headers and matches what the editor's '+' button does).
	FArrayProperty* SensesProp = CastField<FArrayProperty>(PercTemplate->GetClass()->FindPropertyByName(TEXT("SensesConfig")));
	FObjectPropertyBase* ElemProp = SensesProp ? CastField<FObjectPropertyBase>(SensesProp->Inner) : nullptr;
	if (!SensesProp || !ElemProp)
	{
		return MCPError(TEXT("SensesConfig object-array property not found on AIPerceptionComponent (engine drift)"));
	}

	FScriptArrayHelper Helper(SensesProp, SensesProp->ContainerPtrToValuePtr<void>(PercTemplate));

	// Find the sense config this call is about. An existing one and a freshly
	// constructed one are then driven through the SAME settings pass below.
	//
	// The existing-config branch used to return existed: true right here, before
	// the settings pass ran at all, so the ordinary flow - add a sense, then tune
	// it - wrote nothing and reported success. Idempotency means the call says
	// what it did, not that the second call does nothing.
	UObject* Cfg = nullptr;
	int32 ConfigIndex = INDEX_NONE;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		UObject* Existing = ElemProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
		if (Existing && Existing->GetClass() == SenseCfgClass)
		{
			Cfg = Existing;
			ConfigIndex = i;
			break;
		}
	}
	const bool bExisted = (Cfg != nullptr);

	// Validate every setting BEFORE anything is constructed, appended or written.
	//
	// This loop used to run after the config was in the array and did
	// `if (!P) continue;`, so a misspelled key returned success having done
	// nothing, and a key whose value would not convert vanished the same way.
	// Both are the worst failure this bridge has: an ordinary success for a
	// change that never happened.
	//
	// Validating first also matters for the failure path. Applying mid-loop
	// left a half-configured config appended to the array with the Blueprint
	// uncompiled, which AddPerceptionComponent already avoids for its own
	// `senses` array for exactly this reason. The update path below needs the
	// same guarantee, and gets it from the same check plus a per-key snapshot.
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	const bool bHasSettings =
		Params->TryGetObjectField(TEXT("settings"), PropsObj) && PropsObj && (*PropsObj).IsValid();

	if (bHasSettings)
	{
		TArray<FString> UnknownKeys;
		for (const auto& KV : (*PropsObj)->Values)
		{
			if (!SenseCfgClass->FindPropertyByName(FName(*KV.Key))) UnknownKeys.Add(FString(*KV.Key));
		}
		if (UnknownKeys.Num() > 0)
		{
			// Name what is valid, so one error is enough to fix the call.
			TArray<FString> Valid;
			for (TFieldIterator<FProperty> It(SenseCfgClass); It; ++It)
			{
				Valid.Add(It->GetName());
			}
			Valid.Sort();
			return MCPError(FString::Printf(
				TEXT("%s has no propert%s named %s. Valid properties on %s: %s. Nothing was changed."),
				*SenseCfgClass->GetName(),
				UnknownKeys.Num() == 1 ? TEXT("y") : TEXT("ies"),
				*FString::Join(UnknownKeys, TEXT(", ")),
				*SenseCfgClass->GetName(),
				*FString::Join(Valid, TEXT(", "))));
		}
	}

	if (!bExisted)
	{
		Cfg = NewObject<UObject>(PercTemplate, SenseCfgClass, NAME_None, RF_Transactional);
		ConfigIndex = Helper.AddValue();
		ElemProp->SetObjectPropertyValue(Helper.GetRawPtr(ConfigIndex), Cfg);
	}

	// Optional per-sense tuning (e.g. { "SightRadius": 1500 }).
	//
	// Each write is snapshotted first, so a value that will not convert halfway
	// through leaves the component exactly as it was found rather than half
	// applied: the create path drops the whole appended element, the update path
	// puts back the values it had already written.
	struct FSenseSettingWrite
	{
		FProperty* Property = nullptr;
		FString PreviousText;
	};
	TArray<FSenseSettingWrite> Written;
	TArray<FString> AppliedProps;
	TArray<FString> ChangedProps;
	TArray<FString> AlreadyAtValueProps;

	auto UndoWrites = [&]()
	{
		if (!bExisted)
		{
			Helper.RemoveValues(ConfigIndex, 1);
			return;
		}
		for (int32 i = Written.Num() - 1; i >= 0; --i)
		{
			FProperty* P = Written[i].Property;
			P->ImportText_Direct(*Written[i].PreviousText, P->ContainerPtrToValuePtr<void>(Cfg), Cfg, PPF_None);
		}
	};

	if (bHasSettings)
	{
		for (const auto& KV : (*PropsObj)->Values)
		{
			FProperty* P = Cfg->GetClass()->FindPropertyByName(FName(*KV.Key));
			// Existence was proved above, so a miss here is impossible; the
			// guard stays because a null deref would be worse than a message.
			if (!P)
			{
				UndoWrites();
				return MCPError(FString::Printf(
					TEXT("Property '%s' vanished between validation and apply on %s."),
					*FString(*KV.Key), *SenseCfgClass->GetName()));
			}

			void* ValueAddr = P->ContainerPtrToValuePtr<void>(Cfg);
			FString BeforeText;
			P->ExportText_Direct(BeforeText, ValueAddr, ValueAddr, Cfg, PPF_None);

			FString PErr;
			if (!MCPJsonProperty::SetJsonOnProperty(P, ValueAddr, KV.Value, PErr))
			{
				// A value that will not convert is a failed call, not a skipped key.
				UndoWrites();
				return MCPError(FString::Printf(
					TEXT("Could not set '%s' on %s: %s. %s"),
					*FString(*KV.Key), *SenseCfgClass->GetName(), *PErr,
					bExisted
						? TEXT("The sense was left exactly as it was found; no setting was applied.")
						: TEXT("The sense was not added.")));
			}
			Written.Add(FSenseSettingWrite{ P, BeforeText });
			AppliedProps.Add(FString(*KV.Key));

			FString AfterText;
			P->ExportText_Direct(AfterText, ValueAddr, ValueAddr, Cfg, PPF_None);
			if (AfterText == BeforeText) AlreadyAtValueProps.Add(FString(*KV.Key));
			else ChangedProps.Add(FString(*KV.Key));
		}
	}

	// Compile and save only when something actually moved. A call that found the
	// sense already configured exactly as asked has no reason to dirty the asset.
	const bool bChanged = (!bExisted || ChangedProps.Num() > 0);
	if (bChanged)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		SaveAssetPackage(BP);
	}

	// Three outcomes, reported apart: created, existed and updated, existed and
	// unchanged. Collapsing the last two is what made the old result dishonest.
	auto Result = MCPSuccess();
	if (bExisted) MCPSetExisted(Result); else MCPSetCreated(Result);
	if (bExisted && ChangedProps.Num() > 0) MCPSetUpdated(Result);
	else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("changed"), bChanged);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), ResolvedComp);
	Result->SetStringField(TEXT("senseType"), SenseType);
	Result->SetStringField(TEXT("senseConfig"), Cfg->GetClass()->GetName());
	Result->SetNumberField(TEXT("index"), ConfigIndex);

	auto ToJsonArray = [](const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	};
	if (AppliedProps.Num() > 0)
	{
		Result->SetArrayField(TEXT("appliedProperties"), ToJsonArray(AppliedProps));
		Result->SetArrayField(TEXT("changedProperties"), ToJsonArray(ChangedProps));
		Result->SetArrayField(TEXT("unchangedProperties"), ToJsonArray(AlreadyAtValueProps));
	}

	if (!bExisted)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%s was added to '%s' and %d setting(s) were written."),
			*SenseCfgClass->GetName(), *ResolvedComp, AppliedProps.Num()));
	}
	else if (ChangedProps.Num() > 0)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%s was already on '%s'; %d of %d requested setting(s) changed its value."),
			*SenseCfgClass->GetName(), *ResolvedComp, ChangedProps.Num(), AppliedProps.Num()));
	}
	else if (bHasSettings)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%s was already on '%s' and every requested setting already held that value, ")
			TEXT("so the asset was not touched. gameplay(read_perception) reports the current values."),
			*SenseCfgClass->GetName(), *ResolvedComp));
	}
	else
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%s was already on '%s' and no settings were supplied, so nothing was written. ")
			TEXT("Pass 'settings' to tune it."),
			*SenseCfgClass->GetName(), *ResolvedComp));
	}

	// The inverse of adding a sense is removing it, and remove_sense is a real
	// action now. It is only the inverse when THIS call added the config: undoing
	// a tuning pass by destroying a config the caller already had is not an undo.
	if (!bExisted)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("blueprintPath"), BPPath);
		Payload->SetStringField(TEXT("senseType"), SenseType);
		Payload->SetStringField(TEXT("componentName"), ResolvedComp);
		MCPSetRollback(Result, TEXT("remove_sense"), Payload);
	}
	else if (ChangedProps.Num() > 0)
	{
		Result->SetBoolField(TEXT("rollbackUnavailable"), true);
		Result->SetStringField(TEXT("rollbackUnavailableReason"), TEXT(
			"This call tuned a sense config that already existed. Removing it would destroy a config "
			"the caller did not create, and the previous values are not carried here. Read them with "
			"gameplay(read_perception) before tuning, and write them back with configure_sense."));
	}

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

// ─────────────────────────────────────────────────────────────
// #163  get_navmesh_details - Detailed ARecastNavMesh configuration
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

	// Fallback: grab the first ARecastNavMesh in the world
	if (!RecastNav)
	{
		TActorIterator<ARecastNavMesh> It(World);
		if (It)
		{
			RecastNav = *It;
		}
	}

	if (!RecastNav)
	{
		return MCPError(TEXT("No ARecastNavMesh found. Add a NavMeshBoundsVolume and build navigation."));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), RecastNav->GetName());
	Result->SetStringField(TEXT("class"), RecastNav->GetClass()->GetName());

	// Cell / voxelization. These are per-resolution since 5.3 - the flat
	// CellSize/CellHeight/AgentMaxStepHeight properties are deprecated and warn
	// on every user build. The top-level fields keep reporting the Default
	// resolution (what the old properties fed), and the full set is reported
	// alongside so callers can see Low and High too.
	Result->SetNumberField(TEXT("cellSize"), RecastNav->GetCellSize(ENavigationDataResolution::Default));
	Result->SetNumberField(TEXT("cellHeight"), RecastNav->GetCellHeight(ENavigationDataResolution::Default));

	TArray<TSharedPtr<FJsonValue>> Resolutions;
	const TCHAR* ResolutionNames[] = { TEXT("low"), TEXT("default"), TEXT("high") };
	for (int32 ResIndex = 0; ResIndex < UE_ARRAY_COUNT(ResolutionNames); ++ResIndex)
	{
		const ENavigationDataResolution Resolution = static_cast<ENavigationDataResolution>(ResIndex);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("resolution"), ResolutionNames[ResIndex]);
		Entry->SetNumberField(TEXT("cellSize"), RecastNav->GetCellSize(Resolution));
		Entry->SetNumberField(TEXT("cellHeight"), RecastNav->GetCellHeight(Resolution));
		Entry->SetNumberField(TEXT("agentMaxStepHeight"), RecastNav->GetAgentMaxStepHeight(Resolution));
		Resolutions.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("resolutions"), Resolutions);

	// Agent
	Result->SetNumberField(TEXT("agentRadius"), RecastNav->AgentRadius);
	Result->SetNumberField(TEXT("agentHeight"), RecastNav->AgentHeight);
	Result->SetNumberField(TEXT("agentMaxSlope"), RecastNav->AgentMaxSlope);
	Result->SetNumberField(TEXT("agentMaxStepHeight"), RecastNav->GetAgentMaxStepHeight(ENavigationDataResolution::Default));

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
