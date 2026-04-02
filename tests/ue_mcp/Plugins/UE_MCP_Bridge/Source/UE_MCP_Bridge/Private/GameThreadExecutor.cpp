#include "GameThreadExecutor.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"

FMCPGameThreadExecutor::FMCPGameThreadExecutor()
{
}

FMCPGameThreadExecutor::~FMCPGameThreadExecutor()
{
}

bool FMCPGameThreadExecutor::IsGameThread()
{
	return IsInGameThread();
}

TSharedPtr<FJsonValue> FMCPGameThreadExecutor::ExecuteOnGameThread(FHandlerFunction Handler, const TSharedPtr<FJsonObject>& Params, float TimeoutSeconds)
{
	if (IsGameThread())
	{
		// Already on game thread, execute directly
		return Handler(Params);
	}

	// Use FTSTicker to run on the game thread tick loop (NOT inside TaskGraph).
	// This avoids the TaskGraph recursion assertion when handlers trigger
	// subsystems like InterchangeEngine that schedule their own TaskGraph work.
	TSharedPtr<FJsonValue> Result;
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool();

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([&Result, &Handler, &Params, DoneEvent](float) -> bool
		{
			Result = Handler(Params);
			DoneEvent->Trigger();
			return false; // one-shot — do not re-tick
		})
	);

	// Block calling thread until the ticker fires or timeout
	uint32 TimeoutMs = static_cast<uint32>(TimeoutSeconds * 1000.0f);
	bool bCompleted = DoneEvent->Wait(TimeoutMs);
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	if (!bCompleted)
	{
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetStringField(TEXT("error"), TEXT("Handler execution timed out"));
		return MakeShared<FJsonValueObject>(ErrorObject);
	}

	return Result;
}
