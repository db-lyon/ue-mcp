// World Partition actor descriptors (#746).
//
// In a World Partition map an actor whose cell is not loaded is not spawned in
// the editor world at all, so every actor-facing action that iterates spawned
// instances - get_actors_by_class, count_actors_by_class, get_outliner,
// get_actor_details, set_actor_property - silently returned zero for it. A
// caller could not tell "this actor does not exist" from "this actor exists but
// is not streamed in", which quietly corrupts any measurement built on those
// counts. These handlers read the on-disk descriptors instead, so unloaded
// actors are visible and can be streamed in on demand.
//
// This file is a translation-unit partition of FLevelHandlers, like
// LevelHandlers_MultiLevel.cpp - the registrations live in LevelHandlers.cpp.
//
// The engine's own FActorDesc snapshot type is deliberately not used here
// (#805). Its constructors only gained ENGINE_API in UE 5.8, so on 5.7 and
// earlier every reference to them compiles and then fails to link, taking the
// whole plugin down rather than just these two actions. FMCPActorDescSnapshot
// below carries the same fields, is populated the same way the engine populates
// FActorDesc, and only reads exported accessors on
// FWorldPartitionActorDescInstance, so it links on every supported engine.

#include "LevelHandlers.h"

#include "HandlerUtils.h"
#include "HandlerPagination.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

// FWorldPartitionActorDescInstance and the ...ActorDescInstance traversal
// helpers arrived with the actor-desc container instance rework in UE 5.5. On
// 5.4 the whole editor implementation below is compiled out and both actions
// answer with a clear version error instead of failing the build.
#define UE_MCP_HAS_ACTOR_DESC_INSTANCE_API (WITH_EDITOR && UE_MCP_HAS_5_5_API)

#if UE_MCP_HAS_ACTOR_DESC_INSTANCE_API
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#endif

// Editor loaded regions (list_loaded_regions, unload_loaded_regions, load_region).
//
// Editor loaded regions, driven exactly as the World Partition editor window drives them
// (SWorldPartitionEditorGrid2D: CreateRegionFromSelection and UnloadSelectedRegions):
//   UWorldPartition::GetRegisteredEditorLoaderAdapters(), CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, Box, Label)
//   and ReleaseEditorLoaderAdapter(UWorldPartitionEditorLoaderAdapter*) (WorldPartition.h, WITH_EDITOR);
//   IWorldPartitionActorLoaderInterface::ILoaderAdapter::Load(), Unload(), IsLoaded(), GetBoundingBox(),
//   GetUserCreated() and SetUserCreated() (WorldPartitionActorLoaderInterface.h). Location volumes are
//   ALocationVolume actors, which implement the same interface.
#if UE_MCP_HAS_ACTOR_DESC_INSTANCE_API
#define UE_MCP_HAS_LOADED_REGION_API 1
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"
#else
#define UE_MCP_HAS_LOADED_REGION_API 0
#endif

namespace
{
#if UE_MCP_HAS_ACTOR_DESC_INSTANCE_API
	constexpr int32 DefaultActorDescLimit = 500;

	/**
	 * Field-for-field stand-in for the engine's FActorDesc, populated exactly the
	 * way the engine populates it. Declared here so the plugin never links
	 * against FActorDesc's unexported constructors (#805).
	 */
	struct FMCPActorDescSnapshot
	{
		FGuid Guid;
		UClass* NativeClass = nullptr;
		FSoftObjectPath Class;
		FName Name;
		FName Label;
		FBox Bounds = FBox(ForceInit);
		FName RuntimeGrid;
		bool bIsSpatiallyLoaded = false;
		bool bActorIsEditorOnly = false;
		FName ActorPackage;
		FName ActorPath;
		TArray<FSoftObjectPath> DataLayerAssets;

		explicit FMCPActorDescSnapshot(const FWorldPartitionActorDescInstance& Instance)
		{
			Guid = Instance.GetGuid();
			NativeClass = Instance.GetActorNativeClass();
			// A Blueprint actor reports its base class path; a native actor has
			// none, so fall back to the native class path. The single-argument
			// top-level-path constructor is used rather than passing an empty
			// subpath, whose wide-string overload is deprecated in 5.6+.
			Class = Instance.GetBaseClass().IsNull()
				? FSoftObjectPath(Instance.GetActorNativeClass())
				: FSoftObjectPath(Instance.GetBaseClass());
			Name = Instance.GetActorName();
			Label = Instance.GetActorLabel();
			Bounds = Instance.GetEditorBounds();
			RuntimeGrid = Instance.GetRuntimeGrid();
			bIsSpatiallyLoaded = Instance.GetIsSpatiallyLoaded();
			bActorIsEditorOnly = Instance.GetActorIsEditorOnly();
			ActorPackage = Instance.GetActorPackage();
			ActorPath = *Instance.GetActorSoftPath().ToString();

			// #937: IsUsingDataLayerAsset() and GetDataLayers() are deprecated in
			// UE 5.8 (C4996, and the message warns the project stops compiling in
			// the release after). Actors no longer carry data layers without
			// assets, so the old guard is now always true and the branch it
			// protected is unconditional. GetDataLayerInstanceNames() is the
			// supported read and its names are the asset paths this wants.
			const TArray<FName>& DataLayers = Instance.GetDataLayerInstanceNames().ToArray();
			DataLayerAssets.Reserve(DataLayers.Num());
			for (const FName& DataLayerAssetPath : DataLayers)
			{
				DataLayerAssets.Add(FSoftObjectPath(DataLayerAssetPath.ToString()));
			}
		}
	};

