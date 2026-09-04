#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
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
#include "Animation/AnimNotifies/AnimNotifyState.h"
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
// FArrayProperty / FScriptArrayHelper: the #880 readback of the montage's
// private BranchingPointMarkers cache goes through its UPROPERTY.
#include "UObject/UnrealType.h"
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

// IK Rig (#93) - use subdirectory path for UE 5.7
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"

// Control Rig (#11) - ControlRigBlueprint removed in UE 5.7, use reflection
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"

// Curve identifiers for UE5 animation data controller
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"

void FAnimationHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_anim_assets"), &ListAnimAssets);
	Registry.RegisterHandler(TEXT("begin_skeleton_edit"), &BeginSkeletonEdit);
	Registry.RegisterHandler(TEXT("edit_skeleton_bones"), &EditSkeletonBones);
	Registry.RegisterHandler(TEXT("commit_skeleton_edit"), &CommitSkeletonEdit);
	Registry.RegisterHandler(TEXT("cancel_skeleton_edit"), &CancelSkeletonEdit);
	Registry.RegisterHandler(TEXT("set_bone_retargeting"), &SetBoneRetargeting);
	Registry.RegisterHandler(TEXT("author_blend_profile"), &AuthorBlendProfile);
	Registry.RegisterHandler(TEXT("edit_curve_metadata"), &EditCurveMetadata);
	Registry.RegisterHandler(TEXT("register_compatible_skeleton"), &RegisterCompatibleSkeleton);
	Registry.RegisterHandler(TEXT("list_skeletal_meshes"), &ListSkeletalMeshes);
	Registry.RegisterHandler(TEXT("get_skeleton_info"), &GetSkeletonInfo);
	Registry.RegisterHandler(TEXT("list_animation_sockets"), &ListSockets);
	Registry.RegisterHandler(TEXT("get_physics_asset_info"), &GetPhysicsAssetInfo);
	Registry.RegisterHandler(TEXT("read_anim_blueprint"), &ReadAnimBlueprint);
	Registry.RegisterHandler(TEXT("read_anim_montage"), &ReadAnimMontage);
	Registry.RegisterHandler(TEXT("read_anim_sequence"), &ReadAnimSequence);
	Registry.RegisterHandler(TEXT("scan_animation_tracks"), &ScanAnimationTracks);
	Registry.RegisterHandler(TEXT("create_anim_blueprint"), &CreateAnimBlueprint);
	Registry.RegisterHandler(TEXT("create_anim_montage"), &CreateMontage);
	Registry.RegisterHandler(TEXT("author_montages_batch"), &AuthorMontagesBatch);
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

	// Montage segment authoring (#826)
	Registry.RegisterHandler(TEXT("add_montage_segment"), &AddMontageSegment);
	Registry.RegisterHandler(TEXT("remove_montage_segment"), &RemoveMontageSegment);
	Registry.RegisterHandler(TEXT("list_montage_segments"), &ListMontageSegments);

	// IK Rig (#93)
	Registry.RegisterHandler(TEXT("create_ik_rig"), &CreateIKRig);
	Registry.RegisterHandler(TEXT("read_ik_rig"), &ReadIKRig);
	Registry.RegisterHandler(TEXT("configure_ik_rig"), &ConfigureIKRig);
	// #701/#703: IK authoring tail + batch retarget.
	Registry.RegisterHandler(TEXT("set_ik_rig_mesh"), &SetIKRigMesh);
	Registry.RegisterHandler(TEXT("set_ik_retargeter_rig"), &SetIKRetargeterRig);
	Registry.RegisterHandler(TEXT("auto_align_retarget_pose"), &AutoAlignRetargetPose);
	Registry.RegisterHandler(TEXT("reset_retarget_pose"), &ResetRetargetPose);
	Registry.RegisterHandlerWithTimeout(TEXT("batch_retarget_animations"), &BatchRetargetAnimations, 300.0f);

	// Control Rig (#11)
	Registry.RegisterHandler(TEXT("list_control_rig_variables"), &ListControlRigVariables);
	Registry.RegisterHandler(TEXT("read_control_rig_graph"), &ReadControlRigGraph);
	Registry.RegisterHandler(TEXT("read_control_rig_hierarchy"), &ReadControlRigHierarchy);
	Registry.RegisterHandler(TEXT("begin_control_rig_edit"), &BeginControlRigEdit);
	Registry.RegisterHandler(TEXT("read_control_rig_edit"), &ReadControlRigEdit);
	Registry.RegisterHandler(TEXT("apply_control_rig_edits"), &ApplyControlRigEdits);
	Registry.RegisterHandler(TEXT("bake_control_rig_edit"), &BakeControlRigEdit);
	Registry.RegisterHandler(TEXT("analyze_animation"), &AnalyzeAnimation);

	// v0.7.11 - depth
	Registry.RegisterHandler(TEXT("set_root_motion_settings"), &SetRootMotionSettings);
	Registry.RegisterHandler(TEXT("add_virtual_bone"), &AddVirtualBone);
	Registry.RegisterHandler(TEXT("remove_virtual_bone"), &RemoveVirtualBone);
	Registry.RegisterHandler(TEXT("create_anim_composite"), &CreateAnimComposite);
	Registry.RegisterHandler(TEXT("list_anim_modifiers"), &ListAnimModifiers);

	// v0.7.11 - issue fixes
	Registry.RegisterHandler(TEXT("create_ik_retargeter"), &CreateIKRetargeter);
	Registry.RegisterHandler(TEXT("read_ik_retargeter"), &ReadIKRetargeter);
	Registry.RegisterHandler(TEXT("configure_ik_retargeter"), &ConfigureIKRetargeter);
	Registry.RegisterHandler(TEXT("set_anim_blueprint_skeleton"), &SetAnimBlueprintSkeleton);
	Registry.RegisterHandler(TEXT("read_bone_track"), &ReadBoneTrack);

	// v1.0.0-rc.2 - animation authoring gaps (#153, #154)
	Registry.RegisterHandler(TEXT("set_sequence_properties"), &SetSequenceProperties);
	Registry.RegisterHandler(TEXT("bake_root_motion_from_bone"), &BakeRootMotionFromBone);

	// v0.7.15 - PoseSearch (motion matching)
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

	// #713 - distance-matching graph authoring
	Registry.RegisterHandler(TEXT("add_sequence_evaluator"), &AddSequenceEvaluator);
	Registry.RegisterHandler(TEXT("bind_anim_node_function"), &BindAnimNodeFunction);

	// #419/#420 - live-actor skeletal reads + rebind + preview (moved from Level)
	Registry.RegisterHandler(TEXT("get_bone_transform"), &GetBoneTransform);
	Registry.RegisterHandler(TEXT("list_bones"), &ListBones);
	Registry.RegisterHandler(TEXT("rebind_leader_pose"), &RebindLeaderPose);
	Registry.RegisterHandler(TEXT("preview_animation"), &PreviewAnimation);
	Registry.RegisterHandler(TEXT("set_live_post_process_anim_blueprint"), &SetLivePostProcessAnimBlueprint);

	// #922/#923/#926 - evaluated pose reads (live component, and asset sampling)
	Registry.RegisterHandler(TEXT("get_live_bone_transforms"), &GetLiveBoneTransforms);
	Registry.RegisterHandler(TEXT("sample_pose"), &SamplePose);
	Registry.RegisterHandler(TEXT("measure_natural_speed"), &MeasureNaturalSpeed);

	// Authoring depth (AnimationHandlers_Depth.cpp): the entry wiring that made
	// state machines run, the five removals whose adds documented their own
	// missing inverse, windowed notifies, and sync markers.
	Registry.RegisterHandler(TEXT("set_state_machine_entry"), &SetStateMachineEntry);
	Registry.RegisterHandler(TEXT("remove_state"), &RemoveState);
	Registry.RegisterHandler(TEXT("remove_transition"), &RemoveTransition);
	Registry.RegisterHandler(TEXT("remove_state_machine"), &RemoveStateMachine);
	Registry.RegisterHandler(TEXT("remove_montage_section"), &RemoveMontageSection);
	Registry.RegisterHandler(TEXT("remove_anim_curve"), &RemoveAnimCurve);
	Registry.RegisterHandler(TEXT("add_anim_notify_state"), &AddNotifyState);
	Registry.RegisterHandler(TEXT("remove_anim_notify_state"), &RemoveNotifyState);
	Registry.RegisterHandler(TEXT("set_sync_markers"), &SetSyncMarkers);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListAnimAssets(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const FString Directory = OptionalString(Params, TEXT("directory"));

	// T3: paged. Four class queries concatenated is the largest read on this
	// category, and it had no cap at all: a project with a few thousand
	// sequences answered every one of them in a single response.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_anim_assets|directory=%s|recursive=%d"), *Directory, bRecursive ? 1 : 0),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Asset class names to search for
	TArray<FTopLevelAssetPath> ClassPaths;
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimSequence")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimMontage")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("BlendSpace")));

	TArray<MCPPagination::FPageRow> Rows;

	for (const FTopLevelAssetPath& ClassPath : ClassPaths)
	{
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssetsByClass(ClassPath, AssetDataList, bRecursive);

		for (const FAssetData& AssetData : AssetDataList)
		{
			const FString ObjectPath = AssetData.GetObjectPathString();
			// `directory` has been advertised on this action all along and was
			// never read, so a caller scoping to one folder got the whole
			// project back and no sign that the scope had been dropped.
			if (!Directory.IsEmpty() && !ObjectPath.StartsWith(Directory)) continue;

			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), ObjectPath);
			AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
			AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
			// The object path is the anchor: unique across the project, and the
			// one string that names this asset on a second enumeration.
			Rows.Add({ ObjectPath, MakeShared<FJsonValueObject>(AssetObj) });
		}
	}

	// Four separate registry queries concatenated, none of which promises an
	// order, so the whole set is sorted before paging. Without it the same page
	// can come back in a different order between two calls and the anchor would
	// report a change that is only the registry reshuffling.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	if (!Directory.IsEmpty()) Result->SetStringField(TEXT("directory"), Directory);
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListSkeletalMeshes(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const FString Directory = OptionalString(Params, TEXT("directory"));

	// T3: paged. This is the action every animation workflow starts with, so
	// the uncapped list was answered on a project with a full character library
	// as one response holding every mesh in it.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_skeletal_meshes|directory=%s|recursive=%d"), *Directory, bRecursive ? 1 : 0),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SkeletalMesh")), AssetDataList, bRecursive);

	TArray<MCPPagination::FPageRow> Rows;
	for (const FAssetData& AssetData : AssetDataList)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		// `directory` has been advertised on this action all along and was
		// never read, so a caller scoping to one folder got the whole project
		// back and no sign that the scope had been dropped.
		if (!Directory.IsEmpty() && !ObjectPath.StartsWith(Directory)) continue;

		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), ObjectPath);
		AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		// The object path is the anchor: unique, and the same string on a
		// second enumeration.
		Rows.Add({ ObjectPath, MakeShared<FJsonValueObject>(AssetObj) });
	}

	// The asset registry does not promise an enumeration order, so the rows are
	// sorted before paging. A cursor over an unordered enumeration is not
	// resumable: its anchor would land somewhere different every call.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	if (!Directory.IsEmpty()) Result->SetStringField(TEXT("directory"), Directory);
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);

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

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
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

	UAnimSequence* SourceSequence = LoadAssetByPath<UAnimSequence>(AnimSequencePath);
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

