#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UWorld;
class AVolume;
class AActor;
class UClass;

namespace UEMCP
{
	/** Initialize a freshly-spawned AVolume with a UCubeBuilder-built brush.
	 *  AVolumes spawned via SpawnActor have Brush=nullptr, which makes
	 *  UEditorBrushBuilder::EndBrush silently no-op (it returns true without
	 *  populating polys). This mirrors UActorFactoryVolume by initializing
	 *  UModel + UPolys + BrushComponent->Brush in the editor's documented
	 *  order, then running UCubeBuilder + csgPrepMovingBrush, so the
	 *  resulting volume reports real bounds to GetActorBounds() and
	 *  downstream samplers.
	 *
	 *  Shared by LevelHandlers (SpawnVolume / SetVolumeProperties) and
	 *  PCGHandlers (PCG volume creation). Issue #238.
	 */
	void BuildVolumeAsCube(UWorld* World, AVolume* Volume, const FVector& HalfExtent);

	/** Tell the navigation system a volume's shape or transform changed.
	 *  Only NavMeshBoundsVolume registers bounds, and a PostEditChange with
	 *  no property does not re-register them. Call after the final transform. */
	void NotifyVolumeBoundsChanged(AVolume* Volume);

	/**
	 * The one volume spawn behind spawn_volume, add_pcg_volume and
	 * spawn_nav_modifier_volume: place_actor's params (F6), plus extent (a
	 * half-size, positive on every axis) for the cube brush and graphPath for
	 * a PCG component, both checked before anything spawns. Configure runs on
	 * the new volume; a refusal from it destroys the volume. The result is
	 * the spawn shape plus the built extent and the bound graph.
	 */
	TSharedPtr<FJsonValue> SpawnVolumeActor(
		const TSharedPtr<FJsonObject>& Params,
		UClass* VolumeClass,
		const FVector& DefaultExtent,
		const TCHAR* TypeLabel,
		TFunctionRef<TSharedPtr<FJsonValue>(AActor*)> Configure);
}
