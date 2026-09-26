#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * StaticMesh boolean CSG: union, subtract, intersect (#916).
 *
 * A StaticMesh repair needed subtract/union/intersect and no native action
 * exposed one, so the only route was execute_python introspecting the
 * unreal.GeometryScript_* modules at runtime, converting to a DynamicMesh,
 * running the boolean, validating the result and writing it back by hand. That
 * is a lot of moving parts to get right in a script, and every step of it is
 * something the bridge should own.
 *
 * A separate class and translation unit rather than another FAssetHandlers
 * partition: AssetHandlers.cpp is already 3700 lines and this registers itself.
 * The action still reads as asset(mesh_boolean) on the wire.
 *
 * GeometryScripting is an engine PLUGIN, so its classes are reached through
 * reflection at runtime rather than linked in Build.cs, the way the Water
 * handlers reach their optional modules. A project with the plugin disabled
 * gets a clear "GeometryScripting plugin not available" answer naming what to
 * enable, instead of the module failing to link for everyone.
 */
class FAssetMeshBooleanHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	/** Boolean two StaticMeshes into a third (or, opted into, back into one). */
	static TSharedPtr<FJsonValue> MeshBoolean(const TSharedPtr<FJsonObject>& Params);
};
