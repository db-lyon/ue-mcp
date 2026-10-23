// A read capture, not a mesh conversion: no weld, compact, build, apply-build-settings or save.
#include "AssetHandlers_Geometry.h"
#include "HandlerUtils.h"
#include "HandlerEngineVersion.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "JsonObjectConverter.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_UNIX || PLATFORM_MAC
#include <unistd.h>
#endif

namespace MCPMeshInspection
{
constexpr int32 MaxElements = 1000000;
constexpr int32 MaxUVChannels = 8;
constexpr int32 MaxMetadataEntries = 4096;
constexpr int64 MaxSnapshotBytes = 128ll * 1024 * 1024;
constexpr int64 MaxCacheBytes = 256ll * 1024 * 1024;
constexpr int32 MaxSnapshots = 4;
constexpr double IdleSeconds = 300.0;
constexpr int64 MaxExportBytes = 1024ll * 1024 * 1024;
constexpr int32 ExportBufferBytes = 64 * 1024;
constexpr int32 MaxExportRecordChars = 16 * 1024;

struct FVertex
{
	int32 Id;
	FVector3f Position;
};

struct FCorner
{
	int32 Id;
	int32 VertexId;
	FVector3f Normal;
	FVector3f Tangent;
	float BinormalSign;
	FVector4f Color;
	FVector2f UVs[MaxUVChannels];
};

struct FTriangle
{
	int32 Id;
	int32 Corners[3];
	int32 GroupId; // Source polygon group or render section. Never conflated with material index.
	int32 PolygonId; // INDEX_NONE for render data.
};

struct FSnapshot
{
	FString Id;
	FString RequestedPath;
	FString AssetPath;
	FString Source;
	FString CapturedUtc;
	FString Revision;
	int32 LodIndex = 0;
	int32 UVChannels = 0;
	bool bHasColors = false;
	TArray<FVertex> Vertices;
	TArray<FCorner> Corners;
	TArray<FTriangle> Triangles;
	// Stored as text so response objects cannot mutate the cached metadata.
	FString MetadataJson;
	double LastAccess = 0;
	int64 Bytes() const
	{
		return Vertices.GetAllocatedSize() + Corners.GetAllocatedSize() + Triangles.GetAllocatedSize()
			+ MetadataJson.GetAllocatedSize() + RequestedPath.GetAllocatedSize() + AssetPath.GetAllocatedSize() + 4096;
	}
};

// Only the game-thread handler accesses this cache. No UObjects or render pointers survive a call.
TArray<TSharedPtr<FSnapshot>> Snapshots;

TSharedPtr<FJsonValue> Error(const TCHAR* Reason, const FString& Message)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("reason"), Reason);
	Result->SetStringField(TEXT("error"), Message);
	return MCPResult(Result);
}

bool FitsBudget(int64 Vertices, int64 Corners, int64 Triangles)
{
	return Vertices >= 0 && Corners >= 0 && Triangles >= 0
		&& Vertices <= MaxElements && Corners <= MaxElements && Triangles <= MaxElements
		&& Vertices * sizeof(FVertex) + Corners * sizeof(FCorner) + Triangles * sizeof(FTriangle)
			+ 1024 * 1024 <= MaxSnapshotBytes;
}

TSharedPtr<FJsonValue> BudgetError()
{
	return Error(TEXT("snapshot_limit"), TEXT("Geometry exceeds the 1000000 elements per table or 128 MiB snapshot limit. Select a smaller LOD. Nothing was truncated."));
}

template <typename T> bool FiniteVector(const T& V)
{
	return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}

bool FiniteCorner(const FCorner& C, int32 UVChannels)
{
	if (!FiniteVector(C.Normal) || !FiniteVector(C.Tangent) || !FiniteVector(C.Color)
		|| !FMath::IsFinite(C.Color.W) || !FMath::IsFinite(C.BinormalSign)) return false;
	for (int32 Channel = 0; Channel < UVChannels; ++Channel)
		if (!FMath::IsFinite(C.UVs[Channel].X) || !FMath::IsFinite(C.UVs[Channel].Y)) return false;
	return true;
}