	/** GUIDs of actors currently spawned in the editor world. */
	TSet<FGuid> CollectLoadedActorGuids(UWorld* World)
	{
		TSet<FGuid> Loaded;
		if (!World) return Loaded;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (AActor* Actor = *It)
			{
				const FGuid Guid = Actor->GetActorGuid();
				if (Guid.IsValid()) Loaded.Add(Guid);
			}
		}
		return Loaded;
	}

	FString ActorDescClassName(const FMCPActorDescSnapshot& Desc)
	{
		if (Desc.NativeClass) return Desc.NativeClass->GetName();
		return Desc.Class.IsValid() ? Desc.Class.GetAssetName() : FString();
	}

	/** Case-insensitive match of a needle against every identifying field. */
	bool ActorDescMatchesFilter(const FMCPActorDescSnapshot& Desc, const FString& LowerFilter)
	{
		if (LowerFilter.IsEmpty()) return true;
		const FString Blob = (Desc.Label.ToString() + TEXT("|") + Desc.Name.ToString() + TEXT("|") +
			ActorDescClassName(Desc) + TEXT("|") + Desc.Class.ToString() + TEXT("|") +
			Desc.ActorPath.ToString()).ToLower();
		return Blob.Contains(LowerFilter);
	}

	TSharedPtr<FJsonObject> ActorDescToJson(const FMCPActorDescSnapshot& Desc, bool bLoaded)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("guid"), Desc.Guid.ToString());
		Obj->SetStringField(TEXT("label"), Desc.Label.ToString());
		Obj->SetStringField(TEXT("name"), Desc.Name.ToString());
		Obj->SetStringField(TEXT("class"), ActorDescClassName(Desc));
		Obj->SetStringField(TEXT("classPath"), Desc.Class.ToString());
		Obj->SetStringField(TEXT("actorPath"), Desc.ActorPath.ToString());
		Obj->SetStringField(TEXT("package"), Desc.ActorPackage.ToString());
		Obj->SetStringField(TEXT("runtimeGrid"), Desc.RuntimeGrid.ToString());
		Obj->SetBoolField(TEXT("spatiallyLoaded"), Desc.bIsSpatiallyLoaded);
		Obj->SetBoolField(TEXT("editorOnly"), Desc.bActorIsEditorOnly);
		Obj->SetBoolField(TEXT("loaded"), bLoaded);

		if (Desc.Bounds.IsValid)
		{
			const FVector Min = Desc.Bounds.Min;
			const FVector Max = Desc.Bounds.Max;
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> MinObj = MCPVec3ToJsonObject(Min);
			TSharedPtr<FJsonObject> MaxObj = MCPVec3ToJsonObject(Max);
			BoundsObj->SetObjectField(TEXT("min"), MinObj);
			BoundsObj->SetObjectField(TEXT("max"), MaxObj);
			Obj->SetObjectField(TEXT("bounds"), BoundsObj);
		}

		TArray<TSharedPtr<FJsonValue>> DataLayers;
		for (const FSoftObjectPath& Layer : Desc.DataLayerAssets)
		{
			DataLayers.Add(MakeShared<FJsonValueString>(Layer.ToString()));
		}
		Obj->SetArrayField(TEXT("dataLayers"), DataLayers);
		return Obj;
	}

	bool TryReadBounds(const TSharedPtr<FJsonObject>& Params, FBox& OutBox)
	{
		const TSharedPtr<FJsonObject>* BoundsObj = nullptr;
		if (!TryGetObjectParam(Params, TEXT("bounds"), BoundsObj) || !BoundsObj) return false;
		const TSharedPtr<FJsonObject>* MinObj = nullptr;
		const TSharedPtr<FJsonObject>* MaxObj = nullptr;
		if (!(*BoundsObj)->TryGetObjectField(TEXT("min"), MinObj) || !MinObj) return false;
		if (!(*BoundsObj)->TryGetObjectField(TEXT("max"), MaxObj) || !MaxObj) return false;

		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;
		ReadVec3Fields(*MinObj, Min);
		ReadVec3Fields(*MaxObj, Max);
		// An inverted box silently matches nothing, which reads as "no actors".
		OutBox = FBox(FVector::Min(Min, Max), FVector::Max(Min, Max));
		return true;
	}

	/** Descriptors matching the caller's filters, honouring an explicit guid list. */
	bool GatherMatchingDescs(
		const TSharedPtr<FJsonObject>& Params,
		UWorld* World,
		TArray<FMCPActorDescSnapshot>& OutMatches,
		FString& OutError)
	{
		UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
		if (!WorldPartition)
		{
			OutError = TEXT("The current editor world has no World Partition");
			return false;
		}

		// UWorldPartitionBlueprintLibrary is MinimalAPI, so its statics cannot be
		// linked from another module. FWorldPartitionHelpers is exported and
		// gives the same traversal.
		TArray<FMCPActorDescSnapshot> AllDescs;
		FBox Box(ForceInit);
		const bool bHasBounds = TryReadBounds(Params, Box);
		auto Collect = [&AllDescs](const FWorldPartitionActorDescInstance* Instance) -> bool
		{
			if (Instance) AllDescs.Emplace(*Instance);
			return true;
		};
		if (bHasBounds)
		{
			FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(
				WorldPartition, Box, AActor::StaticClass(), Collect);
		}
		else
		{
			FWorldPartitionHelpers::ForEachActorDescInstance(
				WorldPartition, AActor::StaticClass(), Collect);
		}

		TSet<FString> WantedGuids;
		const TArray<TSharedPtr<FJsonValue>>* GuidValues = nullptr;
		if (TryGetArrayParam(Params, TEXT("guids"), GuidValues) && GuidValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *GuidValues)
			{
				FString Guid;
				if (Value.IsValid() && Value->TryGetString(Guid) && !Guid.IsEmpty())
				{
					WantedGuids.Add(Guid.ToLower());
				}
			}
		}

		const FString LowerFilter = OptionalString(Params, TEXT("filter")).ToLower();
		const FString ClassName = OptionalString(Params, TEXT("className")).ToLower();
		const bool bLoadedOnly = OptionalBool(Params, TEXT("loadedOnly"), false);
		const bool bUnloadedOnly = OptionalBool(Params, TEXT("unloadedOnly"), false);
		const TSet<FGuid> LoadedGuids = CollectLoadedActorGuids(World);

		for (const FMCPActorDescSnapshot& Desc : AllDescs)
		{
			if (WantedGuids.Num() > 0 && !WantedGuids.Contains(Desc.Guid.ToString().ToLower())) continue;
			if (!ActorDescMatchesFilter(Desc, LowerFilter)) continue;
			if (!ClassName.IsEmpty() && !ActorDescClassName(Desc).ToLower().Contains(ClassName)) continue;

			const bool bLoaded = LoadedGuids.Contains(Desc.Guid);
			if (bLoadedOnly && !bLoaded) continue;
			if (bUnloadedOnly && bLoaded) continue;

			OutMatches.Add(Desc);
		}
		return true;
	}

	/**
	 * The identity of the descriptor query being paged: the method name plus
	 * every parameter GatherMatchingDescs reads to decide which descriptors
	 * match. A cursor is only valid against the key that issued it, so anything
	 * left out here would let a page-2 cursor be replayed against a different
	 * filter and page one collection at another collection's boundary.
	 */
	FString BuildActorDescCollectionKey(const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		FString Guids;
		const TArray<TSharedPtr<FJsonValue>>* GuidValues = nullptr;
		if (Params.IsValid() && TryGetArrayParam(Params, TEXT("guids"), GuidValues) && GuidValues)
		{
			TArray<FString> Sorted;
			for (const TSharedPtr<FJsonValue>& Value : *GuidValues)
			{
				FString Guid;
				if (Value.IsValid() && Value->TryGetString(Guid) && !Guid.IsEmpty())
				{
					Sorted.Add(Guid.ToLower());
				}
			}
			// Sorted, because the same set of GUIDs listed in a different order
			// is the same query and should not invalidate a live cursor.
			Sorted.Sort();
			Guids = FString::Join(Sorted, TEXT(","));
		}

		FBox Box(ForceInit);
		FString Bounds;
		if (Params.IsValid() && TryReadBounds(Params, Box))
		{
			Bounds = Box.ToString();
		}

		return FString::Printf(
			TEXT("%s|filter=%s|className=%s|guids=%s|bounds=%s|loadedOnly=%d|unloadedOnly=%d"),
			Method,
			*OptionalString(Params, TEXT("filter")).ToLower(),
			*OptionalString(Params, TEXT("className")).ToLower(),
			*Guids,
			*Bounds,
			OptionalBool(Params, TEXT("loadedOnly"), false) ? 1 : 0,
			OptionalBool(Params, TEXT("unloadedOnly"), false) ? 1 : 0);
	}

	bool RequirePartitionedWorld(UWorld* World, TSharedPtr<FJsonValue>& OutError)
	{
		if (!World || !World->IsPartitionedWorld())
		{
			OutError = MCPError(TEXT("The current editor world is not a World Partition map, so it has no actor descriptors. Every actor is already spawned - use get_outliner or get_actors_by_class."));
			return false;
		}
		return true;
	}
