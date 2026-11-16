#include "MCPHandlerRegistration.h"
#include "HandlerRegistry.h"
#include "UE_MCP_BridgeModule.h"
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

		TMap<FString, FMCPHandlerSpec>& ExternalHandlerSpecs()
		{
			static TMap<FString, FMCPHandlerSpec> Map;
			return Map;
		}
	}

	void RegisterExternalHandler(const FString& MethodName, FExternalHandlerFn Handler)
	{
		RegisterExternalHandlerWithTimeout(MethodName, MoveTemp(Handler), 0.0f);
	}

	bool RegisterExternalHandler(const FString& MethodName, FExternalHandlerFn Handler,
		const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules, float TimeoutSeconds)
	{
		RegisterExternalHandlerWithTimeout(MethodName, MoveTemp(Handler), TimeoutSeconds);

		FMCPHandlerSpec Spec;
		Spec.Params = Params;
		Spec.Choices = Rules.Choices;
		Spec.ContractExemptReason = Rules.ContractExemptReason;
		Spec.Facts = Rules.Facts;
		if (TimeoutSeconds > 0.0f) Spec.Facts.TimeoutSeconds = TimeoutSeconds;
		// The socket thread runs only the bridge's own handshake reads; a plugin
		// handler always runs on the game thread.
		const FString Problem = Spec.Facts.bServedOffGameThread
			? FString(TEXT("a plugin handler cannot be served off the game thread; drop ServedOffGameThread"))
			: FMCPHandlerRegistry::ValidateHandlerSpec(Spec);
		if (!Problem.IsEmpty())
		{
			UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] External handler '%s' registered without its spec: %s"), *MethodName, *Problem);
			return false;
		}
		FScopeLock Lock(&ExternalRegistryMutex());
		ExternalHandlerSpecs().Add(MethodName, MoveTemp(Spec));
		return true;
	}

	bool LookupExternalHandlerSpec(const FString& MethodName, FMCPHandlerSpec& OutSpec)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		if (const FMCPHandlerSpec* Found = ExternalHandlerSpecs().Find(MethodName))
		{
			OutSpec = *Found;
			return true;
		}
		return false;
	}

	TMap<FString, FMCPHandlerSpec> GetExternalHandlerSpecs()
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		return ExternalHandlerSpecs();
	}

	void RegisterExternalHandlerWithTimeout(const FString& MethodName, FExternalHandlerFn Handler, float TimeoutSeconds)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		ExternalHandlers().Add(MethodName, MoveTemp(Handler));
		// A registration without a spec drops any earlier one, so the published contract never outlives its handler.
		ExternalHandlerSpecs().Remove(MethodName);
		// A re-registration replaces the timeout too, so a handler registered
		// again without one falls back to the default instead of a stale value.
		if (TimeoutSeconds > 0.0f)
		{
			ExternalHandlerTimeouts().Add(MethodName, TimeoutSeconds);
		}
		else
		{
			ExternalHandlerTimeouts().Remove(MethodName);
		}
	}

	void UnregisterExternalHandler(const FString& MethodName)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		ExternalHandlers().Remove(MethodName);
		ExternalHandlerTimeouts().Remove(MethodName);
		ExternalHandlerSpecs().Remove(MethodName);
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

	bool HasExternalHandler(const FString& MethodName)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		return ExternalHandlers().Contains(MethodName);
	}

	float GetExternalHandlerTimeout(const FString& MethodName)
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		const float* Timeout = ExternalHandlerTimeouts().Find(MethodName);
		return Timeout ? *Timeout : 0.0f;
	}

	TArray<FString> GetExternalHandlerNames()
	{
		FScopeLock Lock(&ExternalRegistryMutex());
		TArray<FString> Names;
		ExternalHandlers().GetKeys(Names);
		return Names;
	}
}
