#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FAnimationHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Existing read-only queries
	static TSharedPtr<FJsonValue> ListAnimAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSkeletalMeshes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSockets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPhysicsAssetInfo(const TSharedPtr<FJsonObject>& Params);

	// Read handlers for animation asset types
	static TSharedPtr<FJsonValue> ReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAnimMontage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAnimSequence(const TSharedPtr<FJsonObject>& Params);

	// Read handlers for blendspace
	static TSharedPtr<FJsonValue> ReadBlendspace(const TSharedPtr<FJsonObject>& Params);

	// Create handlers for animation asset types
	static TSharedPtr<FJsonValue> CreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMontage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBlendspace(const TSharedPtr<FJsonObject>& Params);

	// Notify handlers
	static TSharedPtr<FJsonValue> AddAnimNotify(const TSharedPtr<FJsonObject>& Params);

	// Animation sequence authoring
	static TSharedPtr<FJsonValue> CreateSequence(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetBoneKeyframes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBoneTransforms(const TSharedPtr<FJsonObject>& Params);

	// Montage editing
	static TSharedPtr<FJsonValue> SetMontageSequence(const TSharedPtr<FJsonObject>& Params);
};
