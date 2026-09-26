// reverse_sequence (#1162). A member of FAnimationHandlers in its own
// translation unit; registration stays in AnimationHandlers.cpp.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Curves/RichCurve.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Named, not anonymous: the module is a unity build and anonymous namespaces
// from neighbouring files merge.
namespace MCPReverseSequence
{
	constexpr float TimeEpsilon = 1.0e-4f;

	// Output key I of a track with Count keys: reverse, then rotate the loop of
	// Count-1 intervals by Offset keys. The last key closes the loop onto the
	// new first key.
	int32 SourceKeyIndex(int32 I, int32 Count, int32 Offset)
	{
		const int32 Last = Count - 1;
		if (Offset == 0 || Last <= 0)
		{
			return Last - I;
		}
		const int32 Rotated = (I == Last) ? Offset : (I + Offset) % Last;
		return Last - Rotated;
	}

	// Wrap a time into [0, Length).
	float WrapTime(float Time, float Length)
	{
		if (Length <= 0.f) return 0.f;
		float Wrapped = FMath::Fmod(Time, Length);
		if (Wrapped < 0.f) Wrapped += Length;
		if (Wrapped >= Length - TimeEpsilon) Wrapped = 0.f;
		return Wrapped;
	}

	// Time in the source clip that output time T reads, after reversal and a
	// rotation of Offset seconds.
	float SourceTimeFor(float T, float Offset, float Length)
	{
		if (Offset <= 0.f) return Length - T;
		float U = T + Offset;
		if (U >= Length - TimeEpsilon) U -= Length;
		return FMath::Clamp(Length - U, 0.f, Length);
	}

	// Mirror a rich curve's keys about the clip length. A key's interpolation
	// mode governs the segment after it, so after reversal each key takes the
	// mode of its old predecessor, and tangents swap sides and flip sign. A
	// stepped (constant) segment also moves its held value onto the key that
	// now starts it; OutAddedKeys counts the key that needs at time zero.
	TArray<FRichCurveKey> ReverseKeys(const TArray<FRichCurveKey>& In, float Length, int32& OutAddedKeys)
	{
		TArray<FRichCurveKey> Out;
		const int32 Num = In.Num();
		Out.Reserve(Num + 1);
		for (int32 J = 0; J < Num; ++J)
		{
			const int32 I = Num - 1 - J;
			const FRichCurveKey& Old = In[I];
			const bool bHasPrev = I >= 1;
			const ERichCurveInterpMode Interp = bHasPrev ? In[I - 1].InterpMode.GetValue() : Old.InterpMode.GetValue();

			FRichCurveKey Key = Old;
			Key.Time = Length - Old.Time;
			Key.InterpMode = Interp;
			Key.Value = (bHasPrev && Interp == RCIM_Constant) ? In[I - 1].Value : Old.Value;
			Key.ArriveTangent = -Old.LeaveTangent;
			Key.LeaveTangent = -Old.ArriveTangent;
			Key.ArriveTangentWeight = Old.LeaveTangentWeight;
			Key.LeaveTangentWeight = Old.ArriveTangentWeight;
			if (Old.TangentWeightMode == RCTWM_WeightedArrive) Key.TangentWeightMode = RCTWM_WeightedLeave;
			else if (Old.TangentWeightMode == RCTWM_WeightedLeave) Key.TangentWeightMode = RCTWM_WeightedArrive;
			Out.Add(Key);
		}

		// Before the new first key the reversed curve holds the old last value.
		// A shifted first key no longer carries it, so pin it at time zero.
		if (Num >= 2 && In[Num - 2].InterpMode == RCIM_Constant && Out[0].Time > TimeEpsilon)
		{
			FRichCurveKey Hold(0.f, In[Num - 1].Value);
			Hold.InterpMode = RCIM_Constant;
			Out.Insert(Hold, 0);
			++OutAddedKeys;
		}
		return Out;
	}

