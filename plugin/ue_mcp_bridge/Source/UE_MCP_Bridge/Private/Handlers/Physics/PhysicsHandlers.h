#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FPhysicsHandlers
{
public:
	// Register all physics handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> SetCollisionProfile(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCollisionEnabled(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCollision(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetBodyProperties(const TSharedPtr<FJsonObject>& Params);
	// #676: apply an impulse or force to a (PIE) actor's physics body.
	static TSharedPtr<FJsonValue> AddImpulse(const TSharedPtr<FJsonObject>& Params);
};
