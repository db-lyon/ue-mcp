#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Animation/AnimComposite.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMeshSocket.h"
// PhysicsEngine/SkeletalBodySetup.h is unavailable as a public include on
// UE 5.4. USkeletalBodySetup is still defined transitively via PhysicsAsset.h.
#if __has_include("PhysicsEngine/SkeletalBodySetup.h")
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Editor.h"

// State machine authoring
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// IK Rig (#93) — use subdirectory path for UE 5.7
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"

// Control Rig (#11) — ControlRigBlueprint removed in UE 5.7, use reflection
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"

// Curve identifiers for UE5 animation data controller
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"

void FAnimationHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_anim_assets"), &ListAnimAssets);
	Registry.RegisterHandler(TEXT("list_skeletal_meshes"), &ListSkeletalMeshes);
	Registry.RegisterHandler(TEXT("get_skeleton_info"), &GetSkeletonInfo);
	Registry.RegisterHandler(TEXT("list_sockets"), &ListSockets);
	Registry.RegisterHandler(TEXT("get_physics_asset_info"), &GetPhysicsAssetInfo);
	Registry.RegisterHandler(TEXT("read_anim_blueprint"), &ReadAnimBlueprint);
	Registry.RegisterHandler(TEXT("read_anim_montage"), &ReadAnimMontage);
	Registry.RegisterHandler(TEXT("read_anim_sequence"), &ReadAnimSequence);
	Registry.RegisterHandler(TEXT("scan_animation_tracks"), &ScanAnimationTracks);
	Registry.RegisterHandler(TEXT("create_anim_blueprint"), &CreateAnimBlueprint);
	Registry.RegisterHandler(TEXT("create_anim_montage"), &CreateMontage);
	Registry.RegisterHandler(TEXT("create_blendspace"), &CreateBlendspace);
	Registry.RegisterHandler(TEXT("create_blendspace_1d"), &CreateBlendspace1D);
	Registry.RegisterHandler(TEXT("add_blend_sample"), &AddBlendSample);
	Registry.RegisterHandler(TEXT("set_blend_sample"), &SetBlendSample);
	Registry.RegisterHandler(TEXT("read_blendspace"), &ReadBlendspace);
	// #459: configure axis params + bulk-add samples in one call.
	Registry.RegisterHandler(TEXT("populate_blendspace"), &PopulateBlendspace);
	Registry.RegisterHandler(TEXT("populate_blendspace_1d"), &PopulateBlendspace);
	Registry.RegisterHandler(TEXT("add_anim_notify"), &AddAnimNotify);
	Registry.RegisterHandler(TEXT("remove_anim_notify"), &RemoveAnimNotify);
	Registry.RegisterHandler(TEXT("remove_animation_notify"), &RemoveAnimNotify);
	Registry.RegisterHandler(TEXT("create_sequence"), &CreateSequence);
	Registry.RegisterHandler(TEXT("set_bone_keyframes"), &SetBoneKeyframes);
	Registry.RegisterHandler(TEXT("bake_keyframes_batch"), &BakeKeyframesBatch);
	Registry.RegisterHandler(TEXT("get_bone_transforms"), &GetBoneTransforms);
	// #656: curve vs morph-target comparison.
	Registry.RegisterHandler(TEXT("compare_curves_to_morph_targets"), &CompareCurvesToMorphTargets);
	Registry.RegisterHandler(TEXT("set_montage_sequence"), &SetMontageSequence);
	Registry.RegisterHandler(TEXT("set_montage_properties"), &SetMontageProperties);

	// State machine authoring
	Registry.RegisterHandler(TEXT("create_state_machine"), &CreateStateMachine);
	Registry.RegisterHandler(TEXT("add_state"), &AddState);
	Registry.RegisterHandler(TEXT("add_transition"), &AddTransition);
	Registry.RegisterHandler(TEXT("set_state_animation"), &SetStateAnimation);
	Registry.RegisterHandler(TEXT("set_transition_blend"), &SetTransitionBlend);
	Registry.RegisterHandler(TEXT("set_transition_condition"), &SetTransitionCondition);
	Registry.RegisterHandler(TEXT("read_state_machine"), &ReadStateMachine);

	// AnimGraph inspection (#23 / #91)
	Registry.RegisterHandler(TEXT("read_anim_graph"), &ReadAnimGraph);
	// #657: deep anim-node struct inspection (PoseDriver, RBF, etc.).
	Registry.RegisterHandler(TEXT("inspect_anim_nodes"), &InspectAnimNodes);

	// Float curve authoring (#79 / #24)
	Registry.RegisterHandler(TEXT("add_curve"), &AddCurve);
	Registry.RegisterHandler(TEXT("set_anim_curve_keys"), &SetAnimCurveKeys);
	Registry.RegisterHandler(TEXT("apply_animation_modifier"), &ApplyAnimationModifier);

	// Montage slot & section editing (#78, #27)
	Registry.RegisterHandler(TEXT("set_montage_slot"), &SetMontageSlot);
	Registry.RegisterHandler(TEXT("add_montage_section"), &AddMontageSection);

	// IK Rig (#93)
	Registry.RegisterHandler(TEXT("create_ik_rig"), &CreateIKRig);
	Registry.RegisterHandler(TEXT("read_ik_rig"), &ReadIKRig);
	// #701/#703: IK authoring tail + batch retarget.
	Registry.RegisterHandler(TEXT("set_ik_rig_mesh"), &SetIKRigMesh);
	Registry.RegisterHandler(TEXT("set_ik_retargeter_rig"), &SetIKRetargeterRig);
	Registry.RegisterHandler(TEXT("auto_align_retarget_pose"), &AutoAlignRetargetPose);
	Registry.RegisterHandler(TEXT("reset_retarget_pose"), &ResetRetargetPose);
	Registry.RegisterHandlerWithTimeout(TEXT("batch_retarget_animations"), &BatchRetargetAnimations, 300.0f);

	// Control Rig (#11)
	Registry.RegisterHandler(TEXT("list_control_rig_variables"), &ListControlRigVariables);
	Registry.RegisterHandler(TEXT("read_control_rig_hierarchy"), &ReadControlRigHierarchy);

	// v0.7.11 — depth
	Registry.RegisterHandler(TEXT("set_root_motion_settings"), &SetRootMotionSettings);
	Registry.RegisterHandler(TEXT("add_virtual_bone"), &AddVirtualBone);
	Registry.RegisterHandler(TEXT("remove_virtual_bone"), &RemoveVirtualBone);
	Registry.RegisterHandler(TEXT("create_anim_composite"), &CreateAnimComposite);
	Registry.RegisterHandler(TEXT("list_anim_modifiers"), &ListAnimModifiers);

	// v0.7.11 — issue fixes
	Registry.RegisterHandler(TEXT("create_ik_retargeter"), &CreateIKRetargeter);
	Registry.RegisterHandler(TEXT("read_ik_retargeter"), &ReadIKRetargeter);
	Registry.RegisterHandler(TEXT("set_anim_blueprint_skeleton"), &SetAnimBlueprintSkeleton);
	Registry.RegisterHandler(TEXT("read_bone_track"), &ReadBoneTrack);

	// v1.0.0-rc.2 — animation authoring gaps (#153, #154)
	Registry.RegisterHandler(TEXT("set_sequence_properties"), &SetSequenceProperties);
	Registry.RegisterHandler(TEXT("bake_root_motion_from_bone"), &BakeRootMotionFromBone);

	// v0.7.15 — PoseSearch (motion matching)
	Registry.RegisterHandler(TEXT("create_pose_search_database"), &CreatePoseSearchDatabase);
	Registry.RegisterHandler(TEXT("set_pose_search_schema"), &SetPoseSearchSchema);
	Registry.RegisterHandler(TEXT("add_pose_search_sequence"), &AddPoseSearchSequence);
	Registry.RegisterHandler(TEXT("set_pose_search_clips"), &SetPoseSearchClips);
	Registry.RegisterHandler(TEXT("build_pose_search_index"), &BuildPoseSearchIndex);
	Registry.RegisterHandler(TEXT("read_pose_search_database"), &ReadPoseSearchDatabase);

	// Motion Matching content pipeline (schema / mirror / normalization / tuning)
	Registry.RegisterHandler(TEXT("create_pose_search_schema"), &CreatePoseSearchSchema);
	Registry.RegisterHandler(TEXT("add_pose_search_schema_pose_channel"), &AddPoseSearchSchemaPoseChannel);
	Registry.RegisterHandler(TEXT("add_pose_search_schema_trajectory_channel"), &AddPoseSearchSchemaTrajectoryChannel);
	Registry.RegisterHandler(TEXT("read_pose_search_schema"), &ReadPoseSearchSchema);
	Registry.RegisterHandler(TEXT("create_mirror_data_table"), &CreateMirrorDataTable);
	Registry.RegisterHandler(TEXT("read_mirror_data_table"), &ReadMirrorDataTable);
	Registry.RegisterHandler(TEXT("create_pose_search_normalization_set"), &CreatePoseSearchNormalizationSet);
	Registry.RegisterHandler(TEXT("set_pose_search_database_settings"), &SetPoseSearchDatabaseSettings);
	Registry.RegisterHandler(TEXT("add_motion_matching_node"), &AddMotionMatchingNode);
	Registry.RegisterHandler(TEXT("add_pose_history_node"), &AddPoseHistoryNode);
	Registry.RegisterHandler(TEXT("set_motion_matching_chooser"), &SetMotionMatchingChooser);

	// #713 — distance-matching graph authoring
	Registry.RegisterHandler(TEXT("add_sequence_evaluator"), &AddSequenceEvaluator);
	Registry.RegisterHandler(TEXT("bind_anim_node_function"), &BindAnimNodeFunction);

	// #419/#420 — live-actor skeletal reads + rebind + preview (moved from Level)
	Registry.RegisterHandler(TEXT("get_bone_transform"), &GetBoneTransform);
	Registry.RegisterHandler(TEXT("list_bones"), &ListBones);
	Registry.RegisterHandler(TEXT("rebind_leader_pose"), &RebindLeaderPose);
	Registry.RegisterHandler(TEXT("preview_animation"), &PreviewAnimation);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListAnimAssets(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Asset class names to search for
	TArray<FTopLevelAssetPath> ClassPaths;
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimSequence")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimMontage")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("BlendSpace")));

	TArray<TSharedPtr<FJsonValue>> AssetsArray;

	for (const FTopLevelAssetPath& ClassPath : ClassPaths)
	{
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssetsByClass(ClassPath, AssetDataList, bRecursive);

		for (const FAssetData& AssetData : AssetDataList)
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
			AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
			AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
			AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		}
	}

	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListSkeletalMeshes(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SkeletalMesh")), AssetDataList, bRecursive);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *AssetPath));
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		return MCPError(TEXT("SkeletalMesh has no Skeleton"));
	}

	auto Result = MCPSuccess();

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TArray<TSharedPtr<FJsonValue>> BonesArray;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(i).ToString());
		BoneObj->SetNumberField(TEXT("index"), i);
		BoneObj->SetNumberField(TEXT("parentIndex"), RefSkeleton.GetParentIndex(i));
		BonesArray.Add(MakeShared<FJsonValueObject>(BoneObj));
	}

	Result->SetStringField(TEXT("skeletonName"), Skeleton->GetName());
	Result->SetArrayField(TEXT("bones"), BonesArray);
	Result->SetNumberField(TEXT("boneCount"), BonesArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListSockets(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *AssetPath));
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		return MCPError(TEXT("SkeletalMesh has no Skeleton"));
	}

	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> SocketsArray;
	auto AppendSocket = [&SocketsArray](const USkeletalMeshSocket* Socket, const TCHAR* Source)
	{
		if (!Socket) return;

		TSharedPtr<FJsonObject> SocketObj = MakeShared<FJsonObject>();
		SocketObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		SocketObj->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
		SocketObj->SetStringField(TEXT("source"), Source);

		TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
		LocationObj->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
		LocationObj->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
		LocationObj->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
		SocketObj->SetObjectField(TEXT("relativeLocation"), LocationObj);

		TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
		RotationObj->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
		RotationObj->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
		RotationObj->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
		SocketObj->SetObjectField(TEXT("relativeRotation"), RotationObj);

		TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
		ScaleObj->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
		ScaleObj->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
		ScaleObj->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
		SocketObj->SetObjectField(TEXT("relativeScale"), ScaleObj);

		SocketsArray.Add(MakeShared<FJsonValueObject>(SocketObj));
	};

	for (const USkeletalMeshSocket* Socket : SkeletalMesh->GetMeshOnlySocketList())
	{
		AppendSocket(Socket, TEXT("mesh"));
	}

	for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
	{
		AppendSocket(Socket, TEXT("skeleton"));
	}

	Result->SetArrayField(TEXT("sockets"), SocketsArray);
	Result->SetNumberField(TEXT("count"), SocketsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::GetPhysicsAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *AssetPath));
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return MCPError(TEXT("SkeletalMesh has no PhysicsAsset"));
	}

	auto Result = MCPSuccess();

	Result->SetStringField(TEXT("physicsAssetName"), PhysicsAsset->GetName());
	Result->SetStringField(TEXT("physicsAssetPath"), PhysicsAsset->GetPathName());
	Result->SetNumberField(TEXT("bodyCount"), PhysicsAsset->SkeletalBodySetups.Num());

	TArray<TSharedPtr<FJsonValue>> BodiesArray;
	for (const TObjectPtr<USkeletalBodySetup>& BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup) continue;

		TSharedPtr<FJsonObject> BodyObj = MakeShared<FJsonObject>();
		BodyObj->SetStringField(TEXT("boneName"), BodySetup->BoneName.ToString());
		BodiesArray.Add(MakeShared<FJsonValueObject>(BodyObj));
	}

	Result->SetArrayField(TEXT("bodies"), BodiesArray);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// read_anim_blueprint
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(LoadedAsset);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimBlueprint at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), AnimBP->GetName());
	Result->SetStringField(TEXT("class"), AnimBP->GetClass()->GetName());

	// Target skeleton
	USkeleton* TargetSkeleton = AnimBP->TargetSkeleton.Get();
	if (TargetSkeleton)
	{
		Result->SetStringField(TEXT("targetSkeleton"), TargetSkeleton->GetPathName());
	}
	else
	{
		Result->SetField(TEXT("targetSkeleton"), MakeShared<FJsonValueNull>());
	}

	// Parent class
	UClass* ParentClass = AnimBP->ParentClass;
	if (ParentClass)
	{
		Result->SetStringField(TEXT("parentClass"), ParentClass->GetName());
	}
	else
	{
		Result->SetField(TEXT("parentClass"), MakeShared<FJsonValueNull>());
	}

	// Groups
	TArray<TSharedPtr<FJsonValue>> GroupsArray;
	for (const FAnimGroupInfo& Group : AnimBP->Groups)
	{
		GroupsArray.Add(MakeShared<FJsonValueString>(Group.Name.ToString()));
	}
	Result->SetArrayField(TEXT("groups"), GroupsArray);

	// Variables from the generated class
	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	UAnimBlueprintGeneratedClass* GenClass = Cast<UAnimBlueprintGeneratedClass>(AnimBP->GeneratedClass);
	if (GenClass)
	{
		for (TFieldIterator<FProperty> PropIt(GenClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) continue;

			TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
			VarObj->SetStringField(TEXT("name"), Prop->GetName());
			VarObj->SetStringField(TEXT("type"), Prop->GetCPPType());
			VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
		}
	}
	Result->SetArrayField(TEXT("variables"), VariablesArray);

	// State machine names from the anim graph
	TArray<TSharedPtr<FJsonValue>> StateMachinesArray;
	if (GenClass)
	{
		for (const FBakedAnimationStateMachine& SM : GenClass->BakedStateMachines)
		{
			StateMachinesArray.Add(MakeShared<FJsonValueString>(SM.MachineName.ToString()));
		}
	}
	Result->SetArrayField(TEXT("stateMachines"), StateMachinesArray);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// read_anim_montage
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimMontage(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Montage->GetName());
	Result->SetStringField(TEXT("class"), Montage->GetClass()->GetName());

	// Blend in / blend out times
	Result->SetNumberField(TEXT("blendIn"), Montage->BlendIn.GetBlendTime());
	Result->SetNumberField(TEXT("blendOut"), Montage->BlendOut.GetBlendTime());

	// Sequence length and rate scale
	Result->SetNumberField(TEXT("sequenceLength"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("rateScale"), Montage->RateScale);

	// Composite sections
	TArray<TSharedPtr<FJsonValue>> SectionsArray;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();
		SecObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SecObj->SetNumberField(TEXT("startTime"), Section.GetTime());
		SecObj->SetStringField(TEXT("nextSection"), Section.NextSectionName.ToString());
		SectionsArray.Add(MakeShared<FJsonValueObject>(SecObj));
	}
	Result->SetArrayField(TEXT("sections"), SectionsArray);

	// Notifies
	TArray<TSharedPtr<FJsonValue>> NotifiesArray;
	for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObj = MakeShared<FJsonObject>();
		NotifyObj->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());
		NotifyObj->SetNumberField(TEXT("triggerTime"), NotifyEvent.GetTriggerTime());
		NotifyObj->SetNumberField(TEXT("duration"), NotifyEvent.GetDuration());
		if (NotifyEvent.Notify)
		{
			NotifyObj->SetStringField(TEXT("class"), NotifyEvent.Notify->GetClass()->GetName());
		}
		NotifiesArray.Add(MakeShared<FJsonValueObject>(NotifyObj));
	}
	Result->SetArrayField(TEXT("notifies"), NotifiesArray);

	// Slot anim tracks
	TArray<TSharedPtr<FJsonValue>> SlotTracksArray;
	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
		TrackObj->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());

		TArray<TSharedPtr<FJsonValue>> SegmentsArray;
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
			if (Segment.GetAnimReference())
			{
				SegObj->SetStringField(TEXT("animation"), Segment.GetAnimReference()->GetPathName());
			}
			else
			{
				SegObj->SetField(TEXT("animation"), MakeShared<FJsonValueNull>());
			}
			SegObj->SetNumberField(TEXT("startPos"), Segment.AnimStartTime);
			SegObj->SetNumberField(TEXT("endPos"), Segment.AnimEndTime);
			SegmentsArray.Add(MakeShared<FJsonValueObject>(SegObj));
		}
		TrackObj->SetArrayField(TEXT("segments"), SegmentsArray);
		SlotTracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
	}
	Result->SetArrayField(TEXT("slotAnimTracks"), SlotTracksArray);

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Animations"));
	FString ParentClassName = OptionalString(Params, TEXT("parentClass"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UObject* SkeletonAsset = UEditorAssetLibrary::LoadAsset(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;

	if (!ParentClassName.IsEmpty())
	{
		UClass* FoundClass = FindFirstObject<UClass>(*ParentClassName);
		if (FoundClass && FoundClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			Factory->ParentClass = FoundClass;
		}
	}

	auto Created = MCPCreateAssetIdempotent<UAnimBlueprint>(Name, PackagePath, OnConflict, TEXT("AnimBlueprint"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Created.Asset->GetName());
	Result->SetStringField(TEXT("class"), Created.Asset->GetClass()->GetName());
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// create_montage
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateMontage(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString AnimSequencePath;
	if (auto Err = RequireString(Params, TEXT("animSequencePath"), AnimSequencePath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Animations"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UObject* SourceAsset = UEditorAssetLibrary::LoadAsset(AnimSequencePath);
	UAnimSequence* SourceSequence = Cast<UAnimSequence>(SourceAsset);
	if (!SourceSequence)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AnimSequencePath));
	}

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = SourceSequence->GetSkeleton();
	Factory->SourceAnimation = SourceSequence;

	auto Created = MCPCreateAssetIdempotent<UAnimMontage>(Name, PackagePath, OnConflict, TEXT("AnimMontage"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Created.Asset->GetName());
	Result->SetStringField(TEXT("class"), Created.Asset->GetClass()->GetName());
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// read_blendspace
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlendSpace* BlendSpace = Cast<UBlendSpace>(LoadedAsset);
	if (!BlendSpace)
	{
		return MCPError(FString::Printf(TEXT("Failed to load BlendSpace at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), BlendSpace->GetName());
	Result->SetStringField(TEXT("class"), BlendSpace->GetClass()->GetName());

	// Skeleton
	USkeleton* Skeleton = BlendSpace->GetSkeleton();
	if (Skeleton)
	{
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	}
	else
	{
		Result->SetField(TEXT("skeleton"), MakeShared<FJsonValueNull>());
	}

	// Axis parameters
	TArray<TSharedPtr<FJsonValue>> AxesArray;
	for (int32 i = 0; i < 2; ++i)
	{
		const FBlendParameter& Param = BlendSpace->GetBlendParameter(i);
		TSharedPtr<FJsonObject> AxisObj = MakeShared<FJsonObject>();
		AxisObj->SetStringField(TEXT("displayName"), Param.DisplayName);
		AxisObj->SetNumberField(TEXT("min"), Param.Min);
		AxisObj->SetNumberField(TEXT("max"), Param.Max);
		AxisObj->SetNumberField(TEXT("gridNum"), Param.GridNum);
		AxesArray.Add(MakeShared<FJsonValueObject>(AxisObj));
	}
	Result->SetArrayField(TEXT("axes"), AxesArray);

	// Sample points
	TArray<TSharedPtr<FJsonValue>> SamplesArray;
	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	for (const FBlendSample& Sample : Samples)
	{
		TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
		if (Sample.Animation)
		{
			SampleObj->SetStringField(TEXT("animation"), Sample.Animation->GetPathName());
		}
		else
		{
			SampleObj->SetField(TEXT("animation"), MakeShared<FJsonValueNull>());
		}

		TSharedPtr<FJsonObject> ValueObj = MakeShared<FJsonObject>();
		ValueObj->SetNumberField(TEXT("x"), Sample.SampleValue.X);
		ValueObj->SetNumberField(TEXT("y"), Sample.SampleValue.Y);
		SampleObj->SetObjectField(TEXT("sampleValue"), ValueObj);

		SamplesArray.Add(MakeShared<FJsonValueObject>(SampleObj));
	}
	Result->SetArrayField(TEXT("samples"), SamplesArray);
	Result->SetNumberField(TEXT("sampleCount"), SamplesArray.Num());

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_anim_notify
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::AddAnimNotify(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NotifyName;
	if (auto Err = RequireString(Params, TEXT("notifyName"), NotifyName)) return Err;

	double TriggerTime = 0.0;
	if (!Params->TryGetNumberField(TEXT("triggerTime"), TriggerTime))
	{
		return MCPError(TEXT("Missing 'triggerTime' parameter"));
	}

	FString NotifyClassName = OptionalString(Params, TEXT("notifyClass"));

	// Load the animation asset — could be a montage or a sequence
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(LoadedAsset);
	if (!AnimAsset)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequenceBase at '%s'"), *AssetPath));
	}

	// Clamp trigger time to valid range
	float PlayLength = AnimAsset->GetPlayLength();
	float ClampedTime = FMath::Clamp(static_cast<float>(TriggerTime), 0.0f, PlayLength);

	// Idempotency: check for existing notify with same name at same trigger time
	const FName NotifyFName(*NotifyName);
	for (const FAnimNotifyEvent& Existing : AnimAsset->Notifies)
	{
		if (Existing.NotifyName == NotifyFName && FMath::IsNearlyEqual(Existing.GetTime(), ClampedTime, 0.001f))
		{
			auto ExistedRes = MCPSuccess();
			MCPSetExisted(ExistedRes);
			ExistedRes->SetStringField(TEXT("assetPath"), AssetPath);
			ExistedRes->SetStringField(TEXT("notifyName"), NotifyName);
			ExistedRes->SetNumberField(TEXT("triggerTime"), ClampedTime);
			return MCPResult(ExistedRes);
		}
	}

	// If a notify class is specified, try to find and instantiate it
	UAnimNotify* NewNotify = nullptr;
	if (!NotifyClassName.IsEmpty())
	{
		UClass* NotifyClass = FindFirstObject<UClass>(*NotifyClassName);
		if (!NotifyClass)
		{
			// Try with full path prefix
			NotifyClass = FindFirstObject<UClass>(*(TEXT("AnimNotify_") + NotifyClassName));
		}
		if (NotifyClass && NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
		{
			NewNotify = NewObject<UAnimNotify>(AnimAsset, NotifyClass);
		}
	}

	// Create the notify event
	FAnimNotifyEvent& NewEvent = AnimAsset->Notifies.AddDefaulted_GetRef();
	NewEvent.NotifyName = FName(*NotifyName);
	NewEvent.Link(AnimAsset, ClampedTime);
	NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimAsset->CalculateOffsetForNotify(ClampedTime));
	NewEvent.TrackIndex = 0;

	if (NewNotify)
	{
		NewEvent.Notify = NewNotify;

		// #528: UAnimNotify_PlayMontageNotify::BranchingPointNotify broadcasts the
		// NOTIFY OBJECT's own NotifyName, not the FAnimNotifyEvent's. We only set
		// the event name above, so any name-based routing in user code received
		// 'None'. Mirror the requested name onto the notify object's NotifyName
		// property (present on PlayMontageNotify / PlayMontageNotifyWindow) so
		// OnPlayMontageNotifyBegin broadcasts the correct name.
		if (FNameProperty* NameProp = CastField<FNameProperty>(NewNotify->GetClass()->FindPropertyByName(TEXT("NotifyName"))))
		{
			NameProp->SetPropertyValue_InContainer(NewNotify, NotifyFName);
		}
	}

	AnimAsset->SortNotifies();

	// #528: PostEditChange + save rebuilds the montage's branching-point markers
	// from the notifies (RefreshBranchingPointMarkers itself is private), so the
	// notify fires as a branching point with the name just written.
	AnimAsset->PostEditChange();
	AnimAsset->MarkPackageDirty();

	// Save the asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("notifyName"), NotifyName);
	Result->SetNumberField(TEXT("triggerTime"), ClampedTime);
	if (NewNotify)
	{
		Result->SetStringField(TEXT("notifyClass"), NewNotify->GetClass()->GetName());
	}
	// #471: paired remove handler now exists.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("notifyName"), NotifyName);
	MCPSetRollback(Result, TEXT("remove_anim_notify"), Payload);

	return MCPResult(Result);
}

// #471: remove notifies by name (and optionally by class). Idempotent -
// returns alreadyDeleted=true if no matching notifies exist. Useful for
// ability/montage migration scripts that need to prune obsolete notify
// instances (AuraFireLoopReady, AuraFire, etc.) before adding new ones.
//
// Params: assetPath, notifyName? (string), notifyClass? (string class name
//         or AnimNotify_ prefixed). Pass either or both - both filters
//         apply (AND). Returns the count and timestamps of removed
//         instances.
TSharedPtr<FJsonValue> FAnimationHandlers::RemoveAnimNotify(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NotifyName = OptionalString(Params, TEXT("notifyName"));
	FString NotifyClassName = OptionalString(Params, TEXT("notifyClass"));
	if (NotifyName.IsEmpty() && NotifyClassName.IsEmpty())
	{
		return MCPError(TEXT("Pass at least one of 'notifyName' or 'notifyClass'"));
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(LoadedAsset);
	if (!AnimAsset)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequenceBase at '%s'"), *AssetPath));
	}

	UClass* MatchClass = nullptr;
	if (!NotifyClassName.IsEmpty())
	{
		MatchClass = FindFirstObject<UClass>(*NotifyClassName);
		if (!MatchClass) MatchClass = FindFirstObject<UClass>(*(TEXT("AnimNotify_") + NotifyClassName));
	}

	const FName NotifyFName(*NotifyName);
	TArray<TSharedPtr<FJsonValue>> RemovedTimes;
	for (int32 i = AnimAsset->Notifies.Num() - 1; i >= 0; --i)
	{
		const FAnimNotifyEvent& E = AnimAsset->Notifies[i];
		const bool bNameMatches = NotifyName.IsEmpty() || E.NotifyName == NotifyFName;
		const bool bClassMatches = NotifyClassName.IsEmpty() ||
			(E.Notify && MatchClass && E.Notify->GetClass()->IsChildOf(MatchClass));
		if (bNameMatches && bClassMatches)
		{
			RemovedTimes.Add(MakeShared<FJsonValueNumber>(E.GetTime()));
			AnimAsset->Notifies.RemoveAt(i);
		}
	}

	if (RemovedTimes.Num() == 0)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("notifyName"), NotifyName);
		Noop->SetStringField(TEXT("notifyClass"), NotifyClassName);
		return MCPResult(Noop);
	}

	AnimAsset->SortNotifies();
	AnimAsset->PostEditChange();
	AnimAsset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("notifyName"), NotifyName);
	Result->SetStringField(TEXT("notifyClass"), NotifyClassName);
	Result->SetNumberField(TEXT("removedCount"), RemovedTimes.Num());
	Result->SetArrayField(TEXT("removedTimes"), RemovedTimes);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// create_blendspace
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Animations"));
	FString AxisHorizontal = OptionalString(Params, TEXT("axisHorizontal"), TEXT("Speed"));
	FString AxisVertical = OptionalString(Params, TEXT("axisVertical"), TEXT("Direction"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	double HorizontalMin = OptionalNumber(Params, TEXT("horizontalMin"), 0.0);
	double HorizontalMax = OptionalNumber(Params, TEXT("horizontalMax"), 500.0);
	double VerticalMin = OptionalNumber(Params, TEXT("verticalMin"), -180.0);
	double VerticalMax = OptionalNumber(Params, TEXT("verticalMax"), 180.0);

	UObject* SkeletonAsset = UEditorAssetLibrary::LoadAsset(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
	}

	UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
	Factory->TargetSkeleton = Skeleton;

	auto Created = MCPCreateAssetIdempotent<UBlendSpace>(Name, PackagePath, OnConflict, TEXT("BlendSpace"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UBlendSpace* BlendSpace = Created.Asset;
	FBlendParameter& BlendParam0 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(0));
	BlendParam0.DisplayName = AxisHorizontal;
	BlendParam0.Min = HorizontalMin;
	BlendParam0.Max = HorizontalMax;

	FBlendParameter& BlendParam1 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(1));
	BlendParam1.DisplayName = AxisVertical;
	BlendParam1.Min = VerticalMin;
	BlendParam1.Max = VerticalMax;

	UEditorAssetLibrary::SaveAsset(BlendSpace->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), BlendSpace->GetPathName());
	Result->SetStringField(TEXT("name"), BlendSpace->GetName());
	Result->SetStringField(TEXT("class"), BlendSpace->GetClass()->GetName());
	Result->SetStringField(TEXT("axisHorizontal"), AxisHorizontal);
	Result->SetStringField(TEXT("axisVertical"), AxisVertical);
	MCPSetDeleteAssetRollback(Result, BlendSpace->GetPathName());

	return MCPResult(Result);
}

// #459: explicit BlendSpace1D creation. Single-axis locomotion blendspaces
// (speed → walk/run) are the most common authoring path; the 2D create
// handler creates a UBlendSpace which won't behave as 1D.
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace1D(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Animations"));
	FString AxisName = OptionalString(Params, TEXT("axisName"), TEXT("Speed"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	const double AxisMin = OptionalNumber(Params, TEXT("axisMin"), 0.0);
	const double AxisMax = OptionalNumber(Params, TEXT("axisMax"), 500.0);
	const int32 GridNum = (int32)OptionalNumber(Params, TEXT("gridNum"), 4.0);

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));

	UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
	Factory->TargetSkeleton = Skeleton;

	auto Created = MCPCreateAssetIdempotent<UBlendSpace1D>(Name, PackagePath, OnConflict, TEXT("BlendSpace1D"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UBlendSpace1D* BS = Created.Asset;
	FBlendParameter& BlendParam0 = const_cast<FBlendParameter&>(BS->GetBlendParameter(0));
	BlendParam0.DisplayName = AxisName;
	BlendParam0.Min = AxisMin;
	BlendParam0.Max = AxisMax;
	BlendParam0.GridNum = GridNum;

	UEditorAssetLibrary::SaveAsset(BS->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), BS->GetPathName());
	Result->SetStringField(TEXT("name"), BS->GetName());
	Result->SetStringField(TEXT("class"), BS->GetClass()->GetName());
	Result->SetStringField(TEXT("axisName"), AxisName);
	MCPSetDeleteAssetRollback(Result, BS->GetPathName());
	return MCPResult(Result);
}

// #459: one-call axis-params + samples authoring. Replaces the
// "for each sample, call add_blend_sample" loop and the separate axis
// configuration in CreateBlendspace - the canonical locomotion authoring
// flow is "set axis name/range, plot samples at coordinates, save". Works
// for both UBlendSpace (1D and 2D) and UBlendSpace1D.
//
// Params: assetPath, axis (object: { name?, min?, max?, gridNum? }) OR
//         axisHorizontal/axisVertical with min/max/gridNum (2D-only),
//         samples: [{ animationPath, x, y? }], clearExisting? (default true).
TSharedPtr<FJsonValue> FAnimationHandlers::PopulateBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBlendSpace* BS = LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BS) return MCPError(FString::Printf(TEXT("BlendSpace not found at '%s'"), *AssetPath));

	BS->Modify();

	// Apply axis params. Three accepted shapes:
	// 1. `axis: { name?, min?, max?, gridNum? }` - applies to axis 0 (or pass `axisIndex`).
	// 2. `axes: [ {...}, {...} ]` - per-axis array.
	// 3. Top-level axisHorizontal/axisVertical + horizontalMin/horizontalMax/gridNumHorizontal etc.
	auto ApplyAxis = [&](int32 AxisIdx, const TSharedPtr<FJsonObject>& AxisObj)
	{
		FBlendParameter& BP = const_cast<FBlendParameter&>(BS->GetBlendParameter(AxisIdx));
		FString S; double D = 0; int32 I = 0;
		if (AxisObj->TryGetStringField(TEXT("name"), S)) BP.DisplayName = S;
		if (AxisObj->TryGetNumberField(TEXT("min"), D)) BP.Min = D;
		if (AxisObj->TryGetNumberField(TEXT("max"), D)) BP.Max = D;
		if (AxisObj->TryGetNumberField(TEXT("gridNum"), I)) BP.GridNum = I;
	};

	const TArray<TSharedPtr<FJsonValue>>* AxesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("axes"), AxesArr) && AxesArr)
	{
		for (int32 i = 0; i < AxesArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* AxisObj = nullptr;
			if ((*AxesArr)[i]->TryGetObject(AxisObj) && *AxisObj) ApplyAxis(i, *AxisObj);
		}
	}
	const TSharedPtr<FJsonObject>* AxisObj = nullptr;
	if (Params->TryGetObjectField(TEXT("axis"), AxisObj) && *AxisObj)
	{
		int32 AxisIdx = (int32)OptionalNumber(Params, TEXT("axisIndex"), 0.0);
		ApplyAxis(AxisIdx, *AxisObj);
	}

	// Top-level back-compat shape (same keys as create_blendspace).
	{
		FBlendParameter& BP0 = const_cast<FBlendParameter&>(BS->GetBlendParameter(0));
		FString S; double D = 0;
		if (Params->TryGetStringField(TEXT("axisHorizontal"), S)) BP0.DisplayName = S;
		if (Params->TryGetNumberField(TEXT("horizontalMin"), D)) BP0.Min = D;
		if (Params->TryGetNumberField(TEXT("horizontalMax"), D)) BP0.Max = D;
		int32 I = 0;
		if (Params->TryGetNumberField(TEXT("gridNumHorizontal"), I)) BP0.GridNum = I;

		// Only touch axis 1 if the asset has one (BlendSpace1D returns a stub for index 1 in some versions).
		const bool bIs1D = BS->IsA<UBlendSpace1D>();
		if (!bIs1D)
		{
			FBlendParameter& BP1 = const_cast<FBlendParameter&>(BS->GetBlendParameter(1));
			if (Params->TryGetStringField(TEXT("axisVertical"), S)) BP1.DisplayName = S;
			if (Params->TryGetNumberField(TEXT("verticalMin"), D)) BP1.Min = D;
			if (Params->TryGetNumberField(TEXT("verticalMax"), D)) BP1.Max = D;
			if (Params->TryGetNumberField(TEXT("gridNumVertical"), I)) BP1.GridNum = I;
		}
	}

	// Clear existing samples (default true) so partial-replace edits don't
	// pile up stale entries. Set clearExisting=false to append-only.
	const bool bClear = OptionalBool(Params, TEXT("clearExisting"), true);
	if (bClear)
	{
		const int32 SampleCount = BS->GetNumberOfBlendSamples();
		for (int32 i = SampleCount - 1; i >= 0; --i)
		{
			BS->DeleteSample(i);
		}
	}

	// Add samples.
	TArray<TSharedPtr<FJsonValue>> AddedIndices;
	TArray<TSharedPtr<FJsonValue>> Failed;
	const TArray<TSharedPtr<FJsonValue>>* SamplesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("samples"), SamplesArr) && SamplesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *SamplesArr)
		{
			const TSharedPtr<FJsonObject>* SObj = nullptr;
			if (!V->TryGetObject(SObj) || !*SObj) continue;
			FString AnimPath;
			if (!(*SObj)->TryGetStringField(TEXT("animationPath"), AnimPath))
				if (!(*SObj)->TryGetStringField(TEXT("animation"), AnimPath))
					(*SObj)->TryGetStringField(TEXT("path"), AnimPath);
			if (AnimPath.IsEmpty()) continue;
			UAnimSequence* Anim = LoadAssetByPath<UAnimSequence>(AnimPath);
			if (!Anim)
			{
				Failed.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("anim not found: %s"), *AnimPath)));
				continue;
			}
			double X = 0, Y = 0;
			(*SObj)->TryGetNumberField(TEXT("x"), X);
			(*SObj)->TryGetNumberField(TEXT("y"), Y);
			const int32 Idx = BS->AddSample(Anim, FVector((float)X, (float)Y, 0.0f));
			if (Idx < 0)
			{
				Failed.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("rejected (%.3f, %.3f) for %s"), X, Y, *AnimPath)));
				continue;
			}
			AddedIndices.Add(MakeShared<FJsonValueNumber>(Idx));
		}
	}

	// #710: rebuild BlendSpaceData (Segments/Triangles). Without this the
	// blendspace is un-triangulated and a BlendSpacePlayer outputs the ref
	// pose at runtime until the asset is opened in the editor (which calls
	// ResampleData itself). Bare PostEditChange does NOT rebuild triangulation.
	BS->ResampleData();
	BS->ValidateSampleData();

	BS->PostEditChange();
	BS->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(BS, /*bOnlyIfIsDirty*/ true);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), BS->GetPathName());
	Result->SetStringField(TEXT("class"), BS->GetClass()->GetName());
	Result->SetArrayField(TEXT("sampleIndices"), AddedIndices);
	Result->SetNumberField(TEXT("sampleCount"), BS->GetNumberOfBlendSamples());
	if (Failed.Num() > 0) Result->SetArrayField(TEXT("failed"), Failed);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// #248: append a sample to a BlendSpace's SampleData. UBlendSpace::AddSample
