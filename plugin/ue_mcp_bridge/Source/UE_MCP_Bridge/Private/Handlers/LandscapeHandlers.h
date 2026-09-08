#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FLandscapeHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> GetLandscapeInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLandscapeLayers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SampleLandscape(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLandscapeSplines(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetLandscapeComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params);
	// #303: spawn an ALandscape with a default flat heightmap. Required for
	// PCG/heightmap workflows that need a sampleable landscape without a
	// pre-prepared heightmap PNG.
	static TSharedPtr<FJsonValue> CreateLandscape(const TSharedPtr<FJsonObject>& Params);
	// #251: standalone ULandscapeLayerInfoObject creation (does not require
	// a landscape in the world).
	static TSharedPtr<FJsonValue> CreateLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #150 - concise material + component count summary per proxy
	static TSharedPtr<FJsonValue> GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Params);
	// #733: enumerate loaded World Partition landscape streaming proxies with
	// per-proxy world bounds, and resolve which proxy covers a world position.
	static TSharedPtr<FJsonValue> ListLandscapeProxies(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FindLandscapeProxyAt(const TSharedPtr<FJsonObject>& Params);
	// Refresh the physical-material data embedded in loaded World Partition
	// landscape collision after a LayerInfo physical material changes.
	static TSharedPtr<FJsonValue> RefreshPhysicalMaterialCollision(const TSharedPtr<FJsonObject>& Params);
	// #742: sculpting and weight painting - the writes the category has always
	// advertised but never had.
	static TSharedPtr<FJsonValue> Sculpt(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PaintLayer(const TSharedPtr<FJsonObject>& Params);

	// V1: region-level read and write, shaping operators, erosion, heightmap
	// IO, terrain analysis, weight regions, layer lifecycle and holes. All
	// defined in LandscapeHandlers_Sculpt.cpp, deliberately in the same
	// translation unit as ResolveLandscape / WorldToLandscapeRect /
	// ResolveEditLayerGuid / BrushWeight so they call those rather than
	// copying them (the module is a unity build, and a copied file-local
	// helper is a redefinition once two files land in one blob).
	static TSharedPtr<FJsonValue> GetHeightRegion(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetHeightRegion(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetHeightAtPoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetNormalAtPoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSlopeAtPoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSlopeMap(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SculptRegion(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ApplyErosion(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportHeightmap(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExportHeightmap(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AnalyzeTerrain(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetLayerWeightRegion(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLayerWeightRegion(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LayerExists(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveLayer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetHoles(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetHoles(const TSharedPtr<FJsonObject>& Params);

	// V17: the core half of a real-world terrain pipeline. Heightmap ingestion
	// planning and geographic coordinate conversion; the network fetch of DEM
	// tiles and the opinionated landcover-to-content passes stay outside core.
	static TSharedPtr<FJsonValue> PlanRealWorldLandscape(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ProjectGeoCoordinates(const TSharedPtr<FJsonObject>& Params);
};
