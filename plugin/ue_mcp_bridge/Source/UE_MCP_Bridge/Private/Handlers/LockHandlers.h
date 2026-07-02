#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Per-asset exclusive locking so concurrent agents (or an agent plus a human)
 * don't issue conflicting mutations against the same asset. Backed by
 * FMCPLockRegistry, which lives in the bridge — the single shared editor every
 * agent connects to. Exposed as explicit acquire/release/list actions; the
 * Node dispatch layer also wraps mutating calls with these when locking is
 * enabled (see src/locking.ts).
 */
class FLockHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> AcquireLock(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReleaseLock(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReleaseSessionLocks(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLocks(const TSharedPtr<FJsonObject>& Params);
};
