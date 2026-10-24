#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UWorld;

namespace UEMCP::SceneCapture
{
	constexpr float WarmStreamingViewDurationSeconds = 0.001f;
	// A normal, non-override streaming contribution, renewed for each warm frame.
	void SubmitWarmStreamingView(const FVector& Location, int32 Width, float Fov, UWorld* World);

	// One in-flight capture per bridge; completed results remain addressable
	// until the next warm capture starts. All entry points run on the game thread.
	TSharedPtr<FJsonValue> StartWarmCapture(UWorld* World, const FVector& Location, const FRotator& Rotation,
		int32 Width, int32 Height, float Fov, int32 WarmupFrames, double TimeoutSeconds,
		const FString& OutputPath, const TSharedPtr<FJsonObject>& FocusMetadata);
	TSharedPtr<FJsonValue> ReadWarmCapture(const FString& CaptureId, bool bCancel);
	void ShutdownWarmCapture();
}
