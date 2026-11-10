// level(audit_performance): one read-only pass over the loaded level, or a box
// of it, that names the usual scene-side causes of a slow frame.
//
// level(summarize_static_mesh_usage) answers "which meshes are placed and how
// often" and level(query_components) answers per-component questions with
// predicates. Neither ranks anything by cost, and the questions a performance
// pass starts with are rankings: where the triangles are, which meshes should
// be instanced, which shadow casters cost most, where shadowed lights pile up,
// which surfaces have texel or lightmap density far off the rest of the level,
// which heavy meshes are not Nanite, and which translucent surfaces are large
// or stacked. This computes all of them in one game-thread scan and returns
// bounded, ranked lists with their totals.
//
// It never writes: no Modify, no MarkPackageDirty, no property assignment.
//
// The figures are static estimates from asset and component data, not
// measurements: triangle counts are LOD0 (for a Nanite mesh, its fallback
// mesh), texel density is the mesh's own UV density at the component's scale
// against the largest texture its material samples, and overlap is computed
// from attenuation spheres and bounds. The result says so, and a trace read
// with editor(analyze_trace) is what confirms a suspicion.
//
// Helpers are prefixed MCPPerfAudit for the unity build.

#include "LevelHandlers.h"

#include "HandlerUtils.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshUVChannelInfo.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	constexpr int32 MCPPerfAuditDefaultResults = 20;
	constexpr int32 MCPPerfAuditMaxResults = 200;
	constexpr int32 MCPPerfAuditDumpCap = 5000;
	constexpr int32 MCPPerfAuditMaxCells = 100000;
	constexpr int32 MCPPerfAuditMaxTranslucentPairs = 4000;
	/** The engine assigns stationary lights to four shadow channels, so at most
	 *  four shadowed stationary lights can overlap any point. */
	constexpr int32 MCPPerfAuditStationaryChannelLimit = 4;

	const TCHAR* MCPPerfAuditMobility(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
		case EComponentMobility::Static:     return TEXT("Static");
		case EComponentMobility::Stationary: return TEXT("Stationary");
		case EComponentMobility::Movable:    return TEXT("Movable");
		default:                             return TEXT("Unknown");
		}
	}

	bool MCPPerfAuditIsTranslucent(const UMaterialInterface* Material)
	{
		if (!Material) return false;
		const EBlendMode Blend = Material->GetBlendMode();
		// The engine's own definition: anything that is neither opaque nor masked.
		return Blend != BLEND_Opaque && Blend != BLEND_Masked;
	}

	/** Largest texture dimension the material samples, or 0 when it samples none. */
	float MCPPerfAuditLargestTexture(UMaterialInterface* Material, ERHIFeatureLevel::Type FeatureLevel, TMap<UMaterialInterface*, float>& Cache)
	{
		if (!Material) return 0.0f;
		if (const float* Found = Cache.Find(Material)) return *Found;
		TArray<UTexture*> Used;
#if UE_MCP_HAS_5_7_API
		Material->GetUsedTextures(Used);
#else
		Material->GetUsedTextures(Used, EMaterialQualityLevel::High, true, FeatureLevel, true);
#endif
		float Largest = 0.0f;
		for (UTexture* Texture : Used)
		{
			if (!Texture) continue;
			Largest = FMath::Max(Largest, FMath::Max(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()));
		}
		Cache.Add(Material, Largest);
		return Largest;
	}

	int64 MCPPerfAuditSkeletalTriangles(USkeletalMeshComponent* Component)
	{
		USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
		FSkeletalMeshRenderData* RenderData = Mesh ? Mesh->GetResourceForRendering() : nullptr;
		if (!RenderData || RenderData->LODRenderData.Num() == 0) return 0;
		return RenderData->LODRenderData[0].GetTotalFaces();
	}

	struct FMCPPerfAuditCell
	{
		int32 X = 0;
		int32 Y = 0;
		int64 Triangles = 0;
		int32 Primitives = 0;
		int32 ShadowCasters = 0;
		int32 ShadowedLights = 0;
		int32 TranslucentPrimitives = 0;
	};

	struct FMCPPerfAuditMesh
	{
		FString Path;
		FString Kind;
		int64 Lod0Triangles = 0;
		int64 Placements = 0;
		int32 Components = 0;
		int32 Actors = 0;
		bool bNanite = false;
		FString LastActor;
	};

	struct FMCPPerfAuditInstancing
	{
		FString Path;
		int32 Components = 0;
		int32 Actors = 0;
		int32 Sections = 0;
		int32 Movable = 0;
		int64 Lod0Triangles = 0;
		FString LastActor;
		TArray<FString> SampleActors;
	};

	struct FMCPPerfAuditPrimRow
	{
		FString ActorLabel;
		FString ActorPath;
		FString Component;
		FString Mesh;
		FString Mobility;
		int64 Triangles = 0;
		double Value = 0.0;
		double Ratio = 0.0;
		double SizeM = 0.0;
		double AreaM2 = 0.0;
		int32 Overlaps = 0;
		int32 Extra = 0;
		FBox Box = FBox(ForceInit);
	};

	struct FMCPPerfAuditLight
	{
		FString ActorLabel;
		FString ActorPath;
		FString Component;
		FString Type;
		FString Mobility;
		FVector Location = FVector::ZeroVector;
		double Radius = 0.0;
		int32 Overlaps = 0;
		bool bStationary = false;
	};

	TSharedPtr<FJsonObject> MCPPerfAuditPrimJson(const FMCPPerfAuditPrimRow& Row)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("actorLabel"), Row.ActorLabel);
		Obj->SetStringField(TEXT("actorPath"), Row.ActorPath);
		Obj->SetStringField(TEXT("component"), Row.Component);
		if (!Row.Mesh.IsEmpty()) Obj->SetStringField(TEXT("mesh"), Row.Mesh);
		Obj->SetStringField(TEXT("mobility"), Row.Mobility);
		return Obj;
	}

	double MCPPerfAuditMedian(TArray<double> Values)
	{
		if (Values.Num() == 0) return 0.0;
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return (Values.Num() % 2 == 1) ? Values[Mid] : 0.5 * (Values[Mid - 1] + Values[Mid]);
	}

	/** Rows whose Value is at least Factor times the median, or at most
	 *  1/Factor of it. Ratio is filled in and the rows come back sorted by how
	 *  far off they are, either way. */
	TArray<FMCPPerfAuditPrimRow> MCPPerfAuditOutliers(const TArray<FMCPPerfAuditPrimRow>& Rows, double Factor, double& OutMedian)
	{
		TArray<double> Values;
		for (const FMCPPerfAuditPrimRow& Row : Rows) Values.Add(Row.Value);
		OutMedian = MCPPerfAuditMedian(Values);
		TArray<FMCPPerfAuditPrimRow> Out;
		if (OutMedian <= 0.0) return Out;
		for (const FMCPPerfAuditPrimRow& Row : Rows)
		{
			const double Ratio = Row.Value / OutMedian;
			if (Ratio >= Factor || (Ratio > 0.0 && Ratio <= 1.0 / Factor))
			{
				FMCPPerfAuditPrimRow Copy = Row;
				Copy.Ratio = Ratio;
				Out.Add(Copy);
			}
		}
		Out.Sort([](const FMCPPerfAuditPrimRow& A, const FMCPPerfAuditPrimRow& B)
		{
			const double DA = FMath::Abs(FMath::Loge(FMath::Max(A.Ratio, 1e-9)));
			const double DB = FMath::Abs(FMath::Loge(FMath::Max(B.Ratio, 1e-9)));
			return DA > DB;
		});
		return Out;
	}

	bool MCPPerfAuditCellIntersectsSphere(int32 CX, int32 CY, double Grid, const FVector& Center, double Radius)
	{
		const double MinX = CX * Grid;
		const double MinY = CY * Grid;
		const double DX = FMath::Max(0.0, FMath::Max(MinX - Center.X, Center.X - (MinX + Grid)));
		const double DY = FMath::Max(0.0, FMath::Max(MinY - Center.Y, Center.Y - (MinY + Grid)));
		return DX * DX + DY * DY <= Radius * Radius;
	}
}

