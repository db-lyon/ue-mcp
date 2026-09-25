// Read-only mesh geometry: per-section vertex data, and mesh measurement.
//
// Before this existed the only way to see a vertex was execute_python calling
// UKismetProceduralMeshLibrary.get_section_from_static_mesh, which works but
// makes the ProceduralMeshComponent plugin a silent prerequisite for reading a
// mesh, and returns nothing at all for a skeletal mesh. asset(get_mesh_info)
// gives a vertex COUNT and asset(get_mesh_bounds) gives a box; neither answers
// "where on this asset is the grip" (#926), "which triangles carry this UV
// region" (#948) or "is this mesh watertight and how big is it" (#938).
//
// Both handlers read the engine's own render data - FStaticMeshLODResources for
// a UStaticMesh, FSkeletalMeshLODRenderData for a USkeletalMesh - so there is no
// plugin dependency and the two asset types answer through one code path.

#include "AssetHandlers_Geometry.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
// FSkeletalMaterial moved out of Engine/SkeletalMesh.h in later UE versions.
// Pull it explicitly via SkinnedAssetCommon when available.
#if __has_include("Engine/SkinnedAssetCommon.h")
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
// How many vertices one get_mesh_geometry response may carry inline. A modest
// prop is a few thousand; a scanned asset is millions. Truncating geometry
// would be worse than refusing it, because a partial vertex array still indexes
// as if it were whole, so the cap is a hard stop with dumpToFile as the way
// past it rather than a silent slice.
constexpr int32 GeometryInlineVertexCap = 20000;

// Above this triangle count the edge-adjacency pass that answers isClosed /
// isManifold is skipped and reported as skipped. Every other metric is O(n)
// with no allocation and stays available.
constexpr int32 GeometryTopologyTriangleCap = 1000000;

// Position quantisation used to weld vertices before topology analysis. A UV or
// smoothing seam splits one physical vertex into several render vertices, so
// raw index comparison reports a watertight mesh as full of boundary edges.
constexpr float GeometryWeldTolerance = 0.01f;

/** One renderable section of a LOD, described the same way for both mesh types. */
struct FGeometrySectionInfo
{
	int32 SectionIndex = 0;
	int32 MaterialIndex = 0;
	FString MaterialSlotName;
	FString MaterialPath;
	uint32 FirstIndex = 0;
	uint32 NumTriangles = 0;
	uint32 MinVertexIndex = 0;
	uint32 MaxVertexIndex = 0;
	bool bDisabled = false;
};

/** Everything read off one LOD, in LOD-global vertex indexing. */
struct FGeometrySnapshot
{
	FString AssetType;
	int32 LodCount = 0;
	int32 NumTexCoords = 0;
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<FVector2f> UVs;          // Only the requested channel.
	TArray<uint32> Indices;         // Triangle list, LOD-global vertex indices.
	TArray<FGeometrySectionInfo> Sections;
	FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInit);
};

FString GeometryMaterialPathForSlot(const UMaterialInterface* Material)
{
	return Material ? Material->GetPathName() : FString();
}