// is the canonical entry point - it validates the position against axis
// ranges + sets the GridSamples cache so the editor preview matches.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::AddBlendSample(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBlendSpace* BlendSpace = LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BlendSpace)
	{
		return MCPError(FString::Printf(TEXT("BlendSpace not found at '%s'"), *AssetPath));
	}

	FString AnimationPath;
	if (auto Err = RequireString(Params, TEXT("animation"), AnimationPath)) return Err;
	UAnimSequence* Anim = LoadAssetByPath<UAnimSequence>(AnimationPath);
	if (!Anim)
	{
		return MCPError(FString::Printf(TEXT("AnimSequence not found at '%s'"), *AnimationPath));
	}

	double PosX = 0.0, PosY = 0.0;
	const TSharedPtr<FJsonObject>* PosObj = nullptr;
	if (Params->TryGetObjectField(TEXT("position"), PosObj) && PosObj && (*PosObj).IsValid())
	{
		(*PosObj)->TryGetNumberField(TEXT("x"), PosX);
		(*PosObj)->TryGetNumberField(TEXT("y"), PosY);
	}
	else
	{
		Params->TryGetNumberField(TEXT("x"), PosX);
		Params->TryGetNumberField(TEXT("y"), PosY);
	}

	BlendSpace->Modify();
	const int32 NewSampleIndex = BlendSpace->AddSample(Anim, FVector(PosX, PosY, 0.0));
	if (NewSampleIndex < 0)
	{
		return MCPError(FString::Printf(
			TEXT("BlendSpace::AddSample rejected position (%.3f, %.3f) - check axis ranges via read_blendspace."),
			PosX, PosY));
	}
	// #710: retriangulate so the sample is usable at runtime without a manual editor open.
	BlendSpace->ResampleData();
	BlendSpace->ValidateSampleData();
	BlendSpace->PostEditChange();
	SaveAssetPackage(BlendSpace);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), BlendSpace->GetPathName());
	Result->SetStringField(TEXT("animation"), Anim->GetPathName());
	Result->SetNumberField(TEXT("sampleIndex"), NewSampleIndex);
	Result->SetNumberField(TEXT("x"), PosX);
	Result->SetNumberField(TEXT("y"), PosY);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// #272: relocate an existing BlendSpace sample (and optionally swap its
