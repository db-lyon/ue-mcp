#pragma once

#include "CoreMinimal.h"
#include "Handlers/PIE/PIEClip.h"

/**
 * The drivers an arrangement play hands its lanes to, one per performer kind. The player keeps a driver per
 * performer and sends each Input Action lane to the driver of the performer its track drives; it never looks at a
 * performer's kind itself, so a new kind is a new driver and a case in MakeClipPerformerDriver.
 *
 * The one kind today is a player: the run's own (client 0), fed through its Enhanced Input subsystem, or a remote
 * client, fed through a hold on that client.
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

		/** Input: the lane's raw value at this frame, inactive where nothing covers it. */
		virtual void FeedInput(const FClipTrack& Track, bool bActive, const FVector& Value) = 0;
		/** The run ended or looped: release what the driver holds (a remote hold). */
		virtual void Stop() {}

	protected:
		FClipPerformer Performer;
	};

	/** A driver for the performer's kind. */
	TUniquePtr<FClipPerformerDriver> MakeClipPerformerDriver(const FClipPerformer& Performer, const FClipDriverContext& Context);
}