/** Read a static mesh LOD. Returns false with OutError set on any miss. */
bool BuildGeometrySnapshotFromStaticMesh(
	const UStaticMesh* Mesh,
	int32 LodIndex,
	int32 UVChannel,
	bool bWantNormals,
	bool bWantUVs,
	FGeometrySnapshot& Out,
	FString& OutError)
{
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		OutError = FString::Printf(TEXT("StaticMesh '%s' has no render data built"), *Mesh->GetPathName());
		return false;
	}

	Out.AssetType = TEXT("StaticMesh");
	Out.LodCount = RenderData->LODResources.Num();
	Out.Bounds = Mesh->GetBounds();

	if (LodIndex < 0 || LodIndex >= Out.LodCount)
	{
		OutError = FString::Printf(TEXT("lodIndex %d out of range (mesh has %d LODs)"), LodIndex, Out.LodCount);
		return false;
	}

	const FStaticMeshLODResources& LOD = RenderData->LODResources[LodIndex];
	const FPositionVertexBuffer& PositionBuffer = LOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;

	// GetNumVertices is a count that survives the CPU copy being released, so the
	// data pointer is what says whether the vertices are actually readable here.
	// Editor builds keep them; a cooked, streamed build does not.
	const int32 NumVertices = static_cast<int32>(PositionBuffer.GetNumVertices());
	if (NumVertices == 0 || PositionBuffer.GetVertexData() == nullptr)
	{
		OutError = FString::Printf(TEXT("StaticMesh '%s' LOD %d has no CPU-side vertex data"), *Mesh->GetPathName(), LodIndex);
		return false;
	}

	Out.NumTexCoords = static_cast<int32>(VertexBuffer.GetNumTexCoords());
	if (bWantUVs && Out.NumTexCoords > 0 && (UVChannel < 0 || UVChannel >= Out.NumTexCoords))
	{
		OutError = FString::Printf(TEXT("uvChannel %d out of range (LOD %d has %d UV channels)"), UVChannel, LodIndex, Out.NumTexCoords);
		return false;
	}

	Out.Positions.Reserve(NumVertices);
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		Out.Positions.Add(PositionBuffer.VertexPosition(static_cast<uint32>(VertexIndex)));
	}

	const int32 NumTangentVertices = static_cast<int32>(VertexBuffer.GetNumVertices());
	const bool bTangentsResident = VertexBuffer.GetTangentData() != nullptr;
	const bool bTexCoordsResident = VertexBuffer.GetTexCoordData() != nullptr;
	if (bWantNormals && bTangentsResident && NumTangentVertices >= NumVertices)
	{
		Out.Normals.Reserve(NumVertices);
		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			const FVector4f TangentZ = VertexBuffer.VertexTangentZ(static_cast<uint32>(VertexIndex));
			Out.Normals.Add(FVector3f(TangentZ.X, TangentZ.Y, TangentZ.Z));
		}
	}
	if (bWantUVs && Out.NumTexCoords > 0 && bTexCoordsResident && NumTangentVertices >= NumVertices)
	{
		Out.UVs.Reserve(NumVertices);
		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			Out.UVs.Add(VertexBuffer.GetVertexUV(static_cast<uint32>(VertexIndex), static_cast<uint32>(UVChannel)));
		}
	}

	// GetCopy memcpys from the stored indices, so ask whether they are stored,
	// not whether the buffer was flagged for CPU access: the editor keeps the
	// data regardless of that flag, and a cooked streamed build drops it.
	if (LOD.IndexBuffer.GetNumIndices() > 0 && LOD.IndexBuffer.GetIndexDataSize() == 0)
	{
		OutError = FString::Printf(
			TEXT("StaticMesh '%s' LOD %d index buffer has no CPU-side copy"),
			*Mesh->GetPathName(), LodIndex);
		return false;
	}
	LOD.IndexBuffer.GetCopy(Out.Indices);

	const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
	for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& Section = LOD.Sections[SectionIndex];
		FGeometrySectionInfo Info;
		Info.SectionIndex = SectionIndex;
		Info.MaterialIndex = Section.MaterialIndex;
		Info.FirstIndex = Section.FirstIndex;
		Info.NumTriangles = Section.NumTriangles;
		Info.MinVertexIndex = Section.MinVertexIndex;
		Info.MaxVertexIndex = Section.MaxVertexIndex;
		if (Materials.IsValidIndex(Section.MaterialIndex))
		{
			Info.MaterialSlotName = Materials[Section.MaterialIndex].MaterialSlotName.ToString();
			Info.MaterialPath = GeometryMaterialPathForSlot(Materials[Section.MaterialIndex].MaterialInterface);
		}
		Out.Sections.Add(MoveTemp(Info));
	}

	return true;
}