namespace
{
	bool MontageBatchCallSucceeded(const TSharedPtr<FJsonValue>& Response, FString& OutError)
	{
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			OutError = TEXT("Handler returned an invalid response");
			return false;
		}

		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		bool bSuccess = false;
		if (!Object.IsValid() || !Object->TryGetBoolField(TEXT("success"), bSuccess) || !bSuccess)
		{
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("error"), OutError))
			{
				OutError = TEXT("Handler reported failure without an error message");
			}
			return false;
		}
		return true;
	}

	void CopyOptionalNumber(
		const TSharedPtr<FJsonObject>& Source,
		const TCHAR* Field,
		const TSharedPtr<FJsonObject>& Destination)
	{
		double Value = 0.0;
		if (Source->TryGetNumberField(Field, Value))
		{
			Destination->SetNumberField(Field, Value);
		}
	}

	bool SaveMontagePackage(const FString& AssetPath, FString& OutError)
	{
		UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
		if (!Montage)
		{
			OutError = FString::Printf(TEXT("Failed to reload authored montage '%s' for saving"), *AssetPath);
			return false;
		}

		if (!SaveAssetPackage(Montage))
		{
			OutError = FString::Printf(
				TEXT("Failed to save authored montage package '%s'"),
				*Montage->GetOutermost()->GetName());
			return false;
		}
		return true;
	}
}

