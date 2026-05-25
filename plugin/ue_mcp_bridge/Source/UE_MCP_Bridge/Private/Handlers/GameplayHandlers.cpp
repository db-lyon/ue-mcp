#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerAssetCreate.h"
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
	Registry.RegisterHandler(TEXT("spawn_nav_modifier_volume"), &SpawnNavModifierVolume);
	Registry.RegisterHandler(TEXT("set_world_game_mode"), &SetWorldGameMode);
	Registry.RegisterHandler(TEXT("add_blackboard_key"), &AddBlackboardKey);
	Registry.RegisterHandler(TEXT("set_behavior_tree_blackboard"), &SetBehaviorTreeBlackboard);
	Registry.RegisterHandler(TEXT("rebuild_navigation"), &RebuildNavmesh);
	Registry.RegisterHandler(TEXT("find_nav_path"), &FindNavPath);
	Registry.RegisterHandler(TEXT("list_nav_invokers"), &ListNavInvokers);
	// New handlers
	Registry.RegisterHandler(TEXT("get_behavior_tree_info"), &GetBehaviorTreeInfo);
	Registry.RegisterHandler(TEXT("read_behavior_tree_graph"), &ReadBehaviorTreeGraph);
	Registry.RegisterHandler(TEXT("add_perception_component"), &AddPerceptionComponent);
	Registry.RegisterHandler(TEXT("configure_ai_perception_sense"), &ConfigureAiPerceptionSense);
	Registry.RegisterHandler(TEXT("add_state_tree_component"), &AddStateTreeComponent);
	Registry.RegisterHandler(TEXT("add_smart_object_component"), &AddSmartObjectComponent);
	Registry.RegisterHandler(TEXT("add_smart_object_slot"), &AddSmartObjectSlot);
	Registry.RegisterHandler(TEXT("set_smart_object_slot"), &SetSmartObjectSlot);
	Registry.RegisterHandler(TEXT("remove_smart_object_slot"), &RemoveSmartObjectSlot);
	Registry.RegisterHandler(TEXT("list_smart_object_slots"), &ListSmartObjectSlots);
	Registry.RegisterHandler(TEXT("add_smart_object_slot_behavior"), &AddSmartObjectSlotBehavior);
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
	Registry.RegisterHandler(TEXT("inject_input"), &InjectInput);
	Registry.RegisterHandler(TEXT("inject_input_start"), &InjectInputStart);
	Registry.RegisterHandler(TEXT("inject_input_update"), &InjectInputUpdate);
	Registry.RegisterHandler(TEXT("inject_input_stop"), &InjectInputStop);
	Registry.RegisterHandler(TEXT("inject_input_tape"), &InjectInputTape);
	Registry.RegisterHandler(TEXT("pie_record_arm"), &PieRecordArm);
	Registry.RegisterHandler(TEXT("pie_record_disarm"), &PieRecordDisarm);
	Registry.RegisterHandler(TEXT("pie_record_stop"), &PieRecordStop);
	Registry.RegisterHandler(TEXT("pie_record_status"), &PieRecordStatus);
	Registry.RegisterHandler(TEXT("pie_record_list"), &PieRecordList);
	Registry.RegisterHandler(TEXT("pie_record_read"), &PieRecordRead);
	Registry.RegisterHandler(TEXT("pie_record_delete"), &PieRecordDelete);
	Registry.RegisterHandler(TEXT("pie_mark"), &PieMark);
	Registry.RegisterHandler(TEXT("pie_replay_arm"), &PieReplayArm);
	Registry.RegisterHandler(TEXT("pie_replay_disarm"), &PieReplayDisarm);
	Registry.RegisterHandler(TEXT("pie_replay_stop"), &PieReplayStop);
	Registry.RegisterHandler(TEXT("pie_replay_status"), &PieReplayStatus);
	Registry.RegisterHandler(TEXT("pie_record_diff"), &PieRecordDiff);
	Registry.RegisterHandler(TEXT("pie_snapshot"), &PieSnapshot);
	Registry.RegisterHandler(TEXT("pie_profile_create"), &PieProfileCreate);
	Registry.RegisterHandler(TEXT("pie_profile_read"), &PieProfileRead);
	Registry.RegisterHandler(TEXT("pie_profile_update"), &PieProfileUpdate);
	Registry.RegisterHandler(TEXT("pie_profile_delete"), &PieProfileDelete);
	Registry.RegisterHandler(TEXT("pie_profile_list"), &PieProfileList);
	Registry.RegisterHandler(TEXT("pie_observe_arm"), &PieObserveArm);
	Registry.RegisterHandler(TEXT("pie_observe_disarm"), &PieObserveDisarm);
	Registry.RegisterHandler(TEXT("pie_observe_stop"), &PieObserveStop);
	Registry.RegisterHandler(TEXT("pie_observe_status"), &PieObserveStatus);
	Registry.RegisterHandler(TEXT("pie_observe_list"), &PieObserveList);
	Registry.RegisterHandler(TEXT("pie_observe_read"), &PieObserveRead);
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

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
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
	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), NewIdx);
	Result->SetNumberField(TEXT("slotCount"), Helper.Num());

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
	const FString ApplyErr = ApplySlotFieldsFromJson(SA.SlotStruct, SlotAddr, Params);
	if (!ApplyErr.IsEmpty()) return MCPError(ApplyErr);
	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), SlotIdx);
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
	SA.Asset->Modify();
	Helper.RemoveValues(SlotIdx, 1);
	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), SlotIdx);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetNumberField(TEXT("slotCount"), Helper.Num());
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

	FProperty* BDProp = SA.SlotStruct->Struct->FindPropertyByName(FName(TEXT("BehaviorDefinitions")));
	if (!BDProp) return MCPError(TEXT("Slot struct has no 'BehaviorDefinitions' property"));
	FArrayProperty* BDArr = CastField<FArrayProperty>(BDProp);
	if (!BDArr) return MCPError(TEXT("'BehaviorDefinitions' is not a TArray"));
	FObjectProperty* BDObj = CastField<FObjectProperty>(BDArr->Inner);
	if (!BDObj) return MCPError(TEXT("'BehaviorDefinitions' inner is not a UObject*"));

	// Resolve the behavior class. Caller may pass either a class path or an
	// existing UBehaviorDefinition asset; the array holds object pointers.
	UObject* BehaviorAsset = LoadObject<UObject>(nullptr, *BehaviorClassPath);
	if (!BehaviorAsset)
	{
		// Try as a class path
		UClass* BehaviorClass = LoadClass<UObject>(nullptr, *BehaviorClassPath);
		if (!BehaviorClass) return MCPError(FString::Printf(TEXT("Could not load behavior asset/class '%s'"), *BehaviorClassPath));
		BehaviorAsset = NewObject<UObject>(SA.Asset, BehaviorClass);
	}

	SA.Asset->Modify();
	FScriptArrayHelper BDHelper(BDArr, BDProp->ContainerPtrToValuePtr<void>(SlotAddr));
	const int32 NewBDIdx = BDHelper.AddValue();
	BDObj->SetObjectPropertyValue(BDHelper.GetRawPtr(NewBDIdx), BehaviorAsset);

	// Optional instance properties: dictionary of name -> JSON value applied
	// to the new behavior asset.
	const TSharedPtr<FJsonObject>* InstObj = nullptr;
	if (Params->TryGetObjectField(TEXT("instanceProperties"), InstObj) && InstObj && (*InstObj).IsValid())
	{
		for (const auto& Pair : (*InstObj)->Values)
		{
			FProperty* P = BehaviorAsset->GetClass()->FindPropertyByName(FName(*Pair.Key));
			if (!P) continue;
			FString E;
			MCPJsonProperty::SetJsonOnProperty(P, P->ContainerPtrToValuePtr<void>(BehaviorAsset), Pair.Value, E);
		}
	}

	SA.Asset->PostEditChange();
	SA.Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SA.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SA.Asset->GetPathName());
	Result->SetNumberField(TEXT("slotIndex"), SlotIdx);
	Result->SetNumberField(TEXT("behaviorIndex"), NewBDIdx);
	Result->SetStringField(TEXT("behavior"), BehaviorAsset->GetClass()->GetPathName());
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

	UClass* EQSClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvironmentQuery"));
	if (!EQSClass)
	{
		return MCPError(TEXT("EnvironmentQuery class not found."));
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

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
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
	if (!ContextLabel.IsEmpty()) Context = FindActorByLabel(World, ContextLabel);

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