// AnimSequence). UBlendSpace::EditSampleValue rewrites coordinates + refreshes
// the GridSamples cache; the animation ref is swapped via SampleData direct
// access since there is no first-class setter.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetBlendSample(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBlendSpace* BlendSpace = LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BlendSpace)
	{
		return MCPError(FString::Printf(TEXT("BlendSpace not found at '%s'"), *AssetPath));
	}

	int32 SampleIndex = -1;
	if (!Params->TryGetNumberField(TEXT("sampleIndex"), SampleIndex))
	{
		return MCPError(TEXT("Missing required parameter 'sampleIndex'"));
	}
	if (SampleIndex < 0 || SampleIndex >= BlendSpace->GetNumberOfBlendSamples())
	{
		return MCPError(FString::Printf(
			TEXT("sampleIndex %d out of range (0..%d)"),
			SampleIndex, BlendSpace->GetNumberOfBlendSamples() - 1));
	}

	const FBlendSample& Existing = BlendSpace->GetBlendSample(SampleIndex);
	FVector NewPos = Existing.SampleValue;

	const TSharedPtr<FJsonObject>* PosObj = nullptr;
	bool bHasPos = false;
	if (Params->TryGetObjectField(TEXT("position"), PosObj) && PosObj && (*PosObj).IsValid())
	{
		double PX = NewPos.X, PY = NewPos.Y;
		(*PosObj)->TryGetNumberField(TEXT("x"), PX);
		(*PosObj)->TryGetNumberField(TEXT("y"), PY);
		NewPos = FVector(PX, PY, NewPos.Z);
		bHasPos = true;
	}
	else
	{
		double PX = 0, PY = 0;
		const bool bX = Params->TryGetNumberField(TEXT("x"), PX);
		const bool bY = Params->TryGetNumberField(TEXT("y"), PY);
		if (bX || bY)
		{
			NewPos = FVector(bX ? PX : NewPos.X, bY ? PY : NewPos.Y, NewPos.Z);
			bHasPos = true;
		}
	}

	BlendSpace->Modify();
	bool bUpdated = false;
	if (bHasPos)
	{
		BlendSpace->EditSampleValue(SampleIndex, NewPos);
		bUpdated = true;
	}

	FString NewAnimPath;
	if (Params->TryGetStringField(TEXT("animation"), NewAnimPath) && !NewAnimPath.IsEmpty())
	{
		UAnimSequence* NewAnim = LoadAssetByPath<UAnimSequence>(NewAnimPath);
		if (!NewAnim)
		{
			return MCPError(FString::Printf(TEXT("AnimSequence not found at '%s'"), *NewAnimPath));
		}
		BlendSpace->ReplaceSampleAnimation(SampleIndex, NewAnim);
		bUpdated = true;
	}

	if (!bUpdated)
	{
		return MCPError(TEXT("Nothing to update - provide position {x,y} and/or animation"));
	}

	// #710: retriangulate so the edited sample interpolates at runtime.
	BlendSpace->ResampleData();
	BlendSpace->ValidateSampleData();
	BlendSpace->PostEditChange();
	SaveAssetPackage(BlendSpace);

	const FBlendSample& Updated = BlendSpace->GetBlendSample(SampleIndex);
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), BlendSpace->GetPathName());
	Result->SetNumberField(TEXT("sampleIndex"), SampleIndex);
	Result->SetNumberField(TEXT("x"), Updated.SampleValue.X);
	Result->SetNumberField(TEXT("y"), Updated.SampleValue.Y);
	if (Updated.Animation)
	{
		Result->SetStringField(TEXT("animation"), Updated.Animation->GetPathName());
	}
	return MCPResult(Result);
}
static void SetSegmentLength(FAnimLinkableElement& Element, float NewLength)
{
	FProperty* Prop = FAnimLinkableElement::StaticStruct()->FindPropertyByName(TEXT("SegmentLength"));
	if (!Prop) return;

	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue_InContainer(&Element, NewLength);
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue_InContainer(&Element, static_cast<double>(NewLength));
	}
}

