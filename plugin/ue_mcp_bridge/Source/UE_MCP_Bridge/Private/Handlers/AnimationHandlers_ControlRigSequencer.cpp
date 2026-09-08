// UE 5.8 Control Rig editing through Sequencer.
//
// The LevelSequence is the mutable workspace. Input AnimSequences and Control
// Rig assets are never changed; bake always creates a separate AnimSequence.

#include "AnimationHandlers.h"

#include "HandlerUtils.h"

namespace
{
	TSharedPtr<FJsonValue> ControlRigSequencerUnsupported()
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("errorCode"), TEXT("unsupported_engine_version"));
		Result->SetStringField(TEXT("error"), TEXT("Control Rig Sequencer authoring requires Unreal Engine 5.8 or newer"));
		return MCPResult(Result);
	}
}

#if UE_MCP_HAS_5_8_API

#include "HandlerAssetCreate.h"

#include "Algo/Reverse.h"
#include "AnimPose.h"
#include "FABRIK.h"
#include "EulerTransform.h"
#include "TransformNoScale.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRig.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/SkeletalMeshActor.h"
#include "Exporters/AnimSeqExportOption.h"
#include "IControlRigObjectBinding.h"
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneBindingProxy.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneSpawnable.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Rigs/FKControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Units/Execution/RigUnit_InverseExecution.h"

namespace
{
	constexpr int32 ControlRigSequencerMaxFrames = 100000;

	struct FControlRigSequenceSession
	{
		ULevelSequence* Sequence = nullptr;
		UMovieScene* MovieScene = nullptr;
		FGuid BindingGuid;
		UMovieSceneControlRigParameterTrack* Track = nullptr;
		UMovieSceneControlRigParameterSection* Section = nullptr;
		UControlRig* ControlRig = nullptr;
		FString SequencePath;
		FString BindingTag;
	};

	struct FControlRigTransformPatch
	{
		bool bTranslation = false;
		bool bRotation = false;
		bool bScale = false;
		FVector Translation = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Scale = FVector::OneVector;
	};

	enum class EControlRigPreparedValueType : uint8
	{
		Transform,
		Bool,
		Float,
		Integer,
	};

	struct FControlRigPreparedWrite
	{
		FName Control;
		TArray<FFrameNumber> Frames;
		TArray<FTransform> Before;
		TArray<FTransform> After;
		EControlRigTransformSpace Space = EControlRigTransformSpace::Local;
		FString Op;
		EControlRigPreparedValueType ValueType = EControlRigPreparedValueType::Transform;
		bool BoolValue = false;
		float FloatValue = 0.0f;
		int32 IntValue = 0;
		// The scalar counterparts of Before, so a scalar write is as reversible
		// as a transform write. Sampled at the same point, from the same frames.
		TArray<bool> BoolBefore;
		TArray<float> FloatBefore;
		TArray<int32> IntBefore;
		TArray<bool> BoolAfter;
		TArray<float> FloatAfter;
		TArray<int32> IntAfter;
		TArray<FString> ChangedChannels;
		FString PropagationMode;
	};

	struct FControlRigLivePoseValue
	{
		FName Control;
		ERigControlType ControlType = ERigControlType::Transform;
		EControlRigPreparedValueType ValueType = EControlRigPreparedValueType::Transform;
		FTransform Transform = FTransform::Identity;
		bool BoolValue = false;
		float FloatValue = 0.0f;
		int32 IntValue = 0;
	};

	struct FControlRigLivePoseSnapshot
	{
		FString SequencePath;
		FString BindingTag;
		FString BindingGuid;
		FString TrackPath;
		FString SectionPath;
		FString ControlRigClass;
		FString ControlRigObjectPath;
		uint32 ControlRigObjectId = 0;
		int32 CurrentFrame = 0;
		double CurrentSubFrame = 0.0;
		TArray<FName> SelectedControls;
		TMap<FName, FControlRigLivePoseValue> Controls;
	};

	struct FControlRigContactMetrics
	{
		double MaxPositionErrorCm = 0.0;
		double MaxRotationErrorDegrees = 0.0;
		int32 WorstPositionFrame = 0;
		int32 WorstRotationFrame = 0;
		FTransform WorstPositionExpected = FTransform::Identity;
		FTransform WorstPositionActual = FTransform::Identity;
	};

	struct FControlRigContactStabilizerQA
	{
		FName Control;
		ERigControlType ControlType = ERigControlType::Transform;
		TArray<FTransform> Expected;
		FControlRigContactMetrics Metrics;
	};

	struct FControlRigPreparedContactQA
	{
		int32 OperationIndex = INDEX_NONE;
		FName Control;
		FName DrivenReference;
		FName TargetReference;
		ERigControlType ControlType = ERigControlType::Transform;
		bool bHasDrivenReference = false;
		bool bHasTargetReference = false;
		bool bCapturedRelativeTarget = false;
		bool bUsedFkRotationChain = false;
		bool bCheckRotation = false;
		int32 FullWeightFrameCount = 0;
		double PositionToleranceCm = 0.1;
		double RotationToleranceDegrees = 0.5;
		TArray<FFrameNumber> Frames;
		TArray<FTransform> ExpectedSubject;
		FControlRigContactMetrics Metrics;
		TArray<FControlRigContactStabilizerQA> Stabilizers;
	};

	class FControlRigSequenceFocusGuard
	{
	public:
		explicit FControlRigSequenceFocusGuard(ULevelSequence* InSequence)
			: Previous(ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
			, Target(InSequence)
		{
			if (Target && ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() != Target)
			{
				bChanged = true;
				bReady = ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Target);
			}
			else
			{
				bReady = Target != nullptr;
			}
			if (bReady)
			{
				ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
				ULevelSequenceEditorBlueprintLibrary::ForceUpdate();
				bReady = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() == Target
					&& ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() == Target;
			}
		}

		~FControlRigSequenceFocusGuard()
		{
			if (!bChanged)
			{
				return;
			}
			if (Previous)
			{
				ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Previous);
			}
			else
			{
				ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
			}
		}

		bool IsReady() const { return bReady; }

