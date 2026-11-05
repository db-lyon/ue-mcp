// transform_skinned_part (#1567, #1614): move one bone's reference pose and every
// source vertex skinned to it (or to a bone below it), in every LOD, in one edit.
// A member of FSkeletalMeshHandlers in its own translation unit; registration
// stays in SkeletalMeshHandlers.cpp.
//
// Vertices are moved in place in each LOD's source MeshDescription. Triangles,
// edge hardness and vertex instances are never rebuilt, so a LOD whose build
// recomputes normals recomputes them over the same hard edges, and a LOD that
// keeps its imported normals keeps them (turned with the part on a rotation).
// Every LOD is read and checked before the bone or any vertex is written.

#include "SkeletalMeshHandlers.h"
#include "HandlerUtils.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"

#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API
#include "Animation/Skeleton.h"
#include "BoneWeights.h"
#include "MeshDescription.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "RenderingThread.h"
#include "ScopedTransaction.h"
#include "SkeletalMeshAttributes.h"
#endif

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API
// Named, not anonymous: the module is a unity build and anonymous namespaces
// from neighbouring files merge.
namespace MCPSkinnedPart
{
	/** A source vertex below this weight on the moved bones stays where it is. */
	constexpr float MinWeight = 1.0e-4f;
	/** A render vertex that travelled less than this (cm) did not move. */
	constexpr float MovedTolerance = 1.0e-3f;

	/** The rigid move, in component space: rotate about Pivot, then translate. */
	struct FMove
	{
		FVector Pivot = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Translation = FVector::ZeroVector;

		FVector Apply(const FVector& P, float Weight) const
		{
			const FVector Full = Rotation.RotateVector(P - Pivot) + Pivot + Translation;
			return FMath::Lerp(P, Full, static_cast<double>(Weight));
		}

		FQuat RotationAt(float Weight) const
		{
			return FQuat::Slerp(FQuat::Identity, Rotation, Weight).GetNormalized();
		}

		bool HasRotation() const { return !Rotation.Equals(FQuat::Identity, 1.0e-8); }
	};

	/** One vertex instance's frame, kept to restore a failed commit. */
	struct FInstanceBackup
	{
		FVertexInstanceID Id;
		FVector3f Normal;
		FVector3f Tangent;
	};

	struct FMorphBackup
	{
		FName Name;
		TArray<TPair<FVertexID, FVector3f>> Positions;
		TArray<TPair<FVertexInstanceID, FVector3f>> Normals;
	};

	/** What validation learned about one LOD, and what its write changed. */
	struct FLodPlan
	{
		int32 Index = INDEX_NONE;
		bool bSelected = true;
		bool bSource = false;
		int32 BaseLod = INDEX_NONE;
		bool bRecomputeNormals = false;
		bool bRecomputeTangents = false;
		FMeshDescription* Description = nullptr;
		int32 SourceVertexCount = 0;
		int32 FullyWeighted = 0;
		int32 PartiallyWeighted = 0;
		bool bDescriptionHasBones = false;
		TArray<TPair<FVertexID, float>> Moves;

		// Restore data, filled as the write proceeds.
		bool bWritten = false;
		TArray<TPair<FVertexID, FVector3f>> OldPositions;
		TArray<FInstanceBackup> OldInstances;
		TArray<FMorphBackup> OldMorphs;
		FTransform OldBonePose = FTransform::Identity;
	};

	/** Render vertices of one LOD, flattened across sections. */
	struct FRenderSnapshot
	{
		bool bValid = false;
		TArray<FVector3f> Positions;
		TArray<FVector3f> Normals;
		TArray<float> Weights;
	};

