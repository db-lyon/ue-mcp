#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMeshSocket.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
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

// Control Rig (#11) — ControlRigBlueprint removed in UE 5.7, use reflection
#include "ControlRig.h"

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
	Registry.RegisterHandler(TEXT("create_anim_blueprint"), &CreateAnimBlueprint);
	Registry.RegisterHandler(TEXT("create_montage"), &CreateMontage);
	Registry.RegisterHandler(TEXT("create_anim_montage"), &CreateMontage);  // alias used by TS tools
	Registry.RegisterHandler(TEXT("create_blendspace"), &CreateBlendspace);
	Registry.RegisterHandler(TEXT("read_blendspace"), &ReadBlendspace);
	Registry.RegisterHandler(TEXT("add_anim_notify"), &AddAnimNotify);
	Registry.RegisterHandler(TEXT("create_sequence"), &CreateSequence);
	Registry.RegisterHandler(TEXT("create_anim_sequence"), &CreateSequence);  // alias
	Registry.RegisterHandler(TEXT("set_bone_keyframes"), &SetBoneKeyframes);
	Registry.RegisterHandler(TEXT("get_bone_transforms"), &GetBoneTransforms);
	Registry.RegisterHandler(TEXT("set_montage_sequence"), &SetMontageSequence);
	Registry.RegisterHandler(TEXT("set_montage_properties"), &SetMontageProperties);

	// State machine authoring
	Registry.RegisterHandler(TEXT("create_state_machine"), &CreateStateMachine);
	Registry.RegisterHandler(TEXT("add_state"), &AddState);
	Registry.RegisterHandler(TEXT("add_transition"), &AddTransition);
	Registry.RegisterHandler(TEXT("set_state_animation"), &SetStateAnimation);
	Registry.RegisterHandler(TEXT("set_transition_blend"), &SetTransitionBlend);
	Registry.RegisterHandler(TEXT("read_state_machine"), &ReadStateMachine);

	// AnimGraph inspection (#23 / #91)
	Registry.RegisterHandler(TEXT("read_anim_graph"), &ReadAnimGraph);

	// Float curve authoring (#79 / #24)
	Registry.RegisterHandler(TEXT("add_curve"), &AddCurve);

	// Montage slot & section editing (#78, #27)
	Registry.RegisterHandler(TEXT("set_montage_slot"), &SetMontageSlot);
	Registry.RegisterHandler(TEXT("add_montage_section"), &AddMontageSection);

	// IK Rig (#93)
	Registry.RegisterHandler(TEXT("create_ik_rig"), &CreateIKRig);
	Registry.RegisterHandler(TEXT("read_ik_rig"), &ReadIKRig);

	// Control Rig (#11)
	Registry.RegisterHandler(TEXT("list_control_rig_variables"), &ListControlRigVariables);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListAnimAssets(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListSkeletalMeshes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), TEXT("SkeletalMesh has no Skeleton"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ListSockets(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), TEXT("SkeletalMesh has no Skeleton"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<TSharedPtr<FJsonValue>> SocketsArray;
	const TArray<USkeletalMeshSocket*>& Sockets = Skeleton->Sockets;
	for (const USkeletalMeshSocket* Socket : Sockets)
	{
		if (!Socket) continue;

		TSharedPtr<FJsonObject> SocketObj = MakeShared<FJsonObject>();
		SocketObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		SocketObj->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());

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
	}

	Result->SetArrayField(TEXT("sockets"), SocketsArray);
	Result->SetNumberField(TEXT("count"), SocketsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::GetPhysicsAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("SkeletalMesh has no PhysicsAsset"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// read_anim_blueprint
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(LoadedAsset);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimBlueprint at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// read_anim_montage
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimMontage(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// read_anim_sequence
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimSequence(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
	if (!AnimSeq)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), AnimSeq->GetName());
	Result->SetStringField(TEXT("class"), AnimSeq->GetClass()->GetName());

	// Sequence length
	Result->SetNumberField(TEXT("sequenceLength"), AnimSeq->GetPlayLength());

	// Rate scale
	Result->SetNumberField(TEXT("rateScale"), AnimSeq->RateScale);

	// Number of frames and sampling frame rate
	Result->SetNumberField(TEXT("numberOfFrames"), AnimSeq->GetNumberOfSampledKeys());
	double SamplingRate = AnimSeq->GetSamplingFrameRate().AsDecimal();
	Result->SetNumberField(TEXT("samplingFrameRate"), SamplingRate);

	// Skeleton
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (Skeleton)
	{
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	}
	else
	{
		Result->SetField(TEXT("skeleton"), MakeShared<FJsonValueNull>());
	}

	// Additive animation type
	Result->SetBoolField(TEXT("isAdditive"), AnimSeq->AdditiveAnimType != EAdditiveAnimationType::AAT_None);

	// Notifies
	TArray<TSharedPtr<FJsonValue>> NotifiesArray;
	for (const FAnimNotifyEvent& NotifyEvent : AnimSeq->Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObj = MakeShared<FJsonObject>();
		NotifyObj->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());
		NotifyObj->SetNumberField(TEXT("triggerTime"), NotifyEvent.GetTriggerTime());
		if (NotifyEvent.Notify)
		{
			NotifyObj->SetStringField(TEXT("class"), NotifyEvent.Notify->GetClass()->GetName());
		}
		NotifiesArray.Add(MakeShared<FJsonValueObject>(NotifyObj));
	}
	Result->SetArrayField(TEXT("notifies"), NotifiesArray);

	// Curve names
	TArray<TSharedPtr<FJsonValue>> CurvesArray;
	const TArray<FFloatCurve>& Curves = AnimSeq->GetCurveData().FloatCurves;
	for (const FFloatCurve& Curve : Curves)
	{
		CurvesArray.Add(MakeShared<FJsonValueString>(Curve.GetName().ToString()));
	}
	Result->SetArrayField(TEXT("curveNames"), CurvesArray);

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// create_anim_blueprint
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SkeletonPath;
	if (!Params->TryGetStringField(TEXT("skeletonPath"), SkeletonPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'skeletonPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Animations");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString ParentClassName;
	Params->TryGetStringField(TEXT("parentClass"), ParentClassName);

	// Load the skeleton
	UObject* SkeletonAsset = UEditorAssetLibrary::LoadAsset(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Delete existing asset if present
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	// Create the AnimBlueprint via factory
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;

	// Resolve parent class if specified, default to UAnimInstance
	if (!ParentClassName.IsEmpty())
	{
		UClass* FoundClass = FindFirstObject<UClass>(*ParentClassName);
		if (FoundClass && FoundClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			Factory->ParentClass = FoundClass;
		}
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create AnimBlueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), NewAsset->GetName());
	Result->SetStringField(TEXT("class"), NewAsset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// create_montage
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateMontage(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString AnimSequencePath;
	if (!Params->TryGetStringField(TEXT("animSequencePath"), AnimSequencePath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'animSequencePath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Animations");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Load the source anim sequence
	UObject* SourceAsset = UEditorAssetLibrary::LoadAsset(AnimSequencePath);
	UAnimSequence* SourceSequence = Cast<UAnimSequence>(SourceAsset);
	if (!SourceSequence)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AnimSequencePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Delete existing asset if present
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	// Create the montage via factory
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = SourceSequence->GetSkeleton();
	Factory->SourceAnimation = SourceSequence;

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UAnimMontage::StaticClass(), Factory);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create AnimMontage"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), NewAsset->GetName());
	Result->SetStringField(TEXT("class"), NewAsset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// read_blendspace
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlendSpace* BlendSpace = Cast<UBlendSpace>(LoadedAsset);
	if (!BlendSpace)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load BlendSpace at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// add_anim_notify
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::AddAnimNotify(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString NotifyName;
	if (!Params->TryGetStringField(TEXT("notifyName"), NotifyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'notifyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double TriggerTime = 0.0;
	if (!Params->TryGetNumberField(TEXT("triggerTime"), TriggerTime))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'triggerTime' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString NotifyClassName;
	Params->TryGetStringField(TEXT("notifyClass"), NotifyClassName);

	// Load the animation asset — could be a montage or a sequence
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(LoadedAsset);
	if (!AnimAsset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimSequenceBase at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Clamp trigger time to valid range
	float PlayLength = AnimAsset->GetPlayLength();
	float ClampedTime = FMath::Clamp(static_cast<float>(TriggerTime), 0.0f, PlayLength);

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
	}

	AnimAsset->SortNotifies();
	AnimAsset->PostEditChange();
	AnimAsset->MarkPackageDirty();

	// Save the asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("notifyName"), NotifyName);
	Result->SetNumberField(TEXT("triggerTime"), ClampedTime);
	if (NewNotify)
	{
		Result->SetStringField(TEXT("notifyClass"), NewNotify->GetClass()->GetName());
	}
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// create_blendspace
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SkeletonPath;
	if (!Params->TryGetStringField(TEXT("skeletonPath"), SkeletonPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'skeletonPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Animations");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString AxisHorizontal = TEXT("Speed");
	Params->TryGetStringField(TEXT("axisHorizontal"), AxisHorizontal);

	FString AxisVertical = TEXT("Direction");
	Params->TryGetStringField(TEXT("axisVertical"), AxisVertical);

	double HorizontalMin = 0.0;
	double HorizontalMax = 500.0;
	double VerticalMin = -180.0;
	double VerticalMax = 180.0;
	Params->TryGetNumberField(TEXT("horizontalMin"), HorizontalMin);
	Params->TryGetNumberField(TEXT("horizontalMax"), HorizontalMax);
	Params->TryGetNumberField(TEXT("verticalMin"), VerticalMin);
	Params->TryGetNumberField(TEXT("verticalMax"), VerticalMax);

	// Load the skeleton
	UObject* SkeletonAsset = UEditorAssetLibrary::LoadAsset(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Delete existing asset if present
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	// Create the BlendSpace via factory
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
	Factory->TargetSkeleton = Skeleton;

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UBlendSpace::StaticClass(), Factory);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create BlendSpace"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Configure axis settings on the newly created BlendSpace
	UBlendSpace* BlendSpace = Cast<UBlendSpace>(NewAsset);
	if (BlendSpace)
	{
		FBlendParameter& BlendParam0 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(0));
		BlendParam0.DisplayName = AxisHorizontal;
		BlendParam0.Min = HorizontalMin;
		BlendParam0.Max = HorizontalMax;

		FBlendParameter& BlendParam1 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(1));
		BlendParam1.DisplayName = AxisVertical;
		BlendParam1.Min = VerticalMin;
		BlendParam1.Max = VerticalMax;
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), NewAsset->GetName());
	Result->SetStringField(TEXT("class"), NewAsset->GetClass()->GetName());
	Result->SetStringField(TEXT("axisHorizontal"), AxisHorizontal);
	Result->SetStringField(TEXT("axisVertical"), AxisVertical);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// create_sequence — Create a blank AnimSequence on a skeleton
// Params: name, skeletonPath, packagePath?, numFrames?, frameRate?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateSequence(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SkeletonPath;
	if (!Params->TryGetStringField(TEXT("skeletonPath"), SkeletonPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'skeletonPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Animations");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	double FrameRate = 30.0;
	Params->TryGetNumberField(TEXT("frameRate"), FrameRate);

	double NumFrames = 30.0;
	Params->TryGetNumberField(TEXT("numFrames"), NumFrames);

	// Load the skeleton
	UObject* SkeletonAsset = UEditorAssetLibrary::LoadAsset(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		// Try loading as skeletal mesh and getting its skeleton
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkeletonAsset);
		if (SkelMesh)
		{
			Skeleton = SkelMesh->GetSkeleton();
		}
	}
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Delete existing asset if present
	FString FullAssetPath = PackagePath / Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	// Create the package
	FString PackageName = PackagePath / Name;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create package"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create the AnimSequence
	UAnimSequence* NewSeq = NewObject<UAnimSequence>(Package, *Name, RF_Public | RF_Standalone);
	if (!NewSeq)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create AnimSequence"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewSeq->SetSkeleton(Skeleton);

	// Set up frame count and duration via the data controller
	IAnimationDataController& Controller = NewSeq->GetController();

	FFrameRate DesiredFrameRate(static_cast<int32>(FrameRate), 1);
	int32 FrameCount = static_cast<int32>(NumFrames);

	// Initialize the data model first — required before any modifications
	Controller.InitializeModel();
	Controller.OpenBracket(NSLOCTEXT("MCP", "CreateSequence", "MCP Create Sequence"));
	Controller.SetFrameRate(DesiredFrameRate);
	Controller.SetNumberOfFrames(FrameCount);
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	// Clear any lingering transactions to prevent "transaction still pending" crashes
	// when users later interact with the asset in the editor (e.g. bake to control rig)
	GEditor->ResetTransaction(NSLOCTEXT("MCP", "CreateSequenceReset", "MCP Create Sequence Complete"));

	NewSeq->PostEditChange();
	NewSeq->MarkPackageDirty();

	// Save
	UEditorAssetLibrary::SaveAsset(FullAssetPath);

	Result->SetStringField(TEXT("path"), FullAssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("numFrames"), NumFrames);
	Result->SetNumberField(TEXT("frameRate"), FrameRate);
	Result->SetNumberField(TEXT("sequenceLength"), NewSeq->GetPlayLength());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// set_bone_keyframes — Set bone transform keyframes on an AnimSequence
// Params: assetPath, boneName, keyframes[]
//   Each keyframe: { frame, location?: {x,y,z}, rotation?: {x,y,z,w}, scale?: {x,y,z} }
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetBoneKeyframes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString BoneName;
	if (!Params->TryGetStringField(TEXT("boneName"), BoneName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'boneName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TArray<TSharedPtr<FJsonValue>>* KeyframesArray;
	if (!Params->TryGetArrayField(TEXT("keyframes"), KeyframesArray))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'keyframes' array parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load the anim sequence
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
	if (!AnimSeq)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Verify bone exists in skeleton
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), TEXT("AnimSequence has no Skeleton"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
	if (BoneIndex == INDEX_NONE)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Bone '%s' not found in skeleton"), *BoneName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "SetBoneKeyframes", "MCP Set Bone Keyframes"));

	// Ensure bone track exists — add it if not present
	const FName BoneFName(*BoneName);
	const IAnimationDataModel* DataModel = AnimSeq->GetDataModel();
	if (!DataModel->IsValidBoneTrackName(BoneFName))
	{
		Controller.AddBoneCurve(BoneFName);
	}

	// Get the reference pose transform for this bone as a default
	FTransform RefPose = RefSkeleton.GetRefBonePose()[BoneIndex];

	// Collect all keyframes into arrays, then call SetBoneTrackKeys once
	TArray<FVector> Locations;
	TArray<FQuat> Rotations;
	TArray<FVector> Scales;

	for (const TSharedPtr<FJsonValue>& KeyframeVal : *KeyframesArray)
	{
		const TSharedPtr<FJsonObject>* KeyframeObjPtr;
		if (!KeyframeVal->TryGetObject(KeyframeObjPtr)) continue;
		const TSharedPtr<FJsonObject>& KF = *KeyframeObjPtr;

		// Start with reference pose as defaults
		FVector Location = RefPose.GetLocation();
		FQuat Rotation = RefPose.GetRotation();
		FVector Scale = RefPose.GetScale3D();

		// Override with provided values
		const TSharedPtr<FJsonObject>* LocObj;
		if (KF->TryGetObjectField(TEXT("location"), LocObj))
		{
			(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
			(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
			(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
		}

		const TSharedPtr<FJsonObject>* RotObj;
		if (KF->TryGetObjectField(TEXT("rotation"), RotObj))
		{
			(*RotObj)->TryGetNumberField(TEXT("x"), Rotation.X);
			(*RotObj)->TryGetNumberField(TEXT("y"), Rotation.Y);
			(*RotObj)->TryGetNumberField(TEXT("z"), Rotation.Z);
			(*RotObj)->TryGetNumberField(TEXT("w"), Rotation.W);
		}

		const TSharedPtr<FJsonObject>* ScaleObj;
		if (KF->TryGetObjectField(TEXT("scale"), ScaleObj))
		{
			(*ScaleObj)->TryGetNumberField(TEXT("x"), Scale.X);
			(*ScaleObj)->TryGetNumberField(TEXT("y"), Scale.Y);
			(*ScaleObj)->TryGetNumberField(TEXT("z"), Scale.Z);
		}

		Locations.Add(Location);
		Rotations.Add(Rotation);
		Scales.Add(Scale);
	}

	// Set all keys at once
	int32 KeyframeCount = Locations.Num();
	if (KeyframeCount > 0)
	{
		Controller.SetBoneTrackKeys(BoneFName, Locations, Rotations, Scales);
	}

	Controller.CloseBracket(false);

	// Clear any lingering transactions to prevent "transaction still pending" crashes
	GEditor->ResetTransaction(NSLOCTEXT("MCP", "SetBoneKeyframesReset", "MCP Set Bone Keyframes Complete"));

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("boneName"), BoneName);
	Result->SetNumberField(TEXT("keyframesSet"), KeyframeCount);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// get_bone_transforms — Read reference pose transforms for specified bones
// Params: skeletonPath, boneNames[]? (if omitted, returns all bones)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::GetBoneTransforms(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("skeletonPath"), AssetPath)
		&& !Params->TryGetStringField(TEXT("assetPath"), AssetPath)
		&& !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'skeletonPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load skeleton (accept either USkeleton or USkeletalMesh)
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	USkeleton* Skeleton = Cast<USkeleton>(LoadedAsset);
	if (!Skeleton)
	{
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(LoadedAsset);
		if (SkelMesh) Skeleton = SkelMesh->GetSkeleton();
	}
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Skeleton from '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

	// Optional bone name filter
	TSet<FName> FilterBones;
	const TArray<TSharedPtr<FJsonValue>>* BoneNamesArray;
	if (Params->TryGetArrayField(TEXT("boneNames"), BoneNamesArray))
	{
		for (const TSharedPtr<FJsonValue>& Val : *BoneNamesArray)
		{
			FString BoneStr;
			if (Val->TryGetString(BoneStr))
			{
				FilterBones.Add(FName(*BoneStr));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> BonesArray;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		FName BoneName = RefSkeleton.GetBoneName(i);
		if (FilterBones.Num() > 0 && !FilterBones.Contains(BoneName)) continue;

		const FTransform& T = RefPose[i];

		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), BoneName.ToString());
		BoneObj->SetNumberField(TEXT("index"), i);
		BoneObj->SetNumberField(TEXT("parentIndex"), RefSkeleton.GetParentIndex(i));

		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), T.GetLocation().X);
		LocObj->SetNumberField(TEXT("y"), T.GetLocation().Y);
		LocObj->SetNumberField(TEXT("z"), T.GetLocation().Z);
		BoneObj->SetObjectField(TEXT("location"), LocObj);

		FQuat Q = T.GetRotation();
		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("x"), Q.X);
		RotObj->SetNumberField(TEXT("y"), Q.Y);
		RotObj->SetNumberField(TEXT("z"), Q.Z);
		RotObj->SetNumberField(TEXT("w"), Q.W);
		BoneObj->SetObjectField(TEXT("rotation"), RotObj);

		TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
		ScaleObj->SetNumberField(TEXT("x"), T.GetScale3D().X);
		ScaleObj->SetNumberField(TEXT("y"), T.GetScale3D().Y);
		ScaleObj->SetNumberField(TEXT("z"), T.GetScale3D().Z);
		BoneObj->SetObjectField(TEXT("scale"), ScaleObj);

		BonesArray.Add(MakeShared<FJsonValueObject>(BoneObj));
	}

	Result->SetArrayField(TEXT("bones"), BonesArray);
	Result->SetNumberField(TEXT("boneCount"), BonesArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// Helper: Set the protected SegmentLength property on an FAnimLinkableElement
// (e.g. FCompositeSection) via reflection.
// ---------------------------------------------------------------------------
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
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString AnimSequencePath;
	if (!Params->TryGetStringField(TEXT("animSequencePath"), AnimSequencePath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'animSequencePath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double SlotIndex = 0.0;
	Params->TryGetNumberField(TEXT("slotIndex"), SlotIndex);

	// Load the montage
	UObject* MontageAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(MontageAsset);
	if (!Montage)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load the new sequence
	UObject* SeqAsset = UEditorAssetLibrary::LoadAsset(AnimSequencePath);
	UAnimSequence* NewSequence = Cast<UAnimSequence>(SeqAsset);
	if (!NewSequence)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AnimSequencePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Access the slot tracks
	int32 TrackIdx = static_cast<int32>(SlotIndex);
	if (TrackIdx < 0 || TrackIdx >= Montage->SlotAnimTracks.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Slot track index %d out of range (montage has %d tracks)"), TrackIdx, Montage->SlotAnimTracks.Num()));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[TrackIdx];

	// Replace the animation in all segments of this track
	int32 SegmentsUpdated = 0;
	for (FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
	{
		Segment.SetAnimReference(NewSequence);
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = NewSequence->GetPlayLength();
		SegmentsUpdated++;
	}

	// If no segments exist, add one
	if (SegmentsUpdated == 0)
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

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animSequencePath"), AnimSequencePath);
	Result->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());
	Result->SetNumberField(TEXT("segmentsUpdated"), SegmentsUpdated);
	Result->SetNumberField(TEXT("sequenceLength"), NewSequence->GetPlayLength());
	Result->SetNumberField(TEXT("montageLength"), NewTotalLength);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// set_montage_properties — Set montage properties (duration, rate, blending)
// Params: assetPath, sequenceLength?, rateScale?, blendIn?, blendOut?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetMontageProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* MontageAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(MontageAsset);
	if (!Montage)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<FString> Modified;

	// sequenceLength — update via property reflection (SequenceLength is protected)
	double SeqLen;
	if (Params->TryGetNumberField(TEXT("sequenceLength"), SeqLen))
	{
		float NewLength = static_cast<float>(SeqLen);
		SetMontageSequenceLength(Montage, NewLength);

		// Also update composite sections' segment lengths to match
		for (FCompositeSection& Section : Montage->CompositeSections)
		{
			SetSegmentLength(Section, NewLength);
		}
		Modified.Add(TEXT("sequenceLength"));
	}

	// rateScale
	double RateScale;
	if (Params->TryGetNumberField(TEXT("rateScale"), RateScale))
	{
		Montage->RateScale = static_cast<float>(RateScale);
		Modified.Add(TEXT("rateScale"));
	}

	// blendIn
	double BlendIn;
	if (Params->TryGetNumberField(TEXT("blendIn"), BlendIn))
	{
		Montage->BlendIn.SetBlendTime(static_cast<float>(BlendIn));
		Modified.Add(TEXT("blendIn"));
	}

	// blendOut
	double BlendOut;
	if (Params->TryGetNumberField(TEXT("blendOut"), BlendOut))
	{
		Montage->BlendOut.SetBlendTime(static_cast<float>(BlendOut));
		Modified.Add(TEXT("blendOut"));
	}

	if (Modified.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("No properties to set. Provide at least one of: sequenceLength, rateScale, blendIn, blendOut"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	// Return current state
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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── State Machine Helpers ────────────────────────────────────────

static UAnimBlueprint* LoadAnimBP(const FString& Path)
{
	return LoadObject<UAnimBlueprint>(nullptr, *Path);
}

static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& Name)
{
	TArray<UEdGraph*> All;
	BP->GetAllGraphs(All);
	for (UEdGraph* G : All)
	{
		if (G && G->GetName() == Name) return G;
	}
	return nullptr;
}

// Find the SM container node (UAnimGraphNode_StateMachine) by its machine name
static UAnimGraphNode_StateMachine* FindStateMachineNode(UBlueprint* BP, const FString& MachineName)
{
	TArray<UEdGraph*> All;
	BP->GetAllGraphs(All);
	for (UEdGraph* G : All)
	{
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SM->EditorStateMachineGraph))
				{
					if (SMGraph->GetName() == MachineName || SM->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(MachineName))
					{
						return SM;
					}
				}
			}
		}
	}
	return nullptr;
}

// Find a state node by name within a state machine graph
static UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			if (State->GetStateName() == StateName)
			{
				return State;
			}
		}
	}
	return nullptr;
}

static void CompileAndSave(UBlueprint* BP)
{
	FKismetEditorUtilities::CompileBlueprint(BP);
	UPackage* Package = BP->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs);
	}
}

// ─── State Machine Handlers ──────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreateStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Name = TEXT("NewStateMachine");
	Params->TryGetStringField(TEXT("name"), Name);

	FString GraphName = TEXT("AnimGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEdGraph* TargetGraph = FindGraphByName(AnimBP, GraphName);
	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create the state machine container node in the AnimGraph
	UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(TargetGraph);
	TargetGraph->AddNode(SMNode, false, false);
	SMNode->CreateNewGuid();
	SMNode->PostPlacedNewNode();  // This creates the EditorStateMachineGraph sub-graph
	SMNode->AllocateDefaultPins();
	SMNode->NodePosX = 200;
	SMNode->NodePosY = 0;

	// Rename the state machine graph to the desired name
	if (SMNode->EditorStateMachineGraph)
	{
		SMNode->EditorStateMachineGraph->Rename(*Name);
	}

	CompileAndSave(AnimBP);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddState(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'stateMachineName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("stateName"), StateName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'stateName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		Result->SetStringField(TEXT("error"), TEXT("State machine has no editor graph"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Check for duplicate
	if (FindStateNode(SMGraph, StateName))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' already exists"), *StateName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create state node
	UAnimStateNode* NewState = NewObject<UAnimStateNode>(SMGraph);
	SMGraph->AddNode(NewState, false, false);
	NewState->CreateNewGuid();
	NewState->PostPlacedNewNode();
	NewState->AllocateDefaultPins();

	// Set the state name via the BoundGraph (the state's internal graph)
	if (NewState->BoundGraph)
	{
		NewState->BoundGraph->Rename(*StateName);
	}

	// Position states in a grid
	int32 StateCount = 0;
	for (UEdGraphNode* N : SMGraph->Nodes) { if (Cast<UAnimStateNode>(N)) StateCount++; }
	NewState->NodePosX = 300 + ((StateCount - 1) % 4) * 300;
	NewState->NodePosY = ((StateCount - 1) / 4) * 200;

	CompileAndSave(AnimBP);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddTransition(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'stateMachineName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString FromState, ToState;
	if (!Params->TryGetStringField(TEXT("fromState"), FromState))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'fromState'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	if (!Params->TryGetStringField(TEXT("toState"), ToState))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'toState'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	UAnimStateNode* From = FindStateNode(SMGraph, FromState);
	UAnimStateNode* To = FindStateNode(SMGraph, ToState);
	if (!From)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' not found"), *FromState));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	if (!To)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' not found"), *ToState));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create transition node
	UAnimStateTransitionNode* TransNode = NewObject<UAnimStateTransitionNode>(SMGraph);
	SMGraph->AddNode(TransNode, false, false);
	TransNode->CreateNewGuid();
	TransNode->PostPlacedNewNode();
	TransNode->AllocateDefaultPins();

	// Position between the two states
	TransNode->NodePosX = (From->NodePosX + To->NodePosX) / 2;
	TransNode->NodePosY = (From->NodePosY + To->NodePosY) / 2;

	// Wire: From output → Transition input, Transition output → To input
	UEdGraphPin* FromOut = From->GetOutputPin();
	UEdGraphPin* TransIn = TransNode->GetInputPin();
	UEdGraphPin* TransOut = TransNode->GetOutputPin();
	UEdGraphPin* ToIn = To->GetInputPin();

	if (FromOut && TransIn)
	{
		FromOut->MakeLinkTo(TransIn);
	}
	if (TransOut && ToIn)
	{
		TransOut->MakeLinkTo(ToIn);
	}

	CompileAndSave(AnimBP);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("fromState"), FromState);
	Result->SetStringField(TEXT("toState"), ToState);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::SetStateAnimation(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SMName, StateName, AnimAssetPath;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName) ||
		!Params->TryGetStringField(TEXT("stateName"), StateName) ||
		!Params->TryGetStringField(TEXT("animAssetPath"), AnimAssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing required params: stateMachineName, stateName, animAssetPath"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	UAnimStateNode* State = FindStateNode(SMGraph, StateName);
	if (!State)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State '%s' not found"), *StateName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load the animation asset
	UAnimationAsset* AnimAsset = LoadObject<UAnimationAsset>(nullptr, *AnimAssetPath);
	if (!AnimAsset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Animation asset not found: %s"), *AnimAssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get the state's bound graph (internal graph that plays the animation)
	UEdGraph* StateGraph = State->BoundGraph;
	if (!StateGraph)
	{
		Result->SetStringField(TEXT("error"), TEXT("State has no BoundGraph"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find or create the appropriate player node inside the state graph
	// Look for existing sequence player or blendspace player
	UAnimGraphNode_SequencePlayer* SeqPlayer = nullptr;
	UAnimGraphNode_BlendSpacePlayer* BSPlayer = nullptr;
	for (UEdGraphNode* Node : StateGraph->Nodes)
	{
		if (!SeqPlayer) SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node);
		if (!BSPlayer) BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node);
	}

	if (UAnimSequence* Seq = Cast<UAnimSequence>(AnimAsset))
	{
		if (!SeqPlayer)
		{
			SeqPlayer = NewObject<UAnimGraphNode_SequencePlayer>(StateGraph);
			StateGraph->AddNode(SeqPlayer, false, false);
			SeqPlayer->CreateNewGuid();
			SeqPlayer->PostPlacedNewNode();
			SeqPlayer->AllocateDefaultPins();
		}
		SeqPlayer->SetAnimationAsset(Seq);
	}
	else if (UBlendSpace* BS = Cast<UBlendSpace>(AnimAsset))
	{
		if (!BSPlayer)
		{
			BSPlayer = NewObject<UAnimGraphNode_BlendSpacePlayer>(StateGraph);
			StateGraph->AddNode(BSPlayer, false, false);
			BSPlayer->CreateNewGuid();
			BSPlayer->PostPlacedNewNode();
			BSPlayer->AllocateDefaultPins();
		}
		BSPlayer->SetAnimationAsset(BS);
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported animation asset type: %s"), *AnimAsset->GetClass()->GetName()));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	CompileAndSave(AnimBP);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetStringField(TEXT("animAssetPath"), AnimAssetPath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::SetTransitionBlend(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SMName, FromState, ToState;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName) ||
		!Params->TryGetStringField(TEXT("fromState"), FromState) ||
		!Params->TryGetStringField(TEXT("toState"), ToState))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing required params: stateMachineName, fromState, toState"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);

	// Find the transition between fromState and toState
	UAnimStateTransitionNode* TransNode = nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node))
		{
			UAnimStateNode* Prev = Cast<UAnimStateNode>(T->GetPreviousState());
			UAnimStateNode* Next = Cast<UAnimStateNode>(T->GetNextState());
			if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
			{
				TransNode = T;
				break;
			}
		}
	}

	if (!TransNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("No transition from '%s' to '%s'"), *FromState, *ToState));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set blend duration
	double BlendDuration = 0.2;
	if (Params->TryGetNumberField(TEXT("blendDuration"), BlendDuration))
	{
		TransNode->CrossfadeDuration = static_cast<float>(BlendDuration);
	}

	// Set blend logic (Standard vs Inertialization)
	FString BlendLogic;
	if (Params->TryGetStringField(TEXT("blendLogic"), BlendLogic))
	{
		if (BlendLogic.Equals(TEXT("Inertialization"), ESearchCase::IgnoreCase))
		{
			TransNode->BlendMode = EAlphaBlendOption::Linear;
			TransNode->LogicType = ETransitionLogicType::TLT_Inertialization;
		}
		else // Standard
		{
			TransNode->LogicType = ETransitionLogicType::TLT_StandardBlend;
		}
	}

	CompileAndSave(AnimBP);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("fromState"), FromState);
	Result->SetStringField(TEXT("toState"), ToState);
	Result->SetNumberField(TEXT("blendDuration"), BlendDuration);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ReadStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'stateMachineName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		Result->SetStringField(TEXT("error"), TEXT("State machine has no editor graph"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Enumerate states
	TArray<TSharedPtr<FJsonValue>> StatesArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
			StateObj->SetStringField(TEXT("name"), State->GetStateName());

			// Check for animation asset inside the state
			if (State->BoundGraph)
			{
				for (UEdGraphNode* Inner : State->BoundGraph->Nodes)
				{
					if (UAnimGraphNode_AssetPlayerBase* AssetNode = Cast<UAnimGraphNode_AssetPlayerBase>(Inner))
					{
						if (UAnimationAsset* Asset = AssetNode->GetAnimationAsset())
						{
							StateObj->SetStringField(TEXT("animAsset"), Asset->GetPathName());
						}
					}
				}
			}

			StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
		}
	}
	Result->SetArrayField(TEXT("states"), StatesArray);

	// Enumerate transitions
	TArray<TSharedPtr<FJsonValue>> TransArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node))
		{
			TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();

			UAnimStateNode* Prev = Cast<UAnimStateNode>(T->GetPreviousState());
			UAnimStateNode* Next = Cast<UAnimStateNode>(T->GetNextState());
			if (Prev) TransObj->SetStringField(TEXT("fromState"), Prev->GetStateName());
			if (Next) TransObj->SetStringField(TEXT("toState"), Next->GetStateName());

			TransObj->SetNumberField(TEXT("blendDuration"), T->CrossfadeDuration);
			TransObj->SetStringField(TEXT("logicType"),
				T->LogicType == ETransitionLogicType::TLT_Inertialization ? TEXT("Inertialization") : TEXT("Standard"));

			TransArray.Add(MakeShared<FJsonValueObject>(TransObj));
		}
	}
	Result->SetArrayField(TEXT("transitions"), TransArray);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #23 / #91  read_anim_graph ─────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString GraphName = TEXT("AnimGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEdGraph* TargetGraph = FindGraphByName(AnimBP, GraphName);
	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

		// ── Serialize editable properties via reflection ──
		TArray<TSharedPtr<FJsonValue>> PropsArray;
		UClass* NodeClass = Node->GetClass();
		for (TFieldIterator<FProperty> PropIt(NodeClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

			// Skip internal / noise properties
			FString PropName = Prop->GetName();
			if (PropName.StartsWith(TEXT("Node")) || PropName == TEXT("ErrorMsg") || PropName == TEXT("bHasCompilerMessage"))
				continue;

			TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), PropName);
			PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());

			// Attempt to export the value to a human-readable string
			FString ValueStr;
			const void* Container = Node;
			Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(Container), nullptr, nullptr, PPF_None);
			PropObj->SetStringField(TEXT("value"), ValueStr);

			PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
		}
		NodeObj->SetArrayField(TEXT("properties"), PropsArray);

		// ── Pins ──
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetNumberField(TEXT("nodeCount"), NodesArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #79 / #24  add_curve ───────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::AddCurve(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString CurveName;
	if (!Params->TryGetStringField(TEXT("curveName"), CurveName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'curveName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
	if (!AnimSeq)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), TEXT("AnimSequence has no Skeleton"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Build the curve identifier
	FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "AddCurve", "MCP Add Curve"));

	bool bAdded = Controller.AddCurve(CurveId, AACF_DefaultCurve);

	Controller.CloseBracket();

	if (!bAdded)
	{
		// Curve may already exist – not necessarily an error
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Curve '%s' may already exist"), *CurveName));
	}

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("curveName"), CurveName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #78  set_montage_slot ──────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::SetMontageSlot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SlotName;
	if (!Params->TryGetStringField(TEXT("slotName"), SlotName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'slotName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 TrackIndex = 0;
	Params->TryGetNumberField(TEXT("trackIndex"), TrackIndex);

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (TrackIndex < 0 || TrackIndex >= Montage->SlotAnimTracks.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("trackIndex %d out of range (0..%d)"), TrackIndex, Montage->SlotAnimTracks.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Montage->SlotAnimTracks[TrackIndex].SlotName = FName(*SlotName);

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("slotName"), SlotName);
	Result->SetNumberField(TEXT("trackIndex"), TrackIndex);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #27  add_montage_section ───────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::AddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SectionName;
	if (!Params->TryGetStringField(TEXT("sectionName"), SectionName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sectionName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double StartTime = 0.0;
	Params->TryGetNumberField(TEXT("startTime"), StartTime);

	FString LinkedSection;
	Params->TryGetStringField(TEXT("linkedSection"), LinkedSection);

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Check if section already exists
	int32 ExistingIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (ExistingIdx != INDEX_NONE)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Section '%s' already exists at index %d"), *SectionName, ExistingIdx));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sectionName"), SectionName);
	Result->SetNumberField(TEXT("startTime"), StartTime);
	if (!LinkedSection.IsEmpty())
	{
		Result->SetStringField(TEXT("linkedSection"), LinkedSection);
	}
	Result->SetNumberField(TEXT("totalSections"), Montage->CompositeSections.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #93  create_ik_rig ─────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreateIKRig(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SkeletalMeshPath;
	if (!Params->TryGetStringField(TEXT("skeletalMeshPath"), SkeletalMeshPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'skeletalMeshPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Load the skeletal mesh to get the skeleton
	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
	if (!SkelMesh)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *SkeletalMeshPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create the IKRigDefinition asset via AssetTools
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FindObject<UClass>(nullptr, TEXT("/Script/IKRigEditor.IKRigDefinitionFactory")));
	if (!Factory)
	{
		Result->SetStringField(TEXT("error"), TEXT("IKRigDefinitionFactory not found — is the IKRig plugin enabled?"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UIKRigDefinition::StaticClass(), Factory);
	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(NewAsset);
	if (!IKRig)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create IKRigDefinition asset"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set the preview skeletal mesh (this also sets up the skeleton internally)
	IKRig->SetPreviewMesh(SkelMesh, false);

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRig->GetPathName());

	Result->SetStringField(TEXT("assetPath"), IKRig->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("skeletalMeshPath"), SkeletalMeshPath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #93  read_ik_rig ───────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::ReadIKRig(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(LoadedAsset);
	if (!IKRig)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load IKRigDefinition at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), IKRig->GetName());

	// Preview mesh
	USkeletalMesh* PreviewMesh = IKRig->GetPreviewMesh();
	if (PreviewMesh)
	{
		Result->SetStringField(TEXT("previewMesh"), PreviewMesh->GetPathName());
	}

	// Skeleton
	const FIKRigSkeleton& RigSkeleton = IKRig->GetSkeleton();
	TArray<TSharedPtr<FJsonValue>> BonesArray;
	for (int32 i = 0; i < RigSkeleton.BoneNames.Num(); ++i)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), RigSkeleton.BoneNames[i].ToString());
		BoneObj->SetNumberField(TEXT("index"), i);
		if (i < RigSkeleton.ParentIndices.Num())
		{
			BoneObj->SetNumberField(TEXT("parentIndex"), RigSkeleton.ParentIndices[i]);
		}
		BonesArray.Add(MakeShared<FJsonValueObject>(BoneObj));
	}
	Result->SetArrayField(TEXT("bones"), BonesArray);

	// Retarget chains
	const TArray<FBoneChain>& Chains = IKRig->GetRetargetChains();
	TArray<TSharedPtr<FJsonValue>> ChainsArray;
	for (const FBoneChain& Chain : Chains)
	{
		TSharedPtr<FJsonObject> ChainObj = MakeShared<FJsonObject>();
		ChainObj->SetStringField(TEXT("name"), Chain.ChainName.ToString());
		ChainObj->SetStringField(TEXT("startBone"), Chain.StartBone.BoneName.ToString());
		ChainObj->SetStringField(TEXT("endBone"), Chain.EndBone.BoneName.ToString());
		ChainsArray.Add(MakeShared<FJsonValueObject>(ChainObj));
	}
	Result->SetArrayField(TEXT("retargetChains"), ChainsArray);

	// Solvers — enumerate via reflection since GetSolverArray not available in all UE versions
	TArray<TSharedPtr<FJsonValue>> SolversArray;
	FProperty* SolversProp = IKRig->GetClass()->FindPropertyByName(TEXT("Solvers"));
	if (SolversProp)
	{
		FString SolversStr;
		const void* ValPtr = SolversProp->ContainerPtrToValuePtr<void>(IKRig);
		SolversProp->ExportText_Direct(SolversStr, ValPtr, ValPtr, IKRig, PPF_None);
		if (!SolversStr.IsEmpty())
		{
			TSharedPtr<FJsonObject> SolverInfo = MakeShared<FJsonObject>();
			SolverInfo->SetStringField(TEXT("raw"), SolversStr);
			SolversArray.Add(MakeShared<FJsonValueObject>(SolverInfo));
		}
	}
	Result->SetArrayField(TEXT("solvers"), SolversArray);

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ─── #11  list_control_rig_variables ────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::ListControlRigVariables(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// In UE 5.7, ControlRigBlueprint was removed — load as a generic UBlueprint
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlueprint* CRBlueprint = Cast<UBlueprint>(LoadedAsset);
	if (!CRBlueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