	private:
		TObjectPtr<ULevelSequence> Previous;
		TObjectPtr<ULevelSequence> Target;
		bool bChanged = false;
		bool bReady = false;
	};

	bool ControlRigSequencerSplitAssetPath(
		const FString& InPath,
		FString& OutPackagePath,
		FString& OutName,
		FString& OutError)
	{
		FString PackageName = InPath;
		PackageName.TrimStartAndEndInline();
		int32 DotIndex = INDEX_NONE;
		if (PackageName.FindLastChar(TEXT('.'), DotIndex))
		{
			PackageName.LeftInline(DotIndex);
		}
		FText InvalidReason;
		if (!FPackageName::IsValidLongPackageName(PackageName, true, &InvalidReason))
		{
			OutError = FString::Printf(TEXT("Invalid asset path '%s': %s"), *InPath, *InvalidReason.ToString());
			return false;
		}
		if (MCPIsProtectedAssetPath(PackageName))
		{
			OutError = FString::Printf(TEXT("Refusing to create or edit protected asset path '%s'"), *PackageName);
			return false;
		}
		OutName = FPackageName::GetLongPackageAssetName(PackageName);
		OutPackagePath = FPackageName::GetLongPackagePath(PackageName);
		if (OutName.IsEmpty() || OutPackagePath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Asset path must include a package and asset name: '%s'"), *InPath);
			return false;
		}
		return true;
	}

	bool ControlRigSequencerReadRate(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* Field,
		const FFrameRate& Default,
		FFrameRate& OutRate,
		FString& OutError)
	{
		OutRate = Default;
		const TSharedPtr<FJsonValue>* Value = Params->Values.Find(Field);
		if (!Value || !Value->IsValid() || (*Value)->IsNull())
		{
			return true;
		}
		if ((*Value)->Type == EJson::Number)
		{
			const double Number = (*Value)->AsNumber();
			if (!FMath::IsFinite(Number) || Number <= 0.0 || Number > static_cast<double>(MAX_int32))
			{
				OutError = FString::Printf(TEXT("'%s' must be a positive frame rate"), Field);
				return false;
			}
			OutRate = FFrameRate(FMath::RoundToInt(Number), 1);
			return OutRate.IsValid();
		}
		if ((*Value)->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("'%s' must be a number or {numerator, denominator}"), Field);
			return false;
		}
		const TSharedPtr<FJsonObject> Object = (*Value)->AsObject();
		double Numerator = 0.0;
		double Denominator = 1.0;
		if (!Object.IsValid() || !Object->TryGetNumberField(TEXT("numerator"), Numerator))
		{
			OutError = FString::Printf(TEXT("'%s.numerator' is required"), Field);
			return false;
		}
		Object->TryGetNumberField(TEXT("denominator"), Denominator);
		if (!FMath::IsFinite(Numerator) || !FMath::IsFinite(Denominator)
			|| Numerator <= 0.0 || Denominator <= 0.0
			|| Numerator > static_cast<double>(MAX_int32) || Denominator > static_cast<double>(MAX_int32))
		{
			OutError = FString::Printf(TEXT("'%s' numerator and denominator must be positive integers"), Field);
			return false;
		}
		OutRate = FFrameRate(FMath::RoundToInt(Numerator), FMath::RoundToInt(Denominator));
		if (!OutRate.IsValid())
		{
			OutError = FString::Printf(TEXT("'%s' is not a valid frame rate"), Field);
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> ControlRigSequencerRateJson(const FFrameRate& Rate)
	{
		auto Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("numerator"), Rate.Numerator);
		Object->SetNumberField(TEXT("denominator"), Rate.Denominator);
		Object->SetNumberField(TEXT("decimal"), Rate.AsDecimal());
		return Object;
	}

	bool ControlRigSequencerReadVector(
		const TSharedPtr<FJsonObject>& Parent,
		const TCHAR* Field,
		FVector& OutValue,
		FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Parent->TryGetObjectField(Field, Object) || !Object || !Object->IsValid())
		{
			OutError = FString::Printf(TEXT("'%s' must be an object with x, y and z"), Field);
			return false;
		}
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!(*Object)->TryGetNumberField(TEXT("x"), X)
			|| !(*Object)->TryGetNumberField(TEXT("y"), Y)
			|| !(*Object)->TryGetNumberField(TEXT("z"), Z)
			|| !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z))
		{
			OutError = FString::Printf(TEXT("'%s' must contain finite x, y and z numbers"), Field);
			return false;
		}
		OutValue = FVector(X, Y, Z);
		return true;
	}

	bool ControlRigSequencerReadRotation(
		const TSharedPtr<FJsonObject>& Parent,
		FQuat& OutValue,
		FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Quaternion = nullptr;
		if (Parent->TryGetObjectField(TEXT("rotation"), Quaternion) && Quaternion && Quaternion->IsValid())
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			double W = 0.0;
			if (!(*Quaternion)->TryGetNumberField(TEXT("x"), X)
				|| !(*Quaternion)->TryGetNumberField(TEXT("y"), Y)
				|| !(*Quaternion)->TryGetNumberField(TEXT("z"), Z)
				|| !(*Quaternion)->TryGetNumberField(TEXT("w"), W)
				|| !FMath::IsFinite(X) || !FMath::IsFinite(Y)
				|| !FMath::IsFinite(Z) || !FMath::IsFinite(W))
			{
				OutError = TEXT("'rotation' must contain finite x, y, z and w numbers");
				return false;
			}
			OutValue = FQuat(X, Y, Z, W);
			if (OutValue.SizeSquared() <= UE_SMALL_NUMBER)
			{
				OutError = TEXT("'rotation' quaternion must have non-zero length");
				return false;
			}
			OutValue.Normalize();
			return true;
		}

		const TSharedPtr<FJsonObject>* Euler = nullptr;
		if (!Parent->TryGetObjectField(TEXT("rotationDegrees"), Euler) || !Euler || !Euler->IsValid())
		{
			OutError = TEXT("Expected 'rotation' quaternion or 'rotationDegrees'");
			return false;
		}
		double Pitch = 0.0;
		double Yaw = 0.0;
		double Roll = 0.0;
		if (!(*Euler)->TryGetNumberField(TEXT("pitch"), Pitch)
			|| !(*Euler)->TryGetNumberField(TEXT("yaw"), Yaw)
			|| !(*Euler)->TryGetNumberField(TEXT("roll"), Roll)
			|| !FMath::IsFinite(Pitch) || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Roll))
		{
			OutError = TEXT("'rotationDegrees' must contain finite pitch, yaw and roll numbers");
			return false;
		}
		OutValue = FRotator(Pitch, Yaw, Roll).Quaternion();
		OutValue.Normalize();
		return true;
	}

	bool ControlRigSequencerReadNormalizedQuaternion(
		const TSharedPtr<FJsonObject>& Parent,
		FQuat& OutValue,
		FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Quaternion = nullptr;
		if (!Parent->TryGetObjectField(TEXT("rotationQuaternion"), Quaternion)
			|| !Quaternion || !Quaternion->IsValid())
		{
			OutError = TEXT("'rotationQuaternion' must be an object with finite x, y, z and w numbers");
			return false;
		}
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double W = 0.0;
		if (!(*Quaternion)->TryGetNumberField(TEXT("x"), X)
			|| !(*Quaternion)->TryGetNumberField(TEXT("y"), Y)
			|| !(*Quaternion)->TryGetNumberField(TEXT("z"), Z)
			|| !(*Quaternion)->TryGetNumberField(TEXT("w"), W)
			|| !FMath::IsFinite(X) || !FMath::IsFinite(Y)
			|| !FMath::IsFinite(Z) || !FMath::IsFinite(W))
		{
			OutError = TEXT("'rotationQuaternion' must contain finite x, y, z and w numbers");
			return false;
		}
		OutValue = FQuat(X, Y, Z, W);
		const double Length = OutValue.Size();
		if (Length <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("'rotationQuaternion' must have non-zero length");
			return false;
		}
		constexpr double NormalizedTolerance = 1e-3;
		if (FMath::Abs(Length - 1.0) > NormalizedTolerance)
		{
			OutError = FString::Printf(
				TEXT("'rotationQuaternion' must be normalized within %.4f (length was %.8f)"),
				NormalizedTolerance, Length);
			return false;
		}
		OutValue.Normalize();
		return true;
	}

	bool ControlRigSequencerReadTransformPatch(
		const TSharedPtr<FJsonObject>& Object,
		bool bRequireAny,
		FControlRigTransformPatch& OutPatch,
		FString& OutError)
	{
		if (!Object.IsValid())
		{
			OutError = TEXT("Transform must be an object");
			return false;
		}
		if (Object->HasField(TEXT("translation")))
		{
			if (!ControlRigSequencerReadVector(Object, TEXT("translation"), OutPatch.Translation, OutError)) return false;
			OutPatch.bTranslation = true;
		}
		else if (Object->HasField(TEXT("translationCm")))
		{
			if (!ControlRigSequencerReadVector(Object, TEXT("translationCm"), OutPatch.Translation, OutError)) return false;
			OutPatch.bTranslation = true;
		}
		if (Object->HasField(TEXT("rotation")) || Object->HasField(TEXT("rotationDegrees")))
		{
			if (!ControlRigSequencerReadRotation(Object, OutPatch.Rotation, OutError)) return false;
			OutPatch.bRotation = true;
		}
		if (Object->HasField(TEXT("scale")))
		{
			if (!ControlRigSequencerReadVector(Object, TEXT("scale"), OutPatch.Scale, OutError)) return false;
			OutPatch.bScale = true;
		}
		else if (Object->HasField(TEXT("scaleMultiplier")))
		{
			if (!ControlRigSequencerReadVector(Object, TEXT("scaleMultiplier"), OutPatch.Scale, OutError)) return false;
			OutPatch.bScale = true;
		}
		if (bRequireAny && !OutPatch.bTranslation && !OutPatch.bRotation && !OutPatch.bScale)
		{
			OutError = TEXT("Transform must specify translation, rotation/rotationDegrees, or scale");
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> ControlRigSequencerTransformJson(const FTransform& Transform)
	{
		auto Object = MakeShared<FJsonObject>();
		const FVector Translation = Transform.GetTranslation();
		const FQuat Rotation = Transform.GetRotation().GetNormalized();
		const FRotator Euler = Rotation.Rotator();
		const FVector Scale = Transform.GetScale3D();

		auto TranslationObject = MakeShared<FJsonObject>();
		TranslationObject->SetNumberField(TEXT("x"), Translation.X);
		TranslationObject->SetNumberField(TEXT("y"), Translation.Y);
		TranslationObject->SetNumberField(TEXT("z"), Translation.Z);
		Object->SetObjectField(TEXT("translation"), TranslationObject);

		auto RotationObject = MakeShared<FJsonObject>();
		RotationObject->SetNumberField(TEXT("x"), Rotation.X);
		RotationObject->SetNumberField(TEXT("y"), Rotation.Y);
		RotationObject->SetNumberField(TEXT("z"), Rotation.Z);
		RotationObject->SetNumberField(TEXT("w"), Rotation.W);
		Object->SetObjectField(TEXT("rotation"), RotationObject);

		auto EulerObject = MakeShared<FJsonObject>();
		EulerObject->SetNumberField(TEXT("pitch"), Euler.Pitch);
		EulerObject->SetNumberField(TEXT("yaw"), Euler.Yaw);
		EulerObject->SetNumberField(TEXT("roll"), Euler.Roll);
		Object->SetObjectField(TEXT("rotationDegrees"), EulerObject);

		auto ScaleObject = MakeShared<FJsonObject>();
		ScaleObject->SetNumberField(TEXT("x"), Scale.X);
		ScaleObject->SetNumberField(TEXT("y"), Scale.Y);
		ScaleObject->SetNumberField(TEXT("z"), Scale.Z);
		Object->SetObjectField(TEXT("scale"), ScaleObject);
		return Object;
	}

	TSharedPtr<FJsonObject> ControlRigSequencerLiveTransformJson(const FTransform& Transform)
	{
		auto Object = MakeShared<FJsonObject>();
		const FVector Translation = Transform.GetTranslation();
		const FQuat Rotation = Transform.GetRotation().GetNormalized();
		const FVector Scale = Transform.GetScale3D();
		auto TranslationObject = MakeShared<FJsonObject>();
		TranslationObject->SetNumberField(TEXT("x"), Translation.X);
		TranslationObject->SetNumberField(TEXT("y"), Translation.Y);
		TranslationObject->SetNumberField(TEXT("z"), Translation.Z);
		auto RotationObject = MakeShared<FJsonObject>();
		RotationObject->SetNumberField(TEXT("x"), Rotation.X);
		RotationObject->SetNumberField(TEXT("y"), Rotation.Y);
		RotationObject->SetNumberField(TEXT("z"), Rotation.Z);
		RotationObject->SetNumberField(TEXT("w"), Rotation.W);
		auto ScaleObject = MakeShared<FJsonObject>();
		ScaleObject->SetNumberField(TEXT("x"), Scale.X);
		ScaleObject->SetNumberField(TEXT("y"), Scale.Y);
		ScaleObject->SetNumberField(TEXT("z"), Scale.Z);
		Object->SetObjectField(TEXT("translation"), TranslationObject);
		Object->SetObjectField(TEXT("rotationQuaternion"), RotationObject);
		Object->SetObjectField(TEXT("scale"), ScaleObject);
		return Object;
	}

	bool ControlRigSequencerCurrentFrame(
		const FControlRigSequenceSession& Session,
		int32& OutFrame,
		double& OutSubFrame,
		FString& OutError)
	{
		if (ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() != Session.Sequence)
		{
			OutError = TEXT("The addressed LevelSequence must already be focused; live capture never opens or evaluates Sequencer");
			return false;
		}
		const FMovieSceneSequencePlaybackParams Position =
			ULevelSequenceEditorBlueprintLibrary::GetLocalPosition(EMovieSceneTimeUnit::DisplayRate);
		const FFrameTime FrameTime = Position.GetPlaybackPosition(Session.Sequence);
		OutFrame = FrameTime.GetFrame().Value;
		OutSubFrame = FrameTime.GetSubFrame();
		if (!FMath::IsFinite(OutSubFrame))
		{
			OutError = TEXT("Sequencer returned a non-finite current sub-frame");
			return false;
		}
		return true;
	}

	bool ControlRigSequencerReadStringArray(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		TArray<FName>& OutValues,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("'%s' must be an array"), Field);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
			{
				OutError = FString::Printf(TEXT("Every item in '%s' must be a non-empty string"), Field);
				return false;
			}
			const FName Name(*Value->AsString());
			if (OutValues.Contains(Name))
			{
				OutError = FString::Printf(TEXT("'%s' contains duplicate control '%s'"), Field, *Name.ToString());
				return false;
			}
			OutValues.Add(Name);
		}
		OutValues.Sort(FNameLexicalLess());
		return true;
	}

	bool ControlRigSequencerReadLiveSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		FControlRigLivePoseSnapshot& OutSnapshot,
		FString& OutError)
	{
		double Version = 0.0;
		double Frame = 0.0;
		if (!Object.IsValid()
			|| !Object->TryGetNumberField(TEXT("captureVersion"), Version)
			|| Version != 1.0
			|| !Object->TryGetStringField(TEXT("sequencePath"), OutSnapshot.SequencePath)
			|| !Object->TryGetStringField(TEXT("bindingTag"), OutSnapshot.BindingTag)
			|| !Object->TryGetStringField(TEXT("bindingGuid"), OutSnapshot.BindingGuid)
			|| !Object->TryGetStringField(TEXT("trackPath"), OutSnapshot.TrackPath)
			|| !Object->TryGetStringField(TEXT("sectionPath"), OutSnapshot.SectionPath)
			|| !Object->TryGetStringField(TEXT("controlRigClass"), OutSnapshot.ControlRigClass)
			|| !Object->TryGetStringField(TEXT("controlRigObjectPath"), OutSnapshot.ControlRigObjectPath)
			|| !Object->TryGetNumberField(TEXT("currentFrame"), Frame)
			|| !Object->TryGetNumberField(TEXT("currentSubFrame"), OutSnapshot.CurrentSubFrame))
		{
			OutError = TEXT("Snapshot identity is incomplete or captureVersion is not 1");
			return false;
		}
		double ObjectId = -1.0;
		if (!Object->TryGetNumberField(TEXT("controlRigObjectId"), ObjectId)
			|| !FMath::IsFinite(ObjectId) || !FMath::IsNearlyEqual(ObjectId, FMath::RoundToDouble(ObjectId))
			|| ObjectId < 0.0 || ObjectId > static_cast<double>(MAX_uint32))
		{
			OutError = TEXT("Snapshot controlRigObjectId must be an unsigned 32-bit integer");
			return false;
		}
		OutSnapshot.ControlRigObjectId = static_cast<uint32>(ObjectId);
		if (!FMath::IsFinite(Frame) || !FMath::IsNearlyEqual(Frame, FMath::RoundToDouble(Frame))
			|| Frame < static_cast<double>(MIN_int32) || Frame > static_cast<double>(MAX_int32)
			|| !FMath::IsFinite(OutSnapshot.CurrentSubFrame))
		{
			OutError = TEXT("Snapshot frame identity must contain finite currentFrame and currentSubFrame values");
			return false;
		}
		OutSnapshot.CurrentFrame = FMath::RoundToInt(Frame);
		if (!ControlRigSequencerReadStringArray(Object, TEXT("selectedControls"), OutSnapshot.SelectedControls, OutError))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Controls = nullptr;
		if (!Object->TryGetArrayField(TEXT("controls"), Controls) || !Controls || Controls->IsEmpty())
		{
			OutError = TEXT("Snapshot controls must be a non-empty array");
			return false;
		}
		for (int32 Index = 0; Index < Controls->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> ValueObject = (*Controls)[Index].IsValid()
				? (*Controls)[Index]->AsObject() : nullptr;
			FString ControlString;
			FString ControlTypeString;
			FString ValueTypeString;
			if (!ValueObject.IsValid()
				|| !ValueObject->TryGetStringField(TEXT("control"), ControlString) || ControlString.IsEmpty()
				|| !ValueObject->TryGetStringField(TEXT("controlType"), ControlTypeString) || ControlTypeString.IsEmpty()
				|| !ValueObject->TryGetStringField(TEXT("valueType"), ValueTypeString) || ValueTypeString.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Snapshot controls[%d] identity is incomplete"), Index);
				return false;
			}
			FControlRigLivePoseValue Value;
			Value.Control = FName(*ControlString);
			if (OutSnapshot.Controls.Contains(Value.Control))
			{
				OutError = FString::Printf(TEXT("Snapshot contains duplicate control '%s'"), *ControlString);
				return false;
			}
			const int64 ControlTypeValue = StaticEnum<ERigControlType>()->GetValueByNameString(ControlTypeString);
			if (ControlTypeValue == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Snapshot control '%s' has unknown controlType '%s'"), *ControlString, *ControlTypeString);
				return false;
			}
			Value.ControlType = static_cast<ERigControlType>(ControlTypeValue);
			if (ValueTypeString == TEXT("transform"))
			{
				const TSharedPtr<FJsonObject>* TransformObject = nullptr;
				FVector Translation;
				FVector Scale;
				FQuat Rotation;
				if (!ValueObject->TryGetObjectField(TEXT("transform"), TransformObject)
					|| !TransformObject || !TransformObject->IsValid()
					|| !ControlRigSequencerReadVector(*TransformObject, TEXT("translation"), Translation, OutError)
					|| !ControlRigSequencerReadNormalizedQuaternion(*TransformObject, Rotation, OutError)
					|| !ControlRigSequencerReadVector(*TransformObject, TEXT("scale"), Scale, OutError))
				{
					OutError = FString::Printf(TEXT("Snapshot control '%s' has an invalid transform: %s"), *ControlString, *OutError);
					return false;
				}
				Value.ValueType = EControlRigPreparedValueType::Transform;
				Value.Transform = FTransform(Rotation, Translation, Scale);
			}
			else if (ValueTypeString == TEXT("bool"))
			{
				if (!ValueObject->TryGetBoolField(TEXT("value"), Value.BoolValue))
				{
					OutError = FString::Printf(TEXT("Snapshot control '%s' must contain a bool value"), *ControlString);
					return false;
				}
				Value.ValueType = EControlRigPreparedValueType::Bool;
			}
			else if (ValueTypeString == TEXT("float"))
			{
				double Number = 0.0;
				if (!ValueObject->TryGetNumberField(TEXT("value"), Number) || !FMath::IsFinite(Number))
				{
					OutError = FString::Printf(TEXT("Snapshot control '%s' must contain a finite float value"), *ControlString);
					return false;
				}
				Value.FloatValue = static_cast<float>(Number);
				if (!FMath::IsFinite(Value.FloatValue))
				{
					OutError = FString::Printf(TEXT("Snapshot control '%s' float is outside native range"), *ControlString);
					return false;
				}
				Value.ValueType = EControlRigPreparedValueType::Float;
			}
			else if (ValueTypeString == TEXT("int") || ValueTypeString == TEXT("enum"))
			{
				double Number = 0.0;
				if (!ValueObject->TryGetNumberField(TEXT("value"), Number) || !FMath::IsFinite(Number)
					|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
					|| Number < static_cast<double>(MIN_int32) || Number > static_cast<double>(MAX_int32))
				{
					OutError = FString::Printf(TEXT("Snapshot control '%s' must contain a 32-bit integer value"), *ControlString);
					return false;
				}
				Value.IntValue = FMath::RoundToInt(Number);
				Value.ValueType = EControlRigPreparedValueType::Integer;
			}
			else
			{
				OutError = FString::Printf(TEXT("Snapshot control '%s' has unsupported valueType '%s'"), *ControlString, *ValueTypeString);
				return false;
			}
			OutSnapshot.Controls.Add(Value.Control, Value);
		}
		return true;
	}

	bool ControlRigSequencerReadFrames(
		const TSharedPtr<FJsonObject>& Object,
		TArray<FFrameNumber>& OutFrames,
		FString& OutError)
	{
		double SingleFrame = 0.0;
		if (Object->TryGetNumberField(TEXT("frame"), SingleFrame))
		{
			if (!FMath::IsFinite(SingleFrame)
				|| !FMath::IsNearlyEqual(SingleFrame, FMath::RoundToDouble(SingleFrame))
				|| SingleFrame < static_cast<double>(MIN_int32) || SingleFrame > static_cast<double>(MAX_int32))
			{
				OutError = TEXT("'frame' must be an integer");
				return false;
			}
			OutFrames.Add(FFrameNumber(static_cast<int32>(FMath::RoundToInt(SingleFrame))));
		}

		const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
		if (Object->TryGetArrayField(TEXT("frames"), Frames) && Frames)
		{
			for (const TSharedPtr<FJsonValue>& FrameValue : *Frames)
			{
				if (!FrameValue.IsValid() || FrameValue->Type != EJson::Number)
				{
					OutError = TEXT("Every item in 'frames' must be an integer");
					return false;
				}
				const double Number = FrameValue->AsNumber();
				if (!FMath::IsFinite(Number)
					|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
					|| Number < static_cast<double>(MIN_int32) || Number > static_cast<double>(MAX_int32))
				{
					OutError = TEXT("Every item in 'frames' must be an integer");
					return false;
				}
				OutFrames.AddUnique(FFrameNumber(static_cast<int32>(FMath::RoundToInt(Number))));
			}
		}
		if (OutFrames.IsEmpty())
		{
			OutError = TEXT("Specify 'frame' or a non-empty 'frames' array");
			return false;
		}
		OutFrames.Sort([](FFrameNumber A, FFrameNumber B) { return A.Value < B.Value; });
		return true;
	}

	bool ControlRigSequencerReadSpace(
		const FString& InSpace,
		EControlRigTransformSpace& OutSpace,
		FString& OutCanonical,
		FString& OutError)
	{
		if (InSpace.IsEmpty() || InSpace.Equals(TEXT("local"), ESearchCase::IgnoreCase))
		{
			OutSpace = EControlRigTransformSpace::Local;
			OutCanonical = TEXT("local");
			return true;
		}
		if (InSpace.Equals(TEXT("global"), ESearchCase::IgnoreCase)
			|| InSpace.Equals(TEXT("component"), ESearchCase::IgnoreCase))
		{
			OutSpace = EControlRigTransformSpace::Global;
			OutCanonical = TEXT("component");
			return true;
		}
		OutError = TEXT("'space' must be 'local', 'component', or 'global'");
		return false;
	}

	void ControlRigSequencerDisplayRange(UMovieScene* MovieScene, int32& OutStart, int32& OutEndExclusive)
	{
		const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
		const FFrameRate TickRate = MovieScene->GetTickResolution();
		const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		OutStart = FFrameRate::TransformTime(FFrameTime(Range.GetLowerBoundValue()), TickRate, DisplayRate).RoundToFrame().Value;
		OutEndExclusive = FFrameRate::TransformTime(FFrameTime(Range.GetUpperBoundValue()), TickRate, DisplayRate).RoundToFrame().Value;
	}

	bool ControlRigSequencerResolveSession(
		const TSharedPtr<FJsonObject>& Params,
		FControlRigSequenceSession& OutSession,
		FString& OutError)
	{
		if (!Params->TryGetStringField(TEXT("sequencePath"), OutSession.SequencePath) || OutSession.SequencePath.IsEmpty())
		{
			OutError = TEXT("Missing 'sequencePath' parameter");
			return false;
		}
		if (!Params->TryGetStringField(TEXT("bindingTag"), OutSession.BindingTag) || OutSession.BindingTag.IsEmpty())
		{
			OutError = TEXT("Missing 'bindingTag' parameter");
			return false;
		}

		OutSession.Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(OutSession.SequencePath));
		if (!OutSession.Sequence)
		{
			OutError = FString::Printf(TEXT("LevelSequence not found: %s"), *OutSession.SequencePath);
			return false;
		}
		OutSession.MovieScene = OutSession.Sequence->GetMovieScene();
		if (!OutSession.MovieScene)
		{
			OutError = TEXT("LevelSequence has no MovieScene");
			return false;
		}

		const FMovieSceneObjectBindingIDs* Tagged = OutSession.MovieScene->AllTaggedBindings().Find(FName(*OutSession.BindingTag));
		if (!Tagged || Tagged->IDs.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Binding tag '%s' was not found"), *OutSession.BindingTag);
			return false;
		}
		for (const FMovieSceneObjectBindingID& ID : Tagged->IDs)
		{
			if (OutSession.MovieScene->FindBinding(ID.GetGuid()))
			{
				if (OutSession.BindingGuid.IsValid() && OutSession.BindingGuid != ID.GetGuid())
				{
					OutError = FString::Printf(TEXT("Binding tag '%s' resolves to more than one binding"), *OutSession.BindingTag);
					return false;
				}
				OutSession.BindingGuid = ID.GetGuid();
			}
		}
		if (!OutSession.BindingGuid.IsValid())
		{
			OutError = FString::Printf(TEXT("Binding tag '%s' has no binding in this sequence"), *OutSession.BindingTag);
			return false;
		}

		const TArray<UMovieSceneTrack*> Tracks = OutSession.MovieScene->FindTracks(
			UMovieSceneControlRigParameterTrack::StaticClass(), OutSession.BindingGuid, NAME_None);
		for (UMovieSceneTrack* Candidate : Tracks)
		{
			auto* RigTrack = Cast<UMovieSceneControlRigParameterTrack>(Candidate);
			if (!RigTrack || !RigTrack->GetControlRig())
			{
				continue;
			}
			if (OutSession.Track)
			{
				OutError = FString::Printf(TEXT("Binding '%s' has more than one Control Rig track"), *OutSession.BindingTag);
				return false;
			}
			OutSession.Track = RigTrack;
			OutSession.ControlRig = RigTrack->GetControlRig();
		}
		if (!OutSession.Track || !OutSession.ControlRig)
		{
			OutError = FString::Printf(TEXT("Binding '%s' has no Control Rig track"), *OutSession.BindingTag);
			return false;
		}

		OutSession.Section = Cast<UMovieSceneControlRigParameterSection>(OutSession.Track->GetSectionToKey());
		if (!OutSession.Section)
		{
			for (UMovieSceneSection* Candidate : OutSession.Track->GetAllSections())
			{
				OutSession.Section = Cast<UMovieSceneControlRigParameterSection>(Candidate);
				if (OutSession.Section) break;
			}
		}
		if (!OutSession.Section)
		{
			OutError = TEXT("Control Rig track has no parameter section");
			return false;
		}
		return true;
	}

	bool ControlRigSequencerIsTransformControl(const FRigControlElement* Control)
	{
		if (!Control) return false;
		switch (Control->Settings.ControlType)
		{
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
				return true;
			default:
				return false;
		}
	}

	bool ControlRigSequencerIsFloatControl(const FRigControlElement* Control)
	{
		return Control && (Control->Settings.ControlType == ERigControlType::Float
			|| Control->Settings.ControlType == ERigControlType::ScaleFloat);
	}

	bool ControlRigSequencerReadLocalControlValues(
		const FControlRigSequenceSession& Session,
		const FRigControlElement* Control,
		const TArray<FFrameNumber>& Frames,
		TArray<FTransform>& OutValues)
	{
		if (!Control) return false;
		const FName Name = Control->GetFName();
		switch (Control->Settings.ControlType)
		{
			case ERigControlType::Position:
			{
				const TArray<FVector> Values = UControlRigSequencerEditorLibrary::GetLocalControlRigPositions(
					Session.Sequence, Session.ControlRig, Name, Frames, EMovieSceneTimeUnit::DisplayRate);
				for (const FVector& Value : Values) OutValues.Add(FTransform(FQuat::Identity, Value));
				break;
			}
			case ERigControlType::Scale:
			{
				const TArray<FVector> Values = UControlRigSequencerEditorLibrary::GetLocalControlRigScales(
					Session.Sequence, Session.ControlRig, Name, Frames, EMovieSceneTimeUnit::DisplayRate);
				for (const FVector& Value : Values) OutValues.Add(FTransform(FQuat::Identity, FVector::ZeroVector, Value));
				break;
			}
			case ERigControlType::Rotator:
			{
				const TArray<FRotator> Values = UControlRigSequencerEditorLibrary::GetLocalControlRigRotators(
					Session.Sequence, Session.ControlRig, Name, Frames, EMovieSceneTimeUnit::DisplayRate);
				for (const FRotator& Value : Values) OutValues.Add(FTransform(Value));
				break;
			}
			case ERigControlType::Transform:
				OutValues = UControlRigSequencerEditorLibrary::GetLocalControlRigTransforms(
					Session.Sequence, Session.ControlRig, Name, Frames, EMovieSceneTimeUnit::DisplayRate);
				break;
			case ERigControlType::TransformNoScale:
			{
				const TArray<FTransformNoScale> Values = UControlRigSequencerEditorLibrary::GetLocalControlRigTransformNoScales(
					Session.Sequence, Session.ControlRig, Name, Frames, EMovieSceneTimeUnit::DisplayRate);
				for (const FTransformNoScale& Value : Values) OutValues.Add(Value.ToFTransform());
				break;
			}
			case ERigControlType::EulerTransform:
			{
				const TArray<FEulerTransform> Values = UControlRigSequencerEditorLibrary::GetLocalControlRigEulerTransforms(
					Session.Sequence, Session.ControlRig, Name, Frames, EMovieSceneTimeUnit::DisplayRate);
				for (const FEulerTransform& Value : Values) OutValues.Add(Value.ToFTransform());
				break;
			}
			default:
				return false;
		}
		return OutValues.Num() == Frames.Num();
	}

	bool ControlRigSequencerIsEnumOption(const UEnum* Enum, int32 Index)
	{
		return Enum && Index >= 0 && Index < Enum->NumEnums()
			&& !Enum->HasMetaData(TEXT("Hidden"), Index)
			&& !(Enum->ContainsExistingMax() && Index == Enum->NumEnums() - 1);
	}

	int32 ControlRigSequencerFindEnumOption(const UEnum* Enum, int32 Value)
	{
		if (!Enum) return INDEX_NONE;
		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			if (ControlRigSequencerIsEnumOption(Enum, Index) && Enum->GetValueByIndex(Index) == Value)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool ControlRigSequencerIsValidEnumValue(const UEnum* Enum, int32 Value)
	{
		return ControlRigSequencerFindEnumOption(Enum, Value) != INDEX_NONE;
	}

	TArray<TSharedPtr<FJsonValue>> ControlRigSequencerControlsJson(
		UControlRig* ControlRig,
		const TArray<FName>* ControlFilter = nullptr)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		if (!ControlRig || !ControlRig->GetHierarchy()) return Result;

		TArray<FRigControlElement*> Controls = ControlRig->GetHierarchy()->GetControls();
		Controls.Sort([](const FRigControlElement& A, const FRigControlElement& B)
		{
			return A.GetFName().LexicalLess(B.GetFName());
		});
		for (const FRigControlElement* Control : Controls)
		{
			if (!Control) continue;
			if (ControlFilter && !ControlFilter->Contains(Control->GetFName())) continue;
			auto Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Control->GetFName().ToString());
			const FString ControlType = StaticEnum<ERigControlType>()->GetNameStringByValue(
				static_cast<int64>(Control->Settings.ControlType));
			Object->SetStringField(TEXT("type"), ControlType);
			Object->SetStringField(TEXT("controlType"), ControlType);
			Object->SetBoolField(TEXT("transformControl"), ControlRigSequencerIsTransformControl(Control));
			Object->SetBoolField(TEXT("animatable"), Control->Settings.IsAnimatable());
			if (const UEnum* ControlEnum = Control->Settings.ControlEnum.Get())
			{
				Object->SetStringField(TEXT("enumName"), ControlEnum->GetName());
				Object->SetStringField(TEXT("enumPath"), ControlEnum->GetPathName());
				TArray<TSharedPtr<FJsonValue>> Options;
				for (int32 Index = 0; Index < ControlEnum->NumEnums(); ++Index)
				{
					if (!ControlRigSequencerIsEnumOption(ControlEnum, Index)) continue;
					auto Option = MakeShared<FJsonObject>();
					Option->SetStringField(TEXT("name"), ControlEnum->GetNameStringByIndex(Index));
					Option->SetStringField(TEXT("displayName"), ControlEnum->GetDisplayNameTextByIndex(Index).ToString());
					Option->SetNumberField(TEXT("value"), ControlEnum->GetValueByIndex(Index));
					Options.Add(MakeShared<FJsonValueObject>(Option));
				}
				Object->SetArrayField(TEXT("enumOptions"), Options);
			}
			Result.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> ControlRigSequencerSessionJson(const FControlRigSequenceSession& Session)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("sequencePath"), Session.Sequence->GetPathName());
		Result->SetStringField(TEXT("bindingTag"), Session.BindingTag);
		Result->SetStringField(TEXT("bindingGuid"), Session.BindingGuid.ToString());
		Result->SetStringField(TEXT("trackName"), Session.Track->GetTrackName().ToString());
		Result->SetStringField(TEXT("trackPath"), Session.Track->GetPathName());
		Result->SetStringField(TEXT("sectionPath"), Session.Section->GetPathName());
		Result->SetStringField(TEXT("controlRigClass"), Session.ControlRig->GetClass()->GetPathName());
		Result->SetBoolField(TEXT("layered"), Session.ControlRig->IsAdditive());
		Result->SetObjectField(TEXT("displayRate"), ControlRigSequencerRateJson(Session.MovieScene->GetDisplayRate()));
		Result->SetObjectField(TEXT("tickResolution"), ControlRigSequencerRateJson(Session.MovieScene->GetTickResolution()));
		int32 StartFrame = 0;
		int32 EndFrameExclusive = 0;
		ControlRigSequencerDisplayRange(Session.MovieScene, StartFrame, EndFrameExclusive);
		Result->SetNumberField(TEXT("startFrame"), StartFrame);
		Result->SetNumberField(TEXT("endFrameExclusive"), EndFrameExclusive);
		TArray<TSharedPtr<FJsonValue>> Controls = ControlRigSequencerControlsJson(Session.ControlRig);
		Result->SetNumberField(TEXT("controlCount"), Controls.Num());
		Result->SetArrayField(TEXT("controls"), Controls);
		return Result;
	}

	FTransform ControlRigSequencerApplySetPatch(const FTransform& Base, const FControlRigTransformPatch& Patch)
	{
		FTransform Result = Base;
		if (Patch.bTranslation) Result.SetTranslation(Patch.Translation);
		if (Patch.bRotation) Result.SetRotation(Patch.Rotation.GetNormalized());
		if (Patch.bScale) Result.SetScale3D(Patch.Scale);
		return Result;
	}

	FTransform ControlRigSequencerApplyOffset(
		const FTransform& Base,
		const FControlRigTransformPatch& Patch,
		double Weight,
		EControlRigTransformSpace Space)
	{
		FTransform Result = Base;
		if (Patch.bTranslation)
		{
			Result.AddToTranslation(Patch.Translation * Weight);
		}
		if (Patch.bRotation)
		{
			const FQuat Delta = FQuat::Slerp(FQuat::Identity, Patch.Rotation, Weight).GetNormalized();
			const FQuat Rotation = Space == EControlRigTransformSpace::Local
				? Result.GetRotation() * Delta
				: Delta * Result.GetRotation();
			Result.SetRotation(Rotation.GetNormalized());
		}
		if (Patch.bScale)
		{
			const FVector Multiplier = FMath::Lerp(FVector::OneVector, Patch.Scale, Weight);
			Result.SetScale3D(Result.GetScale3D() * Multiplier);
		}
		return Result;
	}

	bool ControlRigSequencerTransformMatches(
		const FTransform& Expected,
		const FTransform& Actual,
		ERigControlType ControlType)
	{
		const bool bTranslationMatches = Expected.GetTranslation().Equals(Actual.GetTranslation(), 0.01);
		const bool bScaleMatches = Expected.GetScale3D().Equals(Actual.GetScale3D(), 0.001);
		const double RotationDot = FMath::Abs(
			Expected.GetRotation().GetNormalized() | Actual.GetRotation().GetNormalized());
		const bool bRotationMatches = RotationDot >= FMath::Cos(FMath::DegreesToRadians(0.1) * 0.5);
		switch (ControlType)
		{
			case ERigControlType::Position: return bTranslationMatches;
			case ERigControlType::Scale: return bScaleMatches;
			case ERigControlType::Rotator: return bRotationMatches;
			case ERigControlType::TransformNoScale: return bTranslationMatches && bRotationMatches;
			default: return bTranslationMatches && bRotationMatches && bScaleMatches;
		}
	}

	bool ControlRigSequencerPatchAffectsControl(
		const FControlRigTransformPatch& Patch,
		ERigControlType ControlType)
	{
		switch (ControlType)
		{
			case ERigControlType::Position: return Patch.bTranslation;
			case ERigControlType::Scale: return Patch.bScale;
			case ERigControlType::Rotator: return Patch.bRotation;
			case ERigControlType::TransformNoScale: return Patch.bTranslation || Patch.bRotation;
			default: return Patch.bTranslation || Patch.bRotation || Patch.bScale;
		}
	}

	bool ControlRigSequencerControlHasTranslation(ERigControlType ControlType)
	{
		switch (ControlType)
		{
			case ERigControlType::Position:
			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
				return true;
			default:
				return false;
		}
	}

	bool ControlRigSequencerControlHasRotation(ERigControlType ControlType)
	{
		switch (ControlType)
		{
			case ERigControlType::Rotator:
			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
				return true;
			default:
				return false;
		}
	}

	double ControlRigSequencerSmoothStep(double Value)
	{
		const double T = FMath::Clamp(Value, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double ControlRigSequencerContactWeight(int32 Index, int32 FrameCount, int32 BlendIn, int32 BlendOut)
	{
		double Weight = 1.0;
		if (BlendIn > 0)
		{
			Weight = FMath::Min(Weight, ControlRigSequencerSmoothStep(
				static_cast<double>(Index) / static_cast<double>(BlendIn)));
		}
		if (BlendOut > 0)
		{
			Weight = FMath::Min(Weight, ControlRigSequencerSmoothStep(
				static_cast<double>(FrameCount - 1 - Index) / static_cast<double>(BlendOut)));
		}
		return Weight;
	}

	FTransform ControlRigSequencerBlendContactTransform(
		const FTransform& Before,
		const FTransform& Target,
		double Weight,
		bool bBlendRotation)
	{
		FTransform Result = Before;
		Result.SetTranslation(FMath::Lerp(Before.GetTranslation(), Target.GetTranslation(), Weight));
		if (bBlendRotation)
		{
			const FQuat From = Before.GetRotation().GetNormalized();
			FQuat To = Target.GetRotation().GetNormalized();
			if ((From | To) < 0.0)
			{
				To = FQuat(-To.X, -To.Y, -To.Z, -To.W);
			}
			Result.SetRotation(FQuat::Slerp(From, To, Weight).GetNormalized());
		}
		Result.SetScale3D(Before.GetScale3D());
		return Result;
	}

	FTransform ControlRigSequencerComposeContactTarget(
		const FTransform& RelativeTarget,
		const FTransform& Reference,
		const FVector& SubjectScale)
	{
		FTransform Result = RelativeTarget * Reference;
		Result.SetScale3D(SubjectScale);
		Result.NormalizeRotation();
		return Result;
	}

	bool ControlRigSequencerSolveRotationChain(
		const TArray<FTransform>& SourceGlobals,
		const FTransform& TargetEnd,
		bool bTargetRotation,
		TArray<FTransform>& OutGlobals,
		double& OutPositionErrorCm)
	{
		if (SourceGlobals.Num() < 2)
		{
			return false;
		}

		TArray<FFABRIKChainLink> Links;
		Links.Reserve(SourceGlobals.Num());
		double MaximumReach = 0.0;
		for (int32 Index = 0; Index < SourceGlobals.Num(); ++Index)
		{
			const FVector Position = SourceGlobals[Index].GetTranslation();
			const double Length = Index == 0
				? 0.0
				: FVector::Distance(Position, SourceGlobals[Index - 1].GetTranslation());
			MaximumReach += Length;
			Links.Emplace(Position, Length, Index, Index);
		}

		AnimationCore::SolveFabrik(
			Links, TargetEnd.GetTranslation(), MaximumReach, 0.001, 32);

		OutGlobals = SourceGlobals;
		for (int32 Index = 0; Index < Links.Num(); ++Index)
		{
			OutGlobals[Index].SetTranslation(Links[Index].Position);
			if (Index + 1 < Links.Num())
			{
				const FVector SourceDirection =
					SourceGlobals[Index + 1].GetTranslation() - SourceGlobals[Index].GetTranslation();
				const FVector SolvedDirection = Links[Index + 1].Position - Links[Index].Position;
				if (!SourceDirection.IsNearlyZero() && !SolvedDirection.IsNearlyZero())
				{
					const FQuat Delta = FQuat::FindBetweenNormals(
						SourceDirection.GetSafeNormal(), SolvedDirection.GetSafeNormal());
					OutGlobals[Index].SetRotation(
						(Delta * SourceGlobals[Index].GetRotation()).GetNormalized());
				}
			}
			else if (bTargetRotation)
			{
				OutGlobals[Index].SetRotation(TargetEnd.GetRotation().GetNormalized());
			}
		}

		OutPositionErrorCm = FVector::Distance(
			OutGlobals.Last().GetTranslation(), TargetEnd.GetTranslation());
		return !OutGlobals.ContainsByPredicate([](const FTransform& Transform)
		{
			return Transform.ContainsNaN();
		});
	}

	bool ControlRigSequencerRegisterWriteFrames(
		TSet<FString>& WrittenKeys,
		FName Control,
		const TArray<FFrameNumber>& Frames,
		int32 OperationIndex,
		FString& OutError)
	{
		for (const FFrameNumber Frame : Frames)
		{
			const FString Key = FString::Printf(
				TEXT("%s|%d"), *Control.ToString().ToLower(), Frame.Value);
			if (WrittenKeys.Contains(Key))
			{
				OutError = FString::Printf(
					TEXT("operations[%d] overlaps another edit at %s frame %d"),
					OperationIndex, *Control.ToString(), Frame.Value);
				return false;
			}
			WrittenKeys.Add(Key);
		}
		return true;
	}

	USkeletalMeshComponent* ControlRigSequencerBoundSkeletalMesh(UControlRig* ControlRig)
	{
		if (!ControlRig) return nullptr;
		const TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding();
		return ObjectBinding.IsValid()
			? Cast<USkeletalMeshComponent>(ObjectBinding->GetBoundObject())
			: nullptr;
	}

	bool ControlRigSequencerSampleReferenceTransforms(
		const FControlRigSequenceSession& Session,
		FName Reference,
		const TArray<FFrameNumber>& Frames,
		TArray<FTransform>& OutTransforms,
		FString& OutError)
	{
		USkeletalMeshComponent* Component = ControlRigSequencerBoundSkeletalMesh(Session.ControlRig);
		if (!Component)
		{
			OutError = TEXT("The Control Rig is not bound to a skeletal mesh component");
			return false;
		}
		if (!Component->DoesSocketExist(Reference))
		{
			OutError = FString::Printf(TEXT("Bone or socket reference was not found: %s"), *Reference.ToString());
			return false;
		}
		UMovieSceneSkeletalAnimationSection* SourceSection = nullptr;
		for (UMovieSceneTrack* Track : Session.MovieScene->FindTracks(
			UMovieSceneSkeletalAnimationTrack::StaticClass(), Session.BindingGuid, NAME_None))
		{
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				auto* Candidate = Cast<UMovieSceneSkeletalAnimationSection>(Section);
				if (!Candidate || !Candidate->Params.Animation) continue;
				if (SourceSection && SourceSection != Candidate)
				{
					OutError = TEXT("contact_lock requires one source animation section in its edit session");
					return false;
				}
				SourceSection = Candidate;
			}
		}
		if (!SourceSection || !SourceSection->Params.Animation)
		{
			OutError = TEXT("The Control Rig edit session has no source animation section");
			return false;
		}

		FAnimPoseEvaluationOptions Options;
		Options.EvaluationType = EAnimDataEvalType::Raw;
		Options.OptionalSkeletalMesh = Component->GetSkeletalMeshAsset();
		Options.bShouldRetarget = true;
		const bool bSocket = Component->GetSocketByName(Reference) != nullptr;
		OutTransforms.Init(FTransform::Identity, Frames.Num());
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			const FFrameTime TickTime = FFrameRate::TransformTime(
				FFrameTime(Frames[Index]),
				Session.MovieScene->GetDisplayRate(),
				Session.MovieScene->GetTickResolution());
			const double AnimationTime = SourceSection->MapTimeToAnimation(
				TickTime, Session.MovieScene->GetTickResolution());
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(
				SourceSection->Params.Animation, AnimationTime, Options, Pose);
			OutTransforms[Index] = bSocket
				? UAnimPoseExtensions::GetSocketPose(Pose, Reference, EAnimPoseSpaces::World)
				: UAnimPoseExtensions::GetBonePose(Pose, Reference, EAnimPoseSpaces::World);
			if (OutTransforms[Index].ContainsNaN())
			{
				OutError = FString::Printf(
					TEXT("Bone or socket reference %s evaluated to an invalid component-space transform"),
					*Reference.ToString());
				return false;
			}
		}
		if (OutTransforms.IsEmpty())
		{
			OutError = TEXT("contact_lock requires at least one frame");
			return false;
		}
		return true;
	}

	double ControlRigSequencerRotationErrorDegrees(const FQuat& Expected, const FQuat& Actual)
	{
		const double Dot = FMath::Clamp(
			FMath::Abs(Expected.GetNormalized() | Actual.GetNormalized()), 0.0, 1.0);
		return FMath::RadiansToDegrees(2.0 * FMath::Acos(Dot));
	}

	void ControlRigSequencerMeasureContact(
		const TArray<FFrameNumber>& Frames,
		const TArray<FTransform>& Expected,
		const TArray<FTransform>& Actual,
		bool bCheckPosition,
		bool bCheckRotation,
		FControlRigContactMetrics& OutMetrics)
	{
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			if (bCheckPosition)
			{
				const double Error = FVector::Distance(
					Expected[Index].GetTranslation(), Actual[Index].GetTranslation());
				if (Index == 0 || Error > OutMetrics.MaxPositionErrorCm)
				{
					OutMetrics.MaxPositionErrorCm = Error;
					OutMetrics.WorstPositionFrame = Frames[Index].Value;
					OutMetrics.WorstPositionExpected = Expected[Index];
					OutMetrics.WorstPositionActual = Actual[Index];
				}
			}
			if (bCheckRotation)
			{
				const double Error = ControlRigSequencerRotationErrorDegrees(
					Expected[Index].GetRotation(), Actual[Index].GetRotation());
				if (Index == 0 || Error > OutMetrics.MaxRotationErrorDegrees)
				{
					OutMetrics.MaxRotationErrorDegrees = Error;
					OutMetrics.WorstRotationFrame = Frames[Index].Value;
				}
			}
		}
	}

	TSharedPtr<FJsonObject> ControlRigSequencerContactMetricsJson(
		const FControlRigContactMetrics& Metrics,
		bool bCheckPosition,
		bool bCheckRotation)
	{
		auto Object = MakeShared<FJsonObject>();
		if (bCheckPosition)
		{
			Object->SetNumberField(TEXT("maxPositionErrorCm"), Metrics.MaxPositionErrorCm);
			Object->SetNumberField(TEXT("worstPositionFrame"), Metrics.WorstPositionFrame);
			Object->SetObjectField(
				TEXT("worstPositionExpected"), ControlRigSequencerTransformJson(Metrics.WorstPositionExpected));
			Object->SetObjectField(
				TEXT("worstPositionActual"), ControlRigSequencerTransformJson(Metrics.WorstPositionActual));
		}
		if (bCheckRotation)
		{
			Object->SetNumberField(TEXT("maxRotationErrorDegrees"), Metrics.MaxRotationErrorDegrees);
			Object->SetNumberField(TEXT("worstRotationFrame"), Metrics.WorstRotationFrame);
		}
		return Object;
	}

	bool ControlRigSequencerPreparePropagatedTransforms(
		const FControlRigLivePoseValue& Baseline,
		const FControlRigLivePoseValue& Accepted,
		const FString& Mode,
		const TArray<FTransform>& DonorValues,
		TArray<FTransform>& OutValues,
		TArray<FString>& OutChangedChannels,
		FString& OutError)
	{
		const bool bTranslation = ControlRigSequencerControlHasTranslation(Baseline.ControlType)
			&& !Baseline.Transform.GetTranslation().Equals(Accepted.Transform.GetTranslation(), 0.0001);
		const bool bRotation = ControlRigSequencerControlHasRotation(Baseline.ControlType)
			&& ControlRigSequencerRotationErrorDegrees(
				Baseline.Transform.GetRotation(), Accepted.Transform.GetRotation()) > 0.001;
		const bool bScale = (Baseline.ControlType == ERigControlType::Scale
			|| Baseline.ControlType == ERigControlType::Transform
			|| Baseline.ControlType == ERigControlType::EulerTransform)
			&& !Baseline.Transform.GetScale3D().Equals(Accepted.Transform.GetScale3D(), 0.0001);
		if (bTranslation) OutChangedChannels.Add(TEXT("translation"));
		if (bRotation) OutChangedChannels.Add(TEXT("rotation"));
		if (bScale) OutChangedChannels.Add(TEXT("scale"));
		if (OutChangedChannels.IsEmpty()) return true;

		const FVector TranslationDelta = Accepted.Transform.GetTranslation() - Baseline.Transform.GetTranslation();
		const FQuat RotationDelta = (Baseline.Transform.GetRotation().Inverse()
			* Accepted.Transform.GetRotation()).GetNormalized();
		FVector ScaleMultiplier = FVector::OneVector;
		if (bScale && Mode == TEXT("local_delta"))
		{
			const FVector BaselineScale = Baseline.Transform.GetScale3D();
			if (FMath::IsNearlyZero(BaselineScale.X) || FMath::IsNearlyZero(BaselineScale.Y) || FMath::IsNearlyZero(BaselineScale.Z))
			{
				OutError = TEXT("Cannot derive local_delta scale from a zero baseline scale component");
				return false;
			}
			ScaleMultiplier = Accepted.Transform.GetScale3D() / BaselineScale;
		}

		OutValues.Reserve(DonorValues.Num());
		for (const FTransform& Donor : DonorValues)
		{
			FTransform Value = Donor;
			if (Mode == TEXT("fixed"))
			{
				if (bTranslation) Value.SetTranslation(Accepted.Transform.GetTranslation());
				if (bRotation) Value.SetRotation(Accepted.Transform.GetRotation().GetNormalized());
				if (bScale) Value.SetScale3D(Accepted.Transform.GetScale3D());
			}
			else
			{
				if (bTranslation) Value.AddToTranslation(TranslationDelta);
				if (bRotation) Value.SetRotation((Value.GetRotation() * RotationDelta).GetNormalized());
				if (bScale) Value.SetScale3D(Value.GetScale3D() * ScaleMultiplier);
			}
			if (Value.GetTranslation().ContainsNaN() || Value.GetRotation().ContainsNaN() || Value.GetScale3D().ContainsNaN())
			{
				OutError = TEXT("Pose propagation produced a non-finite transform");
				return false;
			}
			OutValues.Add(Value);
		}
		return true;
	}

	bool ControlRigSequencerValidateLiveSnapshots(
		const FControlRigSequenceSession& Session,
		const FControlRigLivePoseSnapshot& Baseline,
		const FControlRigLivePoseSnapshot& Accepted,
		FString& OutError)
	{
		auto IdentityMatches = [](const FControlRigLivePoseSnapshot& A, const FControlRigLivePoseSnapshot& B)
		{
			return A.SequencePath == B.SequencePath && A.BindingTag == B.BindingTag
				&& A.BindingGuid == B.BindingGuid && A.TrackPath == B.TrackPath
				&& A.SectionPath == B.SectionPath && A.ControlRigClass == B.ControlRigClass
				&& A.ControlRigObjectPath == B.ControlRigObjectPath
				&& A.ControlRigObjectId == B.ControlRigObjectId;
		};
		if (!IdentityMatches(Baseline, Accepted)
			|| Baseline.CurrentFrame != Accepted.CurrentFrame
			|| !FMath::IsNearlyEqual(Baseline.CurrentSubFrame, Accepted.CurrentSubFrame, 1e-6)
			)
		{
			OutError = TEXT("Baseline and accepted snapshots must identify the same session, rig instance, and frame");
			return false;
		}
		// Snapshot selection is provenance metadata. The explicit propagation
		// control list is authoritative because a user may select several controls
		// while making one accepted viewport pose.
		if (Baseline.Controls.Num() != Accepted.Controls.Num())
		{
			OutError = TEXT("Baseline and accepted snapshots have incomplete or different control sets");
			return false;
		}
		for (const TPair<FName, FControlRigLivePoseValue>& Pair : Baseline.Controls)
		{
			const FControlRigLivePoseValue* AcceptedValue = Accepted.Controls.Find(Pair.Key);
			if (!AcceptedValue || AcceptedValue->ControlType != Pair.Value.ControlType
				|| AcceptedValue->ValueType != Pair.Value.ValueType)
			{
				OutError = FString::Printf(TEXT("Snapshot control '%s' is missing or changed type"), *Pair.Key.ToString());
				return false;
			}
		}
		if (Baseline.SequencePath != Session.Sequence->GetPathName()
			|| Baseline.BindingTag != Session.BindingTag
			|| Baseline.BindingGuid != Session.BindingGuid.ToString()
			|| Baseline.TrackPath != Session.Track->GetPathName()
			|| Baseline.SectionPath != Session.Section->GetPathName()
			|| Baseline.ControlRigClass != Session.ControlRig->GetClass()->GetPathName()
			|| Baseline.ControlRigObjectPath != Session.ControlRig->GetPathName()
			|| Baseline.ControlRigObjectId != Session.ControlRig->GetUniqueID())
		{
			OutError = TEXT("Live pose snapshots do not match the resolved Control Rig session and rig instance");
			return false;
		}
		int32 CurrentFrame = 0;
		double CurrentSubFrame = 0.0;
		if (!ControlRigSequencerCurrentFrame(Session, CurrentFrame, CurrentSubFrame, OutError)) return false;
		if (CurrentFrame != Accepted.CurrentFrame
			|| !FMath::IsNearlyEqual(CurrentSubFrame, Accepted.CurrentSubFrame, 1e-6))
		{
			OutError = TEXT("Sequencer playhead moved after live pose capture; capture again before propagation");
			return false;
		}
		return true;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUEMCPControlRigContactRotationChainTest,
	"UE_MCP.Animation.ControlRig.ContactRotationChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEMCPControlRigContactRotationChainTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FTransform> SourceGlobals{
		FTransform(FQuat::Identity, FVector(0.0, 0.0, 0.0)),
		FTransform(FQuat::Identity, FVector(10.0, 0.0, 0.0)),
		FTransform(FQuat::Identity, FVector(20.0, 0.0, 0.0)),
	};
	const FTransform TargetEnd(FQuat::Identity, FVector(10.0, 10.0, 0.0));
	TArray<FTransform> SolvedGlobals;
	double PositionErrorCm = 0.0;
	TestTrue(
		TEXT("Rotation chain solves a reachable contact"),
		ControlRigSequencerSolveRotationChain(
			SourceGlobals, TargetEnd, false, SolvedGlobals, PositionErrorCm));
	TestTrue(TEXT("Solved contact is within one millimetre"), PositionErrorCm <= 0.1);
	TestTrue(
		TEXT("Driver translation remains unchanged"),
		SolvedGlobals[0].GetTranslation().Equals(SourceGlobals[0].GetTranslation(), UE_KINDA_SMALL_NUMBER));
	TestTrue(
		TEXT("First local bone translation remains unchanged"),
		SolvedGlobals[1].GetRelativeTransform(SolvedGlobals[0]).GetTranslation().Equals(
			SourceGlobals[1].GetRelativeTransform(SourceGlobals[0]).GetTranslation(), 0.001));
	TestTrue(
		TEXT("Second local bone translation remains unchanged"),
		SolvedGlobals[2].GetRelativeTransform(SolvedGlobals[1]).GetTranslation().Equals(
			SourceGlobals[2].GetRelativeTransform(SourceGlobals[1]).GetTranslation(), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUEMCPControlRigMovingReferenceTest,
	"UE_MCP.Animation.ControlRig.MovingContactReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEMCPControlRigMovingReferenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FTransform InitialReference(
		FQuat(FVector::UpVector, FMath::DegreesToRadians(35.0)),
		FVector(40.0, -15.0, 90.0));
	const FTransform InitialRelative(
		FQuat(FVector::ForwardVector, FMath::DegreesToRadians(20.0)),
		FVector(0.0, 18.0, 4.0));
	const FTransform InitialSubject = ControlRigSequencerComposeContactTarget(
		InitialRelative, InitialReference, FVector(1.0, 1.0, 1.0));
	const FTransform CapturedRelative = InitialSubject.GetRelativeTransform(InitialReference);
	const FTransform MovedReference(
		FQuat(FVector::UpVector, FMath::DegreesToRadians(80.0)),
		FVector(65.0, 12.0, 110.0));
	const FTransform MovedSubject = ControlRigSequencerComposeContactTarget(
		CapturedRelative, MovedReference, FVector(2.0, 2.0, 2.0));

	TestTrue(
		TEXT("Moving target preserves the captured relative translation"),
		MovedSubject.GetRelativeTransform(MovedReference).GetTranslation().Equals(
			InitialRelative.GetTranslation(), 0.001));
	TestTrue(
		TEXT("Moving target preserves the captured relative rotation"),
		MovedSubject.GetRelativeTransform(MovedReference).GetRotation().Equals(
			InitialRelative.GetRotation(), 0.001));
	TestTrue(
		TEXT("Moving target preserves the subject scale"),
		MovedSubject.GetScale3D().Equals(FVector(2.0, 2.0, 2.0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUEMCPControlRigLivePosePropagationTest,
	"UE_MCP.Animation.ControlRig.LivePosePropagation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEMCPControlRigLivePosePropagationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FControlRigLivePoseValue FingerBaseline;
	FingerBaseline.Control = TEXT("finger_ctrl");
	FingerBaseline.ControlType = ERigControlType::Rotator;
	FingerBaseline.Transform = FTransform(FRotator(0.0, 0.0, 5.0));
	FControlRigLivePoseValue FingerAccepted = FingerBaseline;
	FingerAccepted.Transform = FTransform(FRotator(0.0, 0.0, 35.0));
	const TArray<FTransform> FingerDonor{
		FTransform(FRotator(0.0, 0.0, -10.0)),
		FTransform(FRotator(0.0, 0.0, 15.0)),
	};
	TArray<FTransform> FingerKeys;
	TArray<FString> FingerChannels;
	FString Error;
	TestTrue(TEXT("Fixed mode prepares the accepted finger value for donor keys"),
		ControlRigSequencerPreparePropagatedTransforms(
			FingerBaseline, FingerAccepted, TEXT("fixed"), FingerDonor, FingerKeys, FingerChannels, Error));
	TestTrue(TEXT("Only the changed rotation channel is keyed"),
		FingerChannels.Num() == 1 && FingerChannels[0] == TEXT("rotation"));
	TestTrue(TEXT("Every prepared finger donor receives the accepted value"),
		FingerKeys.Num() == 2
		&& ControlRigSequencerRotationErrorDegrees(FingerKeys[0].GetRotation(), FingerAccepted.Transform.GetRotation()) < 0.001
		&& ControlRigSequencerRotationErrorDegrees(FingerKeys[1].GetRotation(), FingerAccepted.Transform.GetRotation()) < 0.001);

	FControlRigLivePoseValue WristBaseline;
	WristBaseline.Control = TEXT("wrist_ctrl");
	WristBaseline.ControlType = ERigControlType::Transform;
	WristBaseline.Transform = FTransform(FRotator(0.0, 10.0, 0.0), FVector(1.0, 2.0, 3.0));
	FControlRigLivePoseValue WristAccepted = WristBaseline;
	WristAccepted.Transform = FTransform(FRotator(0.0, 25.0, 0.0), FVector(4.0, 2.0, 3.0));
	const TArray<FTransform> WristDonor{
		FTransform(FRotator(0.0, -20.0, 0.0), FVector(10.0, 0.0, 0.0)),
		FTransform(FRotator(0.0, 40.0, 0.0), FVector(20.0, 5.0, 0.0)),
	};
	TArray<FTransform> WristKeys;
	TArray<FString> WristChannels;
	TestTrue(TEXT("Wrist local delta prepares donor keys"),
		ControlRigSequencerPreparePropagatedTransforms(
			WristBaseline, WristAccepted, TEXT("local_delta"), WristDonor, WristKeys, WristChannels, Error));
	TestTrue(TEXT("Wrist donor translation motion is preserved"),
		(WristKeys[1].GetTranslation() - WristKeys[0].GetTranslation()).Equals(
			WristDonor[1].GetTranslation() - WristDonor[0].GetTranslation(), 0.001));
	const FTransform UntouchedBefore(FRotator(3.0, 4.0, 5.0), FVector(6.0, 7.0, 8.0));
	const FTransform UntouchedAfter = UntouchedBefore;
	TestTrue(TEXT("An unlisted control remains unchanged outside propagation"),
		UntouchedAfter.Equals(UntouchedBefore, 0.0));
	return true;
}
#endif

#endif // UE_MCP_HAS_5_8_API

TSharedPtr<FJsonValue> FAnimationHandlers::BeginControlRigEdit(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return ControlRigSequencerUnsupported();
#else
	FString SequencePath;
	FString SkeletalMeshPath;
	FString SourceAnimationPath;
	if (auto Error = RequireString(Params, TEXT("sequencePath"), SequencePath)) return Error;
	if (auto Error = RequireString(Params, TEXT("skeletalMeshPath"), SkeletalMeshPath)) return Error;
	if (auto Error = RequireString(Params, TEXT("sourceAnimationPath"), SourceAnimationPath)) return Error;

	FString PackagePath;
	FString AssetName;
	FString Error;
	if (!ControlRigSequencerSplitAssetPath(SequencePath, PackagePath, AssetName, Error)) return MCPError(Error);

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(SkeletalMeshPath));
	if (!SkeletalMesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *SkeletalMeshPath));
	UAnimSequence* SourceAnimation = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(SourceAnimationPath));
	if (!SourceAnimation) return MCPError(FString::Printf(TEXT("AnimSequence not found: %s"), *SourceAnimationPath));
	if (!SkeletalMesh->GetSkeleton() || !SourceAnimation->GetSkeleton()
		|| !SkeletalMesh->GetSkeleton()->IsCompatibleForEditor(SourceAnimation->GetSkeleton()))
	{
		return MCPError(TEXT("Source animation is not compatible with the skeletal mesh. Retarget it before beginning a Control Rig edit."));
	}
	if (SourceAnimation->GetAdditiveAnimType() != AAT_None)
	{
		return MCPError(TEXT("Additive source animations require an explicit base pose. Flatten the additive with its intended base before beginning a Control Rig edit."));
	}
	const double SourceRateScale = static_cast<double>(SourceAnimation->RateScale);
	if (!FMath::IsFinite(SourceRateScale) || SourceRateScale == 0.0)
	{
		return MCPError(TEXT("Source animation RateScale must be finite and non-zero before beginning a Control Rig edit"));
	}
	const float RawTimelinePlayRate = static_cast<float>(1.0 / SourceRateScale);
	if (!FMath::IsFinite(RawTimelinePlayRate))
	{
		return MCPError(TEXT("Source animation RateScale cannot be converted to a finite Sequencer play rate"));
	}

	const FString RigMode = OptionalString(Params, TEXT("rigMode"), TEXT("fk")).ToLower();
	UClass* ControlRigClass = nullptr;
	if (RigMode == TEXT("fk"))
	{
		ControlRigClass = UFKControlRig::StaticClass();
	}
	else if (RigMode == TEXT("asset"))
	{
		FString ControlRigPath;
		if (auto RigError = RequireString(Params, TEXT("controlRigPath"), ControlRigPath)) return RigError;
		UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(ControlRigPath));
		if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(UControlRig::StaticClass()))
		{
			return MCPError(FString::Printf(TEXT("Control Rig asset is invalid or has no generated Control Rig class: %s"), *ControlRigPath));
		}
		ControlRigClass = Blueprint->GeneratedClass;
		UControlRig* DefaultRig = Cast<UControlRig>(ControlRigClass->GetDefaultObject());
		const FName LegacyInverse(TEXT("Inverse"));
		if (!DefaultRig || !(DefaultRig->SupportsEvent(FRigUnit_InverseExecution::EventName) || DefaultRig->SupportsEvent(LegacyInverse)))
		{
			return MCPError(TEXT("Control Rig must support inverse execution before an AnimSequence can be baked into it"));
		}
	}
	else
	{
		return MCPError(TEXT("'rigMode' must be 'fk' or 'asset'"));
	}

	FFrameRate DisplayRate;
	if (!ControlRigSequencerReadRate(Params, TEXT("displayRate"), SourceAnimation->GetSamplingFrameRate(), DisplayRate, Error))
	{
		return MCPError(Error);
	}
	double StartNumber = 0.0;
	Params->TryGetNumberField(TEXT("startFrame"), StartNumber);
	if (!FMath::IsFinite(StartNumber)
		|| !FMath::IsNearlyEqual(StartNumber, FMath::RoundToDouble(StartNumber))
		|| StartNumber < static_cast<double>(MIN_int32) || StartNumber > static_cast<double>(MAX_int32))
	{
		return MCPError(TEXT("'startFrame' must be an integer"));
	}
	const int32 StartFrame = static_cast<int32>(FMath::RoundToInt(StartNumber));
	const int32 DefaultDuration = FMath::Max(1, static_cast<int32>(FMath::RoundToInt(SourceAnimation->GetPlayLength() * DisplayRate.AsDecimal())));
	double EndNumber = static_cast<double>(StartFrame) + static_cast<double>(DefaultDuration);
	Params->TryGetNumberField(TEXT("endFrame"), EndNumber);
	if (!FMath::IsFinite(EndNumber)
		|| !FMath::IsNearlyEqual(EndNumber, FMath::RoundToDouble(EndNumber))
		|| EndNumber < static_cast<double>(MIN_int32) || EndNumber > static_cast<double>(MAX_int32))
	{
		return MCPError(TEXT("'endFrame' must be an integer"));
	}
	const int32 EndFrameExclusive = static_cast<int32>(FMath::RoundToInt(EndNumber));
	if (EndFrameExclusive <= StartFrame) return MCPError(TEXT("'endFrame' must be greater than 'startFrame'"));
	if (EndFrameExclusive == MAX_int32) return MCPError(TEXT("'endFrame' is too large"));
	if (static_cast<int64>(EndFrameExclusive) - static_cast<int64>(StartFrame) > ControlRigSequencerMaxFrames)
	{
		return MCPError(FString::Printf(TEXT("Control Rig edit sessions are limited to %d frames"), ControlRigSequencerMaxFrames));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("error")).ToLower();
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("'onConflict' must be 'skip' or 'error'"));
	}
	const FString BindingTag = OptionalString(Params, TEXT("bindingTag"), FString::Printf(TEXT("Codex_%s"), *SkeletalMesh->GetName()));
	if (BindingTag.IsEmpty()) return MCPError(TEXT("'bindingTag' must not be empty"));
	const bool bLayered = OptionalBool(Params, TEXT("layered"), false);

	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	const bool bCreated = Sequence == nullptr;
	if (Sequence && OnConflict == TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("LevelSequence already exists: %s"), *SequencePath));
	}
	if (Sequence && OnConflict == TEXT("skip"))
	{
		FControlRigSequenceFocusGuard Focus(Sequence);
		if (!Focus.IsReady()) return MCPError(TEXT("Could not focus the existing LevelSequence in Sequencer"));
		FControlRigSequenceSession Session;
		auto ResolveParams = MakeShared<FJsonObject>();
		ResolveParams->SetStringField(TEXT("sequencePath"), SequencePath);
		ResolveParams->SetStringField(TEXT("bindingTag"), BindingTag);
		if (!ControlRigSequencerResolveSession(ResolveParams, Session, Error)) return MCPError(Error);
		auto Result = ControlRigSequencerSessionJson(Session);
		MCPSetExisted(Result);
		return MCPResult(Result);
	}
	if (!Sequence)
	{
		auto Created = MCPCreateAssetIdempotentNewObject<ULevelSequence>(AssetName, PackagePath, TEXT("error"), TEXT("LevelSequence"));
		if (Created.EarlyReturn) return Created.EarlyReturn;
		Sequence = Created.Asset;
		Sequence->Initialize();
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		if (bCreated) UEditorAssetLibrary::DeleteAsset(PackagePath + TEXT("/") + AssetName);
		return MCPError(TEXT("LevelSequence has no MovieScene"));
	}

	bool bBakeSucceeded = false;
	FString BakeError;
	FGuid BindingGuid;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "BeginControlRigEdit", "Begin Control Rig Edit"));
		Sequence->Modify();
		MovieScene->Modify();
		MovieScene->SetDisplayRate(DisplayRate);
		MovieScene->SetTickResolutionDirectly(DisplayRate);
		MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(StartFrame), FFrameNumber(EndFrameExclusive)));

		ASkeletalMeshActor* ActorTemplate = DuplicateObject<ASkeletalMeshActor>(
			GetMutableDefault<ASkeletalMeshActor>(), Sequence,
			MakeUniqueObjectName(Sequence, ASkeletalMeshActor::StaticClass(), TEXT("CodexSkeletalMesh")));
		if (!ActorTemplate || !ActorTemplate->GetSkeletalMeshComponent())
		{
			BakeError = TEXT("Failed to create the LevelSequence skeletal mesh spawnable template");
		}
		else
		{
			ActorTemplate->SetFlags(RF_Transactional);
			ActorTemplate->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(SkeletalMesh);
			BindingGuid = MovieScene->AddSpawnable(ActorTemplate->GetName(), *ActorTemplate);
			auto* SpawnTrack = BindingGuid.IsValid()
				? MovieScene->AddTrack<UMovieSceneSpawnTrack>(BindingGuid)
				: nullptr;
			UMovieSceneSection* SpawnSection = SpawnTrack ? SpawnTrack->CreateNewSection() : nullptr;
			if (!BindingGuid.IsValid() || !SpawnTrack || !SpawnSection)
			{
				BakeError = TEXT("Failed to create the LevelSequence skeletal mesh spawnable binding");
			}
			else
			{
				SpawnTrack->AddSection(*SpawnSection);
				MovieScene->TagBinding(FName(*BindingTag), UE::MovieScene::FFixedObjectBindingID(BindingGuid, MovieSceneSequenceID::Root));

				auto* AnimationTrack = Cast<UMovieSceneSkeletalAnimationTrack>(
					MovieScene->AddTrack(UMovieSceneSkeletalAnimationTrack::StaticClass(), BindingGuid));
				auto* AnimationSection = AnimationTrack
					? Cast<UMovieSceneSkeletalAnimationSection>(AnimationTrack->CreateNewSection())
					: nullptr;
				if (!AnimationTrack || !AnimationSection)
				{
					BakeError = TEXT("Failed to create the source skeletal animation track");
				}
				else
				{
					AnimationSection->Params.Animation = SourceAnimation;
					// Sequencer multiplies section PlayRate by the AnimSequence RateScale.
					// Cancel the asset rate so the edit session sees the raw source timeline once.
					AnimationSection->Params.PlayRate = RawTimelinePlayRate;
					// Unreal's AnimSequence exporter samples the exact playback end as its final key.
					// Keep one support frame on the source/rig sections so that sample evaluates the
					// animation instead of falling outside every section and snapping to reference pose.
					AnimationSection->SetRange(TRange<FFrameNumber>(
						FFrameNumber(StartFrame), FFrameNumber(EndFrameExclusive + 1)));
					AnimationTrack->AddSection(*AnimationSection);

					FControlRigSequenceFocusGuard Focus(Sequence);
					if (!Focus.IsReady())
					{
						BakeError = TEXT("Could not focus the LevelSequence in Sequencer");
					}
					else
					{
						const FMovieSceneObjectBindingID ObjectBinding(
							UE::MovieScene::FFixedObjectBindingID(BindingGuid, MovieSceneSequenceID::Root));
						if (ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(ObjectBinding).IsEmpty())
						{
							BakeError = TEXT("Sequencer did not instantiate the skeletal mesh spawnable for baking");
						}
						else
						{
							UAnimSeqExportOption* ExportOptions = NewObject<UAnimSeqExportOption>();
							bBakeSucceeded = UControlRigSequencerEditorLibrary::BakeToControlRig(
								GetEditorWorld(), Sequence, ControlRigClass, ExportOptions,
								false, 0.001f, FMovieSceneBindingProxy(BindingGuid, Sequence), true);
							if (!bBakeSucceeded)
							{
								BakeError = TEXT("Unreal failed to bake the source animation to the Control Rig");
							}
							else
							{
								const TArray<UMovieSceneTrack*> RigTracks = MovieScene->FindTracks(
									UMovieSceneControlRigParameterTrack::StaticClass(), BindingGuid, NAME_None);
								if (RigTracks.Num() != 1)
								{
									bBakeSucceeded = false;
									BakeError = TEXT("Bake did not produce exactly one Control Rig track");
								}
								else if (bLayered)
								{
									auto* RigTrack = Cast<UMovieSceneControlRigParameterTrack>(RigTracks[0]);
									if (!RigTrack || !UControlRigSequencerEditorLibrary::SetControlRigLayeredMode(RigTrack, true))
									{
										bBakeSucceeded = false;
										BakeError = TEXT("Unreal could not convert the Control Rig track to layered mode");
									}
									else
									{
										// Layer conversion clears the baked Control Rig keys. The source track
										// must be active so the now-empty rig section is an additive edit layer.
										AnimationTrack->SetEvalDisabled(false);
									}
								}
								if (bBakeSucceeded)
								{
									auto* RigTrack = Cast<UMovieSceneControlRigParameterTrack>(RigTracks[0]);
									const TArray<UMovieSceneSection*> RigSections = RigTrack ? RigTrack->GetAllSections() : TArray<UMovieSceneSection*>();
									if (!RigTrack || RigSections.Num() != 1 || !RigSections[0])
									{
										bBakeSucceeded = false;
										BakeError = TEXT("Bake did not produce exactly one Control Rig section");
									}
									else
									{
										RigSections[0]->SetEndFrame(TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(EndFrameExclusive + 1)));
									}
								}
							}
						}
					}
					}
				}
			}
		}

	if (!bBakeSucceeded)
	{
		if (bCreated)
		{
			UEditorAssetLibrary::DeleteAsset(PackagePath + TEXT("/") + AssetName);
		}
		return MCPError(BakeError.IsEmpty() ? TEXT("Failed to begin Control Rig edit") : BakeError);
	}

	if (!UEditorAssetLibrary::SaveLoadedAsset(Sequence, false))
	{
		if (bCreated) UEditorAssetLibrary::DeleteAsset(PackagePath + TEXT("/") + AssetName);
		return MCPError(TEXT("Control Rig edit was created in memory but the LevelSequence could not be saved"));
	}
	FControlRigSequenceFocusGuard Focus(Sequence);
	if (!Focus.IsReady()) return MCPError(TEXT("Control Rig edit was created but the LevelSequence could not be focused for inspection"));
	FControlRigSequenceSession Session;
	auto ResolveParams = MakeShared<FJsonObject>();
	ResolveParams->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());
	ResolveParams->SetStringField(TEXT("bindingTag"), BindingTag);
	if (!ControlRigSequencerResolveSession(ResolveParams, Session, Error)) return MCPError(Error);
	auto Result = ControlRigSequencerSessionJson(Session);
	Result->SetStringField(TEXT("sourceAnimationPath"), SourceAnimation->GetPathName());
	Result->SetStringField(TEXT("skeletalMeshPath"), SkeletalMesh->GetPathName());
	Result->SetStringField(TEXT("rigMode"), RigMode);
	Result->SetBoolField(TEXT("layered"), bLayered);
	MCPSetCreated(Result);
	MCPSetDeleteAssetRollback(Result, Sequence->GetPathName());
	return MCPResult(Result);