	FRenderSnapshot SnapshotRender(USkeletalMesh* Mesh, int32 LodIndex, const TArray<bool>& Moved)
	{
		FRenderSnapshot Out;
		const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
		if (!Model || !Model->LODModels.IsValidIndex(LodIndex)) return Out;
		const FSkeletalMeshLODModel& Lod = Model->LODModels[LodIndex];
		for (const FSkelMeshSection& Section : Lod.Sections)
		{
			for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
			{
				double Total = 0.0;
				double OnPart = 0.0;
				for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
				{
					const double Weight = Vertex.InfluenceWeights[Influence];
					if (Weight <= 0.0) continue;
					Total += Weight;
					const int32 Local = Vertex.InfluenceBones[Influence];
					if (Section.BoneMap.IsValidIndex(Local))
					{
						const int32 Bone = Section.BoneMap[Local];
						if (Moved.IsValidIndex(Bone) && Moved[Bone]) OnPart += Weight;
					}
				}
				Out.Positions.Add(Vertex.Position);
				Out.Normals.Add(FVector3f(Vertex.TangentZ.X, Vertex.TangentZ.Y, Vertex.TangentZ.Z));
				Out.Weights.Add(Total > 0.0 ? static_cast<float>(OnPart / Total) : 0.0f);
			}
		}
		Out.bValid = true;
		return Out;
	}

	int32 CountOnPart(const FRenderSnapshot& Snapshot)
	{
		int32 Count = 0;
		for (const float Weight : Snapshot.Weights) Count += Weight >= MinWeight ? 1 : 0;
		return Count;
	}

	/** Compare a LOD's render vertices after the write with where the move should have put them. */
	void ReportRender(
		const FRenderSnapshot& Before,
		const FRenderSnapshot& After,
		const FMove& Move,
		bool bExpectMove,
		const TSharedPtr<FJsonObject>& Out)
	{
		if (!Before.bValid || !After.bValid)
		{
			Out->SetStringField(TEXT("renderComparison"), TEXT("skipped: the LOD has no render model"));
			return;
		}
		Out->SetNumberField(TEXT("renderVertexCountBefore"), Before.Positions.Num());
		Out->SetNumberField(TEXT("renderVertexCount"), After.Positions.Num());
		Out->SetNumberField(TEXT("renderVerticesOnPart"), CountOnPart(After));
		if (Before.Positions.Num() != After.Positions.Num())
		{
			Out->SetStringField(
				TEXT("renderComparison"),
				TEXT("skipped: the rebuild changed the render vertex count, so vertices cannot be paired by index"));
			return;
		}
		int32 Moved = 0;
		double MaxPositionError = 0.0;
		double MaxNormalChange = 0.0;
		for (int32 Index = 0; Index < Before.Positions.Num(); ++Index)
		{
			const FVector Old(Before.Positions[Index]);
			const FVector New(After.Positions[Index]);
			if (FVector::Dist(Old, New) > MovedTolerance) ++Moved;
			const float Weight = bExpectMove ? Before.Weights[Index] : 0.0f;
			MaxPositionError = FMath::Max(MaxPositionError, FVector::Dist(New, Move.Apply(Old, Weight)));
			const FVector Expected = Move.RotationAt(Weight).RotateVector(FVector(Before.Normals[Index])).GetSafeNormal();
			const FVector Drawn = FVector(After.Normals[Index]).GetSafeNormal();
			if (!Expected.IsNearlyZero() && !Drawn.IsNearlyZero())
			{
				const double Cos = FMath::Clamp(FVector::DotProduct(Expected, Drawn), -1.0, 1.0);
				MaxNormalChange = FMath::Max(MaxNormalChange, FMath::RadiansToDegrees(FMath::Acos(Cos)));
			}
		}
		Out->SetStringField(TEXT("renderComparison"), TEXT("paired by index"));
		Out->SetNumberField(TEXT("renderVerticesMoved"), Moved);
		Out->SetNumberField(TEXT("maxRenderPositionErrorCm"), MaxPositionError);
		Out->SetNumberField(TEXT("maxRenderNormalChangeDegrees"), MaxNormalChange);
	}

	TArray<FTransform> ComponentPose(const FReferenceSkeleton& Ref)
	{
		const TArray<FTransform>& Local = Ref.GetRawRefBonePose();
		const TArray<FMeshBoneInfo>& Info = Ref.GetRawRefBoneInfo();
		TArray<FTransform> Out;
		Out.SetNum(Local.Num());
		for (int32 Bone = 0; Bone < Local.Num(); ++Bone)
		{
			const int32 Parent = Info[Bone].ParentIndex;
			Out[Bone] = Parent == INDEX_NONE ? Local[Bone] : Local[Bone] * Out[Parent];
		}
		return Out;
	}

