// Evaluated pose reads (#923, #926, #922).
//
// Translation-unit partition of FAnimationHandlers - registration stays in
// AnimationHandlers.cpp::RegisterHandlers.
//
// Everything the bridge could previously say about an animation was either raw
// or reference data. read_bone_track returns LOCAL space, so recovering where a
// foot actually is means composing the whole parent chain outside the engine,
// and get_bone_transforms has a component-space mode but reads the REFERENCE
// pose, which is the read that looks right while the evaluated instance is
// wrong - exactly the shape of #922, where a character lay flat and every
// transform the bridge could show read clean.
//
// These evaluate. sample_pose runs the engine's own pose evaluator over an
// AnimSequence (or a BlendSpace at a blend position) and hands back composed
// transforms; measure_natural_speed builds on it to answer the question that
// blocks every retarget-then-rebuild-the-BlendSpace loop (#923): how fast does
// THIS clip on THIS skeleton actually move.

#include "AnimationHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "AnimPose.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FrameRate.h"
#include "ReferenceSkeleton.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
// A pose sample costs one object per bone. Sampling every frame of a long clip
// on a full humanoid skeleton is tens of thousands of transforms, which is a
// payload no caller wants by accident, so the request is refused with the two
// parameters that narrow it named in the message.
constexpr int32 PoseSampleTransformCap = 20000;

/** Resolved evaluation target: an AnimSequenceBase, or a BlendSpace. */
struct FPoseEvaluationTarget
{
	UAnimSequenceBase* Sequence = nullptr;
	UBlendSpace* BlendSpace = nullptr;
	USkeleton* Skeleton = nullptr;
	FString AssetType;
	float PlayLength = 0.0f;
	int32 NumberOfSampledKeys = 0;
	FFrameRate SamplingFrameRate = FFrameRate(30, 1);
	bool bHasRootMotion = false;
};

/** Load the addressed animation asset, rejecting the ones the evaluator cannot
 *  take. Returns an error value on failure, or an unset pointer on success. */
TSharedPtr<FJsonValue> ResolvePoseEvaluationTarget(const FString& AssetPath, FPoseEvaluationTarget& Out)
{
	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));
	}

	if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
	{
		Out.BlendSpace = BlendSpace;
		Out.Skeleton = BlendSpace->GetSkeleton();
		Out.AssetType = BlendSpace->GetClass()->GetName();
		// UBlendSpace::GetPlayLength() is the normalised 1.0 the runtime plays
		// against, not a duration. The real one depends on which samples the
		// blend position lands between, so it is filled in by
		// ResolveBlendSpaceLength once that position is known.
		Out.PlayLength = 0.0f;
		Out.NumberOfSampledKeys = 0;
		return nullptr;
	}

	if (UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
	{
		// UAnimMontage::GetAnimationPose is a hard check(false) in the engine: a
		// montage is a schedule of other assets, so the thing to evaluate is the
		// sequence in the slot, not the montage.
		return MCPError(FString::Printf(
			TEXT("'%s' is an AnimMontage, which has no pose of its own. Read its segments with animation(list_montage_segments) and sample the AnimSequence each one plays."),
			*Montage->GetPathName()));
	}

	if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
	{
		Out.Sequence = Sequence;
		Out.Skeleton = Sequence->GetSkeleton();
		Out.AssetType = Sequence->GetClass()->GetName();
		Out.PlayLength = Sequence->GetPlayLength();
		Out.NumberOfSampledKeys = Sequence->GetNumberOfSampledKeys();
		Out.SamplingFrameRate = Sequence->GetSamplingFrameRate();
		Out.bHasRootMotion = Sequence->HasRootMotion();
		return nullptr;
	}

	return MCPError(FString::Printf(
		TEXT("Asset '%s' is a %s, which is not an AnimSequence or BlendSpace"),
		*AssetPath, *Asset->GetClass()->GetName()));
}

/** Space token to the engine's enum. EAnimPoseSpaces::World is component space
 *  in this API (the engine's own comment says so); an animation asset has no
 *  outer frame beyond its skeleton root, so 'world' resolves to the same pose. */