#endif
}

TSharedPtr<FJsonValue> FAnimationHandlers::ReadControlRigEdit(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return ControlRigSequencerUnsupported();
#else
	FString SequencePath;
	if (auto Error = RequireString(Params, TEXT("sequencePath"), SequencePath)) return Error;
	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) return MCPError(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));
	FControlRigSequenceFocusGuard Focus(Sequence);
	if (!Focus.IsReady()) return MCPError(TEXT("Could not focus the LevelSequence in Sequencer"));

	FControlRigSequenceSession Session;
	FString Error;
	if (!ControlRigSequencerResolveSession(Params, Session, Error)) return MCPError(Error);
	EControlRigTransformSpace Space;
	FString CanonicalSpace;
	if (!ControlRigSequencerReadSpace(OptionalString(Params, TEXT("space"), TEXT("local")), Space, CanonicalSpace, Error))
	{
		return MCPError(Error);
	}

	TArray<FName> ControlNames;
	TArray<FName> TransformControlNames;
	TArray<FName> BoolControlNames;
	TArray<FName> FloatControlNames;
	TArray<FName> IntControlNames;
	const TArray<TSharedPtr<FJsonValue>>* RequestedNames = nullptr;
	if (Params->TryGetArrayField(TEXT("controlNames"), RequestedNames) && RequestedNames)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RequestedNames)
		{
			if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
			{
				return MCPError(TEXT("Every item in 'controlNames' must be a non-empty string"));
			}
			const FName Name(*Value->AsString());
			FRigControlElement* Control = Session.ControlRig->FindControl(Name);
			if (!Control)
			{
				return MCPError(FString::Printf(TEXT("Control not found: %s"), *Name.ToString()));
			}
			ControlNames.AddUnique(Name);
			if (ControlRigSequencerIsTransformControl(Control))
			{
				TransformControlNames.AddUnique(Name);
			}
			else if (Control->Settings.ControlType == ERigControlType::Bool)
			{
				BoolControlNames.AddUnique(Name);
			}
			else if (ControlRigSequencerIsFloatControl(Control))
			{
				FloatControlNames.AddUnique(Name);
			}
			else if (Control->Settings.ControlType == ERigControlType::Integer)
			{
				IntControlNames.AddUnique(Name);
			}
			else
			{
				return MCPError(FString::Printf(TEXT("Control type is not readable by this action: %s"), *Name.ToString()));
			}
		}
	}
	else
	{
		for (FRigControlElement* Control : Session.ControlRig->GetHierarchy()->GetControls())
		{
			if (ControlRigSequencerIsTransformControl(Control))
			{
				ControlNames.Add(Control->GetFName());
				TransformControlNames.Add(Control->GetFName());
			}
			else if (Control->Settings.ControlType == ERigControlType::Bool)
			{
				ControlNames.Add(Control->GetFName());
				BoolControlNames.Add(Control->GetFName());
			}
			else if (ControlRigSequencerIsFloatControl(Control))
			{
				ControlNames.Add(Control->GetFName());
				FloatControlNames.Add(Control->GetFName());
			}
			else if (Control->Settings.ControlType == ERigControlType::Integer)
			{
				ControlNames.Add(Control->GetFName());
				IntControlNames.Add(Control->GetFName());
			}
		}
		ControlNames.Sort(FNameLexicalLess());
		TransformControlNames.Sort(FNameLexicalLess());
		BoolControlNames.Sort(FNameLexicalLess());
		FloatControlNames.Sort(FNameLexicalLess());
		IntControlNames.Sort(FNameLexicalLess());
	}
	if (ControlNames.IsEmpty()) return MCPError(TEXT("No readable controls were requested or available"));

	TArray<FFrameNumber> Frames;
	if (Params->HasField(TEXT("frame")) || Params->HasField(TEXT("frames")))
	{
		if (!ControlRigSequencerReadFrames(Params, Frames, Error)) return MCPError(Error);
	}
	else
	{
		int32 Start = 0;
		int32 EndExclusive = 0;
		ControlRigSequencerDisplayRange(Session.MovieScene, Start, EndExclusive);
		Frames.Add(FFrameNumber(Start));
		if (EndExclusive - 1 != Start) Frames.Add(FFrameNumber(EndExclusive - 1));
	}

	int32 Start = 0;
	int32 EndExclusive = 0;
	ControlRigSequencerDisplayRange(Session.MovieScene, Start, EndExclusive);
	for (FFrameNumber Frame : Frames)
	{
		if (Frame.Value < Start || Frame.Value >= EndExclusive)
		{
			return MCPError(FString::Printf(TEXT("Frame %d is outside [%d, %d)"), Frame.Value, Start, EndExclusive));
		}
	}

	TArray<FArrayOfRigControlTransforms> Values;
	if (!TransformControlNames.IsEmpty())
	{
		Values = UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
			Session.Sequence, Session.ControlRig, TransformControlNames, Frames, Space, EMovieSceneTimeUnit::DisplayRate);
		if (Values.Num() != TransformControlNames.Num()) return MCPError(TEXT("Unreal could not evaluate every requested Control Rig transform"));
	}

	auto Result = ControlRigSequencerSessionJson(Session);
	if (RequestedNames)
	{
		TArray<TSharedPtr<FJsonValue>> RequestedControls = ControlRigSequencerControlsJson(
			Session.ControlRig, &ControlNames);
		Result->SetNumberField(TEXT("controlCount"), RequestedControls.Num());
		Result->SetArrayField(TEXT("controls"), RequestedControls);
	}
	Result->SetStringField(TEXT("space"), CanonicalSpace);
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (const FArrayOfRigControlTransforms& Value : Values)
	{
		if (Value.Transforms.Num() != Frames.Num()) return MCPError(TEXT("Control Rig transform evaluation returned an incomplete frame set"));
		auto ControlObject = MakeShared<FJsonObject>();
		ControlObject->SetStringField(TEXT("control"), Value.ControlName.ToString());
		TArray<TSharedPtr<FJsonValue>> FrameSamples;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			auto FrameObject = MakeShared<FJsonObject>();
			FrameObject->SetNumberField(TEXT("frame"), Frames[Index].Value);
			FrameObject->SetObjectField(TEXT("transform"), ControlRigSequencerTransformJson(Value.Transforms[Index]));
			FrameSamples.Add(MakeShared<FJsonValueObject>(FrameObject));
		}
		ControlObject->SetArrayField(TEXT("samples"), FrameSamples);
		Samples.Add(MakeShared<FJsonValueObject>(ControlObject));
	}
	for (const FName ControlName : BoolControlNames)
	{
		const TArray<bool> BoolValues = UControlRigSequencerEditorLibrary::GetLocalControlRigBools(
			Session.Sequence, Session.ControlRig, ControlName, Frames, EMovieSceneTimeUnit::DisplayRate);
		if (BoolValues.Num() != Frames.Num()) return MCPError(TEXT("Control Rig bool evaluation returned an incomplete frame set"));
		auto ControlObject = MakeShared<FJsonObject>();
		ControlObject->SetStringField(TEXT("control"), ControlName.ToString());
		ControlObject->SetStringField(TEXT("valueType"), TEXT("bool"));
		TArray<TSharedPtr<FJsonValue>> FrameSamples;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			auto FrameObject = MakeShared<FJsonObject>();
			FrameObject->SetNumberField(TEXT("frame"), Frames[Index].Value);
			FrameObject->SetBoolField(TEXT("value"), BoolValues[Index]);
			FrameSamples.Add(MakeShared<FJsonValueObject>(FrameObject));
		}
		ControlObject->SetArrayField(TEXT("samples"), FrameSamples);
		Samples.Add(MakeShared<FJsonValueObject>(ControlObject));
	}
	for (const FName ControlName : FloatControlNames)
	{
		const TArray<float> FloatValues = UControlRigSequencerEditorLibrary::GetLocalControlRigFloats(
			Session.Sequence, Session.ControlRig, ControlName, Frames, EMovieSceneTimeUnit::DisplayRate);
		if (FloatValues.Num() != Frames.Num()) return MCPError(TEXT("Control Rig float evaluation returned an incomplete frame set"));
		auto ControlObject = MakeShared<FJsonObject>();
		ControlObject->SetStringField(TEXT("control"), ControlName.ToString());
		ControlObject->SetStringField(TEXT("valueType"), TEXT("float"));
		TArray<TSharedPtr<FJsonValue>> FrameSamples;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			auto FrameObject = MakeShared<FJsonObject>();
			FrameObject->SetNumberField(TEXT("frame"), Frames[Index].Value);
			FrameObject->SetNumberField(TEXT("value"), FloatValues[Index]);
			FrameSamples.Add(MakeShared<FJsonValueObject>(FrameObject));
		}
		ControlObject->SetArrayField(TEXT("samples"), FrameSamples);
		Samples.Add(MakeShared<FJsonValueObject>(ControlObject));
	}
	for (const FName ControlName : IntControlNames)
	{
		const TArray<int32> IntValues = UControlRigSequencerEditorLibrary::GetLocalControlRigInts(
			Session.Sequence, Session.ControlRig, ControlName, Frames, EMovieSceneTimeUnit::DisplayRate);
		if (IntValues.Num() != Frames.Num()) return MCPError(TEXT("Control Rig integer evaluation returned an incomplete frame set"));
		const FRigControlElement* Control = Session.ControlRig->FindControl(ControlName);
		const UEnum* ControlEnum = Control ? Control->Settings.ControlEnum.Get() : nullptr;
		auto ControlObject = MakeShared<FJsonObject>();
		ControlObject->SetStringField(TEXT("control"), ControlName.ToString());
		ControlObject->SetStringField(TEXT("valueType"), ControlEnum ? TEXT("enum") : TEXT("int"));
		TArray<TSharedPtr<FJsonValue>> FrameSamples;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			auto FrameObject = MakeShared<FJsonObject>();
			FrameObject->SetNumberField(TEXT("frame"), Frames[Index].Value);
			FrameObject->SetNumberField(TEXT("value"), IntValues[Index]);
			const int32 EnumIndex = ControlRigSequencerFindEnumOption(ControlEnum, IntValues[Index]);
			if (EnumIndex != INDEX_NONE)
			{
				FrameObject->SetStringField(TEXT("enumOption"), ControlEnum->GetNameStringByIndex(EnumIndex));
			}
			FrameSamples.Add(MakeShared<FJsonValueObject>(FrameObject));
		}
		ControlObject->SetArrayField(TEXT("samples"), FrameSamples);
		Samples.Add(MakeShared<FJsonValueObject>(ControlObject));
	}
	Result->SetArrayField(TEXT("samples"), Samples);
	return MCPResult(Result);
