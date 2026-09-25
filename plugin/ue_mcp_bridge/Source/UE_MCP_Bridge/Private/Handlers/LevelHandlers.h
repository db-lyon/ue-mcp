#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FLevelHandlers
{
public:
	// Register all level handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

	// Shared by the handler and its focused native automation test.
	static int32 ClearBlueprintGraphNodes(
		class UBlueprint* Blueprint,
		bool bDryRun,
		TArray<TSharedPtr<FJsonValue>>& OutGraphs);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> GetOutliner(const TSharedPtr<FJsonObject>& Params);
	// #717: bulk set editor-only visibility (temporarily hidden in editor)
	static TSharedPtr<FJsonValue> SetEditorVisibility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaceActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActorDetails(const TSharedPtr<FJsonObject>& Params);
	// #240/#241/#302/#320/#370/#353: deep component-tree introspection - per-component
	// attach topology + transforms + collision + mesh/material refs + reflected properties.
	static TSharedPtr<FJsonValue> GetComponentTree(const TSharedPtr<FJsonObject>& Params);
	// #386/#387: relative transform between two actors (target's transform
	// expressed in reference's local space).
	static TSharedPtr<FJsonValue> GetRelativeTransform(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCurrentLevel(const TSharedPtr<FJsonObject>& Params);
	// #964: save the level currently being edited, through the same package
	// path editor(save_dirty) uses, and report what happened to each package
	// rather than one bare boolean. LevelHandlers_Save.cpp.
	static TSharedPtr<FJsonValue> SaveLevel(const TSharedPtr<FJsonObject>& Params);
	// #985: World Partition streaming settings (CellSize / LoadingRange) and the
	// runtime cell transformer stack, whose instanced transformer object had no
	// writable path at all. LevelHandlers_WorldPartitionSettings.cpp.
	static TSharedPtr<FJsonValue> GetWorldPartitionSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWorldPartitionSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddRuntimeCellTransformer(const TSharedPtr<FJsonObject>& Params);
	// #985: bulk HLOD layer assignment, with the other selector-driven batch
	// writes in LevelHandlers_BatchWrite.cpp.
	static TSharedPtr<FJsonValue> SetActorHLODLayer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLevels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSelectedActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListVolumes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveActor(const TSharedPtr<FJsonObject>& Params);
	// #566 point an actor at a target point or actor (computed look-at)
	static TSharedPtr<FJsonValue> AimActorAt(const TSharedPtr<FJsonObject>& Params);
	// #585 project a world point onto the navmesh
	static TSharedPtr<FJsonValue> NavProjectPoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SelectActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnLight(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLightProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddComponentToActor(const TSharedPtr<FJsonObject>& Params);
	// #426: symmetric inverse of add_component_to_actor.
	static TSharedPtr<FJsonValue> RemoveComponentFromActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LoadLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearLevelScript(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> NudgeComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetComponentDetails(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetVolumeProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWorldSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWorldSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorMaterial(const TSharedPtr<FJsonObject>& Params);
	// #94: Fog + sky helpers
	static TSharedPtr<FJsonValue> SetFogProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActorsByClass(const TSharedPtr<FJsonObject>& Params);
	// #582 find actors that own a component of a given class
	static TSharedPtr<FJsonValue> GetActorsByComponentClass(const TSharedPtr<FJsonObject>& Params);
	// Aggregate loaded static-mesh usage in one bounded, read-only world scan.
	static TSharedPtr<FJsonValue> SummarizeStaticMeshUsage(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #146 - actor class histogram (counts by class name)
	static TSharedPtr<FJsonValue> CountActorsByClass(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #150 - RuntimeVirtualTextureVolume / component summary
	static TSharedPtr<FJsonValue> GetRVTSummary(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #151 - set WaterBodyComponent property via runtime class lookup
	static TSharedPtr<FJsonValue> SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Params);
	// #1156: rebuild a WaterZone until its quad tree settles. LevelHandlers_Water.cpp.
	static TSharedPtr<FJsonValue> RebuildWaterZone(const TSharedPtr<FJsonObject>& Params);
	// #1156: WaterZone and WaterBody read-back. LevelHandlers_Water.cpp.
	static TSharedPtr<FJsonValue> GetWaterState(const TSharedPtr<FJsonObject>& Params);
	// #188: get actor origin + extent bounds
	static TSharedPtr<FJsonValue> GetActorBounds(const TSharedPtr<FJsonObject>& Params);
	// #178: resolve actor by internal/runtime UObject name
	static TSharedPtr<FJsonValue> ResolveActor(const TSharedPtr<FJsonObject>& Params);
	// #202/#230: generic per-instance UPROPERTY writer for level actors
	static TSharedPtr<FJsonValue> SetActorProperty(const TSharedPtr<FJsonObject>& Params);
	// #220: bulk delete actors by label prefix / class / tag
	static TSharedPtr<FJsonValue> DeleteActors(const TSharedPtr<FJsonObject>& Params);
	// Safely delete actors with exact editor labels across explicit level
	// packages. Defaults to a dry run and saves only levels changed by a
	// committed request.
	static TSharedPtr<FJsonValue> DeleteExactLabeledActorsInLevels(const TSharedPtr<FJsonObject>& Params);
	// #767: bulk-assign World Outliner folder paths in one transaction.
	static TSharedPtr<FJsonValue> SetActorFolderPath(const TSharedPtr<FJsonObject>& Params);
	// #746: World Partition actor descriptors - see unloaded actors and stream
	// them in, instead of every actor query silently reporting zero for them.
	static TSharedPtr<FJsonValue> ListActorDescs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LoadActorDescs(const TSharedPtr<FJsonObject>& Params);
	// #219: actor tag CRUD
	static TSharedPtr<FJsonValue> AddActorTag(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveActorTag(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorTags(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListActorTags(const TSharedPtr<FJsonObject>& Params);
	// #205: actor attach/detach + mobility
	static TSharedPtr<FJsonValue> AttachActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DetachActor(const TSharedPtr<FJsonObject>& Params);
	// Attach an exact named/root SceneComponent to an exact named/root parent
	// SceneComponent, optionally at a validated socket.
	static TSharedPtr<FJsonValue> AttachComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DetachComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorMobility(const TSharedPtr<FJsonObject>& Params);
	// #204: edit-level current sub-level
	static TSharedPtr<FJsonValue> GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params);
	// #206: streaming sub-level CRUD on the persistent world
	static TSharedPtr<FJsonValue> ListStreamingSublevels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddStreamingSublevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Params);
	// #203: batch spawn / batch transform
	static TSharedPtr<FJsonValue> SpawnGrid(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BatchTranslate(const TSharedPtr<FJsonObject>& Params);
	// #264: explicit per-instance batch spawn (mesh+transform per actor)
	static TSharedPtr<FJsonValue> PlaceActorsBatch(const TSharedPtr<FJsonObject>& Params);
	// #420: raycast + #419 snap-to-floor (spatial level operations)
	static TSharedPtr<FJsonValue> LineTrace(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BulkLineTrace(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SnapActorToFloor(const TSharedPtr<FJsonObject>& Params);
	// #453: per-actor motion snapshot for telemetry / driving probes.
	static TSharedPtr<FJsonValue> ReadActorMotion(const TSharedPtr<FJsonObject>& Params);
	// #434: bulk-add transforms to an actor's HISMC/ISMC for foliage / debris
	// authoring. Python's add_instance crashes in 5.7; the C++ path is fine.
	static TSharedPtr<FJsonValue> AddHismcInstances(const TSharedPtr<FJsonObject>& Params);
	// #697: read back every instance transform on an actor's ISMC/HISMC so a
	// caller can inspect / re-scatter an existing instanced component.
	static TSharedPtr<FJsonValue> GetInstanceTransforms(const TSharedPtr<FJsonObject>& Params);
	// #697: update or remove a single instance on an ISMC/HISMC by index.
	static TSharedPtr<FJsonValue> UpdateInstanceTransform(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveInstance(const TSharedPtr<FJsonObject>& Params);
	// Preview or commit a bounded batch of ISMC/HISMC instances snapped to a
	// filtered collision surface. Defaults to dryRun=true and preflights every
	// trace before one transactional mutation.
	static TSharedPtr<FJsonValue> SnapInstancesToSurface(const TSharedPtr<FJsonObject>& Params);
	// #696: enable + force-build Nanite on a UStaticMesh asset, or read its
	// current Nanite state.
	static TSharedPtr<FJsonValue> SetNaniteSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetNaniteInfo(const TSharedPtr<FJsonObject>& Params);
	// #679/#677: spawn a SkeletalMeshActor with a mesh + optional materials and
	// single-node animation preview for visual/deform verification.
	static TSharedPtr<FJsonValue> SpawnSkeletalMeshActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetComponentSkeletalMesh(const TSharedPtr<FJsonObject>& Params);
	// #666: add a material blendable to a PostProcessVolume's WeightedBlendables.
	static TSharedPtr<FJsonValue> AddPostProcessBlendable(const TSharedPtr<FJsonObject>& Params);
	// #950: FPostProcessSettings values only apply when their bOverride_<Name>
	// bit is on, so the setter writes both halves and the reader reports which
	// settings a volume is actually overriding.
	// LevelHandlers_PostProcess.cpp.
	static TSharedPtr<FJsonValue> SetPostProcessSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPostProcessSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetFixedExposure(const TSharedPtr<FJsonObject>& Params);
	// #637: export a selected actor's mesh to FBX plus a metadata sidecar JSON.
	static TSharedPtr<FJsonValue> ExportActorFbx(const TSharedPtr<FJsonObject>& Params);
	// #910/#943/#912: one editor-side component query. Filters, projection,
	// predicates, grouping and counts all evaluate here, because shipping the
	// candidates out to be filtered by the client is what made these questions
	// megabyte payloads and Python loops.
	static TSharedPtr<FJsonValue> QueryComponents(const TSharedPtr<FJsonObject>& Params);
	// #984/#941/#907/#987: the write side of the same complaint. A selector
	// that runs in the editor, so a level-wide edit is one call rather than a
	// generated label list and a loop.
	static TSharedPtr<FJsonValue> BatchSetActorProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BulkSetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveComponentsByClass(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnActorsBatch(const TSharedPtr<FJsonObject>& Params);
	// #944: rerun construction on PLACED instances. blueprint(run_construction_script)
	// only touches a temporary actor, and the Python wrapper has no equivalent.
	static TSharedPtr<FJsonValue> RerunConstruction(const TSharedPtr<FJsonObject>& Params);
	// #915: rebuild collision after mesh data changed under it. Stale collision
	// makes line_trace report a miss through solid geometry.
	static TSharedPtr<FJsonValue> RecreatePhysicsState(const TSharedPtr<FJsonObject>& Params);
	// #914: geometric overlap between two components, with the unscaled local
	// bounds a caller needs to reason about the answer.
	static TSharedPtr<FJsonValue> TestComponentOverlap(const TSharedPtr<FJsonObject>& Params);
	// #911: wrap UEditorActorSubsystem::ConvertActors for Brush to StaticMesh,
	// with the safety checks that operation needs because it destroys its input.
	static TSharedPtr<FJsonValue> ConvertBrushesToStaticMesh(const TSharedPtr<FJsonObject>& Params);
	// #946: per-slot COMPONENT material overrides on placed actors, which is a
	// different thing from the mesh asset's own slots.
	static TSharedPtr<FJsonValue> SetComponentMaterials(const TSharedPtr<FJsonObject>& Params);
	// #956: a verification subject that cannot be saved into the map.
	static TSharedPtr<FJsonValue> SpawnTransientActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DestroyTransientActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListTransientActors(const TSharedPtr<FJsonObject>& Params);
};
