#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeBool.h"

class FMCPGameThreadExecutor
{
public:
	// Handler function signature
	using FHandlerFunction = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params)>;

	FMCPGameThreadExecutor();
	~FMCPGameThreadExecutor();

	// Execute handler on game thread with timeout.
	//
	// bModalSafe additionally queues the work to run from inside Slate's modal
	// loop. The core ticker does not tick there, so while a dialog is up every
	// normal request times out - including respond_to_dialog, the one call that
	// could clear the dialog. Handlers that only read or answer the active
	// dialog are safe to run in that loop and get unstuck this way; nothing
	// else should set it.
	TSharedPtr<FJsonValue> ExecuteOnGameThread(FHandlerFunction Handler, const TSharedPtr<FJsonObject>& Params, float TimeoutSeconds = 30.0f, bool bModalSafe = false);

	// Run any modal-safe work that was queued while a dialog blocked the
	// engine loop. Called from the Slate modal loop tick. Game thread only.
	static void DrainModalSafeQueue();

	// Check if we're on game thread
	static bool IsGameThread();

	// #603: true while a bridge handler is executing on the game thread. Lets the
	// dialog hook tell a bridge-initiated modal (auto-answer) from a user-raised
	// one (must reach the human). Game-thread only.
	static bool IsHandlerInFlight();

	// Mark the editor as fully initialized and ready to process requests
	void SetEditorReady();

	// Check if the editor is ready
	bool IsEditorReady() const { return bEditorReady; }

	// #821: stop blocking callers. The game thread is inside module teardown by
	// the time the bridge shuts down, so it will never run the queued ticker,
	// and a socket thread waiting the full handler timeout there is a socket
	// thread that holds the editor open for exactly that long. Once this is set
	// an in-flight wait gives up at its next slice.
	void BeginShutdown() { bShuttingDown = true; }
	bool IsShuttingDown() const { return bShuttingDown; }

private:
	FThreadSafeBool bEditorReady{false};
	FThreadSafeBool bShuttingDown{false};
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
