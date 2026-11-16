// compose_pose_sequence (#1230): author a new AnimSequence from poses sampled out
// of other sequences, blended per bone. A member of FAnimationHandlers in its own
// translation unit; registration stays in AnimationHandlers.cpp.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerAnimTrackPose.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "ReferenceSkeleton.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Named, not anonymous: the module is a unity build and anonymous namespaces
// from neighbouring files merge.
namespace MCPComposePose
{
	constexpr int32 MaxSources = 32;
	constexpr int32 MaxLayers = 64;
	constexpr int32 MaxFrames = 10000;

	struct FSource
	{
		UAnimSequence* Sequence = nullptr;
		double Time = 0.0;
		int32 MissingBones = 0;
		TArray<FTransform> Pose;
	};

	struct FLayer
	{
		int32 Source = 0;
		double Weight = 1.0;
		bool bRotationOnly = false;
		TArray<int32> Bones;
	};

	/** Slerp the rotation and lerp translation and scale, unless rotation only. */
	FTransform Blend(const FTransform& A, const FTransform& B, double Alpha, bool bRotationOnly)
	{
		FTransform Out = A;
		Out.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha).GetNormalized());
		if (!bRotationOnly)
		{
			Out.SetLocation(FMath::Lerp(A.GetLocation(), B.GetLocation(), Alpha));
			Out.SetScale3D(FMath::Lerp(A.GetScale3D(), B.GetScale3D(), Alpha));
		}
		return Out;
	}

	TArray<FString> ReadStrings(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Key, Arr) && Arr) return JsonArrayToStringList(Arr);
		FString Single;
		if (Obj->TryGetStringField(Key, Single) && !Single.IsEmpty()) return { Single };
		return {};
	}

	/** Resolve a layer's bone filter against the output skeleton. Returns an error string, or empty. */
	FString MatchLayerBones(const TSharedPtr<FJsonObject>& Obj, int32 Index, const FReferenceSkeleton& Ref, TArray<int32>& OutBones)
	{
		const TArray<FString> Names = ReadStrings(Obj, TEXT("bones"));
		const TArray<FString> Contains = ReadStrings(Obj, TEXT("nameContains"));
		const TArray<FString> Excludes = ReadStrings(Obj, TEXT("excludeContains"));
		bool bDescendants = false;
		Obj->TryGetBoolField(TEXT("includeDescendants"), bDescendants);
		if (Names.Num() == 0 && Contains.Num() == 0)
		{
			return FString::Printf(TEXT("layers[%d] needs bones or nameContains"), Index);
		}

		TSet<int32> Picked;
		for (const FString& Name : Names)
		{
			const int32 Bone = Ref.FindBoneIndex(FName(*Name));
			if (Bone == INDEX_NONE)
			{
				return FString::Printf(TEXT("layers[%d]: bone '%s' is not on the output skeleton"), Index, *Name);
			}
			Picked.Add(Bone);
		}
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
		{
			const FString BoneName = Ref.GetBoneName(Bone).ToString();
			for (const FString& Fragment : Contains)
			{
				if (BoneName.Contains(Fragment, ESearchCase::IgnoreCase)) { Picked.Add(Bone); break; }
			}
		}
		if (bDescendants)
		{
			// The reference skeleton lists parents before children, so one pass suffices.
			for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
			{
				const int32 Parent = Ref.GetParentIndex(Bone);
				if (Parent != INDEX_NONE && Picked.Contains(Parent)) Picked.Add(Bone);
			}
		}
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
		{
			if (!Picked.Contains(Bone)) continue;
			const FString BoneName = Ref.GetBoneName(Bone).ToString();
			const bool bExcluded = Excludes.ContainsByPredicate([&BoneName](const FString& Fragment)
			{
				return BoneName.Contains(Fragment, ESearchCase::IgnoreCase);
			});
			if (!bExcluded) OutBones.Add(Bone);
		}
		if (OutBones.Num() == 0)
		{
			return FString::Printf(TEXT("layers[%d] matches no bone of the output skeleton"), Index);
		}
		return FString();
	}
}