#endif // UE_MCP_HAS_ACTOR_DESC_INSTANCE_API

#if UE_MCP_HAS_LOADED_REGION_API
	constexpr int32 DefaultMaxLoadedRegions = 8;
	constexpr int32 DefaultMaxRegionActors = 256;

	// GetBoundingBox() is a TOptional<FBox> on the engines that have it on the
	// adapter at all, and has been a bare FBox on others; both read the same.
	bool LoadedRegionBoxOf(const FBox& Value, FBox& OutBox)
	{
		// An adapter can report a set but empty box (the editor registers such adapters itself); that is no
		// region, and treating it as one would match every bounds filter that covers the origin.
		if (!Value.IsValid || Value.GetSize().GetMin() <= KINDA_SMALL_NUMBER) return false;
		OutBox = Value;
		return true;
	}

	template <typename TValue>
	bool LoadedRegionBoxOf(const TOptional<TValue>& Value, FBox& OutBox)
	{
		return Value.IsSet() && LoadedRegionBoxOf(static_cast<const FBox&>(Value.GetValue()), OutBox);
	}

	bool ReadRegionBounds(IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter, FBox& OutBox)
	{
		const TOptional<FBox> Bounds = Adapter->GetBoundingBox();
		return Bounds.IsSet() && LoadedRegionBoxOf(Bounds.GetValue(), OutBox);
	}

	// What the editor does to a user-created region after Unload(): hand the adapter object back to the
	// World Partition so the region disappears instead of lingering as an unloaded box.
	bool ReleaseUserRegion(UWorldPartition* WorldPartition, UObject* Region)
	{
		UWorldPartitionEditorLoaderAdapter* EditorAdapter = Cast<UWorldPartitionEditorLoaderAdapter>(Region);
		if (!WorldPartition || !EditorAdapter || !WorldPartition->GetRegisteredEditorLoaderAdapters().Contains(EditorAdapter)) return false;
		WorldPartition->ReleaseEditorLoaderAdapter(EditorAdapter);
		return true;
	}

	bool LoadedRegionClassIsNamed(const UClass* Class, const TCHAR* Name)
	{
		for (const UClass* Walk = Class; Walk; Walk = Walk->GetSuperClass())
		{
			if (Walk->GetName() == Name) return true;
		}
		return false;
	}

	/** One editor loader source: a user loaded region, a location volume, or any other implementer. */
	struct FMCPLoadedRegion
	{
		TWeakObjectPtr<UObject> Object;
		FString Id;
		FString Kind;
		FString Label;
		FString ClassName;
		bool bLoaded = false;
		bool bHasBounds = false;
		FBox Bounds = FBox(ForceInit);
	};

	/**
	 * Every IWorldPartitionActorLoaderInterface implementer that belongs to the
	 * edited world, sorted by id. The user loaded regions are
	 * UWorldPartitionEditorLoaderAdapter objects and a location volume is an
	 * actor; both are found the way the engine finds them, through the interface,
	 * so any other implementer the engine adds is listed without a code change.
	 * The pinned-actor adapter is a plain ILoaderAdapter owned by the World
	 * Partition rather than an interface implementer, and is what
	 * load_actor_descs mode=unpin already releases.
	 */
	TArray<FMCPLoadedRegion> GatherLoadedRegions(UWorld* World)
	{
		TArray<FMCPLoadedRegion> Regions;
		// User regions are UWorldPartitionEditorLoaderAdapter objects in the transient package, so they are read
		// from the World Partition's registry (GetRegisteredEditorLoaderAdapters), not from the world's objects;
		// location volumes and any other loader actors are actors of the edited world.
		TArray<UObject*> Sources;
		if (UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr)
		{
			for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
			{
				if (EditorAdapter) Sources.Add(EditorAdapter.Get());
			}
		}
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if (Cast<IWorldPartitionActorLoaderInterface>(*ActorIt)) Sources.Add(*ActorIt);
		}
		for (UObject* Object : Sources)
		{
			IWorldPartitionActorLoaderInterface* Loader = Cast<IWorldPartitionActorLoaderInterface>(Object);
			if (!Loader) continue;

			FMCPLoadedRegion Region;
			Region.Object = Object;
			Region.Id = Object->GetPathName();
			Region.ClassName = Object->GetClass()->GetName();

			// A location volume carries no adapter until it has been loaded once.
			auto* Adapter = Loader->GetLoaderAdapter();
			Region.bLoaded = Adapter && Adapter->IsLoaded();
			if (Adapter) Region.bHasBounds = ReadRegionBounds(Adapter, Region.Bounds);

			const AActor* Actor = Cast<AActor>(Object);
			Region.Label = Actor ? Actor->GetActorLabel() : Object->GetName();
			if (!Region.bHasBounds && Actor)
			{
				const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
				if (ActorBox.IsValid)
				{
					Region.Bounds = ActorBox;
					Region.bHasBounds = true;
				}
			}

			if (LoadedRegionClassIsNamed(Object->GetClass(), TEXT("LocationVolume"))) Region.Kind = TEXT("locationVolume");
			else if (LoadedRegionClassIsNamed(Object->GetClass(), TEXT("WorldPartitionEditorLoaderAdapter"))) Region.Kind = TEXT("userRegion");
			else if (Actor) Region.Kind = TEXT("actor");
			else Region.Kind = TEXT("other");

			Regions.Add(MoveTemp(Region));
		}
		Regions.Sort([](const FMCPLoadedRegion& A, const FMCPLoadedRegion& B) { return A.Id < B.Id; });
		return Regions;
	}

	/** Actors spawned in the editor world, by GUID, so a descriptor can be matched to its live actor. */
	TMap<FGuid, AActor*> CollectLoadedActorsByGuid(UWorld* World)
	{
		TMap<FGuid, AActor*> Loaded;
		if (!World) return Loaded;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			const FGuid Guid = Actor->GetActorGuid();
			if (Guid.IsValid()) Loaded.Add(Guid, Actor);
		}
		return Loaded;
	}

	/**
	 * The spatially loaded actor descriptors whose bounds meet a box: what a
	 * loader adapter over that box holds. The adapter keeps its own reference
	 * set and does not expose it, so the footprint is measured from the
	 * descriptors instead. An always-loaded actor is not held by any region and
	 * is left out.
	 */
	void CollectRegionFootprint(UWorldPartition* WorldPartition, const FBox& Box, TSet<FGuid>& OutGuids)
	{
		auto Collect = [&OutGuids](const FWorldPartitionActorDescInstance* Instance) -> bool
		{
			if (Instance && Instance->GetIsSpatiallyLoaded()) OutGuids.Add(Instance->GetGuid());
			return true;
		};
		FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(WorldPartition, Box, AActor::StaticClass(), Collect);
	}

	/** Dirty external packages among the resident actors of a footprint. */
	void CollectDirtyFootprintPackages(
		const TSet<FGuid>& Footprint, const TMap<FGuid, AActor*>& LoadedByGuid, TSet<FString>& OutPackages)
	{
		for (const FGuid& Guid : Footprint)
		{
			AActor* const* Found = LoadedByGuid.Find(Guid);
			if (!Found || !*Found) continue;
			// Only an actor with its own package can be lost on its own; one saved
			// inside the map package is not released by a region.
			const UPackage* Package = (*Found)->GetExternalPackage();
			if (Package && Package->IsDirty()) OutPackages.Add(Package->GetName());
		}
	}

	int32 CountResident(const TSet<FGuid>& Footprint, const TMap<FGuid, AActor*>& LoadedByGuid)
	{
		int32 Resident = 0;
		for (const FGuid& Guid : Footprint)
		{
			if (LoadedByGuid.Contains(Guid)) ++Resident;
		}
		return Resident;
	}

	TSharedPtr<FJsonObject> LoadedRegionBoundsJson(const FBox& Box)
	{
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> MinObj = MCPVec3ToJsonObject(Box.Min);
		TSharedPtr<FJsonObject> MaxObj = MCPVec3ToJsonObject(Box.Max);
		BoundsObj->SetObjectField(TEXT("min"), MinObj);
		BoundsObj->SetObjectField(TEXT("max"), MaxObj);
		return BoundsObj;
	}

	TSharedPtr<FJsonObject> LoadedRegionToJson(
		const FMCPLoadedRegion& Region, UWorldPartition* WorldPartition, const TMap<FGuid, AActor*>& LoadedByGuid)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), Region.Id);
		Obj->SetStringField(TEXT("kind"), Region.Kind);
		Obj->SetStringField(TEXT("label"), Region.Label);
		Obj->SetStringField(TEXT("class"), Region.ClassName);
		Obj->SetBoolField(TEXT("loaded"), Region.bLoaded);
		Obj->SetBoolField(TEXT("boundsKnown"), Region.bHasBounds);
		if (Region.bHasBounds)
		{
			Obj->SetObjectField(TEXT("bounds"), LoadedRegionBoundsJson(Region.Bounds));
			TSet<FGuid> Footprint;
			CollectRegionFootprint(WorldPartition, Region.Bounds, Footprint);
			TSet<FString> DirtyPackages;
			CollectDirtyFootprintPackages(Footprint, LoadedByGuid, DirtyPackages);
			Obj->SetNumberField(TEXT("actorCount"), Footprint.Num());
			Obj->SetNumberField(TEXT("residentActors"), CountResident(Footprint, LoadedByGuid));
			Obj->SetNumberField(TEXT("dirtyActorPackages"), DirtyPackages.Num());
		}
		return Obj;
	}
