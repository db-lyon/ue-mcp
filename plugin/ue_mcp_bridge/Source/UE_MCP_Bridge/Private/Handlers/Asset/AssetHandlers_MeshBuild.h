#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Static mesh build settings (FMeshBuildSettings on a source model): recompute normals and tangents, lightmap UV
 * generation, degenerate removal and the rest. Fixing an import that arrived with bad normals needs this, and nothing
 * else on the surface reaches a source model. Registered under the `asset` category.
 */
class FAssetMeshBuildHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> GetStaticMeshBuildSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStaticMeshBuildSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ScaleMeshGeometry(const TSharedPtr<FJsonObject>& Params);
};