/** Read a skeletal mesh LOD. Returns false with OutError set on any miss. */
bool BuildGeometrySnapshotFromSkeletalMesh(
	const USkeletalMesh* Mesh,
	int32 LodIndex,
	int32 UVChannel,
	bool bWantNormals,
	bool bWantUVs,
	FGeometrySnapshot& Out,
	FString& OutError)
{
	const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.Num() == 0)
	{
		OutError = FString::Printf(TEXT("SkeletalMesh '%s' has no render data built"), *Mesh->GetPathName());
		return false;
	}

	Out.AssetType = TEXT("SkeletalMesh");
	Out.LodCount = RenderData->LODRenderData.Num();
	Out.Bounds = Mesh->GetBounds();

	if (LodIndex < 0 || LodIndex >= Out.LodCount)
	{
		OutError = FString::Printf(TEXT("lodIndex %d out of range (mesh has %d LODs)"), LodIndex, Out.LodCount);
		return false;
	}

	const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LodIndex];
	const FPositionVertexBuffer& PositionBuffer = LOD.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = LOD.StaticVertexBuffers.StaticMeshVertexBuffer;

	const int32 NumVertices = static_cast<int32>(PositionBuffer.GetNumVertices());
	if (NumVertices == 0 || PositionBuffer.GetVertexData() == nullptr)
	{
		OutError = FString::Printf(TEXT("SkeletalMesh '%s' LOD %d has no CPU-side vertex data"), *Mesh->GetPathName(), LodIndex);
		return false;
	}

	Out.NumTexCoords = static_cast<int32>(VertexBuffer.GetNumTexCoords());
	if (bWantUVs && Out.NumTexCoords > 0 && (UVChannel < 0 || UVChannel >= Out.NumTexCoords))
	{
		OutError = FString::Printf(TEXT("uvChannel %d out of range (LOD %d has %d UV channels)"), UVChannel, LodIndex, Out.NumTexCoords);
		return false;
	}

	Out.Positions.Reserve(NumVertices);
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		Out.Positions.Add(PositionBuffer.VertexPosition(static_cast<uint32>(VertexIndex)));
	}

	const int32 NumTangentVertices = static_cast<int32>(VertexBuffer.GetNumVertices());
	const bool bTangentsResident = VertexBuffer.GetTangentData() != nullptr;
	const bool bTexCoordsResident = VertexBuffer.GetTexCoordData() != nullptr;
	if (bWantNormals && bTangentsResident && NumTangentVertices >= NumVertices)
	{
		Out.Normals.Reserve(NumVertices);
		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			const FVector4f TangentZ = VertexBuffer.VertexTangentZ(static_cast<uint32>(VertexIndex));
			Out.Normals.Add(FVector3f(TangentZ.X, TangentZ.Y, TangentZ.Z));
		}
	}
	if (bWantUVs && Out.NumTexCoords > 0 && bTexCoordsResident && NumTangentVertices >= NumVertices)
	{
		Out.UVs.Reserve(NumVertices);
		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			Out.UVs.Add(VertexBuffer.GetVertexUV(static_cast<uint32>(VertexIndex), static_cast<uint32>(UVChannel)));
		}
	}

	if (!LOD.MultiSizeIndexContainer.IsIndexBufferValid())
	{
		OutError = FString::Printf(TEXT("SkeletalMesh '%s' LOD %d has no index buffer"), *Mesh->GetPathName(), LodIndex);
		return false;
	}
	LOD.MultiSizeIndexContainer.GetIndexBuffer(Out.Indices);

	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	for (int32 SectionIndex = 0; SectionIndex < LOD.RenderSections.Num(); ++SectionIndex)
	{
		const FSkelMeshRenderSection& Section = LOD.RenderSections[SectionIndex];
		FGeometrySectionInfo Info;
		Info.SectionIndex = SectionIndex;
		Info.MaterialIndex = static_cast<int32>(Section.MaterialIndex);
		Info.FirstIndex = Section.BaseIndex;
		Info.NumTriangles = Section.NumTriangles;
		Info.MinVertexIndex = Section.BaseVertexIndex;
		Info.MaxVertexIndex = Section.NumVertices > 0
			? Section.BaseVertexIndex + Section.NumVertices - 1
			: Section.BaseVertexIndex;
		Info.bDisabled = Section.bDisabled;
		if (Materials.IsValidIndex(Info.MaterialIndex))
		{
			Info.MaterialSlotName = Materials[Info.MaterialIndex].MaterialSlotName.ToString();
			Info.MaterialPath = GeometryMaterialPathForSlot(Materials[Info.MaterialIndex].MaterialInterface);
		}
		Out.Sections.Add(MoveTemp(Info));
	}

	return true;
}

/** Load the addressed mesh and fill a snapshot, or return the caller's error. */
TSharedPtr<FJsonValue> BuildGeometrySnapshot(
	const FString& AssetPath,
	int32 LodIndex,
	int32 UVChannel,
	bool bWantNormals,
	bool bWantUVs,
	FGeometrySnapshot& Out)
{
	FString Error;

	// Load once as UObject and branch on the concrete type. Loading twice with a
	// typed LoadObject logs a cast failure for whichever type the asset is not,
	// which would put a spurious warning in the editor log on every skeletal
	// mesh read.
	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
	{
		if (!BuildGeometrySnapshotFromStaticMesh(StaticMesh, LodIndex, UVChannel, bWantNormals, bWantUVs, Out, Error))
		{
			return MCPError(Error);
		}
		return nullptr;
	}

	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
	{
		if (!BuildGeometrySnapshotFromSkeletalMesh(SkeletalMesh, LodIndex, UVChannel, bWantNormals, bWantUVs, Out, Error))
		{
			return MCPError(Error);
		}
		return nullptr;
	}

	return MCPError(FString::Printf(
		TEXT("Asset '%s' is a %s, not a StaticMesh or SkeletalMesh"),
		*AssetPath, *Asset->GetClass()->GetName()));
}

/** Section-local triangle span, clamped to what the index buffer actually holds. */
bool GeometrySectionTriangleRange(
	const FGeometrySnapshot& Snapshot,
	const FGeometrySectionInfo& Section,
	int32& OutFirstIndex,
	int32& OutIndexCount)
{
	const int64 First = static_cast<int64>(Section.FirstIndex);
	const int64 Count = static_cast<int64>(Section.NumTriangles) * 3;
	if (First < 0 || Count < 0 || First + Count > static_cast<int64>(Snapshot.Indices.Num()))
	{
		return false;
	}
	OutFirstIndex = static_cast<int32>(First);
	OutIndexCount = static_cast<int32>(Count);
	return true;
}

