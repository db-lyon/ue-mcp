#include "Handlers/LockHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "LockRegistry.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** Serialize a lock record to the wire shape shared by list/acquire/release. */
	TSharedPtr<FJsonObject> LockToJson(const FMCPAssetLock& Lock)
	{
		const double Remaining = FMath::Max(0.0, Lock.ExpiresMono - FPlatformTime::Seconds());
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Lock.AssetPath);
		Obj->SetStringField(TEXT("sessionId"), Lock.SessionId);
		Obj->SetStringField(TEXT("acquiredAt"), Lock.AcquiredAtUtc.ToIso8601());
		Obj->SetNumberField(TEXT("ttlSeconds"), Lock.TtlSeconds);
		Obj->SetNumberField(TEXT("ttlSecondsRemaining"), Remaining);
		return Obj;
	}
}

void FLockHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("acquire_lock"), &FLockHandlers::AcquireLock);
	Registry.RegisterHandler(TEXT("release_lock"), &FLockHandlers::ReleaseLock);
	Registry.RegisterHandler(TEXT("release_session_locks"), &FLockHandlers::ReleaseSessionLocks);
	Registry.RegisterHandler(TEXT("list_locks"), &FLockHandlers::ListLocks);
}

TSharedPtr<FJsonValue> FLockHandlers::AcquireLock(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), Path)) return Err;
	FString SessionId;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("sessionId"), SessionId)) return Err;

	double Ttl = OptionalNumber(Params, TEXT("ttlSeconds"), 300.0);
	if (Ttl <= 0.0) Ttl = 300.0;

	FMCPAssetLock Holder;
	const bool bAcquired = FMCPLockRegistry::Get().Acquire(Path, SessionId, Ttl, Holder);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetBoolField(TEXT("acquired"), bAcquired);
	Res->SetStringField(TEXT("path"), Path);
	Res->SetStringField(TEXT("sessionId"), SessionId);
	if (bAcquired)
	{
		Res->SetNumberField(TEXT("ttlSeconds"), Ttl);
		Res->SetNumberField(TEXT("ttlSecondsRemaining"), FMath::Max(0.0, Holder.ExpiresMono - FPlatformTime::Seconds()));
	}
	else
	{
		// Busy: describe the current holder so the caller (or the Node
		// middleware) can surface a retryable error.
		Res->SetObjectField(TEXT("holder"), LockToJson(Holder));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FLockHandlers::ReleaseLock(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), Path)) return Err;
	FString SessionId;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("sessionId"), SessionId)) return Err;
	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	FMCPAssetLock Released;
	const bool bReleased = FMCPLockRegistry::Get().Release(Path, SessionId, bForce, Released);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetBoolField(TEXT("released"), bReleased);
	Res->SetStringField(TEXT("path"), Path);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FLockHandlers::ReleaseSessionLocks(const TSharedPtr<FJsonObject>& Params)
{
	FString SessionId;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("sessionId"), SessionId)) return Err;

	const int32 Count = FMCPLockRegistry::Get().ReleaseSession(SessionId);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("sessionId"), SessionId);
	Res->SetNumberField(TEXT("released"), Count);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FLockHandlers::ListLocks(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<FMCPAssetLock> Locks = FMCPLockRegistry::Get().List();

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Locks.Num());
	for (const FMCPAssetLock& Lock : Locks)
	{
		Arr.Add(MakeShared<FJsonValueObject>(LockToJson(Lock)));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetArrayField(TEXT("locks"), Arr);
	Res->SetNumberField(TEXT("count"), Locks.Num());
	return MCPResult(Res);
}
