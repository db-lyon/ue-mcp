// Split from AnimationHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAnimationHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AnimationHandlers.cpp::RegisterHandlers.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "Curves/RichCurve.h"
#include "AnimationModifier.h"
#include "AnimationModifiersAssetUserData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/MorphTarget.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


// ---------------------------------------------------------------------------
// read_anim_sequence
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
	if (!AnimSeq)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

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

	// #432: explicit per-sequence QA fields. Mirror the property names the
	// agent-feedback issue requested so callers don't have to derive them.
	auto AdditiveTypeName = [&]() -> const TCHAR* {
		switch (AnimSeq->AdditiveAnimType)
		{
		case EAdditiveAnimationType::AAT_None:                 return TEXT("None");
		case EAdditiveAnimationType::AAT_LocalSpaceBase:       return TEXT("LocalSpaceBase");
		case EAdditiveAnimationType::AAT_RotationOffsetMeshSpace: return TEXT("RotationOffsetMeshSpace");
		default: return TEXT("Unknown");
		}
	};
	Result->SetStringField(TEXT("additiveType"), AdditiveTypeName());
	Result->SetNumberField(TEXT("lengthSeconds"), AnimSeq->GetPlayLength());
	Result->SetNumberField(TEXT("numFrames"), AnimSeq->GetNumberOfSampledKeys());
	Result->SetNumberField(TEXT("frameRate"), SamplingRate);
	Result->SetBoolField(TEXT("rootMotionEnabled"), AnimSeq->bEnableRootMotion);
	Result->SetStringField(TEXT("rootMotionRootLock"),
		AnimSeq->RootMotionRootLock == ERootMotionRootLock::RefPose ? TEXT("RefPose")
		: AnimSeq->RootMotionRootLock == ERootMotionRootLock::AnimFirstFrame ? TEXT("AnimFirstFrame")
		: TEXT("Zero"));
	Result->SetBoolField(TEXT("forceRootLock"), AnimSeq->bForceRootLock);
	Result->SetBoolField(TEXT("useNormalizedRootMotionScale"), AnimSeq->bUseNormalizedRootMotionScale);
	Result->SetStringField(TEXT("targetSkeletonPath"), Skeleton ? Skeleton->GetPathName() : FString());
	Result->SetNumberField(TEXT("boneCount"), Skeleton ? Skeleton->GetReferenceSkeleton().GetNum() : 0);
	Result->SetBoolField(TEXT("hasNotifies"), AnimSeq->Notifies.Num() > 0);
	Result->SetBoolField(TEXT("hasCurves"), AnimSeq->GetCurveData().FloatCurves.Num() > 0);

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

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// scan_animation_tracks
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ScanAnimationTracks(const TSharedPtr<FJsonObject>& Params)
{
	const int32 TargetTrackCount = OptionalInt(Params, TEXT("targetTrackCount"), 0);
	const bool bIncludeTrackNames = OptionalBool(Params, TEXT("includeTrackNames"), false);
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	// #672: default to ALL content roots (empty prefix) so AnimSequences under
	// a mounted non-/Game plugin root are discovered. Pass an explicit
	// directory (e.g. "/Game" or "/MyPlugin") to narrow the scan.
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT(""));
	const FString SkeletonFilter = OptionalString(Params, TEXT("skeletonPath"));

	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("assetPaths"), PathsArray))
	{
		for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
		{
			FString Path;
			if (PathValue.IsValid() && PathValue->TryGetString(Path) && !Path.IsEmpty())
			{
				AssetPaths.Add(Path);
			}
		}
	}
	else
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimSequence")), AssetDataList, true);
		for (const FAssetData& AssetData : AssetDataList)
		{
			if (!Directory.IsEmpty() && !AssetData.PackageName.ToString().StartsWith(Directory))
			{
				continue;
			}
			if (!bRecursive && AssetData.PackagePath.ToString() != Directory)
			{
				continue;
			}
			AssetPaths.Add(AssetData.GetObjectPathString());
		}
	}

	TArray<TSharedPtr<FJsonValue>> Sequences;
	int32 ProblemCount = 0;
	int32 InspectedCount = 0;
	int32 FailureCount = 0;

	for (const FString& AssetPath : AssetPaths)
	{
		UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
		UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
		if (!AnimSeq)
		{
			++FailureCount;
			continue;
		}

		const IAnimationDataModel* DataModel = AnimSeq->GetDataModel();
		if (!DataModel)
		{
			++FailureCount;
			continue;
		}

		USkeleton* Skeleton = AnimSeq->GetSkeleton();
		const FString SequenceSkeletonPath = Skeleton ? Skeleton->GetPathName() : FString();
		if (!SkeletonFilter.IsEmpty() && SequenceSkeletonPath != SkeletonFilter)
		{
			continue;
		}

		++InspectedCount;
		TArray<FName> BoneTrackNames;
		DataModel->GetBoneTrackNames(BoneTrackNames);
		const int32 TrackCount = BoneTrackNames.Num();
		const bool bOverTarget = TargetTrackCount > 0 && TrackCount > TargetTrackCount;
		if (bOverTarget)
		{
			++ProblemCount;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AnimSeq->GetPathName());
		Entry->SetStringField(TEXT("packagePath"), AnimSeq->GetOutermost()->GetName());
		Entry->SetStringField(TEXT("name"), AnimSeq->GetName());
		Entry->SetStringField(TEXT("skeleton"), SequenceSkeletonPath);
		Entry->SetNumberField(TEXT("numBoneTracks"), TrackCount);
		Entry->SetBoolField(TEXT("overTarget"), bOverTarget);

		if (bIncludeTrackNames)
		{
			TArray<TSharedPtr<FJsonValue>> TrackArray;
			for (const FName& TrackName : BoneTrackNames)
			{
				TrackArray.Add(MakeShared<FJsonValueString>(TrackName.ToString()));
			}
			Entry->SetArrayField(TEXT("boneTrackNames"), TrackArray);
		}

		if (TargetTrackCount > 0)
		{
			TArray<TSharedPtr<FJsonValue>> OverflowArray;
			for (int32 Index = TargetTrackCount; Index < BoneTrackNames.Num(); ++Index)
			{
				OverflowArray.Add(MakeShared<FJsonValueString>(BoneTrackNames[Index].ToString()));
			}
			Entry->SetArrayField(TEXT("overflowTrackNames"), OverflowArray);
		}

		if (bOverTarget || TargetTrackCount <= 0)
		{
			Sequences.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("targetTrackCount"), TargetTrackCount);
	Result->SetNumberField(TEXT("inspectedCount"), InspectedCount);
	Result->SetNumberField(TEXT("problemCount"), ProblemCount);
	Result->SetNumberField(TEXT("failureCount"), FailureCount);
	Result->SetArrayField(TEXT("sequences"), Sequences);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// create_anim_blueprint
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// create_sequence — Create a blank AnimSequence on a skeleton
// Params: name, skeletonPath, packagePath?, numFrames?, frameRate?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::CreateSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Animations"));
	double FrameRate = OptionalNumber(Params, TEXT("frameRate"), 30.0);
	double NumFrames = OptionalNumber(Params, TEXT("numFrames"), 30.0);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UObject* SkeletonAsset = UEditorAssetLibrary::LoadAsset(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkeletonAsset);
		if (SkelMesh)
		{
			Skeleton = SkelMesh->GetSkeleton();
		}
	}
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
	}

	auto Created = MCPCreateAssetIdempotentNewObject<UAnimSequence>(Name, PackagePath, OnConflict, TEXT("AnimSequence"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UAnimSequence* NewSeq = Created.Asset;
	const FString FullAssetPath = NewSeq->GetPathName();

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

	UEditorAssetLibrary::SaveAsset(FullAssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), FullAssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("numFrames"), NumFrames);
	Result->SetNumberField(TEXT("frameRate"), FrameRate);
	Result->SetNumberField(TEXT("sequenceLength"), NewSeq->GetPlayLength());
	MCPSetDeleteAssetRollback(Result, NewSeq->GetPathName());

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_bone_keyframes — Set bone transform keyframes on an AnimSequence
// Params: assetPath, boneName, keyframes[]
//   Each keyframe: { frame, location?: {x,y,z}, rotation?: {x,y,z,w}, scale?: {x,y,z} }
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// set_bone_keyframes — Set bone transform keyframes on an AnimSequence
// Params: assetPath, boneName, keyframes[]
//   Each keyframe: { frame, location?: {x,y,z}, rotation?: {x,y,z,w}, scale?: {x,y,z} }
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::SetBoneKeyframes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString BoneName;
	if (auto Err = RequireString(Params, TEXT("boneName"), BoneName)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* KeyframesArray;
	if (!Params->TryGetArrayField(TEXT("keyframes"), KeyframesArray))
	{
		return MCPError(TEXT("Missing 'keyframes' array parameter"));
	}

	// Load the anim sequence
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
	if (!AnimSeq)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
	}

	// Verify bone exists in skeleton
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton)
	{
		return MCPError(TEXT("AnimSequence has no Skeleton"));
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
	if (BoneIndex == INDEX_NONE)
	{
		return MCPError(FString::Printf(TEXT("Bone '%s' not found in skeleton"), *BoneName));
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

		// Override with provided values. Rotation is a quaternion {x,y,z,w}
		// here (not pitch/yaw/roll), so leave it as inline TryGet calls.
		Location = OptionalVec3(KF, TEXT("location"), Location);
		Scale = OptionalVec3(KF, TEXT("scale"), Scale);

		const TSharedPtr<FJsonObject>* RotObj;
		if (KF->TryGetObjectField(TEXT("rotation"), RotObj))
		{
			(*RotObj)->TryGetNumberField(TEXT("x"), Rotation.X);
			(*RotObj)->TryGetNumberField(TEXT("y"), Rotation.Y);
			(*RotObj)->TryGetNumberField(TEXT("z"), Rotation.Z);
			(*RotObj)->TryGetNumberField(TEXT("w"), Rotation.W);
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

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("boneName"), BoneName);
	Result->SetNumberField(TEXT("keyframesSet"), KeyframeCount);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// bake_keyframes_batch — write per-bone keyframe arrays for many bones into an
// AnimSequence in one call. (#540) Replaces N round-trips of set_bone_keyframes
// and the silent-T-pose failure mode: set_bone_track_keys returns false when the
// track does not yet exist, so this auto-AddBoneCurve's each track first, wraps
// the whole batch in one open/close bracket, and raises if any bone's write
// fails instead of reporting a hollow success.
// Params: assetPath, tracks: [{bone, keyframes: [{location,rotation{x,y,z,w},scale?}]}], save? (default true)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::BakeKeyframesBatch(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
	if (!Params->TryGetArrayField(TEXT("tracks"), Tracks) || !Tracks)
	{
		return MCPError(TEXT("Missing 'tracks' array parameter ([{bone, keyframes:[...]}, ...])"));
	}

	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!AnimSeq) return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton) return MCPError(TEXT("AnimSequence has no Skeleton"));
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "BakeKeyframesBatch", "MCP Bake Keyframes Batch"));

	TArray<TSharedPtr<FJsonValue>> PerBone;
	FString FailErr;
	int32 BonesBaked = 0;

	for (const TSharedPtr<FJsonValue>& TrackVal : *Tracks)
	{
		const TSharedPtr<FJsonObject>* TrackObjPtr = nullptr;
		if (!TrackVal->TryGetObject(TrackObjPtr) || !TrackObjPtr) continue;
		const TSharedPtr<FJsonObject>& Track = *TrackObjPtr;

		FString BoneName;
		if (!Track->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			FailErr = TEXT("a track is missing its 'bone' name");
			break;
		}
		const FName BoneFName(*BoneName);
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneFName);
		if (BoneIndex == INDEX_NONE)
		{
			FailErr = FString::Printf(TEXT("bone '%s' not found in skeleton"), *BoneName);
			break;
		}

		const TArray<TSharedPtr<FJsonValue>>* KeyframesArray = nullptr;
		if (!Track->TryGetArrayField(TEXT("keyframes"), KeyframesArray) || !KeyframesArray)
		{
			FailErr = FString::Printf(TEXT("bone '%s' has no 'keyframes' array"), *BoneName);
			break;
		}

		// Critical: create the bone track before writing keys, otherwise
		// SetBoneTrackKeys returns false and the asset stays a T-pose.
		const IAnimationDataModel* DataModel = AnimSeq->GetDataModel();
		if (!DataModel->IsValidBoneTrackName(BoneFName))
		{
			Controller.AddBoneCurve(BoneFName);
		}

		const FTransform RefPose = RefSkeleton.GetRefBonePose()[BoneIndex];
		TArray<FVector> Locations;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
		for (const TSharedPtr<FJsonValue>& KeyframeVal : *KeyframesArray)
		{
			const TSharedPtr<FJsonObject>* KFPtr = nullptr;
			if (!KeyframeVal->TryGetObject(KFPtr) || !KFPtr) continue;
			const TSharedPtr<FJsonObject>& KF = *KFPtr;

			FVector Location = OptionalVec3(KF, TEXT("location"), RefPose.GetLocation());
			FVector Scale = OptionalVec3(KF, TEXT("scale"), RefPose.GetScale3D());
			FQuat Rotation = RefPose.GetRotation();
			const TSharedPtr<FJsonObject>* RotObj = nullptr;
			if (KF->TryGetObjectField(TEXT("rotation"), RotObj))
			{
				(*RotObj)->TryGetNumberField(TEXT("x"), Rotation.X);
				(*RotObj)->TryGetNumberField(TEXT("y"), Rotation.Y);
				(*RotObj)->TryGetNumberField(TEXT("z"), Rotation.Z);
				(*RotObj)->TryGetNumberField(TEXT("w"), Rotation.W);
			}
			Locations.Add(Location);
			Rotations.Add(Rotation);
			Scales.Add(Scale);
		}

		if (Locations.Num() == 0)
		{
			FailErr = FString::Printf(TEXT("bone '%s' had no valid keyframes"), *BoneName);
			break;
		}

		const bool bOk = Controller.SetBoneTrackKeys(BoneFName, Locations, Rotations, Scales);
		if (!bOk)
		{
			FailErr = FString::Printf(TEXT("SetBoneTrackKeys failed for bone '%s' (%d keys)"), *BoneName, Locations.Num());
			break;
		}

		TSharedPtr<FJsonObject> BoneRes = MakeShared<FJsonObject>();
		BoneRes->SetStringField(TEXT("bone"), BoneName);
		BoneRes->SetNumberField(TEXT("keyframes"), Locations.Num());
		PerBone.Add(MakeShared<FJsonValueObject>(BoneRes));
		++BonesBaked;
	}

	Controller.CloseBracket(false);
	GEditor->ResetTransaction(NSLOCTEXT("MCP", "BakeKeyframesBatchReset", "MCP Bake Keyframes Batch Complete"));

	if (!FailErr.IsEmpty())
	{
		// The bracket is closed; surface the failure rather than a hollow success.
		return MCPError(FString::Printf(TEXT("bake_keyframes_batch failed after %d bone(s): %s"), BonesBaked, *FailErr));
	}

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();
	if (bSave) UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("bonesBaked"), BonesBaked);
	Result->SetArrayField(TEXT("tracks"), PerBone);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_bone_transforms — Read reference pose transforms for specified bones