// Batch-author montages with deterministic per-item results. Existing montages
// are configured in place, while newly-created montages are included in a
// delete_asset_batch rollback descriptor.
TSharedPtr<FJsonValue> FAnimationHandlers::AuthorMontagesBatch(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Params->TryGetArrayField(TEXT("items"), Items) || !Items)
	{
		return MCPError(TEXT("Missing 'items' array"));
	}

	TArray<TSharedPtr<FJsonValue>> ItemResults;
	TArray<TSharedPtr<FJsonValue>> CreatedPaths;
	int32 Succeeded = 0;
	int32 Failed = 0;

	for (int32 ItemIndex = 0; ItemIndex < Items->Num(); ++ItemIndex)
	{
		const TSharedPtr<FJsonObject> Item = (*Items)[ItemIndex].IsValid()
			? (*Items)[ItemIndex]->AsObject()
			: nullptr;
		TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetNumberField(TEXT("index"), ItemIndex);

		FString Name;
		FString SequencePath;
		if (!Item.IsValid() ||
			!Item->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() ||
			!Item->TryGetStringField(TEXT("animSequencePath"), SequencePath) || SequencePath.IsEmpty())
		{
			ItemResult->SetBoolField(TEXT("success"), false);
			ItemResult->SetStringField(TEXT("stage"), TEXT("validate"));
			ItemResult->SetStringField(TEXT("error"), TEXT("Each item requires non-empty 'name' and 'animSequencePath'"));
			ItemResults.Add(MakeShared<FJsonValueObject>(ItemResult));
			++Failed;
			continue;
		}

		const FString PackagePath = OptionalString(Item, TEXT("packagePath"), TEXT("/Game/Animations"));
		const FString MontagePath = PackagePath + TEXT("/") + Name;
		const bool bExistedBefore = UEditorAssetLibrary::DoesAssetExist(MontagePath);

		TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
		CreateParams->SetStringField(TEXT("name"), Name);
		CreateParams->SetStringField(TEXT("animSequencePath"), SequencePath);
		CreateParams->SetStringField(TEXT("packagePath"), PackagePath);
		CreateParams->SetStringField(TEXT("onConflict"), OptionalString(Item, TEXT("onConflict"), TEXT("skip")));

		FString Error;
		// Which authoring step a failure came from. Every step below writes to
		// the same montage, so the sub-handler's message alone ("Failed to load
		// AnimMontage at ...") does not say what the caller got wrong.
		FString Stage;
		if (!MontageBatchCallSucceeded(FAnimationHandlers::CreateMontage(CreateParams), Error))
		{
			ItemResult->SetBoolField(TEXT("success"), false);
			ItemResult->SetStringField(TEXT("name"), Name);
			ItemResult->SetStringField(TEXT("stage"), TEXT("create"));
			ItemResult->SetStringField(TEXT("error"), Error);
			ItemResults.Add(MakeShared<FJsonValueObject>(ItemResult));
			++Failed;
			continue;
		}

		bool bItemSuccess = true;
		const FString SlotName = OptionalString(Item, TEXT("slotName"));
		if (!SlotName.IsEmpty())
		{
			TSharedPtr<FJsonObject> SlotParams = MakeShared<FJsonObject>();
			SlotParams->SetStringField(TEXT("assetPath"), MontagePath);
			SlotParams->SetStringField(TEXT("slotName"), SlotName);
			double TrackIndex = 0.0;
			if (Item->TryGetNumberField(TEXT("trackIndex"), TrackIndex))
			{
				SlotParams->SetNumberField(TEXT("trackIndex"), TrackIndex);
			}
			bItemSuccess = MontageBatchCallSucceeded(FAnimationHandlers::SetMontageSlot(SlotParams), Error);
			if (!bItemSuccess) Stage = TEXT("slot");
		}

		TSharedPtr<FJsonObject> PropertyParams = MakeShared<FJsonObject>();
		PropertyParams->SetStringField(TEXT("assetPath"), MontagePath);
		CopyOptionalNumber(Item, TEXT("rateScale"), PropertyParams);
		CopyOptionalNumber(Item, TEXT("blendIn"), PropertyParams);
		CopyOptionalNumber(Item, TEXT("blendOut"), PropertyParams);
		CopyOptionalNumber(Item, TEXT("sequenceLength"), PropertyParams);
		if (bItemSuccess && PropertyParams->Values.Num() > 1)
		{
			bItemSuccess = MontageBatchCallSucceeded(FAnimationHandlers::SetMontageProperties(PropertyParams), Error);
			if (!bItemSuccess) Stage = TEXT("properties");
		}

		const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
		if (bItemSuccess && Item->TryGetArrayField(TEXT("sections"), Sections) && Sections)
		{
			for (const TSharedPtr<FJsonValue>& SectionValue : *Sections)
			{
				const TSharedPtr<FJsonObject> Section = SectionValue.IsValid() ? SectionValue->AsObject() : nullptr;
				FString SectionName;
				if (!Section.IsValid() || !Section->TryGetStringField(TEXT("sectionName"), SectionName))
				{
					bItemSuccess = false;
					Stage = TEXT("sections");
					Error = TEXT("Each section requires 'sectionName'");
					break;
				}
				TSharedPtr<FJsonObject> SectionParams = MakeShared<FJsonObject>();
				SectionParams->SetStringField(TEXT("assetPath"), MontagePath);
				SectionParams->SetStringField(TEXT("sectionName"), SectionName);
				CopyOptionalNumber(Section, TEXT("startTime"), SectionParams);
				FString LinkedSection;
				if (Section->TryGetStringField(TEXT("linkedSection"), LinkedSection))
				{
					SectionParams->SetStringField(TEXT("linkedSection"), LinkedSection);
				}
				if (!MontageBatchCallSucceeded(FAnimationHandlers::AddMontageSection(SectionParams), Error))
				{
					bItemSuccess = false;
					Stage = TEXT("sections");
					break;
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Notifies = nullptr;
		if (bItemSuccess && Item->TryGetArrayField(TEXT("notifies"), Notifies) && Notifies)
		{
			for (const TSharedPtr<FJsonValue>& NotifyValue : *Notifies)
			{
				const TSharedPtr<FJsonObject> Notify = NotifyValue.IsValid() ? NotifyValue->AsObject() : nullptr;
				FString NotifyName;
				double TriggerTime = 0.0;
				if (!Notify.IsValid() ||
					!Notify->TryGetStringField(TEXT("notifyName"), NotifyName) ||
					!Notify->TryGetNumberField(TEXT("triggerTime"), TriggerTime))
				{
					bItemSuccess = false;
					Stage = TEXT("notifies");
					Error = TEXT("Each notify requires 'notifyName' and numeric 'triggerTime'");
					break;
				}
				TSharedPtr<FJsonObject> NotifyParams = MakeShared<FJsonObject>();
				NotifyParams->SetStringField(TEXT("assetPath"), MontagePath);
				NotifyParams->SetStringField(TEXT("notifyName"), NotifyName);
				NotifyParams->SetNumberField(TEXT("triggerTime"), TriggerTime);
				FString NotifyClass;
				if (Notify->TryGetStringField(TEXT("notifyClass"), NotifyClass))
				{
					NotifyParams->SetStringField(TEXT("notifyClass"), NotifyClass);
				}
				const TSharedPtr<FJsonObject>* NotifyProperties = nullptr;
				if (Notify->TryGetObjectField(TEXT("properties"), NotifyProperties) && NotifyProperties)
				{
					NotifyParams->SetObjectField(TEXT("notifyProperties"), *NotifyProperties);
				}
				if (!MontageBatchCallSucceeded(FAnimationHandlers::AddAnimNotify(NotifyParams), Error))
				{
					bItemSuccess = false;
					Stage = TEXT("notifies");
					break;
				}
			}
		}

		if (bItemSuccess)
		{
			bItemSuccess = SaveMontagePackage(MontagePath, Error);
			if (!bItemSuccess) Stage = TEXT("save");
		}

		ItemResult->SetBoolField(TEXT("success"), bItemSuccess);
		ItemResult->SetStringField(TEXT("name"), Name);
		ItemResult->SetStringField(TEXT("assetPath"), MontagePath);
		ItemResult->SetBoolField(TEXT("created"), !bExistedBefore);
		ItemResult->SetBoolField(TEXT("existed"), bExistedBefore);
		if (!bItemSuccess)
		{
			ItemResult->SetStringField(TEXT("stage"), Stage);
			ItemResult->SetStringField(TEXT("error"), Error);
			++Failed;
		}
		else
		{
			++Succeeded;
		}
		if (!bExistedBefore)
		{
			CreatedPaths.Add(MakeShared<FJsonValueString>(MontagePath));
		}
		ItemResults.Add(MakeShared<FJsonValueObject>(ItemResult));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("requested"), Items->Num());
	Result->SetNumberField(TEXT("succeeded"), Succeeded);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("items"), ItemResults);
	if (CreatedPaths.Num() > 0)
	{
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetArrayField(TEXT("assetPaths"), CreatedPaths);
		MCPSetRollback(Result, TEXT("delete_asset_batch"), RollbackPayload);
	}
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

	// Load the animation asset - could be a montage or a sequence
	UAnimSequenceBase* AnimAsset = LoadAssetByPath<UAnimSequenceBase>(AssetPath);
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
		UClass* NotifyClass = LoadObject<UClass>(nullptr, *NotifyClassName);
		if (!NotifyClass)
		{
			NotifyClass = FindFirstObject<UClass>(*NotifyClassName);
		}
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

	// notifyProperties writes onto the spawned notify OBJECT, so it only has
	// somewhere to land when notifyClass resolved. Reporting success while
	// quietly dropping the requested values is the failure mode this guards.
	const TSharedPtr<FJsonObject>* NotifyProperties = nullptr;
	if (Params->TryGetObjectField(TEXT("notifyProperties"), NotifyProperties)
		&& NotifyProperties && (*NotifyProperties).IsValid() && (*NotifyProperties)->Values.Num() > 0)
	{
		if (!NewNotify)
		{
			if (NotifyClassName.IsEmpty())
			{
				return MCPError(TEXT("'notifyProperties' requires 'notifyClass': a bare notify event has no notify object to write properties onto"));
			}
			return MCPError(FString::Printf(
				TEXT("notifyClass '%s' did not resolve to a UAnimNotify subclass, so 'notifyProperties' could not be applied"),
				*NotifyClassName));
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*NotifyProperties)->Values)
		{
			FProperty* Property = NewNotify->GetClass()->FindPropertyByName(FName(*Entry.Key));
			if (!Property)
			{
				return MCPError(FString::Printf(
					TEXT("Notify class '%s' has no property '%s'"),
					*NewNotify->GetClass()->GetName(),
					*Entry.Key));
			}
			FString PropertyError;
			if (!MCPJsonProperty::SetJsonOnProperty(
				Property,
				Property->ContainerPtrToValuePtr<void>(NewNotify),
				Entry.Value,
				PropertyError))
			{
				return MCPError(FString::Printf(
					TEXT("Failed to set notify property '%s': %s"),
					*Entry.Key,
					*PropertyError));
			}
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

	// #880: a notify only becomes a branching point when its event says so.
	// FAnimNotifyEvent::MontageTickType defaults to Queued, and
	// UAnimMontage::RefreshBranchingPointMarkers only records the events whose
	// tick type is BranchingPoint - so a PlayMontageNotify added with the
	// default left BranchingPointMarkers empty and
	// UAnimNotify_PlayMontageNotify::BranchingPointNotify, the only thing that
	// broadcasts OnPlayMontageNotifyBegin, was never reached. #528 set the name
	// the delegate carries; this sets the tick type that gets it called.
	//
	// Montage notifies whose class is one of the PlayMontageNotify pair default
	// to BranchingPoint because that is the only tick type at which they do
	// anything. Everything else keeps the engine's Queued default, which is the
	// cheaper tick. `branchingPoint` overrides either way.
	const bool bIsMontage = AnimAsset->IsA<UAnimMontage>();
	bool bWantBranchingPoint = false;
	if (bIsMontage && NewNotify)
	{
		for (const UClass* NotifyClass = NewNotify->GetClass(); NotifyClass; NotifyClass = NotifyClass->GetSuperClass())
		{
			const FString ClassName = NotifyClass->GetName();
			if (ClassName == TEXT("AnimNotify_PlayMontageNotify") ||
				ClassName == TEXT("AnimNotify_PlayMontageNotifyWindow"))
			{
				bWantBranchingPoint = true;
				break;
			}
		}
	}
	bool bExplicitBranchingPoint = false;
	if (Params->TryGetBoolField(TEXT("branchingPoint"), bExplicitBranchingPoint))
	{
		bWantBranchingPoint = bExplicitBranchingPoint;
	}
	if (bWantBranchingPoint)
	{
		NewEvent.MontageTickType = EMontageNotifyTickType::BranchingPoint;
	}

	AnimAsset->SortNotifies();

	// RefreshBranchingPointMarkers itself is private; RefreshCacheData is the
	// public entry that calls it, and it also rebuilds the notify tracks and
	// sync markers the editor caches. PostEditChange alone dispatches a
	// property-changed event with no property attached, which is not the path
	// that rebuilds the cache.
	AnimAsset->RefreshCacheData();
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
	if (bIsMontage)
	{
		// #880 read back what the refresh produced rather than asserting it
		// happened. BranchingPointMarkers is private, so this goes through the
		// UPROPERTY, which is what the runtime reads too.
		Result->SetBoolField(TEXT("branchingPoint"), bWantBranchingPoint);
		int32 MarkerCount = INDEX_NONE;
		if (const FArrayProperty* MarkersProperty =
			CastField<FArrayProperty>(AnimAsset->GetClass()->FindPropertyByName(TEXT("BranchingPointMarkers"))))
		{
			FScriptArrayHelper Helper(MarkersProperty, MarkersProperty->ContainerPtrToValuePtr<void>(AnimAsset));
			MarkerCount = Helper.Num();
		}
		Result->SetNumberField(TEXT("branchingPointMarkerCount"), MarkerCount);
		if (bWantBranchingPoint && MarkerCount == 0)
		{
			Result->SetStringField(TEXT("branchingPointWarning"),
				TEXT("The notify was added as a branching point but the montage's BranchingPointMarkers cache is empty, so OnPlayMontageNotifyBegin will not broadcast until the montage is reloaded."));
		}
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

	UAnimSequenceBase* AnimAsset = LoadAssetByPath<UAnimSequenceBase>(AssetPath);
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
	// Everything the paired add action needs to put one of these back, recorded
	// as it is removed. The filter keys on the notify NAME, and Notifies holds
	// notify STATES as well as instant notifies, so the two kinds are recorded
	// apart: a state carries its own class and a duration and comes back through
	// add_anim_notify_state, while add_anim_notify would replace it with a
	// zero-length plain notify.
	TArray<TSharedPtr<FJsonValue>> RemovedNotifies;
	int32 RemovedStateCount = 0;
	bool bFirstIsState = false;
	bool bFirstBranchingPoint = false;
	FString FirstName;
	FString FirstNotifyClass;
	FString FirstStateClass;
	double FirstTime = 0.0;
	double FirstDuration = 0.0;
	for (int32 i = AnimAsset->Notifies.Num() - 1; i >= 0; --i)
	{
		const FAnimNotifyEvent& E = AnimAsset->Notifies[i];
		const bool bNameMatches = NotifyName.IsEmpty() || E.NotifyName == NotifyFName;
		const bool bClassMatches = NotifyClassName.IsEmpty() ||
			(E.Notify && MatchClass && E.Notify->GetClass()->IsChildOf(MatchClass));
		if (bNameMatches && bClassMatches)
		{
			RemovedTimes.Add(MakeShared<FJsonValueNumber>(E.GetTime()));

			const bool bIsState = E.NotifyStateClass != nullptr;
			const bool bIsBranchingPoint = E.MontageTickType == EMontageNotifyTickType::BranchingPoint;
			if (bIsState) ++RemovedStateCount;

			TSharedPtr<FJsonObject> Removed = MakeShared<FJsonObject>();
			Removed->SetStringField(TEXT("kind"), bIsState ? TEXT("notifyState") : TEXT("notify"));
			Removed->SetStringField(TEXT("notifyName"), E.NotifyName.ToString());
			Removed->SetNumberField(TEXT("triggerTime"), E.GetTime());
			if (bIsState)
			{
				Removed->SetStringField(TEXT("notifyStateClass"), E.NotifyStateClass->GetClass()->GetName());
				Removed->SetNumberField(TEXT("duration"), E.GetDuration());
			}
			else if (E.Notify)
			{
				Removed->SetStringField(TEXT("notifyClass"), E.Notify->GetClass()->GetName());
			}
			Removed->SetBoolField(TEXT("branchingPoint"), bIsBranchingPoint);

			if (RemovedNotifies.Num() == 0)
			{
				bFirstIsState = bIsState;
				bFirstBranchingPoint = bIsBranchingPoint;
				FirstName = E.NotifyName.ToString();
				FirstTime = E.GetTime();
				if (bIsState)
				{
					FirstStateClass = E.NotifyStateClass->GetClass()->GetName();
					FirstDuration = E.GetDuration();
				}
				else if (E.Notify)
				{
					FirstNotifyClass = E.Notify->GetClass()->GetName();
				}
			}
			RemovedNotifies.Add(MakeShared<FJsonValueObject>(Removed));

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
	Result->SetArrayField(TEXT("removedNotifies"), RemovedNotifies);
	Result->SetNumberField(TEXT("removedNotifyStateCount"), RemovedStateCount);

	// Each add action puts back ONE entry, so the rollback replays the first
	// removed one and removedNotifies carries the rest, each tagged with the kind
	// that says which add action it needs. The kind decides the method here too:
	// replaying a removed notify STATE through add_anim_notify would swap a
	// windowed state for an instantaneous notify.
	if (bFirstIsState && FirstDuration <= 0.0)
	{
		// add_anim_notify_state refuses a duration of zero or less, so naming it
		// would hand the flow engine a step that errors.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The first removed entry is a notify state whose duration is not greater than zero, which add_anim_notify_state refuses, so no inverse call can be named for it. ")
			TEXT("removedNotifies lists every removed entry with its kind, class and duration."));
	}
	else
	{
		TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("assetPath"), AssetPath);
		Rollback->SetStringField(TEXT("notifyName"), FirstName);
		Rollback->SetNumberField(TEXT("triggerTime"), FirstTime);
		Rollback->SetBoolField(TEXT("branchingPoint"), bFirstBranchingPoint);
		if (bFirstIsState)
		{
			Rollback->SetStringField(TEXT("notifyStateClass"), FirstStateClass);
			Rollback->SetNumberField(TEXT("duration"), FirstDuration);
			MCPSetRollback(Result, TEXT("add_anim_notify_state"), Rollback);
		}
		else
		{
			if (!FirstNotifyClass.IsEmpty()) Rollback->SetStringField(TEXT("notifyClass"), FirstNotifyClass);
			MCPSetRollback(Result, TEXT("add_anim_notify"), Rollback);
		}
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The add action re-creates the entry of the same class at the same time (and, for a notify state, over the same duration) with DEFAULT property values, ")
			TEXT("so anything configured on the notify or notify-state object itself is not restored. ")
			TEXT("This call removed %d entr(ies), %d of them notify states, and the rollback replays only the first. ")
			TEXT("The rest are listed in removedNotifies, each with a 'kind' saying which action puts it back: animation(add_anim_notify) for 'notify', animation(add_anim_notify_state) for 'notifyState'."),
			RemovedTimes.Num(), RemovedStateCount));
	}
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
// The blendspace's sample list as it stands, in the shape populate_blendspace
// accepts for `samples`. A sample write is reversible because populate_blendspace
// clears and rebuilds the list, so the inverse is that action replaying what was
// captured here.
static TArray<TSharedPtr<FJsonValue>> CaptureBlendSpaceSamples(const UBlendSpace* BS, int32& OutDroppedSampleCount)
{
	OutDroppedSampleCount = 0;
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (const FBlendSample& S : BS->GetBlendSamples())
	{
		// A sample with no animation cannot be described to populate_blendspace,
		// which needs an animationPath, so it is counted rather than silently lost.
		if (!S.Animation) { ++OutDroppedSampleCount; continue; }
		TSharedPtr<FJsonObject> Sample = MakeShared<FJsonObject>();
		Sample->SetStringField(TEXT("animationPath"), S.Animation->GetPathName());
		Sample->SetNumberField(TEXT("x"), S.SampleValue.X);
		Sample->SetNumberField(TEXT("y"), S.SampleValue.Y);
		Samples.Add(MakeShared<FJsonValueObject>(Sample));
	}
	return Samples;
}

// The blendspace's axis parameters, in the shape populate_blendspace accepts for
// `axes`. A 1D blendspace only has axis 0, and its GetBlendParameter(1) is a stub
// the replay must not write through, so the array is sized to the asset.
static TArray<TSharedPtr<FJsonValue>> CaptureBlendSpaceAxes(const UBlendSpace* BS)
{
	TArray<TSharedPtr<FJsonValue>> Axes;
	const int32 AxisCount = BS->IsA<UBlendSpace1D>() ? 1 : 2;
	for (int32 i = 0; i < AxisCount; ++i)
	{
		const FBlendParameter& BP = BS->GetBlendParameter(i);
		TSharedPtr<FJsonObject> Axis = MakeShared<FJsonObject>();
		Axis->SetStringField(TEXT("name"), BP.DisplayName);
		Axis->SetNumberField(TEXT("min"), BP.Min);
		Axis->SetNumberField(TEXT("max"), BP.Max);
		Axis->SetNumberField(TEXT("gridNum"), BP.GridNum);
		Axes.Add(MakeShared<FJsonValueObject>(Axis));
	}
	return Axes;
}

// The rollback every blendspace sample write shares: populate_blendspace with the
// captured samples and axes and clearExisting, which rebuilds the list exactly.
// What it cannot carry is per-sample RateScale and single-frame blending, which
// populate_blendspace has no parameter for, so the record says so.
static void SetBlendSpaceRestoreRollback(
	TSharedPtr<FJsonObject> Result,
	const FString& AssetPath,
	TArray<TSharedPtr<FJsonValue>> PrevSamples,
	TArray<TSharedPtr<FJsonValue>> PrevAxes,
	int32 DroppedSampleCount)
{
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("assetPath"), AssetPath);
	Rollback->SetArrayField(TEXT("samples"), PrevSamples);
	Rollback->SetArrayField(TEXT("axes"), PrevAxes);
	Rollback->SetBoolField(TEXT("clearExisting"), true);
	MCPSetRollback(Result, TEXT("populate_blendspace"), Rollback);
	Result->SetNumberField(TEXT("rollbackDroppedSampleCount"), DroppedSampleCount);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("The replay clears the sample list and re-adds every captured sample at its captured position, and restores each axis name, min, max and grid divisions. ")
#if UE_MCP_HAS_5_8_API
		TEXT("Per-sample rateScale, single-frame blending and mirroring are not carried, because populate_blendspace has no parameter for them, so every restored sample comes back with the engine defaults for those. ")
#else
		TEXT("Per-sample rateScale and single-frame blending are not carried, because populate_blendspace has no parameter for them, so every restored sample comes back with the engine defaults for those. ")
#endif
		TEXT("%d sample(s) held no animation and cannot be described to populate_blendspace, so the replay does not bring them back."),
		DroppedSampleCount));
}