#endif // UE_MCP_HAS_LOADED_REGION_API
}

TSharedPtr<FJsonValue> FLevelHandlers::ListActorDescs(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("filter"), TEXT("className"), TEXT("guids"), TEXT("bounds"), TEXT("loadedOnly"), TEXT("unloadedOnly"),
		TEXT("cursor"), TEXT("limit"),
	});

#if UE_MCP_HAS_ACTOR_DESC_INSTANCE_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Err;
	if (!RequirePartitionedWorld(World, Err)) return Err;

	// T3: paged. This cut the list at 'limit' and told the caller to raise it or
	// narrow the filter, which on a map with 40,000 descriptors is an answer
	// nobody can act on. The cursor walks the whole set instead.
	MCPPagination::FPageRequest Page;
	if (auto PageErr = MCPPagination::ReadPageRequest(
			Params,
			BuildActorDescCollectionKey(TEXT("list_actor_descs"), Params),
			/*DefaultLimit*/ DefaultActorDescLimit, /*MaxLimit*/ 5000, Page))
	{
		return PageErr;
	}

	TArray<FMCPActorDescSnapshot> Matches;
	FString Error;
	if (!GatherMatchingDescs(Params, World, Matches, Error))
	{
		return MCPError(Error);
	}

	const TSet<FGuid> LoadedGuids = CollectLoadedActorGuids(World);

	int32 LoadedCount = 0;
	for (const FMCPActorDescSnapshot& Desc : Matches)
	{
		if (LoadedGuids.Contains(Desc.Guid)) ++LoadedCount;
	}

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Matches.Num());
	for (const FMCPActorDescSnapshot& Desc : Matches)
	{
		// The actor GUID is the anchor. It is the descriptor's durable identity:
		// it survives a rename, a move between packages and a relabel, all of
		// which a path or a label would report as a different row.
		Rows.Add({
			Desc.Guid.ToString(),
			MakeShared<FJsonValueObject>(ActorDescToJson(Desc, LoadedGuids.Contains(Desc.Guid))),
		});
	}

	// ForEachActorDescInstance walks the partition's descriptor containers,
	// whose traversal order is not a contract, so the rows are sorted before
	// paging. A cursor over an unordered enumeration is not resumable.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	Result->SetNumberField(TEXT("loadedMatches"), LoadedCount);
	Result->SetNumberField(TEXT("unloadedMatches"), Matches.Num() - LoadedCount);
	MCPPagination::EmitPage(Page, Rows, TEXT("actorDescs"), Result);
	Result->SetNumberField(TEXT("returned"), Result->GetIntegerField(TEXT("count")));
	return MCPResult(Result);