TSharedPtr<FJsonObject> GeometryVec3ToJson(const FVector3f& V)
{
	return MCPVec3ToJsonObject(FVector(V));
}

TSharedPtr<FJsonObject> GeometryUVToJson(const FVector2f& UV)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("u"), UV.X);
	Obj->SetNumberField(TEXT("v"), UV.Y);
	return Obj;
}

FString MakeDefaultGeometryDumpPath(const FString& AssetPath, int32 LodIndex)
{
	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
	const FString PathHash = FString::Printf(TEXT("%08x"), GetTypeHash(AssetPath));
	const FString BaseName = FPaths::MakeValidFileName(
		FString::Printf(TEXT("%s_LOD%d_%s"), *AssetName, LodIndex, *PathHash));
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP"), TEXT("MeshGeometry"), BaseName + TEXT(".json"));
}

/** Serialise a result object to disk under the shared dump convention in
 *  HandlerUtils.h (MCPResolveDumpPath / MCPWriteDumpFile). */
bool WriteGeometryJsonToFile(
	const TSharedPtr<FJsonObject>& JsonObject,
	const FString& RequestedPath,
	const FString& AssetPath,
	int32 LodIndex,
	FString& OutResolvedPath,
	FString& OutError)
{
	OutResolvedPath = MCPResolveDumpPath(
		RequestedPath.IsEmpty() ? MakeDefaultGeometryDumpPath(AssetPath, LodIndex) : RequestedPath);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
	{
		OutError = TEXT("Failed to serialize mesh geometry JSON");
		return false;
	}

	return MCPWriteDumpFile(OutResolvedPath, JsonText, TEXT("mesh geometry dump"), OutError);
}

/** Welded-vertex key so a UV seam does not read as a hole. */
struct FGeometryWeldKey
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;

	bool operator==(const FGeometryWeldKey& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}
};

uint32 GetTypeHash(const FGeometryWeldKey& Key)
{
	return HashCombine(HashCombine(::GetTypeHash(Key.X), ::GetTypeHash(Key.Y)), ::GetTypeHash(Key.Z));
}

FGeometryWeldKey MakeGeometryWeldKey(const FVector3f& Position)
{
	FGeometryWeldKey Key;
	Key.X = FMath::RoundToInt(Position.X / GeometryWeldTolerance);
	Key.Y = FMath::RoundToInt(Position.Y / GeometryWeldTolerance);
	Key.Z = FMath::RoundToInt(Position.Z / GeometryWeldTolerance);
	return Key;
}
}


void FAssetGeometryHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("asset"));
	Registry.RegisterHandler(TEXT("get_mesh_geometry"), &GetMeshGeometry);
	Registry.RegisterHandler(TEXT("measure_mesh_geometry"), &MeasureMeshGeometry);
}


