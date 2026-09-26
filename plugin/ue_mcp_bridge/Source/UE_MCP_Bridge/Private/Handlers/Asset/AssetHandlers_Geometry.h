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
 * Everything here reads the engine's own render data (FStaticMeshLODResources /
 * FSkeletalMeshLODRenderData). It deliberately does NOT go through
 * UKismetProceduralMeshLibrary::GetSectionFromStaticMesh, which is the python
 * workaround this replaces: that route makes the ProceduralMeshComponent plugin
 * a hidden runtime dependency of reading a vertex.
 */
class FAssetGeometryHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	/** Per-section positions / UVs / normals / triangle indices for one LOD. */
	static TSharedPtr<FJsonValue> GetMeshGeometry(const TSharedPtr<FJsonObject>& Params);

	/** Bounds, dimensions, surface area, volume, and topology for one LOD. */
	static TSharedPtr<FJsonValue> MeasureMeshGeometry(const TSharedPtr<FJsonObject>& Params);
};
