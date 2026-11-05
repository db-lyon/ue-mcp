#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/** Small, editor-only skeletal mesh build-settings handlers. */
class FSkeletalMeshHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> SetOptimizeForInstancing(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBuildSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadSkinWeights(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBoneVertexBounds(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSkinWeights(const TSharedPtr<FJsonObject>& Params);
	// Auto-reduced LODs through RegenerateLOD, in SkeletalMeshHandlers_Lods.cpp (#1372).
	static TSharedPtr<FJsonValue> GenerateSkeletalMeshLods(const TSharedPtr<FJsonObject>& Params);
	// A bone and its skinned vertices moved together in every LOD, in SkeletalMeshHandlers_TransformPart.cpp (#1614).
	static TSharedPtr<FJsonValue> TransformSkinnedPart(const TSharedPtr<FJsonObject>& Params);
};
