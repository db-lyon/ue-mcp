// Foliage: place instances, remove them, read them back, put a type in the
// level's palette, and drive the procedural foliage simulation.
//
// WHAT THE AUDIT FOUND (V12). The shipped foliage surface could create a
// FoliageType asset, read its settings, write its settings one asset at a time
// or in a predicate-driven batch, and COUNT instances inside a sphere. It could
// not place a single instance, could not remove one, could not report where any
// of them are, and could not put a type into a level at all - so
// `create_type` produced an asset that never appeared in the level's foliage
// palette. Painting was the gap, not settings.
//
// There are deliberately NO per-setting actions here. Density, Radius, ScaleX/
// Y/Z, AlignToNormal, GroundSlopeAngle, CullDistance, CastShadow and every
// other tunable on UFoliageType is a plain UPROPERTY that
// foliage(set_settings), foliage(batch_set_settings_where) and
// asset(set_property) already reach. The same is true of the procedural
// spawner's RandomSeed / TileSize / NumUniqueTiles / MinimumQuadTreeSize, of
// UProceduralFoliageComponent's TileOverlap and bAllow* flags, and of
// ULandscapeGrassType's whole GrassVarieties array. A handler earns a place in
// this file only when it must call an engine function that a property write
// cannot stand in for:
//
//   * add_instances       - an instance is not a property. Placing one means
//                           AInstancedFoliageActor::FoliageTrace to find the
//                           surface, FPotentialInstance::PlaceInstance to apply
//                           the type's own scale/rotation/align/Z-offset rules,
//                           and FFoliageInfo::AddInstance to register it with
//                           the level's instanced component and spatial hash.
//   * remove_instances    - FFoliageInfo::RemoveInstances, plus the spatial
//                           queries that pick which indices to hand it.
//   * get_instances       - FFoliageInfo owns the instance array and its hash;
//                           neither is reachable from an asset property read,
//                           and sample_foliage only ever returned counts.
//   * add/remove_type_to_level
//                         - AddFoliageType allocates an FFoliageInfo and its
//                           backing component. That is object creation, which
//                           set_property cannot do.
//   * set_spawner_types   - UProceduralFoliageSpawner::FoliageTypes is a
//                           PRIVATE array of FFoliageTypeObject whose own
//                           object pointer is private too, and every write has
//                           to be followed by RefreshInstance() or the cached
//                           TypeInstance the simulation reads stays stale. A
//                           raw array write desyncs it silently.
//   * simulate_procedural - GenerateProceduralContent runs the tile simulation
//                           and hands back desired instances; something still
//                           has to trace and place each one.
//   * clear_procedural    - RemoveProceduralContent matches on the component's
//                           ProceduralGuid, which nothing else can see.
//
// ALREADY REACHABLE, deliberately NOT built here:
//   * Creating a UProceduralFoliageSpawner or a ULandscapeGrassType asset:
//     asset(create_asset_by_class) creates any concrete UObject class.
//   * Placing a ProceduralFoliageVolume: level(spawn_volume) resolves any
//     volume class by short name and builds the cube brush, which a bare
//     SpawnActor does not.
//   * Grass on landscape layers: the slot list lives in a
//     LandscapeGrassOutput node, which material(add_expression,
//     expressionType="LandscapeGrassOutput") adds, material(list_expressions) /
//     material(export_graph) read back, and editor(set_property) configures at
//     the node's object path. FGrassVariety is all UPROPERTYs.
//
// INDEX MODEL. Every instanceIndex here is an index into the FFoliageInfo the
// addressed InstancedFoliageActor holds for that foliage type - the same index
// space AddInstance appends to and RemoveInstances consumes. It is stable only
// while nothing else edits that type on that actor, which is why every result
// reports the actor it worked on and every rollback names it.

#include "FoliageHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "UObject/UnrealType.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "FoliageTypeObject.h"
#include "ProceduralFoliageComponent.h"
#include "ProceduralFoliageSpawner.h"
#include "ProceduralFoliageVolume.h"

namespace
{
	// Every helper in this file carries the FoliageDepth prefix on purpose. The
	// module is a unity build: two translation units that share a blob merge
	// their anonymous namespaces, so a helper whose name collides with one in a
	// neighbouring handler file is a redefinition (C2084) that appears on
	// whichever machine's adaptive-unity working set groups them together.

	constexpr int32 FoliageDepthMaxInstancesPerCall = 20000;
	constexpr int32 FoliageDepthMaxRollbackTransforms = 2000;