TSharedPtr<FJsonValue> CaptureSource(const UStaticMesh* Mesh, FSnapshot& Out, TArray<TSharedPtr<FJsonValue>>& Groups)
{
	if (Out.LodIndex < 0 || Out.LodIndex >= Mesh->GetNumSourceModels())
		return Error(TEXT("lod_out_of_range"), TEXT("lodIndex is outside the source-model array."));
	// The engine lazily loads/caches the description. This does not create, commit or dirty it.
	const FMeshDescription* Description = Mesh->GetMeshDescription(Out.LodIndex);
	if (!Description)
		return Error(TEXT("source_unavailable"), TEXT("This LOD has no source MeshDescription. Generated/reduced LODs may have only render data; no fallback was substituted."));
	if (!FitsBudget(Description->Vertices().Num(), Description->VertexInstances().Num(), Description->Triangles().Num())
		|| Description->PolygonGroups().Num() > MaxMetadataEntries) return BudgetError();

	const FStaticMeshConstAttributes Attributes(*Description);
	const auto Positions = Attributes.GetVertexPositions();
	const auto Normals = Attributes.GetVertexInstanceNormals();
	const auto Tangents = Attributes.GetVertexInstanceTangents();
	const auto Signs = Attributes.GetVertexInstanceBinormalSigns();
	const auto Colors = Attributes.GetVertexInstanceColors();
	const auto UVs = Attributes.GetVertexInstanceUVs();
	const auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	if (!Positions.IsValid() || !Normals.IsValid() || !Tangents.IsValid() || !Signs.IsValid()
		|| !Colors.IsValid() || !UVs.IsValid() || !SlotNames.IsValid())
		return Error(TEXT("attributes_unavailable"), TEXT("Source MeshDescription is missing a standard static-mesh attribute. No attributes were synthesized."));
	Out.UVChannels = UVs.GetNumChannels();
	if (Out.UVChannels > MaxUVChannels)
		return Error(TEXT("uv_limit"), TEXT("More than 8 source UV channels are unsupported. No channels were truncated."));
	Out.bHasColors = true;
	Out.Vertices.Reserve(Description->Vertices().Num());
	Out.Corners.Reserve(Description->VertexInstances().Num());
	Out.Triangles.Reserve(Description->Triangles().Num());
	for (FVertexID Id : Description->Vertices().GetElementIDs())
	{
		const FVector3f Position = Positions[Id];
		if (!FiniteVector(Position)) return Error(TEXT("non_finite_geometry"), TEXT("A source position is not finite."));
		Out.Vertices.Add({ Id.GetValue(), Position });
	}
	for (FVertexInstanceID Id : Description->VertexInstances().GetElementIDs())
	{
		const FVertexID Vertex = Description->GetVertexInstanceVertex(Id);
		if (!Description->IsVertexValid(Vertex)) return Error(TEXT("invalid_topology"), TEXT("A source corner references an invalid vertex."));
		FCorner Corner{};
		Corner.Id = Id.GetValue();
		Corner.VertexId = Vertex.GetValue();
		Corner.Normal = Normals[Id];
		Corner.Tangent = Tangents[Id];
		Corner.BinormalSign = Signs[Id];
		Corner.Color = Colors[Id];
		for (int32 Channel = 0; Channel < Out.UVChannels; ++Channel) Corner.UVs[Channel] = UVs.Get(Id, Channel);
		if (!FiniteCorner(Corner, Out.UVChannels)) return Error(TEXT("non_finite_geometry"), TEXT("A source corner attribute is not finite."));
		Out.Corners.Add(Corner);
	}
	for (FTriangleID Id : Description->Triangles().GetElementIDs())
	{
		const auto Corners = Description->GetTriangleVertexInstances(Id);
		const FPolygonGroupID Group = Description->GetTrianglePolygonGroup(Id);
		if (Corners.Num() != 3 || !Description->IsPolygonGroupValid(Group))
			return Error(TEXT("invalid_topology"), TEXT("A source triangle has invalid corners or polygon group."));
		FTriangle Triangle{};
		Triangle.Id = Id.GetValue();
		Triangle.GroupId = Group.GetValue();
		Triangle.PolygonId = Description->GetTrianglePolygon(Id).GetValue();
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!Description->IsVertexInstanceValid(Corners[Index])) return Error(TEXT("invalid_topology"), TEXT("A source triangle references an invalid corner."));
			Triangle.Corners[Index] = Corners[Index].GetValue();
		}
		Out.Triangles.Add(Triangle);
	}
	for (FPolygonGroupID Id : Description->PolygonGroups().GetElementIDs())
	{
		auto Group = MakeShared<FJsonObject>();
		Group->SetNumberField(TEXT("id"), Id.GetValue());
		Group->SetStringField(TEXT("importedMaterialSlotName"), SlotNames[Id].ToString());
		// A polygon-group ID is not a material index. Preserve ambiguity instead of guessing.
		TArray<TSharedPtr<FJsonValue>> Matches;
		const auto& Materials = Mesh->GetStaticMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
			if (Materials[Index].ImportedMaterialSlotName == SlotNames[Id])
			{
				// Bound ambiguous mappings too: empty/duplicate slot names can otherwise
				// multiply the metadata by group count times material count.
				if (Matches.Num() >= 16) return Error(TEXT("metadata_limit"), TEXT("A source polygon group matches more than 16 material slots; the ambiguous mapping exceeds the metadata limit."));
				Matches.Add(MakeShared<FJsonValueNumber>(Index));
			}
		Group->SetArrayField(TEXT("materialSlotMatches"), Matches);
		Groups.Add(MakeShared<FJsonValueObject>(Group));
	}
	return nullptr;
}

