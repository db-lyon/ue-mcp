// transform_bone_tracks (#1227): read-modify-write of raw bone tracks across many
// sequences in one call. A member of FAnimationHandlers in its own translation
// unit; registration stays in AnimationHandlers.cpp.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAnimTrackPose.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "ReferenceSkeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Named, not anonymous: the module is a unity build and anonymous namespaces
// from neighbouring files merge.
namespace MCPBoneTrackOps
{
	enum class EOp : uint8 { Rotate, Offset, CopyFromDonor, ReferenceDelta };

	struct FOp
	{
		EOp Kind = EOp::Rotate;
		TArray<FName> Bones;
		FQuat Rotation = FQuat::Identity;
		bool bPost = false;
		bool bRotateTranslation = false;
		FVector Translation = FVector::ZeroVector;
		UAnimSequence* Donor = nullptr;
		double DonorTime = 0.0;
		bool bKeepTranslation = true;
		bool bKeepScale = true;
		FName SourceBone;
		FName TargetBone;
	};

	constexpr int32 MaxAssets = 500;
	constexpr int32 MaxOps = 256;

	TArray<FName> ReadBoneList(const TSharedPtr<FJsonObject>& Obj)
	{
		TArray<FName> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(TEXT("bones"), Arr) && Arr)
		{
			for (const FString& Name : JsonArrayToStringList(Arr))
			{
				if (!Name.IsEmpty()) Out.AddUnique(FName(*Name));
			}
		}
		return Out;
	}

	/** Parse one operation, loading its donor. Returns an error string, or empty. */
	FString ParseOp(const TSharedPtr<FJsonObject>& Obj, int32 Index, TMap<FString, UAnimSequence*>& Donors, FOp& Out)
	{
		FString Kind;
		Obj->TryGetStringField(TEXT("op"), Kind);
		Kind = Kind.ToLower();
		if (Kind == TEXT("rotate")) Out.Kind = EOp::Rotate;
		else if (Kind == TEXT("offset")) Out.Kind = EOp::Offset;
		else if (Kind == TEXT("copy_from_donor")) Out.Kind = EOp::CopyFromDonor;
		else if (Kind == TEXT("reference_delta")) Out.Kind = EOp::ReferenceDelta;
		else return FString::Printf(TEXT("trackOps[%d].op '%s' is not rotate, offset, copy_from_donor or reference_delta"), Index, *Kind);

		Out.Bones = ReadBoneList(Obj);
		if (Out.Kind != EOp::ReferenceDelta && Out.Bones.Num() == 0)
		{
			return FString::Printf(TEXT("trackOps[%d] (%s) needs a non-empty bones array"), Index, *Kind);
		}

		if (Out.Kind == EOp::Rotate)
		{
			const TSharedPtr<FJsonObject>* Rot = nullptr;
			if (!Obj->TryGetObjectField(TEXT("rotation"), Rot) || !Rot)
			{
				return FString::Printf(TEXT("trackOps[%d] (rotate) needs rotation, {x,y,z,w} or {pitch,yaw,roll}"), Index);
			}
			FString RotError;
			if (!MCPAnimTrackPose::ReadRotationObject(*Rot, Out.Rotation, RotError))
			{
				return FString::Printf(TEXT("trackOps[%d].rotation: %s"), Index, *RotError);
			}
			FString Order = TEXT("pre");
			Obj->TryGetStringField(TEXT("order"), Order);
			if (!Order.Equals(TEXT("pre"), ESearchCase::IgnoreCase) && !Order.Equals(TEXT("post"), ESearchCase::IgnoreCase))
			{
				return FString::Printf(TEXT("trackOps[%d].order must be 'pre' (parent space, default) or 'post' (bone space)"), Index);
			}
			Out.bPost = Order.Equals(TEXT("post"), ESearchCase::IgnoreCase);
			Obj->TryGetBoolField(TEXT("rotateTranslation"), Out.bRotateTranslation);
			if (Out.bPost && Out.bRotateTranslation)
			{
				return FString::Printf(TEXT("trackOps[%d]: rotateTranslation only applies to order 'pre'"), Index);
			}
		}
		else if (Out.Kind == EOp::Offset)
		{
			const TSharedPtr<FJsonObject>* Vec = nullptr;
			if (!Obj->TryGetObjectField(TEXT("translation"), Vec) || !Vec || !ReadVec3Fields(*Vec, Out.Translation))
			{
				return FString::Printf(TEXT("trackOps[%d] (offset) needs translation {x,y,z}"), Index);
			}
		}

		FString DonorPath;
		Obj->TryGetStringField(TEXT("donorPath"), DonorPath);
		if (Out.Kind == EOp::CopyFromDonor && DonorPath.IsEmpty())
		{
			return FString::Printf(TEXT("trackOps[%d] (copy_from_donor) needs donorPath"), Index);
		}
		if (!DonorPath.IsEmpty())
		{
			if (Out.Kind != EOp::CopyFromDonor && Out.Kind != EOp::ReferenceDelta)
			{
				return FString::Printf(TEXT("trackOps[%d]: donorPath only applies to copy_from_donor and reference_delta"), Index);
			}
			UAnimSequence** Cached = Donors.Find(DonorPath);
			if (!Cached)
			{
				UAnimSequence* Loaded = nullptr;
				if (MCPAnimTrackPose::LoadSequence(DonorPath, Loaded))
				{
					return FString::Printf(TEXT("trackOps[%d].donorPath '%s' is not an AnimSequence"), Index, *DonorPath);
				}
				Cached = &Donors.Add(DonorPath, Loaded);
			}
			Out.Donor = *Cached;
			double Frame = 0.0, Time = 0.0;
			const bool bFrame = Obj->TryGetNumberField(TEXT("donorFrame"), Frame);
			const bool bTime = Obj->TryGetNumberField(TEXT("donorTime"), Time);
			if (bFrame && bTime)
			{
				return FString::Printf(TEXT("trackOps[%d]: pass donorFrame or donorTime, not both"), Index);
			}
			if (bTime)
			{
				if (Time < 0.0 || Time > Out.Donor->GetPlayLength() + UE_KINDA_SMALL_NUMBER)
				{
					return FString::Printf(TEXT("trackOps[%d].donorTime %g is outside the donor's 0 to %g seconds"), Index, Time, Out.Donor->GetPlayLength());
				}
				Out.DonorTime = Time;
			}
			else
			{
				FString FrameError;
				if (!MCPAnimTrackPose::FrameToTime(Out.Donor, (int32)Frame, Out.DonorTime, FrameError))
				{
					return FString::Printf(TEXT("trackOps[%d].donorFrame: %s"), Index, *FrameError);
				}
			}
		}

		if (Out.Kind == EOp::CopyFromDonor)
		{
			Obj->TryGetBoolField(TEXT("keepTranslation"), Out.bKeepTranslation);
			Obj->TryGetBoolField(TEXT("keepScale"), Out.bKeepScale);
		}
		if (Out.Kind == EOp::ReferenceDelta)
		{
			FString Source, Target;
			Obj->TryGetStringField(TEXT("sourceBone"), Source);
			Obj->TryGetStringField(TEXT("targetBone"), Target);
			if (Source.IsEmpty() || Target.IsEmpty())
			{
				return FString::Printf(TEXT("trackOps[%d] (reference_delta) needs sourceBone and targetBone"), Index);
			}
			Out.SourceBone = FName(*Source);
			Out.TargetBone = FName(*Target);
		}
		return FString();
	}

	/** Every bone an operation reads or writes, for the up-front skeleton check. */
	TArray<FName> BonesOf(const FOp& Op)
	{
		TArray<FName> Out = Op.Bones;
		if (Op.Kind == EOp::ReferenceDelta)
		{
			Out.AddUnique(Op.SourceBone);
			Out.AddUnique(Op.TargetBone);
		}
		return Out;
	}
}