#elif WITH_EDITOR
	return MCPError(TEXT("list_actor_descs needs the World Partition actor descriptor instance API, which is UE 5.5 and newer. On UE 5.4 use get_outliner or get_actors_by_class, which only see actors that are already streamed in."));
#else
	return MCPError(TEXT("World Partition actor descriptors are editor-only"));
#endif
}

TSharedPtr<FJsonValue> FLevelHandlers::LoadActorDescs(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("mode"), TEXT("filter"), TEXT("className"), TEXT("guids"), TEXT("bounds"), TEXT("loadedOnly"),
		TEXT("unloadedOnly"), TEXT("maxActors"), TEXT("dryRun"),
	});

#if UE_MCP_HAS_ACTOR_DESC_INSTANCE_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Err;
	if (!RequirePartitionedWorld(World, Err)) return Err;

	// Pinning is the exported, durable way to make an unloaded actor resident:
	// it keeps the actor loaded regardless of the current streaming sources,
	// which is exactly what a measurement or edit pass needs. (The engine's
	// transient "load" path lives on a MinimalAPI Blueprint library that cannot
	// be linked from here, and would be dropped again by streaming anyway.)
	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("pin")).ToLower();
	if (Mode != TEXT("pin") && Mode != TEXT("unpin"))
	{
		return MCPError(TEXT("'mode' must be 'pin' (make resident) or 'unpin' (release)"));
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return MCPError(TEXT("The current editor world has no World Partition"));
	}

	TArray<FMCPActorDescSnapshot> Matches;
	FString Error;
	if (!GatherMatchingDescs(Params, World, Matches, Error))
	{
		return MCPError(Error);
	}

	if (Matches.Num() == 0)
	{
		auto Empty = MCPSuccess();
		Empty->SetStringField(TEXT("mode"), Mode);
		Empty->SetNumberField(TEXT("matched"), 0);
		Empty->SetNumberField(TEXT("affected"), 0);
		Empty->SetStringField(TEXT("note"), TEXT("No actor descriptor matched. Use list_actor_descs to see what the map contains."));
		Empty->SetBoolField(TEXT("changed"), false);
		Empty->SetBoolField(TEXT("unchanged"), true);
		MCPSetNoRollback(Empty, TEXT("No actor matched, so no pin state changed and there is nothing to undo."));
		return MCPResult(Empty);
	}

	// Refuse to stream the whole map in by accident: an unfiltered call on a
	// large World Partition level would load thousands of actors.
	const int32 MaxAffected = FMath::Max(1, OptionalInt(Params, TEXT("maxActors"), 256));
	if (Matches.Num() > MaxAffected)
	{
		return MCPError(FString::Printf(
			TEXT("%d descriptors matched, above the %d limit for a single %s. Narrow 'filter'/'className'/'bounds' or raise 'maxActors' deliberately."),
			Matches.Num(), MaxAffected, *Mode));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const TSet<FGuid> ResidentBefore = CollectLoadedActorGuids(World);
	TArray<FGuid> Guids;
	TArray<TSharedPtr<FJsonValue>> Affected;
	Guids.Reserve(Matches.Num());
	for (const FMCPActorDescSnapshot& Desc : Matches)
	{
		Guids.Add(Desc.Guid);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("guid"), Desc.Guid.ToString());
		Entry->SetStringField(TEXT("label"), Desc.Label.ToString());
		Entry->SetStringField(TEXT("class"), ActorDescClassName(Desc));
		Affected.Add(MakeShared<FJsonValueObject>(Entry));
	}

	// Pin state before the call, so the inverse touches only the actors this
	// call flipped and never releases a pin someone else placed.
	TSet<FGuid> PinnedBefore;
	for (const FGuid& Guid : Guids)
	{
		if (WorldPartition->IsActorPinned(Guid)) PinnedBefore.Add(Guid);
	}

	if (!bDryRun)
	{
		if (Mode == TEXT("pin")) WorldPartition->PinActors(Guids);
		else                     WorldPartition->UnpinActors(Guids);
	}

	TArray<TSharedPtr<FJsonValue>> FlippedGuids;
	for (const FGuid& Guid : Guids)
	{
		if (WorldPartition->IsActorPinned(Guid) != PinnedBefore.Contains(Guid))
		{
			FlippedGuids.Add(MakeShared<FJsonValueString>(Guid.ToString()));
		}
	}

	// Read the world back so the caller sees what actually became resident
	// rather than trusting the request.
	const TSet<FGuid> NowLoaded = CollectLoadedActorGuids(World);
	int32 ResidentAfter = 0;
	for (const FGuid& Guid : Guids)
	{
		if (NowLoaded.Contains(Guid)) ++ResidentAfter;
	}
	// A real delta: how many actually changed residency. Without the
	// before-snapshot, an already-loaded actor counted as "affected" by a call
	// that did nothing.
	int32 Changed = 0;
	for (const FGuid& Guid : Guids)
	{
		const bool bWas = ResidentBefore.Contains(Guid);
		const bool bNow = NowLoaded.Contains(Guid);
		if (bWas != bNow) ++Changed;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	// Report what actually changed, not what was requested: PinActors is a
	// no-op when the world partition has no pinned-actor adapter.
	Result->SetNumberField(TEXT("requested"), Guids.Num());
	Result->SetNumberField(TEXT("affected"), bDryRun ? 0 : Changed);
	Result->SetNumberField(TEXT("residentAfter"), ResidentAfter);
	Result->SetArrayField(TEXT("actors"), Affected);
	Result->SetNumberField(TEXT("pinStateChanged"), FlippedGuids.Num());

	const bool bChanged = FlippedGuids.Num() > 0;
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("changed"), bChanged);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	if (bChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("mode"), Mode == TEXT("pin") ? TEXT("unpin") : TEXT("pin"));
		Payload->SetArrayField(TEXT("guids"), FlippedGuids);
		Payload->SetNumberField(TEXT("maxActors"), FMath::Max(MaxAffected, FlippedGuids.Num()));
		MCPSetRollback(Result, TEXT("load_actor_descs"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, bDryRun
			? TEXT("Dry run: no pin state was changed, so there is nothing to undo.")
			: TEXT("Every matched actor already had the requested pin state, so nothing changed and there is nothing to undo."));
	}
	return MCPResult(Result);