// Params: skeletonPath, boneNames[]? (if omitted, returns all bones)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::GetBoneTransforms(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("skeletonPath"), AssetPath)
		&& !Params->TryGetStringField(TEXT("assetPath"), AssetPath)
		&& !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		return MCPError(TEXT("Missing 'skeletonPath' parameter"));
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
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton from '%s'"), *AssetPath));
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

	// #245: optional space="component" composes parent transforms so callers
	// can do retarget-chain / anatomical-scale checks without standing up a
	// transient SkeletalMeshActor and walking sockets.
	const FString Space = OptionalString(Params, TEXT("space"), TEXT("local")).ToLower();
	const bool bComponentSpace = (Space == TEXT("component") || Space == TEXT("world"));

	TArray<FTransform> ComponentSpacePose;
	if (bComponentSpace)
	{
		ComponentSpacePose.SetNum(RefSkeleton.GetNum());
		for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
		{
			const int32 ParentIdx = RefSkeleton.GetParentIndex(i);
			ComponentSpacePose[i] = (ParentIdx >= 0)
				? RefPose[i] * ComponentSpacePose[ParentIdx]
				: RefPose[i];
		}
	}

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

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("space"), bComponentSpace ? TEXT("component") : TEXT("local"));

	TArray<TSharedPtr<FJsonValue>> BonesArray;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		FName BoneName = RefSkeleton.GetBoneName(i);
		if (FilterBones.Num() > 0 && !FilterBones.Contains(BoneName)) continue;

		const FTransform& T = bComponentSpace ? ComponentSpacePose[i] : RefPose[i];

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

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Helper: Set the protected SegmentLength property on an FAnimLinkableElement
// (e.g. FCompositeSection) via reflection.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonValue> FAnimationHandlers::AddCurve(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString CurveName;
	if (auto Err = RequireString(Params, TEXT("curveName"), CurveName)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset);
	if (!AnimSeq)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
	}

	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton)
	{
		return MCPError(TEXT("AnimSequence has no Skeleton"));
	}

	// Build the curve identifier
	FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "AddCurve", "MCP Add Curve"));

	bool bAdded = Controller.AddCurve(CurveId, AACF_DefaultCurve);

	Controller.CloseBracket();

	auto Result = MCPSuccess();

	if (!bAdded)
	{
		// Curve already exists — idempotent replay
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("curveName"), CurveName);
		return MCPResult(Result);
	}

	MCPSetCreated(Result);

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("curveName"), CurveName);
	// No rollback: no paired remove_curve handler.

	return MCPResult(Result);
}

