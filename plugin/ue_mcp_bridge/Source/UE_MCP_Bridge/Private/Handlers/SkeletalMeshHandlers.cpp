#include "SkeletalMeshHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/SkeletalMesh.h"
#include "SkeletalMeshEditorSubsystem.h"

#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API
#include "BoneWeights.h"
#include "GPUSkinVertexFactory.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#endif

namespace
{
	constexpr int32 MCPSkinWeightMaxVerticesPerCall = 256;
	constexpr int32 MCPSkinWeightMaxInputInfluences = 64;

	struct FTargetLod
	{
		int32 Index = INDEX_NONE;
		FSkeletalMeshBuildSettings Before;
	};

	TSharedPtr<FJsonValue> ResolveTargetLods(
		const TSharedPtr<FJsonObject>& Params,
		USkeletalMesh* Mesh,
		TArray<FTargetLod>& OutLods)
	{
		if (!Mesh) return MCPError(TEXT("SkeletalMesh is null"));
		const int32 LodCount = Mesh->GetLODNum();
		if (LodCount <= 0) return MCPError(TEXT("SkeletalMesh has no LODs"));

		const bool bAllLods = OptionalBool(Params, TEXT("allLods"), false);
		if (bAllLods && Params->HasField(TEXT("lodIndex")))
		{
			return MCPError(TEXT("Specify either allLods=true or lodIndex, not both"));
		}

		TArray<int32> Indices;
		if (bAllLods)
		{
			for (int32 Index = 0; Index < LodCount; ++Index) Indices.Add(Index);
		}
		else
		{
			int32 LodIndex = 0;
			if (Params->HasField(TEXT("lodIndex")))
			{
				double LodNumber = 0.0;
				if (!Params->TryGetNumberField(TEXT("lodIndex"), LodNumber)
					|| !FMath::IsFinite(LodNumber)
					|| !FMath::IsNearlyEqual(LodNumber, FMath::RoundToDouble(LodNumber)))
				{
					return MCPError(TEXT("lodIndex must be a finite integer"));
				}
				if (LodNumber < static_cast<double>(MIN_int32) || LodNumber > static_cast<double>(MAX_int32))
				{
					return MCPError(TEXT("lodIndex is outside the supported integer range"));
				}
				LodIndex = static_cast<int32>(LodNumber);
			}
			Indices.Add(LodIndex);
		}

		for (const int32 Index : Indices)
		{
			if (Index < 0 || Index >= LodCount)
			{
				return MCPError(FString::Printf(TEXT("Invalid lodIndex %d; mesh has %d LODs"), Index, LodCount));
			}
			FTargetLod& Target = OutLods.AddDefaulted_GetRef();
			Target.Index = Index;
			USkeletalMeshEditorSubsystem::GetLodBuildSettings(Mesh, Index, Target.Before);
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> SerializeBuildSettings(const FSkeletalMeshBuildSettings& Settings)
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("bRecomputeNormals"), Settings.bRecomputeNormals);
		Out->SetBoolField(TEXT("bRecomputeTangents"), Settings.bRecomputeTangents);
		Out->SetBoolField(TEXT("bUseMikkTSpace"), Settings.bUseMikkTSpace);
		Out->SetBoolField(TEXT("bComputeWeightedNormals"), Settings.bComputeWeightedNormals);
		Out->SetBoolField(TEXT("bRemoveDegenerates"), Settings.bRemoveDegenerates);
		Out->SetBoolField(TEXT("bUseHighPrecisionTangentBasis"), Settings.bUseHighPrecisionTangentBasis);
		Out->SetBoolField(TEXT("bUseHighPrecisionSkinWeights"), Settings.bUseHighPrecisionSkinWeights);
		Out->SetBoolField(TEXT("bUseFullPrecisionUVs"), Settings.bUseFullPrecisionUVs);
		Out->SetBoolField(TEXT("bUseBackwardsCompatibleF16TruncUVs"), Settings.bUseBackwardsCompatibleF16TruncUVs);
#if UE_MCP_HAS_5_8_API
		Out->SetBoolField(TEXT("bOptimizeForInstancing"), Settings.bOptimizeForInstancing);
#endif
		Out->SetNumberField(TEXT("thresholdPosition"), Settings.ThresholdPosition);
		Out->SetNumberField(TEXT("thresholdTangentNormal"), Settings.ThresholdTangentNormal);
		Out->SetNumberField(TEXT("thresholdUV"), Settings.ThresholdUV);
		Out->SetNumberField(TEXT("morphThresholdPosition"), Settings.MorphThresholdPosition);
		Out->SetNumberField(TEXT("boneInfluenceLimit"), Settings.BoneInfluenceLimit);
		return Out;
	}

	TSharedPtr<FJsonObject> MakeLodResult(int32 Index, const FSkeletalMeshBuildSettings& Before, const FSkeletalMeshBuildSettings& After)
	{
		auto Lod = MakeShared<FJsonObject>();
		Lod->SetNumberField(TEXT("lodIndex"), Index);
		Lod->SetObjectField(TEXT("beforeBuildSettings"), SerializeBuildSettings(Before));
		Lod->SetObjectField(TEXT("afterBuildSettings"), SerializeBuildSettings(After));
#if UE_MCP_HAS_5_8_API
		Lod->SetBoolField(TEXT("beforeOptimizeForInstancing"), Before.bOptimizeForInstancing);
		Lod->SetBoolField(TEXT("afterOptimizeForInstancing"), After.bOptimizeForInstancing);
		Lod->SetBoolField(TEXT("changed"), Before.bOptimizeForInstancing != After.bOptimizeForInstancing);
#else
		// Every field below reports one 5.8-only build setting, so on an older
		// engine they are omitted and named instead. Reporting false would say
		// the flag is off, which is a different answer from the engine having no
		// such flag at all.
		Lod->SetStringField(TEXT("optimizeForInstancingNote"),
			TEXT("beforeOptimizeForInstancing, afterOptimizeForInstancing, changed and ")
			TEXT("buildSettings.bOptimizeForInstancing are omitted: ")
			TEXT("FSkeletalMeshBuildSettings::bOptimizeForInstancing needs UE 5.8, and this editor is older."));
#endif
		return Lod;
	}

#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API
	struct FMCPSkinWeightTarget
	{
		USkeletalMesh* Mesh = nullptr;
		FMeshDescription* Description = nullptr;
		int32 LodIndex = 0;
		int32 MaxInfluences = UE::AnimationCore::MaxInlineBoneWeightCount;
		FName ProfileName = NAME_None;
		FString DisplayProfileName = TEXT("default");
	};

