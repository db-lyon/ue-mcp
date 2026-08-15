#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/**
 * Editor-only authoring helpers for Mass entity configuration assets.
 *
 * The implementation intentionally resolves Mass classes by reflection. This
 * keeps the bridge loadable on engine versions/projects where Mass is not
 * enabled while still providing a strongly validated path when MassSpawner is
 * present.
 */
class FMassHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> EnsureEntityConfig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadEntityConfig(const TSharedPtr<FJsonObject>& Params);
};