// ---------------------------------------------------------------------------
// Helper: Set the protected SequenceLength property on a montage via reflection.
// Handles both float (UE 5.3 and earlier) and double (UE 5.4+) property types.
// ---------------------------------------------------------------------------
static void SetMontageSequenceLength(UAnimMontage* Montage, float NewLength)
{
	FProperty* Prop = UAnimSequenceBase::StaticClass()->FindPropertyByName(TEXT("SequenceLength"));
	if (!Prop) return;

	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue_InContainer(Montage, NewLength);
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue_InContainer(Montage, static_cast<double>(NewLength));
	}
}

// ---------------------------------------------------------------------------
// set_montage_sequence — Replace the animation sequence in a montage's slot track
// Params: assetPath, animSequencePath, slotIndex? (default 0)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetMontageSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimSequencePath;
	if (auto Err = RequireString(Params, TEXT("animSequencePath"), AnimSequencePath)) return Err;

	double SlotIndex = OptionalNumber(Params, TEXT("slotIndex"), 0.0);

	// Load the montage
	UObject* MontageAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(MontageAsset);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	// Load the new sequence
	UObject* SeqAsset = UEditorAssetLibrary::LoadAsset(AnimSequencePath);
	UAnimSequence* NewSequence = Cast<UAnimSequence>(SeqAsset);
	if (!NewSequence)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AnimSequencePath));
	}

	// Access the slot tracks
	int32 TrackIdx = static_cast<int32>(SlotIndex);
	if (TrackIdx < 0 || TrackIdx >= Montage->SlotAnimTracks.Num())
	{
		return MCPError(FString::Printf(TEXT("Slot track index %d out of range (montage has %d tracks)"), TrackIdx, Montage->SlotAnimTracks.Num()));
	}

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[TrackIdx];

	// #626: when segmentIndex is given, replace only that one segment's
	// sequence; otherwise replace every segment in the slot (prior behavior).
	const bool bHasSegmentIndex = Params->HasField(TEXT("segmentIndex"));
	const int32 SegmentIndex = OptionalInt(Params, TEXT("segmentIndex"), -1);
	if (bHasSegmentIndex)
	{
		if (SegmentIndex < 0 || SegmentIndex >= SlotTrack.AnimTrack.AnimSegments.Num())
		{
			return MCPError(FString::Printf(TEXT("segmentIndex %d out of range (slot has %d segments)"),
				SegmentIndex, SlotTrack.AnimTrack.AnimSegments.Num()));
		}
	}

	// Replace the animation in the target segment(s) of this track
	int32 SegmentsUpdated = 0;
	for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
	{
		if (bHasSegmentIndex && SegIdx != SegmentIndex) continue;
		FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegIdx];
		Segment.SetAnimReference(NewSequence);
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = NewSequence->GetPlayLength();
		SegmentsUpdated++;
	}

	// If no segments exist, add one (only in whole-slot mode).
	if (SegmentsUpdated == 0 && !bHasSegmentIndex)
	{
		FAnimSegment NewSegment;
		NewSegment.SetAnimReference(NewSequence);
		NewSegment.AnimStartTime = 0.0f;
		NewSegment.AnimEndTime = NewSequence->GetPlayLength();
		SlotTrack.AnimTrack.AnimSegments.Add(NewSegment);
		SegmentsUpdated = 1;
	}

	// Recalculate total montage length from all slot tracks
	float NewTotalLength = 0.0f;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		NewTotalLength = FMath::Max(NewTotalLength, Track.AnimTrack.GetLength());
	}

	// Update SequenceLength (protected on UAnimSequenceBase) via property reflection
	SetMontageSequenceLength(Montage, NewTotalLength);

	// Update composite sections' segment lengths to match new duration
	for (FCompositeSection& Section : Montage->CompositeSections)
	{
		SetSegmentLength(Section, NewTotalLength);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animSequencePath"), AnimSequencePath);
	Result->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());
	Result->SetNumberField(TEXT("segmentsUpdated"), SegmentsUpdated);
	if (bHasSegmentIndex) Result->SetNumberField(TEXT("segmentIndex"), SegmentIndex);
	Result->SetNumberField(TEXT("sequenceLength"), NewSequence->GetPlayLength());
	Result->SetNumberField(TEXT("montageLength"), NewTotalLength);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_montage_properties — Set montage properties (duration, rate, blending)
