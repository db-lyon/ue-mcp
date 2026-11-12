#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"

class FEvent;

/**
 * An answer a handler gives after it has returned. The handler defers on the
 * game thread with MCPDeferRequest(), returns MCPDeferredMarker(), and calls
 * Complete from any thread. The connection thread waits for it, forwards
 * progress to the client, and runs the cancel hook when the client cancels or
 * stops waiting first.
 */
class UE_MCP_BRIDGE_API FMCPDeferredCall : public TSharedFromThis<FMCPDeferredCall, ESPMode::ThreadSafe>
{
public:
	FMCPDeferredCall();
	~FMCPDeferredCall();

	/** The answer. The first one wins; any later one, or one after a cancel, is dropped. Any thread. */
	void Complete(const TSharedPtr<FJsonValue>& Result);

	/** Progress for the client. Total <= 0 means unknown. Any thread. */
	void ReportProgress(double Progress, double Total, const FString& Message);

	/** Run on the game thread, once, if the call is cancelled before it completes; at once when it already was. */
	void OnCancel(TFunction<void(const FString& Reason)> Hook);

	/** Stop waiting and schedule the cancel hook on the game thread. No effect once complete. Any thread. */
	void Cancel(const FString& Reason);

	bool IsCancelled() const;
	bool IsComplete() const;

	/** What a cancel does and does not reach, for the cancelled answer. Any thread. */
	void SetCancelNote(const FString& Note);
	FString GetCancelNote() const;

	/** Connection thread: true once Complete or Cancel ran, waiting at most Ms. */
	bool Wait(uint32 Ms);
	TSharedPtr<FJsonValue> GetResult() const;

	/** The progress reported since the last take, if any. */
	bool TakeProgress(double& OutProgress, double& OutTotal, FString& OutMessage);

private:
	mutable FCriticalSection Lock;
	FEvent* DoneEvent = nullptr;
	TSharedPtr<FJsonValue> Result;
	bool bComplete = false;
	bool bCancelled = false;
	TFunction<void(const FString&)> CancelHook;
	double Progress = 0.0;
	double Total = 0.0;
	FString ProgressMessage;
	bool bProgressPending = false;
	FString CancelNote;
	FString CancelReason;
};

using FMCPDeferredCallRef = TSharedRef<FMCPDeferredCall, ESPMode::ThreadSafe>;
using FMCPDeferredCallPtr = TSharedPtr<FMCPDeferredCall, ESPMode::ThreadSafe>;

/**
 * Game thread, inside a handler: defer the answer to the bridge request being
 * served. Null when the handler was called in-process rather than for a bridge
 * request, and then it has to answer now.
 */
UE_MCP_BRIDGE_API FMCPDeferredCallPtr MCPDeferRequest();

/** What a handler returns after MCPDeferRequest. */
UE_MCP_BRIDGE_API TSharedPtr<FJsonValue> MCPDeferredMarker();
UE_MCP_BRIDGE_API bool MCPIsDeferredMarker(const TSharedPtr<FJsonValue>& Value);