	/** Resolve a UFoliageType from an asset path, or from the name of a type
	 *  already placed in the open level. Mirrors what set_foliage_type_settings
	 *  accepts so one spelling works across the whole category. */
	UFoliageType* FoliageDepthResolveType(UWorld* World, const FString& Spec)
	{
		if (Spec.IsEmpty()) return nullptr;
		if (UFoliageType* ByPath = LoadAssetByPath<UFoliageType>(Spec))
		{
			return ByPath;
		}
		if (!World) return nullptr;
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			AInstancedFoliageActor* IFA = *It;
			if (!IFA) continue;
			for (const auto& Pair : IFA->GetFoliageInfos())
			{
				if (Pair.Key && (Pair.Key->GetName() == Spec || Pair.Key->GetPathName() == Spec))
				{
					return Pair.Key;
				}
			}
		}
		return nullptr;
	}

	/** The error a bad foliageTypePath gets: names what was wrong and lists the
	 *  types the open level actually has, since a name is a legal spelling. */
	TSharedPtr<FJsonValue> FoliageDepthTypeNotFound(UWorld* World, const FString& Spec)
	{
		TArray<FString> Known;
		if (World)
		{
			for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
			{
				AInstancedFoliageActor* IFA = *It;
				if (!IFA) continue;
				for (const auto& Pair : IFA->GetFoliageInfos())
				{
					if (Pair.Key) Known.AddUnique(Pair.Key->GetName());
				}
			}
		}
		const FString KnownList = Known.Num() > 0
			? FString::Printf(TEXT(" Types placed in the open level: %s."), *FString::Join(Known, TEXT(", ")))
			: TEXT(" No foliage types are placed in the open level; pass the FoliageType asset path.");
		return MCPError(FString::Printf(
			TEXT("Foliage type not found: '%s'. Pass a FoliageType asset path (foliage(create_type) returns one, asset(search) finds them) or the name of a type already in the level.%s"),
			*Spec, *KnownList));
	}

	/** World-space transform of one instance, read through the foliage
	 *  implementation rather than FFoliageInfo::Instances.
	 *
	 *  Both index the same way, but Instances sits behind WITH_EDITORONLY_DATA
	 *  and its accessibility has moved between engine versions (the existing
	 *  list_foliage_types carries a comment saying it was private on 5.7), while
	 *  FFoliageImpl::GetInstanceWorldTransform has been the public per-index
	 *  accessor throughout and also answers for actor-backed foliage, which has
	 *  no instanced component at all. */
	bool FoliageDepthGetInstanceTransform(const FFoliageInfo& Info, int32 Index, FTransform& Out)
	{
		if (!Info.Implementation.IsValid()) return false;
		if (Index < 0 || Index >= Info.Implementation->GetInstanceCount()) return false;
		Out = Info.Implementation->GetInstanceWorldTransform(Index);
		return true;
	}

	int32 FoliageDepthInstanceCount(const FFoliageInfo& Info)
	{
		return Info.Implementation.IsValid() ? Info.Implementation->GetInstanceCount() : 0;
	}

	TSharedPtr<FJsonObject> FoliageDepthTransformToJson(const FTransform& T)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(T.GetLocation()));
		Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(T.Rotator()));
		Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(T.GetScale3D()));
		return Obj;
	}

	/** Read one {location, rotation?, scale?} entry. Returns false with a reason
	 *  when the entry is not usable, so the whole batch can be validated before
	 *  a single instance is placed. */
	bool FoliageDepthReadTransformEntry(const TSharedPtr<FJsonValue>& Value, int32 Index, FTransform& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj)
		{
			OutError = FString::Printf(TEXT("transforms[%d] is not an object. Each entry is {location:{x,y,z}, rotation?:{pitch,yaw,roll}, scale?:{x,y,z}}."), Index);
			return false;
		}
		const TSharedPtr<FJsonObject>* LocObj = nullptr;
		FVector Location = FVector::ZeroVector;
		if (!(*Obj)->TryGetObjectField(TEXT("location"), LocObj) || !LocObj || !ReadVec3Fields(*LocObj, Location))
		{
			OutError = FString::Printf(TEXT("transforms[%d] has no readable 'location'. Expected {x,y,z} numbers."), Index);
			return false;
		}
		FRotator Rotation = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		if ((*Obj)->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj)
		{
			ReadRotatorFields(*RotObj, Rotation);
		}
		FVector Scale(1.0, 1.0, 1.0);
		const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
		if ((*Obj)->TryGetObjectField(TEXT("scale"), ScaleObj) && ScaleObj)
		{
			ReadVec3Fields(*ScaleObj, Scale);
		}
		Out = FTransform(Rotation, Location, Scale);
		return true;
	}

	/** Turn one desired placement into a registered instance.
	 *
	 *  This is the whole reason painting needs a handler. FoliageTrace finds the
	 *  surface under the requested point and reports the component the instance
	 *  should be based on (which is what makes the instance move when that
	 *  component moves). FPotentialInstance::PlaceInstance then applies the
	 *  FoliageType's own placement rules - random scale between ScaleX/Y/Z,
	 *  random yaw and pitch, align-to-normal within AlignMaxAngle, Z offset, and
	 *  the CollisionWithWorld test - which is what separates a painted instance
	 *  from a transform someone typed in.
	 *
	 *  Returns false and fills OutReason when the point produced no instance. */
	bool FoliageDepthPlaceDesiredInstance(
		UWorld* World,
		const UFoliageType* Type,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		const FGuid& ProceduralGuid,
		bool bProceduralMode,
		const FBodyInstance* ProceduralVolumeBody,
		bool bApplyTypeRules,
		bool bSkipCollision,
		const FTransform& ExplicitTransform,
		bool bHasExplicitTransform,
		FFoliageInstance& OutInstance,
		UActorComponent*& OutBaseComponent,
		FString& OutReason)
	{
		OutBaseComponent = nullptr;

		FDesiredFoliageInstance Desired(TraceStart, TraceEnd, Type, /*InTraceRadius=*/ 0.f);
		Desired.ProceduralGuid = ProceduralGuid;
		Desired.PlacementMode = bProceduralMode
			? EFoliagePlacementMode::Procedural
			: EFoliagePlacementMode::Manual;
		// Not initialised by either constructor, and FoliageTrace reads it in
		// procedural mode, so it is set on both paths rather than left as
		// whatever was on the stack.
		Desired.ProceduralVolumeBodyInstance = ProceduralVolumeBody;

		FHitResult Hit;
		if (!AInstancedFoliageActor::FoliageTrace(World, Hit, Desired))
		{
			OutReason = TEXT("no surface hit between traceStart and traceEnd");
			return false;
		}

		FPotentialInstance Potential(Hit.ImpactPoint, Hit.ImpactNormal, Hit.Component.Get(), /*InHitWeight=*/ 1.f, Desired);
		if (bApplyTypeRules)
		{
			if (!Potential.PlaceInstance(World, Type, OutInstance, bSkipCollision))
			{
				OutReason = TEXT("rejected by the foliage type's own placement rules (ground slope, height range, or CollisionWithWorld)");
				return false;
			}
		}
		else
		{
			OutInstance = FFoliageInstance();
			OutInstance.Location = Hit.ImpactPoint;
		}

		if (bHasExplicitTransform)
		{
			// The caller asked for this exact transform. Keep the traced base
			// component (that is what parents the instance) but not the traced
			// pose.
			OutInstance.SetInstanceWorldTransform(ExplicitTransform);
		}

		OutInstance.ProceduralGuid = ProceduralGuid;
		OutBaseComponent = Hit.Component.Get();
		return true;
	}

	/** Every UProceduralFoliageComponent the caller addressed: one actor by
	 *  label/path, or every volume in the open level bound to one spawner. */
	TSharedPtr<FJsonValue> FoliageDepthCollectProceduralComponents(
		UWorld* World,
		const TSharedPtr<FJsonObject>& Params,
		TArray<UProceduralFoliageComponent*>& OutComponents)
	{
		const FString SpawnerPath = OptionalString(Params, TEXT("spawnerPath"));
		const bool bHasActorSelector =
			Params->HasField(TEXT("actorLabel")) || Params->HasField(TEXT("actorPath"));

		if (bHasActorSelector)
		{
			FMCPActorSelector Selector;
			Selector.Match = EMCPActorMatch::LabelNameOrPath;
			TSharedPtr<FJsonValue> ActorErr;
			AActor* Actor = MCPResolveActor(World, Params, ActorErr, Selector);
			if (!Actor) return ActorErr;
			Actor->GetComponents(OutComponents);
			if (OutComponents.Num() == 0)
			{
				return MCPError(FString::Printf(
					TEXT("Actor '%s' carries no ProceduralFoliageComponent. Place one with level(spawn_volume, volumeType=\"ProceduralFoliageVolume\"), which builds the cube brush a bare spawn leaves empty."),
					*Actor->GetActorLabel()));
			}
			return nullptr;
		}

		if (SpawnerPath.IsEmpty())
		{
			return MCPError(TEXT("Name the target: actorLabel or actorPath for one ProceduralFoliageVolume, or spawnerPath to run every volume in the open level bound to that ProceduralFoliageSpawner."));
		}

		UProceduralFoliageSpawner* Spawner = LoadAssetByPath<UProceduralFoliageSpawner>(SpawnerPath);
		if (!Spawner)
		{
			return MCPAssetLoadError(SpawnerPath, TEXT("ProceduralFoliageSpawner"));
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			TArray<UProceduralFoliageComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UProceduralFoliageComponent* Comp : Comps)
			{
				if (Comp && Comp->FoliageSpawner == Spawner)
				{
					OutComponents.Add(Comp);
				}
			}
		}
		if (OutComponents.Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("No actor in the open level has a ProceduralFoliageComponent whose FoliageSpawner is '%s'. Place a volume with level(spawn_volume, volumeType=\"ProceduralFoliageVolume\") and set its ProceduralComponent.FoliageSpawner with editor(set_property)."),
				*SpawnerPath));
		}
		return nullptr;
	}

	/** The array property behind UProceduralFoliageSpawner::FoliageTypes, and
	 *  the object property inside each FFoliageTypeObject element. Both are
	 *  private UPROPERTYs, which reflection still sees; there is no setter. */
	bool FoliageDepthSpawnerTypeProperties(FArrayProperty*& OutArray, FObjectPropertyBase*& OutInner, FBoolProperty*& OutIsAsset)
	{
		OutArray = CastField<FArrayProperty>(
			UProceduralFoliageSpawner::StaticClass()->FindPropertyByName(TEXT("FoliageTypes")));
		if (!OutArray) return false;
		const UScriptStruct* ElemStruct = FFoliageTypeObject::StaticStruct();
		OutInner = CastField<FObjectPropertyBase>(ElemStruct->FindPropertyByName(TEXT("FoliageTypeObject")));
		OutIsAsset = CastField<FBoolProperty>(ElemStruct->FindPropertyByName(TEXT("bIsAsset")));
		return OutInner != nullptr;
	}

	/** Accept a FoliageType asset, or a Blueprint whose generated class derives
	 *  from UFoliageType - the two things FFoliageTypeObject's own AllowedClasses
	 *  metadata permits. Returns the object to store and whether it is an asset. */
	UObject* FoliageDepthResolveSpawnerEntry(const FString& Spec, bool& bOutIsAsset)
	{
		bOutIsAsset = false;
		if (Spec.IsEmpty()) return nullptr;
		if (UFoliageType* AsType = LoadAssetByPath<UFoliageType>(Spec))
		{
			bOutIsAsset = true;
			return AsType;
		}
		if (UBlueprint* AsBlueprint = LoadAssetByPath<UBlueprint>(Spec))
		{
			UClass* Generated = AsBlueprint->GeneratedClass;
			if (Generated && Generated->IsChildOf(UFoliageType::StaticClass()))
			{
				return AsBlueprint;
			}
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FFoliageHandlers::AddFoliageInstances(const TSharedPtr<FJsonObject>& Params)
{
	FString TypeSpec;
	if (auto Err = RequireString(Params, TEXT("foliageTypePath"), TypeSpec)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	UFoliageType* Type = FoliageDepthResolveType(World, TypeSpec);
	if (!Type) return FoliageDepthTypeNotFound(World, TypeSpec);

	const bool bApplyTypeRules = OptionalBool(Params, TEXT("applyTypeRules"), true);
	const bool bSkipCollision = OptionalBool(Params, TEXT("skipCollision"), false);
	const bool bProjectToGround = OptionalBool(Params, TEXT("projectToGround"), true);
	const double TraceUp = OptionalNumber(Params, TEXT("traceUp"), 10000.0);
	const double TraceDown = OptionalNumber(Params, TEXT("traceDown"), 100000.0);

	// ── Build the candidate list, validating everything before mutating ──
	TArray<FTransform> Explicit;
	bool bHasExplicit = false;
	const TArray<TSharedPtr<FJsonValue>>* TransformsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("transforms"), TransformsArr) && TransformsArr)
	{
		bHasExplicit = true;
		for (int32 i = 0; i < TransformsArr->Num(); ++i)
		{
			FTransform T;
			FString ReadErr;
			if (!FoliageDepthReadTransformEntry((*TransformsArr)[i], i, T, ReadErr))
			{
				return MCPError(ReadErr);
			}
			Explicit.Add(T);
		}
	}

	FVector Center = FVector::ZeroVector;
	const bool bHasCenter = Params->HasField(TEXT("center"));
	if (bHasCenter)
	{
		if (auto Err = RequireVec3(Params, TEXT("center"), Center)) return Err;
	}
	const double Radius = OptionalNumber(Params, TEXT("radius"), 0.0);
	const int32 ScatterCount = OptionalInt(Params, TEXT("count"), 0);

	if (!bHasExplicit && ScatterCount <= 0)
	{
		return MCPError(TEXT("Nothing to place. Pass transforms[] ({location,rotation?,scale?} per instance) for exact placement, or center + radius + count to scatter. count must be 1 or more."));
	}
	if (!bHasExplicit && !bHasCenter)
	{
		return MCPError(TEXT("Scatter placement needs 'center' ({x,y,z}) as well as 'count'. Pass transforms[] instead for exact placement."));
	}
	if (!bHasExplicit && Radius <= 0.0)
	{
		return MCPError(TEXT("Scatter placement needs a 'radius' greater than 0 (centimetres). Pass transforms[] instead for exact placement."));
	}

	const int32 Requested = bHasExplicit ? Explicit.Num() : ScatterCount;
	if (Requested <= 0)
	{
		return MCPError(TEXT("transforms[] is empty and count is 0, so this call would place nothing."));
	}
	if (Requested > FoliageDepthMaxInstancesPerCall)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to place %d instances in one call; the limit is %d. Split the call, or lower count."),
			Requested, FoliageDepthMaxInstancesPerCall));
	}

	// Trace endpoints per candidate.
	TArray<FVector> Points;
	Points.Reserve(Requested);
	if (bHasExplicit)
	{
		for (const FTransform& T : Explicit) Points.Add(T.GetLocation());
	}
	else
	{
		FRandomStream Stream(OptionalInt(Params, TEXT("seed"), 0));
		for (int32 i = 0; i < Requested; ++i)
		{
			// Uniform over the disc: sqrt keeps the density even rather than
			// bunching every point near the centre.
			const double Angle = static_cast<double>(Stream.FRand()) * 2.0 * UE_PI;
			const double R = Radius * FMath::Sqrt(static_cast<double>(Stream.FRand()));
			Points.Add(Center + FVector(R * FMath::Cos(Angle), R * FMath::Sin(Angle), 0.0));
		}
	}

	// ── Resolve the target actor, then place ──
	AInstancedFoliageActor* IFA = AInstancedFoliageActor::Get(World, /*bCreateIfNone=*/ true, nullptr, Points[0]);
	if (!IFA)
	{
		return MCPError(TEXT("Could not get or create an InstancedFoliageActor for the open level."));
	}

	FFoliageInfo* Info = nullptr;
	UFoliageType* LevelType = IFA->AddFoliageType(Type, &Info);
	if (!LevelType || !Info)
	{
		return MCPError(FString::Printf(
			TEXT("Could not register foliage type '%s' with InstancedFoliageActor '%s'."),
			*Type->GetName(), *IFA->GetName()));
	}

	const int32 CountBefore = FoliageDepthInstanceCount(*Info);

	IFA->Modify();

	TArray<TSharedPtr<FJsonValue>> Skipped;
	TArray<TSharedPtr<FJsonValue>> AddedTransforms;
	int32 Added = 0;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const FVector Point = Points[i];
		const FVector Start = bProjectToGround ? Point + FVector(0, 0, TraceUp) : Point + FVector(0, 0, 1.0);
		const FVector End = bProjectToGround ? Point - FVector(0, 0, TraceDown) : Point - FVector(0, 0, 1.0);

		FFoliageInstance Instance;
		UActorComponent* Base = nullptr;
		FString Reason;
		const bool bPlaced = FoliageDepthPlaceDesiredInstance(
			World, LevelType, Start, End,
			FGuid(), /*bProceduralMode=*/ false, /*ProceduralVolumeBody=*/ nullptr,
			bApplyTypeRules, bSkipCollision,
			bHasExplicit ? Explicit[i] : FTransform::Identity, bHasExplicit,
			Instance, Base, Reason);

		if (!bPlaced)
		{
			auto SkipObj = MakeShared<FJsonObject>();
			SkipObj->SetNumberField(TEXT("candidate"), i);
			SkipObj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Point));
			SkipObj->SetStringField(TEXT("reason"), Reason);
			Skipped.Add(MakeShared<FJsonValueObject>(SkipObj));
			continue;
		}

		Info->AddInstance(LevelType, Instance, Base);
		// Capped: the rollback payload crosses the wire, and 20000 transforms
		// in a JSON body is not a rollback anyone can use.
		if (AddedTransforms.Num() < FoliageDepthMaxRollbackTransforms)
		{
			AddedTransforms.Add(MakeShared<FJsonValueObject>(
				FoliageDepthTransformToJson(Instance.GetInstanceWorldTransform())));
		}
		++Added;
	}

	Info->Refresh(/*Async=*/ false, /*Force=*/ true);
	if (World->IsPartitionedWorld())
	{
		// On a World Partition map the engine re-homes instances into the actor
		// that owns each cell. Without this they all stay on the actor picked
		// from the first location's hint.
		AInstancedFoliageActor::UpdateInstancePartitioning(World);
	}

	auto Result = MCPSuccess();
	if (Added > 0) { MCPSetCreated(Result); } else { MCPSetExisted(Result); }
	Result->SetStringField(TEXT("foliageTypePath"), LevelType->GetPathName());
	Result->SetStringField(TEXT("foliageType"), LevelType->GetName());
	Result->SetStringField(TEXT("actorPath"), IFA->GetPathName());
	Result->SetStringField(TEXT("actorLabel"), IFA->GetActorLabel());
	Result->SetNumberField(TEXT("requested"), Requested);
	Result->SetNumberField(TEXT("added"), Added);
	Result->SetNumberField(TEXT("skipped"), Skipped.Num());
	Result->SetArrayField(TEXT("skippedCandidates"), Skipped);
	Result->SetNumberField(TEXT("instanceCountBefore"), CountBefore);
	Result->SetNumberField(TEXT("instanceCount"), FoliageDepthInstanceCount(*Info));
	Result->SetNumberField(TEXT("firstAddedIndex"), Added > 0 ? CountBefore : -1);
	if (World->IsPartitionedWorld())
	{
		Result->SetBoolField(TEXT("repartitioned"), true);
		Result->SetStringField(TEXT("partitionNote"), TEXT("World Partition re-homed the new instances across per-cell InstancedFoliageActors, so the indices reported here are no longer a reliable selector. Remove by center+radius instead."));
	}

	// The inverse is the transforms that were actually placed, replayed as an
	// exact-transform removal. Index-based removal would be wrong the moment
	// World Partition re-homes an instance or another call edits this type.
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("foliageTypePath"), LevelType->GetPathName());
		Payload->SetStringField(TEXT("actorPath"), IFA->GetPathName());
		Payload->SetArrayField(TEXT("transforms"), AddedTransforms);
		Payload->SetNumberField(TEXT("matchTolerance"), 1.0);
		if (Added > AddedTransforms.Num())
		{
			Payload->SetStringField(TEXT("lossy"), FString::Printf(
				TEXT("This call placed %d instances and the inverse names %d of them, which is the %d-transform rollback limit. Replaying it leaves the remainder in the level; foliage(remove_instances) with center and radius removes the rest, and takes everything else of this type in that region with it."),
				Added, AddedTransforms.Num(), FoliageDepthMaxRollbackTransforms));
		}
		MCPSetRollback(Result, TEXT("remove_foliage_instances"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::RemoveFoliageInstances(const TSharedPtr<FJsonObject>& Params)
{
	FString TypeSpec;
	if (auto Err = RequireString(Params, TEXT("foliageTypePath"), TypeSpec)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	UFoliageType* Type = FoliageDepthResolveType(World, TypeSpec);
	if (!Type) return FoliageDepthTypeNotFound(World, TypeSpec);

	const bool bAll = OptionalBool(Params, TEXT("all"), false);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const double MatchTolerance = OptionalNumber(Params, TEXT("matchTolerance"), 1.0);

	const TArray<TSharedPtr<FJsonValue>>* IndicesArr = nullptr;
	const bool bHasIndices = Params->TryGetArrayField(TEXT("instanceIndices"), IndicesArr) && IndicesArr;

	const TArray<TSharedPtr<FJsonValue>>* TransformsArr = nullptr;
	const bool bHasTransforms = Params->TryGetArrayField(TEXT("transforms"), TransformsArr) && TransformsArr;

	FVector Center = FVector::ZeroVector;
	const bool bHasCenter = Params->HasField(TEXT("center"));
	if (bHasCenter)
	{
		if (auto Err = RequireVec3(Params, TEXT("center"), Center)) return Err;
	}
	const double Radius = OptionalNumber(Params, TEXT("radius"), 0.0);
	const bool bHasSphere = bHasCenter && Radius > 0.0;

	if (!bAll && !bHasIndices && !bHasTransforms && !bHasSphere)
	{
		return MCPError(TEXT("Name what to remove: instanceIndices[] (from foliage(get_instances)), transforms[] with matchTolerance, center + radius for a sphere, or all=true for every instance of this type. Valid selectors are exactly those four."));
	}

	// Validate the explicit transform list before touching anything.
	TArray<FVector> WantedLocations;
	if (bHasTransforms)
	{
		for (int32 i = 0; i < TransformsArr->Num(); ++i)
		{
			FTransform T;
			FString ReadErr;
			if (!FoliageDepthReadTransformEntry((*TransformsArr)[i], i, T, ReadErr))
			{
				return MCPError(ReadErr);
			}
			WantedLocations.Add(T.GetLocation());
		}
	}

	// A caller may scope to one InstancedFoliageActor (the rollback of
	// add_instances does). Without that, every actor holding the type is fair game.
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));

	TArray<TSharedPtr<FJsonValue>> PerActor;
	int32 TotalRemoved = 0;
	int32 TotalMatched = 0;
	TArray<TSharedPtr<FJsonValue>> RemovedTransforms;
	const double ToleranceSq = MatchTolerance * MatchTolerance;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		if (!IFA) continue;
		if (!ActorPath.IsEmpty() && IFA->GetPathName() != ActorPath) continue;

		FFoliageInfo* Info = IFA->FindInfo(Type);
		if (!Info) continue;

		const int32 Count = FoliageDepthInstanceCount(*Info);
		TSet<int32> Selected;

		if (bAll)
		{
			for (int32 i = 0; i < Count; ++i) Selected.Add(i);
		}
		if (bHasIndices)
		{
			for (const TSharedPtr<FJsonValue>& V : *IndicesArr)
			{
				const int32 Idx = static_cast<int32>(V->AsNumber());
				if (Idx >= 0 && Idx < Count) Selected.Add(Idx);
			}
		}
		if (bHasSphere)
		{
			TArray<int32> InSphere;
			Info->GetInstancesInsideSphere(FSphere(Center, Radius), InSphere);
			for (int32 Idx : InSphere) Selected.Add(Idx);
		}
		if (bHasTransforms)
		{
			for (int32 i = 0; i < Count; ++i)
			{
				FTransform T;
				if (!FoliageDepthGetInstanceTransform(*Info, i, T)) continue;
				for (const FVector& Wanted : WantedLocations)
				{
					if (FVector::DistSquared(T.GetLocation(), Wanted) <= ToleranceSq)
					{
						Selected.Add(i);
						break;
					}
				}
			}
		}

		if (Selected.Num() == 0) continue;
		TotalMatched += Selected.Num();

		TArray<int32> ToRemove = Selected.Array();
		// Capture the poses first: RemoveInstances is otherwise unrecoverable,
		// and the transforms are the whole of what an instance is.
		for (int32 Idx : ToRemove)
		{
			if (RemovedTransforms.Num() >= FoliageDepthMaxRollbackTransforms) break;
			FTransform T;
			if (FoliageDepthGetInstanceTransform(*Info, Idx, T))
			{
				RemovedTransforms.Add(MakeShared<FJsonValueObject>(FoliageDepthTransformToJson(T)));
			}
		}

		auto ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("actorPath"), IFA->GetPathName());
		ActorObj->SetStringField(TEXT("actorLabel"), IFA->GetActorLabel());
		ActorObj->SetNumberField(TEXT("matched"), ToRemove.Num());
		ActorObj->SetNumberField(TEXT("instanceCountBefore"), Count);

		if (!bDryRun)
		{
			IFA->Modify();
			// Descending, so an earlier removal never shifts a later index.
			ToRemove.Sort([](const int32& A, const int32& B) { return A > B; });
			Info->RemoveInstances(ToRemove, /*RebuildFoliageTree=*/ true);
			TotalRemoved += ToRemove.Num();
			ActorObj->SetNumberField(TEXT("removed"), ToRemove.Num());
			ActorObj->SetNumberField(TEXT("instanceCount"), FoliageDepthInstanceCount(*Info));
		}
		else
		{
			ActorObj->SetNumberField(TEXT("removed"), 0);
			ActorObj->SetNumberField(TEXT("instanceCount"), Count);
		}

		PerActor.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
	Result->SetStringField(TEXT("foliageType"), Type->GetName());
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matched"), TotalMatched);
	Result->SetNumberField(TEXT("removed"), TotalRemoved);
	Result->SetArrayField(TEXT("actors"), PerActor);
	if (TotalRemoved > 0)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("note"), bDryRun
			? TEXT("dryRun=true, so nothing was removed. Rerun with dryRun=false to apply.")
			: TEXT("The selector matched no instance of this type in the open level."));
	}

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
		Payload->SetArrayField(TEXT("transforms"), RemovedTransforms);
		Payload->SetBoolField(TEXT("projectToGround"), false);
		Payload->SetBoolField(TEXT("applyTypeRules"), false);
		if (TotalMatched > RemovedTransforms.Num())
		{
			Payload->SetStringField(TEXT("lossy"), FString::Printf(
				TEXT("%d of %d removed instances are in this inverse; the rollback transform limit is %d, so replaying it restores fewer instances than were removed."),
				RemovedTransforms.Num(), TotalMatched, FoliageDepthMaxRollbackTransforms));
		}
		else
		{
			Payload->SetStringField(TEXT("lossy"), TEXT("Restores each instance's world transform and its foliage type. The base component each instance was painted onto is NOT restored, so a restored instance no longer follows the actor it was attached to."));
		}
		MCPSetRollback(Result, TEXT("add_foliage_instances"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::GetFoliageInstances(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString TypeSpec = OptionalString(Params, TEXT("foliageTypePath"));
	UFoliageType* Filter = nullptr;
	if (!TypeSpec.IsEmpty())
	{
		Filter = FoliageDepthResolveType(World, TypeSpec);
		if (!Filter) return FoliageDepthTypeNotFound(World, TypeSpec);
	}

	FVector Center = FVector::ZeroVector;
	const bool bHasCenter = Params->HasField(TEXT("center"));
	if (bHasCenter)
	{
		if (auto Err = RequireVec3(Params, TEXT("center"), Center)) return Err;
	}
	const double Radius = OptionalNumber(Params, TEXT("radius"), 0.0);
	const bool bHasSphere = bHasCenter && Radius > 0.0;

	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 200), 1, 20000);
	const int32 StartIndex = FMath::Max(0, OptionalInt(Params, TEXT("startIndex"), 0));
	const bool bIncludeTransforms = OptionalBool(Params, TEXT("includeTransforms"), true);

	TArray<TSharedPtr<FJsonValue>> TypesOut;
	int32 TotalMatching = 0;
	int32 Emitted = 0;
	int32 Seen = 0;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		if (!IFA) continue;

		for (const auto& Pair : IFA->GetFoliageInfos())
		{
			UFoliageType* Type = Pair.Key;
			if (!Type) continue;
			if (Filter && Type != Filter) continue;
			const FFoliageInfo& Info = *Pair.Value;

			TArray<int32> Candidates;
			if (bHasSphere)
			{
				Info.GetInstancesInsideSphere(FSphere(Center, Radius), Candidates);
			}
			else
			{
				const int32 Count = FoliageDepthInstanceCount(Info);
				Candidates.Reserve(Count);
				for (int32 i = 0; i < Count; ++i) Candidates.Add(i);
			}
			if (Candidates.Num() == 0) continue;
			Candidates.Sort();
			TotalMatching += Candidates.Num();

			TArray<TSharedPtr<FJsonValue>> InstancesOut;
			for (int32 Idx : Candidates)
			{
				if (Seen++ < StartIndex) continue;
				if (Emitted >= Limit) continue;
				auto InstObj = MakeShared<FJsonObject>();
				InstObj->SetNumberField(TEXT("index"), Idx);
				if (bIncludeTransforms)
				{
					FTransform T;
					if (FoliageDepthGetInstanceTransform(Info, Idx, T))
					{
						InstObj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(T.GetLocation()));
						InstObj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(T.Rotator()));
						InstObj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(T.GetScale3D()));
					}
				}
				InstancesOut.Add(MakeShared<FJsonValueObject>(InstObj));
				++Emitted;
			}

			auto TypeObj = MakeShared<FJsonObject>();
			TypeObj->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
			TypeObj->SetStringField(TEXT("foliageType"), Type->GetName());
			TypeObj->SetStringField(TEXT("actorPath"), IFA->GetPathName());
			TypeObj->SetStringField(TEXT("actorLabel"), IFA->GetActorLabel());
			TypeObj->SetNumberField(TEXT("instanceCount"), FoliageDepthInstanceCount(Info));
			TypeObj->SetNumberField(TEXT("matched"), Candidates.Num());
			TypeObj->SetArrayField(TEXT("instances"), InstancesOut);
			TypesOut.Add(MakeShared<FJsonValueObject>(TypeObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("types"), TypesOut);
	Result->SetNumberField(TEXT("matched"), TotalMatching);
	Result->SetNumberField(TEXT("returned"), Emitted);
	Result->SetNumberField(TEXT("startIndex"), StartIndex);
	Result->SetNumberField(TEXT("limit"), Limit);
	Result->SetBoolField(TEXT("hasMore"), StartIndex + Emitted < TotalMatching);
	Result->SetStringField(TEXT("indexNote"), TEXT("index is the position inside the named actor's FFoliageInfo for that type, which is what foliage(remove_instances, instanceIndices) consumes. It is stable only while nothing else edits that type on that actor."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::AddFoliageTypeToLevel(const TSharedPtr<FJsonObject>& Params)
{
	FString TypePath;
	if (auto Err = RequireString(Params, TEXT("foliageTypePath"), TypePath)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	UFoliageType* Type = LoadAssetByPath<UFoliageType>(TypePath);
	if (!Type)
	{
		return MCPAssetLoadError(TypePath, TEXT("FoliageType"));
	}

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::Get(World, /*bCreateIfNone=*/ true);
	if (!IFA)
	{
		return MCPError(TEXT("Could not get or create an InstancedFoliageActor for the open level."));
	}

	const bool bAlreadyThere = IFA->FindInfo(Type) != nullptr;

	auto Result = MCPSuccess();
	if (bAlreadyThere)
	{
		MCPSetExisted(Result);
	}
	else
	{
		IFA->Modify();
		FFoliageInfo* Info = nullptr;
		UFoliageType* LevelType = IFA->AddFoliageType(Type, &Info);
		if (!LevelType || !Info)
		{
			return MCPError(FString::Printf(
				TEXT("AddFoliageType refused '%s'. A FoliageType_InstancedStaticMesh needs its Mesh set; foliage(get_settings) reports meshPath."),
				*Type->GetName()));
		}
		MCPSetCreated(Result);
	}

	const FFoliageInfo* Info = IFA->FindInfo(Type);
	Result->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
	Result->SetStringField(TEXT("foliageType"), Type->GetName());
	Result->SetStringField(TEXT("actorPath"), IFA->GetPathName());
	Result->SetStringField(TEXT("actorLabel"), IFA->GetActorLabel());
	Result->SetNumberField(TEXT("instanceCount"), Info ? FoliageDepthInstanceCount(*Info) : 0);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
		if (bAlreadyThere)
		{
			Payload->SetStringField(TEXT("lossy"), TEXT("This call changed nothing, so the inverse would remove a type that was already in the level before it ran. Do not replay it."));
		}
		MCPSetRollback(Result, TEXT("remove_foliage_type_from_level"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::RemoveFoliageTypeFromLevel(const TSharedPtr<FJsonObject>& Params)
{
	FString TypeSpec;
	if (auto Err = RequireString(Params, TEXT("foliageTypePath"), TypeSpec)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	UFoliageType* Type = FoliageDepthResolveType(World, TypeSpec);
	if (!Type) return FoliageDepthTypeNotFound(World, TypeSpec);

	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	// Count first, across every actor, so the refusal below is accurate before
	// anything is touched.
	TArray<AInstancedFoliageActor*> Holders;
	int32 TotalInstances = 0;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		if (!IFA) continue;
		if (const FFoliageInfo* Info = IFA->FindInfo(Type))
		{
			Holders.Add(IFA);
			TotalInstances += FoliageDepthInstanceCount(*Info);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
	Result->SetStringField(TEXT("foliageType"), Type->GetName());

	if (Holders.Num() == 0)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetNumberField(TEXT("instancesDestroyed"), 0);
		Result->SetStringField(TEXT("note"), TEXT("This foliage type is not in the open level, so there was nothing to remove. The FoliageType asset itself is untouched; asset(delete) deletes it."));
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
		Payload->SetStringField(TEXT("lossy"), TEXT("This call changed nothing; replaying the inverse would ADD a type the level did not have."));
		MCPSetRollback(Result, TEXT("add_foliage_type_to_level"), Payload);
		return MCPResult(Result);
	}

	if (TotalInstances > 0 && !bForce)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' still has %d instance(s) in the open level across %d InstancedFoliageActor(s). Removing the type destroys all of them and their transforms cannot be recovered. Pass force=true to accept that, or remove the instances first with foliage(remove_instances, all=true), whose rollback carries the transforms."),
			*Type->GetName(), TotalInstances, Holders.Num()));
	}

	TArray<TSharedPtr<FJsonValue>> ActorsOut;
	for (AInstancedFoliageActor* IFA : Holders)
	{
		IFA->Modify();
		UFoliageType* TypePtr = Type;
		IFA->RemoveFoliageType(&TypePtr, 1);
		auto ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("actorPath"), IFA->GetPathName());
		ActorObj->SetStringField(TEXT("actorLabel"), IFA->GetActorLabel());
		ActorObj->SetBoolField(TEXT("stillPresent"), IFA->FindInfo(Type) != nullptr);
		ActorsOut.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	MCPSetUpdated(Result);
	Result->SetArrayField(TEXT("actors"), ActorsOut);
	Result->SetNumberField(TEXT("instancesDestroyed"), TotalInstances);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("foliageTypePath"), Type->GetPathName());
		Payload->SetStringField(TEXT("lossy"), FString::Printf(
			TEXT("The inverse puts the type back in the level's palette but does NOT restore the %d instance(s) this call destroyed; their transforms were not captured."),
			TotalInstances));
		MCPSetRollback(Result, TEXT("add_foliage_type_to_level"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::ReadProceduralFoliageSpawner(const TSharedPtr<FJsonObject>& Params)
{
	FString SpawnerPath;
	if (auto Err = RequireString(Params, TEXT("spawnerPath"), SpawnerPath)) return Err;

	UProceduralFoliageSpawner* Spawner = LoadAssetByPath<UProceduralFoliageSpawner>(SpawnerPath);
	if (!Spawner)
	{
		return MCPAssetLoadError(SpawnerPath, TEXT("ProceduralFoliageSpawner"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("spawnerPath"), Spawner->GetPathName());
	Result->SetStringField(TEXT("objectPath"), Spawner->GetPathName());
	Result->SetStringField(TEXT("settingsNote"), TEXT("RandomSeed, TileSize, NumUniqueTiles, MinimumQuadTreeSize, bUseOverrideFoliageTerrainMaterials and OverrideFoliageTerrainMaterials are plain UPROPERTYs: write them with asset(set_property) at objectPath. Only the FoliageTypes list needs foliage(set_spawner_types), because it is private and every write has to be followed by RefreshInstance()."));
	Result->SetNumberField(TEXT("randomSeed"), Spawner->RandomSeed);
	Result->SetNumberField(TEXT("tileSize"), Spawner->TileSize);
	Result->SetNumberField(TEXT("numUniqueTiles"), Spawner->NumUniqueTiles);
	Result->SetNumberField(TEXT("minimumQuadTreeSize"), Spawner->MinimumQuadTreeSize);
	Result->SetBoolField(TEXT("usesOverrideFoliageTerrainMaterials"), Spawner->UsesOverrideFoliageTerrainMaterials());

	TArray<TSharedPtr<FJsonValue>> TypesOut;
	const TArray<FFoliageTypeObject>& Entries = Spawner->GetFoliageTypes();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FFoliageTypeObject& Entry = Entries[i];
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), i);
		Obj->SetBoolField(TEXT("hasFoliageType"), Entry.HasFoliageType());
		Obj->SetBoolField(TEXT("containsValidInstance"), Entry.ContainsValidInstance());
		if (const UFoliageType* Instance = Entry.GetInstance())
		{
			Obj->SetStringField(TEXT("foliageTypePath"), Instance->GetPathName());
			Obj->SetStringField(TEXT("foliageType"), Instance->GetName());
			Obj->SetStringField(TEXT("className"), Instance->GetClass()->GetName());
			if (const UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Instance))
			{
				if (ISM->Mesh) Obj->SetStringField(TEXT("meshPath"), ISM->Mesh->GetPathName());
			}
		}
		TypesOut.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Result->SetArrayField(TEXT("foliageTypes"), TypesOut);
	Result->SetNumberField(TEXT("foliageTypeCount"), Entries.Num());

	// Which volumes in the open level actually run this spawner, and whether
	// they have produced anything. That is the half nobody can read off the
	// asset, and the usual reason a simulation appears to do nothing.
	TArray<TSharedPtr<FJsonValue>> VolumesOut;
	if (UWorld* World = GetEditorWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			TArray<UProceduralFoliageComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UProceduralFoliageComponent* Comp : Comps)
			{
				if (!Comp || Comp->FoliageSpawner != Spawner) continue;
				auto VObj = MakeShared<FJsonObject>();
				VObj->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
				VObj->SetStringField(TEXT("actorPath"), Actor->GetPathName());
				VObj->SetStringField(TEXT("componentName"), Comp->GetName());
				VObj->SetStringField(TEXT("componentPath"), Comp->GetPathName());
				VObj->SetNumberField(TEXT("tileOverlap"), Comp->TileOverlap);
				VObj->SetBoolField(TEXT("hasSpawnedInstances"), Comp->HasSpawnedAnyInstances());
				const FBox Bounds = Comp->GetBounds();
				VObj->SetBoolField(TEXT("boundsValid"), Bounds.IsValid != 0);
				if (Bounds.IsValid)
				{
					VObj->SetObjectField(TEXT("boundsMin"), MCPVec3ToJsonObject(Bounds.Min));
					VObj->SetObjectField(TEXT("boundsMax"), MCPVec3ToJsonObject(Bounds.Max));
				}
				else
				{
					VObj->SetStringField(TEXT("problem"), TEXT("The volume has zero bounds, so the simulation covers no area. A ProceduralFoliageVolume spawned without a brush is the usual cause; level(spawn_volume) builds one."));
				}
				VolumesOut.Add(MakeShared<FJsonValueObject>(VObj));
			}
		}
	}
	Result->SetArrayField(TEXT("volumes"), VolumesOut);
	Result->SetNumberField(TEXT("volumeCount"), VolumesOut.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SetProceduralFoliageSpawnerTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString SpawnerPath;
	if (auto Err = RequireString(Params, TEXT("spawnerPath"), SpawnerPath)) return Err;

	UProceduralFoliageSpawner* Spawner = LoadAssetByPath<UProceduralFoliageSpawner>(SpawnerPath);
	if (!Spawner)
	{
		return MCPAssetLoadError(SpawnerPath, TEXT("ProceduralFoliageSpawner"));
	}

	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("replace"));
	if (Mode != TEXT("replace") && Mode != TEXT("add") && Mode != TEXT("remove"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown mode '%s'. Valid values: replace (default, the list becomes exactly foliageTypePaths), add (append the ones not already there), remove (drop the ones listed)."),
			*Mode));
	}

	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("foliageTypePaths"), PathsArr) || !PathsArr)
	{
		return MCPError(TEXT("Missing required parameter 'foliageTypePaths' (array of FoliageType asset paths, or Blueprint paths whose generated class derives from FoliageType)."));
	}

	FArrayProperty* ArrayProp = nullptr;
	FObjectPropertyBase* InnerObjProp = nullptr;
	FBoolProperty* IsAssetProp = nullptr;
	if (!FoliageDepthSpawnerTypeProperties(ArrayProp, InnerObjProp, IsAssetProp))
	{
		return MCPError(TEXT("UProceduralFoliageSpawner::FoliageTypes or FFoliageTypeObject::FoliageTypeObject is not reachable by reflection on this engine version, so the spawner's type list cannot be edited."));
	}

	// ── Validate the whole request before writing anything ──
	TArray<UObject*> Wanted;
	TArray<bool> WantedIsAsset;
	for (int32 i = 0; i < PathsArr->Num(); ++i)
	{
		const FString Spec = (*PathsArr)[i]->AsString();
		bool bIsAsset = false;
		UObject* Resolved = FoliageDepthResolveSpawnerEntry(Spec, bIsAsset);
		if (!Resolved)
		{
			return MCPError(FString::Printf(
				TEXT("foliageTypePaths[%d] '%s' is not a FoliageType asset, and not a Blueprint whose generated class derives from FoliageType. Those are the only two things a ProceduralFoliageSpawner accepts; foliage(create_type) makes the first kind."),
				i, *Spec));
		}
		if (Wanted.Contains(Resolved))
		{
			return MCPError(FString::Printf(
				TEXT("foliageTypePaths[%d] '%s' appears twice. A spawner holds each foliage type once."),
				i, *Spec));
		}
		Wanted.Add(Resolved);
		WantedIsAsset.Add(bIsAsset);
	}

	// The list as it stands, for the idempotency answer and the inverse.
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Spawner));
	TArray<UObject*> Before;
	TArray<TSharedPtr<FJsonValue>> BeforePaths;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		UObject* Existing = InnerObjProp->GetObjectPropertyValue(
			InnerObjProp->ContainerPtrToValuePtr<void>(Helper.GetRawPtr(i)));
		Before.Add(Existing);
		BeforePaths.Add(MakeShared<FJsonValueString>(Existing ? Existing->GetPathName() : FString()));
	}

	TArray<UObject*> After;
	TArray<bool> AfterIsAsset;
	auto IsAssetFor = [&](UObject* Obj) -> bool { return Obj && Obj->IsA<UFoliageType>(); };

	if (Mode == TEXT("replace"))
	{
		After = Wanted;
		AfterIsAsset = WantedIsAsset;
	}
	else if (Mode == TEXT("add"))
	{
		After = Before;
		for (UObject* Obj : Before) AfterIsAsset.Add(IsAssetFor(Obj));
		for (int32 i = 0; i < Wanted.Num(); ++i)
		{
			if (!After.Contains(Wanted[i]))
			{
				After.Add(Wanted[i]);
				AfterIsAsset.Add(WantedIsAsset[i]);
			}
		}
	}
	else
	{
		for (UObject* Obj : Before)
		{
			if (!Wanted.Contains(Obj))
			{
				After.Add(Obj);
				AfterIsAsset.Add(IsAssetFor(Obj));
			}
		}
	}

	const bool bChanged = (After != Before);

	if (bChanged)
	{
		Spawner->Modify();
		Helper.Resize(After.Num());
		for (int32 i = 0; i < After.Num(); ++i)
		{
			void* Elem = Helper.GetRawPtr(i);
			InnerObjProp->SetObjectPropertyValue(
				InnerObjProp->ContainerPtrToValuePtr<void>(Elem), After[i]);
			if (IsAssetProp)
			{
				IsAssetProp->SetPropertyValue(
					IsAssetProp->ContainerPtrToValuePtr<void>(Elem), AfterIsAsset[i]);
			}
			// The cached TypeInstance the simulation actually reads is derived,
			// not stored by the write above. Without this the spawner keeps
			// simulating the previous type, or none at all.
			reinterpret_cast<FFoliageTypeObject*>(Elem)->RefreshInstance();
		}
		Spawner->PostEditChange();
		Spawner->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("spawnerPath"), Spawner->GetPathName());
	Result->SetStringField(TEXT("mode"), Mode);
	if (bChanged)
	{
		MCPSetUpdated(Result);
		FString SaveReason;
		const bool bSaved = OptionalBool(Params, TEXT("save"), true)
			? SaveAssetPackageChecked(Spawner, SaveReason)
			: false;
		Result->SetBoolField(TEXT("persisted"), bSaved);
		if (!bSaved && !SaveReason.IsEmpty()) Result->SetStringField(TEXT("persistReason"), SaveReason);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("note"), TEXT("The spawner already held exactly this list, so nothing was written and the package was not dirtied."));
	}

	// Read back rather than echo, so a value the engine coerced is visible.
	TArray<TSharedPtr<FJsonValue>> AfterOut;
	for (const FFoliageTypeObject& Entry : Spawner->GetFoliageTypes())
	{
		auto Obj = MakeShared<FJsonObject>();
		const UFoliageType* Instance = Entry.GetInstance();
		Obj->SetStringField(TEXT("foliageTypePath"), Instance ? Instance->GetPathName() : FString());
		Obj->SetBoolField(TEXT("containsValidInstance"), Entry.ContainsValidInstance());
		AfterOut.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Result->SetArrayField(TEXT("foliageTypes"), AfterOut);
	Result->SetNumberField(TEXT("foliageTypeCount"), AfterOut.Num());

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("spawnerPath"), Spawner->GetPathName());
		Payload->SetStringField(TEXT("mode"), TEXT("replace"));
		Payload->SetArrayField(TEXT("foliageTypePaths"), BeforePaths);
		MCPSetRollback(Result, TEXT("set_procedural_foliage_spawner_types"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SimulateProceduralFoliage(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<UProceduralFoliageComponent*> Components;
	if (auto Err = FoliageDepthCollectProceduralComponents(World, Params, Components)) return Err;

	const bool bClearExisting = OptionalBool(Params, TEXT("clearExisting"), true);
	const bool bSkipCollision = OptionalBool(Params, TEXT("skipCollision"), false);

	// Validate every component before running any of them: a half-simulated set
	// of volumes is worse than a refusal that names what is wrong.
	for (UProceduralFoliageComponent* Comp : Components)
	{
		if (!Comp->FoliageSpawner)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' has no FoliageSpawner set, so it has nothing to simulate. Set it with editor(set_property) at the component's object path, or asset(set_property) on the volume."),
				*Comp->GetPathName()));
		}
		if (Comp->FoliageSpawner->GetFoliageTypes().Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("Spawner '%s' has an empty FoliageTypes list, so the simulation would place nothing. Fill it with foliage(set_spawner_types)."),
				*Comp->FoliageSpawner->GetPathName()));
		}
		const FBox Bounds = Comp->GetBounds();
		if (!Bounds.IsValid || Bounds.GetSize().IsNearlyZero())
		{
			return MCPError(FString::Printf(
				TEXT("'%s' has zero bounds, so the simulation covers no area. A ProceduralFoliageVolume spawned without a brush does this; level(spawn_volume, volumeType=\"ProceduralFoliageVolume\", extent=...) builds one."),
				*Comp->GetPathName()));
		}
	}

	TArray<TSharedPtr<FJsonValue>> PerVolume;
	int32 TotalGenerated = 0;
	int32 TotalPlaced = 0;
	int32 TotalSkipped = 0;
	int32 TotalCleared = 0;

	for (UProceduralFoliageComponent* Comp : Components)
	{
		AActor* Owner = Comp->GetOwner();
		auto VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("actorLabel"), Owner ? Owner->GetActorLabel() : FString());
		VObj->SetStringField(TEXT("actorPath"), Owner ? Owner->GetPathName() : FString());
		VObj->SetStringField(TEXT("componentPath"), Comp->GetPathName());

		const bool bHadContent = Comp->HasSpawnedAnyInstances();
		if (bClearExisting && bHadContent)
		{
			Comp->RemoveProceduralContent(/*bInRebuildTree=*/ false);
			++TotalCleared;
		}
		VObj->SetBoolField(TEXT("clearedPrevious"), bClearExisting && bHadContent);

		TArray<FDesiredFoliageInstance> Desired;
		const bool bSimulated = Comp->GenerateProceduralContent(Desired);
		VObj->SetBoolField(TEXT("simulated"), bSimulated);
		VObj->SetNumberField(TEXT("generated"), Desired.Num());
		TotalGenerated += Desired.Num();

		int32 Placed = 0;
		int32 Skipped = 0;
		const FBodyInstance* VolumeBody = Comp->GetBoundsBodyInstance();
		TSet<AInstancedFoliageActor*> Touched;

		for (const FDesiredFoliageInstance& D : Desired)
		{
			if (!D.FoliageType) { ++Skipped; continue; }

			AInstancedFoliageActor* IFA =
				AInstancedFoliageActor::Get(World, /*bCreateIfNone=*/ true, nullptr, D.EndTrace);
			if (!IFA) { ++Skipped; continue; }

			FFoliageInfo* Info = nullptr;
			UFoliageType* LevelType = IFA->AddFoliageType(D.FoliageType, &Info);
			if (!LevelType || !Info) { ++Skipped; continue; }

			FFoliageInstance Instance;
			UActorComponent* Base = nullptr;
			FString Reason;
			if (!FoliageDepthPlaceDesiredInstance(
					World, LevelType, D.StartTrace, D.EndTrace,
					D.ProceduralGuid, /*bProceduralMode=*/ true, VolumeBody,
					/*bApplyTypeRules=*/ true, bSkipCollision,
					FTransform::Identity, /*bHasExplicitTransform=*/ false,
					Instance, Base, Reason))
			{
				++Skipped;
				continue;
			}

			if (!Touched.Contains(IFA))
			{
				IFA->Modify();
				Touched.Add(IFA);
			}
			Info->AddInstance(LevelType, Instance, Base);
			++Placed;
		}

		for (AInstancedFoliageActor* IFA : Touched)
		{
			IFA->ForEachFoliageInfo([](UFoliageType*, FFoliageInfo& Info)
			{
				Info.Refresh(/*Async=*/ false, /*Force=*/ true);
				return true;
			});
		}

		VObj->SetNumberField(TEXT("placed"), Placed);
		VObj->SetNumberField(TEXT("skipped"), Skipped);
		VObj->SetNumberField(TEXT("actorsTouched"), Touched.Num());
		TotalPlaced += Placed;
		TotalSkipped += Skipped;
		PerVolume.Add(MakeShared<FJsonValueObject>(VObj));
	}

	if (World->IsPartitionedWorld())
	{
		AInstancedFoliageActor::UpdateInstancePartitioning(World);
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("volumes"), PerVolume);
	Result->SetNumberField(TEXT("volumeCount"), Components.Num());
	Result->SetNumberField(TEXT("generated"), TotalGenerated);
	Result->SetNumberField(TEXT("placed"), TotalPlaced);
	Result->SetNumberField(TEXT("skipped"), TotalSkipped);
	Result->SetNumberField(TEXT("clearedVolumes"), TotalCleared);
	if (TotalPlaced > 0)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("note"), TEXT("The simulation placed nothing. Every generated point failed its trace or the foliage type's own placement rules; check that there is geometry inside the volume and that the type's GroundSlopeAngle and Height range admit it. foliage(read_spawner) reports the volume bounds and type list."));
	}

	{
		auto Payload = MakeShared<FJsonObject>();
		if (Params->HasField(TEXT("actorLabel"))) Payload->SetStringField(TEXT("actorLabel"), OptionalString(Params, TEXT("actorLabel")));
		if (Params->HasField(TEXT("actorPath"))) Payload->SetStringField(TEXT("actorPath"), OptionalString(Params, TEXT("actorPath")));
		if (Params->HasField(TEXT("spawnerPath"))) Payload->SetStringField(TEXT("spawnerPath"), OptionalString(Params, TEXT("spawnerPath")));
		Payload->SetStringField(TEXT("lossy"), bClearExisting
			? TEXT("The inverse removes everything this simulation placed, but the content that clearExisting=true discarded before it ran is gone for good. Rerunning simulate_procedural reproduces it only while the spawner's RandomSeed and FoliageTypes are unchanged.")
			: TEXT("The inverse removes every instance this component has spawned, including any that predate this call, because RemoveProceduralContent matches on the component's ProceduralGuid rather than on this run."));
		MCPSetRollback(Result, TEXT("clear_procedural_foliage"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::ClearProceduralFoliage(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<UProceduralFoliageComponent*> Components;
	if (auto Err = FoliageDepthCollectProceduralComponents(World, Params, Components)) return Err;

	TArray<TSharedPtr<FJsonValue>> PerVolume;
	int32 Cleared = 0;
	for (UProceduralFoliageComponent* Comp : Components)
	{
		AActor* Owner = Comp->GetOwner();
		const bool bHadContent = Comp->HasSpawnedAnyInstances();
		if (bHadContent)
		{
			Comp->RemoveProceduralContent(/*bInRebuildTree=*/ true);
			++Cleared;
		}
		auto VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("actorLabel"), Owner ? Owner->GetActorLabel() : FString());
		VObj->SetStringField(TEXT("actorPath"), Owner ? Owner->GetPathName() : FString());
		VObj->SetStringField(TEXT("componentPath"), Comp->GetPathName());
		VObj->SetBoolField(TEXT("hadContent"), bHadContent);
		VObj->SetBoolField(TEXT("cleared"), bHadContent);
		VObj->SetBoolField(TEXT("hasSpawnedInstances"), Comp->HasSpawnedAnyInstances());
		PerVolume.Add(MakeShared<FJsonValueObject>(VObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("volumes"), PerVolume);
	Result->SetNumberField(TEXT("volumeCount"), Components.Num());
	Result->SetNumberField(TEXT("cleared"), Cleared);
	if (Cleared > 0)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetStringField(TEXT("note"), TEXT("None of the addressed components had spawned any procedural content, so nothing was removed."));
	}

	{
		auto Payload = MakeShared<FJsonObject>();
		if (Params->HasField(TEXT("actorLabel"))) Payload->SetStringField(TEXT("actorLabel"), OptionalString(Params, TEXT("actorLabel")));
		if (Params->HasField(TEXT("actorPath"))) Payload->SetStringField(TEXT("actorPath"), OptionalString(Params, TEXT("actorPath")));
		if (Params->HasField(TEXT("spawnerPath"))) Payload->SetStringField(TEXT("spawnerPath"), OptionalString(Params, TEXT("spawnerPath")));
		Payload->SetStringField(TEXT("lossy"), TEXT("The inverse re-runs the simulation. It reproduces the cleared content exactly only while the spawner's RandomSeed, TileSize, NumUniqueTiles and FoliageTypes are unchanged and the geometry under the volume is the same."));
		MCPSetRollback(Result, TEXT("simulate_procedural_foliage"), Payload);
	}
	return MCPResult(Result);
}