TSharedPtr<FJsonValue> CaptureRender(const UStaticMesh* Mesh, FSnapshot& Out, TArray<TSharedPtr<FJsonValue>>& Groups)
{
	const FStaticMeshRenderData* Data = Mesh->GetRenderData();
	if (!Data) return Error(TEXT("render_unavailable"), TEXT("The mesh has no built render data."));
	if (!Data->LODResources.IsValidIndex(Out.LodIndex)) return Error(TEXT("lod_out_of_range"), TEXT("lodIndex is outside the render-LOD array."));
	const FStaticMeshLODResources& LOD = Data->LODResources[Out.LodIndex];
	const auto& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const auto& Attributes = LOD.VertexBuffers.StaticMeshVertexBuffer;
	const auto& Colors = LOD.VertexBuffers.ColorVertexBuffer;
	const uint32 Count = Positions.GetNumVertices();
	const int32 IndexCount = LOD.IndexBuffer.GetNumIndices();
	if (IndexCount % 3 != 0) return Error(TEXT("invalid_topology"), TEXT("The render index buffer is not a triangle list."));
	if (!FitsBudget(Count, Count, IndexCount / 3) || LOD.Sections.Num() > MaxMetadataEntries) return BudgetError();
	Out.UVChannels = Attributes.GetNumTexCoords();
	if (Out.UVChannels > MaxUVChannels) return Error(TEXT("uv_limit"), TEXT("More than 8 render UV channels are unsupported. No channels were truncated."));
	Out.bHasColors = Colors.GetNumVertices() != 0;
	if ((Count && (!Positions.GetVertexData() || Attributes.GetNumVertices() != Count || !Attributes.GetTangentData()
		|| (Out.UVChannels && !Attributes.GetTexCoordData())))
		|| (Out.bHasColors && (Colors.GetNumVertices() != Count || !Colors.GetVertexData()))
		|| (IndexCount && LOD.IndexBuffer.GetIndexDataSize() == 0))
		return Error(TEXT("cpu_data_unavailable"), TEXT("A required render CPU buffer is absent or incomplete. No attributes were omitted or reconstructed."));

	Out.Vertices.Reserve(Count);
	Out.Corners.Reserve(Count);
	Out.Triangles.Reserve(IndexCount / 3);
	for (uint32 Id = 0; Id < Count; ++Id)
	{
		const FVector3f Position = Positions.VertexPosition(Id);
		if (!FiniteVector(Position)) return Error(TEXT("non_finite_geometry"), TEXT("A render position is not finite."));
		Out.Vertices.Add({ static_cast<int32>(Id), Position });
		FCorner Corner{};
		Corner.Id = Corner.VertexId = static_cast<int32>(Id);
		const FVector4f Z = Attributes.VertexTangentZ(Id);
		const FVector4f X = Attributes.VertexTangentX(Id);
		Corner.Normal = FVector3f(Z.X, Z.Y, Z.Z);
		Corner.Tangent = FVector3f(X.X, X.Y, X.Z);
		Corner.BinormalSign = Z.W;
		Corner.Color = FVector4f(0, 0, 0, 0);
		if (Out.bHasColors)
		{
			const FColor Color = Colors.VertexColor(Id);
			// Preserve the stored bytes, normalized without an sRGB transfer function.
			Corner.Color = FVector4f(Color.R / 255.0f, Color.G / 255.0f, Color.B / 255.0f, Color.A / 255.0f);
		}
		for (int32 Channel = 0; Channel < Out.UVChannels; ++Channel) Corner.UVs[Channel] = Attributes.GetVertexUV(Id, Channel);
		if (!FiniteCorner(Corner, Out.UVChannels)) return Error(TEXT("non_finite_geometry"), TEXT("A render corner attribute is not finite."));
		Out.Corners.Add(Corner);
	}
	// View rather than copy: it is consumed synchronously before returning to the editor.
	const FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
	for (int32 Index = 0; Index < IndexCount; Index += 3)
	{
		FTriangle Triangle{};
		Triangle.Id = Index / 3;
		Triangle.GroupId = Triangle.PolygonId = INDEX_NONE;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (Indices[Index + Corner] >= Count) return Error(TEXT("invalid_topology"), TEXT("A render triangle references an invalid vertex."));
			Triangle.Corners[Corner] = static_cast<int32>(Indices[Index + Corner]);
		}
		Out.Triangles.Add(Triangle);
	}
	for (int32 Index = 0; Index < LOD.Sections.Num(); ++Index)
	{
		const FStaticMeshSection& Section = LOD.Sections[Index];
		const uint64 EndIndex = static_cast<uint64>(Section.FirstIndex) + static_cast<uint64>(Section.NumTriangles) * 3;
		if (Section.FirstIndex % 3 || EndIndex > static_cast<uint64>(IndexCount)) return Error(TEXT("invalid_topology"), TEXT("A render section has an invalid index range."));
		for (uint32 TriangleId = Section.FirstIndex / 3; TriangleId < EndIndex / 3; ++TriangleId)
		{
			if (Out.Triangles[TriangleId].GroupId != INDEX_NONE) return Error(TEXT("invalid_topology"), TEXT("Render sections overlap; material assignment would be ambiguous."));
			Out.Triangles[TriangleId].GroupId = Index;
		}
		auto Group = MakeShared<FJsonObject>();
		Group->SetNumberField(TEXT("id"), Index);
		Group->SetNumberField(TEXT("materialIndex"), Section.MaterialIndex);
		Group->SetNumberField(TEXT("firstIndex"), Section.FirstIndex);
		Group->SetNumberField(TEXT("triangleCount"), Section.NumTriangles);
		Group->SetBoolField(TEXT("enableCollision"), Section.bEnableCollision);
		Group->SetBoolField(TEXT("castShadow"), Section.bCastShadow);
		Groups.Add(MakeShared<FJsonValueObject>(Group));
	}
	return nullptr;
}

