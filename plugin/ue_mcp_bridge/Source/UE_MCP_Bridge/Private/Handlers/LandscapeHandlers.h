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
	static TSharedPtr<FJsonValue> SculptLandscape(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PaintLandscapeLayer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportHeightmap(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params);
	// #303: spawn an ALandscape with a default flat heightmap. Required for
	// PCG/heightmap workflows that need a sampleable landscape without a
	// pre-prepared heightmap PNG.
	static TSharedPtr<FJsonValue> CreateLandscape(const TSharedPtr<FJsonObject>& Params);
	// #251: standalone ULandscapeLayerInfoObject creation (does not require
	// a landscape in the world).
	static TSharedPtr<FJsonValue> CreateLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params);
	// v0.7.19 issue #150 — concise material + component count summary per proxy
	static TSharedPtr<FJsonValue> GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Params);
};
