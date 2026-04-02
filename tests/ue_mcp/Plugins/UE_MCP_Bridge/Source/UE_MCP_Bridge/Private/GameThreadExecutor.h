#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"

class FMCPGameThreadExecutor
{
public:
	// Handler function signature
	using FHandlerFunction = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params)>;

	FMCPGameThreadExecutor();
	~FMCPGameThreadExecutor();

	// Execute handler on game thread with timeout
	TSharedPtr<FJsonValue> ExecuteOnGameThread(FHandlerFunction Handler, const TSharedPtr<FJsonObject>& Params, float TimeoutSeconds = 30.0f);

	// Check if we're on game thread
	static bool IsGameThread();

private:
	// Pending execution info
	struct FPendingExecution
	{
		FHandlerFunction Handler;
		TSharedPtr<FJsonObject> Params;
		TSharedPtr<TFuture<TSharedPtr<FJsonValue>>> Future;
		double StartTime;
		float TimeoutSeconds;
	};

	TArray<FPendingExecution> PendingExecutions;
	FCriticalSection ExecutionMutex;
};