bool ParsePoseSpace(const FString& Token, EAnimPoseSpaces& OutSpace, FString& OutResolved)
{
	if (Token == TEXT("local"))
	{
		OutSpace = EAnimPoseSpaces::Local;
		OutResolved = TEXT("local");
		return true;
	}
	if (Token == TEXT("component") || Token == TEXT("world"))
	{
		OutSpace = EAnimPoseSpaces::World;
		OutResolved = TEXT("component");
		return true;
	}
	return false;
}

FAnimPoseEvaluationOptions MakePoseEvaluationOptions(USkeletalMesh* OptionalMesh, bool bIncorporateRootMotion)
{
	FAnimPoseEvaluationOptions Options;
	Options.EvaluationType = EAnimDataEvalType::Raw;
	Options.bShouldRetarget = true;
	Options.bExtractRootMotion = false;
	Options.bIncorporateRootMotionIntoPose = bIncorporateRootMotion;
	Options.bRetrieveAdditiveAsFullPose = true;
	Options.bEvaluateCurves = false;
	Options.OptionalSkeletalMesh = OptionalMesh;
	return Options;
}

TSharedPtr<FJsonObject> PoseBoneTransformToJson(const FName& BoneName, const FTransform& Transform)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), BoneName.ToString());
	Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Transform.GetLocation()));
	Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Transform.GetRotation().Rotator()));
	Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Transform.GetScale3D()));
	return Obj;
}

/** Weighted blend of the contributing samples of a BlendSpace, evaluated at one
 *  normalised phase. Mirrors what the runtime does: every contributing sequence
 *  is sampled at the same phase and the results are averaged by weight. */
bool EvaluateBlendSpacePoseAtTime(
	UBlendSpace* BlendSpace,
	const FVector& BlendInput,
	double Time,
	const FAnimPoseEvaluationOptions& Options,
	const TArray<FName>& RequestedBones,
	EAnimPoseSpaces Space,
	TArray<FName>& OutBoneNames,
	TArray<FTransform>& OutTransforms,
	FString& OutError)
{
	TArray<FBlendSampleData> SampleData;
	int32 CachedTriangulationIndex = INDEX_NONE;
	if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, SampleData, CachedTriangulationIndex, /*bCombineAnimations=*/true))
	{
		OutError = FString::Printf(
			TEXT("BlendSpace '%s' produced no samples at blend position (%g, %g, %g)"),
			*BlendSpace->GetPathName(), BlendInput.X, BlendInput.Y, BlendInput.Z);
		return false;
	}

	const double BlendedLength = static_cast<double>(BlendSpace->GetAnimationLengthFromSampleData(SampleData));
	const double Phase = BlendedLength > KINDA_SMALL_NUMBER ? FMath::Clamp(Time / BlendedLength, 0.0, 1.0) : 0.0;

	double TotalWeight = 0.0;
	bool bSeeded = false;
	TArray<FQuat> AccumulatedRotations;
	TArray<FVector> AccumulatedTranslations;
	TArray<FVector> AccumulatedScales;

	for (const FBlendSampleData& Sample : SampleData)
	{
		UAnimSequence* Animation = Sample.Animation;
		if (!Animation || Sample.TotalWeight <= KINDA_SMALL_NUMBER) continue;

		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(Animation, Phase * Animation->GetPlayLength(), Options, Pose);
		if (!UAnimPoseExtensions::IsValid(Pose)) continue;

		TArray<FName> BoneNames;
		if (RequestedBones.Num() > 0)
		{
			BoneNames = RequestedBones;
		}
		else
		{
			UAnimPoseExtensions::GetBoneNames(Pose, BoneNames);
		}

		if (!bSeeded)
		{
			OutBoneNames = BoneNames;
			AccumulatedRotations.SetNum(BoneNames.Num());
			AccumulatedTranslations.SetNumZeroed(BoneNames.Num());
			AccumulatedScales.SetNumZeroed(BoneNames.Num());
			for (FQuat& Rotation : AccumulatedRotations) Rotation = FQuat(0, 0, 0, 0);
			bSeeded = true;
		}

		const double Weight = static_cast<double>(Sample.TotalWeight);
		TotalWeight += Weight;
		for (int32 BoneIndex = 0; BoneIndex < OutBoneNames.Num(); ++BoneIndex)
		{
			const FTransform BoneTransform = UAnimPoseExtensions::GetBonePose(Pose, OutBoneNames[BoneIndex], Space);
			FQuat Rotation = BoneTransform.GetRotation();
			// Align to the accumulator's hemisphere so opposite-sign quaternions
			// for the same orientation do not cancel each other out.
			if ((AccumulatedRotations[BoneIndex] | Rotation) < 0.0)
			{
				Rotation = -Rotation;
			}
			AccumulatedRotations[BoneIndex] += Rotation * Weight;
			AccumulatedTranslations[BoneIndex] += BoneTransform.GetLocation() * Weight;
			AccumulatedScales[BoneIndex] += BoneTransform.GetScale3D() * Weight;
		}
	}

	if (!bSeeded || TotalWeight <= KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(
			TEXT("BlendSpace '%s' has no evaluable animation at blend position (%g, %g, %g)"),
			*BlendSpace->GetPathName(), BlendInput.X, BlendInput.Y, BlendInput.Z);
		return false;
	}

	OutTransforms.Reset(OutBoneNames.Num());
	for (int32 BoneIndex = 0; BoneIndex < OutBoneNames.Num(); ++BoneIndex)
	{
		FQuat Rotation = AccumulatedRotations[BoneIndex];
		Rotation.Normalize();
		OutTransforms.Add(FTransform(
			Rotation,
			AccumulatedTranslations[BoneIndex] / TotalWeight,
			AccumulatedScales[BoneIndex] / TotalWeight));
	}
	return true;
}