TSharedPtr<FJsonValue> FAnimationHandlers::PopulateBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBlendSpace* BS = LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BS) return MCPError(FString::Printf(TEXT("BlendSpace not found at '%s'"), *AssetPath));

	// Captured before any axis or sample is touched: this action's inverse is
	// itself, replaying the list and axes that were there.
	int32 DroppedSampleCount = 0;
	TArray<TSharedPtr<FJsonValue>> PrevSamples = CaptureBlendSpaceSamples(BS, DroppedSampleCount);
	TArray<TSharedPtr<FJsonValue>> PrevAxes = CaptureBlendSpaceAxes(BS);

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
	SetBlendSpaceRestoreRollback(Result, BS->GetPathName(), PrevSamples, PrevAxes, DroppedSampleCount);
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

	// Captured before the append. There is no remove-by-index action, so the
	// inverse of "append one sample" is "put the whole list back".
	int32 DroppedSampleCount = 0;
	TArray<TSharedPtr<FJsonValue>> PrevSamples = CaptureBlendSpaceSamples(BlendSpace, DroppedSampleCount);
	TArray<TSharedPtr<FJsonValue>> PrevAxes = CaptureBlendSpaceAxes(BlendSpace);

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
	SetBlendSpaceRestoreRollback(Result, BlendSpace->GetPathName(), PrevSamples, PrevAxes, DroppedSampleCount);
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
	// Copied by value, not held by reference: EditSampleValue rewrites the array
	// this reference points into. These two are exactly what the inverse call
	// needs, and this action is its own inverse.
	const FVector PrevPos = Existing.SampleValue;
	const FString PrevAnimPath = Existing.Animation ? Existing.Animation->GetPathName() : FString();

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
	Result->SetNumberField(TEXT("previousX"), PrevPos.X);
	Result->SetNumberField(TEXT("previousY"), PrevPos.Y);
	Result->SetStringField(TEXT("previousAnimation"), PrevAnimPath);

	// This action is its own inverse: the same sampleIndex with the position and
	// animation it held. The index is stable because nothing here adds or removes
	// a sample.
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("assetPath"), BlendSpace->GetPathName());
	Rollback->SetNumberField(TEXT("sampleIndex"), SampleIndex);
	Rollback->SetNumberField(TEXT("x"), PrevPos.X);
	Rollback->SetNumberField(TEXT("y"), PrevPos.Y);
	if (!PrevAnimPath.IsEmpty())
	{
		Rollback->SetStringField(TEXT("animation"), PrevAnimPath);
	}
	MCPSetRollback(Result, TEXT("set_blend_sample"), Rollback);

	// The one thing the replay cannot express: a sample that held NO animation.
	// set_blend_sample needs an animation path and has no form that clears one,
	// so omitting the field leaves whatever this call assigned in place.
	const bool bAssignedToEmptySample = PrevAnimPath.IsEmpty() && Updated.Animation != nullptr;
	Result->SetBoolField(TEXT("rollbackLossy"), bAssignedToEmptySample);
	if (bAssignedToEmptySample)
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The sample held no animation before this call. The replay restores its position, but set_blend_sample requires an animation path and cannot clear one, ")
			TEXT("so the animation this call assigned stays on the sample."));
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
// set_montage_sequence - Replace the animation sequence in a montage's slot track
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
	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	// Load the new sequence
	UAnimSequence* NewSequence = LoadAssetByPath<UAnimSequence>(AnimSequencePath);
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
	// What the affected segments held, recorded as each is overwritten. The
	// inverse can only name ONE animSequencePath, so a slot whose segments
	// referenced different sequences has no single call that restores it.
	TArray<TSharedPtr<FJsonValue>> PreviousSegments;
	FString PrevSharedAnimPath;
	bool bPrevPathsAgree = true;
	bool bPrevTrimmed = false;
	bool bChanged = false;
	for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
	{
		if (bHasSegmentIndex && SegIdx != SegmentIndex) continue;
		FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegIdx];

		const UAnimSequenceBase* PrevAnim = Segment.GetAnimReference().Get();
		const FString PrevPath = PrevAnim ? PrevAnim->GetPathName() : FString();
		const float PrevStart = Segment.AnimStartTime;
		const float PrevEnd = Segment.AnimEndTime;

		TSharedPtr<FJsonObject> Prev = MakeShared<FJsonObject>();
		Prev->SetNumberField(TEXT("segmentIndex"), SegIdx);
		Prev->SetStringField(TEXT("animSequencePath"), PrevPath);
		Prev->SetNumberField(TEXT("animStartTime"), PrevStart);
		Prev->SetNumberField(TEXT("animEndTime"), PrevEnd);
		PreviousSegments.Add(MakeShared<FJsonValueObject>(Prev));

		if (SegmentsUpdated == 0) PrevSharedAnimPath = PrevPath;
		else if (PrevPath != PrevSharedAnimPath) bPrevPathsAgree = false;
		if (PrevAnim && (!FMath::IsNearlyZero(PrevStart) || !FMath::IsNearlyEqual(PrevEnd, PrevAnim->GetPlayLength())))
		{
			bPrevTrimmed = true;
		}
		if (PrevAnim != NewSequence || !FMath::IsNearlyZero(PrevStart)
			|| !FMath::IsNearlyEqual(PrevEnd, NewSequence->GetPlayLength()))
		{
			bChanged = true;
		}

		Segment.SetAnimReference(NewSequence);
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = NewSequence->GetPlayLength();
		SegmentsUpdated++;
	}
	// True when the slot had no segments at all and this call authored one, which
	// is the only branch whose inverse is a removal rather than a replacement.
	const bool bCreatedSegment = (SegmentsUpdated == 0 && !bHasSegmentIndex);

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
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), !bChanged && !bCreatedSegment);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animSequencePath"), AnimSequencePath);
	Result->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());
	Result->SetNumberField(TEXT("segmentsUpdated"), SegmentsUpdated);
	if (bHasSegmentIndex) Result->SetNumberField(TEXT("segmentIndex"), SegmentIndex);
	Result->SetNumberField(TEXT("sequenceLength"), NewSequence->GetPlayLength());
	Result->SetNumberField(TEXT("montageLength"), NewTotalLength);
	Result->SetArrayField(TEXT("previousSegments"), PreviousSegments);

	if (bCreatedSegment)
	{
		// The slot was empty, so the inverse is removing the segment this call
		// authored, and it is the only one in the slot. Addressed by slotIndex,
		// because the forward call selected the track by index and slot NAMES are
		// not unique: remove_montage_segment prefers slotName and takes the FIRST
		// track carrying it, which is a different track whenever a montage repeats
		// a slot name.
		TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("assetPath"), AssetPath);
		Rollback->SetNumberField(TEXT("slotIndex"), TrackIdx);
		Rollback->SetNumberField(TEXT("segmentIndex"), 0);
		MCPSetRollback(Result, TEXT("remove_montage_segment"), Rollback);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else if (bPrevPathsAgree && !PrevSharedAnimPath.IsEmpty())
	{
		// This action is its own inverse when every segment it touched held the
		// same sequence: replay it with that path and the same selector.
		TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("assetPath"), AssetPath);
		Rollback->SetStringField(TEXT("animSequencePath"), PrevSharedAnimPath);
		Rollback->SetNumberField(TEXT("slotIndex"), TrackIdx);
		if (bHasSegmentIndex) Rollback->SetNumberField(TEXT("segmentIndex"), SegmentIndex);
		MCPSetRollback(Result, TEXT("set_montage_sequence"), Rollback);
		Result->SetBoolField(TEXT("rollbackLossy"), bPrevTrimmed);
		if (bPrevTrimmed)
		{
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The replay puts the previous sequence back but re-opens each segment to the sequence's full length, because set_montage_sequence always writes animStartTime 0 and animEndTime = play length. ")
				TEXT("The trim points the segments carried are listed in previousSegments; restore them with asset(set_property) on the slot's AnimSegments."));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), PrevSharedAnimPath.IsEmpty() && bPrevPathsAgree
			? TEXT("The affected segment carried no animation reference, and set_montage_sequence requires an animSequencePath and cannot clear one, so there is no call that restores an empty segment.")
			: TEXT("The slot's segments referenced different sequences and this whole-slot write pointed them all at one. set_montage_sequence names a single animSequencePath, so no one call restores them; previousSegments lists what each segment held, replayable one at a time with segmentIndex."));
	}

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_montage_properties - Set montage properties (duration, rate, blending)
// Params: assetPath, sequenceLength?, rateScale?, blendIn?, blendOut?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetMontageProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
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

	// sequenceLength - update via property reflection (SequenceLength is protected)
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

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
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

