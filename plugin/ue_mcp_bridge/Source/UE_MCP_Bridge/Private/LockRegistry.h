#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"
#include "HAL/CriticalSection.h"

/**
 * A single held asset lock. AcquiredAtUtc is for display; ExpiresMono is the
 * monotonic deadline used for TTL math (wall-clock is unsuitable because it can
 * jump). TtlSeconds is the lease length the holder requested.
 */
struct FMCPAssetLock
{
	FString AssetPath;
	FString SessionId;
	FDateTime AcquiredAtUtc;
	double ExpiresMono = 0.0;
	double TtlSeconds = 0.0;
};

/**
 * Per-asset exclusive lock registry, living in the editor bridge — the single
 * shared resource every agent connects to. A lock registry on the Node side
 * would only serialize one agent process; because every agent's Node server
 * talks to this one bridge, the registry has to live here to serialize across
 * agents (or an agent plus a human).
 *
 * Locks carry a session id and a TTL. A crashed or disconnected agent never
 * wedges an asset: the lease simply expires and the next acquire reclaims it.
 * Re-acquiring a lock you already hold (same session) just refreshes the lease,
 * so a single agent never blocks itself across a multi-step operation.
 *
 * All handler execution is marshalled onto the game thread, so these operations
 * are already serialized; the critical section is defence-in-depth for the
 * List()/expiry paths.
 */
class FMCPLockRegistry
{
public:
	static FMCPLockRegistry& Get();

	/** Canonical key: trimmed, forward-slashed, ObjectPath ("/Game/Foo.Foo")
	 *  collapsed to PackagePath ("/Game/Foo"), lowercased — so every spelling
	 *  of the same asset contends for one lock. */
	static FString NormalizeKey(const FString& AssetPath);

	/** Acquire or refresh. Returns true when the caller holds the lock after the
	 *  call (fresh acquire or same-session refresh). On false the asset is held
	 *  by another live session; OutHolder describes it. */
	bool Acquire(const FString& AssetPath, const FString& SessionId, double TtlSeconds, FMCPAssetLock& OutHolder);

	/** Release a lock. Succeeds when held by SessionId, or unconditionally when
	 *  bForce. OutReleased describes what was removed. */
	bool Release(const FString& AssetPath, const FString& SessionId, bool bForce, FMCPAssetLock& OutReleased);

	/** Release every lock held by a session (agent shutdown). Returns the count. */
	int32 ReleaseSession(const FString& SessionId);

	/** Snapshot of all currently-held (non-expired) locks. */
	TArray<FMCPAssetLock> List();

private:
	void PurgeExpired(double Now);

	FCriticalSection Mutex;
	TMap<FString, FMCPAssetLock> Locks; // key = NormalizeKey(AssetPath)
};