// ─── #712 set_anim_curve_keys ───────────────────────────────────────
// add_curve only creates an empty named curve; it cannot set key VALUES.
// This adds the curve if missing, then replaces its keys with the provided
// [{time,value,interp?}] list. Enables authoring distance / speed / any float
// curve directly, without dropping to an AnimationModifier or Python.
static ERichCurveInterpMode ParseRichCurveInterp(const FString& S, ERichCurveInterpMode Default)
{
	const FString L = S.ToLower();
	if (L == TEXT("constant") || L == TEXT("step"))   return RCIM_Constant;
	if (L == TEXT("linear"))                          return RCIM_Linear;
	if (L == TEXT("cubic") || L == TEXT("auto"))      return RCIM_Cubic;
	return Default;
}

TSharedPtr<FJsonValue> FAnimationHandlers::SetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString CurveName;
	if (auto Err = RequireString(Params, TEXT("curveName"), CurveName)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* KeysArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("keys"), KeysArr) || !KeysArr)
	{
		return MCPError(TEXT("Missing 'keys' array parameter (each entry: {time, value, interp?})"));
	}

	UAnimSequence* Seq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));
	if (!Seq->GetSkeleton()) return MCPError(TEXT("AnimSequence has no Skeleton"));

	const ERichCurveInterpMode DefaultInterp = ParseRichCurveInterp(
		OptionalString(Params, TEXT("interpolation"), TEXT("linear")), RCIM_Linear);

	TArray<FRichCurveKey> Keys;
	Keys.Reserve(KeysArr->Num());
	for (const TSharedPtr<FJsonValue>& V : *KeysArr)
	{
		const TSharedPtr<FJsonObject>* KObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(KObj) || !KObj || !(*KObj).IsValid()) continue;
		double Time = 0.0, Val = 0.0;
		if (!(*KObj)->TryGetNumberField(TEXT("time"), Time)) continue;
		if (!(*KObj)->TryGetNumberField(TEXT("value"), Val)) continue;
		FRichCurveKey Key((float)Time, (float)Val);
		FString KInterp;
		Key.InterpMode = (*KObj)->TryGetStringField(TEXT("interp"), KInterp)
			? ParseRichCurveInterp(KInterp, DefaultInterp) : DefaultInterp;
		Keys.Add(Key);
	}
	if (Keys.Num() == 0)
	{
		return MCPError(TEXT("No valid keys parsed from 'keys' (each entry needs numeric time+value)"));
	}
	// Keep keys time-ordered so the curve evaluates predictably.
	Keys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });

	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);

	IAnimationDataController& Controller = Seq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "SetAnimCurveKeys", "MCP Set Anim Curve Keys"));

	const bool bAdded = Controller.AddCurve(CurveId, AACF_DefaultCurve);
	const bool bSet = Controller.SetCurveKeys(CurveId, Keys);

	Controller.CloseBracket();

	if (!bSet)
	{
		return MCPError(FString::Printf(TEXT("Failed to set keys on curve '%s'"), *CurveName));
	}

	Seq->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Seq, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("curveName"), CurveName);
	Result->SetBoolField(TEXT("curveCreated"), bAdded);
	Result->SetNumberField(TEXT("keyCount"), Keys.Num());
	return MCPResult(Result);
}