TSharedPtr<FJsonValue> Capture(const UStaticMesh* Mesh, FSnapshot& Out)
{
	if (Mesh->IsCompiling()) return Error(TEXT("mesh_compiling"), TEXT("The mesh is compiling. Retry after compilation finishes; this read will not force a build."));
	if (Mesh->GetStaticMaterials().Num() > MaxMetadataEntries) return BudgetError();
	TArray<TSharedPtr<FJsonValue>> Groups;
	if (auto Err = Out.Source == TEXT("sourceModel") ? CaptureSource(Mesh, Out, Groups) : CaptureRender(Mesh, Out, Groups)) return Err;
	Out.Vertices.Sort([](const FVertex& A, const FVertex& B) { return A.Id < B.Id; });
	Out.Corners.Sort([](const FCorner& A, const FCorner& B) { return A.Id < B.Id; });
	Out.Triangles.Sort([](const FTriangle& A, const FTriangle& B) { return A.Id < B.Id; });
	Out.AssetPath = Mesh->GetPathName();
	Out.CapturedUtc = FDateTime::UtcNow().ToIso8601();

	auto Metadata = MakeShared<FJsonObject>();
	Metadata->SetArrayField(TEXT("groups"), Groups);
	Metadata->SetStringField(TEXT("groupKind"), Out.Source == TEXT("sourceModel") ? TEXT("polygonGroup") : TEXT("renderSection"));
	Metadata->SetStringField(TEXT("representation"), Out.Source == TEXT("sourceModel") ? TEXT("authoredSourceModel")
		: Mesh->HasValidNaniteData() ? TEXT("naniteFallbackRenderLOD") : TEXT("renderLOD"));
	Metadata->SetStringField(TEXT("colorEncoding"), Out.Source == TEXT("sourceModel") ? TEXT("linearFloatRGBA") : TEXT("normalizedStoredRGBA8"));
	Metadata->SetBoolField(TEXT("hasColorAttribute"), Out.bHasColors);
	Metadata->SetBoolField(TEXT("naniteEnabled"), MCPGetNaniteSettings(Mesh).bEnabled);
	Metadata->SetBoolField(TEXT("hasNaniteData"), Mesh->HasValidNaniteData());
	Metadata->SetNumberField(TEXT("sourceModelCount"), Mesh->GetNumSourceModels());
	Metadata->SetNumberField(TEXT("lightMapCoordinateIndex"), Mesh->GetLightMapCoordinateIndex());
	Metadata->SetBoolField(TEXT("allowCPUAccess"), Mesh->bAllowCPUAccess);
	Metadata->SetStringField(TEXT("customComplexCollisionMesh"), Mesh->ComplexCollisionMesh ? Mesh->ComplexCollisionMesh->GetPathName() : TEXT(""));
	Metadata->SetNumberField(TEXT("lodForCollision"), Mesh->LODForCollision);
	auto Nanite = MakeShared<FJsonObject>();
	const FMeshNaniteSettings NaniteSettings = MCPGetNaniteSettings(Mesh);
	if (!FJsonObjectConverter::UStructToJsonObject(FMeshNaniteSettings::StaticStruct(), &NaniteSettings, Nanite.ToSharedRef(), 0, 0))
		return Error(TEXT("metadata_unavailable"), TEXT("Could not serialize Nanite settings."));
	Metadata->SetObjectField(TEXT("naniteSettings"), Nanite);
	if (Out.LodIndex < Mesh->GetNumSourceModels())
	{
		auto Build = MakeShared<FJsonObject>();
		if (!FJsonObjectConverter::UStructToJsonObject(FMeshBuildSettings::StaticStruct(), &Mesh->GetSourceModel(Out.LodIndex).BuildSettings, Build.ToSharedRef(), 0, 0))
			return Error(TEXT("metadata_unavailable"), TEXT("Could not serialize source build settings."));
		Metadata->SetObjectField(TEXT("sourceBuildSettings"), Build);
	}
	else Metadata->SetField(TEXT("sourceBuildSettings"), MakeShared<FJsonValueNull>());
	auto Collision = MakeShared<FJsonObject>();
	if (const UBodySetup* Body = Mesh->GetBodySetup())
	{
		Collision->SetNumberField(TEXT("collisionTraceFlag"), static_cast<int32>(Body->CollisionTraceFlag));
		Collision->SetNumberField(TEXT("effectiveCollisionTraceFlag"), static_cast<int32>(Body->GetCollisionTraceFlag()));
		Collision->SetBoolField(TEXT("doubleSidedGeometry"), Body->bDoubleSidedGeometry);
		Collision->SetNumberField(TEXT("simpleShapeCount"), Body->AggGeom.GetElementCount());
		Collision->SetStringField(TEXT("profileName"), Body->DefaultInstance.GetCollisionProfileName().ToString());
		Metadata->SetObjectField(TEXT("collision"), Collision);
	}
	else Metadata->SetField(TEXT("collision"), MakeShared<FJsonValueNull>());
	TArray<TSharedPtr<FJsonValue>> Materials;
	for (const FStaticMaterial& Material : Mesh->GetStaticMaterials())
	{
		auto Slot = MakeShared<FJsonObject>();
		Slot->SetNumberField(TEXT("index"), Materials.Num());
		Slot->SetStringField(TEXT("slotName"), Material.MaterialSlotName.ToString());
		Slot->SetStringField(TEXT("importedSlotName"), Material.ImportedMaterialSlotName.ToString());
		Slot->SetStringField(TEXT("materialPath"), Material.MaterialInterface ? Material.MaterialInterface->GetPathName() : TEXT(""));
		Materials.Add(MakeShared<FJsonValueObject>(Slot));
	}
	Metadata->SetArrayField(TEXT("materials"), Materials);
	if (!FJsonSerializer::Serialize(Metadata.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out.MetadataJson)))
		return Error(TEXT("metadata_unavailable"), TEXT("Could not serialize capture metadata."));
	if (Out.MetadataJson.Len() > 256 * 1024) return Error(TEXT("metadata_limit"), TEXT("Capture metadata exceeds 256 Ki characters. No metadata was truncated."));
	if (Out.Bytes() > MaxSnapshotBytes) return BudgetError();

	// Hash individual fields, never struct padding. This is a capture-content fingerprint,
	// not a package revision, security digest, or cross-engine asset identity.
	FSHA1 Hash;
	auto HashString = [&Hash](const FString& Text)
	{
		FTCHARToUTF8 UTF8(*Text);
		const int32 Length = UTF8.Length();
		Hash.Update(reinterpret_cast<const uint8*>(&Length), sizeof(Length));
		Hash.Update(reinterpret_cast<const uint8*>(UTF8.Get()), Length);
	};
	auto HashValue = [&Hash](const auto& Value) { Hash.Update(reinterpret_cast<const uint8*>(&Value), sizeof(Value)); };
	HashString(TEXT("static-mesh-inspection-v1"));
	HashString(Out.AssetPath); HashString(Out.Source); HashString(Out.MetadataJson);
	HashValue(Out.LodIndex); HashValue(Out.UVChannels);
	HashValue(Out.Vertices.Num()); HashValue(Out.Corners.Num()); HashValue(Out.Triangles.Num());
	for (const FVertex& V : Out.Vertices) { HashValue(V.Id); HashValue(V.Position); }
	for (const FCorner& C : Out.Corners)
	{
		HashValue(C.Id); HashValue(C.VertexId); HashValue(C.Normal); HashValue(C.Tangent); HashValue(C.BinormalSign); HashValue(C.Color);
		for (int32 Channel = 0; Channel < Out.UVChannels; ++Channel) HashValue(C.UVs[Channel]);
	}
	for (const FTriangle& T : Out.Triangles) { HashValue(T.Id); HashValue(T.Corners); HashValue(T.GroupId); HashValue(T.PolygonId); }
	Hash.Final();
	uint8 Digest[FSHA1::DigestSize];
	Hash.GetHash(Digest);
	Out.Revision = BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
	return nullptr;
}

void Expire(double Now)
{
	Snapshots.RemoveAll([Now](const TSharedPtr<FSnapshot>& S) { return Now - S->LastAccess >= IdleSeconds; });
}

bool ReadInteger(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, int32 Default, int32 Min, int32 Max, int32& Out)
{
	const auto Value = TryGetParam(Params, Key);
	Out = Default;
	if (!Value.IsValid()) return true;
	if (Value->Type != EJson::Number) return false;
	const double Number = Value->AsNumber();
	if (!FMath::IsFinite(Number) || Number < Min || Number > Max || Number != FMath::FloorToDouble(Number)) return false;
	Out = static_cast<int32>(Number);
	return true;
}