	/** Put one LOD back as it was before this call wrote it. */
	void RestoreLod(USkeletalMesh* Mesh, FLodPlan& Lod, int32 BoneIndex)
	{
		if (!Lod.bWritten || !Lod.Description) return;
		FSkeletalMeshAttributes Attributes(*Lod.Description);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		for (const TPair<FVertexID, FVector3f>& Old : Lod.OldPositions) Positions[Old.Key] = Old.Value;
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		for (const FInstanceBackup& Old : Lod.OldInstances)
		{
			if (Normals.IsValid()) Normals[Old.Id] = Old.Normal;
			if (Tangents.IsValid()) Tangents[Old.Id] = Old.Tangent;
		}
		for (const FMorphBackup& Morph : Lod.OldMorphs)
		{
			TVertexAttributesRef<FVector3f> Deltas = Attributes.GetVertexMorphPositionDelta(Morph.Name);
			if (Deltas.IsValid())
			{
				for (const TPair<FVertexID, FVector3f>& Old : Morph.Positions) Deltas[Old.Key] = Old.Value;
			}
			TVertexInstanceAttributesRef<FVector3f> NormalDeltas = Attributes.GetVertexInstanceMorphNormalDelta(Morph.Name);
			if (NormalDeltas.IsValid())
			{
				for (const TPair<FVertexInstanceID, FVector3f>& Old : Morph.Normals) NormalDeltas[Old.Key] = Old.Value;
			}
		}
		if (Lod.bDescriptionHasBones)
		{
			Attributes.GetBonePoses().Set(FBoneID(BoneIndex), Lod.OldBonePose);
		}
		USkeletalMesh::FCommitMeshDescriptionParams Params;
		Params.bForceUpdate = true;
		Mesh->CommitMeshDescription(Lod.Index, Params);
		Lod.bWritten = false;
	}
}
#endif // WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::TransformSkinnedPart(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("boneName"), TEXT("translation"), TEXT("rotation"), TEXT("space"),
		TEXT("moveBone"), TEXT("lods"), TEXT("dryRun"), TEXT("save") });
#if !(WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API)
	return MCPError(TEXT("transform_skinned_part requires an Unreal Editor build on Unreal Engine 5.4 or newer"));