// ─── #712 apply_animation_modifier ──────────────────────────────────
// Instantiate a UAnimationModifier subclass and run it on an AnimSequence.
// The headline case is DistanceCurveModifier (bakes a Distance curve from root
// motion for distance matching), but any modifier class works. The concrete
// modifier class is resolved by name at runtime so the plugin providing it does
// not have to be a link-time dependency; if it can't be found we tell the caller
// which plugin to enable rather than failing opaquely.
static UClass* ResolveAnimationModifierClass(const FString& NameOrPath, UClass* BaseClass, FString& OutHint)
{
	// Explicit object/class path.
	if (NameOrPath.Contains(TEXT("/")) || NameOrPath.Contains(TEXT(".")))
	{
		if (UClass* C = LoadClass<UObject>(nullptr, *NameOrPath)) return C;
		const FString WithSuffix = NameOrPath.EndsWith(TEXT("_C")) ? NameOrPath : NameOrPath + TEXT("_C");
		if (UClass* C = LoadClass<UObject>(nullptr, *WithSuffix)) return C;
	}

	// Bare class name (tolerate a leading 'U').
	FString Short = NameOrPath;
	Short.RemoveFromStart(TEXT("U"));

	// Script packages that ship stock modifiers. DistanceCurveModifier lives in the
	// AnimationLocomotionLibrary plugin (module AnimationLocomotionLibraryEditor),
	// which is NOT enabled by default.
	static const TCHAR* ScriptPackages[] = {
		TEXT("/Script/AnimationLocomotionLibraryEditor."),
		TEXT("/Script/AnimationModifierLibrary."),
		TEXT("/Script/AnimationModifiers."),
	};
	for (const TCHAR* Pkg : ScriptPackages)
	{
		if (UClass* C = FindObject<UClass>(nullptr, *(FString(Pkg) + Short))) return C;
	}

	// Global fallback: any loaded UClass with this name.
	if (UClass* C = FindFirstObject<UClass>(*Short, EFindFirstObjectOptions::NativeFirst)) return C;

	if (Short.Equals(TEXT("DistanceCurveModifier"), ESearchCase::IgnoreCase))
	{
		OutHint = TEXT(" - enable the 'Animation Locomotion Library' plugin (it is off by default) and restart the editor");
	}
	return nullptr;
}