/** One evaluated sample: the frame/time it came from and the bones it holds. */
struct FEvaluatedPoseSample
{
	int32 Frame = INDEX_NONE;
	double Time = 0.0;
	TArray<FName> BoneNames;
	TArray<FTransform> Transforms;
};

/** Shared driver for sample_pose and measure_natural_speed. */
TSharedPtr<FJsonValue> EvaluatePoseSamples(
	const FPoseEvaluationTarget& Target,
	const TArray<int32>& Frames,
	const TArray<double>& Times,
	const TArray<FName>& RequestedBones,
	EAnimPoseSpaces Space,
	const FAnimPoseEvaluationOptions& Options,
	const FVector& BlendInput,
	TArray<FEvaluatedPoseSample>& OutSamples)
{
	const int32 SampleCount = Frames.Num() > 0 ? Frames.Num() : Times.Num();
	OutSamples.Reset(SampleCount);

	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		FEvaluatedPoseSample Sample;
		Sample.Frame = Frames.IsValidIndex(SampleIndex) ? Frames[SampleIndex] : INDEX_NONE;
		Sample.Time = Times.IsValidIndex(SampleIndex) ? Times[SampleIndex] : 0.0;

		if (Target.BlendSpace)
		{
			FString Error;
			if (!EvaluateBlendSpacePoseAtTime(
					Target.BlendSpace, BlendInput, Sample.Time, Options, RequestedBones, Space,
					Sample.BoneNames, Sample.Transforms, Error))
			{
				return MCPError(Error);
			}
			OutSamples.Add(MoveTemp(Sample));
			continue;
		}

		FAnimPose Pose;
		if (Sample.Frame != INDEX_NONE)
		{
			UAnimPoseExtensions::GetAnimPoseAtFrame(Target.Sequence, Sample.Frame, Options, Pose);
		}
		else
		{
			UAnimPoseExtensions::GetAnimPoseAtTime(Target.Sequence, Sample.Time, Options, Pose);
		}
		if (!UAnimPoseExtensions::IsValid(Pose))
		{
			return MCPError(FString::Printf(
				TEXT("Evaluating '%s' produced no pose at %s. The asset may have no bone tracks for its skeleton."),
				*Target.Sequence->GetPathName(),
				Sample.Frame != INDEX_NONE
					? *FString::Printf(TEXT("frame %d"), Sample.Frame)
					: *FString::Printf(TEXT("time %gs"), Sample.Time)));
		}

		if (RequestedBones.Num() > 0)
		{
			Sample.BoneNames = RequestedBones;
		}
		else
		{
			UAnimPoseExtensions::GetBoneNames(Pose, Sample.BoneNames);
		}

		Sample.Transforms.Reset(Sample.BoneNames.Num());
		for (const FName& BoneName : Sample.BoneNames)
		{
			Sample.Transforms.Add(UAnimPoseExtensions::GetBonePose(Pose, BoneName, Space));
		}
		OutSamples.Add(MoveTemp(Sample));
	}

	return nullptr;
}

