#pragma once

#include "CoreMinimal.h"

class UWorld;
class AVolume;

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
}