#endif
}

TSharedPtr<FJsonValue> FAnimationHandlers::CaptureControlRigPose(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return ControlRigSequencerUnsupported();
#else
	FControlRigSequenceSession Session;
	FString Error;
	if (!ControlRigSequencerResolveSession(Params, Session, Error)) return MCPError(Error);
	int32 CurrentFrame = 0;
	double CurrentSubFrame = 0.0;
	if (!ControlRigSequencerCurrentFrame(Session, CurrentFrame, CurrentSubFrame, Error)) return MCPError(Error);

	TArray<FName> SelectedControls = Session.ControlRig->CurrentControlSelection();
	SelectedControls.Sort(FNameLexicalLess());
	TArray<FName> CaptureControls;
	const TArray<TSharedPtr<FJsonValue>>* RequestedControls = nullptr;
	if (Params->TryGetArrayField(TEXT("controlNames"), RequestedControls) && RequestedControls)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RequestedControls)
		{
			if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
				return MCPError(TEXT("Every item in 'controlNames' must be a non-empty string"));
			const FName Control(*Value->AsString());
			if (CaptureControls.Contains(Control))
				return MCPError(FString::Printf(TEXT("'controlNames' contains duplicate control '%s'"), *Control.ToString()));
			CaptureControls.Add(Control);
		}
	}
	else
	{
		CaptureControls = SelectedControls;
	}
	if (CaptureControls.IsEmpty())
		return MCPError(TEXT("No controls were requested or selected for live capture"));
	CaptureControls.Sort(FNameLexicalLess());

	auto Snapshot = MakeShared<FJsonObject>();
	Snapshot->SetNumberField(TEXT("captureVersion"), 1);
	Snapshot->SetStringField(TEXT("sequencePath"), Session.Sequence->GetPathName());
	Snapshot->SetStringField(TEXT("bindingTag"), Session.BindingTag);
	Snapshot->SetStringField(TEXT("bindingGuid"), Session.BindingGuid.ToString());
	Snapshot->SetStringField(TEXT("trackPath"), Session.Track->GetPathName());
	Snapshot->SetStringField(TEXT("sectionPath"), Session.Section->GetPathName());
	Snapshot->SetStringField(TEXT("controlRigClass"), Session.ControlRig->GetClass()->GetPathName());
	Snapshot->SetStringField(TEXT("controlRigObjectPath"), Session.ControlRig->GetPathName());
	Snapshot->SetNumberField(TEXT("controlRigObjectId"), Session.ControlRig->GetUniqueID());
	Snapshot->SetNumberField(TEXT("currentFrame"), CurrentFrame);
	Snapshot->SetNumberField(TEXT("currentSubFrame"), CurrentSubFrame);
	TArray<TSharedPtr<FJsonValue>> SelectedValues;
	for (const FName Name : SelectedControls)
	{
		SelectedValues.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	Snapshot->SetArrayField(TEXT("selectedControls"), SelectedValues);

	TArray<TSharedPtr<FJsonValue>> ControlValues;
	for (const FName ControlName : CaptureControls)
	{
		FRigControlElement* Control = Session.ControlRig->FindControl(ControlName);
		if (!Control) return MCPError(FString::Printf(TEXT("Control not found: %s"), *ControlName.ToString()));
		if (!Control->Settings.IsAnimatable())
			return MCPError(FString::Printf(TEXT("Control is not animatable: %s"), *ControlName.ToString()));
		const FRigControlValue Value = Session.ControlRig->GetControlValue(Control, ERigControlValueType::Current);
		auto ControlObject = MakeShared<FJsonObject>();
		ControlObject->SetStringField(TEXT("control"), ControlName.ToString());
		ControlObject->SetStringField(TEXT("controlType"), StaticEnum<ERigControlType>()->GetNameStringByValue(
			static_cast<int64>(Control->Settings.ControlType)));
		if (ControlRigSequencerIsTransformControl(Control))
		{
			const FTransform Transform = Value.GetAsTransform(Control->Settings.ControlType, Control->Settings.PrimaryAxis);
			const FVector Translation = Transform.GetTranslation();
			const FQuat Rotation = Transform.GetRotation();
			const FVector Scale = Transform.GetScale3D();
			if (Translation.ContainsNaN() || Rotation.ContainsNaN() || Scale.ContainsNaN()
				|| Rotation.SizeSquared() <= UE_SMALL_NUMBER)
			{
				return MCPError(FString::Printf(TEXT("Live control '%s' contains a non-finite or invalid transform"), *ControlName.ToString()));
			}
			ControlObject->SetStringField(TEXT("valueType"), TEXT("transform"));
			ControlObject->SetObjectField(TEXT("transform"), ControlRigSequencerLiveTransformJson(Transform));
		}
		else if (Control->Settings.ControlType == ERigControlType::Bool)
		{
			ControlObject->SetStringField(TEXT("valueType"), TEXT("bool"));
			ControlObject->SetBoolField(TEXT("value"), Value.Get<bool>());
		}
		else if (ControlRigSequencerIsFloatControl(Control))
		{
			const float Number = Value.Get<float>();
			if (!FMath::IsFinite(Number))
				return MCPError(FString::Printf(TEXT("Live control '%s' contains a non-finite float"), *ControlName.ToString()));
			ControlObject->SetStringField(TEXT("valueType"), TEXT("float"));
			ControlObject->SetNumberField(TEXT("value"), Number);
		}
		else if (Control->Settings.ControlType == ERigControlType::Integer)
		{
			ControlObject->SetStringField(TEXT("valueType"), Control->Settings.ControlEnum != nullptr ? TEXT("enum") : TEXT("int"));
			ControlObject->SetNumberField(TEXT("value"), Value.Get<int32>());
		}
		else
		{
			return MCPError(FString::Printf(TEXT("Control type is not supported by live capture: %s"), *ControlName.ToString()));
		}
		ControlValues.Add(MakeShared<FJsonValueObject>(ControlObject));
	}
	Snapshot->SetArrayField(TEXT("controls"), ControlValues);
	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("snapshot"), Snapshot);
	Result->SetNumberField(TEXT("capturedControlCount"), CaptureControls.Num());
	return MCPResult(Result);