#elif WITH_EDITOR
	return MCPError(TEXT("load_actor_descs needs the World Partition actor descriptor instance API, which is UE 5.5 and newer. On UE 5.4 load the cell from the World Partition editor window instead."));
#else
	return MCPError(TEXT("World Partition streaming control is editor-only"));
#endif
}

// ---- Editor loaded regions ---------------------------------------------------
//
// load_actor_descs(mode=unpin) releases pins and nothing else, so an actor that
// is resident because the World Partition editor loaded a region (Load Region
// from Selection, or a region the editor restored when the map was reopened) or
// a location volume is loaded stays resident. These three actions drive the same
// loader adapters the editor window does.

#if !UE_MCP_HAS_LOADED_REGION_API
namespace
{
	TSharedPtr<FJsonValue> LoadedRegionsUnavailable(const TCHAR* Action)
	{
#if UE_MCP_HAS_ACTOR_DESC_INSTANCE_API
		return MCPError(FString::Printf(
			TEXT("%s needs the World Partition editor loader interface (IWorldPartitionActorLoaderInterface), and no header for it was found in this engine build. Unload the region from the World Partition editor window instead."),
			Action));
#elif WITH_EDITOR
		return MCPError(FString::Printf(
			TEXT("%s needs the World Partition actor descriptor instance API, which is UE 5.5 and newer. On UE 5.4 use the World Partition editor window."),
			Action));
#else
		return MCPError(FString::Printf(TEXT("%s is editor-only"), Action));
#endif
	}
}
#endif

TSharedPtr<FJsonValue> FLevelHandlers::ListLoadedRegions(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("includeUnloaded"), TEXT("bounds"), TEXT("cursor"), TEXT("limit") });

#if UE_MCP_HAS_LOADED_REGION_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Err;
	if (!RequirePartitionedWorld(World, Err)) return Err;
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return MCPError(TEXT("The current editor world has no World Partition"));
	}

	const bool bIncludeUnloaded = OptionalBool(Params, TEXT("includeUnloaded"), false);
	FBox Filter(ForceInit);
	const bool bHasFilter = TryReadBounds(Params, Filter);

	MCPPagination::FPageRequest Page;
	if (auto PageErr = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_loaded_regions|includeUnloaded=%d|bounds=%s"),
				bIncludeUnloaded ? 1 : 0, bHasFilter ? *Filter.ToString() : TEXT("")),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return PageErr;
	}

	const TMap<FGuid, AActor*> LoadedByGuid = CollectLoadedActorsByGuid(World);
	const TArray<FMCPLoadedRegion> Regions = GatherLoadedRegions(World);

	int32 LoadedCount = 0;
	int32 Matched = 0;
	TArray<MCPPagination::FPageRow> Rows;
	for (const FMCPLoadedRegion& Region : Regions)
	{
		if (Region.bLoaded) ++LoadedCount;
		if (!Region.bLoaded && !bIncludeUnloaded) continue;
		if (bHasFilter && (!Region.bHasBounds || !Region.Bounds.Intersect(Filter))) continue;
		++Matched;
		// The object path is the anchor: it is what unload_loaded_regions accepts
		// and it does not change while the editor session lives.
		Rows.Add({ Region.Id, MakeShared<FJsonValueObject>(LoadedRegionToJson(Region, WorldPartition, LoadedByGuid)) });
	}
	// GatherLoadedRegions sorts by id; the sort is repeated here because the
	// cursor contract is the row order, not an accident of the producer.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetNumberField(TEXT("loadedRegions"), LoadedCount);
	Result->SetNumberField(TEXT("totalSources"), Regions.Num());
	MCPPagination::EmitPage(Page, Rows, TEXT("regions"), Result);
	Result->SetNumberField(TEXT("returned"), Result->GetIntegerField(TEXT("count")));
	return MCPResult(Result);
#else
	return LoadedRegionsUnavailable(TEXT("list_loaded_regions"));
#endif
}

TSharedPtr<FJsonValue> FLevelHandlers::UnloadLoadedRegions(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("ids"), TEXT("bounds"), TEXT("maxRegions"), TEXT("force"), TEXT("dryRun"),
	});