/** Read the caller's frame list, time list, or neither (= every sampled key). */
TSharedPtr<FJsonValue> ResolvePoseSampleTimes(
	const TSharedPtr<FJsonObject>& Params,
	const FPoseEvaluationTarget& Target,
	TArray<int32>& OutFrames,
	TArray<double>& OutTimes)
{
	const TArray<TSharedPtr<FJsonValue>>* FramesArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* TimesArray = nullptr;
	const bool bHasFrames = TryGetArrayParam(Params, TEXT("frames"), FramesArray) && FramesArray;
	const bool bHasTimes = TryGetArrayParam(Params, TEXT("times"), TimesArray) && TimesArray;

	if (bHasFrames && bHasTimes)
	{
		return MCPError(TEXT("Pass 'frames' or 'times', not both: they name the same samples two different ways."));
	}

	const double FrameRate = Target.SamplingFrameRate.AsDecimal() > 0.0
		? Target.SamplingFrameRate.AsDecimal()
		: 30.0;

	if (bHasFrames)
	{
		if (Target.BlendSpace)
		{
			return MCPError(TEXT("A BlendSpace has no frames of its own. Pass 'times' in seconds instead."));
		}
		for (const TSharedPtr<FJsonValue>& Entry : *FramesArray)
		{
			if (!Entry.IsValid()) continue;
			const int32 Frame = static_cast<int32>(Entry->AsNumber());
			if (Frame < 0 || (Target.NumberOfSampledKeys > 0 && Frame >= Target.NumberOfSampledKeys))
			{
				return MCPError(FString::Printf(
					TEXT("frame %d out of range (asset has %d sampled keys)"), Frame, Target.NumberOfSampledKeys));
			}
			OutFrames.Add(Frame);
			OutTimes.Add(static_cast<double>(Frame) / FrameRate);
		}
		if (OutFrames.Num() == 0)
		{
			return MCPError(TEXT("'frames' was empty"));
		}
		return nullptr;
	}

	if (bHasTimes)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *TimesArray)
		{
			if (!Entry.IsValid()) continue;
			const double Time = FMath::Clamp(Entry->AsNumber(), 0.0, static_cast<double>(Target.PlayLength));
			OutTimes.Add(Time);
		}
		if (OutTimes.Num() == 0)
		{
			return MCPError(TEXT("'times' was empty"));
		}
		return nullptr;
	}

	// Neither given: every sampled key for a sequence, and a frame-rate walk of
	// the play length for a BlendSpace, which has no keys of its own.
	if (Target.BlendSpace)
	{
		const int32 StepCount = FMath::Max(1, FMath::CeilToInt(Target.PlayLength * 30.0f));
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			OutTimes.Add(static_cast<double>(Step) / 30.0);
		}
		return nullptr;
	}

	const int32 KeyCount = FMath::Max(1, Target.NumberOfSampledKeys);
	for (int32 Frame = 0; Frame < KeyCount; ++Frame)
	{
		OutFrames.Add(Frame);
		OutTimes.Add(static_cast<double>(Frame) / FrameRate);
	}
	return nullptr;
}

/** Read boneNames, if given. */
void ReadRequestedBoneNames(const TSharedPtr<FJsonObject>& Params, TArray<FName>& OutBones)
{
	const TArray<TSharedPtr<FJsonValue>>* BonesArray = nullptr;
	if (!TryGetArrayParam(Params, TEXT("boneNames"), BonesArray) || !BonesArray) return;
	for (const TSharedPtr<FJsonValue>& Entry : *BonesArray)
	{
		if (!Entry.IsValid()) continue;
		const FString Name = Entry->AsString();
		if (!Name.IsEmpty()) OutBones.Add(FName(*Name));
	}
}

/** Fill in a BlendSpace's real duration at the requested blend position, so
 *  `times` and the default frame walk are in seconds for every asset type. */