	// Rotate a looping curve's keys by Offset seconds: out(t) = in((t + Offset) mod Length).
	// Keys wrap, and the seam gets a key at 0 and at Length holding in(Offset).
	TArray<FRichCurveKey> RotateKeys(const TArray<FRichCurveKey>& In, float Offset, float Length, int32& OutDroppedKeys)
	{
		if (Offset <= 0.f || Length <= 0.f || In.Num() == 0) return In;

		FRichCurve Source;
		Source.SetKeys(In);

		// The seam key: an existing key at Offset if there is one, otherwise a
		// user-tangent key sampled from the curve.
		FRichCurveKey Seam(Offset, Source.Eval(Offset));
		Seam.InterpMode = RCIM_Linear;
		bool bSeamFromKey = false;
		for (const FRichCurveKey& Key : In)
		{
			if (Key.Time <= Offset + TimeEpsilon)
			{
				Seam.InterpMode = Key.InterpMode;
			}
			if (FMath::IsNearlyEqual(Key.Time, Offset, TimeEpsilon))
			{
				Seam = Key;
				bSeamFromKey = true;
			}
		}
		if (!bSeamFromKey)
		{
			const float Step = FMath::Min(1.0e-3f, 0.5f * FMath::Min(Offset, Length - Offset));
			const float Slope = Step > 0.f
				? (Source.Eval(Offset + Step) - Source.Eval(Offset - Step)) / (2.f * Step)
				: 0.f;
			Seam.TangentMode = RCTM_User;
			Seam.ArriveTangent = Slope;
			Seam.LeaveTangent = Slope;
		}

		TArray<FRichCurveKey> Out;
		Out.Reserve(In.Num() + 2);
		for (const FRichCurveKey& Key : In)
		{
			if (Key.Time < -TimeEpsilon || Key.Time > Length + TimeEpsilon)
			{
				++OutDroppedKeys;
				continue;
			}
			float T = Key.Time - Offset;
			if (T < 0.f) T += Length;
			if (T <= TimeEpsilon || T >= Length - TimeEpsilon)
			{
				continue;
			}
			// Keys at 0 and Length land on the same time; the first one wins.
			const bool bDuplicate = Out.ContainsByPredicate([T](const FRichCurveKey& Existing)
			{
				return FMath::IsNearlyEqual(Existing.Time, T, TimeEpsilon);
			});
			if (bDuplicate) continue;
			FRichCurveKey Moved = Key;
			Moved.Time = T;
			Out.Add(Moved);
		}

		FRichCurveKey Start = Seam;
		Start.Time = 0.f;
		FRichCurveKey End = Seam;
		End.Time = Length;
		Out.Add(Start);
		Out.Add(End);
		Out.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });
		return Out;
	}

	// Every distinct key time across a transform curve's nine channels.
	TArray<float> TransformCurveKeyTimes(const FTransformCurve& Curve)
	{
		TArray<float> Times;
		const FVectorCurve* Channels[3] = { &Curve.TranslationCurve, &Curve.RotationCurve, &Curve.ScaleCurve };
		for (const FVectorCurve* Channel : Channels)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				for (const FRichCurveKey& Key : Channel->FloatCurves[Axis].Keys)
				{
					const bool bKnown = Times.ContainsByPredicate([&Key](float Existing)
					{
						return FMath::IsNearlyEqual(Existing, Key.Time, TimeEpsilon);
					});
					if (!bKnown) Times.Add(Key.Time);
				}
			}
		}
		Times.Sort();
		return Times;
	}
}