TSharedPtr<FJsonObject> Row(const FSnapshot& S, const FString& Kind, int32 Index)
{
	auto Result = MakeShared<FJsonObject>();
	if (Kind == TEXT("vertices"))
	{
		const FVertex& V = S.Vertices[Index];
		Result->SetNumberField(TEXT("id"), V.Id);
		Result->SetObjectField(TEXT("position"), MCPVec3ToJsonObject(FVector(V.Position)));
	}
	else if (Kind == TEXT("corners"))
	{
		const FCorner& C = S.Corners[Index];
		Result->SetNumberField(TEXT("id"), C.Id);
		Result->SetNumberField(TEXT("vertexId"), C.VertexId);
		Result->SetObjectField(TEXT("normal"), MCPVec3ToJsonObject(FVector(C.Normal)));
		Result->SetObjectField(TEXT("tangent"), MCPVec3ToJsonObject(FVector(C.Tangent)));
		Result->SetNumberField(TEXT("binormalSign"), C.BinormalSign);
		if (S.bHasColors)
		{
			auto Color = MakeShared<FJsonObject>();
			Color->SetNumberField(TEXT("r"), C.Color.X); Color->SetNumberField(TEXT("g"), C.Color.Y);
			Color->SetNumberField(TEXT("b"), C.Color.Z); Color->SetNumberField(TEXT("a"), C.Color.W);
			Result->SetObjectField(TEXT("color"), Color);
		}
		else Result->SetField(TEXT("color"), MakeShared<FJsonValueNull>());
		TArray<TSharedPtr<FJsonValue>> UVs;
		for (int32 Channel = 0; Channel < S.UVChannels; ++Channel)
		{
			auto UV = MakeShared<FJsonObject>();
			UV->SetNumberField(TEXT("u"), C.UVs[Channel].X); UV->SetNumberField(TEXT("v"), C.UVs[Channel].Y);
			UVs.Add(MakeShared<FJsonValueObject>(UV));
		}
		Result->SetArrayField(TEXT("uvs"), UVs);
	}
	else
	{
		const FTriangle& T = S.Triangles[Index];
		Result->SetNumberField(TEXT("id"), T.Id);
		Result->SetNumberField(TEXT("groupId"), T.GroupId);
		Result->SetNumberField(TEXT("polygonId"), T.PolygonId);
		TArray<TSharedPtr<FJsonValue>> Corners;
		for (int32 Corner : T.Corners) Corners.Add(MakeShared<FJsonValueNumber>(Corner));
		Result->SetArrayField(TEXT("cornerIds"), Corners);
	}
	return Result;
}

TSharedPtr<FJsonObject> Summary(const FSnapshot& S, bool bFresh)
{
	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("schemaVersion"), 1);
	Result->SetStringField(TEXT("snapshotId"), S.Id);
	Result->SetStringField(TEXT("contentRevision"), S.Revision);
	Result->SetStringField(TEXT("assetPath"), S.AssetPath);
	Result->SetStringField(TEXT("geometrySource"), S.Source);
	Result->SetNumberField(TEXT("lodIndex"), S.LodIndex);
	Result->SetStringField(TEXT("capturedUtc"), S.CapturedUtc);
	Result->SetBoolField(TEXT("immutableSnapshot"), true);
	Result->SetBoolField(TEXT("currentAssetStateChecked"), bFresh);
	Result->SetNumberField(TEXT("expiresAfterIdleSeconds"), IdleSeconds);
	Result->SetNumberField(TEXT("snapshotBytes"), S.Bytes());
	Result->SetNumberField(TEXT("vertexCount"), S.Vertices.Num());
	Result->SetNumberField(TEXT("cornerCount"), S.Corners.Num());
	Result->SetNumberField(TEXT("triangleCount"), S.Triangles.Num());
	Result->SetNumberField(TEXT("uvChannelCount"), S.UVChannels);
	Result->SetStringField(TEXT("coordinateSpace"), TEXT("assetLocalCentimeters"));
	return Result;
}

TSharedPtr<FJsonValue> ResolveExportPath(const FString& Requested, const FString& Id, FString& Out)
{
	const FString Path = Requested.IsEmpty() ? FString::Printf(TEXT("UE_MCP/MeshGeometry/%s.json"), *Id) : Requested;
	Out = FPaths::ConvertRelativePathToFull(MCPResolveDumpPath(Path));
	FPaths::NormalizeFilename(Out);
	FText PathError;
	if (Out.Len() > 4096 || !FPaths::CollapseRelativeDirectories(Out) || !FPaths::ValidatePath(Out, &PathError)
		|| !FPaths::GetExtension(Out).Equals(TEXT("json"), ESearchCase::IgnoreCase))
		return Error(TEXT("invalid_output_path"), TEXT("outputPath must be a valid .json file path of at most 4096 characters."));
#if PLATFORM_WINDOWS
	// Reject alternate data streams. The only allowed colon belongs to an absolute drive prefix.
	if (Out.Mid(2).Contains(TEXT(":"))) return Error(TEXT("invalid_output_path"), TEXT("outputPath cannot address a Windows alternate data stream."));
#endif
	if (FPaths::IsRelative(Path) && !FPaths::IsUnderDirectory(Out, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())))
		return Error(TEXT("invalid_output_path"), TEXT("Relative outputPath must remain under Saved/. Use an explicit absolute path for another destination."));
	if (IFileManager::Get().FileExists(*Out) || IFileManager::Get().DirectoryExists(*Out))
		return Error(TEXT("output_exists"), TEXT("The export destination already exists. Choose a new outputPath; existing files are never replaced."));
	return nullptr;
}

// Buffered UTF-8 writes keep file I/O bounded and avoid a system call per vertex.
// At most one <=16 Ki-character row and one <=256 Ki-character metadata conversion
// are live; no full-file JSON string or second geometry copy is constructed.
struct FExportStream
{
	IFileHandle& Handle;
	TArray<uint8> Buffer;
	int32 Used = 0;
	int64 Bytes = 0;
	int64 Limit;
	FString Failure;