#if UE_MCP_HAS_LOADED_REGION_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Err;
	if (!RequirePartitionedWorld(World, Err)) return Err;
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return MCPError(TEXT("The current editor world has no World Partition"));
	}

	TSet<FString> WantedIds;
	const TArray<TSharedPtr<FJsonValue>>* IdValues = nullptr;
	if (TryGetArrayParam(Params, TEXT("ids"), IdValues) && IdValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *IdValues)
		{
			FString Id;
			if (Value.IsValid() && Value->TryGetString(Id) && !Id.IsEmpty()) WantedIds.Add(Id);
		}
	}
	FBox Selection(ForceInit);
	const bool bHasBounds = TryReadBounds(Params, Selection);

	// A bare call must not unload everything the editor has loaded.
	if (WantedIds.Num() == 0 && !bHasBounds)
	{
		return MCPError(TEXT("Name the regions to unload: 'ids' from list_loaded_regions, 'bounds' as {min:{x,y,z}, max:{x,y,z}}, or both (a region must then match both). Nothing was unloaded."));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bForce = OptionalBool(Params, TEXT("force"), false);
	const int32 MaxRegions = FMath::Max(1, OptionalInt(Params, TEXT("maxRegions"), DefaultMaxLoadedRegions));

	const TArray<FMCPLoadedRegion> All = GatherLoadedRegions(World);
	TSet<FString> KnownIds;
	for (const FMCPLoadedRegion& Region : All) KnownIds.Add(Region.Id);

	TArray<FString> MissingIds;
	for (const FString& Id : WantedIds)
	{
		if (!KnownIds.Contains(Id)) MissingIds.Add(Id);
	}
	MissingIds.Sort();

	TArray<const FMCPLoadedRegion*> Matched;
	TArray<const FMCPLoadedRegion*> ToUnload;
	for (const FMCPLoadedRegion& Region : All)
	{
		if (WantedIds.Num() > 0 && !WantedIds.Contains(Region.Id)) continue;
		if (bHasBounds && (!Region.bHasBounds || !Region.Bounds.Intersect(Selection))) continue;
		Matched.Add(&Region);
		if (Region.bLoaded) ToUnload.Add(&Region);
	}

	if (ToUnload.Num() > MaxRegions)
	{
		return MCPError(FString::Printf(
			TEXT("%d loaded regions matched, above the %d limit for a single unload_loaded_regions. Narrow 'ids'/'bounds' or raise 'maxRegions' deliberately. Nothing was unloaded."),
			ToUnload.Num(), MaxRegions));
	}

	// What the selected regions hold, measured before anything changes.
	const TMap<FGuid, AActor*> LoadedBefore = CollectLoadedActorsByGuid(World);
	TSet<FGuid> Footprint;
	for (const FMCPLoadedRegion* Region : Matched)
	{
		if (Region->bHasBounds) CollectRegionFootprint(WorldPartition, Region->Bounds, Footprint);
	}

	// Preflight: unloading destroys the live actor, so an actor with unsaved
	// changes would lose them. Refuse the whole request before touching any region.
	TSet<FString> DirtyPackages;
	TArray<FString> DirtyRegionIds;
	TArray<FString> UnverifiableRegionIds;
	for (const FMCPLoadedRegion* Region : ToUnload)
	{
		if (!Region->bHasBounds)
		{
			UnverifiableRegionIds.Add(Region->Id);
			continue;
		}
		TSet<FGuid> RegionFootprint;
		CollectRegionFootprint(WorldPartition, Region->Bounds, RegionFootprint);
		TSet<FString> RegionDirty;
		CollectDirtyFootprintPackages(RegionFootprint, LoadedBefore, RegionDirty);
		if (RegionDirty.Num() > 0) DirtyRegionIds.Add(Region->Id);
		DirtyPackages.Append(RegionDirty);
	}
	if (!bForce && (DirtyPackages.Num() > 0 || UnverifiableRegionIds.Num() > 0))
	{
		TArray<FString> DirtyList = DirtyPackages.Array();
		DirtyList.Sort();
		TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
			TEXT("%d region(s) hold actors with unsaved changes (%d package(s)) or have no bounds to check them against; unloading would discard those edits. Save them with level(save), or pass force=true to unload anyway. Nothing was unloaded."),
			DirtyRegionIds.Num() + UnverifiableRegionIds.Num(), DirtyList.Num()));
		Refusal->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyList));
		Refusal->SetArrayField(TEXT("dirtyRegionIds"), MCPStringListToJson(DirtyRegionIds));
		Refusal->SetArrayField(TEXT("unverifiableRegionIds"), MCPStringListToJson(UnverifiableRegionIds));
		return MCPResult(Refusal);
	}

	TArray<TSharedPtr<FJsonValue>> RegionEntries;
	TArray<TSharedPtr<FJsonValue>> AlreadyUnloaded;
	int32 UnloadedCount = 0;
	int32 ReleasedCount = 0;
	for (const FMCPLoadedRegion* Region : Matched)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Region->Id);
		Entry->SetStringField(TEXT("kind"), Region->Kind);
		Entry->SetStringField(TEXT("label"), Region->Label);
		Entry->SetBoolField(TEXT("loadedBefore"), Region->bLoaded);
		if (Region->bHasBounds) Entry->SetObjectField(TEXT("bounds"), LoadedRegionBoundsJson(Region->Bounds));

		if (!Region->bLoaded)
		{
			AlreadyUnloaded.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		bool bUnloaded = false;
		bool bReleased = false;
		if (!bDryRun)
		{
			UObject* Object = Region->Object.Get();
			IWorldPartitionActorLoaderInterface* Loader = Object ? Cast<IWorldPartitionActorLoaderInterface>(Object) : nullptr;
			auto* Adapter = Loader ? Loader->GetLoaderAdapter() : nullptr;
			if (Adapter)
			{
				// Unload() drops the adapter's actor references, which is what lets the
				// World Partition destroy the actors nothing else holds.
				Adapter->Unload();
				bUnloaded = !Adapter->IsLoaded();
				// The editor also hands a user-created region back to the World Partition,
				// so the box does not linger as an unloaded region. A location volume is
				// an actor in the level and stays where it is.
				if (bUnloaded && Region->Kind == TEXT("userRegion"))
				{
					bReleased = ReleaseUserRegion(WorldPartition, Object);
				}
			}
		}
		if (bUnloaded) ++UnloadedCount;
		if (bReleased) ++ReleasedCount;
		Entry->SetBoolField(TEXT("unloaded"), bUnloaded);
		Entry->SetBoolField(TEXT("released"), bReleased);
		RegionEntries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	// Read the world back, so the caller sees what actually left memory rather
	// than trusting the request.
	const TMap<FGuid, AActor*> LoadedAfter = CollectLoadedActorsByGuid(World);
	const int32 ResidentBefore = CountResident(Footprint, LoadedBefore);
	const int32 ResidentAfter = CountResident(Footprint, LoadedAfter);
	int32 PinnedAfter = 0;
	for (const FGuid& Guid : Footprint)
	{
		if (LoadedAfter.Contains(Guid) && WorldPartition->IsActorPinned(Guid)) ++PinnedAfter;
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("forced"), bForce);
	Result->SetNumberField(TEXT("matchedRegions"), Matched.Num());
	Result->SetNumberField(TEXT("loadedRegionsMatched"), ToUnload.Num());
	Result->SetNumberField(TEXT("unloadedRegions"), UnloadedCount);
	Result->SetNumberField(TEXT("releasedRegions"), ReleasedCount);
	Result->SetArrayField(TEXT("regions"), RegionEntries);
	Result->SetArrayField(TEXT("alreadyUnloaded"), AlreadyUnloaded);
	Result->SetArrayField(TEXT("missingIds"), MCPStringListToJson(MissingIds));
	Result->SetNumberField(TEXT("footprintActors"), Footprint.Num());
	Result->SetNumberField(TEXT("residentBefore"), ResidentBefore);
	Result->SetNumberField(TEXT("residentAfter"), ResidentAfter);
	Result->SetNumberField(TEXT("residentPinnedAfter"), PinnedAfter);
	Result->SetNumberField(TEXT("worldActorsBefore"), LoadedBefore.Num());
	Result->SetNumberField(TEXT("worldActorsAfter"), LoadedAfter.Num());
	if (DirtyPackages.Num() > 0)
	{
		TArray<FString> DirtyList = DirtyPackages.Array();
		DirtyList.Sort();
		Result->SetArrayField(TEXT("discardedDirtyPackages"), MCPStringListToJson(DirtyList));
	}
	if (ResidentAfter > 0 && !bDryRun)
	{
		Result->SetStringField(TEXT("residentNote"),
			TEXT("Some actors inside the unloaded regions are still resident: another loader holds them (a pin, which residentPinnedAfter counts, or another region or location volume that overlaps), or the editor's streaming does. List them with list_loaded_regions."));
	}
	if (MissingIds.Num() > 0)
	{
		Result->SetStringField(TEXT("missingIdsNote"),
			TEXT("These ids named no region in the edited world. Either they were already released (a user region disappears once unloaded) or the id is stale; list_loaded_regions returns the current ones."));
	}

	const bool bChanged = UnloadedCount > 0;
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("changed"), bChanged);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	MCPSetNoRollback(Result, bChanged
		? TEXT("An unloaded user region is released, so its id is gone, and level(load_region) would create a new region over the same bounds with a different id rather than restore the old one; a location volume is loaded again from the World Partition editor. The bounds in 'regions' are what load_region needs, but the call is a new action, not an inverse of this one.")
		: TEXT("Nothing was unloaded (a dry run, or every matched region was already unloaded), so there is nothing to undo."));
	return MCPResult(Result);