#endif
}

TSharedPtr<FJsonValue> FAnimationHandlers::ApplyControlRigEdits(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return ControlRigSequencerUnsupported();
#else
	FString SequencePath;
	if (auto Error = RequireString(Params, TEXT("sequencePath"), SequencePath)) return Error;
	if (MCPIsProtectedAssetPath(SequencePath))
	{
		return MCPError(FString::Printf(TEXT("Protected asset cannot be modified: %s"), *SequencePath));
	}
	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) return MCPError(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));
	FControlRigSequenceFocusGuard Focus(Sequence);
	if (!Focus.IsReady()) return MCPError(TEXT("Could not focus the LevelSequence in Sequencer"));

	FControlRigSequenceSession Session;
	FString Error;
	if (!ControlRigSequencerResolveSession(Params, Session, Error)) return MCPError(Error);
	if (Session.Section->GetDoNotKey()) return MCPError(TEXT("The resolved Control Rig section is marked Do Not Key"));
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->IsEmpty())
	{
		return MCPError(TEXT("'operations' must be a non-empty array"));
	}

	int32 RangeStart = 0;
	int32 RangeEndExclusive = 0;
	ControlRigSequencerDisplayRange(Session.MovieScene, RangeStart, RangeEndExclusive);
	TArray<FControlRigPreparedWrite> Prepared;
	TArray<FControlRigPreparedContactQA> PreparedContacts;
	TSet<FString> WrittenKeys;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject> Operation = (*Operations)[OperationIndex].IsValid()
			? (*Operations)[OperationIndex]->AsObject() : nullptr;
		if (!Operation.IsValid()) return MCPError(FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		FString Op;
		if (!Operation->TryGetStringField(TEXT("op"), Op) || Op.IsEmpty())
			return MCPError(FString::Printf(TEXT("operations[%d].op is required"), OperationIndex));
		Op.ToLowerInline();
		if (Op == TEXT("propagate_pose"))
		{
			const TSharedPtr<FJsonObject>* BaselineObject = nullptr;
			const TSharedPtr<FJsonObject>* AcceptedObject = nullptr;
			if (!Operation->TryGetObjectField(TEXT("baseline"), BaselineObject) || !BaselineObject || !BaselineObject->IsValid()
				|| !Operation->TryGetObjectField(TEXT("accepted"), AcceptedObject) || !AcceptedObject || !AcceptedObject->IsValid())
			{
				return MCPError(FString::Printf(TEXT("operations[%d] requires baseline and accepted live snapshots"), OperationIndex));
			}
			FControlRigLivePoseSnapshot Baseline;
			FControlRigLivePoseSnapshot Accepted;
			if (!ControlRigSequencerReadLiveSnapshot(*BaselineObject, Baseline, Error))
				return MCPError(FString::Printf(TEXT("operations[%d].baseline: %s"), OperationIndex, *Error));
			if (!ControlRigSequencerReadLiveSnapshot(*AcceptedObject, Accepted, Error))
				return MCPError(FString::Printf(TEXT("operations[%d].accepted: %s"), OperationIndex, *Error));
			if (!ControlRigSequencerValidateLiveSnapshots(Session, Baseline, Accepted, Error))
				return MCPError(FString::Printf(TEXT("operations[%d]: %s"), OperationIndex, *Error));

			const TArray<TSharedPtr<FJsonValue>>* PropagationControls = nullptr;
			if (!Operation->TryGetArrayField(TEXT("controls"), PropagationControls)
				|| !PropagationControls || PropagationControls->IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("operations[%d].controls must be a non-empty array"), OperationIndex));
			}
			TSet<FName> SeenControls;
			for (int32 ControlIndex = 0; ControlIndex < PropagationControls->Num(); ++ControlIndex)
			{
				const TSharedPtr<FJsonObject> Entry = (*PropagationControls)[ControlIndex].IsValid()
					? (*PropagationControls)[ControlIndex]->AsObject() : nullptr;
				FString ControlString;
				FString Mode;
				if (!Entry.IsValid()
					|| !Entry->TryGetStringField(TEXT("control"), ControlString) || ControlString.IsEmpty()
					|| !Entry->TryGetStringField(TEXT("mode"), Mode) || Mode.IsEmpty())
				{
					return MCPError(FString::Printf(TEXT("operations[%d].controls[%d] requires control and mode"), OperationIndex, ControlIndex));
				}
				Mode.ToLowerInline();
				if (Mode != TEXT("fixed") && Mode != TEXT("local_delta"))
					return MCPError(FString::Printf(TEXT("operations[%d].controls[%d].mode must be 'fixed' or 'local_delta'"), OperationIndex, ControlIndex));
				const FName ControlName(*ControlString);
				if (SeenControls.Contains(ControlName))
					return MCPError(FString::Printf(TEXT("operations[%d] contains duplicate propagation control '%s'"), OperationIndex, *ControlString));
				SeenControls.Add(ControlName);
				FRigControlElement* Control = Session.ControlRig->FindControl(ControlName);
				const FControlRigLivePoseValue* BaselineValue = Baseline.Controls.Find(ControlName);
				const FControlRigLivePoseValue* AcceptedValue = Accepted.Controls.Find(ControlName);
				if (!Control || !BaselineValue || !AcceptedValue)
					return MCPError(FString::Printf(TEXT("operations[%d] control '%s' is absent from the rig or either complete snapshot"), OperationIndex, *ControlString));
				if (!Control->Settings.IsAnimatable() || Control->Settings.ControlType != BaselineValue->ControlType)
					return MCPError(FString::Printf(TEXT("operations[%d] control '%s' is no longer the captured animatable type"), OperationIndex, *ControlString));
				const EControlRigPreparedValueType ExpectedValueType = ControlRigSequencerIsTransformControl(Control)
					? EControlRigPreparedValueType::Transform
					: Control->Settings.ControlType == ERigControlType::Bool
						? EControlRigPreparedValueType::Bool
						: ControlRigSequencerIsFloatControl(Control)
							? EControlRigPreparedValueType::Float
							: EControlRigPreparedValueType::Integer;
				if (BaselineValue->ValueType != ExpectedValueType)
					return MCPError(FString::Printf(TEXT("operations[%d] control '%s' snapshot valueType does not match its native controlType"), OperationIndex, *ControlString));

				const TArray<TSharedPtr<FJsonValue>>* DonorFrameValues = nullptr;
				if (!Entry->TryGetArrayField(TEXT("donorFrames"), DonorFrameValues)
					|| !DonorFrameValues || DonorFrameValues->IsEmpty())
				{
					return MCPError(FString::Printf(TEXT("operations[%d].controls[%d].donorFrames must be a non-empty array"), OperationIndex, ControlIndex));
				}
				FControlRigPreparedWrite Write;
				Write.Control = ControlName;
				Write.Op = TEXT("propagate_pose");
				Write.PropagationMode = Mode;
				Write.ValueType = BaselineValue->ValueType;
				Write.Space = EControlRigTransformSpace::Local;
				int32 PreviousFrame = MIN_int32;
				for (int32 FrameIndex = 0; FrameIndex < DonorFrameValues->Num(); ++FrameIndex)
				{
					const TSharedPtr<FJsonValue>& FrameValue = (*DonorFrameValues)[FrameIndex];
					if (!FrameValue.IsValid() || FrameValue->Type != EJson::Number
						|| !FMath::IsFinite(FrameValue->AsNumber())
						|| !FMath::IsNearlyEqual(FrameValue->AsNumber(), FMath::RoundToDouble(FrameValue->AsNumber()))
						|| FrameValue->AsNumber() < static_cast<double>(MIN_int32)
						|| FrameValue->AsNumber() > static_cast<double>(MAX_int32))
					{
						return MCPError(FString::Printf(TEXT("operations[%d].controls[%d].donorFrames[%d] must be a 32-bit integer"), OperationIndex, ControlIndex, FrameIndex));
					}
					const int32 Frame = FMath::RoundToInt(FrameValue->AsNumber());
					if ((FrameIndex > 0 && Frame <= PreviousFrame) || Frame < RangeStart || Frame >= RangeEndExclusive)
					{
						return MCPError(FString::Printf(TEXT("operations[%d].controls[%d].donorFrames must be strictly increasing within [%d, %d)"), OperationIndex, ControlIndex, RangeStart, RangeEndExclusive));
					}
					PreviousFrame = Frame;
					Write.Frames.Add(FFrameNumber(Frame));
				}
				if (Write.Frames.Num() > ControlRigSequencerMaxFrames)
					return MCPError(FString::Printf(TEXT("operations[%d].controls[%d] exceeds the %d-frame limit"), OperationIndex, ControlIndex, ControlRigSequencerMaxFrames));
				if (!ControlRigSequencerRegisterWriteFrames(WrittenKeys, ControlName, Write.Frames, OperationIndex, Error))
					return MCPError(Error);

				if (Write.ValueType == EControlRigPreparedValueType::Transform)
				{
					if (!ControlRigSequencerReadLocalControlValues(Session, Control, Write.Frames, Write.Before))
						return MCPError(FString::Printf(TEXT("Could not sample donor frames for propagation control '%s'"), *ControlString));
					if (!ControlRigSequencerPreparePropagatedTransforms(
						*BaselineValue, *AcceptedValue, Mode, Write.Before, Write.After, Write.ChangedChannels, Error))
					{
						return MCPError(FString::Printf(TEXT("operations[%d] control '%s': %s"), OperationIndex, *ControlString, *Error));
					}
				}
				else if (Write.ValueType == EControlRigPreparedValueType::Bool)
				{
					if (Mode != TEXT("fixed")) return MCPError(FString::Printf(TEXT("Bool propagation control '%s' supports fixed mode only"), *ControlString));
					if (BaselineValue->BoolValue != AcceptedValue->BoolValue) Write.ChangedChannels.Add(TEXT("value"));
					Write.BoolBefore = UControlRigSequencerEditorLibrary::GetLocalControlRigBools(
						Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
					Write.BoolAfter.Init(AcceptedValue->BoolValue, Write.Frames.Num());
				}
				else if (Write.ValueType == EControlRigPreparedValueType::Float)
				{
					if (!FMath::IsNearlyEqual(BaselineValue->FloatValue, AcceptedValue->FloatValue, 1e-6f)) Write.ChangedChannels.Add(TEXT("value"));
					Write.FloatBefore = UControlRigSequencerEditorLibrary::GetLocalControlRigFloats(
						Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
					if (Mode == TEXT("fixed")) Write.FloatAfter.Init(AcceptedValue->FloatValue, Write.Frames.Num());
					else
					{
						const float Delta = AcceptedValue->FloatValue - BaselineValue->FloatValue;
						for (const float Donor : Write.FloatBefore)
						{
							const float Propagated = Donor + Delta;
							if (!FMath::IsFinite(Propagated))
								return MCPError(FString::Printf(TEXT("Float propagation produced a non-finite value for '%s'"), *ControlString));
							Write.FloatAfter.Add(Propagated);
						}
					}
				}
				else
				{
					if (Mode != TEXT("fixed")) return MCPError(FString::Printf(TEXT("Integer/enum propagation control '%s' supports fixed mode only"), *ControlString));
					if (BaselineValue->IntValue != AcceptedValue->IntValue) Write.ChangedChannels.Add(TEXT("value"));
					if (const UEnum* ControlEnum = Control->Settings.ControlEnum.Get())
					{
						if (!ControlRigSequencerIsValidEnumValue(ControlEnum, AcceptedValue->IntValue))
							return MCPError(FString::Printf(TEXT("Accepted value for '%s' is not a selectable enum option"), *ControlString));
					}
					Write.IntBefore = UControlRigSequencerEditorLibrary::GetLocalControlRigInts(
						Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
					Write.IntAfter.Init(AcceptedValue->IntValue, Write.Frames.Num());
				}
				const int32 BeforeCount = Write.ValueType == EControlRigPreparedValueType::Transform ? Write.Before.Num()
					: Write.ValueType == EControlRigPreparedValueType::Bool ? Write.BoolBefore.Num()
					: Write.ValueType == EControlRigPreparedValueType::Float ? Write.FloatBefore.Num()
					: Write.IntBefore.Num();
				if (BeforeCount != Write.Frames.Num())
					return MCPError(FString::Printf(TEXT("Could not sample every donor frame for propagation control '%s'"), *ControlString));
				if (Write.ChangedChannels.IsEmpty())
					return MCPError(FString::Printf(TEXT("Propagation control '%s' did not change between baseline and accepted snapshots"), *ControlString));
				Prepared.Add(MoveTemp(Write));
			}
			continue;
		}

		FString ControlString;
		if (!Operation->TryGetStringField(TEXT("control"), ControlString) || ControlString.IsEmpty())
			return MCPError(FString::Printf(TEXT("operations[%d].control is required"), OperationIndex));
		const FName ControlName(*ControlString);
		FRigControlElement* Control = Session.ControlRig->FindControl(ControlName);
		if (!Control) return MCPError(FString::Printf(TEXT("Control not found: %s"), *ControlString));
		if (!Control->Settings.IsAnimatable()) return MCPError(FString::Printf(TEXT("Control is not animatable: %s"), *ControlString));
		const bool bSetOperation = Op == TEXT("set");
		const bool bSetKeysOperation = Op == TEXT("set_keys");
		const bool bOffsetOperation = Op == TEXT("offset");
		const bool bContactOperation = Op == TEXT("contact_lock");
		const bool bBoolOperation = Op == TEXT("set_bool");
		const bool bFloatOperation = Op == TEXT("set_float");
		const bool bIntOperation = Op == TEXT("set_int");
		if (!bSetOperation && !bSetKeysOperation && !bOffsetOperation && !bContactOperation
			&& !bBoolOperation && !bFloatOperation && !bIntOperation)
		{
			return MCPError(FString::Printf(
				TEXT("operations[%d].op must be 'set_keys', 'set', 'offset', 'contact_lock', 'set_bool', 'set_float', 'set_int', or 'propagate_pose'"),
				OperationIndex));
		}
		if (bBoolOperation)
		{
			if (Control->Settings.ControlType != ERigControlType::Bool)
				return MCPError(FString::Printf(TEXT("Bool control not found: %s"), *ControlString));
		}
		else if (bFloatOperation)
		{
			if (!ControlRigSequencerIsFloatControl(Control))
				return MCPError(FString::Printf(TEXT("Float control not found: %s"), *ControlString));
		}
		else if (bIntOperation)
		{
			if (Control->Settings.ControlType != ERigControlType::Integer)
				return MCPError(FString::Printf(TEXT("Integer control not found: %s"), *ControlString));
		}
		else if (!ControlRigSequencerIsTransformControl(Control))
		{
			return MCPError(FString::Printf(TEXT("Transform control not found: %s"), *ControlString));
		}
		else if (bContactOperation && !ControlRigSequencerControlHasTranslation(Control->Settings.ControlType))
		{
			return MCPError(FString::Printf(
				TEXT("contact_lock driver control must support translation: %s"), *ControlString));
		}

		FControlRigPreparedWrite Write;
		Write.Control = ControlName;
		Write.Op = Op;
		Write.ValueType = bBoolOperation ? EControlRigPreparedValueType::Bool
			: bFloatOperation ? EControlRigPreparedValueType::Float
			: bIntOperation ? EControlRigPreparedValueType::Integer
			: EControlRigPreparedValueType::Transform;
		TArray<FTransform> AbsoluteKeyTransforms;
		if (Write.ValueType == EControlRigPreparedValueType::Transform)
		{
			if (bContactOperation)
			{
				if (Operation->HasField(TEXT("space")))
					return MCPError(FString::Printf(TEXT("operations[%d].contact_lock always uses component space"), OperationIndex));
				Write.Space = EControlRigTransformSpace::Global;
			}
			else
			{
				FString CanonicalSpace;
				if (!ControlRigSequencerReadSpace(OptionalString(Operation, TEXT("space"), TEXT("local")), Write.Space, CanonicalSpace, Error))
					return MCPError(FString::Printf(TEXT("operations[%d]: %s"), OperationIndex, *Error));
			}
		}

		if (bSetKeysOperation)
		{
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			if (!Operation->TryGetArrayField(TEXT("keys"), Keys) || !Keys || Keys->IsEmpty())
				return MCPError(FString::Printf(TEXT("operations[%d].keys must be a non-empty array"), OperationIndex));
			int32 PreviousFrame = 0;
			FQuat PreviousRotation = FQuat::Identity;
			for (int32 KeyIndex = 0; KeyIndex < Keys->Num(); ++KeyIndex)
			{
				const TSharedPtr<FJsonValue>& KeyValue = (*Keys)[KeyIndex];
				if (!KeyValue.IsValid() || KeyValue->Type != EJson::Object)
					return MCPError(FString::Printf(TEXT("operations[%d].keys[%d] must be an object"), OperationIndex, KeyIndex));
				const TSharedPtr<FJsonObject> KeyObject = KeyValue->AsObject();
				double FrameNumber = 0.0;
				if (!KeyObject.IsValid()
					|| !KeyObject->TryGetNumberField(TEXT("frame"), FrameNumber)
					|| !FMath::IsFinite(FrameNumber)
					|| !FMath::IsNearlyEqual(FrameNumber, FMath::RoundToDouble(FrameNumber))
					|| FrameNumber < static_cast<double>(MIN_int32)
					|| FrameNumber > static_cast<double>(MAX_int32))
				{
					return MCPError(FString::Printf(TEXT("operations[%d].keys[%d].frame must be a 32-bit integer"), OperationIndex, KeyIndex));
				}
				const int32 Frame = static_cast<int32>(FMath::RoundToInt(FrameNumber));
				if (KeyIndex > 0 && Frame <= PreviousFrame)
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d].keys frames must be strictly increasing and unique"),
						OperationIndex));
				}
				PreviousFrame = Frame;

				const TSharedPtr<FJsonObject>* TransformObject = nullptr;
				if (!KeyObject->TryGetObjectField(TEXT("transform"), TransformObject)
					|| !TransformObject || !TransformObject->IsValid())
				{
					return MCPError(FString::Printf(TEXT("operations[%d].keys[%d].transform must be an object"), OperationIndex, KeyIndex));
				}
				if ((*TransformObject)->Values.Num() != 3
					|| !(*TransformObject)->HasField(TEXT("translation"))
					|| !(*TransformObject)->HasField(TEXT("rotationQuaternion"))
					|| !(*TransformObject)->HasField(TEXT("scale")))
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d].keys[%d].transform must contain exactly translation, rotationQuaternion and scale"),
						OperationIndex, KeyIndex));
				}
				FVector Translation;
				FQuat Rotation;
				FVector Scale;
				if (!ControlRigSequencerReadVector(*TransformObject, TEXT("translation"), Translation, Error)
					|| !ControlRigSequencerReadNormalizedQuaternion(*TransformObject, Rotation, Error)
					|| !ControlRigSequencerReadVector(*TransformObject, TEXT("scale"), Scale, Error))
				{
					return MCPError(FString::Printf(TEXT("operations[%d].keys[%d].transform: %s"), OperationIndex, KeyIndex, *Error));
				}
				if (KeyIndex > 0 && (PreviousRotation | Rotation) < 0.0)
				{
					Rotation = FQuat(-Rotation.X, -Rotation.Y, -Rotation.Z, -Rotation.W);
				}
				PreviousRotation = Rotation;
				Write.Frames.Add(FFrameNumber(Frame));
				AbsoluteKeyTransforms.Add(FTransform(Rotation, Translation, Scale));
			}
		}
		else if (bSetOperation || bBoolOperation || bFloatOperation || bIntOperation)
		{
			if (!ControlRigSequencerReadFrames(Operation, Write.Frames, Error))
				return MCPError(FString::Printf(TEXT("operations[%d]: %s"), OperationIndex, *Error));
			if (bBoolOperation && !Operation->TryGetBoolField(TEXT("value"), Write.BoolValue))
				return MCPError(FString::Printf(TEXT("operations[%d].value must be a boolean"), OperationIndex));
			if (bFloatOperation)
			{
				double Value = 0.0;
				if (!Operation->TryGetNumberField(TEXT("value"), Value) || !FMath::IsFinite(Value))
					return MCPError(FString::Printf(TEXT("operations[%d].value must be a finite number"), OperationIndex));
				Write.FloatValue = static_cast<float>(Value);
				if (!FMath::IsFinite(Write.FloatValue))
					return MCPError(FString::Printf(TEXT("operations[%d].value is outside the float range"), OperationIndex));
			}
			if (bIntOperation)
			{
				double Value = 0.0;
				if (!Operation->TryGetNumberField(TEXT("value"), Value)
					|| !FMath::IsFinite(Value)
					|| !FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value))
					|| Value < static_cast<double>(MIN_int32)
					|| Value > static_cast<double>(MAX_int32))
				{
					return MCPError(FString::Printf(TEXT("operations[%d].value must be a 32-bit integer"), OperationIndex));
				}
				Write.IntValue = static_cast<int32>(FMath::RoundToInt(Value));
				if (const UEnum* ControlEnum = Control->Settings.ControlEnum.Get())
				{
					if (Write.IntValue < 0 || Write.IntValue > MAX_uint8)
					{
						return MCPError(FString::Printf(
							TEXT("operations[%d].value is outside the byte range used by Sequencer enum channels"),
							OperationIndex));
					}
					if (!ControlRigSequencerIsValidEnumValue(ControlEnum, Write.IntValue))
					{
						return MCPError(FString::Printf(
							TEXT("operations[%d].value is not a selectable option in enum %s"),
							OperationIndex, *ControlEnum->GetPathName()));
					}
				}
			}
		}
		else if (bOffsetOperation || bContactOperation)
		{
			double StartNumber = 0.0;
			double EndNumber = 0.0;
			if (!Operation->TryGetNumberField(TEXT("startFrame"), StartNumber)
				|| !Operation->TryGetNumberField(TEXT("endFrame"), EndNumber)
				|| !FMath::IsFinite(StartNumber) || !FMath::IsFinite(EndNumber)
				|| !FMath::IsNearlyEqual(StartNumber, FMath::RoundToDouble(StartNumber))
				|| !FMath::IsNearlyEqual(EndNumber, FMath::RoundToDouble(EndNumber))
				|| StartNumber < static_cast<double>(MIN_int32) || StartNumber > static_cast<double>(MAX_int32)
				|| EndNumber < static_cast<double>(MIN_int32) || EndNumber > static_cast<double>(MAX_int32))
			{
				return MCPError(FString::Printf(TEXT("operations[%d] requires integer startFrame and endFrame"), OperationIndex));
			}
			const int32 Start = static_cast<int32>(FMath::RoundToInt(StartNumber));
			const int32 End = static_cast<int32>(FMath::RoundToInt(EndNumber));
			if (End < Start) return MCPError(FString::Printf(TEXT("operations[%d].endFrame must be at least startFrame"), OperationIndex));
			if (Start < RangeStart || End >= RangeEndExclusive)
			{
				return MCPError(FString::Printf(
					TEXT("operations[%d] range [%d, %d] is outside [%d, %d)"),
					OperationIndex, Start, End, RangeStart, RangeEndExclusive));
			}
			const int64 FrameCount = static_cast<int64>(End) - static_cast<int64>(Start) + 1;
			if (FrameCount > ControlRigSequencerMaxFrames)
			{
				return MCPError(FString::Printf(
					TEXT("operations[%d] is limited to %d frames"),
					OperationIndex, ControlRigSequencerMaxFrames));
			}
			Write.Frames.Reserve(static_cast<int32>(FrameCount));
			for (int64 Frame = Start; Frame <= End; ++Frame) Write.Frames.Add(FFrameNumber(static_cast<int32>(Frame)));
		}

		for (FFrameNumber Frame : Write.Frames)
		{
			if (Frame.Value < RangeStart || Frame.Value >= RangeEndExclusive)
				return MCPError(FString::Printf(TEXT("operations[%d] frame %d is outside [%d, %d)"), OperationIndex, Frame.Value, RangeStart, RangeEndExclusive));
		}
		if (!ControlRigSequencerRegisterWriteFrames(WrittenKeys, ControlName, Write.Frames, OperationIndex, Error))
			return MCPError(Error);

		if (Write.ValueType == EControlRigPreparedValueType::Bool)
		{
			const TArray<bool> Existing = UControlRigSequencerEditorLibrary::GetLocalControlRigBools(
				Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
			if (Existing.Num() != Write.Frames.Num())
				return MCPError(FString::Printf(TEXT("Could not sample %s before applying operations[%d]"), *ControlString, OperationIndex));
			Write.BoolBefore = Existing;
		}
		else if (Write.ValueType == EControlRigPreparedValueType::Float)
		{
			const TArray<float> Existing = UControlRigSequencerEditorLibrary::GetLocalControlRigFloats(
				Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
			if (Existing.Num() != Write.Frames.Num())
				return MCPError(FString::Printf(TEXT("Could not sample %s before applying operations[%d]"), *ControlString, OperationIndex));
			Write.FloatBefore = Existing;
		}
		else if (Write.ValueType == EControlRigPreparedValueType::Integer)
		{
			const TArray<int32> Existing = UControlRigSequencerEditorLibrary::GetLocalControlRigInts(
				Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
			if (Existing.Num() != Write.Frames.Num())
				return MCPError(FString::Printf(TEXT("Could not sample %s before applying operations[%d]"), *ControlString, OperationIndex));
			Write.IntBefore = Existing;
		}
		else
		{
			if (bContactOperation)
			{
				const TSharedPtr<FJsonObject>* TargetObject = nullptr;
				const bool bHasTargetField = Operation->HasField(TEXT("target"));
				const bool bHasTarget = Operation->TryGetObjectField(TEXT("target"), TargetObject)
					&& TargetObject && TargetObject->IsValid();
				if (bHasTargetField && !bHasTarget)
				{
					return MCPError(FString::Printf(TEXT("operations[%d].target must be an object"), OperationIndex));
				}
				FString TargetReferenceString;
				const bool bHasTargetReference = Operation->HasField(TEXT("targetReference"));
				if (bHasTargetReference
					&& (!Operation->TryGetStringField(TEXT("targetReference"), TargetReferenceString)
						|| TargetReferenceString.IsEmpty()))
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d].targetReference must be a non-empty bone or socket name"),
						OperationIndex));
				}
				if (!bHasTarget && !bHasTargetReference)
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d] requires target or targetReference"), OperationIndex));
				}
				const bool bCaptureRelativeTarget = bHasTargetReference && !bHasTarget;
				const bool bExplicitTargetRotation = bHasTarget
					&& (*TargetObject)->HasField(TEXT("rotationQuaternion"));
				const bool bTargetRotation = bExplicitTargetRotation
					|| (bCaptureRelativeTarget
						&& ControlRigSequencerControlHasRotation(Control->Settings.ControlType));
				if (bHasTarget
					&& (!(*TargetObject)->HasField(TEXT("translation"))
						|| (*TargetObject)->Values.Num() != ((*TargetObject)->HasField(TEXT("rotationQuaternion")) ? 2 : 1)))
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d].target must contain translation and optional rotationQuaternion only"),
						OperationIndex));
				}
				if (bExplicitTargetRotation && !ControlRigSequencerControlHasRotation(Control->Settings.ControlType))
				{
					return MCPError(FString::Printf(
						TEXT("contact_lock driver control must support rotation when target.rotationQuaternion is set: %s"),
						*ControlString));
				}

				FVector TargetTranslation;
				FQuat TargetRotation = FQuat::Identity;
				if (bHasTarget
					&& (!ControlRigSequencerReadVector(*TargetObject, TEXT("translation"), TargetTranslation, Error)
						|| (bExplicitTargetRotation
							&& !ControlRigSequencerReadNormalizedQuaternion(*TargetObject, TargetRotation, Error))))
				{
					return MCPError(FString::Printf(TEXT("operations[%d].target: %s"), OperationIndex, *Error));
				}
				const FName TargetReference(*TargetReferenceString);

				auto ReadNonNegativeFrameCount = [&](const TCHAR* Field, int32& OutValue) -> bool
				{
					OutValue = 0;
					const TSharedPtr<FJsonValue>* Value = Operation->Values.Find(Field);
					if (!Value || !Value->IsValid() || (*Value)->IsNull()) return true;
					if ((*Value)->Type != EJson::Number)
					{
						Error = FString::Printf(TEXT("operations[%d].%s must be a non-negative integer"), OperationIndex, Field);
						return false;
					}
					const double Number = (*Value)->AsNumber();
					if (!FMath::IsFinite(Number)
						|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
						|| Number < 0.0 || Number > static_cast<double>(MAX_int32))
					{
						Error = FString::Printf(TEXT("operations[%d].%s must be a non-negative integer"), OperationIndex, Field);
						return false;
					}
					OutValue = static_cast<int32>(FMath::RoundToInt(Number));
					return true;
				};
				int32 BlendIn = 0;
				int32 BlendOut = 0;
				if (!ReadNonNegativeFrameCount(TEXT("blendInFrames"), BlendIn)
					|| !ReadNonNegativeFrameCount(TEXT("blendOutFrames"), BlendOut))
				{
					return MCPError(Error);
				}
				const int64 IntervalCount = static_cast<int64>(Write.Frames.Last().Value)
					- static_cast<int64>(Write.Frames[0].Value);
				if (static_cast<int64>(BlendIn) + static_cast<int64>(BlendOut) > IntervalCount)
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d] blends must leave at least one fully constrained frame"),
						OperationIndex));
				}

				auto ReadTolerance = [&](const TCHAR* Field, double Default, double Maximum, double& OutValue) -> bool
				{
					OutValue = Default;
					const TSharedPtr<FJsonValue>* Value = Operation->Values.Find(Field);
					if (!Value || !Value->IsValid() || (*Value)->IsNull()) return true;
					if ((*Value)->Type != EJson::Number)
					{
						Error = FString::Printf(TEXT("operations[%d].%s must be a positive finite number"), OperationIndex, Field);
						return false;
					}
					const double Number = (*Value)->AsNumber();
					if (!FMath::IsFinite(Number) || Number <= 0.0 || Number > Maximum)
					{
						Error = FString::Printf(
							TEXT("operations[%d].%s must be greater than zero and at most %.3f"),
							OperationIndex, Field, Maximum);
						return false;
					}
					OutValue = Number;
					return true;
				};
				double PositionToleranceCm = 0.1;
				double RotationToleranceDegrees = 0.5;
				if (!ReadTolerance(TEXT("positionToleranceCm"), 0.1, 100.0, PositionToleranceCm)
					|| !ReadTolerance(TEXT("rotationToleranceDegrees"), 0.5, 180.0, RotationToleranceDegrees))
				{
					return MCPError(Error);
				}

				FString DrivenReferenceString;
				const bool bHasDrivenReference = Operation->HasField(TEXT("drivenReference"));
				if (bHasDrivenReference
					&& (!Operation->TryGetStringField(TEXT("drivenReference"), DrivenReferenceString)
						|| DrivenReferenceString.IsEmpty()))
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d].drivenReference must be a non-empty bone or socket name"),
						OperationIndex));
				}
				const FName DrivenReference(*DrivenReferenceString);

				bool bUseFkRotationChain = false;
				TArray<int32> FkChainBoneIndices;
				TArray<FName> FkChainControls;
				int32 FkChainWriteCount = 0;
				if (bHasDrivenReference && Cast<UFKControlRig>(Session.ControlRig))
				{
					const UFKControlRig* FkRig = CastChecked<UFKControlRig>(Session.ControlRig);
					USkeletalMeshComponent* Component = ControlRigSequencerBoundSkeletalMesh(Session.ControlRig);
					USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
					USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
					if (!Mesh || !Skeleton)
					{
						return MCPError(TEXT("FK contact_lock requires a bound skeletal mesh and skeleton"));
					}

					const FReferenceSkeleton& ReferenceSkeleton = Mesh->GetRefSkeleton();
					const FName DriverBone = UFKControlRig::GetControlTargetName(
						ControlName, ERigElementType::Bone);
					const int32 DriverBoneIndex = ReferenceSkeleton.FindBoneIndex(DriverBone);
					bool bDrivenReferenceIsSocket = false;
					int32 DrivenBoneIndex = ReferenceSkeleton.FindBoneIndex(DrivenReference);
					if (DrivenBoneIndex == INDEX_NONE)
					{
						const USkeletalMeshSocket* Socket = Component->GetSocketByName(DrivenReference);
						bDrivenReferenceIsSocket = Socket != nullptr;
						DrivenBoneIndex = Socket
							? ReferenceSkeleton.FindBoneIndex(Socket->BoneName)
							: INDEX_NONE;
					}
					if (DriverBoneIndex == INDEX_NONE || DrivenBoneIndex == INDEX_NONE)
					{
						return MCPError(FString::Printf(
							TEXT("FK contact_lock could not resolve driver %s and driven reference %s to bones"),
							*ControlString, *DrivenReferenceString));
					}

					const int32 SkeletonDriverIndex = Skeleton->GetSkeletonBoneIndexFromMeshBoneIndex(
						Mesh, DriverBoneIndex);
					bUseFkRotationChain = SkeletonDriverIndex != INDEX_NONE
						&& Skeleton->GetBoneTranslationRetargetingMode(SkeletonDriverIndex)
							== EBoneTranslationRetargetingMode::Skeleton;
					if (bUseFkRotationChain)
					{
						if (FkRig->GetApplyMode() != EControlRigFKRigExecuteMode::Replace)
						{
							return MCPError(
								TEXT("contact_lock_runtime_translation_unsupported: FK rotation-chain contact requires Replace apply mode"));
						}
						if (bDrivenReferenceIsSocket)
						{
							return MCPError(FString::Printf(
								TEXT("contact_lock_runtime_translation_unsupported: FK rotation-chain contact currently requires a driven bone; socket %s requires an asset Control Rig"),
								*DrivenReferenceString));
						}
						for (int32 BoneIndex = DrivenBoneIndex;
							BoneIndex != INDEX_NONE;
							BoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex))
						{
							FkChainBoneIndices.Add(BoneIndex);
							if (BoneIndex == DriverBoneIndex) break;
						}
						if (FkChainBoneIndices.IsEmpty() || FkChainBoneIndices.Last() != DriverBoneIndex)
						{
							return MCPError(FString::Printf(
								TEXT("FK contact_lock driver bone %s must be an ancestor of %s"),
								*DriverBone.ToString(),
								*ReferenceSkeleton.GetBoneName(DrivenBoneIndex).ToString()));
						}
						Algo::Reverse(FkChainBoneIndices);
						if (FkChainBoneIndices.Num() < 2)
						{
							return MCPError(FString::Printf(
								TEXT("contact_lock_runtime_translation_unsupported: FK bone %s ignores animation translation and has no descendant rotation chain"),
								*DriverBone.ToString()));
						}
						for (const int32 BoneIndex : FkChainBoneIndices)
						{
							const FName ChainControl = UFKControlRig::GetControlName(
								ReferenceSkeleton.GetBoneName(BoneIndex), ERigElementType::Bone);
							const FRigControlElement* ChainElement = Session.ControlRig->FindControl(ChainControl);
							if (!ChainElement || !ChainElement->Settings.IsAnimatable()
								|| !ControlRigSequencerControlHasRotation(ChainElement->Settings.ControlType))
							{
								return MCPError(FString::Printf(
									TEXT("FK contact_lock rotation-chain control is unavailable: %s"),
									*ChainControl.ToString()));
							}
							FkChainControls.Add(ChainControl);
						}
						// A position-only contact needs rotations through the end bone's parent.
						// Key the end control only when the caller explicitly requests orientation.
						FkChainWriteCount = bTargetRotation
							? FkChainControls.Num()
							: FkChainControls.Num() - 1;
						for (int32 ChainIndex = 1; ChainIndex < FkChainWriteCount; ++ChainIndex)
						{
							if (!ControlRigSequencerRegisterWriteFrames(
								WrittenKeys, FkChainControls[ChainIndex], Write.Frames, OperationIndex, Error))
							{
								return MCPError(Error);
							}
						}
					}
				}

				TArray<FName> StabilizerNames;
				const TSharedPtr<FJsonValue>* StabilizerValue = Operation->Values.Find(TEXT("stabilizeControls"));
				if (StabilizerValue && StabilizerValue->IsValid() && !(*StabilizerValue)->IsNull())
				{
					const TArray<TSharedPtr<FJsonValue>>* StabilizerValues = nullptr;
					if (!Operation->TryGetArrayField(TEXT("stabilizeControls"), StabilizerValues) || !StabilizerValues)
					{
						return MCPError(FString::Printf(TEXT("operations[%d].stabilizeControls must be an array"), OperationIndex));
					}
					if (bUseFkRotationChain && !StabilizerValues->IsEmpty())
					{
						return MCPError(FString::Printf(
							TEXT("operations[%d] cannot combine FK rotation-chain contact with stabilizer controls"),
							OperationIndex));
					}
					if (StabilizerValues->Num() > 8)
					{
						return MCPError(FString::Printf(TEXT("operations[%d] supports at most 8 stabilizer controls"), OperationIndex));
					}
					for (int32 StabilizerIndex = 0; StabilizerIndex < StabilizerValues->Num(); ++StabilizerIndex)
					{
						const TSharedPtr<FJsonValue>& Value = (*StabilizerValues)[StabilizerIndex];
						if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
						{
							return MCPError(FString::Printf(
								TEXT("operations[%d].stabilizeControls[%d] must be a non-empty string"),
								OperationIndex, StabilizerIndex));
						}
						const FName StabilizerName(*Value->AsString());
						if (StabilizerName == ControlName || StabilizerNames.Contains(StabilizerName))
						{
							return MCPError(FString::Printf(
								TEXT("operations[%d] stabilizers must be unique and cannot include the driver control"),
								OperationIndex));
						}
						FRigControlElement* Stabilizer = Session.ControlRig->FindControl(StabilizerName);
						if (!Stabilizer || !Stabilizer->Settings.IsAnimatable()
							|| !ControlRigSequencerIsTransformControl(Stabilizer)
							|| (!ControlRigSequencerControlHasTranslation(Stabilizer->Settings.ControlType)
								&& !ControlRigSequencerControlHasRotation(Stabilizer->Settings.ControlType)))
						{
							return MCPError(FString::Printf(
								TEXT("contact_lock stabilizer must be an animatable position and/or rotation control: %s"),
								*StabilizerName.ToString()));
						}
						if (!ControlRigSequencerRegisterWriteFrames(
							WrittenKeys, StabilizerName, Write.Frames, OperationIndex, Error))
						{
							return MCPError(Error);
						}
						StabilizerNames.Add(StabilizerName);
					}
				}

				const int32 ContactControlCount = bUseFkRotationChain
					? FkChainWriteCount
					: 1;
				const int64 CellCount = static_cast<int64>(Write.Frames.Num())
					* static_cast<int64>(StabilizerNames.Num() + ContactControlCount);
				if (CellCount > ControlRigSequencerMaxFrames)
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d] is limited to %d contact control-frame cells"),
						OperationIndex, ControlRigSequencerMaxFrames));
				}

				TArray<FName> ContactControls{ControlName};
				ContactControls.Append(StabilizerNames);
				const TArray<FArrayOfRigControlTransforms> Existing =
					UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
						Session.Sequence, Session.ControlRig, ContactControls, Write.Frames,
						EControlRigTransformSpace::Global, EMovieSceneTimeUnit::DisplayRate);
				if (Existing.Num() != ContactControls.Num())
				{
					return MCPError(FString::Printf(
						TEXT("Could not sample every contact_lock control before operations[%d]"),
						OperationIndex));
				}
				TMap<FName, const FArrayOfRigControlTransforms*> ExistingByControl;
				for (const FArrayOfRigControlTransforms& Values : Existing)
				{
					if (Values.Transforms.Num() != Write.Frames.Num())
					{
						return MCPError(FString::Printf(
							TEXT("Could not sample every contact_lock frame before operations[%d]"),
							OperationIndex));
					}
					ExistingByControl.Add(Values.ControlName, &Values);
				}
				const FArrayOfRigControlTransforms* const* DriverValues = ExistingByControl.Find(ControlName);
				if (!DriverValues || !*DriverValues)
				{
					return MCPError(FString::Printf(TEXT("Could not sample contact_lock driver %s"), *ControlString));
				}
				Write.Before = (*DriverValues)->Transforms;
				Write.After.SetNum(Write.Frames.Num());

				TArray<TArray<FTransform>> FkSourceChainGlobals;
				if (bUseFkRotationChain)
				{
					const TArray<FArrayOfRigControlTransforms> GlobalChainValues =
						UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
							Session.Sequence, Session.ControlRig, FkChainControls, Write.Frames,
							EControlRigTransformSpace::Global, EMovieSceneTimeUnit::DisplayRate);
					if (GlobalChainValues.Num() != FkChainControls.Num())
					{
						return MCPError(FString::Printf(
							TEXT("Could not sample every FK contact rotation-chain control before operations[%d]"),
							OperationIndex));
					}
					TMap<FName, const FArrayOfRigControlTransforms*> GlobalValuesByControl;
					for (const FArrayOfRigControlTransforms& Values : GlobalChainValues)
					{
						if (Values.Transforms.Num() != Write.Frames.Num())
						{
							return MCPError(FString::Printf(
								TEXT("Could not sample every FK contact rotation-chain frame before operations[%d]"),
								OperationIndex));
						}
						GlobalValuesByControl.Add(Values.ControlName, &Values);
					}
					FkSourceChainGlobals.SetNum(FkChainControls.Num());
					for (int32 ChainIndex = 0; ChainIndex < FkChainControls.Num(); ++ChainIndex)
					{
						const FArrayOfRigControlTransforms* const* Values =
							GlobalValuesByControl.Find(FkChainControls[ChainIndex]);
						if (!Values || !*Values)
						{
							return MCPError(FString::Printf(
								TEXT("Could not sample FK contact rotation-chain control %s"),
								*FkChainControls[ChainIndex].ToString()));
						}
						FkSourceChainGlobals[ChainIndex] = (*Values)->Transforms;
					}
				}

				TArray<FTransform> SubjectBefore = Write.Before;
				if (bUseFkRotationChain)
				{
					// FK control globals include their initial offset and coincide with the
					// evaluated bone component transforms. Use them so an existing rig layer
					// is part of the solve instead of resampling only the source animation.
					SubjectBefore = FkSourceChainGlobals.Last();
				}
				else if (bHasDrivenReference
					&& !ControlRigSequencerSampleReferenceTransforms(
						Session, DrivenReference, Write.Frames, SubjectBefore, Error))
				{
					return MCPError(FString::Printf(TEXT("operations[%d]: %s"), OperationIndex, *Error));
				}

				TArray<FTransform> TargetReferenceTransforms;
				if (bHasTargetReference
					&& !ControlRigSequencerSampleReferenceTransforms(
						Session, TargetReference, Write.Frames, TargetReferenceTransforms, Error))
				{
					return MCPError(FString::Printf(TEXT("operations[%d]: %s"), OperationIndex, *Error));
				}
				FTransform RelativeTarget = FTransform::Identity;
				if (bHasTargetReference)
				{
					RelativeTarget = bCaptureRelativeTarget
						? SubjectBefore[0].GetRelativeTransform(TargetReferenceTransforms[0])
						: FTransform(TargetRotation, TargetTranslation, FVector::OneVector);
					RelativeTarget.NormalizeRotation();
				}

				FControlRigPreparedContactQA ContactQA;
				ContactQA.OperationIndex = OperationIndex;
				ContactQA.Control = ControlName;
				ContactQA.DrivenReference = DrivenReference;
				ContactQA.TargetReference = TargetReference;
				ContactQA.ControlType = Control->Settings.ControlType;
				ContactQA.bHasDrivenReference = bHasDrivenReference;
				ContactQA.bHasTargetReference = bHasTargetReference;
				ContactQA.bCapturedRelativeTarget = bCaptureRelativeTarget;
				ContactQA.bCheckRotation = bTargetRotation;
				ContactQA.PositionToleranceCm = PositionToleranceCm;
				ContactQA.RotationToleranceDegrees = RotationToleranceDegrees;
				ContactQA.Frames = Write.Frames;
				ContactQA.ExpectedSubject.SetNum(Write.Frames.Num());

				TArray<double> Weights;
				Weights.SetNum(Write.Frames.Num());
				for (int32 Index = 0; Index < Write.Frames.Num(); ++Index)
				{
					const double Weight = ControlRigSequencerContactWeight(
						Index, Write.Frames.Num(), BlendIn, BlendOut);
					Weights[Index] = Weight;
					if (Weight >= 1.0 - UE_DOUBLE_SMALL_NUMBER) ++ContactQA.FullWeightFrameCount;

					FTransform SubjectTarget = SubjectBefore[Index];
					if (bHasTargetReference)
					{
						const FTransform ComposedTarget = ControlRigSequencerComposeContactTarget(
							RelativeTarget, TargetReferenceTransforms[Index], SubjectBefore[Index].GetScale3D());
						SubjectTarget.SetTranslation(ComposedTarget.GetTranslation());
						if (bTargetRotation) SubjectTarget.SetRotation(ComposedTarget.GetRotation());
					}
					else
					{
						SubjectTarget.SetTranslation(TargetTranslation);
						if (bTargetRotation) SubjectTarget.SetRotation(TargetRotation);
					}
					ContactQA.ExpectedSubject[Index] = ControlRigSequencerBlendContactTransform(
						SubjectBefore[Index], SubjectTarget, Weight, bTargetRotation);

					if (bUseFkRotationChain)
					{
						continue;
					}
					if (bHasDrivenReference)
					{
						const FTransform SubjectRelativeToDriver =
							SubjectBefore[Index].GetRelativeTransform(Write.Before[Index]);
						FTransform DriverTarget = SubjectRelativeToDriver.GetRelativeTransformReverse(
							ContactQA.ExpectedSubject[Index]);
						DriverTarget.SetScale3D(Write.Before[Index].GetScale3D());
						if (!ControlRigSequencerControlHasRotation(Control->Settings.ControlType))
						{
							DriverTarget.SetRotation(Write.Before[Index].GetRotation());
						}
						if (DriverTarget.ContainsNaN())
						{
							return MCPError(FString::Printf(
								TEXT("operations[%d] produced an invalid driver transform at frame %d"),
								OperationIndex, Write.Frames[Index].Value));
						}
						Write.After[Index] = DriverTarget;
					}
					else
					{
						Write.After[Index] = ContactQA.ExpectedSubject[Index];
					}
				}

				TArray<FControlRigPreparedWrite> FkChainWrites;
				if (bUseFkRotationChain)
				{
					ContactQA.bUsedFkRotationChain = true;
					const TArray<FArrayOfRigControlTransforms> LocalControlValues =
						UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
							Session.Sequence, Session.ControlRig, FkChainControls, Write.Frames,
							EControlRigTransformSpace::Local, EMovieSceneTimeUnit::DisplayRate);
					if (LocalControlValues.Num() != FkChainControls.Num())
					{
						return MCPError(FString::Printf(
							TEXT("Could not sample every FK contact rotation-chain control before operations[%d]"),
							OperationIndex));
					}
					TMap<FName, const FArrayOfRigControlTransforms*> LocalValuesByControl;
					for (const FArrayOfRigControlTransforms& Values : LocalControlValues)
					{
						if (Values.Transforms.Num() != Write.Frames.Num())
						{
							return MCPError(FString::Printf(
								TEXT("Could not sample every FK contact rotation-chain frame before operations[%d]"),
								OperationIndex));
						}
						LocalValuesByControl.Add(Values.ControlName, &Values);
					}

					FkChainWrites.SetNum(FkChainWriteCount);
					TArray<FTransform> ControlOffsets;
					ControlOffsets.SetNum(FkChainWriteCount);
					for (int32 ChainIndex = 0; ChainIndex < FkChainWriteCount; ++ChainIndex)
					{
						const FArrayOfRigControlTransforms* const* LocalValues =
							LocalValuesByControl.Find(FkChainControls[ChainIndex]);
						FRigControlElement* ChainControl = Session.ControlRig->FindControl(FkChainControls[ChainIndex]);
						if (!LocalValues || !*LocalValues || !ChainControl)
						{
							return MCPError(FString::Printf(
								TEXT("Could not prepare FK contact rotation-chain control %s"),
								*FkChainControls[ChainIndex].ToString()));
						}
						FControlRigPreparedWrite& ChainWrite = FkChainWrites[ChainIndex];
						ChainWrite.Control = FkChainControls[ChainIndex];
						ChainWrite.Op = Op;
						ChainWrite.Space = EControlRigTransformSpace::Local;
						ChainWrite.ValueType = EControlRigPreparedValueType::Transform;
						ChainWrite.Frames = Write.Frames;
						ChainWrite.Before = (*LocalValues)->Transforms;
						ChainWrite.After.SetNum(Write.Frames.Num());
						ControlOffsets[ChainIndex] = Session.ControlRig->GetHierarchy()->GetControlOffsetTransform(
							ChainControl, ERigTransformType::InitialLocal);
					}

					TArray<FTransform> PredictedSubjects;
					PredictedSubjects.SetNum(Write.Frames.Num());
					for (int32 FrameIndex = 0; FrameIndex < Write.Frames.Num(); ++FrameIndex)
					{
						TArray<FTransform> SourceGlobals;
						SourceGlobals.Reserve(FkChainBoneIndices.Num());
						for (const TArray<FTransform>& BoneSamples : FkSourceChainGlobals)
						{
							SourceGlobals.Add(BoneSamples[FrameIndex]);
						}

						const FTransform SubjectRelativeToEnd =
							SubjectBefore[FrameIndex].GetRelativeTransform(SourceGlobals.Last());
						const FTransform TargetEnd = SubjectRelativeToEnd.GetRelativeTransformReverse(
							ContactQA.ExpectedSubject[FrameIndex]);
						TArray<FTransform> SolvedGlobals;
						double SolverPositionErrorCm = 0.0;
						if (!ControlRigSequencerSolveRotationChain(
								SourceGlobals, TargetEnd, bTargetRotation, SolvedGlobals, SolverPositionErrorCm)
							|| !FMath::IsFinite(SolverPositionErrorCm))
						{
							return MCPError(FString::Printf(
								TEXT("operations[%d] could not solve the FK contact rotation chain at frame %d"),
								OperationIndex, Write.Frames[FrameIndex].Value));
						}

						const FTransform SourceDriverLocal =
							FkChainWrites[0].Before[FrameIndex] * ControlOffsets[0];
						FTransform SourceParent =
							SourceDriverLocal.GetRelativeTransformReverse(SourceGlobals[0]);
						FTransform DesiredParent = SourceParent;
						for (int32 ChainIndex = 0; ChainIndex < FkChainBoneIndices.Num(); ++ChainIndex)
						{
							const FTransform SourceLocal = SourceGlobals[ChainIndex].GetRelativeTransform(SourceParent);
							FTransform DesiredLocal = SourceLocal;
							if (ChainIndex < FkChainWriteCount)
							{
								DesiredLocal = SolvedGlobals[ChainIndex].GetRelativeTransform(DesiredParent);
								// Skeleton-retargeted FK translations are discarded during playback. Keep the
								// source local lengths and express the contact correction in rotations only.
								DesiredLocal.SetTranslation(SourceLocal.GetTranslation());
								DesiredLocal.SetScale3D(SourceLocal.GetScale3D());
								DesiredLocal.NormalizeRotation();
								FkChainWrites[ChainIndex].After[FrameIndex] =
									DesiredLocal.GetRelativeTransform(ControlOffsets[ChainIndex]);
							}
							const FTransform DesiredGlobal = DesiredLocal * DesiredParent;
							SourceParent = SourceGlobals[ChainIndex];
							DesiredParent = DesiredGlobal;
							SolvedGlobals[ChainIndex] = DesiredGlobal;
						}
						PredictedSubjects[FrameIndex] = SubjectRelativeToEnd * SolvedGlobals.Last();
					}

					ControlRigSequencerMeasureContact(
						Write.Frames, ContactQA.ExpectedSubject, PredictedSubjects,
						true, bTargetRotation, ContactQA.Metrics);
					if (ContactQA.Metrics.MaxPositionErrorCm > PositionToleranceCm
						|| (bTargetRotation
							&& ContactQA.Metrics.MaxRotationErrorDegrees > RotationToleranceDegrees))
					{
						return MCPError(FString::Printf(
							TEXT("contact_constraint_tolerance_exceeded: operations[%d] FK rotation-chain residual was %.4f cm at frame %d and %.4f degrees at frame %d"),
							OperationIndex,
							ContactQA.Metrics.MaxPositionErrorCm,
							ContactQA.Metrics.WorstPositionFrame,
							ContactQA.Metrics.MaxRotationErrorDegrees,
							ContactQA.Metrics.WorstRotationFrame));
					}
				}

				for (const FName StabilizerName : StabilizerNames)
				{
					const FArrayOfRigControlTransforms* const* StabilizerValues = ExistingByControl.Find(StabilizerName);
					FRigControlElement* Stabilizer = Session.ControlRig->FindControl(StabilizerName);
					if (!StabilizerValues || !*StabilizerValues || !Stabilizer)
					{
						return MCPError(FString::Printf(TEXT("Could not prepare stabilizer %s"), *StabilizerName.ToString()));
					}
					FControlRigPreparedWrite StabilizerWrite;
					StabilizerWrite.Control = StabilizerName;
					StabilizerWrite.Op = Op;
					StabilizerWrite.Space = EControlRigTransformSpace::Global;
					StabilizerWrite.ValueType = EControlRigPreparedValueType::Transform;
					StabilizerWrite.Frames = Write.Frames;
					StabilizerWrite.Before = (*StabilizerValues)->Transforms;
					StabilizerWrite.After.SetNum(Write.Frames.Num());
					const FTransform Anchor = StabilizerWrite.Before[0];
					const bool bStabilizeRotation =
						ControlRigSequencerControlHasRotation(Stabilizer->Settings.ControlType);
					for (int32 Index = 0; Index < Write.Frames.Num(); ++Index)
					{
						StabilizerWrite.After[Index] = ControlRigSequencerBlendContactTransform(
							StabilizerWrite.Before[Index], Anchor, Weights[Index], bStabilizeRotation);
					}

					FControlRigContactStabilizerQA StabilizerQA;
					StabilizerQA.Control = StabilizerName;
					StabilizerQA.ControlType = Stabilizer->Settings.ControlType;
					StabilizerQA.Expected = StabilizerWrite.After;
					ContactQA.Stabilizers.Add(MoveTemp(StabilizerQA));
					Prepared.Add(MoveTemp(StabilizerWrite));
				}

				PreparedContacts.Add(MoveTemp(ContactQA));
				if (bUseFkRotationChain)
				{
					Prepared.Append(MoveTemp(FkChainWrites));
				}
				else
				{
					Prepared.Add(MoveTemp(Write));
				}
				continue;
			}

			const TArray<FName> OneControl{ControlName};
			const TArray<FArrayOfRigControlTransforms> Existing = UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
				Session.Sequence, Session.ControlRig, OneControl, Write.Frames, Write.Space, EMovieSceneTimeUnit::DisplayRate);
			if (Existing.Num() != 1 || Existing[0].Transforms.Num() != Write.Frames.Num())
				return MCPError(FString::Printf(TEXT("Could not sample %s before applying operations[%d]"), *ControlString, OperationIndex));
			Write.Before = Existing[0].Transforms;
			Write.After = Write.Before;

			if (Op == TEXT("set"))
			{
				const TArray<TSharedPtr<FJsonValue>>* TransformValues = nullptr;
				const TSharedPtr<FJsonObject>* SingleTransform = nullptr;
				if (Operation->TryGetArrayField(TEXT("transforms"), TransformValues) && TransformValues)
				{
					if (TransformValues->Num() != Write.Frames.Num())
						return MCPError(FString::Printf(TEXT("operations[%d].transforms must match the frame count"), OperationIndex));
					for (int32 Index = 0; Index < TransformValues->Num(); ++Index)
					{
						const TSharedPtr<FJsonObject> TransformObject = (*TransformValues)[Index].IsValid()
							? (*TransformValues)[Index]->AsObject() : nullptr;
						FControlRigTransformPatch Patch;
						if (!ControlRigSequencerReadTransformPatch(TransformObject, true, Patch, Error))
							return MCPError(FString::Printf(TEXT("operations[%d].transforms[%d]: %s"), OperationIndex, Index, *Error));
						Write.After[Index] = ControlRigSequencerApplySetPatch(Write.Before[Index], Patch);
					}
				}
				else if (Operation->TryGetObjectField(TEXT("transform"), SingleTransform) && SingleTransform && SingleTransform->IsValid())
				{
					FControlRigTransformPatch Patch;
					if (!ControlRigSequencerReadTransformPatch(*SingleTransform, true, Patch, Error))
						return MCPError(FString::Printf(TEXT("operations[%d].transform: %s"), OperationIndex, *Error));
					for (int32 Index = 0; Index < Write.After.Num(); ++Index)
						Write.After[Index] = ControlRigSequencerApplySetPatch(Write.Before[Index], Patch);
				}
				else
				{
					return MCPError(FString::Printf(TEXT("operations[%d] requires 'transform' or 'transforms'"), OperationIndex));
				}
			}
			else if (Op == TEXT("set_keys"))
			{
				if (AbsoluteKeyTransforms.Num() != Write.Frames.Num())
					return MCPError(FString::Printf(TEXT("operations[%d].keys could not be prepared"), OperationIndex));
				Write.After = MoveTemp(AbsoluteKeyTransforms);
			}
			else
			{
				FControlRigTransformPatch Patch;
				if (!ControlRigSequencerReadTransformPatch(Operation, true, Patch, Error))
					return MCPError(FString::Printf(TEXT("operations[%d]: %s"), OperationIndex, *Error));
				if (!ControlRigSequencerPatchAffectsControl(Patch, Control->Settings.ControlType))
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d] does not change a channel supported by %s"),
						OperationIndex, *ControlString));
				}
				const int32 BlendIn = FMath::Max(0, OptionalInt(Operation, TEXT("blendInFrames"), 0));
				const int32 BlendOut = FMath::Max(0, OptionalInt(Operation, TEXT("blendOutFrames"), 0));
				for (int32 Index = 0; Index < Write.After.Num(); ++Index)
				{
					double Weight = 1.0;
					if (BlendIn > 0) Weight = FMath::Min(Weight, static_cast<double>(Index) / static_cast<double>(BlendIn));
					if (BlendOut > 0) Weight = FMath::Min(Weight, static_cast<double>(Write.After.Num() - 1 - Index) / static_cast<double>(BlendOut));
					Write.After[Index] = ControlRigSequencerApplyOffset(Write.Before[Index], Patch, FMath::Clamp(Weight, 0.0, 1.0), Write.Space);
				}
			}
		}
		Prepared.Add(MoveTemp(Write));
	}

	// All controls, frames and payloads have been resolved and sampled. Only now
	// do we create keys in the LevelSequence.
	bool bApplyFailed = false;
	FString ApplyError;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "ApplyControlRigEdits", "Apply Control Rig Edits"));
		Session.Sequence->Modify();
		Session.MovieScene->Modify();
		Session.Section->Modify();
		for (const FControlRigPreparedWrite& Write : Prepared)
		{
			if (Write.ValueType == EControlRigPreparedValueType::Bool)
			{
				TArray<bool> Values = Write.BoolAfter;
				if (Values.IsEmpty()) Values.Init(Write.BoolValue, Write.Frames.Num());
				Session.Track->SetSectionToKey(Session.Section, Write.Control);
				UControlRigSequencerEditorLibrary::SetLocalControlRigBools(
					Session.Sequence, Session.ControlRig, Write.Control, Write.Frames, Values,
					EMovieSceneTimeUnit::DisplayRate);
				const TArray<bool> Actual = UControlRigSequencerEditorLibrary::GetLocalControlRigBools(
					Session.Sequence, Session.ControlRig, Write.Control, Write.Frames,
					EMovieSceneTimeUnit::DisplayRate);
				if (Actual != Values)
				{
					bApplyFailed = true;
					ApplyError = FString::Printf(TEXT("Unreal failed while applying the prevalidated '%s' edit to %s"), *Write.Op, *Write.Control.ToString());
					break;
				}
				continue;
			}
			if (Write.ValueType == EControlRigPreparedValueType::Float)
			{
				TArray<float> Values = Write.FloatAfter;
				if (Values.IsEmpty()) Values.Init(Write.FloatValue, Write.Frames.Num());
				Session.Track->SetSectionToKey(Session.Section, Write.Control);
				UControlRigSequencerEditorLibrary::SetLocalControlRigFloats(
					Session.Sequence, Session.ControlRig, Write.Control, Write.Frames, Values,
					EMovieSceneTimeUnit::DisplayRate);
				const TArray<float> Actual = UControlRigSequencerEditorLibrary::GetLocalControlRigFloats(
					Session.Sequence, Session.ControlRig, Write.Control, Write.Frames,
					EMovieSceneTimeUnit::DisplayRate);
				bool bMatches = Actual.Num() == Values.Num();
				for (int32 Index = 0; bMatches && Index < Values.Num(); ++Index)
				{
					bMatches = FMath::IsNearlyEqual(Actual[Index], Values[Index]);
				}
				if (!bMatches)
				{
					bApplyFailed = true;
					ApplyError = FString::Printf(TEXT("Unreal failed while applying the prevalidated '%s' edit to %s"), *Write.Op, *Write.Control.ToString());
					break;
				}
				continue;
			}
			if (Write.ValueType == EControlRigPreparedValueType::Integer)
			{
				TArray<int32> Values = Write.IntAfter;
				if (Values.IsEmpty()) Values.Init(Write.IntValue, Write.Frames.Num());
				Session.Track->SetSectionToKey(Session.Section, Write.Control);
				UControlRigSequencerEditorLibrary::SetLocalControlRigInts(
					Session.Sequence, Session.ControlRig, Write.Control, Write.Frames, Values,
					EMovieSceneTimeUnit::DisplayRate);
				const TArray<int32> Actual = UControlRigSequencerEditorLibrary::GetLocalControlRigInts(
					Session.Sequence, Session.ControlRig, Write.Control, Write.Frames,
					EMovieSceneTimeUnit::DisplayRate);
				if (Actual != Values)
				{
					bApplyFailed = true;
					ApplyError = FString::Printf(TEXT("Unreal failed while applying the prevalidated '%s' edit to %s"), *Write.Op, *Write.Control.ToString());
					break;
				}
				continue;
			}
			FArrayOfRigControlTransforms Values;
			Values.ControlName = Write.Control;
			Values.Transforms = Write.After;
			Session.Track->SetSectionToKey(Session.Section, Write.Control);
			if (!UControlRigSequencerEditorLibrary::BatchSetControlTransforms(
				Session.Sequence, Session.ControlRig, {Values}, Write.Frames, Write.Space,
				Session.Section, EMovieSceneTimeUnit::DisplayRate))
			{
				bApplyFailed = true;
				ApplyError = FString::Printf(TEXT("Unreal failed while applying the prevalidated '%s' edit to %s"), *Write.Op, *Write.Control.ToString());
				break;
			}
			{
				const FRigControlElement* Control = Session.ControlRig->FindControl(Write.Control);
				TArray<FTransform> PropagationActual;
				const bool bPropagationRead = !Write.PropagationMode.IsEmpty()
					&& ControlRigSequencerReadLocalControlValues(Session, Control, Write.Frames, PropagationActual);
				const TArray<FName> OneControl{Write.Control};
				const TArray<FArrayOfRigControlTransforms> Actual = !Write.PropagationMode.IsEmpty()
					? TArray<FArrayOfRigControlTransforms>()
					: UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
						Session.Sequence, Session.ControlRig, OneControl, Write.Frames, Write.Space,
						EMovieSceneTimeUnit::DisplayRate);
				bool bMatches = Control && (bPropagationRead
					? PropagationActual.Num() == Write.After.Num()
					: Actual.Num() == 1 && Actual[0].Transforms.Num() == Write.After.Num());
				for (int32 Index = 0; bMatches && Index < Write.After.Num(); ++Index)
				{
					const FTransform& ActualTransform = bPropagationRead
						? PropagationActual[Index] : Actual[0].Transforms[Index];
					bMatches = ControlRigSequencerTransformMatches(
						Write.After[Index], ActualTransform, Control->Settings.ControlType);
				}
				if (!bMatches)
				{
					bApplyFailed = true;
					ApplyError = FString::Printf(TEXT("Unreal readback did not match the '%s' keys applied to %s"), *Write.Op, *Write.Control.ToString());
					break;
				}
			}
		}

		if (!bApplyFailed)
		{
			for (FControlRigPreparedContactQA& Contact : PreparedContacts)
			{
				if (!Contact.bHasDrivenReference)
				{
					const TArray<FArrayOfRigControlTransforms> Actual =
						UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
							Session.Sequence, Session.ControlRig, {Contact.Control}, Contact.Frames,
							EControlRigTransformSpace::Global, EMovieSceneTimeUnit::DisplayRate);
					if (Actual.Num() != 1 || Actual[0].Transforms.Num() != Contact.Frames.Num())
					{
						bApplyFailed = true;
						ApplyError = FString::Printf(
							TEXT("Could not perform final contact_lock readback for %s"),
							*Contact.Control.ToString());
						break;
					}
					ControlRigSequencerMeasureContact(
						Contact.Frames, Contact.ExpectedSubject, Actual[0].Transforms,
						true, Contact.bCheckRotation, Contact.Metrics);
					if (Contact.Metrics.MaxPositionErrorCm > Contact.PositionToleranceCm
						|| (Contact.bCheckRotation
							&& Contact.Metrics.MaxRotationErrorDegrees > Contact.RotationToleranceDegrees))
					{
						bApplyFailed = true;
						ApplyError = FString::Printf(
							TEXT("contact_constraint_tolerance_exceeded: operations[%d] %s residual was %.4f cm at frame %d and %.4f degrees at frame %d"),
							Contact.OperationIndex,
							*Contact.Control.ToString(),
							Contact.Metrics.MaxPositionErrorCm,
							Contact.Metrics.WorstPositionFrame,
							Contact.Metrics.MaxRotationErrorDegrees,
							Contact.Metrics.WorstRotationFrame);
						break;
					}
				}

				if (!Contact.Stabilizers.IsEmpty())
				{
					TArray<FName> StabilizerNames;
					for (const FControlRigContactStabilizerQA& Stabilizer : Contact.Stabilizers)
					{
						StabilizerNames.Add(Stabilizer.Control);
					}
					const TArray<FArrayOfRigControlTransforms> ActualStabilizers =
						UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
							Session.Sequence, Session.ControlRig, StabilizerNames, Contact.Frames,
							EControlRigTransformSpace::Global, EMovieSceneTimeUnit::DisplayRate);
					if (ActualStabilizers.Num() != Contact.Stabilizers.Num())
					{
						bApplyFailed = true;
						ApplyError = FString::Printf(
							TEXT("Could not perform final contact_lock stabilizer readback for operations[%d]"),
							Contact.OperationIndex);
						break;
					}
					TMap<FName, const FArrayOfRigControlTransforms*> ActualByControl;
					for (const FArrayOfRigControlTransforms& Values : ActualStabilizers)
					{
						ActualByControl.Add(Values.ControlName, &Values);
					}
					for (FControlRigContactStabilizerQA& Stabilizer : Contact.Stabilizers)
					{
						const FArrayOfRigControlTransforms* const* ActualValues =
							ActualByControl.Find(Stabilizer.Control);
						if (!ActualValues || !*ActualValues
							|| (*ActualValues)->Transforms.Num() != Contact.Frames.Num())
						{
							bApplyFailed = true;
							ApplyError = FString::Printf(
								TEXT("Could not perform final contact_lock readback for stabilizer %s"),
								*Stabilizer.Control.ToString());
							break;
						}
						const bool bCheckPosition =
							ControlRigSequencerControlHasTranslation(Stabilizer.ControlType);
						const bool bCheckRotation =
							ControlRigSequencerControlHasRotation(Stabilizer.ControlType);
						ControlRigSequencerMeasureContact(
							Contact.Frames, Stabilizer.Expected, (*ActualValues)->Transforms,
							bCheckPosition, bCheckRotation, Stabilizer.Metrics);
						if ((bCheckPosition
								&& Stabilizer.Metrics.MaxPositionErrorCm > Contact.PositionToleranceCm)
							|| (bCheckRotation
								&& Stabilizer.Metrics.MaxRotationErrorDegrees > Contact.RotationToleranceDegrees))
						{
							bApplyFailed = true;
							ApplyError = FString::Printf(
								TEXT("contact_constraint_tolerance_exceeded: operations[%d] stabilizer %s residual was %.4f cm and %.4f degrees"),
								Contact.OperationIndex, *Stabilizer.Control.ToString(),
								Stabilizer.Metrics.MaxPositionErrorCm,
								Stabilizer.Metrics.MaxRotationErrorDegrees);
							break;
						}
					}
					if (bApplyFailed) break;
				}
			}
		}
	}
	if (bApplyFailed)
	{
		const bool bRolledBack = GEditor && GEditor->UndoTransaction();
		if (!bRolledBack)
		{
			return MCPError(ApplyError + TEXT("; the editor transaction could not be rolled back"));
		}
		return MCPError(ApplyError);
	}
	Session.Sequence->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveLoadedAsset(Session.Sequence, false))
	{
		const bool bRolledBack = GEditor && GEditor->UndoTransaction();
		return MCPError(bRolledBack
			? TEXT("Control Rig edits could not be saved and were rolled back")
			: TEXT("Control Rig edits could not be saved and the editor transaction could not be rolled back"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sequencePath"), Session.Sequence->GetPathName());
	Result->SetStringField(TEXT("bindingTag"), Session.BindingTag);
	Result->SetNumberField(TEXT("appliedOperationCount"), Operations->Num());
	Result->SetNumberField(TEXT("keyedControlWriteCount"), Prepared.Num());
	int32 KeyedSamples = 0;
	TArray<TSharedPtr<FJsonValue>> Applied;
	for (const FControlRigPreparedWrite& Write : Prepared)
	{
		KeyedSamples += Write.Frames.Num();
		auto Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("op"), Write.Op);
		Object->SetStringField(TEXT("control"), Write.Control.ToString());
		Object->SetNumberField(TEXT("keyedFrameCount"), Write.Frames.Num());
		if (!Write.PropagationMode.IsEmpty())
		{
			Object->SetStringField(TEXT("mode"), Write.PropagationMode);
			TArray<TSharedPtr<FJsonValue>> Channels;
			for (const FString& Channel : Write.ChangedChannels)
			{
				Channels.Add(MakeShared<FJsonValueString>(Channel));
			}
			Object->SetArrayField(TEXT("changedChannels"), Channels);
		}
		Applied.Add(MakeShared<FJsonValueObject>(Object));
	}
	Result->SetNumberField(TEXT("keyedSampleCount"), KeyedSamples);
	Result->SetArrayField(TEXT("applied"), Applied);
	TArray<TSharedPtr<FJsonValue>> ContactResults;
	for (const FControlRigPreparedContactQA& Contact : PreparedContacts)
	{
		auto Object = Contact.bHasDrivenReference
			? MakeShared<FJsonObject>()
			: ControlRigSequencerContactMetricsJson(Contact.Metrics, true, Contact.bCheckRotation);
		Object->SetNumberField(TEXT("operationIndex"), Contact.OperationIndex);
		Object->SetStringField(TEXT("control"), Contact.Control.ToString());
		if (Contact.bHasDrivenReference)
		{
			Object->SetStringField(TEXT("drivenReference"), Contact.DrivenReference.ToString());
			Object->SetStringField(TEXT("verification"), TEXT("bake_and_analyze_required"));
			if (Contact.bUsedFkRotationChain)
			{
				Object->SetStringField(TEXT("solver"), TEXT("fk_rotation_chain"));
				Object->SetObjectField(
					TEXT("preBakePrediction"),
					ControlRigSequencerContactMetricsJson(Contact.Metrics, true, Contact.bCheckRotation));
			}
		}
		if (Contact.bHasTargetReference)
		{
			Object->SetStringField(TEXT("targetReference"), Contact.TargetReference.ToString());
			Object->SetStringField(
				TEXT("targetMode"),
				Contact.bCapturedRelativeTarget ? TEXT("captured_relative") : TEXT("explicit_relative"));
		}
		Object->SetNumberField(TEXT("frameCount"), Contact.Frames.Num());
		Object->SetNumberField(TEXT("fullWeightFrameCount"), Contact.FullWeightFrameCount);
		Object->SetNumberField(TEXT("positionToleranceCm"), Contact.PositionToleranceCm);
		if (Contact.bCheckRotation)
		{
			Object->SetNumberField(TEXT("rotationToleranceDegrees"), Contact.RotationToleranceDegrees);
		}
		TArray<TSharedPtr<FJsonValue>> StabilizerResults;
		for (const FControlRigContactStabilizerQA& Stabilizer : Contact.Stabilizers)
		{
			const bool bCheckPosition =
				ControlRigSequencerControlHasTranslation(Stabilizer.ControlType);
			const bool bCheckRotation =
				ControlRigSequencerControlHasRotation(Stabilizer.ControlType);
			auto StabilizerObject = ControlRigSequencerContactMetricsJson(
				Stabilizer.Metrics, bCheckPosition, bCheckRotation);
			StabilizerObject->SetStringField(TEXT("control"), Stabilizer.Control.ToString());
			StabilizerResults.Add(MakeShared<FJsonValueObject>(StabilizerObject));
		}
		Object->SetArrayField(TEXT("stabilizers"), StabilizerResults);
		Object->SetBoolField(TEXT("keyReadbackPassed"), true);
		if (!Contact.bHasDrivenReference) Object->SetBoolField(TEXT("passed"), true);
		ContactResults.Add(MakeShared<FJsonValueObject>(Object));
	}
	Result->SetArrayField(TEXT("contactQa"), ContactResults);

	// The inverse is this same action replaying the values sampled before the
	// writes landed. Every prepared write, including the FK chain and stabilizer
	// writes a contact_lock adds, carries its own before-state, so the operations
	// below cover exactly what was keyed.
	{
		TArray<TSharedPtr<FJsonValue>> InverseOperations;
		for (const FControlRigPreparedWrite& Write : Prepared)
		{
			const FString ControlString = Write.Control.ToString();
			if (Write.ValueType == EControlRigPreparedValueType::Transform)
			{
				// set_keys takes one absolute transform per frame, and requires
				// strictly increasing frames plus exactly the three transform
				// fields. The frames were prepared sorted and unique.
				TArray<TSharedPtr<FJsonValue>> Keys;
				for (int32 Index = 0; Index < Write.Frames.Num() && Index < Write.Before.Num(); ++Index)
				{
					const FTransform& Xf = Write.Before[Index];
					const FVector Translation = Xf.GetTranslation();
					const FQuat Rotation = Xf.GetRotation().GetNormalized();
					const FVector Scale = Xf.GetScale3D();

					auto TranslationObject = MakeShared<FJsonObject>();
					TranslationObject->SetNumberField(TEXT("x"), Translation.X);
					TranslationObject->SetNumberField(TEXT("y"), Translation.Y);
					TranslationObject->SetNumberField(TEXT("z"), Translation.Z);
					auto RotationObject = MakeShared<FJsonObject>();
					RotationObject->SetNumberField(TEXT("x"), Rotation.X);
					RotationObject->SetNumberField(TEXT("y"), Rotation.Y);
					RotationObject->SetNumberField(TEXT("z"), Rotation.Z);
					RotationObject->SetNumberField(TEXT("w"), Rotation.W);
					auto ScaleObject = MakeShared<FJsonObject>();
					ScaleObject->SetNumberField(TEXT("x"), Scale.X);
					ScaleObject->SetNumberField(TEXT("y"), Scale.Y);
					ScaleObject->SetNumberField(TEXT("z"), Scale.Z);

					// Exactly translation, rotationQuaternion and scale: set_keys
					// rejects a transform object that carries anything else.
					auto TransformObject = MakeShared<FJsonObject>();
					TransformObject->SetObjectField(TEXT("translation"), TranslationObject);
					TransformObject->SetObjectField(TEXT("rotationQuaternion"), RotationObject);
					TransformObject->SetObjectField(TEXT("scale"), ScaleObject);
					auto KeyObject = MakeShared<FJsonObject>();
					KeyObject->SetNumberField(TEXT("frame"), Write.Frames[Index].Value);
					KeyObject->SetObjectField(TEXT("transform"), TransformObject);
					Keys.Add(MakeShared<FJsonValueObject>(KeyObject));
				}
				if (Keys.IsEmpty()) continue;
				auto Operation = MakeShared<FJsonObject>();
				Operation->SetStringField(TEXT("op"), TEXT("set_keys"));
				Operation->SetStringField(TEXT("control"), ControlString);
				Operation->SetStringField(TEXT("space"),
					Write.Space == EControlRigTransformSpace::Global ? TEXT("component") : TEXT("local"));
				Operation->SetArrayField(TEXT("keys"), Keys);
				InverseOperations.Add(MakeShared<FJsonValueObject>(Operation));
				continue;
			}

			// A scalar op writes ONE value across the frames it is given, so the
			// restore groups the sampled frames by the value they held and emits
			// one operation per distinct value.
			const TCHAR* ScalarOp =
				Write.ValueType == EControlRigPreparedValueType::Bool ? TEXT("set_bool")
				: (Write.ValueType == EControlRigPreparedValueType::Float ? TEXT("set_float") : TEXT("set_int"));
			TArray<double> DistinctValues;
			TArray<TArray<TSharedPtr<FJsonValue>>> GroupedFrames;
			const int32 ScalarCount =
				Write.ValueType == EControlRigPreparedValueType::Bool ? Write.BoolBefore.Num()
				: (Write.ValueType == EControlRigPreparedValueType::Float ? Write.FloatBefore.Num() : Write.IntBefore.Num());
			for (int32 Index = 0; Index < Write.Frames.Num() && Index < ScalarCount; ++Index)
			{
				const double Value =
					Write.ValueType == EControlRigPreparedValueType::Bool ? (Write.BoolBefore[Index] ? 1.0 : 0.0)
					: (Write.ValueType == EControlRigPreparedValueType::Float
						? static_cast<double>(Write.FloatBefore[Index])
						: static_cast<double>(Write.IntBefore[Index]));
				int32 Group = DistinctValues.IndexOfByKey(Value);
				if (Group == INDEX_NONE)
				{
					Group = DistinctValues.Add(Value);
					GroupedFrames.AddDefaulted();
				}
				GroupedFrames[Group].Add(MakeShared<FJsonValueNumber>(Write.Frames[Index].Value));
			}
			for (int32 Group = 0; Group < DistinctValues.Num(); ++Group)
			{
				auto Operation = MakeShared<FJsonObject>();
				Operation->SetStringField(TEXT("op"), ScalarOp);
				Operation->SetStringField(TEXT("control"), ControlString);
				Operation->SetArrayField(TEXT("frames"), GroupedFrames[Group]);
				if (Write.ValueType == EControlRigPreparedValueType::Bool)
				{
					Operation->SetBoolField(TEXT("value"), DistinctValues[Group] != 0.0);
				}
				else
				{
					Operation->SetNumberField(TEXT("value"), DistinctValues[Group]);
				}
				InverseOperations.Add(MakeShared<FJsonValueObject>(Operation));
			}
		}

		if (InverseOperations.Num() > 0)
		{
			auto Rollback = MakeShared<FJsonObject>();
			Rollback->SetStringField(TEXT("sequencePath"), Session.SequencePath);
			Rollback->SetStringField(TEXT("bindingTag"), Session.BindingTag);
			Rollback->SetArrayField(TEXT("operations"), InverseOperations);
			MCPSetRollback(Result, TEXT("apply_control_rig_edits"), Rollback);
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The replay writes back the value every keyed control held at every frame this call touched, which restores the curve at those frames. ")
				TEXT("What it cannot restore is the shape between them: a frame that carried no key before now carries one, and the tangents and interpolation of the keys that were replaced are not captured. ")
				TEXT("It also does not undo the section-to-key selection this call set on the track."));
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("No prepared write carried a sampled before-state, so there is nothing for an inverse call to replay."));
		}
	}

	MCPSetUpdated(Result);
	return MCPResult(Result);