// Params: assetPath, sequenceLength?, rateScale?, blendIn?, blendOut?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetMontageProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* MontageAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(MontageAsset);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	// Capture previous values for rollback
	const float PrevSeqLen = Montage->GetPlayLength();
	const float PrevRateScale = Montage->RateScale;
	const float PrevBlendIn = Montage->BlendIn.GetBlendTime();
	const float PrevBlendOut = Montage->BlendOut.GetBlendTime();

	TArray<FString> Modified;
	bool bAnyChanged = false;

	// sequenceLength — update via property reflection (SequenceLength is protected)
	double SeqLen;
	const bool bHasSeqLen = Params->TryGetNumberField(TEXT("sequenceLength"), SeqLen);
	if (bHasSeqLen)
	{
		float NewLength = static_cast<float>(SeqLen);
		if (!FMath::IsNearlyEqual(NewLength, PrevSeqLen))
		{
			SetMontageSequenceLength(Montage, NewLength);
			for (FCompositeSection& Section : Montage->CompositeSections)
			{
				SetSegmentLength(Section, NewLength);
			}
			Modified.Add(TEXT("sequenceLength"));
			bAnyChanged = true;
		}
	}

	// rateScale
	double RateScale;
	const bool bHasRate = Params->TryGetNumberField(TEXT("rateScale"), RateScale);
	if (bHasRate)
	{
		float NewRate = static_cast<float>(RateScale);
		if (!FMath::IsNearlyEqual(NewRate, PrevRateScale))
		{
			Montage->RateScale = NewRate;
			Modified.Add(TEXT("rateScale"));
			bAnyChanged = true;
		}
	}

	// blendIn
	double BlendIn;
	const bool bHasBlendIn = Params->TryGetNumberField(TEXT("blendIn"), BlendIn);
	if (bHasBlendIn)
	{
		float NewIn = static_cast<float>(BlendIn);
		if (!FMath::IsNearlyEqual(NewIn, PrevBlendIn))
		{
			Montage->BlendIn.SetBlendTime(NewIn);
			Modified.Add(TEXT("blendIn"));
			bAnyChanged = true;
		}
	}

	// blendOut
	double BlendOut;
	const bool bHasBlendOut = Params->TryGetNumberField(TEXT("blendOut"), BlendOut);
	if (bHasBlendOut)
	{
		float NewOut = static_cast<float>(BlendOut);
		if (!FMath::IsNearlyEqual(NewOut, PrevBlendOut))
		{
			Montage->BlendOut.SetBlendTime(NewOut);
			Modified.Add(TEXT("blendOut"));
			bAnyChanged = true;
		}
	}

	if (!bHasSeqLen && !bHasRate && !bHasBlendIn && !bHasBlendOut)
	{
		return MCPError(TEXT("No properties to set. Provide at least one of: sequenceLength, rateScale, blendIn, blendOut"));
	}

	// Idempotent: requested values match current state
	if (!bAnyChanged)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		return MCPResult(Noop);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	// Return current state
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& M : Modified)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(M));
	}
	Result->SetArrayField(TEXT("modified"), ModifiedArray);
	Result->SetNumberField(TEXT("sequenceLength"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("rateScale"), Montage->RateScale);
	Result->SetNumberField(TEXT("blendIn"), Montage->BlendIn.GetBlendTime());
	Result->SetNumberField(TEXT("blendOut"), Montage->BlendOut.GetBlendTime());

	// Rollback: self-inverse with previous values
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	if (bHasSeqLen) Payload->SetNumberField(TEXT("sequenceLength"), PrevSeqLen);
	if (bHasRate) Payload->SetNumberField(TEXT("rateScale"), PrevRateScale);
	if (bHasBlendIn) Payload->SetNumberField(TEXT("blendIn"), PrevBlendIn);
	if (bHasBlendOut) Payload->SetNumberField(TEXT("blendOut"), PrevBlendOut);
	MCPSetRollback(Result, TEXT("set_montage_properties"), Payload);

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAnimationHandlers::SetMontageSlot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SlotName;
	if (auto Err = RequireString(Params, TEXT("slotName"), SlotName)) return Err;

	int32 TrackIndex = OptionalInt(Params, TEXT("trackIndex"), 0);

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	if (TrackIndex < 0 || TrackIndex >= Montage->SlotAnimTracks.Num())
	{
		return MCPError(FString::Printf(TEXT("trackIndex %d out of range (0..%d)"), TrackIndex, Montage->SlotAnimTracks.Num() - 1));
	}

	// Capture previous slot name for rollback and idempotency
	const FName PrevSlot = Montage->SlotAnimTracks[TrackIndex].SlotName;
	const FName NewSlotFName(*SlotName);
	if (PrevSlot == NewSlotFName)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("slotName"), SlotName);
		Noop->SetNumberField(TEXT("trackIndex"), TrackIndex);
		return MCPResult(Noop);
	}

	Montage->SlotAnimTracks[TrackIndex].SlotName = NewSlotFName;

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("slotName"), SlotName);
	Result->SetNumberField(TEXT("trackIndex"), TrackIndex);

	// Rollback: self-inverse with previous slot name
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("slotName"), PrevSlot.ToString());
	Payload->SetNumberField(TEXT("trackIndex"), TrackIndex);
	MCPSetRollback(Result, TEXT("set_montage_slot"), Payload);

	return MCPResult(Result);
}