// ---------------------------------------------------------------------------
// audit_level_performance -- ranked scene-cost estimates for the loaded level.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::AuditLevelPerformance(const TSharedPtr<FJsonObject>& Params)
{
	check(IsInGameThread());
	MCPReadParamsAhead(Params, { TEXT("regionMin"), TEXT("regionMax"), TEXT("world"), TEXT("pieInstance"), TEXT("maxResults") });

	const FString RequestedWorld = OptionalString(Params, TEXT("world"), TEXT("editor"));
	if (!RequestedWorld.Equals(TEXT("editor"), ESearchCase::IgnoreCase) && !RequestedWorld.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
	{
		return MCPError(TEXT("Invalid 'world': expected 'editor' or 'pie'"));
	}
	const FString WorldScope = RequestedWorld.ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	const bool bHasMin = HasParam(Params, TEXT("regionMin"));
	const bool bHasMax = HasParam(Params, TEXT("regionMax"));
	if (bHasMin != bHasMax)
	{
		return MCPError(TEXT("Pass both regionMin and regionMax to audit a box, or neither to audit the whole loaded level."));
	}
	FBox Region(ForceInit);
	if (bHasMin)
	{
		FVector Min, Max;
		if (auto Err = RequireVec3(Params, TEXT("regionMin"), Min)) return Err;
		if (auto Err = RequireVec3(Params, TEXT("regionMax"), Max)) return Err;
		if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z)
		{
			return MCPError(TEXT("regionMax must be at least regionMin on every axis."));
		}
		Region = FBox(Min, Max);
	}

	const double Grid = FMath::Max(100.0, OptionalNumber(Params, TEXT("gridSize"), 5000.0));
	const bool bDump = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"));
	const int32 MaxResults = bDump
		? MCPPerfAuditDumpCap
		: FMath::Clamp(OptionalInt(Params, TEXT("maxResults"), MCPPerfAuditDefaultResults), 1, MCPPerfAuditMaxResults);
	const int32 InstancingMin = FMath::Max(2, OptionalInt(Params, TEXT("instancingMinCount"), 10));
	const int64 HeavyTriangles = FMath::Max<int64>(1, static_cast<int64>(OptionalNumber(Params, TEXT("heavyTriangleThreshold"), 50000.0)));
	const double OutlierFactor = FMath::Max(1.5, OptionalNumber(Params, TEXT("outlierFactor"), 4.0));
	const double TranslucentAreaM2 = FMath::Max(0.01, OptionalNumber(Params, TEXT("translucentAreaM2"), 100.0));
	const int32 StackedTranslucent = FMath::Max(2, OptionalInt(Params, TEXT("stackedTranslucentCount"), 3));
	if (!OutputPath.IsEmpty() && !bDump)
	{
		return MCPError(TEXT("outputPath needs dumpToFile=true."));
	}

	auto CellKey = [Grid](const FVector& P) -> FIntPoint
	{
		return FIntPoint(FMath::FloorToInt(P.X / Grid), FMath::FloorToInt(P.Y / Grid));
	};

	TMap<FIntPoint, FMCPPerfAuditCell> Cells;
	TMap<FString, FMCPPerfAuditMesh> Meshes;
	TMap<FString, FMCPPerfAuditInstancing> Instancing;
	TMap<UMaterialInterface*, float> TextureCache;
	TArray<FMCPPerfAuditPrimRow> ShadowCasters;
	TArray<FMCPPerfAuditPrimRow> TexelRows;
	TArray<FMCPPerfAuditPrimRow> LightmapRows;
	TArray<FMCPPerfAuditPrimRow> TranslucentRows;
	TArray<FMCPPerfAuditLight> Lights;
	TMap<FString, TArray<FString>> NaniteBlockers;
	int32 ActorsScanned = 0;
	int32 PrimitivesScanned = 0;
	int32 SkippedHidden = 0;
	int32 MovableShadowCasters = 0;
	int32 ShadowedDirectional = 0;
	int64 TotalTriangles = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) continue;
		++ActorsScanned;
		const FString ActorPath = Actor->GetPathName();
		const FString ActorLabel = Actor->GetActorLabel();

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component) || !Component->IsRegistered() || Component->IsEditorOnly()) continue;

			// ── Lights ──────────────────────────────────────────────────────
			if (ULightComponent* Light = Cast<ULightComponent>(Component))
			{
				if (!Light->IsVisible() || !Light->bAffectsWorld || !Light->CastShadows) continue;
				if (Light->GetLightType() == LightType_Directional)
				{
					++ShadowedDirectional;
					continue;
				}
				ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light);
				if (!Local) continue;
				const FVector Location = Local->GetComponentLocation();
				const double Radius = Local->AttenuationRadius;
				if (Region.IsValid && !FMath::SphereAABBIntersection(FSphere(Location, Radius), Region)) continue;
				FMCPPerfAuditLight Row;
				Row.ActorLabel = ActorLabel;
				Row.ActorPath = ActorPath;
				Row.Component = Light->GetName();
				Row.Type = Light->GetLightType() == LightType_Spot ? TEXT("spot")
					: Light->GetLightType() == LightType_Rect ? TEXT("rect") : TEXT("point");
				Row.Mobility = MCPPerfAuditMobility(Light->Mobility);
				Row.bStationary = Light->Mobility == EComponentMobility::Stationary;
				Row.Location = Location;
				Row.Radius = Radius;
				Lights.Add(Row);
				continue;
			}

			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Component);
			if (!Prim) continue;
			const FBox Box = Prim->Bounds.GetBox();
			if (Region.IsValid && !Box.Intersect(Region)) continue;
			const bool bRendered = Prim->IsVisible() && !Prim->bHiddenInGame && !Actor->IsHidden();
			if (!bRendered)
			{
				++SkippedHidden;
				continue;
			}
			++PrimitivesScanned;

			UStaticMeshComponent* StaticComp = Cast<UStaticMeshComponent>(Prim);
			UStaticMesh* StaticMesh = StaticComp ? StaticComp->GetStaticMesh() : nullptr;
			USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Prim);
			UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Prim);
			const int64 Placements = ISM ? FMath::Max(0, ISM->GetInstanceCount()) : 1;

			int64 Lod0 = 0;
			FString MeshPath;
			FString Kind;
			bool bNanite = false;
			if (StaticMesh)
			{
				Lod0 = StaticMesh->GetNumTriangles(0);
				MeshPath = StaticMesh->GetPathName();
				Kind = TEXT("static");
				bNanite = StaticMesh->IsNaniteEnabled() && !MCPIsDisallowNanite(StaticComp) && !MCPIsForceDisableNanite(StaticComp);
			}
			else if (SkelComp && SkelComp->GetSkeletalMeshAsset())
			{
				Lod0 = MCPPerfAuditSkeletalTriangles(SkelComp);
				MeshPath = SkelComp->GetSkeletalMeshAsset()->GetPathName();
				Kind = TEXT("skeletal");
			}
			const int64 Triangles = Lod0 * Placements;
			TotalTriangles += Triangles;

			const FIntPoint Key = CellKey(Prim->Bounds.Origin);
			FMCPPerfAuditCell* Cell = Cells.Find(Key);
			if (!Cell)
			{
				if (Cells.Num() >= MCPPerfAuditMaxCells)
				{
					return MCPError(FString::Printf(
						TEXT("gridSize %.0f splits this level into more than %d occupied cells. Pass a larger gridSize or a region."),
						Grid, MCPPerfAuditMaxCells));
				}
				Cell = &Cells.Add(Key);
				Cell->X = Key.X;
				Cell->Y = Key.Y;
			}
			Cell->Triangles += Triangles;
			Cell->Primitives += 1;

			FMCPPerfAuditPrimRow Base;
			Base.ActorLabel = ActorLabel;
			Base.ActorPath = ActorPath;
			Base.Component = Prim->GetName();
			Base.Mesh = MeshPath;
			Base.Mobility = MCPPerfAuditMobility(Prim->Mobility);
			Base.Triangles = Triangles;
			Base.Box = Box;

			// ── Triangles by mesh ───────────────────────────────────────────
			if (!MeshPath.IsEmpty())
			{
				FMCPPerfAuditMesh& Mesh = Meshes.FindOrAdd(MeshPath);
				Mesh.Path = MeshPath;
				Mesh.Kind = Kind;
				Mesh.Lod0Triangles = Lod0;
				Mesh.Placements += Placements;
				Mesh.Components += 1;
				Mesh.bNanite = Mesh.bNanite || bNanite;
				if (Mesh.LastActor != ActorPath)
				{
					Mesh.LastActor = ActorPath;
					Mesh.Actors += 1;
				}
			}

			// ── Instancing candidates: plain components, one draw each ─────
			if (StaticMesh && !ISM)
			{
				FMCPPerfAuditInstancing& Inst = Instancing.FindOrAdd(MeshPath);
				Inst.Path = MeshPath;
				Inst.Components += 1;
				Inst.Sections = FMath::Max(1, StaticComp->GetNumMaterials());
				Inst.Lod0Triangles = Lod0;
				if (Prim->Mobility == EComponentMobility::Movable) Inst.Movable += 1;
				if (Inst.LastActor != ActorPath)
				{
					Inst.LastActor = ActorPath;
					Inst.Actors += 1;
					if (Inst.SampleActors.Num() < 5) Inst.SampleActors.Add(ActorLabel);
				}
			}

			// ── Dynamic shadow casters ──────────────────────────────────────
			if (Prim->CastShadow && Prim->bCastDynamicShadow)
			{
				Cell->ShadowCasters += 1;
				if (Prim->Mobility == EComponentMobility::Movable) ++MovableShadowCasters;
				ShadowCasters.Add(Base);
			}

			// ── Materials: translucency and texel density ───────────────────
			const int32 NumMaterials = Prim->GetNumMaterials();
			bool bTranslucent = false;
			double BestTexelsPerM = 0.0;
			for (int32 Slot = 0; Slot < NumMaterials; ++Slot)
			{
				UMaterialInterface* Material = Prim->GetMaterial(Slot);
				if (!Material) continue;
				if (MCPPerfAuditIsTranslucent(Material))
				{
					bTranslucent = true;
					if (StaticMesh)
					{
						NaniteBlockers.FindOrAdd(MeshPath).AddUnique(FString::Printf(
							TEXT("slot %d uses translucent material %s"), Slot, *Material->GetPathName()));
					}
				}
				if (StaticMesh)
				{
					const FMeshUVChannelInfo* UV = StaticMesh->GetUVChannelData(Slot);
					if (UV && UV->bInitialized && UV->LocalUVDensities[0] > KINDA_SMALL_NUMBER)
					{
						const float TextureSize = MCPPerfAuditLargestTexture(Material, World->GetFeatureLevel(), TextureCache);
						const double WorldPerUV = UV->LocalUVDensities[0] * FMath::Max(KINDA_SMALL_NUMBER, StaticComp->GetTextureStreamingTransformScale());
						if (TextureSize > 0.0f && WorldPerUV > 0.0)
						{
							BestTexelsPerM = FMath::Max(BestTexelsPerM, TextureSize / WorldPerUV * 100.0);
						}
					}
				}
			}
			if (BestTexelsPerM > 0.0)
			{
				FMCPPerfAuditPrimRow Row = Base;
				Row.Value = BestTexelsPerM;
				TexelRows.Add(Row);
			}

			// ── Lightmap density, static lighting only ──────────────────────
			if (StaticComp && Prim->Mobility == EComponentMobility::Static && StaticComp->HasValidSettingsForStaticLighting(false))
			{
				const int32 Resolution = StaticComp->GetStaticLightMapResolution();
				const double SizeM = Box.GetSize().GetMax() / 100.0;
				if (Resolution > 0 && SizeM > KINDA_SMALL_NUMBER)
				{
					FMCPPerfAuditPrimRow Row = Base;
					Row.Value = Resolution / SizeM;
					Row.Extra = Resolution;
					Row.SizeM = SizeM;
					LightmapRows.Add(Row);
				}
			}

			if (bTranslucent)
			{
				Cell->TranslucentPrimitives += 1;
				const FVector Size = Box.GetSize() / 100.0;
				FMCPPerfAuditPrimRow Row = Base;
				Row.AreaM2 = FMath::Max3(Size.X * Size.Y, Size.X * Size.Z, Size.Y * Size.Z);
				TranslucentRows.Add(Row);
			}
		}
	}

	// ── Lights against cells and against each other ─────────────────────────
	for (FMCPPerfAuditLight& Light : Lights)
	{
		for (TPair<FIntPoint, FMCPPerfAuditCell>& Pair : Cells)
		{
			if (MCPPerfAuditCellIntersectsSphere(Pair.Key.X, Pair.Key.Y, Grid, Light.Location, Light.Radius))
			{
				Pair.Value.ShadowedLights += 1;
			}
		}
	}
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		for (int32 j = i + 1; j < Lights.Num(); ++j)
		{
			const double Reach = Lights[i].Radius + Lights[j].Radius;
			if (FVector::DistSquared(Lights[i].Location, Lights[j].Location) <= Reach * Reach)
			{
				Lights[i].Overlaps += 1;
				Lights[j].Overlaps += 1;
			}
		}
	}

	// ── Translucent stacking ────────────────────────────────────────────────
	bool bTranslucentPairsCapped = false;
	if (TranslucentRows.Num() <= MCPPerfAuditMaxTranslucentPairs)
	{
		for (int32 i = 0; i < TranslucentRows.Num(); ++i)
		{
			for (int32 j = i + 1; j < TranslucentRows.Num(); ++j)
			{
				if (TranslucentRows[i].Box.Intersect(TranslucentRows[j].Box))
				{
					TranslucentRows[i].Overlaps += 1;
					TranslucentRows[j].Overlaps += 1;
				}
			}
		}
	}
	else
	{
		bTranslucentPairsCapped = true;
	}

	// ── Build the result ────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), WorldScope);
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetBoolField(TEXT("loadedOnly"), true);
	Result->SetNumberField(TEXT("gridSize"), Grid);
	Result->SetNumberField(TEXT("maxResults"), MaxResults);
	if (Region.IsValid)
	{
		TSharedPtr<FJsonObject> RegionObj = MakeShared<FJsonObject>();
		RegionObj->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(Region.Min));
		RegionObj->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(Region.Max));
		Result->SetObjectField(TEXT("region"), RegionObj);
	}

	TSharedPtr<FJsonObject> Totals = MakeShared<FJsonObject>();
	Totals->SetNumberField(TEXT("actorsScanned"), ActorsScanned);
	Totals->SetNumberField(TEXT("primitives"), PrimitivesScanned);
	Totals->SetNumberField(TEXT("skippedHidden"), SkippedHidden);
	Totals->SetNumberField(TEXT("lod0Triangles"), static_cast<double>(TotalTriangles));
	Totals->SetNumberField(TEXT("uniqueMeshes"), Meshes.Num());
	Totals->SetNumberField(TEXT("occupiedCells"), Cells.Num());
	Result->SetObjectField(TEXT("totals"), Totals);

	auto Cap = [MaxResults](int32 Num) { return FMath::Min(Num, MaxResults); };

	// Triangles by mesh.
	{
		TArray<FMCPPerfAuditMesh> Rows;
		Meshes.GenerateValueArray(Rows);
		Rows.Sort([](const FMCPPerfAuditMesh& A, const FMCPPerfAuditMesh& B)
		{
			const int64 TA = A.Lod0Triangles * A.Placements;
			const int64 TB = B.Lod0Triangles * B.Placements;
			return TA != TB ? TA > TB : A.Path < B.Path;
		});
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Cap(Rows.Num()); ++i)
		{
			const FMCPPerfAuditMesh& Row = Rows[i];
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("mesh"), Row.Path);
			Obj->SetStringField(TEXT("kind"), Row.Kind);
			Obj->SetNumberField(TEXT("lod0Triangles"), static_cast<double>(Row.Lod0Triangles));
			Obj->SetNumberField(TEXT("placements"), static_cast<double>(Row.Placements));
			Obj->SetNumberField(TEXT("components"), Row.Components);
			Obj->SetNumberField(TEXT("actors"), Row.Actors);
			Obj->SetNumberField(TEXT("totalTriangles"), static_cast<double>(Row.Lod0Triangles * Row.Placements));
			Obj->SetNumberField(TEXT("shareOfTriangles"), TotalTriangles > 0
				? static_cast<double>(Row.Lod0Triangles * Row.Placements) / static_cast<double>(TotalTriangles) : 0.0);
			if (Row.Kind == TEXT("static")) Obj->SetBoolField(TEXT("nanite"), Row.bNanite);
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("total"), Rows.Num());
		Section->SetArrayField(TEXT("rows"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < Rows.Num());
		Section->SetStringField(TEXT("note"), TEXT("LOD0 triangles times placements. For a Nanite mesh this counts its fallback mesh, not the Nanite geometry."));
		Result->SetObjectField(TEXT("trianglesByMesh"), Section);
	}

	// Cells, by triangles and by shadowed-light overlap.
	{
		TArray<FMCPPerfAuditCell> Rows;
		Cells.GenerateValueArray(Rows);
		auto CellJson = [Grid](const FMCPPerfAuditCell& Cell)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("cellX"), Cell.X);
			Obj->SetNumberField(TEXT("cellY"), Cell.Y);
			TSharedPtr<FJsonObject> Min = MakeShared<FJsonObject>();
			Min->SetNumberField(TEXT("x"), Cell.X * Grid);
			Min->SetNumberField(TEXT("y"), Cell.Y * Grid);
			Obj->SetObjectField(TEXT("min"), Min);
			TSharedPtr<FJsonObject> Max = MakeShared<FJsonObject>();
			Max->SetNumberField(TEXT("x"), (Cell.X + 1) * Grid);
			Max->SetNumberField(TEXT("y"), (Cell.Y + 1) * Grid);
			Obj->SetObjectField(TEXT("max"), Max);
			Obj->SetNumberField(TEXT("triangles"), static_cast<double>(Cell.Triangles));
			Obj->SetNumberField(TEXT("primitives"), Cell.Primitives);
			Obj->SetNumberField(TEXT("shadowCasters"), Cell.ShadowCasters);
			Obj->SetNumberField(TEXT("shadowedLights"), Cell.ShadowedLights);
			Obj->SetNumberField(TEXT("translucentPrimitives"), Cell.TranslucentPrimitives);
			return Obj;
		};
		TArray<FMCPPerfAuditCell> ByTriangles = Rows;
		ByTriangles.Sort([](const FMCPPerfAuditCell& A, const FMCPPerfAuditCell& B) { return A.Triangles > B.Triangles; });
		TArray<TSharedPtr<FJsonValue>> TriOut;
		for (int32 i = 0; i < Cap(ByTriangles.Num()); ++i) TriOut.Add(MakeShared<FJsonValueObject>(CellJson(ByTriangles[i])));

		TArray<FMCPPerfAuditCell> ByLights = Rows.FilterByPredicate([](const FMCPPerfAuditCell& C) { return C.ShadowedLights > 0; });
		ByLights.Sort([](const FMCPPerfAuditCell& A, const FMCPPerfAuditCell& B) { return A.ShadowedLights > B.ShadowedLights; });
		TArray<TSharedPtr<FJsonValue>> LightOut;
		for (int32 i = 0; i < Cap(ByLights.Num()); ++i) LightOut.Add(MakeShared<FJsonValueObject>(CellJson(ByLights[i])));

		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("total"), Rows.Num());
		Section->SetArrayField(TEXT("byTriangles"), TriOut);
		Section->SetArrayField(TEXT("byShadowedLights"), LightOut);
		Section->SetBoolField(TEXT("truncated"), TriOut.Num() < Rows.Num());
		Section->SetNumberField(TEXT("maxShadowedLightsInCell"), ByLights.Num() > 0 ? ByLights[0].ShadowedLights : 0);
		Section->SetStringField(TEXT("note"), TEXT("Cells are gridSize squares on X/Y; a primitive belongs to the cell its bounds centre is in, and a light counts in every occupied cell its attenuation sphere reaches."));
		Result->SetObjectField(TEXT("regions"), Section);
	}

	// Instancing candidates.
	{
		TArray<FMCPPerfAuditInstancing> Rows;
		for (const TPair<FString, FMCPPerfAuditInstancing>& Pair : Instancing)
		{
			if (Pair.Value.Components >= InstancingMin) Rows.Add(Pair.Value);
		}
		Rows.Sort([](const FMCPPerfAuditInstancing& A, const FMCPPerfAuditInstancing& B)
		{
			return A.Components * A.Sections != B.Components * B.Sections
				? A.Components * A.Sections > B.Components * B.Sections : A.Path < B.Path;
		});
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Cap(Rows.Num()); ++i)
		{
			const FMCPPerfAuditInstancing& Row = Rows[i];
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("mesh"), Row.Path);
			Obj->SetNumberField(TEXT("components"), Row.Components);
			Obj->SetNumberField(TEXT("actors"), Row.Actors);
			Obj->SetNumberField(TEXT("materialSections"), Row.Sections);
			Obj->SetNumberField(TEXT("separateDraws"), Row.Components * Row.Sections);
			Obj->SetNumberField(TEXT("movableComponents"), Row.Movable);
			Obj->SetNumberField(TEXT("lod0Triangles"), static_cast<double>(Row.Lod0Triangles));
			Obj->SetArrayField(TEXT("sampleActors"), MCPStringListToJson(Row.SampleActors));
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("minCount"), InstancingMin);
		Section->SetNumberField(TEXT("total"), Rows.Num());
		Section->SetArrayField(TEXT("rows"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < Rows.Num());
		Section->SetStringField(TEXT("note"), TEXT("The same static mesh placed as separate (non-instanced) components. Each component is its own draw per material section; placed as instances of one ISM or HISM component (level(add_hismc_instances)) they draw together."));
		Result->SetObjectField(TEXT("instancingCandidates"), Section);
	}

	// Dynamic shadow casters.
	{
		ShadowCasters.Sort([](const FMCPPerfAuditPrimRow& A, const FMCPPerfAuditPrimRow& B) { return A.Triangles > B.Triangles; });
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Cap(ShadowCasters.Num()); ++i)
		{
			TSharedPtr<FJsonObject> Obj = MCPPerfAuditPrimJson(ShadowCasters[i]);
			Obj->SetNumberField(TEXT("triangles"), static_cast<double>(ShadowCasters[i].Triangles));
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("count"), ShadowCasters.Num());
		Section->SetNumberField(TEXT("movableCount"), MovableShadowCasters);
		Section->SetArrayField(TEXT("costliest"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < ShadowCasters.Num());
		Section->SetStringField(TEXT("note"), TEXT("Rendered primitives with CastShadow and bCastDynamicShadow, ranked by LOD0 triangles (times instances). Movable casters re-render into shadow maps whenever they or a light move."));
		Result->SetObjectField(TEXT("dynamicShadowCasters"), Section);
	}

	// Shadowed lights.
	{
		Lights.Sort([](const FMCPPerfAuditLight& A, const FMCPPerfAuditLight& B) { return A.Overlaps > B.Overlaps; });
		TArray<TSharedPtr<FJsonValue>> Out;
		TArray<TSharedPtr<FJsonValue>> OverLimit;
		int32 StationaryOver = 0;
		int32 MaxOverlap = 0;
		for (int32 i = 0; i < Lights.Num(); ++i)
		{
			const FMCPPerfAuditLight& Light = Lights[i];
			MaxOverlap = FMath::Max(MaxOverlap, Light.Overlaps);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("actorLabel"), Light.ActorLabel);
			Obj->SetStringField(TEXT("actorPath"), Light.ActorPath);
			Obj->SetStringField(TEXT("component"), Light.Component);
			Obj->SetStringField(TEXT("type"), Light.Type);
			Obj->SetStringField(TEXT("mobility"), Light.Mobility);
			Obj->SetNumberField(TEXT("attenuationRadius"), Light.Radius);
			Obj->SetNumberField(TEXT("overlappingShadowedLights"), Light.Overlaps);
			if (i < MaxResults) Out.Add(MakeShared<FJsonValueObject>(Obj));
			if (Light.bStationary && Light.Overlaps >= MCPPerfAuditStationaryChannelLimit)
			{
				++StationaryOver;
				if (OverLimit.Num() < MaxResults) OverLimit.Add(MakeShared<FJsonValueObject>(Obj));
			}
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("shadowedLocalLights"), Lights.Num());
		Section->SetNumberField(TEXT("shadowedDirectionalLights"), ShadowedDirectional);
		Section->SetNumberField(TEXT("maxOverlap"), MaxOverlap);
		Section->SetArrayField(TEXT("rows"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < Lights.Num());
		Section->SetNumberField(TEXT("stationaryOverChannelLimit"), StationaryOver);
		Section->SetArrayField(TEXT("stationaryOverChannelLimitRows"), OverLimit);
		Section->SetStringField(TEXT("note"), TEXT("Shadow-casting point, spot and rect lights; overlap is attenuation spheres touching. A stationary light whose sphere touches four or more other shadowed stationary lights may exceed the four shadow channels stationary lights share, and the engine then drops its shadows."));
		Result->SetObjectField(TEXT("lights"), Section);
	}

	auto OutlierSection = [&](const TArray<FMCPPerfAuditPrimRow>& Rows, const TCHAR* ValueKey, const TCHAR* Note, bool bLightmap)
	{
		double Median = 0.0;
		const TArray<FMCPPerfAuditPrimRow> Outliers = MCPPerfAuditOutliers(Rows, OutlierFactor, Median);
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Cap(Outliers.Num()); ++i)
		{
			const FMCPPerfAuditPrimRow& Row = Outliers[i];
			TSharedPtr<FJsonObject> Obj = MCPPerfAuditPrimJson(Row);
			Obj->SetNumberField(ValueKey, Row.Value);
			Obj->SetNumberField(TEXT("ratioToMedian"), Row.Ratio);
			Obj->SetStringField(TEXT("direction"), Row.Ratio >= 1.0 ? TEXT("high") : TEXT("low"));
			if (bLightmap)
			{
				Obj->SetNumberField(TEXT("lightmapResolution"), Row.Extra);
				Obj->SetNumberField(TEXT("worldSizeM"), Row.SizeM);
			}
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("evaluated"), Rows.Num());
		Section->SetNumberField(TEXT("median"), Median);
		Section->SetNumberField(TEXT("outlierFactor"), OutlierFactor);
		Section->SetNumberField(TEXT("count"), Outliers.Num());
		Section->SetArrayField(TEXT("rows"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < Outliers.Num());
		Section->SetStringField(TEXT("note"), Note);
		return Section;
	};

	Result->SetObjectField(TEXT("texelDensityOutliers"), OutlierSection(TexelRows, TEXT("texelsPerMeter"),
		TEXT("Largest texture the material samples over the mesh's UV density at the component's scale, per component (its densest slot), against the median of every component that could be evaluated. A material that samples no texture, or a mesh without computed UV density, is not evaluated."),
		false));

	{
		bool bForceNoPrecomputed = false;
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			bForceNoPrecomputed = Settings->bForceNoPrecomputedLighting != 0;
		}
		bool bAllowStatic = true;
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting")))
		{
			bAllowStatic = CVar->GetInt() != 0;
		}
		TSharedPtr<FJsonObject> Section = OutlierSection(LightmapRows, TEXT("lightmapTexelsPerMeter"),
			TEXT("Static-mobility static mesh components set up for static lighting: effective lightmap resolution over the largest bounds dimension, against the median."),
			true);
		Section->SetBoolField(TEXT("staticLightingAllowed"), bAllowStatic);
		Section->SetBoolField(TEXT("forceNoPrecomputedLighting"), bForceNoPrecomputed);
		if (!bAllowStatic || bForceNoPrecomputed)
		{
			Section->SetStringField(TEXT("staticLightingNote"),
				TEXT("Static lighting is off for this project or level, so lightmap resolution costs nothing at runtime here; the rows matter only if static lighting is turned back on."));
		}
		Result->SetObjectField(TEXT("lightmapResolutionOutliers"), Section);
	}

	// Nanite candidates: heavy static meshes that are not Nanite.
	{
		TArray<FMCPPerfAuditMesh> Rows;
		for (const TPair<FString, FMCPPerfAuditMesh>& Pair : Meshes)
		{
			if (Pair.Value.Kind == TEXT("static") && !Pair.Value.bNanite && Pair.Value.Lod0Triangles >= HeavyTriangles) Rows.Add(Pair.Value);
		}
		Rows.Sort([](const FMCPPerfAuditMesh& A, const FMCPPerfAuditMesh& B)
		{
			return A.Lod0Triangles * A.Placements > B.Lod0Triangles * B.Placements;
		});
		TArray<TSharedPtr<FJsonValue>> Out;
		int32 Eligible = 0;
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FMCPPerfAuditMesh& Row = Rows[i];
			const TArray<FString>* Blockers = NaniteBlockers.Find(Row.Path);
			const bool bEligible = !Blockers || Blockers->Num() == 0;
			if (bEligible) ++Eligible;
			if (Out.Num() >= MaxResults) continue;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("mesh"), Row.Path);
			Obj->SetNumberField(TEXT("lod0Triangles"), static_cast<double>(Row.Lod0Triangles));
			Obj->SetNumberField(TEXT("placements"), static_cast<double>(Row.Placements));
			Obj->SetNumberField(TEXT("totalTriangles"), static_cast<double>(Row.Lod0Triangles * Row.Placements));
			Obj->SetBoolField(TEXT("eligible"), bEligible);
			Obj->SetArrayField(TEXT("blockers"), MCPStringListToJson(Blockers ? *Blockers : TArray<FString>()));
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("heavyTriangleThreshold"), static_cast<double>(HeavyTriangles));
		Section->SetNumberField(TEXT("total"), Rows.Num());
		Section->SetNumberField(TEXT("eligible"), Eligible);
		Section->SetArrayField(TEXT("rows"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < Rows.Num());
		Section->SetStringField(TEXT("note"), TEXT("Static meshes at or over heavyTriangleThreshold LOD0 triangles that render without Nanite (mesh setting off, or the component disallows it). A translucent material blocks Nanite; level(set_nanite_settings) turns it on for the rest."));
		Result->SetObjectField(TEXT("naniteCandidates"), Section);
	}

	// Translucent overdraw candidates: large, or stacked on other translucency.
	{
		TArray<FMCPPerfAuditPrimRow> Rows = TranslucentRows.FilterByPredicate([&](const FMCPPerfAuditPrimRow& Row)
		{
			return Row.AreaM2 >= TranslucentAreaM2 || Row.Overlaps + 1 >= StackedTranslucent;
		});
		Rows.Sort([](const FMCPPerfAuditPrimRow& A, const FMCPPerfAuditPrimRow& B)
		{
			return A.AreaM2 * (A.Overlaps + 1) > B.AreaM2 * (B.Overlaps + 1);
		});
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Cap(Rows.Num()); ++i)
		{
			const FMCPPerfAuditPrimRow& Row = Rows[i];
			TSharedPtr<FJsonObject> Obj = MCPPerfAuditPrimJson(Row);
			Obj->SetNumberField(TEXT("areaM2"), Row.AreaM2);
			Obj->SetNumberField(TEXT("stackDepth"), Row.Overlaps + 1);
			TArray<FString> Reasons;
			if (Row.AreaM2 >= TranslucentAreaM2) Reasons.Add(TEXT("large"));
			if (Row.Overlaps + 1 >= StackedTranslucent) Reasons.Add(TEXT("stacked"));
			Obj->SetArrayField(TEXT("reasons"), MCPStringListToJson(Reasons));
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("translucentPrimitives"), TranslucentRows.Num());
		Section->SetNumberField(TEXT("areaThresholdM2"), TranslucentAreaM2);
		Section->SetNumberField(TEXT("stackedCount"), StackedTranslucent);
		Section->SetNumberField(TEXT("count"), Rows.Num());
		Section->SetArrayField(TEXT("rows"), Out);
		Section->SetBoolField(TEXT("truncated"), Out.Num() < Rows.Num());
		Section->SetBoolField(TEXT("stackingSkipped"), bTranslucentPairsCapped);
		Section->SetStringField(TEXT("note"), TEXT("Rendered primitives with a translucent material whose largest bounds face is at least areaThresholdM2, or whose bounds overlap enough other translucent primitives to stack stackedCount deep. Every translucent layer shades every pixel it covers."));
		Result->SetObjectField(TEXT("translucentOverdraw"), Section);
	}

	Result->SetStringField(TEXT("note"),
		TEXT("Static estimates from asset and component data over the loaded actors, not measurements. Confirm a suspicion with a trace: editor(start_trace), editor(stop_trace), editor(analyze_trace)."));

	if (!bDump)
	{
		return MCPResult(Result);
	}

	const FString DefaultPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP"), TEXT("PerfAudit"),
		FString::Printf(TEXT("%s_%s.json"), *World->GetName(), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	const FString Target = MCPResolveDumpPath(OutputPath.IsEmpty() ? DefaultPath : OutputPath);
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Result.ToSharedRef(), Writer))
	{
		return MCPError(TEXT("Failed to serialise the performance audit to JSON."));
	}
	FString Error;
	if (!MCPWriteDumpFile(Target, JsonText, TEXT("performance audit"), Error))
	{
		return MCPError(Error);
	}
	TSharedPtr<FJsonObject> Short = MCPSuccess();
	Short->SetBoolField(TEXT("dumpedToFile"), true);
	Short->SetStringField(TEXT("outputPath"), FPaths::ConvertRelativePathToFull(Target));
	Short->SetStringField(TEXT("world"), WorldScope);
	Short->SetObjectField(TEXT("totals"), Totals);
	return MCPResult(Short);
}