// ─── #826  montage segment authoring helpers ────────────────────────
//
// UAnimMontage::SlotAnimTracks carries no script-visible specifier, so the whole
// slot -> FAnimTrack -> FAnimSegment chain is unreachable from Python. Every
// multi-segment montage edit therefore has to be driven from here.

namespace MCPMontageSegments
{
	/** Resolve the slot a call targets. 'slotName' wins over 'slotIndex', and
	 *  omitting both targets slot 0, which is the one create_montage produces.
	 *  Never creates a slot: callers that may create do so after validating, so
	 *  a rejected call cannot leave an empty slot on the asset. */
	static FSlotAnimationTrack* FindSlot(
		UAnimMontage* Montage,
		const TSharedPtr<FJsonObject>& Params,
		int32& OutSlotIndex,
		FString& OutError)
	{
		const FString SlotName = OptionalString(Params, TEXT("slotName"));
		if (!SlotName.IsEmpty())
		{
			const FName Wanted(*SlotName);
			for (int32 Idx = 0; Idx < Montage->SlotAnimTracks.Num(); ++Idx)
			{
				if (Montage->SlotAnimTracks[Idx].SlotName == Wanted)
				{
					OutSlotIndex = Idx;
					return &Montage->SlotAnimTracks[Idx];
				}
			}

			TArray<FString> Known;
			for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
			{
				Known.Add(Track.SlotName.ToString());
			}
			OutError = FString::Printf(
				TEXT("Montage has no slot named '%s' (slots: %s)"),
				*SlotName,
				Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none"));
			return nullptr;
		}

		const int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);
		if (!Montage->SlotAnimTracks.IsValidIndex(SlotIndex))
		{
			OutError = FString::Printf(
				TEXT("slotIndex %d out of range (montage has %d slot(s))"),
				SlotIndex, Montage->SlotAnimTracks.Num());
			return nullptr;
		}
		OutSlotIndex = SlotIndex;
		return &Montage->SlotAnimTracks[SlotIndex];
	}

