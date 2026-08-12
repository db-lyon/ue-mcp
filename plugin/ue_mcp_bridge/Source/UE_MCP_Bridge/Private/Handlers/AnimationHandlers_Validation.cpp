// Deterministic, data-first AnimSequence validation.
//
// Screenshots are useful review evidence, but exact pose samples are the
// source of truth for root motion, seams, bounds and numeric integrity.

#include "AnimationHandlers.h"

#include "HandlerUtils.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "BoneIndices.h"
#include "BonePose.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/MemStack.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TSharedPtr<FJsonObject> AnimQaVectorJson(const FVector& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("x"), Value.X);
		Result->SetNumberField(TEXT("y"), Value.Y);
		Result->SetNumberField(TEXT("z"), Value.Z);
		return Result;
	}

	TSharedPtr<FJsonObject> AnimQaQuatJson(const FQuat& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("x"), Value.X);
		Result->SetNumberField(TEXT("y"), Value.Y);
		Result->SetNumberField(TEXT("z"), Value.Z);
		Result->SetNumberField(TEXT("w"), Value.W);
		return Result;
	}

	TSharedPtr<FJsonObject> AnimQaRotatorJson(const FRotator& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("pitch"), Value.Pitch);
		Result->SetNumberField(TEXT("yaw"), Value.Yaw);
		Result->SetNumberField(TEXT("roll"), Value.Roll);
		return Result;
	}

	TSharedPtr<FJsonObject> AnimQaTransformJson(const FTransform& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("translation"), AnimQaVectorJson(Value.GetTranslation()));
		Result->SetObjectField(TEXT("rotation"), AnimQaQuatJson(Value.GetRotation()));
		Result->SetObjectField(TEXT("rotationDegrees"), AnimQaRotatorJson(Value.Rotator()));
		Result->SetObjectField(TEXT("scale"), AnimQaVectorJson(Value.GetScale3D()));
		return Result;
	}

	bool AnimQaRawTransformIsValid(const FTransform& Value)
	{
		const FVector Translation = Value.GetTranslation();
		const FVector Scale = Value.GetScale3D();
		const FQuat Rotation = Value.GetRotation();
		return !Translation.ContainsNaN()
			&& !Scale.ContainsNaN()
			&& !Rotation.ContainsNaN()
			&& FMath::IsFinite(Translation.X) && FMath::IsFinite(Translation.Y) && FMath::IsFinite(Translation.Z)
			&& FMath::IsFinite(Scale.X) && FMath::IsFinite(Scale.Y) && FMath::IsFinite(Scale.Z)
			&& FMath::IsFinite(Rotation.X) && FMath::IsFinite(Rotation.Y)
			&& FMath::IsFinite(Rotation.Z) && FMath::IsFinite(Rotation.W)
			&& !Scale.IsNearlyZero()
			&& Rotation.SizeSquared() > SMALL_NUMBER;
	}

	bool AnimQaTransformIsFinite(const FTransform& Value)
	{
		return AnimQaRawTransformIsValid(Value)
			&& FMath::Abs(Value.GetRotation().SizeSquared() - 1.0) < 0.01;
	}

	double AnimQaQuatAngleDegrees(const FQuat& A, const FQuat& B)
	{
		const double Dot = FMath::Clamp(FMath::Abs(static_cast<double>(A | B)), 0.0, 1.0);
		return FMath::RadiansToDegrees(2.0 * FMath::Acos(Dot));
	}

	bool AnimQaSerializeObject(const TSharedPtr<FJsonObject>& Object, FString& OutJson, bool bCondensed)
	{
		if (bCondensed)
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
			return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	}

	bool AnimQaResolveOutputDirectory(const FString& Requested, FString& OutDirectory, FString& OutError)
	{
		FString NormalizedRoot = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Codex"), TEXT("AnimationQA")));
		FPaths::NormalizeDirectoryName(NormalizedRoot);

		FString Candidate;
		if (Requested.IsEmpty())
		{
			OutDirectory.Reset();
			return true;
		}
		if (FPaths::IsRelative(Requested))
		{
			FString ProjectRelativeCandidate = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectDir(), Requested));
			FPaths::NormalizeDirectoryName(ProjectRelativeCandidate);
			Candidate = FPaths::IsSamePath(ProjectRelativeCandidate, NormalizedRoot)
				|| FPaths::IsUnderDirectory(ProjectRelativeCandidate, NormalizedRoot)
				? ProjectRelativeCandidate
				: FPaths::Combine(NormalizedRoot, Requested);
		}
		else
		{
			Candidate = Requested;
		}

		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Candidate);
		if (!FPaths::IsSamePath(Candidate, NormalizedRoot)
			&& !FPaths::IsUnderDirectory(Candidate, NormalizedRoot))
		{
			OutError = FString::Printf(
				TEXT("outputDirectory must resolve under '%s'"), *NormalizedRoot);
			return false;
		}

		OutDirectory = Candidate;
		return true;
	}

	void AnimQaAddDefaultBoneIfPresent(
		const FReferenceSkeleton& RefSkeleton,
		const TCHAR* BoneName,
		TArray<int32>& BoneIndices)
	{
		const int32 Index = RefSkeleton.FindBoneIndex(FName(BoneName));
		if (Index != INDEX_NONE)
		{
			BoneIndices.AddUnique(Index);
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationAnalysisOutputDirectoryNormalizationTest,
	"UE.MCP.Animation.Analysis.OutputDirectoryNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnalysisOutputDirectoryNormalizationTest::RunTest(const FString& Parameters)
{
	const FString Leaf = TEXT("throw_production_v003_full");
	const FString QualifiedRequest = FPaths::Combine(
		TEXT("Saved"), TEXT("Codex"), TEXT("AnimationQA"), Leaf);
	FString RootRelativeOutput;
	FString QualifiedOutput;
	FString RootRelativeError;
	FString QualifiedError;
	TestTrue(
		TEXT("a path relative to the AnimationQA root resolves"),
		AnimQaResolveOutputDirectory(Leaf, RootRelativeOutput, RootRelativeError));
	TestTrue(
		TEXT("a Project/Saved-qualified path resolves"),
		AnimQaResolveOutputDirectory(QualifiedRequest, QualifiedOutput, QualifiedError));
	TestTrue(
		TEXT("both forms resolve to the same directory exactly once"),
		FPaths::IsSamePath(RootRelativeOutput, QualifiedOutput));
	return true;
}

#endif

TSharedPtr<FJsonValue> FAnimationHandlers::AnalyzeAnimation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Error = RequireString(Params, TEXT("assetPath"), AssetPath)) return Error;

	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *AssetPath);
	if (!Sequence)
	{
		return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));
	}
	USkeleton* Skeleton = Sequence->GetSkeleton();
	if (!Skeleton)
	{
		return MCPError(TEXT("AnimSequence has no skeleton"));
	}
	if (Sequence->GetAdditiveAnimType() != AAT_None)
	{
		return MCPError(TEXT("Additive AnimSequences are not supported by analyze_animation v1; provide a baked full-pose sequence"));
	}
	const FString SkeletalMeshPath = OptionalString(Params, TEXT("skeletalMeshPath"));
	USkeletalMesh* SkeletalMesh = nullptr;
	if (!SkeletalMeshPath.IsEmpty())
	{
		SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
		if (!SkeletalMesh)
		{
			return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *SkeletalMeshPath));
		}
		if (!SkeletalMesh->GetSkeleton()
			|| !SkeletalMesh->GetSkeleton()->IsCompatibleForEditor(Skeleton))
		{
			return MCPError(FString::Printf(
				TEXT("SkeletalMesh '%s' is not compatible with animation skeleton '%s'"),
				*SkeletalMeshPath,
				*Skeleton->GetPathName()));
		}
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh
		? SkeletalMesh->GetRefSkeleton()
		: Skeleton->GetReferenceSkeleton();
	const int32 BoneCount = RefSkeleton.GetNum();
	if (BoneCount <= 0)
	{
		return MCPError(TEXT("Skeleton has no bones"));
	}

	const IAnimationDataModel* DataModel = Sequence->GetDataModel();
	if (!DataModel)
	{
		return MCPError(TEXT("AnimSequence has no animation data model"));
	}
	const FFrameRate SourceRate = DataModel->GetFrameRate();
	const double SourceRateDecimal = SourceRate.AsDecimal();
	if (!SourceRate.IsValid() || SourceRate.Numerator <= 0 || SourceRate.Denominator <= 0
		|| !FMath::IsFinite(SourceRateDecimal) || SourceRateDecimal <= 0.0)
	{
		return MCPError(TEXT("AnimSequence has an invalid frame rate"));
	}
	const int32 SourceFrameCount = DataModel->GetNumberOfFrames();
	const double DurationSeconds = Sequence->GetPlayLength();
	if (SourceFrameCount < 1 || !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0)
	{
		return MCPError(TEXT("AnimSequence has an invalid duration or frame count"));
	}
	const double RateScale = static_cast<double>(Sequence->RateScale);
	if (!FMath::IsFinite(RateScale))
	{
		return MCPError(TEXT("AnimSequence has an invalid RateScale"));
	}
	const double PlaybackRateMagnitude = FMath::Abs(RateScale);
	const bool bHasEffectiveTiming = PlaybackRateMagnitude > 0.0;
	const double EffectiveDurationSeconds = bHasEffectiveTiming
		? DurationSeconds / PlaybackRateMagnitude
		: 0.0;

	TArray<TSharedPtr<FJsonValue>> NotifyValues;
	NotifyValues.Reserve(Sequence->Notifies.Num());
	for (const FAnimNotifyEvent& NotifyEvent : Sequence->Notifies)
	{
		const double RawTriggerTimeSeconds = static_cast<double>(NotifyEvent.GetTriggerTime());
		if (!FMath::IsFinite(RawTriggerTimeSeconds))
		{
			return MCPError(TEXT("AnimSequence has a notify with an invalid trigger time"));
		}

		TSharedPtr<FJsonObject> NotifyObject = MakeShared<FJsonObject>();
		NotifyObject->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());
		NotifyObject->SetNumberField(TEXT("rawTriggerTimeSeconds"), RawTriggerTimeSeconds);
		if (bHasEffectiveTiming)
		{
			NotifyObject->SetNumberField(
				TEXT("effectiveTriggerTimeSeconds"),
				RawTriggerTimeSeconds / PlaybackRateMagnitude);
		}
		else
		{
			NotifyObject->SetField(TEXT("effectiveTriggerTimeSeconds"), MakeShared<FJsonValueNull>());
		}
		NotifyValues.Add(MakeShared<FJsonValueObject>(NotifyObject));
	}

	TArray<int32> BoneIndices;
	const TArray<TSharedPtr<FJsonValue>>* BoneNamesJson = nullptr;
	if (Params->TryGetArrayField(TEXT("boneNames"), BoneNamesJson))
	{
		for (const TSharedPtr<FJsonValue>& Value : *BoneNamesJson)
		{
			FString BoneName;
			if (!Value.IsValid() || !Value->TryGetString(BoneName))
			{
				return MCPError(TEXT("boneNames must contain only strings"));
			}
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
			if (BoneIndex == INDEX_NONE)
			{
				return MCPError(FString::Printf(TEXT("Bone not found on skeleton: %s"), *BoneName));
			}
			BoneIndices.AddUnique(BoneIndex);
		}
	}
	else
	{
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("root"), BoneIndices);
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("pelvis"), BoneIndices);
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("head"), BoneIndices);
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("hand_l"), BoneIndices);
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("hand_r"), BoneIndices);
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("foot_l"), BoneIndices);
		AnimQaAddDefaultBoneIfPresent(RefSkeleton, TEXT("foot_r"), BoneIndices);
		if (BoneIndices.IsEmpty())
		{
			for (int32 Index = 0; Index < FMath::Min(BoneCount, 16); ++Index)
			{
				BoneIndices.Add(Index);
			}
		}
	}
	if (BoneIndices.IsEmpty())
	{
		return MCPError(TEXT("No bones selected for analysis"));
	}
	if (BoneIndices.Num() > 256)
	{
		return MCPError(TEXT("analyze_animation supports at most 256 selected bones per call"));
	}

	const bool bLoop = OptionalBool(Params, TEXT("loop"), false);
	TArray<int32> Frames;
	const TArray<TSharedPtr<FJsonValue>>* FramesJson = nullptr;
	if (Params->TryGetArrayField(TEXT("frames"), FramesJson))
	{
		if (FramesJson->Num() > 2401)
		{
			return MCPError(TEXT("analyze_animation supports at most 2401 explicit frames per call"));
		}
		for (const TSharedPtr<FJsonValue>& Value : *FramesJson)
		{
			double Number = 0.0;
			if (!Value.IsValid() || !Value->TryGetNumber(Number) || !FMath::IsFinite(Number)
				|| Number < 0.0 || Number > SourceFrameCount
				|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
			{
				return MCPError(FString::Printf(
					TEXT("frames must contain only integers in [0, %d]"), SourceFrameCount));
			}
			Frames.AddUnique(FMath::RoundToInt(Number));
		}
	}
	else
	{
		double RequestedRate = SourceRateDecimal;
		if (Params->HasField(TEXT("sampleRate"))
			&& (!Params->TryGetNumberField(TEXT("sampleRate"), RequestedRate)
				|| !FMath::IsFinite(RequestedRate) || RequestedRate < 1.0 || RequestedRate > 240.0))
		{
			return MCPError(TEXT("'sampleRate' must be a finite number in [1, 240]"));
		}
		const int32 SampleCount = FMath::Clamp(
			FMath::CeilToInt(DurationSeconds * RequestedRate) + 1,
			2,
			2401);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const double Alpha = SampleCount > 1
				? static_cast<double>(SampleIndex) / static_cast<double>(SampleCount - 1)
				: 0.0;
			Frames.AddUnique(FMath::Clamp(FMath::RoundToInt(Alpha * SourceFrameCount), 0, SourceFrameCount));
		}
	}
	if (Frames.IsEmpty())
	{
		return MCPError(TEXT("No frames selected for analysis"));
	}
	if (bLoop)
	{
		Frames.AddUnique(0);
		Frames.AddUnique(SourceFrameCount);
	}
	Frames.Sort();
	if (static_cast<int64>(Frames.Num()) * static_cast<int64>(BoneIndices.Num()) > 100000)
	{
		return MCPError(TEXT("analyze_animation supports at most 100000 bone-frame samples per call"));
	}

	TArray<FBoneIndexType> RequiredBoneIndices;
	RequiredBoneIndices.Reserve(BoneCount);
	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		RequiredBoneIndices.Add(static_cast<FBoneIndexType>(Index));
	}
	FBoneContainer RequiredBones;
	RequiredBones.InitializeTo(
		RequiredBoneIndices,
		UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),
		SkeletalMesh ? static_cast<UObject&>(*SkeletalMesh) : static_cast<UObject&>(*Skeleton));
	const auto CompactFromReferenceIndex = [&RequiredBones, SkeletalMesh](int32 Index)
	{
		return SkeletalMesh
			? RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index))
			: RequiredBones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(Index));
	};
	const auto ReferenceIndexFromCompact = [&RequiredBones, SkeletalMesh](FCompactPoseBoneIndex Index)
	{
		return SkeletalMesh
			? RequiredBones.MakeMeshPoseIndex(Index).GetInt()
			: RequiredBones.GetSkeletonPoseIndexFromCompactPoseIndex(Index).GetInt();
	};

	bool bNumericIntegrity = true;
	int32 InvalidTransformCount = 0;
	FVector BoundsMin(UE_BIG_NUMBER, UE_BIG_NUMBER, UE_BIG_NUMBER);
	FVector BoundsMax(-UE_BIG_NUMBER, -UE_BIG_NUMBER, -UE_BIG_NUMBER);
	FVector FirstRoot = FVector::ZeroVector;
	FVector LastRoot = FVector::ZeroVector;
	FVector PreviousRoot = FVector::ZeroVector;
	double PreviousTime = 0.0;
	double MaxRootSpeed = 0.0;
	bool bHasPreviousRoot = false;
	TArray<FTransform> FirstLocalTransforms;
	TArray<FTransform> LastLocalTransforms;
	TArray<TSharedPtr<FJsonValue>> SampleValues;
	TArray<FString> SampleLines;

	for (int32 Frame : Frames)
	{
		FMemMark FrameMark(FMemStack::Get());
		const double TimeSeconds = FMath::Clamp(
			static_cast<double>(Frame) / SourceRateDecimal,
			0.0,
			DurationSeconds);

		FCompactPose CompactPose;
		CompactPose.SetBoneContainer(&RequiredBones);
		CompactPose.ResetToRefPose();
		FBlendedCurve Curve;
		Curve.InitFrom(RequiredBones);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(CompactPose, Curve, Attributes);
		FAnimExtractContext ExtractContext(TimeSeconds, false);
#if WITH_EDITOR
		ExtractContext.bIgnoreRootLock = true;
#endif
		Sequence->GetAnimationPose(PoseData, ExtractContext);
		for (const FCompactPoseBoneIndex BoneIndex : CompactPose.ForEachBoneIndex())
		{
			if (!AnimQaRawTransformIsValid(CompactPose[BoneIndex]))
			{
				const int32 ReferenceBoneIndex = ReferenceIndexFromCompact(BoneIndex);
				return MCPError(FString::Printf(
					TEXT("AnimSequence produced an invalid raw transform at frame %d for bone %s"),
					Frame,
					*RefSkeleton.GetBoneName(ReferenceBoneIndex).ToString()));
			}
		}
		CompactPose.NormalizeRotations();

		FCSPose<FCompactPose> ComponentPose;
		ComponentPose.InitPose(CompactPose);

		TSharedPtr<FJsonObject> SampleObject = MakeShared<FJsonObject>();
		SampleObject->SetNumberField(TEXT("frame"), Frame);
		SampleObject->SetNumberField(TEXT("timeSeconds"), TimeSeconds);
		SampleObject->SetStringField(TEXT("timeRational"), FString::Printf(
			TEXT("%lld/%d"),
			static_cast<long long>(Frame) * static_cast<long long>(SourceRate.Denominator),
			SourceRate.Numerator));

		TArray<TSharedPtr<FJsonValue>> BoneValues;
		TArray<FTransform> CurrentLocalTransforms;
		CurrentLocalTransforms.Reserve(BoneIndices.Num());
		for (int32 ReferenceBoneIndex : BoneIndices)
		{
			const FCompactPoseBoneIndex CompactIndex = CompactFromReferenceIndex(ReferenceBoneIndex);
			if (CompactIndex == INDEX_NONE)
			{
				return MCPError(FString::Printf(
					TEXT("Bone %s could not be mapped into the evaluated compact pose"),
					*RefSkeleton.GetBoneName(ReferenceBoneIndex).ToString()));
			}

			const FTransform LocalTransform = CompactPose[CompactIndex];
			const FTransform ComponentTransform = ComponentPose.GetComponentSpaceTransform(CompactIndex);
			CurrentLocalTransforms.Add(LocalTransform);
			if (!AnimQaTransformIsFinite(LocalTransform) || !AnimQaTransformIsFinite(ComponentTransform))
			{
				return MCPError(FString::Printf(
					TEXT("AnimSequence produced an invalid evaluated transform at frame %d for bone %s"),
					Frame,
					*RefSkeleton.GetBoneName(ReferenceBoneIndex).ToString()));
			}

			const FVector Position = ComponentTransform.GetTranslation();
			BoundsMin.X = FMath::Min(BoundsMin.X, Position.X);
			BoundsMin.Y = FMath::Min(BoundsMin.Y, Position.Y);
			BoundsMin.Z = FMath::Min(BoundsMin.Z, Position.Z);
			BoundsMax.X = FMath::Max(BoundsMax.X, Position.X);
			BoundsMax.Y = FMath::Max(BoundsMax.Y, Position.Y);
			BoundsMax.Z = FMath::Max(BoundsMax.Z, Position.Z);

			TSharedPtr<FJsonObject> BoneObject = MakeShared<FJsonObject>();
			BoneObject->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(ReferenceBoneIndex).ToString());
			BoneObject->SetNumberField(TEXT("index"), ReferenceBoneIndex);
			BoneObject->SetNumberField(TEXT("parentIndex"), RefSkeleton.GetParentIndex(ReferenceBoneIndex));
			BoneObject->SetObjectField(TEXT("local"), AnimQaTransformJson(LocalTransform));
			BoneObject->SetObjectField(TEXT("component"), AnimQaTransformJson(ComponentTransform));
			BoneValues.Add(MakeShared<FJsonValueObject>(BoneObject));
		}
		SampleObject->SetArrayField(TEXT("bones"), BoneValues);

		const FCompactPoseBoneIndex RootIndex = CompactFromReferenceIndex(0);
		const FTransform RootTransform = ComponentPose.GetComponentSpaceTransform(RootIndex);
		SampleObject->SetObjectField(TEXT("rootComponent"), AnimQaTransformJson(RootTransform));
		const FVector RootPosition = RootTransform.GetTranslation();
		if (!bHasPreviousRoot)
		{
			FirstRoot = RootPosition;
			FirstLocalTransforms = CurrentLocalTransforms;
		}
		else
		{
			const double DeltaSeconds = TimeSeconds - PreviousTime;
			if (DeltaSeconds > SMALL_NUMBER)
			{
				MaxRootSpeed = FMath::Max(
					MaxRootSpeed,
					static_cast<double>(FVector::Distance(RootPosition, PreviousRoot)) / DeltaSeconds);
			}
		}
		bHasPreviousRoot = true;
		PreviousRoot = RootPosition;
		PreviousTime = TimeSeconds;
		LastRoot = RootPosition;
		LastLocalTransforms = CurrentLocalTransforms;

		SampleValues.Add(MakeShared<FJsonValueObject>(SampleObject));
		FString SampleJson;
		if (!AnimQaSerializeObject(SampleObject, SampleJson, true))
		{
			return MCPError(FString::Printf(TEXT("Failed to serialize animation sample at frame %d"), Frame));
		}
		SampleLines.Add(MoveTemp(SampleJson));
	}

	double LoopMaxAngle = 0.0;
	double LoopAngleSquaredSum = 0.0;
	int32 LoopAngleCount = 0;
	const bool bHasRootMotion = Sequence->HasRootMotion();
	if (bLoop && FirstLocalTransforms.Num() == LastLocalTransforms.Num())
	{
		for (int32 Index = 0; Index < FirstLocalTransforms.Num(); ++Index)
		{
			if (bHasRootMotion && BoneIndices[Index] == 0)
			{
				continue;
			}
			const double Angle = AnimQaQuatAngleDegrees(
				FirstLocalTransforms[Index].GetRotation(),
				LastLocalTransforms[Index].GetRotation());
			LoopMaxAngle = FMath::Max(LoopMaxAngle, Angle);
			LoopAngleSquaredSum += Angle * Angle;
			++LoopAngleCount;
		}
	}
	const double LoopRmsAngle = LoopAngleCount > 0
		? FMath::Sqrt(LoopAngleSquaredSum / static_cast<double>(LoopAngleCount))
		: 0.0;
	const double RootDisplacement = FVector::Distance(FirstRoot, LastRoot);
	const double LoopRootTranslationError = bHasRootMotion ? 0.0 : RootDisplacement;
	FAnimExtractContext RootMotionContext;