// ─── #27  add_montage_section ───────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::AddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SectionName;
	if (auto Err = RequireString(Params, TEXT("sectionName"), SectionName)) return Err;

	double StartTime = OptionalNumber(Params, TEXT("startTime"), 0.0);
	FString LinkedSection = OptionalString(Params, TEXT("linkedSection"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	// Idempotency: existing section short-circuits
	int32 ExistingIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (ExistingIdx != INDEX_NONE)
	{
		const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Section '%s' already exists at index %d"), *SectionName, ExistingIdx));
		}
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("assetPath"), AssetPath);
		Existed->SetStringField(TEXT("sectionName"), SectionName);
		Existed->SetNumberField(TEXT("sectionIndex"), ExistingIdx);
		return MCPResult(Existed);
	}

	// Add the composite section
	FCompositeSection NewSection;
	NewSection.SectionName = FName(*SectionName);
	NewSection.SetTime(static_cast<float>(StartTime));
	if (!LinkedSection.IsEmpty())
	{
		NewSection.NextSectionName = FName(*LinkedSection);
	}

	Montage->CompositeSections.Add(NewSection);

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sectionName"), SectionName);
	Result->SetNumberField(TEXT("startTime"), StartTime);
	if (!LinkedSection.IsEmpty())
	{
		Result->SetStringField(TEXT("linkedSection"), LinkedSection);
	}
	Result->SetNumberField(TEXT("totalSections"), Montage->CompositeSections.Num());
	// No rollback: no paired remove_montage_section handler.

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAnimationHandlers::ListControlRigVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	// In UE 5.7, ControlRigBlueprint was removed — load as a generic UBlueprint
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlueprint* CRBlueprint = Cast<UBlueprint>(LoadedAsset);
	if (!CRBlueprint)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Blueprint at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), CRBlueprint->GetName());
	Result->SetStringField(TEXT("class"), CRBlueprint->GetClass()->GetName());
	if (CRBlueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parentClass"), CRBlueprint->ParentClass->GetName());
	}

	// Read user-defined variables from the blueprint
	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	for (const FBPVariableDescription& Var : CRBlueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("defaultValue"), Var.DefaultValue);
		}
		VarObj->SetBoolField(TEXT("isPublic"),
			!!(Var.PropertyFlags & CPF_BlueprintVisible));
		VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VariablesArray);
	Result->SetNumberField(TEXT("variableCount"), VariablesArray.Num());

	// List all graphs
	TArray<UEdGraph*> AllGraphs;
	CRBlueprint->GetAllGraphs(AllGraphs);
	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	return MCPResult(Result);
}

