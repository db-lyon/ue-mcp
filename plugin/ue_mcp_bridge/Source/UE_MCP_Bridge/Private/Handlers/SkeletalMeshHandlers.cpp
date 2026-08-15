#include "SkeletalMeshHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/SkeletalMesh.h"
#include "SkeletalMeshEditorSubsystem.h"

namespace
{
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
		Out->SetBoolField(TEXT("bOptimizeForInstancing"), Settings.bOptimizeForInstancing);
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
		Lod->SetBoolField(TEXT("beforeOptimizeForInstancing"), Before.bOptimizeForInstancing);
		Lod->SetBoolField(TEXT("afterOptimizeForInstancing"), After.bOptimizeForInstancing);
		Lod->SetBoolField(TEXT("changed"), Before.bOptimizeForInstancing != After.bOptimizeForInstancing);
		return Lod;
	}
}
void FSkeletalMeshHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("set_skeletal_mesh_optimize_for_instancing"), &SetOptimizeForInstancing);
	Registry.RegisterHandler(TEXT("read_skeletal_mesh_build_settings"), &ReadBuildSettings);
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
	int32 ChangedLodCount = 0;
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
				++ChangedLodCount;
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
	Result->SetNumberField(TEXT("changedLods"), ChangedLodCount);
	Result->SetArrayField(TEXT("lods"), Lods);
	return MCPResult(Result);
}
