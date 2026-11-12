#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Handlers/PIE/PIESequenceFormat.h"

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
 *   2. AttachToPIE(world) - resolve the configured local player in supplied
 *      world, discover actions, capture pawn class. Returns false until the
 *      pawn and its EnhancedInputComponent exist.
 *   3. SampleFrame(...) once per end-of-frame.
 *   4. Reset() between sessions.
 */
namespace MCPPIE
{
	class FPIEFrameSampler
	{
	public:
		struct FConfig
		{
			// Optional whitelist of UInputAction asset paths, package or object
			// form (normalized by SetConfig). Empty = record every action bound
			// to the player.
			TArray<FString> ActionPaths;
			// Dotted reflection paths sampled to doubles per frame.
			TArray<FString> TrackedValuePaths;
			float AxisThreshold = 0.15f;
			bool bCapturePawnState = true;
			bool bCaptureMontage = true;
			// Local player index inside selected PIEInstance. 0 is the first
			// local player; 1+ selects subsequent local players.
			int32 ClientIndex = 0;
		};

		FPIEFrameSampler();

		// Replaces the whole config, including the tracked value list (#1690).
		void SetConfig(const FConfig& InConfig);
		void Reset();

		// "/Game/Input/IA_Move" -> "/Game/Input/IA_Move.IA_Move". Object paths
		// and export text ("InputAction'/Game/...'") normalize to the object path.
		static FString NormalizeActionPath(const FString& InPath);

		// Walk the PIE world's first player and bind to its pawn's
		// EnhancedInputComponent. Returns true once attached; subsequent
		// calls are no-ops. Safe to call every frame until it succeeds.
		// bRequireInputComponent=false attaches to a pawn that has no input
		// component (input disabled, or a pawn left behind by an earlier run).
		bool AttachToPIE(UWorld* PIEWorld, bool bRequireInputComponent = true);
		bool IsAttached() const { return bAttached; }
		// Why the last AttachToPIE call did not attach. Empty once attached.
		const FString& GetAttachBlocker() const { return AttachBlocker; }
		// The last AttachToPIE call found the pawn and only its input component was missing.
		bool IsBlockedOnInputComponent() const { return bBlockedOnInputComponent; }

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

		// Whitelisted action paths no bound action matched. Empty until attach.
		TArray<FString> GetUnmatchedActionPaths() const;
		// One line per problem found at attach: unmatched actions, tracked
		// values that do not resolve to a numeric leaf. Empty when clean.
		const TArray<FString>& GetWarnings() const { return Warnings; }

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

		void DiscoverActions(APlayerController* PC, APawn* Pawn);
		void SeedTrackedValues();
		void ProbeTrackedValues(UWorld* PIEWorld, APawn* Pawn);
		bool SampleTrackedValue(UWorld* PIEWorld, APawn* Pawn, const FString& Path, double& OutValue, FString* OutWhy);
		// "<Actor>.<path>": the level actor named by the head segment, or null.
		AActor* FindTrackedActor(UWorld* PIEWorld, const FString& Id);

		FConfig Config;
		bool bAttached = false;
		FString AttachBlocker;
		bool bBlockedOnInputComponent = false;
		FString PawnClassPath;
		FString PIEWorldPath;
		TArray<FActionSpec> Actions;
		TArray<FTrackedValueSpec> TrackedValues;
		TArray<FTrackedAction> Tracked;
		TArray<FString> PendingMarkerLabels;
		TArray<FString> Warnings;
		// Actor lookups for actor-qualified tracked values (#1655). A miss is
		// retried at most once a second so an unresolved path does not walk
		// every actor in the world each frame.
		struct FTrackedActorRef
		{
			TWeakObjectPtr<AActor> Actor;
			double NextRetrySeconds = 0.0;
		};
		TMap<FString, FTrackedActorRef> TrackedActorRefs;
		FVector PrevPawnLocation = FVector::ZeroVector;
	};
}
