// get_skinned_vertices (#1452): a placed skinned mesh component's vertices as
// posed, leader-pose followers included. A member of FSkeletalMeshHandlers in
// its own translation unit; registration stays in SkeletalMeshHandlers.cpp.
//
// Positions are skinned on the CPU from the component's render geometry with
// the matrices the renderer is handed (GetCurrentRefToLocalMatrices, which
// reads the leader's pose through the follower's bone map and zeroes hidden
// bones), after the component's active morph targets. Selection, paging and
// the dump are shared with read_skeletal_mesh_vertices.

#include "SkeletalMeshHandlers.h"
#include "HandlerUtils.h"
#include "SkeletalMeshVertexRead.h"

#include "Animation/MorphTarget.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

TSharedPtr<FJsonValue> FSkeletalMeshHandlers::GetSkinnedVertices(const TSharedPtr<FJsonObject>& Params)
{
	// Read before the target resolves (#1057).
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("world"), TEXT("pieInstance"),
		TEXT("lodIndex"), TEXT("space"), TEXT("boneNames"), TEXT("includeChildBones"), TEXT("minWeight"),
		TEXT("box"), TEXT("nearPoint"), TEXT("radius"), TEXT("include"), TEXT("startIndex"), TEXT("limit"),
		TEXT("dumpToFile"), TEXT("outputPath") });

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie")
			? TEXT("PIE not running (or no such pieInstance). See editor(list_pie_instances).")
			: TEXT("Editor world not available"));
	}
	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	USkinnedMeshComponent* Comp = ComponentName.IsEmpty()
		? Actor->FindComponentByClass<USkinnedMeshComponent>()
		: MCPFindComponentByName<USkinnedMeshComponent>(Actor, ComponentName);
	if (!Comp)
	{
		TArray<FString> Available;
		for (UActorComponent* Candidate : Actor->GetComponents())
		{
			if (Cast<USkinnedMeshComponent>(Candidate)) Available.Add(Candidate->GetName());
		}
		const FString Named = ComponentName.IsEmpty() ? FString() : FString::Printf(TEXT(" named '%s'"), *ComponentName);
		return MCPError(FString::Printf(
			TEXT("No skinned mesh component%s on '%s'. Skinned mesh components: [%s]"),
			*Named, *Actor->GetActorLabel(), *FString::Join(Available, TEXT(", "))));
	}

	USkinnedAsset* Asset = Comp->GetSkinnedAsset();
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' has no skinned mesh assigned"), *Comp->GetName()));
	}
#if WITH_EDITOR
	if (const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset); Mesh && Mesh->IsCompiling())
	{
		return MCPError(FString::Printf(TEXT("'%s' is still compiling; read it again once it finishes"), *Asset->GetPathName()));
	}