#else
	using namespace MCPSkinnedPart;

	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString BoneNameString;
	if (auto Err = RequireString(Params, TEXT("boneName"), BoneNameString)) return Err;

	const bool bHasTranslation = HasParam(Params, TEXT("translation"));
	const bool bHasRotation = HasParam(Params, TEXT("rotation"));
	if (!bHasTranslation && !bHasRotation)
	{
		return MCPError(TEXT("Pass translation {x,y,z}, rotation {pitch,yaw,roll}, or both"));
	}
	FVector Translation = FVector::ZeroVector;
	if (bHasTranslation)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!TryGetObjectParam(Params, TEXT("translation"), Object) || !Object || !ReadVec3FieldsStrict(*Object, Translation)
			|| !FMath::IsFinite(Translation.X) || !FMath::IsFinite(Translation.Y) || !FMath::IsFinite(Translation.Z))
		{
			return MCPError(TEXT("translation must be {x, y, z} with three finite numbers"));
		}
	}
	FRotator Rotation = FRotator::ZeroRotator;
	if (bHasRotation)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!TryGetObjectParam(Params, TEXT("rotation"), Object) || !Object || !ReadRotatorFieldsStrict(*Object, Rotation)
			|| !FMath::IsFinite(Rotation.Pitch) || !FMath::IsFinite(Rotation.Yaw) || !FMath::IsFinite(Rotation.Roll))
		{
			return MCPError(TEXT("rotation must be {pitch, yaw, roll} with three finite numbers (degrees)"));
		}
	}
	const FString Space = OptionalString(Params, TEXT("space"), TEXT("bone")).ToLower();
	if (Space != TEXT("bone") && Space != TEXT("component"))
	{
		return MCPError(FString::Printf(TEXT("space must be 'bone' or 'component', not '%s'"), *Space));
	}
	const bool bMoveBone = OptionalBool(Params, TEXT("moveBone"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(AssetPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 RawBoneCount = Ref.GetRawBoneNum();
	const FName BoneName(*BoneNameString);
	const int32 BoneIndex = Ref.FindRawBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is not a bone of '%s' (virtual bones carry no vertices and cannot be moved this way)"),
			*BoneNameString, *Mesh->GetPathName()));
	}

	// The bone and everything below it move together: a child keeps its local pose,
	// so its bind pose travels with the parent and its vertices must too.
	const TArray<FMeshBoneInfo>& BoneInfo = Ref.GetRawRefBoneInfo();
	TArray<bool> MovedBones;
	MovedBones.Init(false, RawBoneCount);
	TArray<TSharedPtr<FJsonValue>> MovedBoneNames;
	for (int32 Bone = 0; Bone < RawBoneCount; ++Bone)
	{
		const int32 Parent = BoneInfo[Bone].ParentIndex;
		MovedBones[Bone] = Bone == BoneIndex || (Parent != INDEX_NONE && MovedBones[Parent]);
		if (MovedBones[Bone]) MovedBoneNames.Add(MakeShared<FJsonValueString>(BoneInfo[Bone].Name.ToString()));
	}

	const TArray<FTransform> PoseBefore = ComponentPose(Ref);
	const FTransform BoneBefore = PoseBefore[BoneIndex];
	const FQuat BoneAxes = BoneBefore.GetRotation().GetNormalized();
	FMove Move;
	Move.Pivot = BoneBefore.GetLocation();
	const FQuat Requested = Rotation.Quaternion().GetNormalized();
	if (Space == TEXT("bone"))
	{
		Move.Rotation = (BoneAxes * Requested * BoneAxes.Inverse()).GetNormalized();
		Move.Translation = BoneAxes.RotateVector(Translation);
	}
	else
	{
		Move.Rotation = Requested;
		Move.Translation = Translation;
	}
	FTransform BoneAfter = BoneBefore;
	BoneAfter.SetRotation((Move.Rotation * BoneAxes).GetNormalized());
	BoneAfter.SetLocation(Move.Pivot + Move.Translation);
	const int32 ParentIndex = BoneInfo[BoneIndex].ParentIndex;
	const FTransform NewLocalPose = ParentIndex == INDEX_NONE
		? BoneAfter
		: BoneAfter.GetRelativeTransform(PoseBefore[ParentIndex]);

	// ── Validate every LOD before anything is written ────────────────────────
	const int32 LodCount = Mesh->GetLODNum();
	if (LodCount <= 0) return MCPError(FString::Printf(TEXT("'%s' has no LODs"), *Mesh->GetPathName()));
	TArray<FLodPlan> Lods;
	Lods.SetNum(LodCount);
	if (HasParam(Params, TEXT("lods")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!TryGetArrayParam(Params, TEXT("lods"), Values) || !Values || Values->IsEmpty())
		{
			return MCPError(TEXT("lods must be a non-empty array of LOD indices; omit it for every LOD"));
		}
		for (FLodPlan& Lod : Lods) Lod.bSelected = false;
		for (int32 Input = 0; Input < Values->Num(); ++Input)
		{
			double Number = 0.0;
			if (!(*Values)[Input].IsValid() || !(*Values)[Input]->TryGetNumber(Number)
				|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
				|| Number < 0.0 || Number >= LodCount)
			{
				return MCPError(FString::Printf(TEXT("lods[%d] must be a LOD index from 0 to %d"), Input, LodCount - 1));
			}
			Lods[static_cast<int32>(Number)].bSelected = true;
		}
	}

	for (int32 Index = 0; Index < LodCount; ++Index)
	{
		FLodPlan& Lod = Lods[Index];
		Lod.Index = Index;
		const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(Index);
		if (Info)
		{
			Lod.bRecomputeNormals = Info->BuildSettings.bRecomputeNormals;
			Lod.bRecomputeTangents = Info->BuildSettings.bRecomputeTangents;
			Lod.BaseLod = Info->ReductionSettings.BaseLOD;
		}
		Lod.bSource = Mesh->HasMeshDescription(Index);
		if (!Lod.bSource)
		{
			if (Index > 0 && Mesh->IsReductionActive(Index)) continue;
			return MCPError(FString::Printf(
				TEXT("LOD %d of '%s' has no source geometry and is not generated from another LOD, so its part cannot be moved. Nothing was changed."),
				Index, *Mesh->GetPathName()));
		}
		Lod.Description = Mesh->GetMeshDescription(Index);
		if (!Lod.Description)
		{
			return MCPError(FString::Printf(
				TEXT("Failed to load the source MeshDescription of LOD %d of '%s'. Nothing was changed."), Index, *Mesh->GetPathName()));
		}
		FSkeletalMeshConstAttributes Attributes(*Lod.Description);
		Lod.bDescriptionHasBones = Attributes.HasBones() && Attributes.GetNumBones() > 0;
		if (Lod.bDescriptionHasBones)
		{
			const FSkeletalMeshAttributes::FBoneNameAttributesConstRef Names = Attributes.GetBoneNames();
			bool bSame = Attributes.GetNumBones() == RawBoneCount;
			for (int32 Bone = 0; bSame && Bone < RawBoneCount; ++Bone)
			{
				bSame = Names.Get(FBoneID(Bone)) == BoneInfo[Bone].Name;
			}
			if (!bSame)
			{
				return MCPError(FString::Printf(
					TEXT("LOD %d of '%s' names different bones (%d) from the mesh's reference skeleton (%d), so its weights cannot be trusted to point at '%s'. Nothing was changed."),
					Index, *Mesh->GetPathName(), Attributes.GetNumBones(), RawBoneCount, *BoneNameString));
			}
		}
		const FSkinWeightsVertexAttributesConstRef Weights = Attributes.GetVertexSkinWeights();
		if (!Weights.IsValid())
		{
			return MCPError(FString::Printf(
				TEXT("LOD %d of '%s' has no skin weights. Nothing was changed."), Index, *Mesh->GetPathName()));
		}
		for (const FVertexID Vertex : Lod.Description->Vertices().GetElementIDs())
		{
			++Lod.SourceVertexCount;
			const FVertexBoneWeightsConst Influences = Weights.Get(Vertex);
			float Total = 0.0f;
			float OnPart = 0.0f;
			for (int32 Influence = 0; Influence < Influences.Num(); ++Influence)
			{
				const int32 Bone = static_cast<int32>(Influences[Influence].GetBoneIndex());
				const float Weight = Influences[Influence].GetWeight();
				if (Weight <= 0.0f) continue;
				if (Bone < 0 || Bone >= RawBoneCount)
				{
					return MCPError(FString::Printf(
						TEXT("LOD %d of '%s': vertex %d is weighted to bone index %d, outside the %d-bone reference skeleton. Nothing was changed."),
						Index, *Mesh->GetPathName(), Vertex.GetValue(), Bone, RawBoneCount));
				}
				Total += Weight;
				if (MovedBones[Bone]) OnPart += Weight;
			}
			const float Share = Total > 0.0f ? OnPart / Total : 0.0f;
			if (Share < MinWeight) continue;
			if (Share >= 1.0f - MinWeight) ++Lod.FullyWeighted;
			else ++Lod.PartiallyWeighted;
			if (Lod.bSelected) Lod.Moves.Add(TPair<FVertexID, float>(Vertex, FMath::Min(Share, 1.0f)));
		}
	}
	if (Lods[0].bSelected && Lods[0].bSource && Lods[0].FullyWeighted + Lods[0].PartiallyWeighted == 0)
	{
		return MCPError(FString::Printf(
			TEXT("No LOD 0 vertex of '%s' is skinned to '%s' or a bone below it, so there is no part to move. Nothing was changed."),
			*Mesh->GetPathName(), *BoneNameString));
	}
	int32 TotalMoves = 0;
	for (const FLodPlan& Lod : Lods) TotalMoves += Lod.Moves.Num();
	if (TotalMoves == 0 && !bMoveBone)
	{
		return MCPError(TEXT("No selected LOD has a vertex on the bone and moveBone is false, so nothing would change"));
	}

	TArray<FRenderSnapshot> RenderBefore;
	for (int32 Index = 0; Index < LodCount; ++Index) RenderBefore.Add(SnapshotRender(Mesh, Index, MovedBones));

	auto LodReport = [&](const FLodPlan& Lod) -> TSharedPtr<FJsonObject>
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("lodIndex"), Lod.Index);
		Out->SetStringField(TEXT("geometry"), Lod.bSource ? TEXT("source") : TEXT("generated"));
		Out->SetBoolField(TEXT("selected"), Lod.bSelected);
		Out->SetBoolField(TEXT("recomputeNormals"), Lod.bRecomputeNormals);
		Out->SetBoolField(TEXT("recomputeTangents"), Lod.bRecomputeTangents);
		if (Lod.bSource)
		{
			Out->SetNumberField(TEXT("sourceVertexCount"), Lod.SourceVertexCount);
			Out->SetNumberField(TEXT("verticesMoved"), Lod.bSelected ? Lod.Moves.Num() : 0);
			Out->SetNumberField(TEXT("fullyWeighted"), Lod.FullyWeighted);
			Out->SetNumberField(TEXT("partiallyWeighted"), Lod.PartiallyWeighted);
			if (Lod.FullyWeighted + Lod.PartiallyWeighted == 0)
			{
				Out->SetStringField(TEXT("note"), TEXT("No vertex at this LOD is skinned to the moved bones, so the part stays put here."));
			}
		}
		else
		{
			Out->SetNumberField(TEXT("baseLod"), Lod.BaseLod);
			Out->SetStringField(TEXT("note"), TEXT("Reduced from its base LOD by the build, so it follows that LOD's moved vertices."));
		}
		return Out;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetStringField(TEXT("boneName"), BoneNameString);
	Result->SetStringField(TEXT("space"), Space);
	Result->SetBoolField(TEXT("moveBone"), bMoveBone);
	Result->SetArrayField(TEXT("movedBones"), MovedBoneNames);
	Result->SetObjectField(TEXT("pivot"), MCPVec3ToJsonObject(Move.Pivot));
	Result->SetObjectField(TEXT("componentTranslation"), MCPVec3ToJsonObject(Move.Translation));
	Result->SetObjectField(TEXT("componentRotation"), MCPRotatorToJsonObject(Move.Rotation.Rotator()));
	Result->SetObjectField(TEXT("bonePoseBefore"), MCPTransformToJsonObject(BoneBefore));
	Result->SetObjectField(TEXT("bonePoseAfter"), MCPTransformToJsonObject(bMoveBone ? BoneAfter : BoneBefore));

	if (bDryRun)
	{
		TArray<TSharedPtr<FJsonValue>> Reports;
		for (int32 Index = 0; Index < LodCount; ++Index)
		{
			TSharedPtr<FJsonObject> Report = LodReport(Lods[Index]);
			if (RenderBefore[Index].bValid)
			{
				Report->SetNumberField(TEXT("renderVertexCount"), RenderBefore[Index].Positions.Num());
				Report->SetNumberField(TEXT("renderVerticesOnPart"), CountOnPart(RenderBefore[Index]));
			}
			Reports.Add(MakeShared<FJsonValueObject>(Report));
		}
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetArrayField(TEXT("lods"), Reports);
		MCPSetNoRollback(Result, TEXT("dryRun changed nothing."));
		return MCPResult(Result);
	}

	if (auto Blocked = MCPAssetWriteBlockedError(Mesh, Mesh->GetPathName(), TEXT("transform a skinned part")))
	{
		return Blocked;
	}

	// ── Write ────────────────────────────────────────────────────────────────
	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "TransformSkinnedPart", "Transform Skinned Part"));
	FlushRenderingCommands();
	Mesh->SetFlags(RF_Transactional);
	Mesh->Modify();

	const FReferenceSkeleton OldRef = Mesh->GetRefSkeleton();
	if (bMoveBone)
	{
		FReferenceSkeleton NewRef = OldRef;
		{
			FReferenceSkeletonModifier Modifier(NewRef, Mesh->GetSkeleton());
			Modifier.UpdateRefPoseTransform(BoneIndex, NewLocalPose);
		}
		Mesh->SetRefSkeleton(NewRef);
		Mesh->GetRefBasesInvMatrix().Reset();
		Mesh->CalculateInvRefMatrices();
	}

	USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