	FExportStream(IFileHandle& InHandle, int64 InLimit) : Handle(InHandle), Limit(InLimit) { Buffer.SetNumUninitialized(ExportBufferBytes); }
	bool FlushBuffer()
	{
		if (Used && !Handle.Write(Buffer.GetData(), Used)) { Failure = TEXT("export_write_failed"); return false; }
		Used = 0;
		return true;
	}
	bool Write(const FString& Text)
	{
		if (!Failure.IsEmpty()) return false;
		FTCHARToUTF8 UTF8(*Text);
		if (UTF8.Length() > Limit - Bytes) { Failure = TEXT("export_limit"); return false; }
		Bytes += UTF8.Length();
		int32 Offset = 0;
		while (Offset < UTF8.Length())
		{
			const int32 Count = FMath::Min(UTF8.Length() - Offset, ExportBufferBytes - Used);
			FMemory::Memcpy(Buffer.GetData() + Used, UTF8.Get() + Offset, Count);
			Used += Count; Offset += Count;
			if (Used == ExportBufferBytes && !FlushBuffer()) return false;
		}
		return true;
	}
};

bool PublishExportNoReplace(const FString& Stage, const FString& Output)
{
#if PLATFORM_WINDOWS
	// Same-directory MoveFileW is atomic and refuses an existing destination.
	return IFileManager::Get().Move(*Output, *Stage, false, false, false, true);
#elif PLATFORM_UNIX || PLATFORM_MAC
	// UE's POSIX MoveFile uses rename(), which can replace a destination even
	// with IFileManager::Move(Replace=false). link() atomically creates a new name
	// for the fully written sibling inode and fails if ANY entry already exists.
	return ::link(TCHAR_TO_UTF8(*Stage), TCHAR_TO_UTF8(*Output)) == 0;
#else
	return false;
#endif
}

TSharedPtr<FJsonValue> ExportSnapshot(const FSnapshot& S, const FString& Output, bool bFresh, int64 ByteLimit = MaxExportBytes)
{
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (Files.FileExists(*Output) || Files.DirectoryExists(*Output)) return Error(TEXT("output_exists"), TEXT("Export destination exists; nothing was replaced."));
	if (!Files.CreateDirectoryTree(*FPaths::GetPath(Output))) return Error(TEXT("export_open_failed"), TEXT("Could not create the export directory."));
	const FString Stage = Output + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".partial");
	if (Files.FileExists(*Stage) || Files.DirectoryExists(*Stage)) return Error(TEXT("export_open_failed"), TEXT("Unique export staging path is already occupied."));
	TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*Stage));
	if (!Handle) return Error(TEXT("export_open_failed"), TEXT("Could not create the export staging file."));
	auto Fail = [&](const FString& Reason) -> TSharedPtr<FJsonValue>
	{
		Handle.Reset();
		auto Failure = Error(*Reason, TEXT("The snapshot export did not complete. The destination was not published by this call."));
		if (!Files.DeleteFile(*Stage)) Failure->AsObject()->SetStringField(TEXT("partialOutputPath"), Stage);
		return Failure;
	};
	FExportStream Stream(*Handle, ByteLimit);
	const double Started = FPlatformTime::Seconds();
	auto Header = Summary(S, bFresh);
	const FString ExportedUtc = FDateTime::UtcNow().ToIso8601();
	Header->SetStringField(TEXT("exportedUtc"), ExportedUtc);
	FString Text;
	if (!FJsonSerializer::Serialize(Header.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))
		|| Text.Len() > MaxExportRecordChars || !Text.EndsWith(TEXT("}"))) return Fail(TEXT("export_serialization_failed"));
	Text.LeftChopInline(1);
	if (!Stream.Write(Text) || !Stream.Write(TEXT(",\"metadata\":")) || !Stream.Write(S.MetadataJson)) return Fail(Stream.Failure);
	for (const TCHAR* Kind : { TEXT("vertices"), TEXT("corners"), TEXT("triangles") })
	{
		if (!Stream.Write(FString::Printf(TEXT(",\"%s\":["), Kind))) return Fail(Stream.Failure);
		const FString Table(Kind);
		const int32 Count = Table == TEXT("vertices") ? S.Vertices.Num() : Table == TEXT("corners") ? S.Corners.Num() : S.Triangles.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if ((Index % 512) == 0 && FPlatformTime::Seconds() - Started > 240.0) return Fail(TEXT("export_timeout"));
			Text.Reset();
			if (!FJsonSerializer::Serialize(Row(S, Table, Index).ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))
				|| Text.Len() > MaxExportRecordChars) return Fail(TEXT("export_serialization_failed"));
			if ((Index && !Stream.Write(TEXT(","))) || !Stream.Write(Text)) return Fail(Stream.Failure);
		}
		if (!Stream.Write(TEXT("]"))) return Fail(Stream.Failure);
	}
	if (!Stream.Write(TEXT(",\"exportComplete\":true}")) || !Stream.FlushBuffer()) return Fail(Stream.Failure);
	if (!Handle->Flush(true)) return Fail(TEXT("export_flush_failed"));
	Handle.Reset();
	if (!PublishExportNoReplace(Stage, Output))
		return Fail(Files.FileExists(*Output) || Files.DirectoryExists(*Output) ? TEXT("output_exists") : TEXT("export_publish_failed"));
	auto Receipt = Summary(S, bFresh);
	Receipt->SetBoolField(TEXT("dumpedToFile"), true);
	Receipt->SetBoolField(TEXT("created"), true);
	Receipt->SetBoolField(TEXT("exportComplete"), true);
	Receipt->SetStringField(TEXT("exportedUtc"), ExportedUtc);
	Receipt->SetStringField(TEXT("outputPath"), Output);
	Receipt->SetNumberField(TEXT("bytes"), Stream.Bytes);
	if (Files.FileExists(*Stage) && !Files.DeleteFile(*Stage)) Receipt->SetStringField(TEXT("stagingCleanupWarning"), Stage);
	return MCPResult(Receipt);
}
} // namespace MCPMeshInspection

