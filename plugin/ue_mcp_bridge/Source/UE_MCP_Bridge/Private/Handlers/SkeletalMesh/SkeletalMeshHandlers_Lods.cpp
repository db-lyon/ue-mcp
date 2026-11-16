// generate_skeletal_mesh_lods (#1372): auto-reduced LODs on skeletal meshes through
// USkeletalMeshEditorSubsystem::RegenerateLOD. A member of FSkeletalMeshHandlers in
// its own translation unit; registration stays in SkeletalMeshHandlers.cpp.

#include "SkeletalMeshHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "SkeletalMeshEditorSubsystem.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Named, not anonymous: the module is a unity build and anonymous namespaces
// from neighbouring files merge.
namespace MCPSkeletalLods
{
	constexpr int32 MaxLods = 8;
	constexpr int32 MaxMeshes = 500;

	/** Triangles and vertices of every LOD the mesh's source model holds. */
	TArray<TSharedPtr<FJsonValue>> DescribeLods(USkeletalMesh* Mesh)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
		for (int32 Index = 0; Index < Mesh->GetLODNum(); ++Index)
		{
			TSharedPtr<FJsonObject> Lod = MakeShared<FJsonObject>();
			Lod->SetNumberField(TEXT("index"), Index);
			if (Model && Model->LODModels.IsValidIndex(Index))
			{
				const FSkeletalMeshLODModel& LodModel = Model->LODModels[Index];
				int32 Triangles = 0;
				for (const FSkelMeshSection& Section : LodModel.Sections) Triangles += Section.NumTriangles;
				Lod->SetNumberField(TEXT("triangles"), Triangles);
				Lod->SetNumberField(TEXT("vertices"), LodModel.NumVertices);
			}
			if (const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(Index))
			{
				Lod->SetBoolField(TEXT("generated"), Info->bHasBeenSimplified);
				if (Index > 0) Lod->SetNumberField(TEXT("trianglePercentage"), Info->ReductionSettings.NumOfTrianglesPercentage * 100.0);
			}
			Out.Add(MakeShared<FJsonValueObject>(Lod));
		}
		return Out;
	}
}

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::GenerateSkeletalMeshLods(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPSkeletalLods;

	// Every parameter is read before anything can fail (#1057).
	FString AssetPath;
	const bool bHasPath = TryGetStringParam(Params, TEXT("assetPath"), AssetPath) && !AssetPath.IsEmpty();
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("assetPaths"), PathsArr) && PathsArr;
	FString Directory;
	const bool bHasDirectory = TryGetStringParam(Params, TEXT("directory"), Directory) && !Directory.IsEmpty();
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	int32 LodCount = 0;
	const bool bHasLodCount = TryGetNumberParam(Params, TEXT("lodCount"), LodCount);
	const TArray<TSharedPtr<FJsonValue>>* PercentArr = nullptr;
	const bool bHasPercents = TryGetArrayParam(Params, TEXT("trianglePercentages"), PercentArr) && PercentArr;
	const bool bRegenerateImported = OptionalBool(Params, TEXT("regenerateImported"), false);
	const bool bSkipExisting = OptionalBool(Params, TEXT("skipExisting"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	if ((bHasPath ? 1 : 0) + (bHasPaths ? 1 : 0) + (bHasDirectory ? 1 : 0) != 1)
	{
		return MCPError(TEXT("Pass exactly one of assetPath, assetPaths and directory"));
	}
	if (!bHasLodCount || LodCount < 1 || LodCount > MaxLods)
	{
		return MCPError(FString::Printf(TEXT("lodCount must be 1 to %d, counting LOD 0"), MaxLods));
	}
	TArray<float> Percents;
	if (bHasPercents)
	{
		for (int32 Index = 0; Index < PercentArr->Num(); ++Index)
		{
			double Value = 0.0;
			if (!(*PercentArr)[Index].IsValid() || !(*PercentArr)[Index]->TryGetNumber(Value) || Value <= 0.0 || Value > 100.0)
			{
				return MCPError(FString::Printf(TEXT("trianglePercentages[%d] must be above 0 and at most 100"), Index));
			}
			Percents.Add((float)(Value / 100.0));
		}
		if (Percents.Num() != LodCount - 1)
		{
			return MCPError(FString::Printf(TEXT("trianglePercentages lists %d value(s); lodCount %d needs %d, one per LOD after LOD 0"),
				Percents.Num(), LodCount, LodCount - 1));
		}
	}

	TArray<FString> Paths;
	if (bHasPath) Paths.Add(AssetPath);
	else if (bHasPaths) Paths = JsonArrayToStringList(PathsArr);
	else
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Directory));
		Filter.bRecursivePaths = bRecursive;
		Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
		TArray<FAssetData> Found;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Found);
		for (const FAssetData& Data : Found) Paths.Add(Data.GetObjectPathString());
		Paths.Sort();
	}
	if (Paths.Num() == 0) return MCPError(TEXT("No SkeletalMesh to process"));
	if (Paths.Num() > MaxMeshes) return MCPError(FString::Printf(TEXT("%d meshes matched; the limit is %d per call. Split the work into several calls of %d or fewer meshes, each on a narrower directory or an explicit list of paths."), Paths.Num(), MaxMeshes, MaxMeshes));

	TArray<USkeletalMesh*> Meshes;
	for (const FString& Path : Paths)
	{
		UObject* Loaded = MCPLoadAssetObject(Path);
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(Loaded);
		if (!Mesh) return Loaded ? MCPAssetWrongTypeError(Path, Loaded, TEXT("SkeletalMesh")) : MCPAssetNotFoundError(Path);
		if (!bDryRun)
		{
			if (auto Blocked = MCPAssetWriteBlockedError(Mesh, Mesh->GetPathName(), TEXT("generate its LODs"))) return Blocked;
		}
		Meshes.Add(Mesh);
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Generated = 0, Skipped = 0, Failed = 0;
	for (USkeletalMesh* Mesh : Meshes)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		const int32 Before = Mesh->GetLODNum();
		Row->SetNumberField(TEXT("lodCountBefore"), Before);

		FString SkipReason;
		if (Before > LodCount) SkipReason = TEXT("the mesh already has more LODs than lodCount; nothing is removed");
		else if (Before == LodCount && bSkipExisting) SkipReason = TEXT("already at lodCount; pass skipExisting=false to regenerate");
		if (!SkipReason.IsEmpty() || bDryRun)
		{
			Row->SetStringField(TEXT("status"), SkipReason.IsEmpty() ? TEXT("would_generate") : TEXT("skipped"));
			if (!SkipReason.IsEmpty()) Row->SetStringField(TEXT("reason"), SkipReason);
			Row->SetArrayField(TEXT("lods"), DescribeLods(Mesh));
			if (!SkipReason.IsEmpty()) ++Skipped;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		bool bOk = USkeletalMeshEditorSubsystem::RegenerateLOD(Mesh, LodCount, bRegenerateImported, false);
		if (bOk && Percents.Num() > 0)
		{
			// The first pass creates the LOD entries; the second reduces each to its own percentage.
			for (int32 Index = 1; Index < LodCount; ++Index)
			{
				if (FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(Index))
				{
					Info->ReductionSettings.TerminationCriterion = SMTC_NumOfTriangles;
					Info->ReductionSettings.NumOfTrianglesPercentage = Percents[Index - 1];
				}
			}
			bOk = USkeletalMeshEditorSubsystem::RegenerateLOD(Mesh, LodCount, true, false);
		}
		Row->SetNumberField(TEXT("lodCountAfter"), Mesh->GetLODNum());
		Row->SetArrayField(TEXT("lods"), DescribeLods(Mesh));
		if (!bOk)
		{
			Row->SetStringField(TEXT("status"), TEXT("failed"));
			Row->SetStringField(TEXT("error"), TEXT("RegenerateLOD returned false; an imported LOD is kept unless regenerateImported is true"));
			++Failed;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		Row->SetStringField(TEXT("status"), TEXT("generated"));
		if (bSave)
		{
			FString SaveError;
			const bool bSaved = SaveAssetPackageChecked(Mesh, SaveError);
			Row->SetBoolField(TEXT("saved"), bSaved);
			if (!bSaved)
			{
				Row->SetStringField(TEXT("saveError"), SaveError);
				++Failed;
			}
		}
		++Generated;
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	if (Generated > 0) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("lodCount"), LodCount);
	Result->SetNumberField(TEXT("generated"), Generated);
	Result->SetNumberField(TEXT("skipped"), Skipped);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("meshes"), Rows);
	if (Failed > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("%d mesh(es) failed to generate or save; meshes lists each one"), Failed));
	}
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("Generated LODs replace the previous ones. Remove them with the skeletal mesh editor's LOD removal, or revert the packages."));
	return MCPResult(Result);
}
