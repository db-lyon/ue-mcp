#include "GameThreadExecutor.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"

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
	};
}

TSharedPtr<FJsonValue> FMCPGameThreadExecutor::ExecuteOnGameThread(FHandlerFunction Handler, const TSharedPtr<FJsonObject>& Params, float TimeoutSeconds)
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
		return Handler(Params);
	}

	// Use FTSTicker to run on the game thread tick loop (NOT inside TaskGraph).
	// This avoids the TaskGraph recursion assertion when handlers trigger
	// subsystems like InterchangeEngine that schedule their own TaskGraph work.
	TSharedRef<FSharedExecState> State = MakeShared<FSharedExecState>();
	State->DoneEvent = FPlatformProcess::GetSynchEventFromPool();

	// Capture Handler and Params by value so they outlive the caller's stack
	// if the caller abandons the wait.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([State, Handler = MoveTemp(Handler), Params](float) -> bool
		{
			// Caller already gave up — skip the work entirely. Python may
			// still be mid-execution; we cannot safely cancel it, but we
			// can avoid starting it.
			if (State->bAbandoned)
			{
				return false;
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
			return false; // one-shot — do not re-tick
		})
	);

	// Block calling thread until the ticker fires or timeout
	uint32 TimeoutMs = static_cast<uint32>(TimeoutSeconds * 1000.0f);
	bool bCompleted = State->DoneEvent->Wait(TimeoutMs);

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
		ErrorObject->SetStringField(TEXT("error"), TEXT("Handler execution timed out"));
		return MakeShared<FJsonValueObject>(ErrorObject);
	}

	return State->Result;
}
