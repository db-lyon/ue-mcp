#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Read-only mesh geometry for static and skeletal meshes (#948, #926, #938,
 * #953).
 *
 * A separate class rather than another FAssetHandlers partition, because these
 * register themselves: AssetHandlers.cpp::RegisterHandlers is a busy file and
 * a new read surface has no reason to touch it. The actions still live under
 * the `asset` category on the wire, which is what a caller sees.
 *
 * The legacy geometry and measurement actions read the engine's render data
 * (FStaticMeshLODResources / FSkeletalMeshLODRenderData). The static-mesh capture
 * also offers authored MeshDescription data with explicit source selection.
 * These actions deliberately do NOT go through
 * UKismetProceduralMeshLibrary::GetSectionFromStaticMesh, which is the python
 * workaround this replaces: that route makes the ProceduralMeshComponent plugin
 * a hidden runtime dependency of reading a vertex.
 */
class FAssetGeometryHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	/** Immutable, bounded source-model or render-data pages with every standard corner attribute. */
	static TSharedPtr<FJsonValue> GetStaticMeshGeometry(const TSharedPtr<FJsonObject>& Params);

	/** Per-section positions / UVs / normals / triangle indices for one LOD. */
	static TSharedPtr<FJsonValue> GetMeshGeometry(const TSharedPtr<FJsonObject>& Params);

	/** Bounds, dimensions, surface area, volume, and topology for one LOD. */
	static TSharedPtr<FJsonValue> MeasureMeshGeometry(const TSharedPtr<FJsonObject>& Params);
};