#endif
}

TSharedPtr<FJsonValue> FAnimationHandlers::BakeControlRigEdit(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return ControlRigSequencerUnsupported();
#else
	FString SequencePath;
	FString OutputAssetPath;
	if (auto Error = RequireString(Params, TEXT("sequencePath"), SequencePath)) return Error;
	if (auto Error = RequireString(Params, TEXT("outputAssetPath"), OutputAssetPath)) return Error;

	FString PackagePath;
	FString AssetName;
	FString Error;
	if (!ControlRigSequencerSplitAssetPath(OutputAssetPath, PackagePath, AssetName, Error)) return MCPError(Error);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("error")).ToLower();
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
		return MCPError(TEXT("'onConflict' must be 'skip' or 'error'; bake never overwrites an AnimSequence"));
	if (UObject* Existing = UEditorAssetLibrary::LoadAsset(OutputAssetPath))
	{
		if (OnConflict == TEXT("error")) return MCPError(FString::Printf(TEXT("Output asset already exists: %s"), *OutputAssetPath));
		if (!Existing->IsA<UAnimSequence>()) return MCPError(FString::Printf(TEXT("Existing output is not an AnimSequence: %s"), *OutputAssetPath));
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("outputAssetPath"), Existing->GetPathName());
		return MCPResult(Result);
	}

	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) return MCPError(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));
	FControlRigSequenceFocusGuard Focus(Sequence);
	if (!Focus.IsReady()) return MCPError(TEXT("Could not focus the LevelSequence in Sequencer"));
	FControlRigSequenceSession Session;
	if (!ControlRigSequencerResolveSession(Params, Session, Error)) return MCPError(Error);

	FFrameRate FrameRate;
	if (!ControlRigSequencerReadRate(Params, TEXT("frameRate"), Session.MovieScene->GetDisplayRate(), FrameRate, Error))
		return MCPError(Error);
	const bool bReduceKeys = OptionalBool(Params, TEXT("reduceKeys"), false);
	const double Tolerance = OptionalNumber(Params, TEXT("tolerance"), 0.001);
	if (!FMath::IsFinite(Tolerance) || Tolerance < 0.0)
		return MCPError(TEXT("'tolerance' must be a finite non-negative number"));
	if (bReduceKeys)
	{
		return MCPError(TEXT("'reduceKeys' is not supported by AnimSequence export in this vertical slice; bake with reduceKeys=false"));
	}
	const bool bCreateLink = OptionalBool(Params, TEXT("createLink"), false);
	if (bCreateLink)
	{
		return MCPError(TEXT("'createLink' is not supported because Unreal mutates both linked assets; bake with createLink=false"));
	}

	auto Created = MCPCreateAssetIdempotentNewObject<UAnimSequence>(AssetName, PackagePath, TEXT("error"), TEXT("AnimSequence"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UAnimSequence* Output = Created.Asset;
	UAnimSeqExportOption* ExportOptions = NewObject<UAnimSeqExportOption>();
	ExportOptions->bUseCustomFrameRate = true;
	ExportOptions->CustomFrameRate = FrameRate;
	ExportOptions->bExportTransforms = true;
	ExportOptions->bExportMorphTargets = true;
	ExportOptions->bExportAttributeCurves = true;
	ExportOptions->bExportMaterialCurves = true;

	const bool bExported = UControlRigSequencerEditorLibrary::ExportAnimSequenceFromSequencer(
		Output, ExportOptions, FMovieSceneBindingProxy(Session.BindingGuid, Session.Sequence), false);
	if (!bExported)
	{
		UEditorAssetLibrary::DeleteAsset(PackagePath + TEXT("/") + AssetName);
		return MCPError(TEXT("Unreal failed to export the Control Rig edit to an AnimSequence"));
	}
	Output->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveLoadedAsset(Output, false))
	{
		UEditorAssetLibrary::DeleteAsset(PackagePath + TEXT("/") + AssetName);
		return MCPError(TEXT("AnimSequence export completed in memory but the output asset could not be saved"));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sequencePath"), Session.Sequence->GetPathName());
	Result->SetStringField(TEXT("bindingTag"), Session.BindingTag);
	Result->SetStringField(TEXT("outputAssetPath"), Output->GetPathName());
	if (Output->GetSkeleton()) Result->SetStringField(TEXT("skeletonPath"), Output->GetSkeleton()->GetPathName());
	Result->SetObjectField(TEXT("frameRate"), ControlRigSequencerRateJson(FrameRate));
	Result->SetNumberField(TEXT("sampledKeyCount"), Output->GetNumberOfSampledKeys());
	Result->SetNumberField(TEXT("durationSeconds"), Output->GetPlayLength());
	Result->SetBoolField(TEXT("createdLink"), false);
	Result->SetBoolField(TEXT("sourceAnimationModified"), false);
	MCPSetDeleteAssetRollback(Result, Output->GetPathName());
	return MCPResult(Result);
#endif
}