// ---------------------------------------------------------------------------
// reverse_sequence - a time-reversed copy of an AnimSequence, or the sequence
// itself reversed with inPlace. Bone tracks, float and transform curves,
// notifies and sync markers all move; cycleOffsetFrames/Seconds then rotates
// the reversed loop. A blendspace sample with RateScale -1 is not a substitute:
// it freezes under the non-legacy sample length calculation.
// Params: sourcePath, destinationPath? | name? + packagePath?, inPlace?,
//         cycleOffsetFrames? | cycleOffsetSeconds?, onConflict? (skip|error)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAnimationHandlers::ReverseSequence(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPReverseSequence;

	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;

	const bool bInPlace = OptionalBool(Params, TEXT("inPlace"), false);
	const FString DestinationPath = OptionalString(Params, TEXT("destinationPath"));
	const FString RequestedName = OptionalString(Params, TEXT("name"));
	const FString RequestedPackagePath = OptionalString(Params, TEXT("packagePath"));
	FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip")).ToLower();
	// Read before anything can fail (#1057).
	double OffsetFramesParam = 0.0;
	double OffsetSecondsParam = 0.0;
	const bool bHasOffsetFrames = TryGetNumberParam(Params, TEXT("cycleOffsetFrames"), OffsetFramesParam);
	const bool bHasOffsetSeconds = TryGetNumberParam(Params, TEXT("cycleOffsetSeconds"), OffsetSecondsParam);
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("onConflict must be 'skip' or 'error'. reverse_sequence never overwrites an existing asset; pass inPlace=true to reverse the source itself."));
	}
	if (bHasOffsetFrames && bHasOffsetSeconds)
	{
		return MCPError(TEXT("Pass cycleOffsetFrames or cycleOffsetSeconds, not both."));
	}
	if (bHasOffsetFrames && !FMath::IsNearlyEqual(OffsetFramesParam, FMath::RoundToDouble(OffsetFramesParam)))
	{
		return MCPError(FString::Printf(TEXT("cycleOffsetFrames must be a whole number of frames, got %g. Use cycleOffsetSeconds for a time."), OffsetFramesParam));
	}

	if (bInPlace && (!DestinationPath.IsEmpty() || !RequestedName.IsEmpty() || !RequestedPackagePath.IsEmpty()))
	{
		return MCPError(TEXT("inPlace=true reverses the source itself, so destinationPath, name and packagePath do not apply. Drop them, or drop inPlace to write a copy."));
	}
	if (!DestinationPath.IsEmpty() && (!RequestedName.IsEmpty() || !RequestedPackagePath.IsEmpty()))
	{
		return MCPError(TEXT("Pass destinationPath, or name with packagePath, not both."));
	}

	UObject* SourceObject = MCPLoadAssetObject(SourcePath);
	UAnimSequence* Source = Cast<UAnimSequence>(SourceObject);
	if (!Source)
	{
		return SourceObject
			? MCPAssetWrongTypeError(SourcePath, SourceObject, TEXT("AnimSequence"))
			: MCPAssetNotFoundError(SourcePath);
	}
	if (bInPlace)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Source, Source->GetPathName(), TEXT("reverse this sequence in place"))) return Blocked;
	}

	const IAnimationDataModel* SourceModel = Source->GetDataModel();
	if (!SourceModel)
	{
		return MCPError(TEXT("The source sequence has no data model, so its tracks cannot be read"));
	}

	const int32 NumFrames = SourceModel->GetNumberOfFrames();
	const int32 NumKeys = SourceModel->GetNumberOfKeys();
	const FFrameRate FrameRate = SourceModel->GetFrameRate();
	const double FrameRateDecimal = FrameRate.AsDecimal();
	const float Length = static_cast<float>(SourceModel->GetPlayLength());

	// The rotation is whole frames, so bone keys move by index and the rest by
	// the matching time.
	int32 OffsetFrames = 0;
	if (bHasOffsetFrames)
	{
		OffsetFrames = static_cast<int32>(FMath::RoundToDouble(OffsetFramesParam));
	}
	else if (bHasOffsetSeconds)
	{
		OffsetFrames = static_cast<int32>(FMath::RoundToDouble(OffsetSecondsParam * FrameRateDecimal));
	}
	if ((bHasOffsetFrames || bHasOffsetSeconds) && NumFrames <= 0)
	{
		return MCPError(TEXT("The sequence has no frames, so there is no loop to rotate. Omit the cycle offset."));
	}
	if (NumFrames > 0)
	{
		OffsetFrames = ((OffsetFrames % NumFrames) + NumFrames) % NumFrames;
	}
	const float OffsetSeconds = FrameRateDecimal > 0.0 ? static_cast<float>(OffsetFrames / FrameRateDecimal) : 0.f;

	// Resolve the target: the source itself, or a fresh duplicate.
	UAnimSequence* Target = Source;
	FString TargetPath = Source->GetPathName();
	if (!bInPlace)
	{
		FString DestPackageName;
		FString DestName;
		if (!DestinationPath.IsEmpty())
		{
			const FMCPAssetPathForms Forms = MCPAssetPathForms(DestinationPath);
			DestPackageName = Forms.PackagePath;
			DestName = Forms.AssetName;
			if (DestName != FPackageName::GetShortName(DestPackageName))
			{
				return MCPError(FString::Printf(
					TEXT("destinationPath '%s' names an asset '%s' that differs from its package name. Use '/Path/Name' or '/Path/Name.Name'."),
					*DestinationPath, *DestName));
			}
		}
		else
		{
			const FString SourcePackageName = Source->GetOutermost()->GetName();
			const FString Folder = RequestedPackagePath.IsEmpty()
				? FPackageName::GetLongPackagePath(SourcePackageName)
				: RequestedPackagePath;
			DestName = RequestedName.IsEmpty() ? Source->GetName() + TEXT("_Reversed") : RequestedName;
			FString TrimmedFolder = Folder;
			while (TrimmedFolder.EndsWith(TEXT("/"))) TrimmedFolder.LeftChopInline(1);
			DestPackageName = TrimmedFolder + TEXT("/") + DestName;
		}

		FText Reason;
		if (DestName.IsEmpty() || !FName(*DestName).IsValidObjectName(Reason))
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a valid asset name: %s"), *DestName, *Reason.ToString()));
		}
		if (!FPackageName::IsValidLongPackageName(DestPackageName, /*bIncludeReadOnlyRoots=*/false, &Reason))
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a valid destination package: %s"), *DestPackageName, *Reason.ToString()));
		}
		if (MCPIsProtectedAssetPath(DestPackageName))
		{
			return MCPProtectedPathError(DestPackageName);
		}
		if (DestPackageName == Source->GetOutermost()->GetName())
		{
			return MCPError(TEXT("The destination is the source itself. Pass inPlace=true to reverse it in place."));
		}

		const FString DestObjectPath = DestPackageName + TEXT(".") + DestName;
		FMCPAssetPathForms DestForms = MCPAssetPathForms(DestObjectPath);
		UObject* Existing = FindObject<UObject>(nullptr, *DestObjectPath);
		if (!MCPIsLiveAssetObject(Existing))
		{
			Existing = MCPAssetExistsWithoutLoading(DestForms) ? MCPLoadAssetObject(DestObjectPath) : nullptr;
		}
		if (Existing || MCPAssetExistsWithoutLoading(DestForms))
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("Destination '%s' already exists"), *DestObjectPath));
			}
			auto Res = MCPSuccess();
			MCPSetExisted(Res);
			Res->SetStringField(TEXT("path"), Existing ? Existing->GetPathName() : DestObjectPath);
			Res->SetStringField(TEXT("sourcePath"), Source->GetPathName());
			Res->SetStringField(TEXT("note"), TEXT("The destination already exists and was left untouched; it was not re-reversed. Pass onConflict='error' to be told instead, or a new destination."));
			return MCPResult(Res);
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* Duplicate = AssetTools.DuplicateAsset(DestName, FPackageName::GetLongPackagePath(DestPackageName), Source);
		Target = Cast<UAnimSequence>(Duplicate);
		if (!Target)
		{
			return MCPError(FString::Printf(TEXT("Duplicating '%s' to '%s' failed"), *Source->GetPathName(), *DestObjectPath));
		}
		TargetPath = Target->GetPathName();
	}

	const IAnimationDataModel* Model = Target->GetDataModel();
	if (!Model)
	{
		return MCPError(TEXT("The target sequence has no data model, so its tracks cannot be written"));
	}

	// Read everything before writing anything.
	TArray<FName> BoneNames;
	Model->GetBoneTrackNames(BoneNames);
	TMap<FName, TArray<FTransform>> BoneKeys;
	for (const FName& Bone : BoneNames)
	{
		TArray<FTransform>& Keys = BoneKeys.Add(Bone);
		Model->GetBoneTrackTransforms(Bone, Keys);
	}
	const TArray<FFloatCurve> FloatCurves = Model->GetFloatCurves();
	const TArray<FTransformCurve> TransformCurves = Model->GetTransformCurves();
	const int32 AttributeCount = Model->GetNumberOfAttributes();

	int32 BoneTracksReversed = 0;
	int32 FloatCurvesReversed = 0;
	int32 TransformCurvesReversed = 0;
	int32 CurveKeysAdded = 0;
	int32 CurveKeysDropped = 0;
	FString FailErr;

	IAnimationDataController& Controller = Target->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "ReverseSequence", "MCP Reverse Sequence"), false);

	for (const FName& Bone : BoneNames)
	{
		const TArray<FTransform>& Keys = BoneKeys.FindChecked(Bone);
		const int32 Count = Keys.Num();
		if (Count == 0) continue;
		const int32 TrackOffset = Count - 1 > 0 ? OffsetFrames % (Count - 1) : 0;

		TArray<FVector> Locations;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
		Locations.Reserve(Count);
		Rotations.Reserve(Count);
		Scales.Reserve(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			const FTransform& Key = Keys[SourceKeyIndex(I, Count, TrackOffset)];
			Locations.Add(Key.GetLocation());
			Rotations.Add(Key.GetRotation());
			Scales.Add(Key.GetScale3D());
		}
		if (!Controller.SetBoneTrackKeys(Bone, Locations, Rotations, Scales, false))
		{
			FailErr = FString::Printf(TEXT("writing the reversed keys of bone track '%s' failed"), *Bone.ToString());
			break;
		}
		++BoneTracksReversed;
	}

	if (FailErr.IsEmpty())
	{
		for (const FFloatCurve& Curve : FloatCurves)
		{
			TArray<FRichCurveKey> Keys = ReverseKeys(Curve.FloatCurve.Keys, Length, CurveKeysAdded);
			Keys = RotateKeys(Keys, OffsetSeconds, Length, CurveKeysDropped);
			const FAnimationCurveIdentifier CurveId(Curve.GetName(), ERawCurveTrackTypes::RCT_Float);
			if (!Controller.SetCurveKeys(CurveId, Keys, false))
			{
				FailErr = FString::Printf(TEXT("writing the reversed keys of curve '%s' failed"), *Curve.GetName().ToString());
				break;
			}
			++FloatCurvesReversed;
		}
	}

	if (FailErr.IsEmpty())
	{
		for (const FTransformCurve& Curve : TransformCurves)
		{
			// Key times of the reversed, rotated curve, each sampled from the
			// source curve at the time it now plays.
			TArray<float> Times;
			for (const float SourceTime : TransformCurveKeyTimes(Curve))
			{
				const float T = OffsetSeconds > 0.f ? WrapTime(Length - SourceTime - OffsetSeconds, Length) : Length - SourceTime;
				const bool bKnown = Times.ContainsByPredicate([T](float Existing) { return FMath::IsNearlyEqual(Existing, T, TimeEpsilon); });
				if (!bKnown) Times.Add(T);
			}
			if (OffsetSeconds > 0.f)
			{
				if (!Times.ContainsByPredicate([](float Existing) { return FMath::IsNearlyEqual(Existing, 0.f, TimeEpsilon); })) Times.Add(0.f);
				if (!Times.ContainsByPredicate([Length](float Existing) { return FMath::IsNearlyEqual(Existing, Length, TimeEpsilon); })) Times.Add(Length);
			}
			Times.Sort();

			TArray<FTransform> Values;
			Values.Reserve(Times.Num());
			for (const float T : Times)
			{
				Values.Add(Curve.Evaluate(SourceTimeFor(T, OffsetSeconds, Length), 1.f));
			}

			// Replace rather than merge: drop the curve and add it back with
			// its flags and colour before writing the new keys.
			const FAnimationCurveIdentifier CurveId(Curve.GetName(), ERawCurveTrackTypes::RCT_Transform);
			const int32 Flags = Curve.GetCurveTypeFlags();
			Controller.RemoveCurve(CurveId, false);
			if (!Controller.AddCurve(CurveId, Flags, false)
				|| !Controller.SetTransformCurveKeys(CurveId, Values, Times, false))
			{
				FailErr = FString::Printf(TEXT("writing the reversed keys of transform curve '%s' failed"), *Curve.GetName().ToString());
				break;
			}
#if WITH_EDITORONLY_DATA
			Controller.SetCurveColor(CurveId, Curve.Color, false);
#endif
			++TransformCurvesReversed;
		}
	}

	Controller.CloseBracket(false);

	if (!FailErr.IsEmpty())
	{
		if (!bInPlace)
		{
			// Half a reversal must not survive under the requested name.
			UEditorAssetLibrary::DeleteLoadedAsset(Target);
			return MCPError(FString::Printf(TEXT("reverse_sequence failed, and the partial copy was deleted: %s"), *FailErr));
		}
		return MCPError(FString::Printf(
			TEXT("reverse_sequence failed part-way through an in-place reversal: %s. '%s' is partly reversed (%d of %d bone tracks, %d float curves, %d transform curves written) and is not saved. ")
			TEXT("Replaying would reverse the written tracks back, so revert the package instead."),
			*FailErr, *TargetPath, BoneTracksReversed, BoneNames.Num(), FloatCurvesReversed, TransformCurvesReversed));
	}

	// Notifies: a point notify at t plays at Length - t; a state window
	// [t, t + d] plays at [Length - t - d, Length - t].
	int32 NotifiesReversed = 0;
	int32 NotifyStatesReversed = 0;
	int32 NotifyStatesClamped = 0;
	for (FAnimNotifyEvent& Notify : Target->Notifies)
	{
		const float Start = Notify.GetTime();
		const float Duration = Notify.NotifyStateClass ? Notify.GetDuration() : 0.f;
		float NewStart = FMath::Clamp(Length - Start - Duration, 0.f, Length);
		float NewDuration = Duration;
		if (OffsetSeconds > 0.f)
		{
			NewStart = WrapTime(NewStart - OffsetSeconds, Length);
			if (NewStart + NewDuration > Length + TimeEpsilon)
			{
				NewDuration = Length - NewStart;
				++NotifyStatesClamped;
			}
		}

		Notify.Link(Target, NewStart);
		Notify.TriggerTimeOffset = GetTriggerTimeOffsetForType(Target->CalculateOffsetForNotify(NewStart));
		if (Notify.NotifyStateClass)
		{
			Notify.EndLink.Link(Target, NewStart + NewDuration);
			Notify.SetDuration(NewDuration);
			Notify.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(Target->CalculateOffsetForNotify(NewStart + NewDuration));
			++NotifyStatesReversed;
		}
		else
		{
			++NotifiesReversed;
		}
	}
	Target->SortNotifies();
	Target->RefreshCacheData();

	// Sync markers mirror like point notifies.
	int32 SyncMarkersReversed = 0;
	for (FAnimSyncMarker& Marker : Target->AuthoredSyncMarkers)
	{
		float NewTime = FMath::Clamp(Length - Marker.Time, 0.f, Length);
		if (OffsetSeconds > 0.f)
		{
			NewTime = WrapTime(NewTime - OffsetSeconds, Length);
		}
		Marker.Time = NewTime;
		++SyncMarkersReversed;
	}
	if (SyncMarkersReversed > 0)
	{
		Target->AuthoredSyncMarkers.Sort([](const FAnimSyncMarker& A, const FAnimSyncMarker& B) { return A.Time < B.Time; });
		Target->RefreshSyncMarkerDataFromAuthored();
	}

	Target->PostEditChange();
	Target->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Target, SaveError);

	auto Result = MCPSuccess();
	if (bInPlace)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		MCPSetCreated(Result);
	}
	Result->SetStringField(TEXT("path"), TargetPath);
	Result->SetStringField(TEXT("sourcePath"), Source->GetPathName());
	Result->SetBoolField(TEXT("inPlace"), bInPlace);
	Result->SetNumberField(TEXT("numFrames"), NumFrames);
	Result->SetNumberField(TEXT("numKeys"), NumKeys);
	Result->SetNumberField(TEXT("frameRate"), FrameRateDecimal);
	Result->SetNumberField(TEXT("playLength"), Length);
	Result->SetNumberField(TEXT("cycleOffsetFrames"), OffsetFrames);
	Result->SetNumberField(TEXT("cycleOffsetSeconds"), OffsetSeconds);
	Result->SetBoolField(TEXT("saved"), bSaved);

	TSharedPtr<FJsonObject> Reversed = MakeShared<FJsonObject>();
	Reversed->SetNumberField(TEXT("boneTracks"), BoneTracksReversed);
	Reversed->SetNumberField(TEXT("floatCurves"), FloatCurvesReversed);
	Reversed->SetNumberField(TEXT("transformCurves"), TransformCurvesReversed);
	Reversed->SetNumberField(TEXT("notifies"), NotifiesReversed);
	Reversed->SetNumberField(TEXT("notifyStates"), NotifyStatesReversed);
	Reversed->SetNumberField(TEXT("syncMarkers"), SyncMarkersReversed);
	Result->SetObjectField(TEXT("reversed"), Reversed);

	TArray<FString> Warnings;
	if (AttributeCount > 0)
	{
		Warnings.Add(FString::Printf(TEXT("%d animation attribute track(s) were not reversed; they still play forward."), AttributeCount));
	}
	if (NotifyStatesClamped > 0)
	{
		Warnings.Add(FString::Printf(TEXT("%d notify state(s) would have crossed the loop seam after the cycle offset and were shortened to end at the clip end."), NotifyStatesClamped));
	}
	if (CurveKeysDropped > 0)
	{
		Warnings.Add(FString::Printf(TEXT("%d curve key(s) outside the clip were dropped by the cycle offset."), CurveKeysDropped));
	}
	if (!bSaved)
	{
		Warnings.Add(FString::Printf(TEXT("The reversed sequence is in memory but was not saved: %s"), *SaveError));
	}
	if (Warnings.Num() > 0)
	{
		Result->SetArrayField(TEXT("warnings"), MCPStringListToJson(Warnings));
	}

	if (bInPlace)
	{
		// Reversal undoes itself, and so does the same rotation applied after
		// it, provided the last key of the loop repeats the first.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("sourcePath"), TargetPath);
		Payload->SetBoolField(TEXT("inPlace"), true);
		if (OffsetFrames != 0)
		{
			Payload->SetNumberField(TEXT("cycleOffsetFrames"), OffsetFrames);
		}
		MCPSetRollback(Result, TEXT("reverse_sequence"), Payload);
		const bool bLossy = OffsetFrames != 0 || CurveKeysAdded > 0 || CurveKeysDropped > 0 || NotifyStatesClamped > 0;
		Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
		Result->SetStringField(TEXT("rollbackNote"), bLossy
			? TEXT("Reversing again with the same cycleOffsetFrames restores the sequence when its last key repeats its first. The cycle offset rewrote the last key to close the loop, and any added, dropped or shortened curve keys or notify windows reported here are not restored. Keep a duplicate of the source when an exact restore matters.")
			: TEXT("Reversing the sequence again in place restores it exactly."));
	}
	else
	{
		MCPSetDeleteAssetRollback(Result, TargetPath);
	}

	return MCPResult(Result);
}