TSharedPtr<FJsonValue> FAnimationHandlers::ComposePoseSequence(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPComposePose;

	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const TArray<TSharedPtr<FJsonValue>>* SourcesArr = nullptr;
	const bool bHasSources = TryGetArrayParam(Params, TEXT("sources"), SourcesArr) && SourcesArr;
	const TArray<TSharedPtr<FJsonValue>>* LayersArr = nullptr;
	const bool bHasLayers = TryGetArrayParam(Params, TEXT("layers"), LayersArr) && LayersArr;
	const double NumFramesParam = OptionalNumber(Params, TEXT("numFrames"), 1.0);
	const double FrameRateParam = OptionalNumber(Params, TEXT("frameRate"), 30.0);
	int32 EaseTo = INDEX_NONE;
	const bool bHasEase = TryGetNumberParam(Params, TEXT("easeToSource"), EaseTo);
	const FString EaseCurve = OptionalString(Params, TEXT("easeCurve"), TEXT("linear")).ToLower();

	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("onConflict must be 'skip' or 'error'; compose_pose_sequence never overwrites"));
	}
	if (!bHasSources || SourcesArr->Num() == 0) return MCPError(TEXT("sources must be a non-empty array of {assetPath, frame?|time?}"));
	if (SourcesArr->Num() > MaxSources)
	{
		return MCPError(FString::Printf(TEXT("sources holds %d entries; the limit is %d. Compose in stages: build intermediate sequences from %d or fewer sources each, then compose those."),
			SourcesArr->Num(), MaxSources, MaxSources));
	}
	if (bHasLayers && LayersArr->Num() > MaxLayers)
	{
		return MCPError(FString::Printf(TEXT("layers holds %d entries; the limit is %d. Compose in stages: apply %d or fewer layers, then layer the result again."),
			LayersArr->Num(), MaxLayers, MaxLayers));
	}
	if (NumFramesParam < 1.0 || NumFramesParam > MaxFrames || NumFramesParam != FMath::FloorToDouble(NumFramesParam))
	{
		return MCPError(FString::Printf(TEXT("numFrames must be a whole number from 1 to %d"), MaxFrames));
	}
	if (FrameRateParam < 1.0 || FrameRateParam > 1000.0 || FrameRateParam != FMath::FloorToDouble(FrameRateParam))
	{
		return MCPError(TEXT("frameRate must be a whole number from 1 to 1000"));
	}
	const int32 NumFrames = (int32)NumFramesParam;
	const int32 FrameRate = (int32)FrameRateParam;
	if (EaseCurve != TEXT("linear") && EaseCurve != TEXT("smooth")) return MCPError(TEXT("easeCurve must be 'linear' (default) or 'smooth'"));
	if (bHasEase && (EaseTo < 0 || EaseTo >= SourcesArr->Num()))
	{
		return MCPError(FString::Printf(TEXT("easeToSource %d is not an index into sources (0 to %d)"), EaseTo, SourcesArr->Num() - 1));
	}

	// Load and sample every source before anything is created.
	TArray<FSource> Sources;
	for (int32 Index = 0; Index < SourcesArr->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!(*SourcesArr)[Index].IsValid() || !(*SourcesArr)[Index]->TryGetObject(Obj) || !Obj)
		{
			return MCPError(FString::Printf(TEXT("sources[%d] must be an object {assetPath, frame?|time?}"), Index));
		}
		FString Path;
		if (!(*Obj)->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("sources[%d] needs assetPath"), Index));
		}
		FSource Source;
		if (auto Err = MCPAnimTrackPose::LoadSequence(Path, Source.Sequence)) return Err;
		if (!Source.Sequence->GetSkeleton() || !Source.Sequence->GetDataModel())
		{
			return MCPError(FString::Printf(TEXT("sources[%d] '%s' has no skeleton or data model"), Index, *Path));
		}
		double Frame = 0.0, Time = 0.0;
		const bool bFrame = (*Obj)->TryGetNumberField(TEXT("frame"), Frame);
		const bool bTime = (*Obj)->TryGetNumberField(TEXT("time"), Time);
		if (bFrame && bTime) return MCPError(FString::Printf(TEXT("sources[%d]: pass frame or time, not both"), Index));
		if (bTime)
		{
			if (Time < 0.0 || Time > Source.Sequence->GetPlayLength() + UE_KINDA_SMALL_NUMBER)
			{
				return MCPError(FString::Printf(TEXT("sources[%d].time %g is outside 0 to %g seconds"), Index, Time, Source.Sequence->GetPlayLength()));
			}
			Source.Time = Time;
		}
		else
		{
			FString FrameError;
			if (!MCPAnimTrackPose::FrameToTime(Source.Sequence, (int32)Frame, Source.Time, FrameError))
			{
				return MCPError(FString::Printf(TEXT("sources[%d]: %s"), Index, *FrameError));
			}
		}
		Sources.Add(Source);
	}

	USkeleton* Skeleton = Sources[0].Sequence->GetSkeleton();
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	const int32 BoneCount = Ref.GetNum();
	for (FSource& Source : Sources)
	{
		const FReferenceSkeleton& SourceRef = Source.Sequence->GetSkeleton()->GetReferenceSkeleton();
		Source.Pose.SetNum(BoneCount);
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			const int32 SourceBone = SourceRef.FindBoneIndex(Ref.GetBoneName(Bone));
			if (SourceBone == INDEX_NONE)
			{
				Source.Pose[Bone] = Ref.GetRefBonePose()[Bone];
				++Source.MissingBones;
				continue;
			}
			Source.Pose[Bone] = MCPAnimTrackPose::LocalBoneAtTime(Source.Sequence, SourceRef, SourceBone, Source.Time);
		}
	}

	TArray<FLayer> Layers;
	if (bHasLayers)
	{
		for (int32 Index = 0; Index < LayersArr->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*LayersArr)[Index].IsValid() || !(*LayersArr)[Index]->TryGetObject(Obj) || !Obj)
			{
				return MCPError(FString::Printf(TEXT("layers[%d] must be an object"), Index));
			}
			FLayer Layer;
			double SourceIndex = -1.0;
			if (!(*Obj)->TryGetNumberField(TEXT("source"), SourceIndex) || SourceIndex < 0 || SourceIndex >= Sources.Num()
				|| SourceIndex != FMath::FloorToDouble(SourceIndex))
			{
				return MCPError(FString::Printf(TEXT("layers[%d].source must be an index into sources (0 to %d)"), Index, Sources.Num() - 1));
			}
			Layer.Source = (int32)SourceIndex;
			(*Obj)->TryGetNumberField(TEXT("weight"), Layer.Weight);
			if (Layer.Weight < 0.0 || Layer.Weight > 1.0)
			{
				return MCPError(FString::Printf(TEXT("layers[%d].weight %g is outside 0 to 1"), Index, Layer.Weight));
			}
			(*Obj)->TryGetBoolField(TEXT("rotationOnly"), Layer.bRotationOnly);
			const FString Error = MatchLayerBones(*Obj, Index, Ref, Layer.Bones);
			if (!Error.IsEmpty()) return MCPError(Error);
			Layers.Add(Layer);
		}
	}

	// Frame 0: the first source's pose with every layer blended over it in order.
	TArray<FTransform> Composed = Sources[0].Pose;
	for (const FLayer& Layer : Layers)
	{
		for (const int32 Bone : Layer.Bones)
		{
			Composed[Bone] = Blend(Composed[Bone], Sources[Layer.Source].Pose[Bone], Layer.Weight, Layer.bRotationOnly);
		}
	}

	if (PackagePath.IsEmpty())
	{
		PackagePath = FPackageName::GetLongPackagePath(Sources[0].Sequence->GetOutermost()->GetName());
	}
	auto Created = MCPCreateAssetIdempotentNewObject<UAnimSequence>(Name, PackagePath, OnConflict, TEXT("AnimSequence"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UAnimSequence* NewSeq = Created.Asset;
	const FString AssetPath = NewSeq->GetPathName();
	NewSeq->SetSkeleton(Skeleton);

	const int32 NumKeys = NumFrames + 1;
	FString FailErr;
	IAnimationDataController& Controller = NewSeq->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(NSLOCTEXT("MCP", "ComposePoseSequence", "MCP Compose Pose Sequence"));
	Controller.SetFrameRate(FFrameRate(FrameRate, 1));
	Controller.SetNumberOfFrames(NumFrames);
	for (int32 Bone = 0; Bone < BoneCount && FailErr.IsEmpty(); ++Bone)
	{
		const FName BoneName = Ref.GetBoneName(Bone);
		TArray<FVector> Locations;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
		for (int32 Key = 0; Key < NumKeys; ++Key)
		{
			FTransform Value = Composed[Bone];
			if (bHasEase)
			{
				double Alpha = (double)Key / NumFrames;
				if (EaseCurve == TEXT("smooth")) Alpha = Alpha * Alpha * (3.0 - 2.0 * Alpha);
				Value = Blend(Composed[Bone], Sources[EaseTo].Pose[Bone], Alpha, false);
			}
			Locations.Add(Value.GetLocation());
			Rotations.Add(Value.GetRotation());
			Scales.Add(Value.GetScale3D());
		}
		Controller.AddBoneCurve(BoneName);
		if (!Controller.SetBoneTrackKeys(BoneName, Locations, Rotations, Scales))
		{
			FailErr = FString::Printf(TEXT("writing bone track '%s' failed"), *BoneName.ToString());
		}
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	if (GEditor)
	{
		// A creation bracket left in the undo buffer breaks later editor edits of the asset.
		GEditor->ResetTransaction(NSLOCTEXT("MCP", "ComposePoseSequenceReset", "MCP Compose Pose Sequence Complete"));
	}

	if (!FailErr.IsEmpty())
	{
		UEditorAssetLibrary::DeleteLoadedAsset(NewSeq);
		return MCPError(FString::Printf(TEXT("compose_pose_sequence failed, and the partial sequence was deleted: %s"), *FailErr));
	}
	NewSeq->PostEditChange();
	NewSeq->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPAnimSaveOutcome(Result, NewSeq, AssetPath);
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("numFrames"), NumFrames);
	Result->SetNumberField(TEXT("numKeys"), NumKeys);
	Result->SetNumberField(TEXT("frameRate"), FrameRate);
	Result->SetNumberField(TEXT("playLength"), NewSeq->GetPlayLength());
	Result->SetNumberField(TEXT("boneTracks"), BoneCount);

	TArray<TSharedPtr<FJsonValue>> SourceRows;
	for (const FSource& Source : Sources)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), Source.Sequence->GetPathName());
		Row->SetNumberField(TEXT("time"), Source.Time);
		Row->SetNumberField(TEXT("missingBones"), Source.MissingBones);
		SourceRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("sources"), SourceRows);

	TArray<TSharedPtr<FJsonValue>> LayerRows;
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FLayer& Layer = Layers[Index];
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetNumberField(TEXT("source"), Layer.Source);
		Row->SetNumberField(TEXT("weight"), Layer.Weight);
		Row->SetNumberField(TEXT("bonesMatched"), Layer.Bones.Num());
		TArray<FString> Names;
		for (int32 Bone : Layer.Bones) Names.Add(Ref.GetBoneName(Bone).ToString());
		Row->SetArrayField(TEXT("bones"), MCPStringListToJson(Names));
		LayerRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("layers"), LayerRows);
	MCPSetDeleteAssetRollback(Result, AssetPath);
	return MCPResult(Result);
}
