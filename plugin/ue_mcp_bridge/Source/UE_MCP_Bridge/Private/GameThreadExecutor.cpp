#include "GameThreadExecutor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Ticker.h"
#include "Containers/Queue.h"
#include "Editor.h"

FMCPGameThreadExecutor::FMCPGameThreadExecutor()
{
}

FMCPGameThreadExecutor::~FMCPGameThreadExecutor()
{
}

void FMCPGameThreadExecutor::SetEditorReady()
{
	bEditorReady = true;
}

bool FMCPGameThreadExecutor::IsGameThread()
{
	return IsInGameThread();
}

namespace
{
	// #603: depth of bridge handlers currently running on the game thread.
	// Game-thread only, so a plain int is safe (no atomics needed). Modal
	// dialogs are also raised on the game thread, so the hook can read this
	// to know whether the modal came from an in-flight bridge request.
	int32 GHandlerInFlightDepth = 0;

	struct FHandlerInFlightScope
	{
		FHandlerInFlightScope() { ++GHandlerInFlightDepth; }
		~FHandlerInFlightScope() { --GHandlerInFlightDepth; }
	};
}

bool FMCPGameThreadExecutor::IsHandlerInFlight()
{
	return IsInGameThread() && GHandlerInFlightDepth > 0;
}

namespace
{
	// Shared between the calling thread (which may abandon the wait on
	// timeout) and the game-thread ticker lambda (which completes the work).
	// Captured by value into the lambda so its lifetime extends past the
	// caller's stack frame — critical when the caller times out on a long
	// Python script. Without this shared state, the ticker would later
	// write through dangling references and trigger a pool-returned event,
	// producing EXCEPTION_ACCESS_VIOLATION (issue #128 item 5).
	struct FSharedExecState
	{
		FCriticalSection EventMutex;
		FEvent* DoneEvent = nullptr;
		TSharedPtr<FJsonValue> Result;
		FThreadSafeBool bAbandoned{false};
		// Modal-safe work is queued twice - once on the core ticker, once on
		// the modal loop drain - because only one of the two runs depending on
		// what the game thread is doing. Whichever gets here first claims it.
		FThreadSafeCounter Claimed;
	};

	// MPSC: any socket thread can enqueue, only the game thread drains.
	TQueue<TFunction<void()>, EQueueMode::Mpsc> GModalSafeQueue;
}

void FMCPGameThreadExecutor::DrainModalSafeQueue()
{
	if (!IsInGameThread())
	{
		return;
	}

	TFunction<void()> Work;
	while (GModalSafeQueue.Dequeue(Work))
	{
		Work();
	}
}

TSharedPtr<FJsonValue> FMCPGameThreadExecutor::ExecuteOnGameThread(FHandlerFunction Handler, const TSharedPtr<FJsonObject>& Params, float TimeoutSeconds, bool bModalSafe)
{
	if (!bEditorReady)
	{
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetStringField(TEXT("error"), TEXT("Editor is still initializing. Please wait and retry."));
		return MakeShared<FJsonValueObject>(ErrorObject);
	}

	if (IsGameThread())
	{
		// Already on game thread, execute directly
		FHandlerInFlightScope InFlight; // #603
		return Handler(Params);
	}

	// Use FTSTicker to run on the game thread tick loop (NOT inside TaskGraph).
	// This avoids the TaskGraph recursion assertion when handlers trigger
	// subsystems like InterchangeEngine that schedule their own TaskGraph work.
	TSharedRef<FSharedExecState> State = MakeShared<FSharedExecState>();
	State->DoneEvent = FPlatformProcess::GetSynchEventFromPool();

	// Capture Handler and Params by value so they outlive the caller's stack
	// if the caller abandons the wait.
	auto RunOnce = [State, Handler, Params]()
	{
		// Caller already gave up — skip the work entirely. Python may
		// still be mid-execution; we cannot safely cancel it, but we
		// can avoid starting it.
		if (State->bAbandoned)
		{
			return;
		}

		// Queued on two paths when modal-safe; run on exactly one of them.
		if (State->Claimed.Set(1) != 0)
		{
			return;
		}

		// Safety: verify GEditor is available before running handlers
		if (!GEditor)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("error"), TEXT("Editor world not ready yet. Retry in a moment."));
			State->Result = MakeShared<FJsonValueObject>(ErrorObject);
		}
		else
		{
			FHandlerInFlightScope InFlight; // #603
			State->Result = Handler(Params);
		}

		// Trigger the event only if it is still live (i.e. the caller
		// has not already returned it to the pool). The mutex serialises
		// with the caller's Return-to-pool below.
		FScopeLock Lock(&State->EventMutex);
		if (State->DoneEvent)
		{
			State->DoneEvent->Trigger();
		}
	};

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([RunOnce](float) -> bool
		{
			RunOnce();
			return false; // one-shot — do not re-tick
		})
	);

	if (bModalSafe)
	{
		GModalSafeQueue.Enqueue(RunOnce);
	}

	// Block the calling thread until the ticker fires, the timeout expires, or
	// the bridge starts shutting down.
	//
	// #821: waiting in slices rather than one long wait is what lets shutdown
	// reclaim this thread. Module teardown runs on the game thread, so once it
	// has begun the queued ticker will never fire, and a single Wait(30s) here
	// (or the several minutes some handlers are allowed) would hold the editor
	// open for the whole of it.
	const uint32 TimeoutMs = static_cast<uint32>(TimeoutSeconds * 1000.0f);
	constexpr uint32 SliceMs = 50;
	uint32 WaitedMs = 0;
	bool bCompleted = false;
	while (WaitedMs < TimeoutMs)
	{
		const uint32 ThisSliceMs = FMath::Min(SliceMs, TimeoutMs - WaitedMs);
		if (State->DoneEvent->Wait(ThisSliceMs))
		{
			bCompleted = true;
			break;
		}
		WaitedMs += ThisSliceMs;
		if (bShuttingDown)
		{
			break;
		}
	}

	if (!bCompleted)
	{
		State->bAbandoned = true;
	}

	// Return the event under the same mutex the ticker uses. If the ticker
	// is about to Trigger, it will block until we null the pointer, then
	// skip. If the ticker has not yet run, the lambda's bAbandoned check
	// will cause it to exit without touching the event.
	{
		FScopeLock Lock(&State->EventMutex);
		FPlatformProcess::ReturnSynchEventToPool(State->DoneEvent);
		State->DoneEvent = nullptr;
	}

	if (!bCompleted)
	{
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetStringField(TEXT("error"), bShuttingDown
			? TEXT("Editor is shutting down; the request was not run.")
			: TEXT("Handler execution timed out"));
		return MakeShared<FJsonValueObject>(ErrorObject);
	}

	return State->Result;
}
