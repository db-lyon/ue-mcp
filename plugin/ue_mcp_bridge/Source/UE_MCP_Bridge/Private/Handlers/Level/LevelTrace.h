#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "CollisionShape.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

namespace MCPTrace
{
	UWorld* ResolveTraceWorld(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError);
	TSharedPtr<FJsonValue> Execute(UWorld* World, const TSharedPtr<FJsonObject>& Params,
		bool bShape = false, const AActor* IgnoredActor = nullptr, bool bCollision = false);
	bool MakeShape(const FString& Shape, double Radius, FVector HalfExtent, double HalfHeight,
		FCollisionShape& OutShape, FString& OutError);
	// Channel == INDEX_NONE emits responses differing from Block, capped at 32.
	void EmitCollision(const TSharedPtr<FJsonObject>& Result, const UPrimitiveComponent* Component,
		int32 Channel = INDEX_NONE);
}
