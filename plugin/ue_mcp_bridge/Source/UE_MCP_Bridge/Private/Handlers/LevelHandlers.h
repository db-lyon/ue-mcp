#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FLevelHandlers
{
public:
	// Register all level handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> GetOutliner(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaceActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActorDetails(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCurrentLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLevels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSelectedActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListVolumes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SelectActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnLight(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLightProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddComponentToActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LoadLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSublevels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetVolumeProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWorldSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWorldSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorMaterial(const TSharedPtr<FJsonObject>& Params);
	// #94: Fog + sky helpers
	static TSharedPtr<FJsonValue> SetFogProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActorsByClass(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #146 — actor class histogram (counts by class name)
	static TSharedPtr<FJsonValue> CountActorsByClass(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #150 — RuntimeVirtualTextureVolume / component summary
	static TSharedPtr<FJsonValue> GetRVTSummary(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #151 — set WaterBodyComponent property via runtime class lookup
	static TSharedPtr<FJsonValue> SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Params);
	// #188: get actor origin + extent bounds
	static TSharedPtr<FJsonValue> GetActorBounds(const TSharedPtr<FJsonObject>& Params);
	// #178: resolve actor by internal/runtime UObject name
	static TSharedPtr<FJsonValue> ResolveActor(const TSharedPtr<FJsonObject>& Params);
	// #202/#230: generic per-instance UPROPERTY writer for level actors
	static TSharedPtr<FJsonValue> SetActorProperty(const TSharedPtr<FJsonObject>& Params);
	// #220: bulk delete actors by label prefix / class / tag
	static TSharedPtr<FJsonValue> DeleteActors(const TSharedPtr<FJsonObject>& Params);
	// #219: actor tag CRUD
	static TSharedPtr<FJsonValue> AddActorTag(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveActorTag(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorTags(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListActorTags(const TSharedPtr<FJsonObject>& Params);
	// #205: actor attach/detach + mobility
	static TSharedPtr<FJsonValue> AttachActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DetachActor(const TSharedPtr<FJsonObject>& Params);
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
};