TSharedPtr<FJsonValue> FAssetGeometryHandlers::GetMeshGeometry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const int32 LodIndex = OptionalInt(Params, TEXT("lodIndex"), 0);
	const int32 UVChannel = OptionalInt(Params, TEXT("uvChannel"), 0);
	const bool bDumpToFile = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));

	const bool bHasSectionIndex = HasParam(Params, TEXT("sectionIndex"));
	const int32 RequestedSection = OptionalInt(Params, TEXT("sectionIndex"), 0);

	// `include` selects which arrays come back. Omitted means all four, which is
	// what a caller who does not know what they need should get.
	bool bWantPositions = true;
	bool bWantUVs = true;
	bool bWantNormals = true;
	bool bWantTriangles = true;
	const TArray<TSharedPtr<FJsonValue>>* IncludeArray = nullptr;
	if (TryGetArrayParam(Params, TEXT("include"), IncludeArray) && IncludeArray)
	{
		bWantPositions = bWantUVs = bWantNormals = bWantTriangles = false;
		for (const TSharedPtr<FJsonValue>& Entry : *IncludeArray)
		{
			if (!Entry.IsValid()) continue;
			const FString Token = Entry->AsString().ToLower();
			if (Token == TEXT("positions")) bWantPositions = true;
			else if (Token == TEXT("uvs")) bWantUVs = true;
			else if (Token == TEXT("normals")) bWantNormals = true;
			else if (Token == TEXT("triangles")) bWantTriangles = true;
			else
			{
				return MCPError(FString::Printf(
					TEXT("include entry '%s' is not one of: positions, uvs, normals, triangles"),
					*Entry->AsString()));
			}
		}
		if (!bWantPositions && !bWantUVs && !bWantNormals && !bWantTriangles)
		{
			return MCPError(TEXT("'include' was empty: pass at least one of positions, uvs, normals, triangles"));
		}
	}

	FGeometrySnapshot Snapshot;
	if (auto Err = BuildGeometrySnapshot(AssetPath, LodIndex, UVChannel, bWantNormals, bWantUVs, Snapshot))
	{
		return Err;
	}

	TArray<int32> SectionsToEmit;
	if (bHasSectionIndex)
	{
		if (!Snapshot.Sections.IsValidIndex(RequestedSection))
		{
			return MCPError(FString::Printf(
				TEXT("sectionIndex %d out of range (LOD %d has %d sections)"),
				RequestedSection, LodIndex, Snapshot.Sections.Num()));
		}
		SectionsToEmit.Add(RequestedSection);
	}
	else
	{
		for (int32 Index = 0; Index < Snapshot.Sections.Num(); ++Index)
		{
			SectionsToEmit.Add(Index);
		}
	}

	// Size gate. Count what would actually be emitted, not the whole LOD, so
	// asking for one section of a scanned asset still answers.
	int64 EmittedVertices = 0;
	for (int32 SectionIndex : SectionsToEmit)
	{
		const FGeometrySectionInfo& Section = Snapshot.Sections[SectionIndex];
		EmittedVertices += static_cast<int64>(Section.MaxVertexIndex) - static_cast<int64>(Section.MinVertexIndex) + 1;
	}
	if (!bDumpToFile && EmittedVertices > GeometryInlineVertexCap)
	{
		return MCPError(FString::Printf(
			TEXT("This request would return %lld vertices, over the %d inline limit. Pass dumpToFile=true to write the full data to a file, or narrow it with sectionIndex / a higher lodIndex."),
			EmittedVertices, GeometryInlineVertexCap));
	}

	TArray<TSharedPtr<FJsonValue>> SectionObjects;
	int64 TotalEmittedTriangles = 0;
	for (int32 SectionIndex : SectionsToEmit)
	{
		const FGeometrySectionInfo& Section = Snapshot.Sections[SectionIndex];

		TSharedPtr<FJsonObject> SectionObj = MakeShared<FJsonObject>();
		SectionObj->SetNumberField(TEXT("sectionIndex"), Section.SectionIndex);
		SectionObj->SetNumberField(TEXT("materialIndex"), Section.MaterialIndex);
		if (!Section.MaterialSlotName.IsEmpty()) SectionObj->SetStringField(TEXT("materialSlotName"), Section.MaterialSlotName);
		if (!Section.MaterialPath.IsEmpty()) SectionObj->SetStringField(TEXT("materialPath"), Section.MaterialPath);
		SectionObj->SetNumberField(TEXT("firstIndex"), Section.FirstIndex);
		SectionObj->SetNumberField(TEXT("triangleCount"), Section.NumTriangles);
		SectionObj->SetNumberField(TEXT("baseVertexIndex"), Section.MinVertexIndex);
		SectionObj->SetNumberField(TEXT("maxVertexIndex"), Section.MaxVertexIndex);
		if (Section.bDisabled) SectionObj->SetBoolField(TEXT("disabled"), true);

		const int32 SectionFirstVertex = static_cast<int32>(Section.MinVertexIndex);
		const int32 SectionLastVertex = static_cast<int32>(Section.MaxVertexIndex);
		const int32 SectionVertexCount = SectionLastVertex - SectionFirstVertex + 1;
		SectionObj->SetNumberField(TEXT("vertexCount"), SectionVertexCount);

		if (bWantPositions)
		{
			TArray<TSharedPtr<FJsonValue>> Positions;
			Positions.Reserve(SectionVertexCount);
			for (int32 VertexIndex = SectionFirstVertex; VertexIndex <= SectionLastVertex; ++VertexIndex)
			{
				if (!Snapshot.Positions.IsValidIndex(VertexIndex)) break;
				Positions.Add(MakeShared<FJsonValueObject>(GeometryVec3ToJson(Snapshot.Positions[VertexIndex])));
			}
			SectionObj->SetArrayField(TEXT("positions"), Positions);
		}

		if (bWantNormals)
		{
			TArray<TSharedPtr<FJsonValue>> Normals;
			Normals.Reserve(SectionVertexCount);
			for (int32 VertexIndex = SectionFirstVertex; VertexIndex <= SectionLastVertex; ++VertexIndex)
			{
				if (!Snapshot.Normals.IsValidIndex(VertexIndex)) break;
				Normals.Add(MakeShared<FJsonValueObject>(GeometryVec3ToJson(Snapshot.Normals[VertexIndex])));
			}
			SectionObj->SetArrayField(TEXT("normals"), Normals);
		}

		if (bWantUVs)
		{
			TArray<TSharedPtr<FJsonValue>> UVs;
			UVs.Reserve(SectionVertexCount);
			for (int32 VertexIndex = SectionFirstVertex; VertexIndex <= SectionLastVertex; ++VertexIndex)
			{
				if (!Snapshot.UVs.IsValidIndex(VertexIndex)) break;
				UVs.Add(MakeShared<FJsonValueObject>(GeometryUVToJson(Snapshot.UVs[VertexIndex])));
			}
			SectionObj->SetArrayField(TEXT("uvs"), UVs);
		}

		if (bWantTriangles)
		{
			int32 FirstIndex = 0;
			int32 IndexCount = 0;
			TArray<TSharedPtr<FJsonValue>> Triangles;
			if (GeometrySectionTriangleRange(Snapshot, Section, FirstIndex, IndexCount))
			{
				Triangles.Reserve(IndexCount);
				for (int32 Offset = 0; Offset < IndexCount; ++Offset)
				{
					// Section-local, so it indexes this section's own positions
					// array directly. baseVertexIndex maps it back to LOD-global.
					const int64 Local = static_cast<int64>(Snapshot.Indices[FirstIndex + Offset]) - SectionFirstVertex;
					Triangles.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Local)));
				}
			}
			SectionObj->SetArrayField(TEXT("triangles"), Triangles);
			SectionObj->SetBoolField(TEXT("trianglesAreSectionLocal"), true);
		}

		TotalEmittedTriangles += Section.NumTriangles;
		SectionObjects.Add(MakeShared<FJsonValueObject>(SectionObj));
	}

	auto BuildResult = [&]() -> TSharedPtr<FJsonObject>
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("assetType"), Snapshot.AssetType);
		Result->SetNumberField(TEXT("lodIndex"), LodIndex);
		Result->SetNumberField(TEXT("lodCount"), Snapshot.LodCount);
		Result->SetNumberField(TEXT("uvChannel"), UVChannel);
		Result->SetNumberField(TEXT("uvChannelCount"), Snapshot.NumTexCoords);
		Result->SetNumberField(TEXT("vertexCount"), Snapshot.Positions.Num());
		Result->SetNumberField(TEXT("triangleCount"), Snapshot.Indices.Num() / 3);
		Result->SetNumberField(TEXT("sectionCount"), Snapshot.Sections.Num());
		Result->SetNumberField(TEXT("emittedVertexCount"), static_cast<double>(EmittedVertices));
		Result->SetNumberField(TEXT("emittedTriangleCount"), static_cast<double>(TotalEmittedTriangles));
		Result->SetArrayField(TEXT("sections"), SectionObjects);
		return Result;
	};

	if (bDumpToFile)
	{
		const TSharedPtr<FJsonObject> DumpResult = BuildResult();
		FString ResolvedDumpPath;
		FString DumpError;
		if (!WriteGeometryJsonToFile(DumpResult, OutputPath, AssetPath, LodIndex, ResolvedDumpPath, DumpError))
		{
			return MCPError(DumpError);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("assetType"), Snapshot.AssetType);
		Result->SetNumberField(TEXT("lodIndex"), LodIndex);
		Result->SetNumberField(TEXT("lodCount"), Snapshot.LodCount);
		Result->SetBoolField(TEXT("dumpedToFile"), true);
		Result->SetStringField(TEXT("outputPath"), ResolvedDumpPath);
		Result->SetNumberField(TEXT("vertexCount"), Snapshot.Positions.Num());
		Result->SetNumberField(TEXT("triangleCount"), Snapshot.Indices.Num() / 3);
		Result->SetNumberField(TEXT("sectionCount"), Snapshot.Sections.Num());
		Result->SetNumberField(TEXT("emittedVertexCount"), static_cast<double>(EmittedVertices));
		Result->SetNumberField(TEXT("emittedTriangleCount"), static_cast<double>(TotalEmittedTriangles));
		return MCPResult(Result);
	}

	return MCPResult(BuildResult());
}


