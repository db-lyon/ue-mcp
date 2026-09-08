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
	// timeout) and the game-thread runners (which complete the work).
	// Held by shared pointer so its lifetime extends past the caller's stack
	// frame - critical when the caller times out on a long Python script.
	// Without this shared state, a runner would later write through dangling
	// references and trigger a pool-returned event, producing
	// EXCEPTION_ACCESS_VIOLATION (issue #128 item 5).
	struct FSharedExecState
	{
		FCriticalSection EventMutex;
		FEvent* DoneEvent = nullptr;
		TSharedPtr<FJsonValue> Result;
		FThreadSafeBool bAbandoned{false};
		// Modal-safe work is offered to two runners - the core ticker and the
		// modal loop drain - because only one of the two runs depending on
		// what the game thread is doing. Whichever gets here first claims it.
		FThreadSafeCounter Claimed;

		// The request itself lives here rather than in a lambda copy per
		// runner. A lambda that captured this state would keep the state alive
		// from wherever it was parked, and that is exactly what let the
		// modal-safe queue pin one handler copy and one whole params object
		// per call for the life of the process.
		FMCPGameThreadExecutor::FHandlerFunction Handler;
		TSharedPtr<FJsonObject> Params;
		bool bModalSafe = false;

		void Run();
	};

	void FSharedExecState::Run()
	{
		// Caller already gave up - skip the work entirely. Python may
		// still be mid-execution; we cannot safely cancel it, but we
		// can avoid starting it.
		if (bAbandoned)
		{
			return;
		}

		// Offered to two runners when modal-safe; run on exactly one of them.
		if (Claimed.Set(1) != 0)
		{
			return;
		}

		// Safety: verify GEditor is available before running handlers.
		//
		// #968: except for the modal-safe ones. They go through Slate and check
		// FSlateApplication for themselves, so GEditor is not something they
		// need, and refusing them here would put the startup deadlock back one
		// layer down: a dialog raised before GEditor exists is exactly the one
		// nothing else can clear.
		if (!GEditor && !bModalSafe)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("error"), TEXT("Editor world not ready yet. Retry in a moment."));
			Result = MakeShared<FJsonValueObject>(ErrorObject);
		}
		else
		{
			FHandlerInFlightScope InFlight; // #603
			Result = Handler(Params);
		}

		// Trigger the event only if it is still live (i.e. the caller
		// has not already returned it to the pool). The mutex serialises
		// with the caller's Return-to-pool.
		FScopeLock Lock(&EventMutex);
		if (DoneEvent)
		{
			DoneEvent->Trigger();
		}
	}

	// MPSC: any socket thread can enqueue, only the game thread drains.
	//
	// Weak, and swept when stale. DrainModalSafeQueue has one caller and it is
	// the Slate modal-loop tick, so with no dialog on screen it never fires.
	// A strong entry per modal-safe call therefore never came off again, and
	// list_dialogs and get_dialog_policy are ordinary polling reads an agent
	// makes with no dialog present: an hour of once-a-second polling parked
	// 3600 live handler copies and 3600 whole request params objects in a
	// process-lifetime queue. A weak entry owns none of that, and the sweep
	// below drops it as soon as the work it named is over.
	TQueue<TWeakPtr<FSharedExecState>, EQueueMode::Mpsc> GModalSafeQueue;

	// Drop every queue entry whose work is finished, abandoned, or gone.
	//
	// Game thread only. TQueue in Mpsc mode permits exactly one consumer, and
	// every caller of this and of DrainModalSafeQueue runs on the game thread,
	// so no two are ever inside the queue at the same time.
	void SweepModalSafeQueueImpl()
	{
		while (TWeakPtr<FSharedExecState>* Head = GModalSafeQueue.Peek())
		{
			const TSharedPtr<FSharedExecState> Pinned = Head->Pin();
			// Alive, unclaimed and not abandoned means a caller is still
			// waiting for the modal loop to run this one. Stop here: it is the
			// oldest entry, and popping past it would drop live work.
			if (Pinned.IsValid() && Pinned->Claimed.GetValue() == 0 && !Pinned->bAbandoned)
			{
				break;
			}
			GModalSafeQueue.Pop();
		}
	}
}

void FMCPGameThreadExecutor::SweepModalSafeQueue()
{
	if (!IsInGameThread())
	{
		return;
	}
	SweepModalSafeQueueImpl();
}

void FMCPGameThreadExecutor::DrainModalSafeQueue()
{
	if (!IsInGameThread())
	{
		return;
	}

	TWeakPtr<FSharedExecState> Weak;
	while (GModalSafeQueue.Dequeue(Weak))
	{
		// A pin that fails means the caller returned and the ticker copy is
		// already destroyed: nobody is waiting for this and there is nothing
		// left to dereference. A pin that succeeds holds the state for the
		// whole call, so a caller returning underneath cannot free it
		// mid-handler.
		if (const TSharedPtr<FSharedExecState> Pinned = Weak.Pin())
		{
			Pinned->Run();
		}
	}
}

TSharedPtr<FJsonValue> FMCPGameThreadExecutor::ExecuteOnGameThread(FHandlerFunction Handler, const TSharedPtr<FJsonObject>& Params, float TimeoutSeconds, bool bModalSafe)
{
	// #968: the readiness gate does not apply to the handlers whose job is to
	// clear a block. A modal raised during startup (the "Restore Packages"
	// prompt after an unclean shutdown) holds the game thread before the editor
	// is ready, and this gate then rejected respond_to_dialog and
	// set_dialog_policy - the two calls that could dismiss it - with "Editor is
	// still initializing", while get_engine_state cheerfully described the
	// dialog down to its button labels because it answers off the game thread.
	// The gate was blocked by the very dialog the call would have dismissed,
	// and the only way out was an OS kill that discarded the user's autosave
	// choice. Modal-safe handlers read or answer the active dialog through
	// Slate and touch nothing that startup has yet to build, so they run.
	if (!bEditorReady && !bModalSafe)
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
	// Handler and Params live in the shared state so they outlive the caller's
	// stack if the caller abandons the wait, without a second copy per runner.
	TSharedRef<FSharedExecState> State = MakeShared<FSharedExecState>();
	State->DoneEvent = FPlatformProcess::GetSynchEventFromPool();
	State->Handler = Handler;
	State->Params = Params;
	State->bModalSafe = bModalSafe;

	// The ticker delegate and this stack frame hold the only strong references,
	// so the work cannot outlive the pair of them.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([State](float) -> bool
		{
			State->Run();
			// Every call registers one of these, so this is where the modal-safe
			// queue is swept: a finished entry comes off here rather than
			// waiting for a dialog that may never appear. While a modal is up
			// the core ticker does not tick, but then the modal loop is
			// draining the queue outright.
			SweepModalSafeQueueImpl();
			return false; // one-shot - do not re-tick
		})
	);

	if (bModalSafe)
	{
		GModalSafeQueue.Enqueue(TWeakPtr<FSharedExecState>(State));
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

	// Return the event under the same mutex the runners use. If one is about
	// to Trigger, it will block until we null the pointer, then skip. If
	// neither has run yet, Run's bAbandoned check makes it exit without
	// touching the event.
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
