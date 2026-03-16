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
