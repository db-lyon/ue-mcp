#include "MCPHandlerRegistration.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

/**
 * Global registry for handlers contributed by third-party plugin modules.
 * The bridge's per-server FMCPHandlerRegistry falls back to this map when
 * a method isn't found locally, so plugins that load before, after, or
 * concurrently with the bridge module all participate uniformly.
 */
namespace UEMCP
{
	namespace
	{
		FCriticalSection& ExternalRegistryMutex()
		{
			static FCriticalSection Mutex;
			return Mutex;
		}

		TMap<FString, FExternalHandlerFn>& ExternalHandlers()
		{
			static TMap<FString, FExternalHandlerFn> Map;
			return Map;
		}

		TMap<FString, float>& ExternalHandlerTimeouts()
		{
			static TMap<FString, float> Map;
			return Map;
		}
	}

	void RegisterExternalHandler(const FString& MethodName, FExternalHandlerFn Handler)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		ExternalHandlers().Add(MethodName, MoveTemp(Handler));
	}

	void RegisterExternalHandlerWithTimeout(const FString& MethodName, FExternalHandlerFn Handler, float TimeoutSeconds)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		ExternalHandlers().Add(MethodName, MoveTemp(Handler));
		if (TimeoutSeconds > 0.0f)
		{
			ExternalHandlerTimeouts().Add(MethodName, TimeoutSeconds);
		}
	}

	void UnregisterExternalHandler(const FString& MethodName)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		ExternalHandlers().Remove(MethodName);
		ExternalHandlerTimeouts().Remove(MethodName);
	}

	bool LookupExternalHandler(const FString& MethodName, FExternalHandlerFn& OutFn, float& OutTimeoutSeconds)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		if (const FExternalHandlerFn* Found = ExternalHandlers().Find(MethodName))
		{
			OutFn = *Found;
			if (const float* T = ExternalHandlerTimeouts().Find(MethodName))
			{
				OutTimeoutSeconds = *T;
			}
			else
			{
				OutTimeoutSeconds = 0.0f;
			}
			return true;
		}
		return false;
	}

	TArray<FString> GetExternalHandlerNames()
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		TArray<FString> Names;
		ExternalHandlers().GetKeys(Names);
		return Names;
	}
}