TSharedPtr<FJsonValue> FAnimationHandlers::ApplyAnimationModifier(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString ModifierClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("modifierClass"), TEXT("modifier"), ModifierClassName)) return Err;

	UAnimSequence* Seq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	FString Hint;
	UClass* ModClass = ResolveAnimationModifierClass(ModifierClassName, UAnimationModifier::StaticClass(), Hint);
	if (!ModClass)
	{
		return MCPError(FString::Printf(TEXT("Animation modifier class not found: '%s'%s"), *ModifierClassName, *Hint));
	}
	if (!ModClass->IsChildOf(UAnimationModifier::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a UAnimationModifier subclass"), *ModClass->GetName()));
	}
	if (ModClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return MCPError(FString::Printf(TEXT("'%s' is abstract and cannot be applied"), *ModClass->GetName()));
	}

	// Register the modifier on the sequence (creating the asset-user-data + instance
	// if needed) so it shows up in list_anim_modifiers and re-applies on reimport.
	// Reuse an existing instance of the same class for idempotent replay.
	UAnimationModifiersAssetUserData* UserData = Seq->GetAssetUserData<UAnimationModifiersAssetUserData>();
	UAnimationModifier* Instance = nullptr;
	if (UserData)
	{
		for (UAnimationModifier* M : UserData->GetAnimationModifierInstances())
		{
			if (M && M->GetClass() == ModClass) { Instance = M; break; }
		}
	}
	bool bRegistered = false;
	if (!Instance)
	{
		UAnimationModifiersAssetUserData::AddAnimationModifierOfClass(Seq, ModClass);
		bRegistered = true;
		UserData = Seq->GetAssetUserData<UAnimationModifiersAssetUserData>();
		if (UserData)
		{
			for (UAnimationModifier* M : UserData->GetAnimationModifierInstances())
			{
				if (M && M->GetClass() == ModClass) { Instance = M; }
			}
		}
	}
	if (!Instance)
	{
		// Fall back to a transient instance so the bake still happens even if the
		// user-data registration path is unavailable.
		Instance = NewObject<UAnimationModifier>(Seq, ModClass);
	}

	// Apply caller-provided settings (CurveName, Axis, SampleRate, ...).
	TArray<FString> AppliedProps;
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("props"), PropsObj) && PropsObj && (*PropsObj).IsValid())
	{
		Instance->Modify();
		for (const auto& Pair : (*PropsObj)->Values)
		{
			const FString PropName(*Pair.Key);
			FProperty* Prop = ModClass->FindPropertyByName(FName(*PropName));
			if (!Prop)
			{
				return MCPError(FString::Printf(TEXT("Modifier '%s' has no property '%s'"), *ModClass->GetName(), *PropName));
			}
			void* Addr = Prop->ContainerPtrToValuePtr<void>(Instance);
			FString E;
			if (!MCPJsonProperty::SetJsonOnProperty(Prop, Addr, Pair.Value, E))
			{
				return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropName, *E));
			}
			AppliedProps.Add(PropName);
		}
		Instance->PostEditChange();
	}

	// Bake. ApplyToAnimationSequence reverts any prior application then runs OnApply.
	Instance->ApplyToAnimationSequence(Seq);

	Seq->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Seq, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("modifierClass"), ModClass->GetName());
	Result->SetBoolField(TEXT("registered"), bRegistered);
	TArray<TSharedPtr<FJsonValue>> PropArr;
	for (const FString& P : AppliedProps) PropArr.Add(MakeShared<FJsonValueString>(P));
	Result->SetArrayField(TEXT("appliedProps"), PropArr);
	return MCPResult(Result);
}

// ─── #78  set_montage_slot ──────────────────────────────────────────


TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimComposite(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Animations"));

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	auto Created = MCPCreateAssetIdempotentNewObject<UAnimComposite>(Name, PackagePath, OptionalString(Params, TEXT("onConflict"), TEXT("skip")), TEXT("AnimComposite"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UAnimComposite* Composite = Created.Asset;
	Composite->SetSkeleton(Skeleton);
	UEditorAssetLibrary::SaveLoadedAsset(Composite);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Composite->GetPathName());
	MCPSetDeleteAssetRollback(Result, Composite->GetPathName());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::ListAnimModifiers(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UAnimSequence* Seq = LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> Arr;
	TSet<UObject*> Seen;
	auto AddModifier = [&Arr, &Seen](UObject* Modifier)
	{
		if (!Modifier || Seen.Contains(Modifier)) return;
		Seen.Add(Modifier);
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("class"), Modifier->GetClass()->GetName());
		M->SetStringField(TEXT("classPath"), Modifier->GetClass()->GetPathName());
		M->SetStringField(TEXT("name"), Modifier->GetName());
		Arr.Add(MakeShared<FJsonValueObject>(M));
	};

	// The modern store is UAnimationModifiersAssetUserData::AnimationModifierInstances -
	// what the Animation Data Modifiers window shows and what apply_animation_modifier
	// registers into (#712).
	if (UAnimationModifiersAssetUserData* UserData = Seq->GetAssetUserData<UAnimationModifiersAssetUserData>())
	{
		for (UAnimationModifier* M : UserData->GetAnimationModifierInstances()) AddModifier(M);
	}

	// AppliedAnimationModifiers is an editor-only TArray<UAnimationModifier*> on the
	// AnimSequence (legacy store). Enumerate it via reflection (portable across module
	// linkage): each element is an instanced UAnimationModifier subobject.
	FArrayProperty* ModifiersProp = CastField<FArrayProperty>(
		Seq->GetClass()->FindPropertyByName(TEXT("AppliedAnimationModifiers")));
	if (ModifiersProp)
	{
		FObjectPropertyBase* ElemProp = CastField<FObjectPropertyBase>(ModifiersProp->Inner);
		FScriptArrayHelper Helper(ModifiersProp, ModifiersProp->ContainerPtrToValuePtr<void>(Seq));
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			UObject* Modifier = ElemProp ? ElemProp->GetObjectPropertyValue(Helper.GetRawPtr(i)) : nullptr;
			AddModifier(Modifier);
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("modifiers"), Arr);
	Result->SetNumberField(TEXT("count"), Arr.Num());
	return MCPResult(Result);
}


// ─── #112 read_bone_track ─────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::ReadBoneTrack(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString BoneName;
	if (auto Err = RequireString(Params, TEXT("boneName"), BoneName)) return Err;

	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	const IAnimationDataModel* DataModel = Seq->GetDataModel();
	if (!DataModel) return MCPError(TEXT("Sequence has no data model"));

	const int32 NumFrames = DataModel->GetNumberOfFrames();
	const double FrameRate = DataModel->GetFrameRate().AsDecimal();

	// Frame selection
	TArray<int32> FramesToSample;
	const TArray<TSharedPtr<FJsonValue>>* FramesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("frames"), FramesArr))
	{
		for (const auto& V : *FramesArr)
		{
			double N = 0;
			if (V.IsValid() && V->TryGetNumber(N))
			{
				FramesToSample.Add(FMath::Clamp((int32)N, 0, NumFrames));
			}
		}
	}
	else
	{
		FramesToSample.Add(0);
		FramesToSample.Add(NumFrames / 2);
		FramesToSample.Add(NumFrames);
	}

	FName BoneFName(*BoneName);

	TArray<TSharedPtr<FJsonValue>> SamplesArr;
	for (int32 Frame : FramesToSample)
	{
		FTransform Xf = DataModel->EvaluateBoneTrackTransform(BoneFName, DataModel->GetFrameRate().AsFrameTime((double)Frame / FrameRate), EAnimInterpolationType::Linear);
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("frame"), Frame);
		FVector Loc = Xf.GetLocation();
		TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetNumberField(TEXT("x"), Loc.X); L->SetNumberField(TEXT("y"), Loc.Y); L->SetNumberField(TEXT("z"), Loc.Z);
		S->SetObjectField(TEXT("location"), L);
		FRotator R = Xf.Rotator();
		TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
		RO->SetNumberField(TEXT("pitch"), R.Pitch); RO->SetNumberField(TEXT("yaw"), R.Yaw); RO->SetNumberField(TEXT("roll"), R.Roll);
		S->SetObjectField(TEXT("rotation"), RO);
		FVector Sc = Xf.GetScale3D();
		TSharedPtr<FJsonObject> SO = MakeShared<FJsonObject>();
		SO->SetNumberField(TEXT("x"), Sc.X); SO->SetNumberField(TEXT("y"), Sc.Y); SO->SetNumberField(TEXT("z"), Sc.Z);
		S->SetObjectField(TEXT("scale"), SO);
		SamplesArr.Add(MakeShared<FJsonValueObject>(S));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("boneName"), BoneName);
	Result->SetNumberField(TEXT("numFrames"), NumFrames);
	Result->SetNumberField(TEXT("frameRate"), FrameRate);
	Result->SetArrayField(TEXT("samples"), SamplesArr);
	return MCPResult(Result);
}

// ===========================================================================
// v1.0.0-rc.2 — animation authoring gaps
// ===========================================================================

// #153: batch-set properties on AnimSequence assets, optionally resolving
// montage inputs to their first underlying sequence. Saves each mutated
// sequence; returns per-path results so callers can diagnose mixed outcomes.


// ===========================================================================
// v1.0.0-rc.2 — animation authoring gaps
// ===========================================================================

// #153: batch-set properties on AnimSequence assets, optionally resolving
// montage inputs to their first underlying sequence. Saves each mutated
// sequence; returns per-path results so callers can diagnose mixed outcomes.
TSharedPtr<FJsonValue> FAnimationHandlers::SetSequenceProperties(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("assetPaths"), PathsArr))
	{
		return MCPError(TEXT("Missing 'assetPaths' array parameter"));
	}

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj || !(*PropsObj).IsValid())
	{
		return MCPError(TEXT("Missing 'properties' object parameter"));
	}
	const TSharedPtr<FJsonObject>& Props = *PropsObj;

	const bool bResolveMontages = OptionalBool(Params, TEXT("resolveFromMontages"), true);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 UpdatedCount = 0;
	int32 SkippedCount = 0;

	for (const TSharedPtr<FJsonValue>& PathVal : *PathsArr)
	{
		FString Path;
		if (!PathVal.IsValid() || !PathVal->TryGetString(Path)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), Path);

		UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
		UAnimSequence* Seq = Cast<UAnimSequence>(Loaded);
		FString ResolvedPath = Path;
		if (!Seq && bResolveMontages)
		{
			if (UAnimMontage* Montage = Cast<UAnimMontage>(Loaded))
			{
				UAnimSequenceBase* FirstRef = Montage->GetFirstAnimReference();
				Seq = Cast<UAnimSequence>(FirstRef);
				if (Seq)
				{
					ResolvedPath = Seq->GetPathName();
					Entry->SetStringField(TEXT("resolvedFromMontage"), Path);
					Entry->SetStringField(TEXT("assetPath"), ResolvedPath);
				}
			}
		}

		if (!Seq)
		{
			Entry->SetStringField(TEXT("status"), TEXT("skipped"));
			Entry->SetStringField(TEXT("reason"), TEXT("not an AnimSequence (or no resolvable sequence from montage)"));
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			++SkippedCount;
			continue;
		}

		Seq->Modify();

		bool EnableRootMotion;
		if (Props->TryGetBoolField(TEXT("enableRootMotion"), EnableRootMotion))
		{
			Seq->bEnableRootMotion = EnableRootMotion;
		}
		bool ForceRootLock;
		if (Props->TryGetBoolField(TEXT("forceRootLock"), ForceRootLock))
		{
			Seq->bForceRootLock = ForceRootLock;
		}
		bool UseNormalizedRootMotionScale;
		if (Props->TryGetBoolField(TEXT("useNormalizedRootMotionScale"), UseNormalizedRootMotionScale))
		{
			Seq->bUseNormalizedRootMotionScale = UseNormalizedRootMotionScale;
		}
		FString RootMotionMode;
		if (Props->TryGetStringField(TEXT("rootMotionRootLock"), RootMotionMode))
		{
			if      (RootMotionMode.Equals(TEXT("RefPose"),        ESearchCase::IgnoreCase)) Seq->RootMotionRootLock = ERootMotionRootLock::RefPose;
			else if (RootMotionMode.Equals(TEXT("AnimFirstFrame"), ESearchCase::IgnoreCase)) Seq->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
			else if (RootMotionMode.Equals(TEXT("Zero"),           ESearchCase::IgnoreCase)) Seq->RootMotionRootLock = ERootMotionRootLock::Zero;
		}

		Seq->PostEditChange();
		UEditorAssetLibrary::SaveLoadedAsset(Seq, /*bOnlyIfIsDirty=*/false);

		Entry->SetStringField(TEXT("status"), TEXT("updated"));
		Entry->SetBoolField(TEXT("enableRootMotion"), Seq->bEnableRootMotion);
		Entry->SetBoolField(TEXT("forceRootLock"), Seq->bForceRootLock);
		Results.Add(MakeShared<FJsonValueObject>(Entry));
		++UpdatedCount;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("updated"), UpdatedCount);
	Result->SetNumberField(TEXT("skipped"), SkippedCount);
	Result->SetArrayField(TEXT("results"), Results);
	return MCPResult(Result);
}

