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
		ERigControlType ControlType = ERigControlType::Transform;
		bool bHasDrivenReference = false;
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
			OutError = FString::Printf(TEXT("Driven bone or socket was not found: %s"), *Reference.ToString());
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
					TEXT("Driven reference %s evaluated to an invalid component-space transform"),
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
		FString ControlString;
		if (!Operation->TryGetStringField(TEXT("op"), Op) || Op.IsEmpty())
			return MCPError(FString::Printf(TEXT("operations[%d].op is required"), OperationIndex));
		if (!Operation->TryGetStringField(TEXT("control"), ControlString) || ControlString.IsEmpty())
			return MCPError(FString::Printf(TEXT("operations[%d].control is required"), OperationIndex));
		Op.ToLowerInline();
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
				TEXT("operations[%d].op must be 'set_keys', 'set', 'offset', 'contact_lock', 'set_bool', 'set_float', or 'set_int'"),
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
		}
		else if (Write.ValueType == EControlRigPreparedValueType::Float)
		{
			const TArray<float> Existing = UControlRigSequencerEditorLibrary::GetLocalControlRigFloats(
				Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
			if (Existing.Num() != Write.Frames.Num())
				return MCPError(FString::Printf(TEXT("Could not sample %s before applying operations[%d]"), *ControlString, OperationIndex));
		}
		else if (Write.ValueType == EControlRigPreparedValueType::Integer)
		{
			const TArray<int32> Existing = UControlRigSequencerEditorLibrary::GetLocalControlRigInts(
				Session.Sequence, Session.ControlRig, ControlName, Write.Frames, EMovieSceneTimeUnit::DisplayRate);
			if (Existing.Num() != Write.Frames.Num())
				return MCPError(FString::Printf(TEXT("Could not sample %s before applying operations[%d]"), *ControlString, OperationIndex));
		}
		else
		{
			if (bContactOperation)
			{
				const TSharedPtr<FJsonObject>* TargetObject = nullptr;
				if (!Operation->TryGetObjectField(TEXT("target"), TargetObject)
					|| !TargetObject || !TargetObject->IsValid())
				{
					return MCPError(FString::Printf(TEXT("operations[%d].target must be an object"), OperationIndex));
				}
				const bool bTargetRotation = (*TargetObject)->HasField(TEXT("rotationQuaternion"));
				if (!(*TargetObject)->HasField(TEXT("translation"))
					|| (*TargetObject)->Values.Num() != (bTargetRotation ? 2 : 1))
				{
					return MCPError(FString::Printf(
						TEXT("operations[%d].target must contain translation and optional rotationQuaternion only"),
						OperationIndex));
				}
				if (bTargetRotation && !ControlRigSequencerControlHasRotation(Control->Settings.ControlType))
				{
					return MCPError(FString::Printf(
						TEXT("contact_lock driver control must support rotation when target.rotationQuaternion is set: %s"),
						*ControlString));
				}

				FVector TargetTranslation;
				FQuat TargetRotation = FQuat::Identity;
				if (!ControlRigSequencerReadVector(*TargetObject, TEXT("translation"), TargetTranslation, Error)
					|| (bTargetRotation && !ControlRigSequencerReadNormalizedQuaternion(*TargetObject, TargetRotation, Error)))
				{
					return MCPError(FString::Printf(TEXT("operations[%d].target: %s"), OperationIndex, *Error));
				}

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

				FControlRigPreparedContactQA ContactQA;
				ContactQA.OperationIndex = OperationIndex;
				ContactQA.Control = ControlName;
				ContactQA.DrivenReference = DrivenReference;
				ContactQA.ControlType = Control->Settings.ControlType;
				ContactQA.bHasDrivenReference = bHasDrivenReference;
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
					SubjectTarget.SetTranslation(TargetTranslation);
					if (bTargetRotation) SubjectTarget.SetRotation(TargetRotation);
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
				TArray<bool> Values;
				Values.Init(Write.BoolValue, Write.Frames.Num());
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
				TArray<float> Values;
				Values.Init(Write.FloatValue, Write.Frames.Num());
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
				TArray<int32> Values;
				Values.Init(Write.IntValue, Write.Frames.Num());
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
				const TArray<FName> OneControl{Write.Control};
				const TArray<FArrayOfRigControlTransforms> Actual = UControlRigSequencerEditorLibrary::BatchGetControlTransforms(
					Session.Sequence, Session.ControlRig, OneControl, Write.Frames, Write.Space,
					EMovieSceneTimeUnit::DisplayRate);
				const FRigControlElement* Control = Session.ControlRig->FindControl(Write.Control);
				bool bMatches = Control && Actual.Num() == 1 && Actual[0].Transforms.Num() == Write.After.Num();
				for (int32 Index = 0; bMatches && Index < Write.After.Num(); ++Index)
				{
					bMatches = ControlRigSequencerTransformMatches(
						Write.After[Index], Actual[0].Transforms[Index], Control->Settings.ControlType);
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