#else
	return LoadedRegionsUnavailable(TEXT("unload_loaded_regions"));
#endif
}

TSharedPtr<FJsonValue> FLevelHandlers::LoadRegion(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("bounds"), TEXT("label"), TEXT("maxActors"), TEXT("dryRun") });

#if UE_MCP_HAS_LOADED_REGION_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Err;
	if (!RequirePartitionedWorld(World, Err)) return Err;
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return MCPError(TEXT("The current editor world has no World Partition"));
	}

	FBox Box(ForceInit);
	if (!TryReadBounds(Params, Box))
	{
		return MCPError(TEXT("'bounds' is required, as {min:{x,y,z}, max:{x,y,z}}"));
	}
	// The editor's own default name for a region made from a selection.
	const FString Label = OptionalString(Params, TEXT("label"), TEXT("Loaded Region"));
	const int32 MaxActors = FMath::Max(1, OptionalInt(Params, TEXT("maxActors"), DefaultMaxRegionActors));
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	TSet<FGuid> Footprint;
	CollectRegionFootprint(WorldPartition, Box, Footprint);
	if (Footprint.Num() > MaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("%d actors fall inside those bounds, above the %d limit for a single load_region. Narrow 'bounds' or raise 'maxActors' deliberately."),
			Footprint.Num(), MaxActors));
	}

	const TArray<FMCPLoadedRegion> Before = GatherLoadedRegions(World);
	TSet<FString> IdsBefore;
	for (const FMCPLoadedRegion& Region : Before)
	{
		IdsBefore.Add(Region.Id);
		// A loaded user region over the same box is the state asked for.
		if (Region.bLoaded && Region.Kind == TEXT("userRegion") && Region.bHasBounds
			&& Region.Bounds.Min.Equals(Box.Min, 1.0) && Region.Bounds.Max.Equals(Box.Max, 1.0))
		{
			auto Existing = MCPSuccess();
			MCPSetExisted(Existing);
			Existing->SetStringField(TEXT("id"), Region.Id);
			Existing->SetStringField(TEXT("label"), Region.Label);
			Existing->SetBoolField(TEXT("loaded"), true);
			Existing->SetObjectField(TEXT("bounds"), LoadedRegionBoundsJson(Region.Bounds));
			Existing->SetNumberField(TEXT("actorCount"), Footprint.Num());
			Existing->SetNumberField(TEXT("residentAfter"), CountResident(Footprint, CollectLoadedActorsByGuid(World)));
			Existing->SetBoolField(TEXT("changed"), false);
			Existing->SetBoolField(TEXT("unchanged"), true);
			MCPSetNoRollback(Existing, TEXT("A loaded region over these bounds already existed, so nothing was created and there is nothing to undo."));
			return MCPResult(Existing);
		}
	}

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("wouldCreate"), true);
		Preview->SetStringField(TEXT("label"), Label);
		Preview->SetObjectField(TEXT("bounds"), LoadedRegionBoundsJson(Box));
		Preview->SetNumberField(TEXT("actorCount"), Footprint.Num());
		Preview->SetNumberField(TEXT("residentBefore"), CountResident(Footprint, CollectLoadedActorsByGuid(World)));
		Preview->SetBoolField(TEXT("changed"), false);
		Preview->SetBoolField(TEXT("unchanged"), true);
		MCPSetNoRollback(Preview, TEXT("Dry run: no region was created, so there is nothing to undo."));
		return MCPResult(Preview);
	}

	// Load Region from Selection: a shape adapter over the box, marked as the user's, then loaded
	// (SWorldPartitionEditorGrid2D::CreateRegionFromSelection).
	UWorldPartitionEditorLoaderAdapter* EditorAdapter = WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, Box, Label);
	auto* Adapter = EditorAdapter ? EditorAdapter->GetLoaderAdapter() : nullptr;
	if (!Adapter)
	{
		return MCPError(TEXT("The World Partition did not create a loader adapter for the region, so nothing was loaded."));
	}
	Adapter->SetUserCreated(true);
	Adapter->Load();
	const FMCPLoadedRegion* Created = nullptr;
	const TArray<FMCPLoadedRegion> After = GatherLoadedRegions(World);
	for (const FMCPLoadedRegion& Region : After)
	{
		if (Region.Object.Get() == EditorAdapter)
		{
			Created = &Region;
			break;
		}
	}
	if (!Created)
	{
		return MCPError(TEXT("The new region was created and loaded but is not listed among the World Partition's registered loader adapters."));
	}

	const FString CreatedId = Created->Id;
	const bool bLoaded = Adapter->IsLoaded();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("id"), CreatedId);
	Result->SetStringField(TEXT("kind"), Created->Kind);
	Result->SetStringField(TEXT("label"), Label);
	Result->SetBoolField(TEXT("loaded"), bLoaded);
	Result->SetObjectField(TEXT("bounds"), LoadedRegionBoundsJson(Box));
	Result->SetNumberField(TEXT("actorCount"), Footprint.Num());
	Result->SetNumberField(TEXT("residentAfter"), CountResident(Footprint, CollectLoadedActorsByGuid(World)));
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetBoolField(TEXT("unchanged"), false);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RollbackIds;
	RollbackIds.Add(MakeShared<FJsonValueString>(CreatedId));
	Payload->SetArrayField(TEXT("ids"), RollbackIds);
	MCPSetRollback(Result, TEXT("unload_loaded_regions"), Payload);
	return MCPResult(Result);
#else
	return LoadedRegionsUnavailable(TEXT("load_region"));
#endif
}