	struct FMCPSkinWeightEdit
	{
		int32 VertexIndex = INDEX_NONE;
		int32 RequestedInfluenceCount = 0;
		UE::AnimationCore::FBoneWeights Before;
		UE::AnimationCore::FBoneWeights Desired;
		UE::AnimationCore::FBoneWeights After;
	};

	TSharedPtr<FJsonValue> MCPParseFiniteInteger(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		int32& OutValue,
		const FString& Context)
	{
		double Number = 0.0;
		if (!Object.IsValid() || !Object->TryGetNumberField(Field, Number)
			|| !FMath::IsFinite(Number)
			|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
			|| Number < static_cast<double>(MIN_int32)
			|| Number > static_cast<double>(MAX_int32))
		{
			return MCPError(FString::Printf(TEXT("%s.%s must be a finite integer"), *Context, Field));
		}
		OutValue = static_cast<int32>(Number);
		return nullptr;
	}

	TSharedPtr<FJsonValue> MCPResolveSkinWeightTarget(
		const TSharedPtr<FJsonObject>& Params,
		FMCPSkinWeightTarget& Out)
	{
		FString AssetPath;
		if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

		Out.Mesh = LoadAssetByPath<USkeletalMesh>(AssetPath);
		if (!Out.Mesh)
		{
			return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));
		}

		if (Params->HasField(TEXT("lodIndex")))
		{
			if (TSharedPtr<FJsonValue> Error = MCPParseFiniteInteger(Params, TEXT("lodIndex"), Out.LodIndex, TEXT("params")))
			{
				return Error;
			}
		}
		const int32 SourceLodCount = Out.Mesh->GetNumSourceModels();
		if (Out.LodIndex < 0 || Out.LodIndex >= SourceLodCount)
		{
			return MCPError(FString::Printf(
				TEXT("lodIndex %d is out of range: '%s' has %d source LOD(s)"),
				Out.LodIndex,
				*Out.Mesh->GetPathName(),
				SourceLodCount));
		}
		if (!Out.Mesh->HasMeshDescription(Out.LodIndex))
		{
			return MCPError(FString::Printf(
				TEXT("LOD %d of '%s' has no authorable source MeshDescription; generated LOD skin weights cannot be edited directly"),
				Out.LodIndex,
				*Out.Mesh->GetPathName()));
		}
		if (const FSkeletalMeshLODInfo* LodInfo = Out.Mesh->GetLODInfo(Out.LodIndex))
		{
			Out.MaxInfluences = FMath::Clamp(
				FGPUBaseSkinVertexFactory::GetBoneInfluenceLimitForAsset(
					LodInfo->BuildSettings.BoneInfluenceLimit),
				1,
				UE::AnimationCore::MaxInlineBoneWeightCount);
		}
		Out.Description = Out.Mesh->GetMeshDescription(Out.LodIndex);
		if (!Out.Description)
		{
			return MCPError(FString::Printf(
				TEXT("Failed to load the source MeshDescription for LOD %d of '%s'"),
				Out.LodIndex,
				*Out.Mesh->GetPathName()));
		}

		if (Params->HasField(TEXT("profileName")))
		{
			FString RequestedProfile;
			if (!Params->TryGetStringField(TEXT("profileName"), RequestedProfile))
			{
				return MCPError(TEXT("profileName must be a string"));
			}
			RequestedProfile.TrimStartAndEndInline();
			if (RequestedProfile.IsEmpty())
			{
				return MCPError(TEXT("profileName cannot be empty; omit it or pass 'default' for the default profile"));
			}
			if (!RequestedProfile.Equals(TEXT("default"), ESearchCase::IgnoreCase))
			{
				Out.ProfileName = FName(*RequestedProfile);
				Out.DisplayProfileName = RequestedProfile;
			}
		}

		FSkeletalMeshConstAttributes Attributes(*Out.Description);
		if (!Out.ProfileName.IsNone() && !Attributes.GetSkinWeightProfileNames().Contains(Out.ProfileName))
		{
			return MCPError(FString::Printf(
				TEXT("Skin weight profile '%s' does not exist on LOD %d of '%s'; this action edits existing profiles only"),
				*Out.DisplayProfileName,
				Out.LodIndex,
				*Out.Mesh->GetPathName()));
		}
		if (!Attributes.GetVertexSkinWeights(Out.ProfileName).IsValid())
		{
			return MCPError(FString::Printf(
				TEXT("Skin weight profile '%s' has no vertex-weight attribute on LOD %d of '%s'"),
				*Out.DisplayProfileName,
				Out.LodIndex,
				*Out.Mesh->GetPathName()));
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPSerializeSkinWeightVertex(
		const int32 VertexIndex,
		const UE::AnimationCore::FBoneWeights& Weights,
		const FReferenceSkeleton& ReferenceSkeleton)
	{
		auto Vertex = MakeShared<FJsonObject>();
		Vertex->SetNumberField(TEXT("vertexIndex"), VertexIndex);
		TArray<TSharedPtr<FJsonValue>> Influences;
		double Sum = 0.0;
		for (int32 InfluenceIndex = 0; InfluenceIndex < Weights.Num(); ++InfluenceIndex)
		{
			const UE::AnimationCore::FBoneWeight Weight = Weights[InfluenceIndex];
			const int32 BoneIndex = static_cast<int32>(Weight.GetBoneIndex());
			auto Influence = MakeShared<FJsonObject>();
			Influence->SetNumberField(TEXT("boneIndex"), BoneIndex);
			Influence->SetStringField(
				TEXT("boneName"),
				ReferenceSkeleton.IsValidIndex(BoneIndex)
					? ReferenceSkeleton.GetBoneName(BoneIndex).ToString()
					: FString());
			Influence->SetNumberField(TEXT("weight"), Weight.GetWeight());
			Influence->SetNumberField(TEXT("rawWeight"), Weight.GetRawWeight());
			Sum += Weight.GetWeight();
			Influences.Add(MakeShared<FJsonValueObject>(Influence));
		}
		Vertex->SetNumberField(TEXT("influenceCount"), Influences.Num());
		Vertex->SetNumberField(TEXT("weightSum"), Sum);
		Vertex->SetArrayField(TEXT("influences"), Influences);
		return Vertex;
	}

	TSharedPtr<FJsonValue> MCPParseSkinWeightVertexIndices(
		const TSharedPtr<FJsonObject>& Params,
		const FMeshDescription& Description,
		TArray<int32>& OutVertexIndices)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(TEXT("vertexIndices"), Values) || !Values || Values->IsEmpty())
		{
			return MCPError(TEXT("vertexIndices must contain at least one source MeshDescription vertex index"));
		}
		if (Values->Num() > MCPSkinWeightMaxVerticesPerCall)
		{
			return MCPError(FString::Printf(
				TEXT("vertexIndices contains %d entries; the maximum per read is %d"),
				Values->Num(),
				MCPSkinWeightMaxVerticesPerCall));
		}

		TSet<int32> Seen;
		for (int32 InputIndex = 0; InputIndex < Values->Num(); ++InputIndex)
		{
			double Number = 0.0;
			if (!(*Values)[InputIndex].IsValid() || !(*Values)[InputIndex]->TryGetNumber(Number)
				|| !FMath::IsFinite(Number)
				|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
				|| Number < 0.0
				|| Number > static_cast<double>(MAX_int32))
			{
				return MCPError(FString::Printf(TEXT("vertexIndices[%d] must be a non-negative finite integer"), InputIndex));
			}
			const int32 VertexIndex = static_cast<int32>(Number);
			if (!Description.IsVertexValid(FVertexID(VertexIndex)))
			{
				return MCPError(FString::Printf(TEXT("vertexIndices[%d]=%d is not a valid source vertex ID"), InputIndex, VertexIndex));
			}
			if (Seen.Contains(VertexIndex))
			{
				return MCPError(FString::Printf(TEXT("vertexIndices contains duplicate vertex ID %d"), VertexIndex));
			}
			Seen.Add(VertexIndex);
			OutVertexIndices.Add(VertexIndex);
		}
		return nullptr;
	}

	/**
	 * Copy one vertex's stored source weights without normalizing or clipping them.
	 * The ordinary FBoneWeights::Create defaults are unsafe for a snapshot: they
	 * normalize and cap at MaxInlineBoneWeightCount. This tool's read result and
	 * rollback payload must instead be able to express every positive source
	 * influence that it exposes.
	 */
	TSharedPtr<FJsonValue> MCPReadExactSkinWeights(
		const FVertexBoneWeightsConst& SourceWeights,
		const FReferenceSkeleton& ReferenceSkeleton,
		const FString& Context,
		UE::AnimationCore::FBoneWeights& OutWeights)
	{
		const int32 SourceCount = SourceWeights.Num();
		if (SourceCount <= 0 || SourceCount > MCPSkinWeightMaxInputInfluences)
		{
			return MCPError(FString::Printf(
				TEXT("%s must contain between 1 and %d source influences to support an exact rollback"),
				*Context,
				MCPSkinWeightMaxInputInfluences));
		}

		TSet<int32> SeenBones;
		int32 RawWeightSum = 0;
		for (int32 InfluenceIndex = 0; InfluenceIndex < SourceCount; ++InfluenceIndex)
		{
			const UE::AnimationCore::FBoneWeight Weight = SourceWeights[InfluenceIndex];
			const int32 BoneIndex = static_cast<int32>(Weight.GetBoneIndex());
			if (!ReferenceSkeleton.IsValidIndex(BoneIndex))
			{
				return MCPError(FString::Printf(
					TEXT("%s has source influence %d with invalid bone index %d"),
					*Context,
					InfluenceIndex,
					BoneIndex));
			}
			if (SeenBones.Contains(BoneIndex))
			{
				return MCPError(FString::Printf(
					TEXT("%s has duplicate source bone index %d and cannot be rolled back exactly"),
					*Context,
					BoneIndex));
			}
			SeenBones.Add(BoneIndex);

			const uint16 RawWeight = Weight.GetRawWeight();
			if (RawWeight == 0)
			{
				return MCPError(FString::Printf(
					TEXT("%s has a zero source rawWeight and cannot be represented by the rollback contract"),
					*Context));
			}
			RawWeightSum += RawWeight;
		}
		if (RawWeightSum != static_cast<int32>(UE::AnimationCore::FBoneWeight::GetMaxRawWeight()))
		{
			return MCPError(FString::Printf(
				TEXT("%s source rawWeight values sum to %d instead of %d and cannot be restored exactly"),
				*Context,
				RawWeightSum,
				UE::AnimationCore::FBoneWeight::GetMaxRawWeight()));
		}

		UE::AnimationCore::FBoneWeightsSettings ExactSettings;
		// The engine clamps this setting to a raw threshold of 1, preserving every
		// positive uint16 source weight including the smallest representable value.
		ExactSettings.SetWeightThreshold(0.0f);
		ExactSettings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::None);
		ExactSettings.SetMaxWeightCount(SourceCount);
		OutWeights = UE::AnimationCore::FBoneWeights::Create(SourceWeights, ExactSettings);
		if (OutWeights.Num() != SourceCount)
		{
			return MCPError(FString::Printf(
				TEXT("%s could not be copied into an exact source-weight snapshot"),
				*Context));
		}
		return nullptr;
	}

	TSharedPtr<FJsonValue> MCPParseSkinWeightEdits(
		const TSharedPtr<FJsonObject>& Params,
		const FMCPSkinWeightTarget& Target,
		TArray<FMCPSkinWeightEdit>& OutEdits)
	{
		bool bRestoreRawWeights = false;
		if (Params->HasField(TEXT("restoreRawWeights"))
			&& !Params->TryGetBoolField(TEXT("restoreRawWeights"), bRestoreRawWeights))
		{
			return MCPError(TEXT("restoreRawWeights must be a boolean"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(TEXT("edits"), Values) || !Values || Values->IsEmpty())
		{
			return MCPError(TEXT("edits must contain at least one {vertexIndex, influences} entry"));
		}
		if (Values->Num() > MCPSkinWeightMaxVerticesPerCall)
		{
			return MCPError(FString::Printf(
				TEXT("edits contains %d vertices; the maximum per call is %d"),
				Values->Num(),
				MCPSkinWeightMaxVerticesPerCall));
		}

		const FReferenceSkeleton& RefSkeleton = Target.Mesh->GetRefSkeleton();
		FSkeletalMeshConstAttributes Attributes(*Target.Description);
		const FSkinWeightsVertexAttributesConstRef ExistingWeights = Attributes.GetVertexSkinWeights(Target.ProfileName);
		TSet<int32> SeenVertices;

		for (int32 EditIndex = 0; EditIndex < Values->Num(); ++EditIndex)
		{
			const TSharedPtr<FJsonValue>& Value = (*Values)[EditIndex];
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				return MCPError(FString::Printf(TEXT("edits[%d] must be an object"), EditIndex));
			}
			const TSharedPtr<FJsonObject> EditObject = Value->AsObject();
			FMCPSkinWeightEdit Prepared;
			if (TSharedPtr<FJsonValue> Error = MCPParseFiniteInteger(
				EditObject,
				TEXT("vertexIndex"),
				Prepared.VertexIndex,
				FString::Printf(TEXT("edits[%d]"), EditIndex)))
			{
				return Error;
			}
			if (Prepared.VertexIndex < 0 || !Target.Description->IsVertexValid(FVertexID(Prepared.VertexIndex)))
			{
				return MCPError(FString::Printf(
					TEXT("edits[%d].vertexIndex=%d is not a valid source vertex ID"),
					EditIndex,
					Prepared.VertexIndex));
			}
			if (SeenVertices.Contains(Prepared.VertexIndex))
			{
				return MCPError(FString::Printf(TEXT("edits contains duplicate vertex ID %d"), Prepared.VertexIndex));
			}
			SeenVertices.Add(Prepared.VertexIndex);

			const TArray<TSharedPtr<FJsonValue>>* InfluenceValues = nullptr;
			if (!EditObject->TryGetArrayField(TEXT("influences"), InfluenceValues)
				|| !InfluenceValues
				|| InfluenceValues->IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("edits[%d].influences must contain at least one {boneName, weight} entry"), EditIndex));
			}
			if (InfluenceValues->Num() > MCPSkinWeightMaxInputInfluences)
			{
				return MCPError(FString::Printf(
					TEXT("edits[%d].influences contains %d entries; the input safety maximum is %d"),
					EditIndex,
					InfluenceValues->Num(),
					MCPSkinWeightMaxInputInfluences));
			}
			if (bRestoreRawWeights && InfluenceValues->Num() > MCPSkinWeightMaxInputInfluences)
			{
				return MCPError(FString::Printf(
					TEXT("edits[%d].influences contains %d entries; exact raw restores support at most %d source influences"),
					EditIndex,
					InfluenceValues->Num(),
					MCPSkinWeightMaxInputInfluences));
			}

			TArray<UE::AnimationCore::FBoneWeight> RequestedWeights;
			RequestedWeights.Reserve(InfluenceValues->Num());
			TSet<int32> SeenBones;
			double PositiveWeightSum = 0.0;
			int32 RawWeightSum = 0;
			for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceValues->Num(); ++InfluenceIndex)
			{
				const TSharedPtr<FJsonValue>& InfluenceValue = (*InfluenceValues)[InfluenceIndex];
				if (!InfluenceValue.IsValid() || InfluenceValue->Type != EJson::Object)
				{
					return MCPError(FString::Printf(TEXT("edits[%d].influences[%d] must be an object"), EditIndex, InfluenceIndex));
				}
				const TSharedPtr<FJsonObject> InfluenceObject = InfluenceValue->AsObject();
				FString BoneName;
				if (!InfluenceObject->TryGetStringField(TEXT("boneName"), BoneName))
				{
					return MCPError(FString::Printf(TEXT("edits[%d].influences[%d].boneName must be a string"), EditIndex, InfluenceIndex));
				}
				BoneName.TrimStartAndEndInline();
				if (BoneName.IsEmpty())
				{
					return MCPError(FString::Printf(TEXT("edits[%d].influences[%d].boneName cannot be empty"), EditIndex, InfluenceIndex));
				}
				const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
				if (BoneIndex == INDEX_NONE || BoneIndex > static_cast<int32>(MAX_uint16))
				{
					return MCPError(FString::Printf(
						TEXT("edits[%d].influences[%d] names unknown or unsupported bone '%s'"),
						EditIndex,
						InfluenceIndex,
						*BoneName));
				}
				if (SeenBones.Contains(BoneIndex))
				{
					return MCPError(FString::Printf(
						TEXT("edits[%d].influences contains duplicate bone '%s'"),
						EditIndex,
						*BoneName));
				}
				SeenBones.Add(BoneIndex);

				if (bRestoreRawWeights)
				{
					if (InfluenceObject->HasField(TEXT("weight")))
					{
						return MCPError(FString::Printf(
							TEXT("edits[%d].influences[%d] must use rawWeight only when restoreRawWeights=true"),
							EditIndex,
							InfluenceIndex));
					}
					int32 RawWeight = 0;
					if (TSharedPtr<FJsonValue> Error = MCPParseFiniteInteger(
						InfluenceObject,
						TEXT("rawWeight"),
						RawWeight,
						FString::Printf(TEXT("edits[%d].influences[%d]"), EditIndex, InfluenceIndex)))
					{
						return Error;
					}
					if (RawWeight <= 0 || RawWeight > static_cast<int32>(UE::AnimationCore::FBoneWeight::GetMaxRawWeight()))
					{
						return MCPError(FString::Printf(
							TEXT("edits[%d].influences[%d].rawWeight must be an integer between 1 and %d"),
							EditIndex,
							InfluenceIndex,
							UE::AnimationCore::FBoneWeight::GetMaxRawWeight()));
					}
					RawWeightSum += RawWeight;
					RequestedWeights.Emplace(static_cast<FBoneIndexType>(BoneIndex), static_cast<uint16>(RawWeight));
				}
				else
				{
					if (InfluenceObject->HasField(TEXT("rawWeight")))
					{
						return MCPError(FString::Printf(
							TEXT("edits[%d].influences[%d].rawWeight requires restoreRawWeights=true"),
							EditIndex,
							InfluenceIndex));
					}
					double Weight = 0.0;
					if (!InfluenceObject->TryGetNumberField(TEXT("weight"), Weight)
						|| !FMath::IsFinite(Weight)
						|| Weight < 0.0
						|| Weight > 1.0)
					{
						return MCPError(FString::Printf(
							TEXT("edits[%d].influences[%d].weight must be finite and between 0 and 1"),
							EditIndex,
							InfluenceIndex));
					}
					PositiveWeightSum += Weight;
					RequestedWeights.Emplace(static_cast<FBoneIndexType>(BoneIndex), static_cast<float>(Weight));
				}
			}
			if (!bRestoreRawWeights && PositiveWeightSum <= 0.0)
			{
				return MCPError(FString::Printf(TEXT("edits[%d].influences cannot be all zero"), EditIndex));
			}
			if (bRestoreRawWeights && RawWeightSum != static_cast<int32>(UE::AnimationCore::FBoneWeight::GetMaxRawWeight()))
			{
				return MCPError(FString::Printf(
					TEXT("edits[%d].influences rawWeight values must sum to %d for an exact restore"),
					EditIndex,
					UE::AnimationCore::FBoneWeight::GetMaxRawWeight()));
			}

			Prepared.RequestedInfluenceCount = RequestedWeights.Num();
			UE::AnimationCore::FBoneWeightsSettings WeightSettings;
			if (bRestoreRawWeights)
			{
				// Rollbacks preserve source weights even when the render LOD has a
				// lower BoneInfluenceLimit. Validated raw values need no normalization.
				WeightSettings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::None);
				WeightSettings.SetMaxWeightCount(RequestedWeights.Num());
			}
			else
			{
				WeightSettings.SetMaxWeightCount(Target.MaxInfluences);
			}
			Prepared.Desired = UE::AnimationCore::FBoneWeights::Create(RequestedWeights, WeightSettings);
			if (Prepared.Desired.Num() == 0)
			{
				return MCPError(FString::Printf(
					TEXT("edits[%d].influences quantize to all zero at the engine's 16-bit source-weight precision"),
					EditIndex));
			}
			const FVertexBoneWeightsConst SourceWeights = ExistingWeights.Get(FVertexID(Prepared.VertexIndex));
			if (TSharedPtr<FJsonValue> Error = MCPReadExactSkinWeights(
				SourceWeights,
				RefSkeleton,
				FString::Printf(TEXT("edits[%d].vertexIndex=%d"), EditIndex, Prepared.VertexIndex),
				Prepared.Before))
			{
				return Error;
			}
			OutEdits.Add(MoveTemp(Prepared));
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPSerializeSkinWeightEdit(
		const FMCPSkinWeightEdit& Edit,
		const FReferenceSkeleton& RefSkeleton)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("vertexIndex"), Edit.VertexIndex);
		Result->SetNumberField(TEXT("requestedInfluenceCount"), Edit.RequestedInfluenceCount);
		Result->SetNumberField(TEXT("storedInfluenceCount"), Edit.After.Num());
		Result->SetNumberField(TEXT("prunedInfluenceCount"), Edit.RequestedInfluenceCount - Edit.After.Num());
		Result->SetBoolField(TEXT("changed"), Edit.Before != Edit.After);
		Result->SetObjectField(TEXT("before"), MCPSerializeSkinWeightVertex(Edit.VertexIndex, Edit.Before, RefSkeleton));
		Result->SetObjectField(TEXT("after"), MCPSerializeSkinWeightVertex(Edit.VertexIndex, Edit.After, RefSkeleton));
		return Result;
	}

	TSharedPtr<FJsonObject> MCPMakeSkinWeightRollbackPayload(
		const FMCPSkinWeightTarget& Target,
		const TArray<FMCPSkinWeightEdit>& Edits)
	{
		const FReferenceSkeleton& RefSkeleton = Target.Mesh->GetRefSkeleton();
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.Mesh->GetPathName());
		Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		if (!Target.ProfileName.IsNone())
		{
			Payload->SetStringField(TEXT("profileName"), Target.DisplayProfileName);
		}
		TArray<TSharedPtr<FJsonValue>> RollbackEdits;
		for (const FMCPSkinWeightEdit& Edit : Edits)
		{
			if (Edit.Before == Edit.After) continue;
			auto RollbackEdit = MakeShared<FJsonObject>();
			RollbackEdit->SetNumberField(TEXT("vertexIndex"), Edit.VertexIndex);
			TArray<TSharedPtr<FJsonValue>> Influences;
			for (int32 InfluenceIndex = 0; InfluenceIndex < Edit.Before.Num(); ++InfluenceIndex)
			{
				const UE::AnimationCore::FBoneWeight Weight = Edit.Before[InfluenceIndex];
				const int32 BoneIndex = static_cast<int32>(Weight.GetBoneIndex());
				auto Influence = MakeShared<FJsonObject>();
				Influence->SetStringField(
					TEXT("boneName"),
					RefSkeleton.IsValidIndex(BoneIndex) ? RefSkeleton.GetBoneName(BoneIndex).ToString() : FString());
				Influence->SetNumberField(TEXT("rawWeight"), Weight.GetRawWeight());
				Influences.Add(MakeShared<FJsonValueObject>(Influence));
			}
			RollbackEdit->SetArrayField(TEXT("influences"), Influences);
			RollbackEdits.Add(MakeShared<FJsonValueObject>(RollbackEdit));
		}
		Payload->SetArrayField(TEXT("edits"), RollbackEdits);
		// This payload is made from source weights read before mutation. It restores
		// their exact uint16 values without applying a lower render LOD limit.
		Payload->SetBoolField(TEXT("restoreRawWeights"), true);
		return Payload;
	}