// #619 read_control_rig_hierarchy - per-element hierarchy metadata (name, type,
// index, parent) from a Control Rig asset's URigHierarchy. ControlRigBlueprint
// is gone in 5.7, so reach the 'Hierarchy' UPROPERTY via reflection.
TSharedPtr<FJsonValue> FAnimationHandlers::ReadControlRigHierarchy(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlueprint* CRBlueprint = Cast<UBlueprint>(LoadedAsset);
	if (!CRBlueprint) return MCPError(FString::Printf(TEXT("Failed to load Blueprint at '%s'"), *AssetPath));

	FObjectProperty* HierProp = CastField<FObjectProperty>(CRBlueprint->GetClass()->FindPropertyByName(TEXT("Hierarchy")));
	if (!HierProp) return MCPError(TEXT("Asset has no 'Hierarchy' property - is this a Control Rig?"));

	URigHierarchy* Hierarchy = Cast<URigHierarchy>(HierProp->GetObjectPropertyValue_InContainer(CRBlueprint));
	if (!Hierarchy) return MCPError(TEXT("Control Rig Hierarchy is null"));

	const UEnum* TypeEnum = StaticEnum<ERigElementType>();
	TArray<TSharedPtr<FJsonValue>> Elements;
	for (const FRigElementKey& Key : Hierarchy->GetAllKeys(/*bTraverse=*/true))
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("name"), Key.Name.ToString());
		E->SetStringField(TEXT("type"), TypeEnum ? TypeEnum->GetNameStringByValue((int64)Key.Type) : FString::FromInt((int32)Key.Type));
		E->SetNumberField(TEXT("index"), Hierarchy->GetIndex(Key));
		const FRigElementKey Parent = Hierarchy->GetFirstParent(Key);
		if (Parent.IsValid())
		{
			E->SetStringField(TEXT("parent"), Parent.Name.ToString());
			E->SetStringField(TEXT("parentType"), TypeEnum ? TypeEnum->GetNameStringByValue((int64)Parent.Type) : FString());
		}
		Elements.Add(MakeShared<FJsonValueObject>(E));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("elementCount"), Elements.Num());
	Result->SetArrayField(TEXT("elements"), Elements);
	return MCPResult(Result);
}