TSharedPtr<FJsonValue> FAssetGeometryHandlers::GetStaticMeshGeometry(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPMeshInspection;
	FString AssetPath, Source;
	// Read every declared parameter before validation (the bridge audits this contract).
	const auto PathError = RequireString(Params, TEXT("assetPath"), AssetPath);
	const auto SourceError = RequireString(Params, TEXT("geometrySource"), Source);
	int32 LodIndex, Offset, PageSize;
	const bool bValidLOD = ReadInteger(Params, TEXT("lodIndex"), 0, 0, MAX_int32, LodIndex);
	const FString Kind = OptionalString(Params, TEXT("elementType"), TEXT("triangles"));
	const bool bValidOffset = ReadInteger(Params, TEXT("elementOffset"), 0, 0, MaxElements, Offset);
	const bool bValidSize = ReadInteger(Params, TEXT("pageSize"), 256, 1, 512, PageSize);
	const FString SnapshotId = OptionalString(Params, TEXT("snapshotId"), TEXT(""));
	const auto SnapshotValue = TryGetParam(Params, TEXT("snapshotId"));
	const auto KindValue = TryGetParam(Params, TEXT("elementType"));
	const bool bDump = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));
	const auto DumpValue = TryGetParam(Params, TEXT("dumpToFile"));
	const auto OutputValue = TryGetParam(Params, TEXT("outputPath"));
	if (PathError) return PathError;
	if (SourceError) return SourceError;
	if (Source != TEXT("sourceModel") && Source != TEXT("renderData")) return Error(TEXT("invalid_source"), TEXT("geometrySource must be sourceModel or renderData."));
	if ((KindValue && KindValue->Type != EJson::String) || (Kind != TEXT("vertices") && Kind != TEXT("corners") && Kind != TEXT("triangles"))) return Error(TEXT("invalid_element_type"), TEXT("elementType must be vertices, corners or triangles."));
	if (SnapshotValue && (SnapshotValue->Type != EJson::String || SnapshotId.IsEmpty() || SnapshotId.Len() > 64))
		return Error(TEXT("invalid_snapshot"), TEXT("snapshotId must be a nonempty ID returned by this action."));
	if (!bValidLOD || !bValidOffset || !bValidSize)
		return Error(TEXT("invalid_page"), TEXT("Use lodIndex >= 0, elementOffset 0..1000000 and pageSize 1..512."));
	if ((DumpValue && DumpValue->Type != EJson::Boolean) || (OutputValue && OutputValue->Type != EJson::String))
		return Error(TEXT("invalid_export"), TEXT("dumpToFile must be a boolean and outputPath must be a string."));
	if ((!bDump && OutputValue) || (bDump && (HasParam(Params, TEXT("elementType")) || HasParam(Params, TEXT("elementOffset")) || HasParam(Params, TEXT("pageSize")))))
		return Error(TEXT("invalid_export"), TEXT("outputPath requires dumpToFile=true. A full export does not accept elementType, elementOffset or pageSize."));
	if (!IsInGameThread()) return Error(TEXT("wrong_thread"), TEXT("Static mesh snapshots must be read on the game thread."));
	const FString CaptureId = SnapshotId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : SnapshotId;
	FString ExportPath;
	if (bDump) if (auto Err = ResolveExportPath(OutputPath, CaptureId, ExportPath)) return Err;
	const double Now = FPlatformTime::Seconds();
	Expire(Now);
	TSharedPtr<FSnapshot> Snapshot;
	if (!SnapshotId.IsEmpty())
	{
		for (const auto& Candidate : Snapshots) if (Candidate->Id == SnapshotId) { Snapshot = Candidate; break; }
		if (!Snapshot) return Error(TEXT("snapshot_unavailable"), TEXT("Snapshot expired, was evicted, or belongs to a previous editor process. Restart the capture at offset 0 without snapshotId; do not combine old and new pages."));
		if ((AssetPath != Snapshot->RequestedPath && AssetPath != Snapshot->AssetPath) || Source != Snapshot->Source || LodIndex != Snapshot->LodIndex)
			return Error(TEXT("snapshot_mismatch"), TEXT("snapshotId belongs to a different assetPath, geometrySource or lodIndex."));
	}
	else
	{
		if (Offset != 0) return Error(TEXT("snapshot_required"), TEXT("Continuation requires snapshotId. Start a capture at elementOffset 0."));
		UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
		if (!Asset) return Error(TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset);
		if (!Mesh) return Error(TEXT("unsupported_asset"), TEXT("get_static_mesh_geometry requires a StaticMesh."));
		Snapshot = MakeShared<FSnapshot>();
		Snapshot->RequestedPath = AssetPath; Snapshot->Source = Source; Snapshot->LodIndex = LodIndex;
		if (auto Err = Capture(Mesh, *Snapshot)) return Err;
		Snapshot->Id = CaptureId;
		int64 CachedBytes = 0;
		for (const auto& Existing : Snapshots) CachedBytes += Existing->Bytes();
		while (Snapshots.Num() && (Snapshots.Num() >= MaxSnapshots || CachedBytes + Snapshot->Bytes() > MaxCacheBytes))
		{
			int32 Oldest = 0;
			for (int32 Index = 1; Index < Snapshots.Num(); ++Index) if (Snapshots[Index]->LastAccess < Snapshots[Oldest]->LastAccess) Oldest = Index;
			CachedBytes -= Snapshots[Oldest]->Bytes();
			Snapshots.RemoveAt(Oldest);
		}
		Snapshots.Add(Snapshot);
	}
	if (bDump)
	{
		auto Export = ExportSnapshot(*Snapshot, ExportPath, SnapshotId.IsEmpty());
		Snapshot->LastAccess = FPlatformTime::Seconds();
		// Even a failed export can be retried from this exact capture with a new path.
		Export->AsObject()->SetStringField(TEXT("snapshotId"), Snapshot->Id);
		Export->AsObject()->SetStringField(TEXT("contentRevision"), Snapshot->Revision);
		return Export;
	}
	const int32 Total = Kind == TEXT("vertices") ? Snapshot->Vertices.Num() : Kind == TEXT("corners") ? Snapshot->Corners.Num() : Snapshot->Triangles.Num();
	if (Offset > Total) return Error(TEXT("offset_out_of_range"), TEXT("elementOffset is past the end of the selected snapshot table."));
	Snapshot->LastAccess = FPlatformTime::Seconds();
	auto Result = Summary(*Snapshot, SnapshotId.IsEmpty());
	Result->SetStringField(TEXT("elementType"), Kind);
	Result->SetNumberField(TEXT("elementOffset"), Offset);
	Result->SetNumberField(TEXT("totalElements"), Total);
	const int32 End = FMath::Min(Total, Offset + PageSize);
	TArray<TSharedPtr<FJsonValue>> Elements;
	for (int32 Index = Offset; Index < End; ++Index) Elements.Add(MakeShared<FJsonValueObject>(Row(*Snapshot, Kind, Index)));
	Result->SetArrayField(TEXT("elements"), Elements);
	Result->SetBoolField(TEXT("hasMore"), End < Total);
	if (End < Total) Result->SetNumberField(TEXT("nextOffset"), End);
	else Result->SetField(TEXT("nextOffset"), MakeShared<FJsonValueNull>());
	TSharedPtr<FJsonObject> Metadata;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot->MetadataJson), Metadata) || !Metadata)
		return Error(TEXT("metadata_unavailable"), TEXT("Cached capture metadata could not be read. Start a new capture."));
	Result->SetObjectField(TEXT("metadata"), Metadata);
	return MCPResult(Result);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Tests/MCPScopedTestMount.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPMeshInspectionBoundsTest,
	"UE.MCP.Asset.StaticMeshInspection.MemoryAndExpiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPMeshInspectionBoundsTest::RunTest(const FString& Parameters)
{
	using namespace MCPMeshInspection;
	TestTrue(TEXT("small capture fits"), FitsBudget(4, 7, 2));
	TestFalse(TEXT("element count capped before allocation"), FitsBudget(MaxElements + 1ll, 0, 0));
	TestFalse(TEXT("byte budget independent of count cap"), FitsBudget(MaxElements, MaxElements, MaxElements));
	TestFalse(TEXT("negative counts rejected"), FitsBudget(-1, 0, 0));
	// Preserve the live cache even if the suite is run after a user capture.
	auto Previous = MoveTemp(Snapshots);
	auto S = MakeShared<FSnapshot>();
	S->LastAccess = 10;
	Snapshots.Add(S);
	Expire(10 + IdleSeconds - 1);
	TestEqual(TEXT("unexpired capture retained"), Snapshots.Num(), 1);
	Expire(10 + IdleSeconds);
	TestEqual(TEXT("idle deadline expires exactly"), Snapshots.Num(), 0);
	Snapshots = MoveTemp(Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPMeshInspectionExportFailureTest,
	"UE.MCP.Asset.StaticMeshInspection.ExportFailureAndNoClobber", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPMeshInspectionExportFailureTest::RunTest(const FString& Parameters)
{
	using namespace MCPMeshInspection;
	const FMCPScopedTestMount Mount{ TEXT("/UEMCPMeshExportFailure/"), TEXT("UEMCPMeshExportFailure") };
	FSnapshot S;
	S.Id = TEXT("bounded-fixture"); S.AssetPath = TEXT("/Temp/Fixture.Mesh"); S.Source = TEXT("sourceModel");
	S.MetadataJson = TEXT("{\"groups\":[],\"materials\":[]}");
	for (int32 Index = 0; Index < 3000; ++Index) S.Vertices.Add({ Index, FVector3f(Index, 0, 0) });
	const FString FailedPath = FPaths::Combine(Mount.ContentPath, TEXT("too-large.json"));
	// A deliberately small test limit reaches disk before refusing further bytes.
	const auto Failed = ExportSnapshot(S, FailedPath, false, ExportBufferBytes + 1024)->AsObject();
	TestFalse(TEXT("oversized export fails"), Failed->GetBoolField(TEXT("success")));
	TestEqual(TEXT("failure identifies byte bound"), Failed->GetStringField(TEXT("reason")), FString(TEXT("export_limit")));
	TestFalse(TEXT("partial capture never published"), IFileManager::Get().FileExists(*FailedPath));
	TArray<FString> Partials;
	IFileManager::Get().FindFiles(Partials, *FPaths::Combine(Mount.ContentPath, TEXT("*.partial")), true, false);
	TestEqual(TEXT("partial stage cleaned"), Partials.Num(), 0);

	const FString Stage = FPaths::Combine(Mount.ContentPath, TEXT("publish.partial"));
	const FString Destination = FPaths::Combine(Mount.ContentPath, TEXT("occupied.json"));
	TestTrue(TEXT("test stage written"), FFileHelper::SaveStringToFile(TEXT("complete-new-file"), *Stage));
	TestTrue(TEXT("test competing destination written"), FFileHelper::SaveStringToFile(TEXT("existing-file"), *Destination));
	// Calls the final primitive directly, simulating a destination created after preflight.
	TestFalse(TEXT("publication refuses racing destination"), PublishExportNoReplace(Stage, Destination));
	FString Existing;
	TestTrue(TEXT("existing destination readable"), FFileHelper::LoadFileToString(Existing, *Destination));
	TestEqual(TEXT("existing bytes unchanged"), Existing, FString(TEXT("existing-file")));
	TestTrue(TEXT("failed publication retains its stage for cleanup"), IFileManager::Get().FileExists(*Stage));
	const FString NewDestination = FPaths::Combine(Mount.ContentPath, TEXT("published.json"));
	TestTrue(TEXT("complete stage publishes once"), PublishExportNoReplace(Stage, NewDestination));
	TestTrue(TEXT("published file readable"), FFileHelper::LoadFileToString(Existing, *NewDestination));
	TestEqual(TEXT("published bytes exact"), Existing, FString(TEXT("complete-new-file")));
	FString Resolved;
	TestTrue(TEXT("relative traversal refused"), ResolveExportPath(TEXT("../outside.json"), S.Id, Resolved).IsValid());
	TestTrue(TEXT("wrong extension refused"), ResolveExportPath(TEXT("file.uasset"), S.Id, Resolved).IsValid());
	TestFalse(TEXT("safe relative path resolves without writing"), ResolveExportPath(TEXT("UE_MCP/unique-inspection-test.json"), S.Id, Resolved).IsValid());
	TestTrue(TEXT("relative path confined to Saved"), FPaths::IsUnderDirectory(Resolved, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));
	return true;
}
#endif