#endif // WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API
}
void FSkeletalMeshHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("set_skeletal_mesh_optimize_for_instancing"), &SetOptimizeForInstancing);
	Registry.RegisterHandler(TEXT("read_skeletal_mesh_build_settings"), &ReadBuildSettings);
	Registry.RegisterHandler(TEXT("read_skeletal_mesh_skin_weights"), &ReadSkinWeights);
	Registry.RegisterHandler(TEXT("set_skeletal_mesh_skin_weights"), &SetSkinWeights);
}

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::ReadSkinWeights(const TSharedPtr<FJsonObject>& Params)
{
#if !(WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API)
	return MCPError(TEXT("read_skeletal_mesh_skin_weights requires an Unreal Editor build on Unreal Engine 5.4 or newer"));
#else
	FMCPSkinWeightTarget Target;
	if (TSharedPtr<FJsonValue> Error = MCPResolveSkinWeightTarget(Params, Target)) return Error;

	TArray<int32> VertexIndices;
	if (TSharedPtr<FJsonValue> Error = MCPParseSkinWeightVertexIndices(Params, *Target.Description, VertexIndices)) return Error;

	const FSkeletalMeshConstAttributes Attributes(*Target.Description);
	const FSkinWeightsVertexAttributesConstRef SkinWeights = Attributes.GetVertexSkinWeights(Target.ProfileName);
	const FReferenceSkeleton& RefSkeleton = Target.Mesh->GetRefSkeleton();
	TArray<TSharedPtr<FJsonValue>> Vertices;
	for (const int32 VertexIndex : VertexIndices)
	{
		UE::AnimationCore::FBoneWeights Weights;
		const FVertexBoneWeightsConst SourceWeights = SkinWeights.Get(FVertexID(VertexIndex));
		if (TSharedPtr<FJsonValue> Error = MCPReadExactSkinWeights(
			SourceWeights,
			RefSkeleton,
			FString::Printf(TEXT("vertexIndices[%d]=%d"), Vertices.Num(), VertexIndex),
			Weights))
		{
			return Error;
		}
		Vertices.Add(MakeShared<FJsonValueObject>(MCPSerializeSkinWeightVertex(VertexIndex, Weights, RefSkeleton)));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Target.Mesh->GetPathName());
	Result->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
	Result->SetStringField(TEXT("profileName"), Target.DisplayProfileName);
	Result->SetNumberField(TEXT("vertexCount"), Target.Description->Vertices().Num());
	Result->SetNumberField(TEXT("count"), Vertices.Num());
	Result->SetNumberField(TEXT("maxVerticesPerCall"), MCPSkinWeightMaxVerticesPerCall);
	Result->SetNumberField(TEXT("maxInfluences"), Target.MaxInfluences);
	Result->SetStringField(
		TEXT("weightEncoding"),
		TEXT("source MeshDescription weights are normalized and quantized to uint16; rawWeight is exact source storage, while the rebuilt render buffer may use 8-bit weights unless high-precision skin weights are enabled"));
	Result->SetArrayField(TEXT("vertices"), Vertices);
	return MCPResult(Result);
#endif
}

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::SetSkinWeights(const TSharedPtr<FJsonObject>& Params)
{
#if !(WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API)
	return MCPError(TEXT("set_skeletal_mesh_skin_weights requires an Unreal Editor build on Unreal Engine 5.4 or newer"));
#else
	FMCPSkinWeightTarget Target;
	if (TSharedPtr<FJsonValue> Error = MCPResolveSkinWeightTarget(Params, Target)) return Error;

	// The complete batch is parsed, range checked, bone resolved, de-duplicated,
	// quantized and normalized before Modify() or ModifyMeshDescription(). A bad
	// final entry therefore cannot leave the valid prefix written.
	TArray<FMCPSkinWeightEdit> Edits;
	if (TSharedPtr<FJsonValue> Error = MCPParseSkinWeightEdits(Params, Target, Edits)) return Error;

	bool bNeedsMutation = false;
	for (const FMCPSkinWeightEdit& Edit : Edits)
	{
		if (Edit.Before != Edit.Desired)
		{
			bNeedsMutation = true;
			break;
		}
	}
	if (bNeedsMutation)
	{
		if (TSharedPtr<FJsonValue> Blocked = MCPAssetWriteBlockedError(
			Target.Mesh,
			Target.Mesh->GetPathName(),
			TEXT("set skeletal mesh skin weights")))
		{
			return Blocked;
		}
	}

	FSkeletalMeshAttributes Attributes(*Target.Description);
	FSkinWeightsVertexAttributesRef SkinWeights = Attributes.GetVertexSkinWeights(Target.ProfileName);
	const FSkinWeightsVertexAttributesConstRef ReadbackWeights =
		FSkeletalMeshConstAttributes(*Target.Description).GetVertexSkinWeights(Target.ProfileName);
	if (bNeedsMutation)
	{
		Target.Mesh->Modify();
		if (!Target.Mesh->ModifyMeshDescription(Target.LodIndex))
		{
			return MCPError(FString::Printf(
				TEXT("Failed to open LOD %d of '%s' for a transactional MeshDescription edit"),
				Target.LodIndex,
				*Target.Mesh->GetPathName()));
		}
		TSharedPtr<FJsonValue> ReadbackError;
		for (FMCPSkinWeightEdit& Edit : Edits)
		{
			if (Edit.Before != Edit.Desired)
			{
				SkinWeights.Set(FVertexID(Edit.VertexIndex), Edit.Desired);
			}
			// Read exact source storage back. A failed validation is restored below
			// before this handler reports an error.
			const FVertexBoneWeightsConst SourceWeights = ReadbackWeights.Get(FVertexID(Edit.VertexIndex));
			if (TSharedPtr<FJsonValue> Error = MCPReadExactSkinWeights(
				SourceWeights,
				Target.Mesh->GetRefSkeleton(),
				FString::Printf(TEXT("edited vertex %d"), Edit.VertexIndex),
				Edit.After))
			{
				ReadbackError = Error;
				break;
			}
		}
		if (ReadbackError.IsValid())
		{
			for (const FMCPSkinWeightEdit& Edit : Edits)
			{
				SkinWeights.Set(FVertexID(Edit.VertexIndex), Edit.Before);
			}
			Target.Mesh->CommitMeshDescription(Target.LodIndex);
			return ReadbackError;
		}

		USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
		CommitParams.bUpdateMorphTargets = false;
		// A named profile needs mesh-level profile metadata synchronized for rebuilt
		// render data. This does not write another profile's source weights.
		CommitParams.bUpdateSkinWeightProfiles = !Target.ProfileName.IsNone();
		CommitParams.bUpdateVertexAttributes = false;
		CommitParams.bUpdateVertexColors = false;
		CommitParams.bForceUpdate = true;
		if (!Target.Mesh->CommitMeshDescription(Target.LodIndex, CommitParams))
		{
			// Keep a failed commit from leaving the selected source attributes changed
			// in memory. No other vertex, LOD or profile was addressed at any point.
			for (const FMCPSkinWeightEdit& Edit : Edits)
			{
				SkinWeights.Set(FVertexID(Edit.VertexIndex), Edit.Before);
			}
			Target.Mesh->CommitMeshDescription(Target.LodIndex, CommitParams);
			return MCPError(FString::Printf(
				TEXT("Failed to commit skin weights to LOD %d of '%s'; selected weights were restored in memory"),
				Target.LodIndex,
				*Target.Mesh->GetPathName()));
		}
		Target.Mesh->Build();
		Target.Mesh->PostEditChange();
		Target.Mesh->MarkPackageDirty();
		if (!SaveAssetPackage(Target.Mesh))
		{
			auto Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Failed to save SkeletalMesh: %s. The selected source weights changed in memory and remain dirty; use the rollback payload to restore them or save the package after fixing the write failure."),
				*Target.Mesh->GetPathName()));
			Result->SetStringField(TEXT("assetPath"), Target.Mesh->GetPathName());
			Result->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
			Result->SetStringField(TEXT("profileName"), Target.DisplayProfileName);
			Result->SetBoolField(TEXT("updated"), true);
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetBoolField(TEXT("changedButUnsaved"), true);
			TArray<TSharedPtr<FJsonValue>> FailedEditResults;
			const FReferenceSkeleton& RefSkeleton = Target.Mesh->GetRefSkeleton();
			for (const FMCPSkinWeightEdit& Edit : Edits)
			{
				FailedEditResults.Add(MakeShared<FJsonValueObject>(MCPSerializeSkinWeightEdit(Edit, RefSkeleton)));
			}
			Result->SetArrayField(TEXT("edits"), FailedEditResults);
			MCPSetRollback(Result, TEXT("set_skeletal_mesh_skin_weights"), MCPMakeSkinWeightRollbackPayload(Target, Edits));
			return MCPResult(Result);
		}
	}
	else
	{
		for (FMCPSkinWeightEdit& Edit : Edits)
		{
			Edit.After = Edit.Before;
		}
	}

	const FReferenceSkeleton& RefSkeleton = Target.Mesh->GetRefSkeleton();
	TArray<TSharedPtr<FJsonValue>> EditResults;
	int32 ChangedVertices = 0;
	for (const FMCPSkinWeightEdit& Edit : Edits)
	{
		ChangedVertices += Edit.Before != Edit.After ? 1 : 0;
		EditResults.Add(MakeShared<FJsonValueObject>(MCPSerializeSkinWeightEdit(Edit, RefSkeleton)));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Target.Mesh->GetPathName());
	Result->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
	Result->SetStringField(TEXT("profileName"), Target.DisplayProfileName);
	Result->SetBoolField(TEXT("updated"), ChangedVertices > 0);
	Result->SetBoolField(TEXT("saved"), ChangedVertices > 0);
	Result->SetNumberField(TEXT("changedVertices"), ChangedVertices);
	Result->SetNumberField(TEXT("unchangedVertices"), Edits.Num() - ChangedVertices);
	Result->SetNumberField(TEXT("maxInfluences"), Target.MaxInfluences);
	Result->SetStringField(
		TEXT("weightEncoding"),
		TEXT("after is read back from uint16 source MeshDescription storage after native normalization/pruning; the rebuilt render buffer may use 8-bit weights unless high-precision skin weights are enabled"));
	Result->SetArrayField(TEXT("edits"), EditResults);
	if (ChangedVertices > 0)
	{
		MCPSetRollback(Result, TEXT("set_skeletal_mesh_skin_weights"), MCPMakeSkinWeightRollbackPayload(Target, Edits));
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Every selected vertex already held the requested normalized weights, so nothing was written."));
	}
	return MCPResult(Result);