TSharedPtr<FJsonValue> FAssetGeometryHandlers::MeasureMeshGeometry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const int32 LodIndex = OptionalInt(Params, TEXT("lodIndex"), 0);
	const bool bHasSectionIndex = HasParam(Params, TEXT("sectionIndex"));
	const int32 RequestedSection = OptionalInt(Params, TEXT("sectionIndex"), 0);

	FGeometrySnapshot Snapshot;
	if (auto Err = BuildGeometrySnapshot(AssetPath, LodIndex, /*UVChannel=*/0, /*bWantNormals=*/false, /*bWantUVs=*/false, Snapshot))
	{
		return Err;
	}

	TArray<int32> SectionsToMeasure;
	if (bHasSectionIndex)
	{
		if (!Snapshot.Sections.IsValidIndex(RequestedSection))
		{
			return MCPError(FString::Printf(
				TEXT("sectionIndex %d out of range (LOD %d has %d sections)"),
				RequestedSection, LodIndex, Snapshot.Sections.Num()));
		}
		SectionsToMeasure.Add(RequestedSection);
	}
	else
	{
		for (int32 Index = 0; Index < Snapshot.Sections.Num(); ++Index)
		{
			SectionsToMeasure.Add(Index);
		}
	}

	// Gather the triangles under measurement once, then every metric reads the
	// same set. Sections can overlap vertex ranges, so triangles, not vertices,
	// are the unit.
	TArray<int32> TriangleStarts;
	for (int32 SectionIndex : SectionsToMeasure)
	{
		int32 FirstIndex = 0;
		int32 IndexCount = 0;
		if (!GeometrySectionTriangleRange(Snapshot, Snapshot.Sections[SectionIndex], FirstIndex, IndexCount))
		{
			continue;
		}
		for (int32 Offset = 0; Offset + 2 < IndexCount; Offset += 3)
		{
			TriangleStarts.Add(FirstIndex + Offset);
		}
	}

	// #938: get_mesh_volume_area() in the engine returns (SurfaceArea, Volume) in
	// that order, which is the reverse of what its name says, and the two are
	// plausible numbers for each other so a swap is invisible. Both come back
	// here as named fields for exactly that reason: there is no positional pair
	// to get the wrong way round.
	double SurfaceArea = 0.0;
	double SignedVolume = 0.0;
	FBox LocalBox(ForceInit);
	TSet<int32> TouchedVertices;

	for (int32 TriangleStart : TriangleStarts)
	{
		const uint32 I0 = Snapshot.Indices[TriangleStart];
		const uint32 I1 = Snapshot.Indices[TriangleStart + 1];
		const uint32 I2 = Snapshot.Indices[TriangleStart + 2];
		if (!Snapshot.Positions.IsValidIndex(static_cast<int32>(I0)) ||
			!Snapshot.Positions.IsValidIndex(static_cast<int32>(I1)) ||
			!Snapshot.Positions.IsValidIndex(static_cast<int32>(I2)))
		{
			continue;
		}

		const FVector V0(Snapshot.Positions[static_cast<int32>(I0)]);
		const FVector V1(Snapshot.Positions[static_cast<int32>(I1)]);
		const FVector V2(Snapshot.Positions[static_cast<int32>(I2)]);

		SurfaceArea += 0.5 * FVector::CrossProduct(V1 - V0, V2 - V0).Size();
		// Signed tetrahedron volume against the origin. Summed over a closed
		// surface this is the enclosed volume; over an open one it is meaningless,
		// which is why isClosed is reported next to it.
		SignedVolume += FVector::DotProduct(V0, FVector::CrossProduct(V1, V2)) / 6.0;

		LocalBox += V0;
		LocalBox += V1;
		LocalBox += V2;
		TouchedVertices.Add(static_cast<int32>(I0));
		TouchedVertices.Add(static_cast<int32>(I1));
		TouchedVertices.Add(static_cast<int32>(I2));
	}

	// Topology: weld by quantised position first, so a UV seam does not read as
	// an open boundary, then count how many triangles share each undirected edge.
	bool bTopologyAnalyzed = false;
	bool bIsClosed = false;
	bool bIsManifold = false;
	int32 BoundaryEdgeCount = 0;
	int32 NonManifoldEdgeCount = 0;
	FString TopologySkippedReason;

	if (TriangleStarts.Num() > GeometryTopologyTriangleCap)
	{
		TopologySkippedReason = FString::Printf(
			TEXT("%d triangles is over the %d triangle limit for edge-adjacency analysis"),
			TriangleStarts.Num(), GeometryTopologyTriangleCap);
	}
	else if (TriangleStarts.Num() > 0)
	{
		TMap<FGeometryWeldKey, int32> WeldedIds;
		WeldedIds.Reserve(Snapshot.Positions.Num());
		TArray<int32> VertexToWelded;
		VertexToWelded.Init(INDEX_NONE, Snapshot.Positions.Num());
		int32 NextWeldedId = 0;
		for (int32 VertexIndex = 0; VertexIndex < Snapshot.Positions.Num(); ++VertexIndex)
		{
			const FGeometryWeldKey Key = MakeGeometryWeldKey(Snapshot.Positions[VertexIndex]);
			if (const int32* Existing = WeldedIds.Find(Key))
			{
				VertexToWelded[VertexIndex] = *Existing;
			}
			else
			{
				WeldedIds.Add(Key, NextWeldedId);
				VertexToWelded[VertexIndex] = NextWeldedId;
				++NextWeldedId;
			}
		}

		TMap<uint64, int32> EdgeUseCount;
		EdgeUseCount.Reserve(TriangleStarts.Num() * 3);
		auto AddEdge = [&EdgeUseCount](int32 A, int32 B)
		{
			const uint32 Low = static_cast<uint32>(FMath::Min(A, B));
			const uint32 High = static_cast<uint32>(FMath::Max(A, B));
			const uint64 Key = (static_cast<uint64>(Low) << 32) | static_cast<uint64>(High);
			EdgeUseCount.FindOrAdd(Key)++;
		};

		for (int32 TriangleStart : TriangleStarts)
		{
			const int32 I0 = static_cast<int32>(Snapshot.Indices[TriangleStart]);
			const int32 I1 = static_cast<int32>(Snapshot.Indices[TriangleStart + 1]);
			const int32 I2 = static_cast<int32>(Snapshot.Indices[TriangleStart + 2]);
			if (!VertexToWelded.IsValidIndex(I0) || !VertexToWelded.IsValidIndex(I1) || !VertexToWelded.IsValidIndex(I2))
			{
				continue;
			}
			const int32 W0 = VertexToWelded[I0];
			const int32 W1 = VertexToWelded[I1];
			const int32 W2 = VertexToWelded[I2];
			// A degenerate triangle contributes no real edges.
			if (W0 == W1 || W1 == W2 || W0 == W2) continue;
			AddEdge(W0, W1);
			AddEdge(W1, W2);
			AddEdge(W2, W0);
		}

		for (const TPair<uint64, int32>& Edge : EdgeUseCount)
		{
			if (Edge.Value == 1) ++BoundaryEdgeCount;
			else if (Edge.Value > 2) ++NonManifoldEdgeCount;
		}

		bTopologyAnalyzed = true;
		bIsManifold = NonManifoldEdgeCount == 0;
		bIsClosed = bIsManifold && BoundaryEdgeCount == 0;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetType"), Snapshot.AssetType);
	Result->SetNumberField(TEXT("lodIndex"), LodIndex);
	Result->SetNumberField(TEXT("lodCount"), Snapshot.LodCount);
	Result->SetNumberField(TEXT("sectionCount"), Snapshot.Sections.Num());
	if (bHasSectionIndex) Result->SetNumberField(TEXT("sectionIndex"), RequestedSection);

	Result->SetNumberField(TEXT("triangleCount"), TriangleStarts.Num());
	Result->SetNumberField(TEXT("vertexCount"), TouchedVertices.Num());
	Result->SetNumberField(TEXT("lodVertexCount"), Snapshot.Positions.Num());
	Result->SetNumberField(TEXT("lodTriangleCount"), Snapshot.Indices.Num() / 3);

	Result->SetNumberField(TEXT("surfaceArea"), SurfaceArea);
	Result->SetNumberField(TEXT("volume"), FMath::Abs(SignedVolume));
	Result->SetNumberField(TEXT("signedVolume"), SignedVolume);
	Result->SetBoolField(TEXT("volumeIsMeaningful"), bTopologyAnalyzed && bIsClosed);
	Result->SetStringField(TEXT("units"), TEXT("surfaceArea in cm2, volume in cm3, lengths in cm (Unreal units)"));

	TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
	const FVector BoxMin = LocalBox.IsValid ? LocalBox.Min : FVector::ZeroVector;
	const FVector BoxMax = LocalBox.IsValid ? LocalBox.Max : FVector::ZeroVector;
	BoundsObj->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(BoxMin));
	BoundsObj->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(BoxMax));
	BoundsObj->SetObjectField(TEXT("center"), MCPVec3ToJsonObject((BoxMin + BoxMax) * 0.5));
	BoundsObj->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject((BoxMax - BoxMin) * 0.5));
	Result->SetObjectField(TEXT("bounds"), BoundsObj);
	Result->SetObjectField(TEXT("dimensions"), MCPVec3ToJsonObject(BoxMax - BoxMin));

	// The asset's own cached bounds, which include every LOD and any extension
	// the artist set, kept separate from the measured box above so the two are
	// never confused for one another.
	TSharedPtr<FJsonObject> AssetBoundsObj = MakeShared<FJsonObject>();
	AssetBoundsObj->SetObjectField(TEXT("origin"), MCPVec3ToJsonObject(Snapshot.Bounds.Origin));
	AssetBoundsObj->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Snapshot.Bounds.BoxExtent));
	AssetBoundsObj->SetNumberField(TEXT("sphereRadius"), Snapshot.Bounds.SphereRadius);
	Result->SetObjectField(TEXT("assetBounds"), AssetBoundsObj);

	Result->SetBoolField(TEXT("topologyAnalyzed"), bTopologyAnalyzed);
	if (bTopologyAnalyzed)
	{
		Result->SetBoolField(TEXT("isClosed"), bIsClosed);
		Result->SetBoolField(TEXT("isManifold"), bIsManifold);
		Result->SetNumberField(TEXT("boundaryEdgeCount"), BoundaryEdgeCount);
		Result->SetNumberField(TEXT("nonManifoldEdgeCount"), NonManifoldEdgeCount);
		Result->SetNumberField(TEXT("weldToleranceCm"), GeometryWeldTolerance);
	}
	else if (!TopologySkippedReason.IsEmpty())
	{
		Result->SetStringField(TEXT("topologySkippedReason"), TopologySkippedReason);
	}

	return MCPResult(Result);
}
