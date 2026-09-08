#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FFoliageHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListFoliageTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SampleFoliage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetFoliageSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetFoliageTypeSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateFoliageType(const TSharedPtr<FJsonObject>& Params);
	// #988: write settings to every FoliageType matching a predicate on an
	// EXISTING property value. Lives in FoliageHandlers_Batch.cpp.
	static TSharedPtr<FJsonValue> BatchSetFoliageSettingsWhere(const TSharedPtr<FJsonObject>& Params);

	// V12 depth: placing, removing and reading instances, level palette
	// membership, and the procedural foliage simulation. All in
	// FoliageHandlers_Depth.cpp; see that file's header for why each one needs
	// a handler rather than a property write.
	static TSharedPtr<FJsonValue> AddFoliageInstances(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveFoliageInstances(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetFoliageInstances(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddFoliageTypeToLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveFoliageTypeFromLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadProceduralFoliageSpawner(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetProceduralFoliageSpawnerTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SimulateProceduralFoliage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearProceduralFoliage(const TSharedPtr<FJsonObject>& Params);
};
