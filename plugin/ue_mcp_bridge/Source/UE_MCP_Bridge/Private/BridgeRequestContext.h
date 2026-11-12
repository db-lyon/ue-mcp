#pragma once

#include "CoreMinimal.h"
#include "HandlerDeferred.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"

/**
 * One bridge request while it is being served: the deferred answer its handler
 * may have asked for, and whether the client has cancelled it. Shared between
 * the connection thread, the game thread and a cancel_request arriving on
 * another connection.
 */
class FMCPRequestContext : public TSharedFromThis<FMCPRequestContext, ESPMode::ThreadSafe>
{
public:
	explicit FMCPRequestContext(const FString& InCancelToken) : CancelToken(InCancelToken) {}

	const FString CancelToken;

	FMCPDeferredCallPtr GetDeferred() const;
	FMCPDeferredCallRef MakeDeferred();

	/** Flags the request and cancels its deferred answer when there is one. Returns that answer's state. */
	FString RequestCancel();
	bool IsCancelRequested() const { return bCancelRequested; }

private:
	mutable FCriticalSection Lock;
	FMCPDeferredCallPtr Deferred;
	FThreadSafeBool bCancelRequested{false};
};

using FMCPRequestContextRef = TSharedRef<FMCPRequestContext, ESPMode::ThreadSafe>;

namespace MCPRequestContext
{
	/** Game thread: the request MCPDeferRequest defers while a handler runs for it. */
	class FScope
	{
	public:
		explicit FScope(const TSharedPtr<FMCPRequestContext, ESPMode::ThreadSafe>& Context);
		~FScope();
	private:
		TSharedPtr<FMCPRequestContext, ESPMode::ThreadSafe> Previous;
	};

	/** Requests a cancel_request can reach, by the token the client sent with them. */
	void Register(const FMCPRequestContextRef& Context);
	void Unregister(const FMCPRequestContextRef& Context);

	/** "deferred", "running", or "unknown" when no request carries the token. */
	FString Cancel(const FString& CancelToken);
}
