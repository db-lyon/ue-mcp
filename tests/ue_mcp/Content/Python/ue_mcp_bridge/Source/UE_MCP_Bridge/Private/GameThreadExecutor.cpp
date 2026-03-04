#include "GameThreadExecutor.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"

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

	// Execute on game thread via AsyncTask
	TSharedPtr<TFuture<TSharedPtr<FJsonValue>>> Future = MakeShared<TFuture<TSharedPtr<FJsonValue>>>(
		Async(EAsyncExecution::TaskGraphMainThread, [Handler, Params]() -> TSharedPtr<FJsonValue>
		{
			return Handler(Params);
		})
	);

	// Wait for result with timeout
	double StartTime = FPlatformTime::Seconds();
	while (!Future->IsReady())
	{
		if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
		{
			// Timeout
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("error"), TEXT("Handler execution timed out"));
			return MakeShared<FJsonValueObject>(ErrorObject);
		}
		FPlatformProcess::Sleep(0.01f); // 10ms sleep
	}

	return Future->Get();
}
