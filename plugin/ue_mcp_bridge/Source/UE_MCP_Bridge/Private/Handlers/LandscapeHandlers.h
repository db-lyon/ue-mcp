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
};