#endif
}

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::ReadBuildSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(AssetPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	TArray<FTargetLod> Targets;
	if (TSharedPtr<FJsonValue> Error = ResolveTargetLods(Params, Mesh, Targets)) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("lodCount"), Mesh->GetLODNum());
	TArray<TSharedPtr<FJsonValue>> Lods;
	for (const FTargetLod& Target : Targets)
	{
		Lods.Add(MakeShared<FJsonValueObject>(MakeLodResult(Target.Index, Target.Before, Target.Before)));
	}
	Result->SetArrayField(TEXT("lods"), Lods);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::SetOptimizeForInstancing(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	// bOptimizeForInstancing is a 5.8 engine feature with no earlier equivalent,
	// so there is nothing to write and nothing to stand in for it. The action
	// stays registered and says why, rather than answering "Unknown method".
	return MCPError(
		TEXT("set_skeletal_mesh_optimize_for_instancing requires Unreal Engine 5.8 or newer: ")
		TEXT("FSkeletalMeshBuildSettings::bOptimizeForInstancing does not exist in this engine, ")
		TEXT("and no earlier skeletal mesh build setting stands in for it."));
#else
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	bool bEnabled = false;
	if (!Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		return MCPError(TEXT("Missing required parameter 'enabled'"));
	}

	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(AssetPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	TArray<FTargetLod> Targets;
	if (TSharedPtr<FJsonValue> Error = ResolveTargetLods(Params, Mesh, Targets)) return Error;

	bool bNeedsMutation = false;
	for (const FTargetLod& Target : Targets)
	{
		if (Target.Before.bOptimizeForInstancing != bEnabled)
		{
			bNeedsMutation = true;
			break;
		}
	}

	TArray<TSharedPtr<FJsonValue>> Lods;
	// Which LODs this call actually flipped, not how many. The rollback below has
	// to address exactly those and no others.
	TArray<int32> ChangedLodIndices;
	if (bNeedsMutation)
	{
		// Establish the transaction before the first setter. The subsystem also
		// protects its scoped rebuild, but this preserves the handler's explicit
		// mutation boundary for undo/redo and fail-closed preflight behavior.
		Mesh->Modify();
		for (const FTargetLod& Target : Targets)
		{
			if (Target.Before.bOptimizeForInstancing != bEnabled)
			{
				ChangedLodIndices.Add(Target.Index);
				FSkeletalMeshBuildSettings Updated = Target.Before;
				Updated.bOptimizeForInstancing = bEnabled;
				USkeletalMeshEditorSubsystem::SetLodBuildSettings(Mesh, Target.Index, Updated);
			}
			FSkeletalMeshBuildSettings After;
			USkeletalMeshEditorSubsystem::GetLodBuildSettings(Mesh, Target.Index, After);
			Lods.Add(MakeShared<FJsonValueObject>(MakeLodResult(Target.Index, Target.Before, After)));
		}
		if (!SaveAssetPackage(Mesh))
		{
			return MCPError(FString::Printf(TEXT("Failed to save SkeletalMesh: %s"), *Mesh->GetPathName()));
		}
	}
	else
	{
		for (const FTargetLod& Target : Targets)
		{
			Lods.Add(MakeShared<FJsonValueObject>(MakeLodResult(Target.Index, Target.Before, Target.Before)));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetBoolField(TEXT("updated"), bNeedsMutation);
	Result->SetBoolField(TEXT("saved"), bNeedsMutation);
	Result->SetNumberField(TEXT("changedLods"), ChangedLodIndices.Num());
	Result->SetArrayField(TEXT("lods"), Lods);

	// The inverse is this same action carrying the flag the mesh used to hold.
	// bOptimizeForInstancing is a bool, so every LOD this call flipped held
	// !bEnabled, and one replay restores all of them. What limits the rollback is
	// addressing: the action targets one lodIndex or every LOD, so a partial set
	// of flipped LODs cannot be named.
	const int32 LodCount = Mesh->GetLODNum();
	if (ChangedLodIndices.IsEmpty())
	{
		MCPSetNoRollback(Result,
			TEXT("Every targeted LOD already held the requested bOptimizeForInstancing value, ")
			TEXT("so no build setting was written and there is nothing to undo."));
	}
	else if (ChangedLodIndices.Num() == 1 || ChangedLodIndices.Num() == LodCount)
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		Payload->SetBoolField(TEXT("enabled"), !bEnabled);
		if (ChangedLodIndices.Num() == LodCount)
		{
			Payload->SetBoolField(TEXT("allLods"), true);
		}
		else
		{
			Payload->SetNumberField(TEXT("lodIndex"), ChangedLodIndices[0]);
		}
		MCPSetRollback(Result, TEXT("set_skeletal_mesh_optimize_for_instancing"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("bOptimizeForInstancing was flipped on %d of this mesh's %d LODs, and ")
			TEXT("set_skeletal_mesh_optimize_for_instancing addresses a single lodIndex or all LODs; ")
			TEXT("restoring that mixed state needs a call that accepts a list of LOD indices."),
			ChangedLodIndices.Num(),
			LodCount));
	}
	return MCPResult(Result);
#endif // UE_MCP_HAS_5_8_API
}
