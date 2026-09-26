#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Effective collision reads (#925).
 *
 * What already existed reports the collision a component STORES, not the
 * collision it HAS. blueprint(read_component_properties) exports BodyInstance
 * as text, and that text carries CollisionProfileName plus the ResponseArray of
 * overrides that DIFFER from the profile. A response inherited from the profile
 * (the Pawn profile blocking Camera, say) appears nowhere in it, so "does this
 * capsule block ECC_Camera" could not be answered from the dump.
 * editor(get_object_properties) has the same limit: it exports property text,
 * it does not evaluate the profile.
 *
 * That gap has teeth. A live-coded constructor change to
 * SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore) LOOKED applied, because
 * the Live Coding compile succeeded, and the CDO still answered ECR_BLOCK until
 * a proper on-disk build and an editor restart. The text dump was byte for byte
 * identical before and after. Only the effective response, which is what
 * UPrimitiveComponent::GetCollisionResponseToChannel returns, showed it.
 *
 * A separate class rather than another FBlueprintHandlers partition, because
 * these register themselves and the two actions land in two different wire
 * categories (`blueprint` and `project`) while sharing one channel table. The
 * actions still read as blueprint(get_component_collision) and
 * project(resolve_collision_profile) to a caller.
 */
class FCollisionQueryHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	/** Resolved collision state of one component template or CDO component. */
	static TSharedPtr<FJsonValue> GetComponentCollision(const TSharedPtr<FJsonObject>& Params);

	/** Per-channel responses of one named collision profile. */
	static TSharedPtr<FJsonValue> ResolveCollisionProfile(const TSharedPtr<FJsonObject>& Params);
};
