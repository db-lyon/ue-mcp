#pragma once

#include "CoreMinimal.h"
#include "Handlers/PIE/PIEClip.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UInputAction;
class UWorld;

/**
 * The drivers a clip run hands its lanes to (#2137), one per performer kind. The clip player keeps a driver per
 * performer and sends each lane to the driver of the performer it drives; it never looks at a performer's kind
 * itself, so a new kind (a group of actors, say) is a new driver and a case in MakeClipPerformerDriver.
 *
 * A driver acts on a performer through the capabilities its kind takes. The base class gives every kind action
 * and value lanes against the actor it resolves (a player's pawn, or the actor), and refuses the rest; each kind
 * adds what only it does: a player feeds Input Actions, an actor walks to points through its controller.
 */
namespace MCPPIE
{
	struct FClipDriverContext
	{
		// The PIE instance and local player the run's own player (client 0) is.
		int32 PIEInstance = INDEX_NONE;
		int32 OwnClientId = 0;
	};

	class FClipPerformerDriver
	{
	public:
		explicit FClipPerformerDriver(const FClipPerformer& InPerformer) : Performer(InPerformer) {}
		virtual ~FClipPerformerDriver() = default;

		const FClipPerformer& GetPerformer() const { return Performer; }
		bool Accepts(EClipLaneCapability Capability) const { return ClipPerformerAccepts(Performer.Kind, Capability); }

		/** The actor action and value lanes act on, and a recording reads: a player's pawn, or the actor. */
		virtual AActor* ResolveActor(UWorld* World, FString& OutError) = 0;

		/** Input: the lane's raw value at this frame, inactive where nothing covers it. */
		virtual void FeedInput(const FClipTrack& Track, bool bActive, const FVector& Value);
		/** Move: walk to the point through the performer's controller. */
		virtual bool MoveTo(UWorld* World, const FClipMovePoint& Point, FString& OutError);
		/** Action: call the function on the performer, or on its named component. */
		virtual bool Call(UWorld* World, const FClipActionCall& Call, FString& OutError);
		/** Value: write the lane's value to its target, a property path or bb:<Key>. */
		virtual bool SetValue(UWorld* World, const FClipLane& Lane, double Value, FString& OutError);
		/** Recording: read the lane's target back. */
		virtual bool ReadValue(UWorld* World, const FClipLane& Lane, double& OutValue, FString& OutError);
		/** Recording: where the performer stands now. */
		virtual bool ReadLocation(UWorld* World, FVector& OutLocation, FString& OutError);
		/** The run ended: release what the driver holds (a remote hold, a move in flight). */
		virtual void Stop() {}

	protected:
		FClipPerformer Performer;
	};

	/** A driver for the performer's kind, or null for a kind no driver handles yet. */
	TUniquePtr<FClipPerformerDriver> MakeClipPerformerDriver(const FClipPerformer& Performer, const FClipDriverContext& Context);

	/**
	 * Records the calls an armed action lane's actor takes while a take is recorded: each watched function on the
	 * actor's class is wrapped so a call that reaches it through ProcessEvent or a Blueprint calling a native
	 * function is noted, with the frame it fell on, before it runs as before. Calls one Blueprint function makes to
	 * another inside the same graph do not pass through the wrapper. The wrappers come off when the take ends.
	 */
	class FClipCallRecorder
	{
	public:
		static FClipCallRecorder& Get();
		/** Watch Functions on Actor for LaneId. Returns the functions it could not find, for a warning. */
		TArray<FString> Watch(const FString& LaneId, AActor* Actor, const TArray<FString>& Functions);
		/** The frame a call seen now lands on. */
		void SetFrame(double Frame) { CurrentFrame = Frame; }
		/** Every call seen per lane since Watch, and stop watching. */
		TMap<FString, TArray<FClipActionCall>> Finish();
		bool IsWatching() const { return Hooks.Num() > 0; }

	private:
		struct FHook
		{
			TWeakObjectPtr<UFunction> Function;
			FNativeFuncPtr Original = nullptr;
			// Lane ids by the object whose calls they record (the actor or one of its components).
			TArray<TPair<TWeakObjectPtr<UObject>, FString>> Watchers;
		};
		static void Thunk(UObject* Context, FFrame& Stack, RESULT_DECL);
		void Note(UFunction* Function, UObject* Context, FFrame& Stack);

		TMap<UFunction*, FHook> Hooks;
		TMap<FString, TArray<FClipActionCall>> Seen;
		double CurrentFrame = 0.0;
	};
}