	/** Lay every slot's segments out back to back in array order, refresh the
	 *  sections and notifies that link to them, then write the montage length the
	 *  new layout implies. Without this pass an edited montage keeps its old
	 *  length and its section markers point at times that no longer exist, which
	 *  is what makes the asset unplayable past the first segment. */
	static float Relayout(UAnimMontage* Montage)
	{
		for (FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			float Cursor = 0.0f;
			for (FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				Segment.StartPos = Cursor;
				Cursor += Segment.GetLength();
			}
		}

		// Re-resolve section/notify links against the new segment positions.
		Montage->UpdateLinkableElements();

		const float NewLength = Montage->CalculateSequenceLength();

		// Pull anything now past the end back in before the length shrinks.
		for (FCompositeSection& Section : Montage->CompositeSections)
		{
			if (Section.GetTime() > NewLength) Section.SetTime(NewLength);
		}
		for (FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			if (Notify.GetTime() > NewLength) Notify.SetTime(NewLength);
		}

		Montage->SetCompositeLength(NewLength);

		Montage->CompositeSections.Sort([](const FCompositeSection& A, const FCompositeSection& B)
		{
			return A.GetTime() < B.GetTime();
		});
		UAnimMontageFactory::EnsureStartingSection(Montage);

		// Also refreshes the montage's common target frame rate, which follows
		// whatever the segments now reference.
		Montage->PostEditChange();

		// SetCompositeLength quantizes to whole frames, so report what the asset
		// actually ended up with rather than the raw sum.
		return Montage->GetPlayLength();
	}

	/** One segment as JSON. Field names match read_montage's segment shape so a
	 *  caller can read and address segments with the same vocabulary. */
	static TSharedPtr<FJsonObject> DescribeSegment(const FAnimSegment& Segment, int32 SegmentIndex)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("segmentIndex"), SegmentIndex);
		if (const UAnimSequenceBase* Anim = Segment.GetAnimReference().Get())
		{
			Obj->SetStringField(TEXT("animation"), Anim->GetPathName());
			Obj->SetStringField(TEXT("animationName"), Anim->GetName());
		}
		else
		{
			Obj->SetField(TEXT("animation"), MakeShared<FJsonValueNull>());
		}
		Obj->SetNumberField(TEXT("startPos"), Segment.AnimStartTime);
		Obj->SetNumberField(TEXT("endPos"), Segment.AnimEndTime);
		Obj->SetNumberField(TEXT("playRate"), Segment.AnimPlayRate);
		Obj->SetNumberField(TEXT("loopCount"), Segment.LoopingCount);
		Obj->SetNumberField(TEXT("trackStartPos"), Segment.StartPos);
		Obj->SetNumberField(TEXT("trackEndPos"), Segment.GetEndPos());
		Obj->SetNumberField(TEXT("length"), Segment.GetLength());
		return Obj;
	}
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

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	// #826: anchor the section to a specific segment. A bare startTime marker
	// drifts the moment a segment is inserted ahead of it; a linked section
	// follows its segment instead. Resolved before the idempotency check so a
	// bad slot or segment index is reported rather than silently skipped.
	const bool bHasSegmentIndex = Params->HasField(TEXT("segmentIndex"));
	int32 SlotIndex = 0;
	int32 SegmentIndex = INDEX_NONE;
	FString SlotNameUsed;
	if (bHasSegmentIndex)
	{
		FString SlotError;
		FSlotAnimationTrack* SlotTrack = MCPMontageSegments::FindSlot(Montage, Params, SlotIndex, SlotError);
		if (!SlotTrack) return MCPError(SlotError);

		SegmentIndex = OptionalInt(Params, TEXT("segmentIndex"), 0);
		if (!SlotTrack->AnimTrack.AnimSegments.IsValidIndex(SegmentIndex))
		{
			return MCPError(FString::Printf(
				TEXT("segmentIndex %d out of range (slot '%s' has %d segment(s))"),
				SegmentIndex,
				*SlotTrack->SlotName.ToString(),
				SlotTrack->AnimTrack.AnimSegments.Num()));
		}

		SlotNameUsed = SlotTrack->SlotName.ToString();
		StartTime = SlotTrack->AnimTrack.AnimSegments[SegmentIndex].StartPos;
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
	if (bHasSegmentIndex)
	{
		// Link resolves the segment from the time; pin the index explicitly so a
		// zero-length neighbour cannot claim the shared boundary, then refresh the
		// cached segment begin time and length from that index.
		NewSection.Link(Montage, static_cast<float>(StartTime), SlotIndex);
		NewSection.SetSegmentIndex(SegmentIndex);
		NewSection.Update();
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
	if (bHasSegmentIndex)
	{
		Result->SetNumberField(TEXT("segmentIndex"), SegmentIndex);
		Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
		Result->SetStringField(TEXT("slotName"), SlotNameUsed);
	}
	Result->SetNumberField(TEXT("totalSections"), Montage->CompositeSections.Num());
	// This branch only runs when the section did not exist, and the section is
	// appended without touching any other section's next-section link, so
	// removing it by name puts the montage back.
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("assetPath"), AssetPath);
	Rollback->SetStringField(TEXT("sectionName"), SectionName);
	MCPSetRollback(Result, TEXT("remove_montage_section"), Rollback);
	Result->SetBoolField(TEXT("rollbackLossy"), false);

	return MCPResult(Result);
}