// #154: bake delta translation from a source bone (e.g. pelvis) onto the root
// bone across the full sequence, compensating the source bone so world-space
// position is unchanged. Default bakes X/Y (horizontal); Z is typically left
// on the source bone for gravity. Linear interpolation from frame 0 delta.


// #154: bake delta translation from a source bone (e.g. pelvis) onto the root
// bone across the full sequence, compensating the source bone so world-space
// position is unchanged. Default bakes X/Y (horizontal); Z is typically left
// on the source bone for gravity. Linear interpolation from frame 0 delta.
TSharedPtr<FJsonValue> FAnimationHandlers::BakeRootMotionFromBone(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SourceBoneName;
	if (auto Err = RequireString(Params, TEXT("sourceBone"), SourceBoneName)) return Err;

	const FString RootBoneName = OptionalString(Params, TEXT("rootBone"), TEXT("root"));
	const FString InterpMode = OptionalString(Params, TEXT("interpolation"), TEXT("linear"));

	bool bBakeX = true, bBakeY = true, bBakeZ = false;
	const TArray<TSharedPtr<FJsonValue>>* AxesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("axes"), AxesArr))
	{
		bBakeX = bBakeY = bBakeZ = false;
		for (const TSharedPtr<FJsonValue>& V : *AxesArr)
		{
			FString Ax; if (V.IsValid() && V->TryGetString(Ax))
			{
				Ax = Ax.ToLower();
				if (Ax == TEXT("x")) bBakeX = true;
				else if (Ax == TEXT("y")) bBakeY = true;
				else if (Ax == TEXT("z")) bBakeZ = true;
			}
		}
	}

	UAnimSequence* Seq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Seq) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	USkeleton* Skeleton = Seq->GetSkeleton();
	if (!Skeleton) return MCPError(TEXT("AnimSequence has no Skeleton"));

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const FName SourceFName(*SourceBoneName);
	const FName RootFName(*RootBoneName);
	if (RefSkeleton.FindBoneIndex(SourceFName) == INDEX_NONE)
	{
		return MCPError(FString::Printf(TEXT("Source bone '%s' not found in skeleton"), *SourceBoneName));
	}
	if (RefSkeleton.FindBoneIndex(RootFName) == INDEX_NONE)
	{
		return MCPError(FString::Printf(TEXT("Root bone '%s' not found in skeleton"), *RootBoneName));
	}

	IAnimationDataModel* DataModel = Seq->GetDataModel();
	if (!DataModel) return MCPError(TEXT("Sequence has no data model"));

	const int32 NumFrames = DataModel->GetNumberOfFrames();
	const int32 NumKeys = NumFrames + 1;
	if (NumKeys < 2) return MCPError(TEXT("Sequence must have at least 2 keys to bake root motion"));

	const FFrameRate FrameRate = DataModel->GetFrameRate();

	TArray<FVector> SourceLocIn, SourceLocOut, RootLocOut;
	TArray<FQuat>   SourceRotIn, RootRotOut;
	TArray<FVector> SourceSclIn, RootSclOut;
	SourceLocIn.Reserve(NumKeys); SourceLocOut.Reserve(NumKeys); RootLocOut.Reserve(NumKeys);
	SourceRotIn.Reserve(NumKeys); RootRotOut.Reserve(NumKeys);
	SourceSclIn.Reserve(NumKeys); RootSclOut.Reserve(NumKeys);

	for (int32 Key = 0; Key < NumKeys; ++Key)
	{
		const FFrameTime FT = FrameRate.AsFrameTime((double)Key / FrameRate.AsDecimal());
		const FTransform Xf = DataModel->EvaluateBoneTrackTransform(SourceFName, FT, EAnimInterpolationType::Linear);
		SourceLocIn.Add(Xf.GetLocation());
		SourceRotIn.Add(Xf.GetRotation());
		SourceSclIn.Add(Xf.GetScale3D());
	}

	const FVector StartLoc = SourceLocIn[0];
	const FVector EndLoc = SourceLocIn.Last();
	const FVector TotalDelta(
		bBakeX ? (EndLoc.X - StartLoc.X) : 0.0,
		bBakeY ? (EndLoc.Y - StartLoc.Y) : 0.0,
		bBakeZ ? (EndLoc.Z - StartLoc.Z) : 0.0);

	const bool bPerFrame = InterpMode.Equals(TEXT("per_frame"), ESearchCase::IgnoreCase);

	for (int32 Key = 0; Key < NumKeys; ++Key)
	{
		FVector RootDelta = FVector::ZeroVector;
		if (bPerFrame)
		{
			const FVector Cur = SourceLocIn[Key] - StartLoc;
			RootDelta = FVector(bBakeX ? Cur.X : 0.0, bBakeY ? Cur.Y : 0.0, bBakeZ ? Cur.Z : 0.0);
		}
		else
		{
			const double T = (NumKeys > 1) ? ((double)Key / (double)(NumKeys - 1)) : 0.0;
			RootDelta = TotalDelta * T;
		}
		RootLocOut.Add(RootDelta);
		RootRotOut.Add(FQuat::Identity);
		RootSclOut.Add(FVector::OneVector);

		FVector SrcLoc = SourceLocIn[Key];
		if (bBakeX) SrcLoc.X -= RootDelta.X;
		if (bBakeY) SrcLoc.Y -= RootDelta.Y;
		if (bBakeZ) SrcLoc.Z -= RootDelta.Z;
		SourceLocOut.Add(SrcLoc);
	}

	IAnimationDataController& Controller = Seq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "BakeRootMotion", "Bake Root Motion From Bone"));

	if (!DataModel->IsValidBoneTrackName(RootFName)) Controller.AddBoneCurve(RootFName);
	if (!DataModel->IsValidBoneTrackName(SourceFName)) Controller.AddBoneCurve(SourceFName);

	Controller.SetBoneTrackKeys(RootFName, RootLocOut, RootRotOut, RootSclOut);
	Controller.SetBoneTrackKeys(SourceFName, SourceLocOut, SourceRotIn, SourceSclIn);

	Controller.CloseBracket(false);

	GEditor->ResetTransaction(NSLOCTEXT("MCP", "BakeRootMotionReset", "Bake Root Motion Complete"));

	Seq->bEnableRootMotion = true;
	Seq->PostEditChange();
	Seq->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Seq, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceBone"), SourceBoneName);
	Result->SetStringField(TEXT("rootBone"), RootBoneName);
	Result->SetNumberField(TEXT("keys"), NumKeys);
	TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetNumberField(TEXT("x"), TotalDelta.X);
	Delta->SetNumberField(TEXT("y"), TotalDelta.Y);
	Delta->SetNumberField(TEXT("z"), TotalDelta.Z);
	Result->SetObjectField(TEXT("totalDelta"), Delta);
	Result->SetStringField(TEXT("interpolation"), bPerFrame ? TEXT("per_frame") : TEXT("linear"));
	return MCPResult(Result);
}