// ===========================================================================
// v0.7.11 — Animation depth
// ===========================================================================

TSharedPtr<FJsonValue> FAnimationHandlers::SetRootMotionSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UAnimSequence* Seq = LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	Seq->Modify();
	bool EnableRootMotion;
	if (Params->TryGetBoolField(TEXT("enableRootMotion"), EnableRootMotion))
	{
		Seq->bEnableRootMotion = EnableRootMotion;
	}
	bool ForceRootLock;
	if (Params->TryGetBoolField(TEXT("forceRootLock"), ForceRootLock))
	{
		Seq->bForceRootLock = ForceRootLock;
	}
	bool UseNormalizedRootMotionScale;
	if (Params->TryGetBoolField(TEXT("useNormalizedRootMotionScale"), UseNormalizedRootMotionScale))
	{
		Seq->bUseNormalizedRootMotionScale = UseNormalizedRootMotionScale;
	}
	FString RootMotionMode;
	if (Params->TryGetStringField(TEXT("rootMotionRootLock"), RootMotionMode))
	{
		if      (RootMotionMode.Equals(TEXT("RefPose"),       ESearchCase::IgnoreCase)) Seq->RootMotionRootLock = ERootMotionRootLock::RefPose;
		else if (RootMotionMode.Equals(TEXT("AnimFirstFrame"), ESearchCase::IgnoreCase)) Seq->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		else if (RootMotionMode.Equals(TEXT("Zero"),          ESearchCase::IgnoreCase)) Seq->RootMotionRootLock = ERootMotionRootLock::Zero;
	}

	Seq->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Seq);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("enableRootMotion"), Seq->bEnableRootMotion);
	Result->SetBoolField(TEXT("forceRootLock"), Seq->bForceRootLock);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddVirtualBone(const TSharedPtr<FJsonObject>& Params)
{
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	FString SourceBone;
	if (auto Err = RequireString(Params, TEXT("sourceBone"), SourceBone)) return Err;
	FString TargetBone;
	if (auto Err = RequireString(Params, TEXT("targetBone"), TargetBone)) return Err;

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	Skeleton->Modify();
	FName NewBoneName;
	const bool bOk = Skeleton->AddNewVirtualBone(FName(*SourceBone), FName(*TargetBone), NewBoneName);
	if (!bOk)
	{
		return MCPError(TEXT("Failed to add virtual bone (source/target invalid or duplicate)"));
	}
	Skeleton->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Skeleton);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("virtualBoneName"), NewBoneName.ToString());

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Rollback->SetStringField(TEXT("virtualBoneName"), NewBoneName.ToString());
	MCPSetRollback(Result, TEXT("remove_virtual_bone"), Rollback);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::RemoveVirtualBone(const TSharedPtr<FJsonObject>& Params)
{
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	FString BoneName;
	if (auto Err = RequireString(Params, TEXT("virtualBoneName"), BoneName)) return Err;

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	// Idempotency: check if virtual bone exists
	const FName BoneFName(*BoneName);
	bool bFound = false;
	for (const FVirtualBone& VB : Skeleton->GetVirtualBones())
	{
		if (VB.VirtualBoneName == BoneFName) { bFound = true; break; }
	}
	if (!bFound)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		Noop->SetStringField(TEXT("virtualBoneName"), BoneName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	Skeleton->Modify();
	TArray<FName> ToRemove = { BoneFName };
	Skeleton->RemoveVirtualBones(ToRemove);
	Skeleton->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Skeleton);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("removed"), BoneName);
	Result->SetBoolField(TEXT("deleted"), true);
	// No rollback: removal of a virtual bone is not reversible without source/target capture.
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAnimationHandlers::SetAnimBlueprintSkeleton(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
	if (!AnimBP) return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	AnimBP->TargetSkeleton = Skeleton;
	AnimBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	return MCPResult(Result);
}