// ─── #826  add_montage_segment ──────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::AddMontageSegment(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimSequencePath;
	if (auto Err = RequireString(Params, TEXT("animSequencePath"), AnimSequencePath)) return Err;

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	UAnimSequenceBase* Source = LoadAssetByPath<UAnimSequenceBase>(AnimSequencePath);
	if (!Source)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequenceBase at '%s'"), *AnimSequencePath));
	}
	if (Source == Montage)
	{
		return MCPError(TEXT("A montage cannot contain itself as a segment"));
	}
	if (!Source->CanBeUsedInComposition())
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a %s, which cannot be used as a montage segment (use an AnimSequence or AnimComposite)"),
			*AnimSequencePath, *Source->GetClass()->GetName()));
	}

	// A segment from a foreign skeleton evaluates to garbage at runtime instead
	// of failing loudly, so refuse it up front. Skeletons the montage's skeleton
	// declares compatible are accepted, matching what the editor allows.
	USkeleton* MontageSkeleton = Montage->GetSkeleton();
	USkeleton* SourceSkeleton = Source->GetSkeleton();
	if (MontageSkeleton && SourceSkeleton
		&& MontageSkeleton != SourceSkeleton
		&& !MontageSkeleton->IsCompatibleForEditor(SourceSkeleton))
	{
		return MCPError(FString::Printf(
			TEXT("Skeleton mismatch: montage '%s' uses '%s' but '%s' uses '%s', which is not listed as a compatible skeleton"),
			*Montage->GetName(), *MontageSkeleton->GetPathName(),
			*Source->GetName(), *SourceSkeleton->GetPathName()));
	}

	const float SourceLength = Source->GetPlayLength();
	if (SourceLength <= 0.0f)
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' has no playable length"), *AnimSequencePath));
	}

	const float StartPos = static_cast<float>(OptionalNumber(Params, TEXT("startPos"), 0.0));
	const float EndPos = static_cast<float>(OptionalNumber(Params, TEXT("endPos"), SourceLength));
	const float PlayRate = static_cast<float>(OptionalNumber(Params, TEXT("playRate"), 1.0));
	const int32 LoopCount = OptionalInt(Params, TEXT("loopCount"), 1);

	if (StartPos < 0.0f || EndPos > SourceLength + UE_KINDA_SMALL_NUMBER || StartPos >= EndPos)
	{
		return MCPError(FString::Printf(
			TEXT("Invalid trim: startPos %.4f and endPos %.4f must satisfy 0 <= startPos < endPos <= %.4f (play length of '%s')"),
			StartPos, EndPos, SourceLength, *Source->GetName()));
	}
	if (FMath::IsNearlyZero(PlayRate))
	{
		return MCPError(TEXT("'playRate' cannot be zero"));
	}
	if (LoopCount < 1)
	{
		return MCPError(TEXT("'loopCount' must be 1 or greater"));
	}

	// Resolve the slot without mutating: a later validation failure must not
	// leave a stray empty slot on the montage.
	const FString SlotNameParam = OptionalString(Params, TEXT("slotName"));
	int32 SlotIndex = INDEX_NONE;
	if (!SlotNameParam.IsEmpty())
	{
		const FName Wanted(*SlotNameParam);
		for (int32 Idx = 0; Idx < Montage->SlotAnimTracks.Num(); ++Idx)
		{
			if (Montage->SlotAnimTracks[Idx].SlotName == Wanted)
			{
				SlotIndex = Idx;
				break;
			}
		}
	}
	else
	{
		SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);
		if (!Montage->SlotAnimTracks.IsValidIndex(SlotIndex))
		{
			return MCPError(FString::Printf(
				TEXT("slotIndex %d out of range (montage has %d slot(s)); pass 'slotName' to author a new slot"),
				SlotIndex, Montage->SlotAnimTracks.Num()));
		}
	}

	const bool bCreateSlot = (SlotIndex == INDEX_NONE);
	const int32 SegmentCount = bCreateSlot
		? 0
		: Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Num();

	// Rejects a source whose additive type disagrees with what the track already
	// holds, which would otherwise blend incorrectly at runtime.
	if (!bCreateSlot)
	{
		FText AddReason;
		if (!Montage->SlotAnimTracks[SlotIndex].AnimTrack.IsValidToAdd(Source, &AddReason))
		{
			return MCPError(FString::Printf(
				TEXT("Cannot add '%s' to slot '%s': %s"),
				*AnimSequencePath,
				*Montage->SlotAnimTracks[SlotIndex].SlotName.ToString(),
				*AddReason.ToString()));
		}
	}

	const int32 InsertIndex = OptionalInt(Params, TEXT("insertIndex"), SegmentCount);
	if (InsertIndex < 0 || InsertIndex > SegmentCount)
	{
		return MCPError(FString::Printf(
			TEXT("insertIndex %d out of range (slot holds %d segment(s); %d appends)"),
			InsertIndex, SegmentCount, SegmentCount));
	}

	// Everything validated: mutate.
	if (bCreateSlot)
	{
		Montage->AddSlot(FName(*SlotNameParam));
		SlotIndex = Montage->SlotAnimTracks.Num() - 1;
	}

	FAnimSegment NewSegment;
	NewSegment.SetAnimReference(Source, /*bInitialize*/ true);
	NewSegment.AnimStartTime = StartPos;
	NewSegment.AnimEndTime = EndPos;
	NewSegment.AnimPlayRate = PlayRate;
	NewSegment.LoopingCount = LoopCount;

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIndex];
	SlotTrack.AnimTrack.AnimSegments.Insert(NewSegment, InsertIndex);

	const float MontageLength = MCPMontageSegments::Relayout(Montage);
	SaveAssetPackage(Montage);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animSequencePath"), AnimSequencePath);
	Result->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetBoolField(TEXT("slotCreated"), bCreateSlot);
	Result->SetNumberField(TEXT("segmentIndex"), InsertIndex);
	Result->SetNumberField(TEXT("segmentCount"), SlotTrack.AnimTrack.AnimSegments.Num());
	Result->SetObjectField(
		TEXT("segment"),
		MCPMontageSegments::DescribeSegment(SlotTrack.AnimTrack.AnimSegments[InsertIndex], InsertIndex));
	Result->SetNumberField(TEXT("montageLength"), MontageLength);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Payload->SetNumberField(TEXT("segmentIndex"), InsertIndex);
	MCPSetRollback(Result, TEXT("remove_montage_segment"), Payload);

	return MCPResult(Result);
}

// ─── #826  remove_montage_segment ───────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::RemoveMontageSegment(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	if (!Params->HasField(TEXT("segmentIndex")))
	{
		return MCPError(TEXT("Missing required parameter 'segmentIndex'"));
	}
	const int32 SegmentIndex = OptionalInt(Params, TEXT("segmentIndex"), 0);

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	int32 SlotIndex = 0;
	FString SlotError;
	FSlotAnimationTrack* SlotTrack = MCPMontageSegments::FindSlot(Montage, Params, SlotIndex, SlotError);
	if (!SlotTrack) return MCPError(SlotError);

	TArray<FAnimSegment>& Segments = SlotTrack->AnimTrack.AnimSegments;
	if (Segments.Num() == 0)
	{
		// Idempotent replay: an empty slot has nothing left to remove.
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("slotName"), SlotTrack->SlotName.ToString());
		Noop->SetNumberField(TEXT("slotIndex"), SlotIndex);
		Noop->SetNumberField(TEXT("segmentCount"), 0);
		return MCPResult(Noop);
	}
	if (!Segments.IsValidIndex(SegmentIndex))
	{
		return MCPError(FString::Printf(
			TEXT("segmentIndex %d out of range (slot '%s' has %d segment(s))"),
			SegmentIndex, *SlotTrack->SlotName.ToString(), Segments.Num()));
	}

	// Copy before removal so the rollback can rebuild the exact segment.
	const FAnimSegment Removed = Segments[SegmentIndex];
	Segments.RemoveAt(SegmentIndex);

	const float MontageLength = MCPMontageSegments::Relayout(Montage);
	SaveAssetPackage(Montage);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("slotName"), SlotTrack->SlotName.ToString());
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetNumberField(TEXT("segmentIndex"), SegmentIndex);
	Result->SetNumberField(TEXT("segmentCount"), Segments.Num());
	Result->SetObjectField(TEXT("removed"), MCPMontageSegments::DescribeSegment(Removed, SegmentIndex));
	Result->SetNumberField(TEXT("montageLength"), MontageLength);

	if (const UAnimSequenceBase* RemovedAnim = Removed.GetAnimReference().Get())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("animSequencePath"), RemovedAnim->GetPathName());
		Payload->SetStringField(TEXT("slotName"), SlotTrack->SlotName.ToString());
		Payload->SetNumberField(TEXT("insertIndex"), SegmentIndex);
		Payload->SetNumberField(TEXT("startPos"), Removed.AnimStartTime);
		Payload->SetNumberField(TEXT("endPos"), Removed.AnimEndTime);
		Payload->SetNumberField(TEXT("playRate"), Removed.AnimPlayRate);
		Payload->SetNumberField(TEXT("loopCount"), Removed.LoopingCount);
		MCPSetRollback(Result, TEXT("add_montage_segment"), Payload);
	}

	return MCPResult(Result);
}