// #656: compare the curve names on an AnimSequence or PoseAsset against the
// morph target names on a SkeletalMesh, so a caller can verify (without
// Python) that authored curves actually drive morphs and spot the mismatches.
TSharedPtr<FJsonValue> FAnimationHandlers::CompareCurvesToMorphTargets(const TSharedPtr<FJsonObject>& Params)
{
	FString CurveAssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("animPath"), TEXT("assetPath"), CurveAssetPath)) return Err;
	FString MeshPath;
	if (auto Err = RequireStringAlt(Params, TEXT("skeletalMeshPath"), TEXT("meshPath"), MeshPath)) return Err;

	UObject* CurveAsset = UEditorAssetLibrary::LoadAsset(CurveAssetPath);
	if (!CurveAsset) return MCPError(FString::Printf(TEXT("Asset not found: %s"), *CurveAssetPath));
	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(MeshPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath));

	// Collect curve names from an AnimSequence(Base) or a PoseAsset.
	TSet<FString> CurveNames;
	FString CurveKind;
	if (UPoseAsset* Pose = Cast<UPoseAsset>(CurveAsset))
	{
		CurveKind = TEXT("PoseAsset");
		for (const FName& N : Pose->GetCurveFNames()) CurveNames.Add(N.ToString());
	}
	else if (UAnimSequenceBase* Anim = Cast<UAnimSequenceBase>(CurveAsset))
	{
		CurveKind = TEXT("AnimSequence");
		for (const FFloatCurve& C : Anim->GetCurveData().FloatCurves)
		{
			CurveNames.Add(C.GetName().ToString());
		}
	}
	else
	{
		return MCPError(TEXT("Asset is not an AnimSequence or PoseAsset"));
	}

	// Collect morph target names from the skeletal mesh.
	TSet<FString> MorphNames;
	for (UMorphTarget* MT : Mesh->GetMorphTargets())
	{
		if (MT) MorphNames.Add(MT->GetName());
	}

	TArray<TSharedPtr<FJsonValue>> Matched, CurvesNoMorph, MorphsNoCurve, CurveList, MorphList;
	for (const FString& C : CurveNames)
	{
		CurveList.Add(MakeShared<FJsonValueString>(C));
		if (MorphNames.Contains(C)) Matched.Add(MakeShared<FJsonValueString>(C));
		else CurvesNoMorph.Add(MakeShared<FJsonValueString>(C));
	}
	for (const FString& M : MorphNames)
	{
		MorphList.Add(MakeShared<FJsonValueString>(M));
		if (!CurveNames.Contains(M)) MorphsNoCurve.Add(MakeShared<FJsonValueString>(M));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("curveAsset"), CurveAssetPath);
	Result->SetStringField(TEXT("curveKind"), CurveKind);
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("curveCount"), CurveNames.Num());
	Result->SetNumberField(TEXT("morphTargetCount"), MorphNames.Num());
	Result->SetNumberField(TEXT("matchedCount"), Matched.Num());
	Result->SetArrayField(TEXT("curves"), CurveList);
	Result->SetArrayField(TEXT("morphTargets"), MorphList);
	Result->SetArrayField(TEXT("matched"), Matched);
	Result->SetArrayField(TEXT("curvesWithoutMorph"), CurvesNoMorph);
	Result->SetArrayField(TEXT("morphsWithoutCurve"), MorphsNoCurve);
	return MCPResult(Result);
}