TSharedPtr<FJsonValue> ResolveBlendSpaceLength(FPoseEvaluationTarget& Target, const FVector& BlendInput)
{
	if (!Target.BlendSpace) return nullptr;

	TArray<FBlendSampleData> SampleData;
	int32 CachedTriangulationIndex = INDEX_NONE;
	if (!Target.BlendSpace->GetSamplesFromBlendInput(BlendInput, SampleData, CachedTriangulationIndex, /*bCombineAnimations=*/true)
		|| SampleData.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("BlendSpace '%s' produced no samples at blend position (%g, %g, %g). Check the axis ranges with animation(read_blendspace)."),
			*Target.BlendSpace->GetPathName(), BlendInput.X, BlendInput.Y, BlendInput.Z));
	}

	Target.PlayLength = Target.BlendSpace->GetAnimationLengthFromSampleData(SampleData);
	if (Target.PlayLength <= KINDA_SMALL_NUMBER)
	{
		return MCPError(FString::Printf(
			TEXT("BlendSpace '%s' blends to a zero-length animation at blend position (%g, %g, %g)"),
			*Target.BlendSpace->GetPathName(), BlendInput.X, BlendInput.Y, BlendInput.Z));
	}
	return nullptr;
}

FVector ReadBlendPosition(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* PositionObject = nullptr;
	if (TryGetObjectParam(Params, TEXT("blendPosition"), PositionObject) && PositionObject && (*PositionObject).IsValid())
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		(*PositionObject)->TryGetNumberField(TEXT("x"), X);
		(*PositionObject)->TryGetNumberField(TEXT("y"), Y);
		(*PositionObject)->TryGetNumberField(TEXT("z"), Z);
		return FVector(X, Y, Z);
	}
	return FVector::ZeroVector;
}
}