TSharedPtr<FJsonValue> FAnimationHandlers::TransformBoneTracks(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBoneTrackOps;

	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("assetPaths"), PathsArr) && PathsArr;
	const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
	const bool bHasOps = TryGetArrayParam(Params, TEXT("trackOps"), OpsArr) && OpsArr;
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	if (!bHasPaths || PathsArr->Num() == 0) return MCPError(TEXT("assetPaths must be a non-empty array of AnimSequence paths"));
	if (!bHasOps || OpsArr->Num() == 0) return MCPError(TEXT("trackOps must be a non-empty array"));
	if (PathsArr->Num() > MaxAssets) return MCPBatchTooLargeError(FString::Printf(TEXT("assetPaths holds %d entries; the limit is %d"), PathsArr->Num(), MaxAssets), MaxAssets);
	if (OpsArr->Num() > MaxOps) return MCPBatchTooLargeError(FString::Printf(TEXT("trackOps holds %d entries; the limit is %d"), OpsArr->Num(), MaxOps), MaxOps);

	// Parse and load everything before any sequence is written.
	TMap<FString, UAnimSequence*> Donors;
	TArray<FOp> Ops;
	for (int32 Index = 0; Index < OpsArr->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* OpObj = nullptr;
		if (!(*OpsArr)[Index].IsValid() || !(*OpsArr)[Index]->TryGetObject(OpObj) || !OpObj)
		{
			return MCPError(FString::Printf(TEXT("trackOps[%d] must be an object"), Index));
		}
		FOp Op;
		const FString Error = ParseOp(*OpObj, Index, Donors, Op);
		if (!Error.IsEmpty()) return MCPError(Error);
		if (Op.Donor)
		{
			const FReferenceSkeleton* DonorRef = Op.Donor->GetSkeleton() ? &Op.Donor->GetSkeleton()->GetReferenceSkeleton() : nullptr;
			if (!DonorRef) return MCPError(FString::Printf(TEXT("trackOps[%d]: the donor has no skeleton"), Index));
			const TArray<FName> DonorBones = Op.Kind == EOp::ReferenceDelta ? TArray<FName>{ Op.SourceBone } : Op.Bones;
			for (const FName& Bone : DonorBones)
			{
				if (DonorRef->FindBoneIndex(Bone) == INDEX_NONE)
				{
					return MCPError(FString::Printf(TEXT("trackOps[%d]: bone '%s' is not on the donor's skeleton"), Index, *Bone.ToString()));
				}
			}
		}
		Ops.Add(Op);
	}

	TArray<UAnimSequence*> Sequences;
	for (const FString& Path : JsonArrayToStringList(PathsArr))
	{
		UAnimSequence* Seq = nullptr;
		if (auto Err = MCPAnimTrackPose::LoadSequence(Path, Seq)) return Err;
		if (Sequences.Contains(Seq)) return MCPError(FString::Printf(TEXT("'%s' is listed twice"), *Path));
		if (auto Blocked = MCPAssetWriteBlockedError(Seq, Seq->GetPathName(), TEXT("rewrite its bone tracks"))) return Blocked;
		if (!Seq->GetDataModel() || !Seq->GetSkeleton())
		{
			return MCPError(FString::Printf(TEXT("'%s' has no data model or skeleton, so its tracks cannot be rewritten"), *Path));
		}
		const FReferenceSkeleton& Ref = Seq->GetSkeleton()->GetReferenceSkeleton();
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			for (const FName& Bone : BonesOf(Ops[Index]))
			{
				if (Ref.FindBoneIndex(Bone) == INDEX_NONE)
				{
					return MCPError(FString::Printf(TEXT("trackOps[%d]: bone '%s' is not on the skeleton of '%s'. Nothing was written."), Index, *Bone.ToString(), *Path));
				}
			}
		}
		Sequences.Add(Seq);
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	TSharedPtr<FJsonObject> RestorePayload;
	int32 CreatedTrackCount = 0;
	int32 SaveFailed = 0;
	FString LastSaveError;
	for (UAnimSequence* Seq : Sequences)
	{
		const FReferenceSkeleton& Ref = Seq->GetSkeleton()->GetReferenceSkeleton();
		const IAnimationDataModel* Model = Seq->GetDataModel();
		const int32 NumKeys = Model->GetNumberOfKeys();

		// Working copy of every touched track, in the order first touched.
		TMap<FName, TArray<FTransform>> Work;
		TMap<FName, TArray<FTransform>> Original;
		TArray<FName> Order;
		auto Track = [&](const FName& Bone) -> TArray<FTransform>&
		{
			if (TArray<FTransform>* Found = Work.Find(Bone)) return *Found;
			Order.Add(Bone);
			TArray<FTransform> Keys = MCPAnimTrackPose::TrackKeysOrReference(Seq, Ref, Ref.FindBoneIndex(Bone), NumKeys);
			Original.Add(Bone, Keys);
			return Work.Add(Bone, MoveTemp(Keys));
		};
		TMap<FName, FQuat> Before;

		for (const FOp& Op : Ops)
		{
			if (Op.Kind == EOp::ReferenceDelta)
			{
				const FTransform& RefSource = Ref.GetRefBonePose()[Ref.FindBoneIndex(Op.SourceBone)];
				const FTransform& RefTarget = Ref.GetRefBonePose()[Ref.FindBoneIndex(Op.TargetBone)];
				const TArray<FTransform> SourceKeys = Op.Donor ? TArray<FTransform>() : Track(Op.SourceBone);
				FQuat DonorSource = FQuat::Identity;
				if (Op.Donor)
				{
					const FReferenceSkeleton& DonorRef = Op.Donor->GetSkeleton()->GetReferenceSkeleton();
					DonorSource = MCPAnimTrackPose::LocalBoneAtTime(Op.Donor, DonorRef, DonorRef.FindBoneIndex(Op.SourceBone), Op.DonorTime).GetRotation();
				}
				TArray<FTransform>& Keys = Track(Op.TargetBone);
				if (!Before.Contains(Op.TargetBone) && Keys.Num() > 0) Before.Add(Op.TargetBone, Keys[0].GetRotation());
				for (int32 K = 0; K < Keys.Num(); ++K)
				{
					const FQuat Source = Op.Donor ? DonorSource : SourceKeys[K].GetRotation();
					FQuat Q = RefTarget.GetRotation() * (RefSource.GetRotation().Inverse() * Source);
					Q.Normalize();
					Keys[K].SetRotation(Q);
				}
				continue;
			}

			for (const FName& Bone : Op.Bones)
			{
				TArray<FTransform>& Keys = Track(Bone);
				if (!Before.Contains(Bone) && Keys.Num() > 0) Before.Add(Bone, Keys[0].GetRotation());
				FTransform DonorPose = FTransform::Identity;
				if (Op.Kind == EOp::CopyFromDonor)
				{
					const FReferenceSkeleton& DonorRef = Op.Donor->GetSkeleton()->GetReferenceSkeleton();
					DonorPose = MCPAnimTrackPose::LocalBoneAtTime(Op.Donor, DonorRef, DonorRef.FindBoneIndex(Bone), Op.DonorTime);
				}
				for (FTransform& Key : Keys)
				{
					switch (Op.Kind)
					{
					case EOp::Rotate:
					{
						FQuat Q = Op.bPost ? Key.GetRotation() * Op.Rotation : Op.Rotation * Key.GetRotation();
						Q.Normalize();
						Key.SetRotation(Q);
						if (Op.bRotateTranslation) Key.SetLocation(Op.Rotation.RotateVector(Key.GetLocation()));
						break;
					}
					case EOp::Offset:
						Key.SetLocation(Key.GetLocation() + Op.Translation);
						break;
					case EOp::CopyFromDonor:
						Key.SetRotation(DonorPose.GetRotation());
						if (!Op.bKeepTranslation) Key.SetLocation(DonorPose.GetLocation());
						if (!Op.bKeepScale) Key.SetScale3D(DonorPose.GetScale3D());
						break;
					default:
						break;
					}
				}
			}
		}

		TArray<FString> Written;
		TArray<FString> Created;
		FString FailErr;
		IAnimationDataController& Controller = Seq->GetController();
		Controller.OpenBracket(NSLOCTEXT("MCP", "TransformBoneTracks", "MCP Transform Bone Tracks"));
		for (const FName& Bone : Order)
		{
			const TArray<FTransform>& Keys = Work.FindChecked(Bone);
			if (!Model->IsValidBoneTrackName(Bone))
			{
				Controller.AddBoneCurve(Bone);
				Created.Add(Bone.ToString());
			}
			TArray<FVector> Locations;
			TArray<FQuat> Rotations;
			TArray<FVector> Scales;
			for (const FTransform& Key : Keys)
			{
				Locations.Add(Key.GetLocation());
				Rotations.Add(Key.GetRotation());
				Scales.Add(Key.GetScale3D());
			}
			if (!Controller.SetBoneTrackKeys(Bone, Locations, Rotations, Scales))
			{
				FailErr = FString::Printf(TEXT("writing bone track '%s' failed"), *Bone.ToString());
				break;
			}
			Written.Add(Bone.ToString());
		}
		Controller.CloseBracket();
		if (!FailErr.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("transform_bone_tracks failed on '%s': %s. Sequences before it in assetPaths were written%s; this one is partly written and not saved."),
				*Seq->GetPathName(), *FailErr, bSave ? TEXT(" and saved") : TEXT("")));
		}
		Seq->PostEditChange();
		Seq->MarkPackageDirty();

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), Seq->GetPathName());
		Row->SetNumberField(TEXT("numKeys"), NumKeys);
		Row->SetArrayField(TEXT("bonesWritten"), MCPStringListToJson(Written));
		if (Created.Num() > 0) Row->SetArrayField(TEXT("tracksCreated"), MCPStringListToJson(Created));
		// Frame 0 of each written track before and after, as exact quaternions.
		TArray<TSharedPtr<FJsonValue>> Samples;
		for (const FName& Bone : Order)
		{
			if (Samples.Num() >= 64) break;
			const FQuat* Prior = Before.Find(Bone);
			const TArray<FTransform>& Keys = Work.FindChecked(Bone);
			if (!Prior || Keys.Num() == 0) continue;
			TSharedPtr<FJsonObject> Sample = MakeShared<FJsonObject>();
			Sample->SetStringField(TEXT("bone"), Bone.ToString());
			Sample->SetObjectField(TEXT("frame0Before"), MCPQuatToJsonObject(*Prior));
			Sample->SetObjectField(TEXT("frame0After"), MCPQuatToJsonObject(Keys[0].GetRotation()));
			Samples.Add(MakeShared<FJsonValueObject>(Sample));
		}
		Row->SetArrayField(TEXT("samples"), Samples);
		if (bSave)
		{
			FString SaveError;
			const bool bSaved = SaveAssetPackageChecked(Seq, SaveError);
			Row->SetBoolField(TEXT("saved"), bSaved);
			if (!bSaved)
			{
				Row->SetStringField(TEXT("saveError"), SaveError);
				++SaveFailed;
				LastSaveError = FString::Printf(TEXT("'%s': %s"), *Seq->GetPathName(), *SaveError);
			}
		}
		Rows.Add(MakeShared<FJsonValueObject>(Row));

		// The keys every written track held before, in the shape bake_keyframes_batch replays.
		CreatedTrackCount += Created.Num();
		TArray<TSharedPtr<FJsonValue>> RestoreTracks;
		for (const FName& Bone : Order)
		{
			TArray<TSharedPtr<FJsonValue>> Keyframes;
			for (const FTransform& Key : Original.FindChecked(Bone))
			{
				TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
				Frame->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Key.GetLocation()));
				Frame->SetObjectField(TEXT("rotation"), MCPQuatToJsonObject(Key.GetRotation()));
				Frame->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Key.GetScale3D()));
				Keyframes.Add(MakeShared<FJsonValueObject>(Frame));
			}
			TSharedPtr<FJsonObject> RestoreTrack = MakeShared<FJsonObject>();
			RestoreTrack->SetStringField(TEXT("bone"), Bone.ToString());
			RestoreTrack->SetArrayField(TEXT("keyframes"), Keyframes);
			RestoreTracks.Add(MakeShared<FJsonValueObject>(RestoreTrack));
		}
		RestorePayload = MakeShared<FJsonObject>();
		RestorePayload->SetStringField(TEXT("assetPath"), Seq->GetPathName());
		RestorePayload->SetArrayField(TEXT("tracks"), RestoreTracks);
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("sequenceCount"), Rows.Num());
	Result->SetNumberField(TEXT("operationCount"), Ops.Num());
	Result->SetArrayField(TEXT("sequences"), Rows);
	if (SaveFailed > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetNumberField(TEXT("saveFailed"), SaveFailed);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("%d sequence(s) were rewritten in memory but not saved, last %s"), SaveFailed, *LastSaveError));
	}
	if (Sequences.Num() == 1 && RestorePayload.IsValid())
	{
		MCPSetRollback(Result, TEXT("bake_keyframes_batch"), RestorePayload);
		Result->SetBoolField(TEXT("rollbackLossy"), CreatedTrackCount > 0);
		if (CreatedTrackCount > 0)
		{
			Result->SetStringField(TEXT("rollbackNote"), TEXT("Tracks this call created are restored as reference-pose keys rather than removed, which plays the same pose."));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("One inverse call restores one sequence, and this call rewrote several. Run it once per sequence for a rollback, or revert the packages."));
	}
	return MCPResult(Result);
}