#endif
	const FSkeletalMeshRenderData* RenderData = Asset->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("'%s' has no render data to skin"), *Asset->GetPathName()));
	}

	const FString Space = OptionalString(Params, TEXT("space"), TEXT("component")).ToLower();
	if (Space != TEXT("component") && Space != TEXT("world"))
	{
		return MCPError(FString::Printf(TEXT("space='%s' is not one of component, world"), *Space));
	}
	const bool bWorldSpace = Space == TEXT("world");

	const int32 DrawnLod = FMath::Clamp(Comp->GetPredictedLODLevel(), 0, RenderData->LODRenderData.Num() - 1);
	int32 LodIndex = DrawnLod;
	if (HasParam(Params, TEXT("lodIndex")))
	{
		if (TSharedPtr<FJsonValue> Error = MCPParseFiniteInteger(Params, TEXT("lodIndex"), LodIndex, TEXT("params"))) return Error;
		if (!RenderData->LODRenderData.IsValidIndex(LodIndex))
		{
			return MCPError(FString::Printf(
				TEXT("lodIndex %d is out of range: '%s' has %d render LOD(s)"),
				LodIndex, *Asset->GetPathName(), RenderData->LODRenderData.Num()));
		}
	}

	// Every selector and the page are checked before any vertex is read.
	const FReferenceSkeleton& RefSkeleton = Asset->GetRefSkeleton();
	MCPVertexRead::FIncludes Include;
	MCPVertexRead::FBoneSelection Bones;
	bool bByBox = false;
	FBox SelectionBox(ForceInit);
	MCPVertexRead::FPage Page;
	if (TSharedPtr<FJsonValue> Error = MCPVertexRead::ParseIncludes(Params, Include)) return Error;
	if (TSharedPtr<FJsonValue> Error = MCPVertexRead::ParseBoneSelection(Params, RefSkeleton, Asset->GetPathName(), Bones)) return Error;
	if (TSharedPtr<FJsonValue> Error = MCPVertexRead::ParseBox(Params, bByBox, SelectionBox)) return Error;
	if (TSharedPtr<FJsonValue> Error = MCPVertexRead::ParsePage(Params, TEXT("startIndex"), Page)) return Error;

	const bool bNear = HasParam(Params, TEXT("nearPoint")) || HasParam(Params, TEXT("radius"));
	FVector NearPoint = FVector::ZeroVector;
	double Radius = 0.0;
	if (bNear)
	{
		const TSharedPtr<FJsonObject>* PointObject = nullptr;
		if (!TryGetObjectParam(Params, TEXT("nearPoint"), PointObject) || !PointObject
			|| !ReadVec3FieldsStrict(*PointObject, NearPoint))
		{
			return MCPError(TEXT("nearPoint must be {x,y,z} with every axis given, and needs radius"));
		}
		if (!TryGetNumberParam(Params, TEXT("radius"), Radius) || !FMath::IsFinite(Radius) || Radius <= 0.0)
		{
			return MCPError(TEXT("radius must be a finite number above 0, and needs nearPoint"));
		}
	}

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LodIndex];
	const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
	const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
	const FSkinWeightVertexBuffer* SkinWeights = Comp->GetSkinWeightBuffer(LodIndex);
	if (!PositionBuffer.GetVertexData() || static_cast<int32>(PositionBuffer.GetNumVertices()) != VertexCount)
	{
		return MCPError(FString::Printf(
			TEXT("LOD %d of '%s' keeps no CPU copy of its vertex positions, so it cannot be skinned here"),
			LodIndex, *Asset->GetPathName()));
	}
	if (!SkinWeights || !SkinWeights->GetDataVertexBuffer()->GetWeightData()
		|| static_cast<int32>(SkinWeights->GetNumVertices()) != VertexCount)
	{
		return MCPError(FString::Printf(
			TEXT("LOD %d of '%s' keeps no CPU copy of its skin weights, so it cannot be skinned here"),
			LodIndex, *Asset->GetPathName()));
	}

	// Bind-pose positions plus the active morph targets, as the renderer applies them.
	TArray<FVector3f> Source;
	Source.SetNumUninitialized(VertexCount);
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex) Source[Vertex] = PositionBuffer.VertexPosition(Vertex);
	TArray<TSharedPtr<FJsonValue>> AppliedMorphs;
	for (const auto& Active : Comp->ActiveMorphTargets)
	{
		const UMorphTarget* Morph = Active.Key;
		if (!Morph || !Comp->MorphTargetWeights.IsValidIndex(Active.Value)) continue;
		const float Weight = Comp->MorphTargetWeights[Active.Value];
		if (FMath::IsNearlyZero(Weight)) continue;
#if UE_MCP_HAS_5_7_API
		const TConstArrayView<FMorphTargetDelta> Deltas = Morph->GetMorphTargetDeltas(LodIndex);
#else
		int32 DeltaCount = 0;
		const FMorphTargetDelta* DeltaData = Morph->GetMorphTargetDelta(LodIndex, DeltaCount);
		const TConstArrayView<FMorphTargetDelta> Deltas(DeltaData, DeltaData ? DeltaCount : 0);
#endif
		for (const FMorphTargetDelta& Delta : Deltas)
		{
			const int32 Target = static_cast<int32>(Delta.SourceIdx);
			if (Source.IsValidIndex(Target)) Source[Target] += Delta.PositionDelta * Weight;
		}
		auto Applied = MakeShared<FJsonObject>();
		Applied->SetStringField(TEXT("name"), Morph->GetName());
		Applied->SetNumberField(TEXT("weight"), Weight);
		Applied->SetNumberField(TEXT("deltaCount"), Deltas.Num());
		AppliedMorphs.Add(MakeShared<FJsonValueObject>(Applied));
	}

	TArray<FMatrix44f> RefToLocals;
	Comp->GetCurrentRefToLocalMatrices(RefToLocals, LodIndex);
	const FTransform ComponentToWorld = Comp->GetComponentTransform();
	const uint32 MaxInfluences = SkinWeights->GetMaxBoneInfluences();
	constexpr float InvMaxRawWeight = 1.0f / 65535.0f;

	// Section of each render vertex, for its bone map.
	TArray<int32> SectionOfVertex;
	SectionOfVertex.Init(INDEX_NONE, VertexCount);
	for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
		const int32 Base = static_cast<int32>(Section.BaseVertexIndex);
		const int32 End = FMath::Min(VertexCount, Base + static_cast<int32>(Section.NumVertices));
		for (int32 Vertex = FMath::Max(0, Base); Vertex < End; ++Vertex) SectionOfVertex[Vertex] = SectionIndex;
	}

	// Visits Vertex's influences as (skeleton bone index, normalized weight).
	auto ForEachInfluence = [&](int32 Vertex, auto&& Visit)
	{
		const int32 SectionIndex = SectionOfVertex[Vertex];
		if (SectionIndex == INDEX_NONE) return;
		const TArray<FBoneIndexType>& BoneMap = LODData.RenderSections[SectionIndex].BoneMap;
		for (uint32 Influence = 0; Influence < MaxInfluences; ++Influence)
		{
			const uint16 RawWeight = SkinWeights->GetBoneWeight(static_cast<uint32>(Vertex), Influence);
			if (RawWeight == 0) continue;
			const int32 MapIndex = static_cast<int32>(SkinWeights->GetBoneIndex(static_cast<uint32>(Vertex), Influence));
			if (!BoneMap.IsValidIndex(MapIndex)) continue;
			Visit(static_cast<int32>(BoneMap[MapIndex]), static_cast<float>(RawWeight) * InvMaxRawWeight);
		}
	};

	// Skinned in render vertex order, so pages are stable while the pose holds.
	struct FSelectedVertex
	{
		int32 Index = INDEX_NONE;
		FVector Position = FVector::ZeroVector;
		float SelectedWeight = 0.0f;
	};
	TArray<FSelectedVertex> Selected;
	TArray<int32> SelectionOrdinal;
	SelectionOrdinal.Init(INDEX_NONE, VertexCount);
	FBox SelectedBounds(ForceInit);
	const double RadiusSquared = Radius * Radius;
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
	{
		FVector3f Skinned = FVector3f::ZeroVector;
		float BoneWeight = 0.0f;
		ForEachInfluence(Vertex, [&](int32 BoneIndex, float Weight)
		{
			if (RefToLocals.IsValidIndex(BoneIndex)) Skinned += FVector3f(RefToLocals[BoneIndex].TransformPosition(Source[Vertex])) * Weight;
			if (Bones.IsSelected(BoneIndex)) BoneWeight += Weight;
		});
		const FVector Position = bWorldSpace ? ComponentToWorld.TransformPosition(FVector(Skinned)) : FVector(Skinned);
		if (bByBox && !SelectionBox.IsInsideOrOn(Position)) continue;
		if (bNear && FVector::DistSquared(Position, NearPoint) > RadiusSquared) continue;
		if (Bones.bActive && !Bones.Keeps(BoneWeight)) continue;
		SelectionOrdinal[Vertex] = Selected.Num();
		Selected.Add({ Vertex, Position, BoneWeight });
		SelectedBounds += Position;
	}
	Page.Resolve(Selected.Num());

	TArray<TSharedPtr<FJsonValue>> Vertices;
	Vertices.Reserve(Page.End - Page.First);
	for (int32 Ordinal = Page.First; Ordinal < Page.End; ++Ordinal)
	{
		const FSelectedVertex& Entry = Selected[Ordinal];
		auto Vertex = MakeShared<FJsonObject>();
		Vertex->SetNumberField(TEXT("vertexIndex"), Entry.Index);
		if (Include.bPositions) Vertex->SetObjectField(TEXT("position"), MCPVec3ToJsonObject(Entry.Position));
		if (Bones.bActive) Vertex->SetNumberField(TEXT("selectedWeight"), Entry.SelectedWeight);
		if (Include.bWeights)
		{
			TArray<TSharedPtr<FJsonValue>> Influences;
			ForEachInfluence(Entry.Index, [&](int32 BoneIndex, float Weight)
			{
				auto Influence = MakeShared<FJsonObject>();
				Influence->SetStringField(
					TEXT("boneName"),
					RefSkeleton.IsValidIndex(BoneIndex) ? RefSkeleton.GetBoneName(BoneIndex).ToString() : FString());
				Influence->SetNumberField(TEXT("boneIndex"), BoneIndex);
				Influence->SetNumberField(TEXT("weight"), Weight);
				Influences.Add(MakeShared<FJsonValueObject>(Influence));
			});
			Vertex->SetArrayField(TEXT("influences"), Influences);
		}
		Vertices.Add(MakeShared<FJsonValueObject>(Vertex));
	}

	TArray<TSharedPtr<FJsonValue>> Triangles;
	int32 SelectedTriangleCount = 0;
	if (Include.bTriangles)
	{
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
		if (!IndexBuffer || IndexBuffer->Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("LOD %d of '%s' keeps no CPU copy of its index buffer; read without include=triangles"),
				LodIndex, *Asset->GetPathName()));
		}
		SelectedTriangleCount = MCPVertexRead::CollectTriangles(
			[&LODData, IndexBuffer](auto&& Visit)
			{
				for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
				{
					const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
					for (uint32 Local = 0; Local < Section.NumTriangles; ++Local)
					{
						const uint32 First = Section.BaseIndex + Local * 3;
						if (static_cast<int32>(First + 2) >= IndexBuffer->Num()) break;
						MCPVertexRead::FTriangle Triangle;
						Triangle.Index = static_cast<int32>(First / 3);
						for (int32 Corner = 0; Corner < 3; ++Corner)
						{
							Triangle.Corners[Corner] = static_cast<int32>(IndexBuffer->Get(First + Corner));
						}
						Triangle.Group = SectionIndex;
						Visit(Triangle);
					}
				}
			},
			SelectionOrdinal, Page, TEXT("sectionIndex"), Triangles);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), Comp->GetName());
	Result->SetStringField(TEXT("componentClass"), Comp->GetClass()->GetName());
	Result->SetStringField(TEXT("skinnedAsset"), Asset->GetPathName());
	Result->SetStringField(TEXT("world"), MCPDescribeWorldType(World));
	Result->SetStringField(TEXT("space"), Space);
	Result->SetNumberField(TEXT("lodIndex"), LodIndex);
	Result->SetNumberField(TEXT("drawnLodIndex"), DrawnLod);
	Result->SetObjectField(TEXT("componentTransform"), MCPTransformToJsonObject(ComponentToWorld));
	if (const USkinnedMeshComponent* Leader = Comp->LeaderPoseComponent.Get())
	{
		auto LeaderObject = MakeShared<FJsonObject>();
		LeaderObject->SetStringField(TEXT("componentName"), Leader->GetName());
		if (const AActor* LeaderOwner = Leader->GetOwner()) LeaderObject->SetStringField(TEXT("actorLabel"), LeaderOwner->GetActorLabel());
		if (const USkinnedAsset* LeaderAsset = Leader->GetSkinnedAsset()) LeaderObject->SetStringField(TEXT("skinnedAsset"), LeaderAsset->GetPathName());
		Result->SetObjectField(TEXT("leaderPoseComponent"), LeaderObject);
	}
	Result->SetArrayField(TEXT("morphTargets"), AppliedMorphs);
	MCPVertexRead::SetSelectionSummary(Result, VertexCount, Selected.Num(), Bones, SelectedBounds);
	if (Include.bTriangles) Result->SetNumberField(TEXT("selectedTriangleCount"), SelectedTriangleCount);
	Result->SetStringField(
		TEXT("coordinateSpace"),
		bWorldSpace
			? TEXT("positions, box, nearPoint and selectedBounds are world-space posed positions in cm; vertexIndex and triangleIndex index this LOD's render geometry, not source vertex IDs")
			: TEXT("positions, box, nearPoint and selectedBounds are posed positions in the component's space in cm (componentTransform maps them to world); vertexIndex and triangleIndex index this LOD's render geometry, not source vertex IDs"));
	if (LodIndex != DrawnLod)
	{
		Result->SetStringField(
			TEXT("warning"),
			FString::Printf(TEXT("LOD %d is the one drawn. Bones only LOD %d uses keep the pose they last had."),
				DrawnLod, LodIndex));
	}

	const FString PathHash = FString::Printf(TEXT("%08x"), GetTypeHash(Comp->GetPathName()));
	const FString DefaultDumpPath = FPaths::Combine(
		TEXT("UE_MCP"), TEXT("SkinnedVertices"),
		FPaths::MakeValidFileName(FString::Printf(
			TEXT("%s_%s_LOD%d_%s"), *Actor->GetActorLabel(), *Comp->GetName(), LodIndex, *PathHash)) + TEXT(".json"));
	return MCPVertexRead::Finish(
		Result, Vertices, Triangles, Include.bTriangles, Page, TEXT("startIndex"), Selected.Num(),
		DefaultDumpPath, TEXT("skinned vertex dump"));
}
