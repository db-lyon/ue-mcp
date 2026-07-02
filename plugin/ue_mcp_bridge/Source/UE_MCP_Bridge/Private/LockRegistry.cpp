#include "LockRegistry.h"
#include "HAL/PlatformTime.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

FMCPLockRegistry& FMCPLockRegistry::Get()
{
	static FMCPLockRegistry Instance;
	return Instance;
}

FString FMCPLockRegistry::NormalizeKey(const FString& AssetPath)
{
	FString Key = AssetPath;
	Key.TrimStartAndEndInline();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Collapse ObjectPath ("/Game/Foo.Foo") to PackagePath ("/Game/Foo"): if the
	// last '.' comes after the last '/', drop from that '.' onward so both
	// spellings key the same lock. Paths without a dotted object suffix are left
	// as-is.
	int32 SlashIdx = INDEX_NONE;
	int32 DotIdx = INDEX_NONE;
	Key.FindLastChar(TEXT('/'), SlashIdx);
	Key.FindLastChar(TEXT('.'), DotIdx);
	if (DotIdx != INDEX_NONE && DotIdx > SlashIdx)
	{
		Key = Key.Left(DotIdx);
	}

	return Key.ToLower();
}

void FMCPLockRegistry::PurgeExpired(double Now)
{
	for (auto It = Locks.CreateIterator(); It; ++It)
	{
		if (It->Value.ExpiresMono <= Now)
		{
			It.RemoveCurrent();
		}
	}
}

bool FMCPLockRegistry::Acquire(const FString& AssetPath, const FString& SessionId, double TtlSeconds, FMCPAssetLock& OutHolder)
{
	FScopeLock ScopeLock(&Mutex);
	const double Now = FPlatformTime::Seconds();
	PurgeExpired(Now);

	const FString Key = NormalizeKey(AssetPath);
	if (FMCPAssetLock* Existing = Locks.Find(Key))
	{
		if (Existing->SessionId == SessionId)
		{
			// Re-entrant: same session refreshes its own lease rather than
			// deadlocking on a lock it already holds.
			Existing->ExpiresMono = Now + TtlSeconds;
			Existing->TtlSeconds = TtlSeconds;
			OutHolder = *Existing;
			return true;
		}
		// Held by a different, still-live session.
		OutHolder = *Existing;
		return false;
	}

	FMCPAssetLock New;
	New.AssetPath = AssetPath;
	New.SessionId = SessionId;
	New.AcquiredAtUtc = FDateTime::UtcNow();
	New.ExpiresMono = Now + TtlSeconds;
	New.TtlSeconds = TtlSeconds;
	Locks.Add(Key, New);
	OutHolder = New;
	return true;
}

bool FMCPLockRegistry::Release(const FString& AssetPath, const FString& SessionId, bool bForce, FMCPAssetLock& OutReleased)
{
	FScopeLock ScopeLock(&Mutex);
	const FString Key = NormalizeKey(AssetPath);
	if (FMCPAssetLock* Existing = Locks.Find(Key))
	{
		if (bForce || Existing->SessionId == SessionId)
		{
			OutReleased = *Existing;
			Locks.Remove(Key);
			return true;
		}
	}
	return false;
}

int32 FMCPLockRegistry::ReleaseSession(const FString& SessionId)
{
	FScopeLock ScopeLock(&Mutex);
	int32 Count = 0;
	for (auto It = Locks.CreateIterator(); It; ++It)
	{
		if (It->Value.SessionId == SessionId)
		{
			It.RemoveCurrent();
			++Count;
		}
	}
	return Count;
}

TArray<FMCPAssetLock> FMCPLockRegistry::List()
{
	FScopeLock ScopeLock(&Mutex);
	PurgeExpired(FPlatformTime::Seconds());
	TArray<FMCPAssetLock> Out;
	Out.Reserve(Locks.Num());
	for (const TPair<FString, FMCPAssetLock>& Pair : Locks)
	{
		Out.Add(Pair.Value);
	}
	return Out;
}