#if UE_MCP_HAS_5_5_API
	CommitParams.bUpdateMorphTargets = false;
	CommitParams.bUpdateSkinWeightProfiles = false;
	CommitParams.bUpdateVertexAttributes = false;
	CommitParams.bUpdateVertexColors = false;
#endif
	CommitParams.bForceUpdate = true;

	const bool bRotate = Move.HasRotation();
	int32 FailedLod = INDEX_NONE;
	for (FLodPlan& Lod : Lods)
	{
		const bool bTouchBone = bMoveBone && Lod.bDescriptionHasBones;
		if (!Lod.bSource || (Lod.Moves.IsEmpty() && !bTouchBone)) continue;

		Mesh->ModifyMeshDescription(Lod.Index);
		FSkeletalMeshAttributes Attributes(*Lod.Description);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TArray<FName> MorphNames;
		if (bRotate)
		{
			MorphNames = Attributes.GetMorphTargetNames();
			for (const FName& Name : MorphNames) Lod.OldMorphs.AddDefaulted_GetRef().Name = Name;
		}

		for (const TPair<FVertexID, float>& Entry : Lod.Moves)
		{
			const FVertexID Vertex = Entry.Key;
			const float Weight = Entry.Value;
			const FVector3f Old = Positions[Vertex];
			Lod.OldPositions.Add(TPair<FVertexID, FVector3f>(Vertex, Old));
			Positions[Vertex] = FVector3f(Move.Apply(FVector(Old), Weight));
			if (!bRotate) continue;

			// Turn the frame of a rotated vertex, so imported normals and morph deltas go with the part.
			const FQuat Turn = Move.RotationAt(Weight);
			for (const FVertexInstanceID Instance : Lod.Description->GetVertexVertexInstanceIDs(Vertex))
			{
				FInstanceBackup& Backup = Lod.OldInstances.AddDefaulted_GetRef();
				Backup.Id = Instance;
				if (Normals.IsValid())
				{
					Backup.Normal = Normals[Instance];
					Normals[Instance] = FVector3f(Turn.RotateVector(FVector(Backup.Normal)));
				}
				if (Tangents.IsValid())
				{
					Backup.Tangent = Tangents[Instance];
					Tangents[Instance] = FVector3f(Turn.RotateVector(FVector(Backup.Tangent)));
				}
			}
			for (int32 Morph = 0; Morph < MorphNames.Num(); ++Morph)
			{
				TVertexAttributesRef<FVector3f> Deltas = Attributes.GetVertexMorphPositionDelta(MorphNames[Morph]);
				if (Deltas.IsValid())
				{
					const FVector3f Delta = Deltas[Vertex];
					Lod.OldMorphs[Morph].Positions.Add(TPair<FVertexID, FVector3f>(Vertex, Delta));
					Deltas[Vertex] = FVector3f(Turn.RotateVector(FVector(Delta)));
				}
				TVertexInstanceAttributesRef<FVector3f> NormalDeltas = Attributes.GetVertexInstanceMorphNormalDelta(MorphNames[Morph]);
				if (NormalDeltas.IsValid())
				{
					for (const FVertexInstanceID Instance : Lod.Description->GetVertexVertexInstanceIDs(Vertex))
					{
						const FVector3f Delta = NormalDeltas[Instance];
						Lod.OldMorphs[Morph].Normals.Add(TPair<FVertexInstanceID, FVector3f>(Instance, Delta));
						NormalDeltas[Instance] = FVector3f(Turn.RotateVector(FVector(Delta)));
					}
				}
			}
		}
		if (bTouchBone)
		{
			FSkeletalMeshAttributes::FBonePoseAttributesRef Poses = Attributes.GetBonePoses();
			Lod.OldBonePose = Poses.Get(FBoneID(BoneIndex));
			Poses.Set(FBoneID(BoneIndex), NewLocalPose);
		}
		Lod.bWritten = true;
		if (!Mesh->CommitMeshDescription(Lod.Index, CommitParams))
		{
			FailedLod = Lod.Index;
			break;
		}
	}

	if (FailedLod != INDEX_NONE)
	{
		for (FLodPlan& Lod : Lods) RestoreLod(Mesh, Lod, BoneIndex);
		Mesh->SetRefSkeleton(OldRef);
		Mesh->GetRefBasesInvMatrix().Reset();
		Mesh->CalculateInvRefMatrices();
		Mesh->Build();
		Mesh->PostEditChange();
		return MCPError(FString::Printf(
			TEXT("Failed to commit LOD %d of '%s'; the bone and every LOD written before it were restored in memory."),
			FailedLod, *Mesh->GetPathName()));
	}

	Mesh->Build();
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	TArray<TSharedPtr<FJsonValue>> Reports;
	for (int32 Index = 0; Index < LodCount; ++Index)
	{
		TSharedPtr<FJsonObject> Report = LodReport(Lods[Index]);
		// A source LOD left out of lods keeps its vertices; a generated one follows its base LOD.
		const bool bExpectMove = Lods[Index].bSource ? Lods[Index].bSelected : true;
		ReportRender(RenderBefore[Index], SnapshotRender(Mesh, Index, MovedBones), Move, bExpectMove, Report);
		Reports.Add(MakeShared<FJsonValueObject>(Report));
	}
	Result->SetBoolField(TEXT("dryRun"), false);
	Result->SetBoolField(TEXT("updated"), true);
	Result->SetArrayField(TEXT("lods"), Reports);
	Result->SetStringField(
		TEXT("normals"),
		TEXT("Vertices moved in place in each LOD's source geometry; triangles and hard edges are unchanged, so a LOD that recomputes normals keeps its hard edges and one that keeps imported normals keeps them, turned with the part on a rotation. maxRenderNormalChangeDegrees compares each drawn normal with where the move should have put it."));

	// The inverse is exact only when every moved vertex moved rigidly.
	bool bExactInverse = !bRotate;
	if (!bExactInverse)
	{
		bExactInverse = true;
		for (const FLodPlan& Lod : Lods) bExactInverse &= Lod.PartiallyWeighted == 0 || !Lod.bSelected;
	}
	if (bExactInverse)
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		Payload->SetStringField(TEXT("boneName"), BoneNameString);
		Payload->SetStringField(TEXT("space"), TEXT("component"));
		Payload->SetObjectField(TEXT("translation"), MCPVec3ToJsonObject(-Move.Translation));
		if (bRotate) Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Move.Rotation.Inverse().Rotator()));
		Payload->SetBoolField(TEXT("moveBone"), bMoveBone);
		if (HasParam(Params, TEXT("lods")))
		{
			TArray<TSharedPtr<FJsonValue>> Selected;
			for (const FLodPlan& Lod : Lods) if (Lod.bSelected) Selected.Add(MakeShared<FJsonValueNumber>(Lod.Index));
			Payload->SetArrayField(TEXT("lods"), Selected);
		}
		Payload->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("transform_skinned_part"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("A rotation of partially weighted vertices blends linearly and has no exact inverse; use editor undo, which this edit is recorded in."));
	}

	if (bSave)
	{
		FString SaveError;
		if (!SaveAssetPackageChecked(Mesh, SaveError))
		{
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetStringField(TEXT("saveError"), SaveError);
			Result->SetStringField(TEXT("warning"), TEXT("The part moved in memory but the package did not save; it stays dirty."));
			return MCPResult(Result);
		}
	}
	Result->SetBoolField(TEXT("saved"), bSave);
	return MCPResult(Result);
#endif
}
