#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FMCPHandlerRegistry;

/**
 * Localization: the project's cultures, its localization targets (list,
 * create or change, remove), a gather and a compile for one target through the
 * same GatherText commandlet the Localization Dashboard runs, and the
 * namespace and key of an FText property.
 *
 * The target and culture actions register under the project category, the
 * text-key actions under asset. String table entries and their metadata stay
 * in AssetHandlers_Import.cpp beside the rest of the string table actions.
 */
class FLocalizationHandlers
{
public:
	static void RegisterHandlers(FMCPHandlerRegistry& Registry);

	static TSharedPtr<FJsonValue> ListCultures(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListTargets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTarget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveTarget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GatherTarget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CompileTarget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetTextLocalization(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTextLocalization(const TSharedPtr<FJsonObject>& Params);
};