TSharedPtr<FJsonValue> FAnimationHandlers::SamplePose(const TSharedPtr<FJsonObject>& Params)
{
	// Everything but the asset is read after it resolves (#1057).
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("boneNames"), TEXT("frames"), TEXT("times"), TEXT("space"),
		TEXT("skeletalMeshPath"), TEXT("incorporateRootMotion"), TEXT("blendPosition"),
	});
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FPoseEvaluationTarget Target;
	if (auto Err = ResolvePoseEvaluationTarget(AssetPath, Target)) return Err;

	const FString SpaceToken = OptionalString(Params, TEXT("space"), TEXT("component")).ToLower();
	EAnimPoseSpaces Space = EAnimPoseSpaces::World;
	FString ResolvedSpace;
	if (!ParsePoseSpace(SpaceToken, Space, ResolvedSpace))
	{
		return MCPError(TEXT("space must be 'component' (default), 'local', or 'world'"));
	}

	USkeletalMesh* OptionalMesh = nullptr;
	const FString SkeletalMeshPath = OptionalString(Params, TEXT("skeletalMeshPath"));
	if (!SkeletalMeshPath.IsEmpty())
	{
		OptionalMesh = LoadAssetByPath<USkeletalMesh>(SkeletalMeshPath);
		if (!OptionalMesh)
		{
			return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *SkeletalMeshPath));
		}
	}

	const bool bIncorporateRootMotion = OptionalBool(Params, TEXT("incorporateRootMotion"), true);
	const FAnimPoseEvaluationOptions Options = MakePoseEvaluationOptions(OptionalMesh, bIncorporateRootMotion);

	TArray<FName> RequestedBones;
	ReadRequestedBoneNames(Params, RequestedBones);

	const FVector BlendInput = ReadBlendPosition(Params);
	if (auto Err = ResolveBlendSpaceLength(Target, BlendInput)) return Err;

	TArray<int32> Frames;
	TArray<double> Times;
	if (auto Err = ResolvePoseSampleTimes(Params, Target, Frames, Times)) return Err;

	// Cost gate. A bone count of zero here means "every bone", which is the
	// expensive default, so estimate it from the skeleton before evaluating.
	const int32 BoneCountEstimate = RequestedBones.Num() > 0
		? RequestedBones.Num()
		: (Target.Skeleton ? Target.Skeleton->GetReferenceSkeleton().GetNum() : 0);
	const int64 SampleCount = Frames.Num() > 0 ? Frames.Num() : Times.Num();
	if (BoneCountEstimate > 0 && SampleCount * BoneCountEstimate > PoseSampleTransformCap)
	{
		return MCPError(FString::Printf(
			TEXT("This request would return %lld bone transforms (%lld samples x %d bones), over the %d limit. Narrow it with 'boneNames' or with 'frames' / 'times'."),
			SampleCount * BoneCountEstimate, SampleCount, BoneCountEstimate, PoseSampleTransformCap));
	}

	TArray<FEvaluatedPoseSample> Samples;
	if (auto Err = EvaluatePoseSamples(Target, Frames, Times, RequestedBones, Space, Options, BlendInput, Samples))
	{
		return Err;
	}

	TArray<TSharedPtr<FJsonValue>> SampleObjects;
	SampleObjects.Reserve(Samples.Num());
	for (const FEvaluatedPoseSample& Sample : Samples)
	{
		TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
		if (Sample.Frame != INDEX_NONE) SampleObj->SetNumberField(TEXT("frame"), Sample.Frame);
		SampleObj->SetNumberField(TEXT("time"), Sample.Time);

		TArray<TSharedPtr<FJsonValue>> Bones;
		Bones.Reserve(Sample.BoneNames.Num());
		for (int32 BoneIndex = 0; BoneIndex < Sample.BoneNames.Num(); ++BoneIndex)
		{
			Bones.Add(MakeShared<FJsonValueObject>(
				PoseBoneTransformToJson(Sample.BoneNames[BoneIndex], Sample.Transforms[BoneIndex])));
		}
		SampleObj->SetArrayField(TEXT("bones"), Bones);
		SampleObjects.Add(MakeShared<FJsonValueObject>(SampleObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetType"), Target.AssetType);
	if (Target.Skeleton) Result->SetStringField(TEXT("skeleton"), Target.Skeleton->GetPathName());
	if (OptionalMesh) Result->SetStringField(TEXT("skeletalMeshPath"), OptionalMesh->GetPathName());
	Result->SetNumberField(TEXT("playLength"), Target.PlayLength);
	Result->SetNumberField(TEXT("sampledKeyCount"), Target.NumberOfSampledKeys);
	Result->SetNumberField(TEXT("frameRate"), Target.SamplingFrameRate.AsDecimal());
	Result->SetBoolField(TEXT("hasRootMotion"), Target.bHasRootMotion);
	Result->SetBoolField(TEXT("incorporateRootMotion"), bIncorporateRootMotion);
	Result->SetStringField(TEXT("space"), ResolvedSpace);
	if (ResolvedSpace != SpaceToken)
	{
		Result->SetStringField(TEXT("requestedSpace"), SpaceToken);
		Result->SetStringField(TEXT("spaceResolution"),
			TEXT("An animation asset is authored relative to its skeleton root, so component space is the outermost frame it has and 'world' resolves to it. For a placed character use animation(get_live_bone_transforms)."));
	}
	if (Target.BlendSpace)
	{
		Result->SetObjectField(TEXT("blendPosition"), MCPVec3ToJsonObject(BlendInput));
	}
	Result->SetNumberField(TEXT("sampleCount"), SampleObjects.Num());
	Result->SetNumberField(TEXT("boneCount"), Samples.Num() > 0 ? Samples[0].BoneNames.Num() : 0);
	Result->SetArrayField(TEXT("samples"), SampleObjects);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::MeasureNaturalSpeed(const TSharedPtr<FJsonObject>& Params)
{
	// Everything after footBones is read once the asset resolves (#1057).
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("footBones"), TEXT("contactThreshold"), TEXT("skeletalMeshPath"),
		TEXT("frames"), TEXT("times"), TEXT("blendPosition"),
	});
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	TArray<FName> FootBones;
	{
		const TArray<TSharedPtr<FJsonValue>>* FootArray = nullptr;
		if (!TryGetArrayParam(Params, TEXT("footBones"), FootArray) || !FootArray || FootArray->Num() == 0)
		{
			return MCPError(TEXT("Missing required parameter 'footBones' (array of bone names, e.g. [\"foot_l\",\"foot_r\"])"));
		}
		for (const TSharedPtr<FJsonValue>& Entry : *FootArray)
		{
			if (!Entry.IsValid()) continue;
			const FString Name = Entry->AsString();
			if (!Name.IsEmpty()) FootBones.Add(FName(*Name));
		}
		if (FootBones.Num() == 0)
		{
			return MCPError(TEXT("'footBones' held no usable bone names"));
		}
	}

	FPoseEvaluationTarget Target;
	if (auto Err = ResolvePoseEvaluationTarget(AssetPath, Target)) return Err;

	if (Target.Skeleton)
	{
		const FReferenceSkeleton& RefSkeleton = Target.Skeleton->GetReferenceSkeleton();
		for (const FName& Bone : FootBones)
		{
			if (RefSkeleton.FindBoneIndex(Bone) == INDEX_NONE)
			{
				return MCPError(FString::Printf(
					TEXT("Bone '%s' is not on skeleton '%s'. Use animation(get_skeleton_info) for the bone list."),
					*Bone.ToString(), *Target.Skeleton->GetPathName()));
			}
		}
	}

	USkeletalMesh* OptionalMesh = nullptr;
	const FString SkeletalMeshPath = OptionalString(Params, TEXT("skeletalMeshPath"));
	if (!SkeletalMeshPath.IsEmpty())
	{
		OptionalMesh = LoadAssetByPath<USkeletalMesh>(SkeletalMeshPath);
		if (!OptionalMesh)
		{
			return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *SkeletalMeshPath));
		}
	}

	// Root motion stays out of the pose here on purpose. The measurement asks
	// how far the body travels relative to the ground the foot is planted on,
	// which is what an in-place locomotion clip encodes as backwards foot slide;
	// folding root motion in would move the whole pose with the character and
	// leave a planted foot reading as stationary.
	const FAnimPoseEvaluationOptions Options = MakePoseEvaluationOptions(OptionalMesh, /*bIncorporateRootMotion=*/false);

	const FVector BlendInput = ReadBlendPosition(Params);
	if (auto Err = ResolveBlendSpaceLength(Target, BlendInput)) return Err;

	TArray<int32> Frames;
	TArray<double> Times;
	if (auto Err = ResolvePoseSampleTimes(Params, Target, Frames, Times)) return Err;
	if (Times.Num() < 2)
	{
		return MCPError(TEXT("Measuring speed needs at least two samples; this asset resolved to fewer."));
	}

	TArray<FEvaluatedPoseSample> Samples;
	if (auto Err = EvaluatePoseSamples(Target, Frames, Times, FootBones, EAnimPoseSpaces::World, Options, BlendInput, Samples))
	{
		return Err;
	}

	// Contact threshold: an explicit height in Unreal units, or derived from the
	// clip itself as the lowest foot height plus a margin, which is what makes
	// this work across skeletons of different scale without being told the scale.
	const bool bHasExplicitThreshold = HasParam(Params, TEXT("contactThreshold"));
	double LowestFootHeight = TNumericLimits<double>::Max();
	double HighestFootHeight = -TNumericLimits<double>::Max();
	for (const FEvaluatedPoseSample& Sample : Samples)
	{
		for (const FTransform& Transform : Sample.Transforms)
		{
			LowestFootHeight = FMath::Min(LowestFootHeight, Transform.GetLocation().Z);
			HighestFootHeight = FMath::Max(HighestFootHeight, Transform.GetLocation().Z);
		}
	}
	const double DerivedMargin = FMath::Max(1.0, (HighestFootHeight - LowestFootHeight) * 0.15);
	const double ContactThreshold = bHasExplicitThreshold
		? OptionalNumber(Params, TEXT("contactThreshold"), 0.0)
		: LowestFootHeight + DerivedMargin;

	double PlantedDistance = 0.0;
	double PlantedTime = 0.0;
	double TotalDistance = 0.0;
	double TotalTime = 0.0;
	int32 PlantedIntervalCount = 0;

	TArray<double> PerBoneDistance;
	TArray<double> PerBoneTime;
	PerBoneDistance.SetNumZeroed(FootBones.Num());
	PerBoneTime.SetNumZeroed(FootBones.Num());

	for (int32 SampleIndex = 1; SampleIndex < Samples.Num(); ++SampleIndex)
	{
		const FEvaluatedPoseSample& Previous = Samples[SampleIndex - 1];
		const FEvaluatedPoseSample& Current = Samples[SampleIndex];
		const double DeltaTime = Current.Time - Previous.Time;
		if (DeltaTime <= KINDA_SMALL_NUMBER) continue;
		TotalTime += DeltaTime;

		// The planted foot for this interval is the one lowest to the ground
		// across both ends of it. Ties go to the first, which is stable.
		int32 BestBone = INDEX_NONE;
		double BestHeight = TNumericLimits<double>::Max();
		for (int32 BoneIndex = 0; BoneIndex < FootBones.Num(); ++BoneIndex)
		{
			if (!Previous.Transforms.IsValidIndex(BoneIndex) || !Current.Transforms.IsValidIndex(BoneIndex)) continue;
			const double Height = FMath::Max(
				Previous.Transforms[BoneIndex].GetLocation().Z,
				Current.Transforms[BoneIndex].GetLocation().Z);
			if (Height < BestHeight)
			{
				BestHeight = Height;
				BestBone = BoneIndex;
			}
		}
		if (BestBone == INDEX_NONE) continue;

		const FVector From = Previous.Transforms[BestBone].GetLocation();
		const FVector To = Current.Transforms[BestBone].GetLocation();
		// Horizontal only: a foot rising and falling is not the body travelling.
		const double Distance = FVector2D(To.X - From.X, To.Y - From.Y).Size();
		TotalDistance += Distance;

		if (BestHeight <= ContactThreshold)
		{
			PlantedDistance += Distance;
			PlantedTime += DeltaTime;
			PerBoneDistance[BestBone] += Distance;
			PerBoneTime[BestBone] += DeltaTime;
			++PlantedIntervalCount;
		}
	}

	const double NaturalSpeed = PlantedTime > KINDA_SMALL_NUMBER ? PlantedDistance / PlantedTime : 0.0;
	const double AverageSpeed = TotalTime > KINDA_SMALL_NUMBER ? TotalDistance / TotalTime : 0.0;

	TArray<TSharedPtr<FJsonValue>> PerBone;
	for (int32 BoneIndex = 0; BoneIndex < FootBones.Num(); ++BoneIndex)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), FootBones[BoneIndex].ToString());
		BoneObj->SetNumberField(TEXT("plantedDistance"), PerBoneDistance[BoneIndex]);
		BoneObj->SetNumberField(TEXT("plantedTime"), PerBoneTime[BoneIndex]);
		BoneObj->SetNumberField(TEXT("speed"),
			PerBoneTime[BoneIndex] > KINDA_SMALL_NUMBER ? PerBoneDistance[BoneIndex] / PerBoneTime[BoneIndex] : 0.0);
		PerBone.Add(MakeShared<FJsonValueObject>(BoneObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetType"), Target.AssetType);
	if (Target.Skeleton) Result->SetStringField(TEXT("skeleton"), Target.Skeleton->GetPathName());
	if (OptionalMesh) Result->SetStringField(TEXT("skeletalMeshPath"), OptionalMesh->GetPathName());
	Result->SetNumberField(TEXT("naturalSpeed"), NaturalSpeed);
	Result->SetStringField(TEXT("naturalSpeedUnits"), TEXT("cm/s (Unreal units per second)"));
	Result->SetNumberField(TEXT("averageFootSpeed"), AverageSpeed);
	Result->SetNumberField(TEXT("plantedDistance"), PlantedDistance);
	Result->SetNumberField(TEXT("plantedTime"), PlantedTime);
	Result->SetNumberField(TEXT("plantedIntervalCount"), PlantedIntervalCount);
	Result->SetNumberField(TEXT("intervalCount"), FMath::Max(0, Samples.Num() - 1));
	Result->SetNumberField(TEXT("contactThreshold"), ContactThreshold);
	Result->SetBoolField(TEXT("contactThresholdWasDerived"), !bHasExplicitThreshold);
	Result->SetNumberField(TEXT("lowestFootHeight"), Samples.Num() > 0 ? LowestFootHeight : 0.0);
	Result->SetNumberField(TEXT("highestFootHeight"), Samples.Num() > 0 ? HighestFootHeight : 0.0);
	Result->SetNumberField(TEXT("playLength"), Target.PlayLength);
	Result->SetNumberField(TEXT("sampleCount"), Samples.Num());
	Result->SetBoolField(TEXT("hasRootMotion"), Target.bHasRootMotion);
	Result->SetArrayField(TEXT("footBones"), PerBone);
	if (Target.BlendSpace)
	{
		Result->SetObjectField(TEXT("blendPosition"), MCPVec3ToJsonObject(BlendInput));
	}
	return MCPResult(Result);
}
