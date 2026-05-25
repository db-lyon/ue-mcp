#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "PIESequenceFormat.h"

class UInputAction;
class UEnhancedPlayerInput;
class UEnhancedInputComponent;
class APlayerController;
class APawn;
class UWorld;

/**
 * Per-frame state sampler used by both the PIE recorder (writes CSV +
 * markers) and the PIE replayer (writes drift). Discovers tracked
 * UInputAction bindings off the player pawn's UEnhancedInputComponent and
 * samples their per-frame values plus a snapshot of the pawn (location,
 * rotation, velocity, Speed2D, active montage:section). Optional dotted
 * reflection-path "tracked values" (e.g. "Hero.AbilitySystem.Health") get
 * sampled as doubles each frame.
 *
 * Lifecycle (per session):
 *   1. SetConfig(actions whitelist, tracked paths, axis_threshold)
 *   2. AttachToPIE(world) — discover actions, capture pawn class. Returns
 *      false until the pawn and its EnhancedInputComponent exist.
 *   3. SampleFrame(...) once per end-of-frame.
 *   4. Reset() between sessions.
 */
namespace UEMCPPIE
{
	class FPIEFrameSampler
	{
	public:
		struct FConfig
		{
			// Optional whitelist of UInputAction asset paths. Empty = record all
			// actions bound to the pawn's UEnhancedInputComponent.
			TArray<FString> ActionPaths;
			// Dotted reflection paths sampled to doubles per frame.
			TArray<FString> TrackedValuePaths;
			float AxisThreshold = 0.15f;
			bool bCapturePawnState = true;
			bool bCaptureMontage = true;
			// Local player to sample. 0 = first player controller (the legacy
			// single-client behaviour); 1+ selects subsequent local players in
			// multi-player PIE sessions (UGameInstance::GetLocalPlayers order).
			int32 ClientIndex = 0;
		};

		FPIEFrameSampler();

		void SetConfig(const FConfig& InConfig);
		void Reset();

		// Walk the PIE world's first player and bind to its pawn's
		// EnhancedInputComponent. Returns true once attached; subsequent
		// calls are no-ops. Safe to call every frame until it succeeds.
		bool AttachToPIE(UWorld* PIEWorld);
		bool IsAttached() const { return bAttached; }

		// Sample one frame. Caller supplies the absolute game time, dt, and
		// frame index. Returns the populated row, including edge events
		// computed against the previous frame's state.
		FCSVRow SampleFrame(UWorld* PIEWorld, uint64 FrameNumber, double GameTime, double DeltaTime);

		// Capture the action specs that were discovered for the manifest /
		// CSV header. Empty until AttachToPIE succeeds.
		const TArray<FActionSpec>& GetActions() const { return Actions; }
		const TArray<FTrackedValueSpec>& GetTrackedValues() const { return TrackedValues; }
		FString GetPawnClassPath() const { return PawnClassPath; }
		FString GetPIEWorldPath() const { return PIEWorldPath; }

		// Insert a marker label into the next emitted row's EdgeEvents.
		// Multiple markers in the same frame are concatenated.
		void QueueMarker(const FString& Label);

	private:
		struct FTrackedAction
		{
			TWeakObjectPtr<const UInputAction> Action;
			FString Name;
			FString Path;
			EActionValueType ValueType = EActionValueType::Boolean;
			bool bWasActive = false;
		};

		FConfig Config;
		bool bAttached = false;
		FString PawnClassPath;
		FString PIEWorldPath;
		TArray<FActionSpec> Actions;
		TArray<FTrackedValueSpec> TrackedValues;
		TArray<FTrackedAction> Tracked;
		TArray<FString> PendingMarkerLabels;
		FVector PrevPawnLocation = FVector::ZeroVector;
	};
}