#if WITH_EDITOR
	RootMotionContext.bIgnoreRootLock = true;
#endif
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	const FTransform CycleRootMotion = Sequence->ExtractRootMotionFromRange(0.0, DurationSeconds, RootMotionContext);
#else
	const FTransform CycleRootMotion = Sequence->ExtractRootMotionFromRange(0.0, DurationSeconds);
#endif
	const bool bLoopWarn = bLoop && (LoopRootTranslationError > 1.0 || LoopMaxAngle > 5.0);

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("status"), !bNumericIntegrity ? TEXT("fail") : (bLoopWarn ? TEXT("warn") : TEXT("pass")));
	Summary->SetStringField(TEXT("confidence"), TEXT("high"));
	Summary->SetBoolField(TEXT("numericIntegrity"), bNumericIntegrity);
	Summary->SetNumberField(TEXT("invalidTransformCount"), InvalidTransformCount);
	Summary->SetBoolField(TEXT("hasRootMotion"), bHasRootMotion);
	Summary->SetNumberField(TEXT("rootDisplacementCm"), RootDisplacement);
	Summary->SetNumberField(TEXT("maxRootSpeedCmPerSecond"), MaxRootSpeed);
	Summary->SetObjectField(TEXT("boundsMinCm"), AnimQaVectorJson(BoundsMin));
	Summary->SetObjectField(TEXT("boundsMaxCm"), AnimQaVectorJson(BoundsMax));
	TSharedPtr<FJsonObject> LoopSummary = MakeShared<FJsonObject>();
	LoopSummary->SetBoolField(TEXT("evaluated"), bLoop);
	LoopSummary->SetBoolField(TEXT("rootExcludedFromPoseSeam"), bHasRootMotion);
	LoopSummary->SetNumberField(TEXT("rootTranslationErrorCm"), bLoop ? LoopRootTranslationError : 0.0);
	LoopSummary->SetNumberField(TEXT("jointAngleRmsDegrees"), bLoop ? LoopRmsAngle : 0.0);
	LoopSummary->SetNumberField(TEXT("jointAngleMaxDegrees"), bLoop ? LoopMaxAngle : 0.0);
	LoopSummary->SetObjectField(TEXT("cycleRootMotion"), AnimQaTransformJson(CycleRootMotion));
	Summary->SetObjectField(TEXT("loopSeam"), LoopSummary);

	FString OutputDirectory;
	FString OutputError;
	if (!AnimQaResolveOutputDirectory(OptionalString(Params, TEXT("outputDirectory")), OutputDirectory, OutputError))
	{
		return MCPError(OutputError);
	}

	TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("schema"), TEXT("ue-mcp://animation-validation/v1"));
	Manifest->SetStringField(TEXT("assetPath"), AssetPath);
	Manifest->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	if (!SkeletalMeshPath.IsEmpty()) Manifest->SetStringField(TEXT("skeletalMeshPath"), SkeletalMeshPath);
	Manifest->SetNumberField(TEXT("durationSeconds"), DurationSeconds);
	Manifest->SetNumberField(TEXT("rateScale"), RateScale);
	if (bHasEffectiveTiming)
	{
		Manifest->SetNumberField(TEXT("effectiveDurationSeconds"), EffectiveDurationSeconds);
	}
	else
	{
		Manifest->SetField(TEXT("effectiveDurationSeconds"), MakeShared<FJsonValueNull>());
	}
	Manifest->SetArrayField(TEXT("notifies"), NotifyValues);
	TSharedPtr<FJsonObject> FrameRateObject = MakeShared<FJsonObject>();
	FrameRateObject->SetNumberField(TEXT("numerator"), SourceRate.Numerator);
	FrameRateObject->SetNumberField(TEXT("denominator"), SourceRate.Denominator);
	Manifest->SetObjectField(TEXT("displayRate"), FrameRateObject);
	Manifest->SetNumberField(TEXT("sourceFrameCount"), SourceFrameCount);
	Manifest->SetNumberField(TEXT("sampleCount"), Frames.Num());
	Manifest->SetBoolField(TEXT("loop"), bLoop);
	Manifest->SetStringField(TEXT("units"), TEXT("centimeters"));
	Manifest->SetStringField(TEXT("handedness"), TEXT("left"));
	Manifest->SetStringField(TEXT("forward"), TEXT("+X"));
	Manifest->SetStringField(TEXT("right"), TEXT("+Y"));
	Manifest->SetStringField(TEXT("up"), TEXT("+Z"));
	Manifest->SetObjectField(TEXT("summary"), Summary);
	Manifest->SetStringField(TEXT("samplesFile"), TEXT("samples.ndjson"));

	FString ManifestPath;
	FString SamplesPath;
	if (!OutputDirectory.IsEmpty())
	{
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			return MCPError(FString::Printf(TEXT("Failed to create output directory: %s"), *OutputDirectory));
		}
		ManifestPath = FPaths::Combine(OutputDirectory, TEXT("manifest.json"));
		SamplesPath = FPaths::Combine(OutputDirectory, TEXT("samples.ndjson"));
		if (IFileManager::Get().FileExists(*ManifestPath) || IFileManager::Get().FileExists(*SamplesPath))
		{
			return MCPError(FString::Printf(
				TEXT("outputDirectory already contains animation validation artifacts: %s"),
				*OutputDirectory));
		}
		const FString ManifestTempPath = ManifestPath + TEXT(".tmp");
		const FString SamplesTempPath = SamplesPath + TEXT(".tmp");
		IFileManager::Get().Delete(*ManifestTempPath, false, true);
		IFileManager::Get().Delete(*SamplesTempPath, false, true);
		FString ManifestJson;
		if (!AnimQaSerializeObject(Manifest, ManifestJson, false))
		{
			return MCPError(TEXT("Failed to serialize animation validation manifest"));
		}
		const FString SamplesJson = FString::Join(SampleLines, TEXT("\n")) + TEXT("\n");
		if (!FFileHelper::SaveStringToFile(
			SamplesJson,
			*SamplesTempPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return MCPError(FString::Printf(TEXT("Failed to write samples: %s"), *SamplesPath));
		}
		if (!FFileHelper::SaveStringToFile(
			ManifestJson,
			*ManifestTempPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IFileManager::Get().Delete(*SamplesTempPath, false, true);
			return MCPError(FString::Printf(TEXT("Failed to write manifest: %s"), *ManifestPath));
		}
		if (!IFileManager::Get().Move(*SamplesPath, *SamplesTempPath, false, true))
		{
			IFileManager::Get().Delete(*ManifestTempPath, false, true);
			IFileManager::Get().Delete(*SamplesTempPath, false, true);
			return MCPError(FString::Printf(TEXT("Failed to commit samples: %s"), *SamplesPath));
		}
		if (!IFileManager::Get().Move(*ManifestPath, *ManifestTempPath, false, true))
		{
			IFileManager::Get().Delete(*ManifestTempPath, false, true);
			IFileManager::Get().Delete(*SamplesPath, false, true);
			return MCPError(FString::Printf(TEXT("Failed to commit manifest: %s"), *ManifestPath));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	if (!SkeletalMeshPath.IsEmpty()) Result->SetStringField(TEXT("skeletalMeshPath"), SkeletalMeshPath);
	Result->SetNumberField(TEXT("durationSeconds"), DurationSeconds);
	Result->SetNumberField(TEXT("rateScale"), RateScale);
	if (bHasEffectiveTiming)
	{
		Result->SetNumberField(TEXT("effectiveDurationSeconds"), EffectiveDurationSeconds);
	}
	else
	{
		Result->SetField(TEXT("effectiveDurationSeconds"), MakeShared<FJsonValueNull>());
	}
	Result->SetArrayField(TEXT("notifies"), NotifyValues);
	Result->SetNumberField(TEXT("sourceFrameCount"), SourceFrameCount);
	Result->SetNumberField(TEXT("sampleCount"), Frames.Num());
	Result->SetObjectField(TEXT("displayRate"), FrameRateObject);
	Result->SetObjectField(TEXT("summary"), Summary);
	Result->SetArrayField(TEXT("samples"), SampleValues);
	if (!OutputDirectory.IsEmpty())
	{
		Result->SetStringField(TEXT("outputDirectory"), OutputDirectory);
		Result->SetStringField(TEXT("manifestPath"), ManifestPath);
		Result->SetStringField(TEXT("samplesPath"), SamplesPath);
	}
	return MCPResult(Result);
}