// ─── #826  list_montage_segments ────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::ListMontageSegments(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

	const FString SlotFilter = OptionalString(Params, TEXT("slotName"));

	TArray<TSharedPtr<FJsonValue>> SlotsArray;
	int32 TotalSegments = 0;
	for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
	{
		const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIdx];
		if (!SlotFilter.IsEmpty() && SlotTrack.SlotName != FName(*SlotFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetNumberField(TEXT("slotIndex"), SlotIdx);
		SlotObj->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());
		SlotObj->SetNumberField(TEXT("trackLength"), SlotTrack.AnimTrack.GetLength());

		TArray<TSharedPtr<FJsonValue>> SegmentsArray;
		for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
		{
			SegmentsArray.Add(MakeShared<FJsonValueObject>(
				MCPMontageSegments::DescribeSegment(SlotTrack.AnimTrack.AnimSegments[SegIdx], SegIdx)));
		}
		TotalSegments += SegmentsArray.Num();
		SlotObj->SetNumberField(TEXT("segmentCount"), SegmentsArray.Num());
		SlotObj->SetArrayField(TEXT("segments"), SegmentsArray);
		SlotsArray.Add(MakeShared<FJsonValueObject>(SlotObj));
	}

	if (!SlotFilter.IsEmpty() && SlotsArray.Num() == 0)
	{
		TArray<FString> Known;
		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			Known.Add(Track.SlotName.ToString());
		}
		return MCPError(FString::Printf(
			TEXT("Montage has no slot named '%s' (slots: %s)"),
			*SlotFilter,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none")));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("montageLength"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("slotCount"), SlotsArray.Num());
	Result->SetNumberField(TEXT("totalSegments"), TotalSegments);
	Result->SetArrayField(TEXT("slots"), SlotsArray);

	// Sections, so a caller can see which markers already anchor to a segment.
	TArray<TSharedPtr<FJsonValue>> SectionsArray;
	for (int32 SectionIdx = 0; SectionIdx < Montage->CompositeSections.Num(); ++SectionIdx)
	{
		const FCompositeSection& Section = Montage->CompositeSections[SectionIdx];
		TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();
		SecObj->SetNumberField(TEXT("sectionIndex"), SectionIdx);
		SecObj->SetStringField(TEXT("sectionName"), Section.SectionName.ToString());
		SecObj->SetNumberField(TEXT("startTime"), Section.GetTime());
		SecObj->SetStringField(TEXT("nextSection"), Section.NextSectionName.ToString());
		SecObj->SetNumberField(TEXT("slotIndex"), Section.GetSlotIndex());
		SecObj->SetNumberField(TEXT("segmentIndex"), Section.GetSegmentIndex());
		SectionsArray.Add(MakeShared<FJsonValueObject>(SecObj));
	}
	Result->SetArrayField(TEXT("sections"), SectionsArray);

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAnimationHandlers::ListControlRigVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	// In UE 5.7, ControlRigBlueprint was removed - load as a generic UBlueprint
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
// v0.7.11 - Animation depth
// ===========================================================================

TSharedPtr<FJsonValue> FAnimationHandlers::SetRootMotionSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UAnimSequence* Seq = LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	// Every field this action can write, read before any of them is touched.
	// The action is its own inverse, so the rollback carries all four whether or
	// not the caller passed them.
	const bool bPrevEnableRootMotion = Seq->bEnableRootMotion;
	const bool bPrevForceRootLock = Seq->bForceRootLock;
	const bool bPrevUseNormalizedRootMotionScale = Seq->bUseNormalizedRootMotionScale;
	const ERootMotionRootLock::Type PrevRootLock = Seq->RootMotionRootLock;
	const FString PrevRootLockName =
		PrevRootLock == ERootMotionRootLock::RefPose ? TEXT("RefPose")
		: (PrevRootLock == ERootMotionRootLock::AnimFirstFrame ? TEXT("AnimFirstFrame") : TEXT("Zero"));

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
	Result->SetBoolField(TEXT("unchanged"),
		bPrevEnableRootMotion == Seq->bEnableRootMotion
		&& bPrevForceRootLock == Seq->bForceRootLock
		&& bPrevUseNormalizedRootMotionScale == Seq->bUseNormalizedRootMotionScale
		&& PrevRootLock == Seq->RootMotionRootLock);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("enableRootMotion"), Seq->bEnableRootMotion);
	Result->SetBoolField(TEXT("forceRootLock"), Seq->bForceRootLock);
	Result->SetBoolField(TEXT("previousEnableRootMotion"), bPrevEnableRootMotion);
	Result->SetBoolField(TEXT("previousForceRootLock"), bPrevForceRootLock);
	Result->SetBoolField(TEXT("previousUseNormalizedRootMotionScale"), bPrevUseNormalizedRootMotionScale);
	Result->SetStringField(TEXT("previousRootMotionRootLock"), PrevRootLockName);

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("assetPath"), AssetPath);
	Rollback->SetBoolField(TEXT("enableRootMotion"), bPrevEnableRootMotion);
	Rollback->SetBoolField(TEXT("forceRootLock"), bPrevForceRootLock);
	Rollback->SetBoolField(TEXT("useNormalizedRootMotionScale"), bPrevUseNormalizedRootMotionScale);
	Rollback->SetStringField(TEXT("rootMotionRootLock"), PrevRootLockName);
	MCPSetRollback(Result, TEXT("set_root_motion_settings"), Rollback);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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
	// The two bones the virtual bone spans. Captured here because the removal
	// destroys them, and they are what add_virtual_bone needs to re-create it.
	FString SourceBone, TargetBone;
	for (const FVirtualBone& VB : Skeleton->GetVirtualBones())
	{
		if (VB.VirtualBoneName == BoneFName)
		{
			bFound = true;
			SourceBone = VB.SourceBoneName.ToString();
			TargetBone = VB.TargetBoneName.ToString();
			break;
		}
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
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("removed"), BoneName);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("sourceBone"), SourceBone);
	Result->SetStringField(TEXT("targetBone"), TargetBone);

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Rollback->SetStringField(TEXT("sourceBone"), SourceBone);
	Rollback->SetStringField(TEXT("targetBone"), TargetBone);
	MCPSetRollback(Result, TEXT("add_virtual_bone"), Rollback);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("add_virtual_bone re-creates the bone from its source and target, and Unreal mints the name, so a virtual bone that had been renamed comes back under the generated name. ")
		TEXT("Anything that referenced the old name (sockets, curves, animation node bone references) still points at the name that was removed."));
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

	// The skeleton the Blueprint targeted, read before the assignment: it is the
	// one value an inverse call can put back.
	const USkeleton* PrevSkeleton = AnimBP->TargetSkeleton;
	const FString PrevSkeletonPath = PrevSkeleton ? PrevSkeleton->GetPathName() : FString();

	AnimBP->TargetSkeleton = Skeleton;
	AnimBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), PrevSkeleton == Skeleton);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("previousSkeletonPath"), PrevSkeletonPath);

	if (!PrevSkeletonPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("assetPath"), AssetPath);
		Rollback->SetStringField(TEXT("skeletonPath"), PrevSkeletonPath);
		MCPSetRollback(Result, TEXT("set_anim_blueprint_skeleton"), Rollback);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The Blueprint had no target skeleton before this call. set_anim_blueprint_skeleton requires a skeletonPath and cannot clear the assignment, so there is no call that returns it to having none."));
	}
	return MCPResult(Result);
}
