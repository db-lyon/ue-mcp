#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Dom/JsonObject.h"

class FRunnableThread;

DECLARE_LOG_CATEGORY_EXTERN(LogMCPBridgeStatus, Log, All);

/**
 * A picture of what the engine is doing, kept up to date by the game thread and
 * readable by everyone else.
 *
 * Every other sensor in the bridge routes through FMCPGameThreadExecutor, so
 * the instant the game thread stops returning to the tick loop - a modal
 * dialog, a long FSlowTask, a hitching import - every request degrades to
 * "Handler execution timed out" and the caller learns nothing about why. This
 * class answers that question from outside:
 *
 *   - the game thread refreshes the snapshot from hooks that keep firing while
 *     a long operation or a modal dialog owns the main loop, which is exactly
 *     when the core ticker is suspended;
 *   - the socket thread serves the snapshot directly, never scheduling game
 *     thread work;
 *   - a writer thread flushes it to Saved/UE_MCP_Bridge/status.json so the
 *     state survives a genuinely wedged process and is readable before a
 *     WebSocket client has connected at all.
 *
 * This lives in its own module so it can load at PostConfigInit, long before
 * the bridge module (PostEngineInit). Its two Core-only hooks - the core
 * ticker and, on UE 5.8+, FCoreDelegates::ApplicationHeartbeat, which every
 * slow-task progress frame broadcasts - cover the entire startup window that
 * used to be invisible: asset registry scan, startup shader compilation, map
 * load. Earlier engine versions have no heartbeat delegate; see Install() for
 * what covers those frames there and what it costs.
 *
 * Sources that need Slate or Engine are injected by the bridge module when it
 * loads (SetModalProvider / SetCompileProvider / CaptureNow from Slate ticks),
 * because neither is reliably up at PostConfigInit.
 *
 * Every field is guarded by a single critical section. Captures are cheap
 * (reading GWarn's scope stack and two counters), so running one per Slate
 * tick is noise next to the frame itself.
 */
class UE_MCP_BRIDGESTATUS_API FMCPEngineStatus : public FRunnable
{
public:
	static FMCPEngineStatus& Get();

	/** Install the Core-only hooks and start the status writer thread. */
	void Install();

	/** Remove hooks and stop the writer thread. Safe to call twice. */
	void Shutdown();

	/** Thread-safe. Never touches the game thread. */
	TSharedPtr<FJsonObject> Snapshot() const;

	/**
	 * The modal blocking the editor right now, read off the snapshot rather
	 * than by walking Slate, so it is safe on the socket thread. The snapshot
	 * is refreshed from the modal-loop tick, which is the one tick that keeps
	 * firing while a modal is up, so this is current rather than stale.
	 *
	 * Returns false and leaves the outputs alone when nothing is modal.
	 */
	bool GetActiveModal(FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons) const;

	/** Coarse lifecycle label ("config init", "modules loaded", "ready"). */
	void SetPhase(const FString& InPhase);

	/** Called by the socket thread around a dispatched request. */
	void NoteHandlerBegin(const FString& Method);
	void NoteHandlerEnd(const FString& Method);

	/** Refresh the snapshot. Game thread only; no-op elsewhere. */
	void CaptureNow();

	/**
	 * Describes the modal dialog blocking the editor, if any. Supplied by the
	 * bridge module, which owns the Slate widget walk. Until it loads, the
	 * snapshot simply reports no dialog rather than pretending to know.
	 */
	using FModalProvider = TFunction<bool(FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons)>;
	void SetModalProvider(FModalProvider Provider);

	/** Remaining shader jobs and asset compiles. Supplied by the bridge module. */
	using FCompileProvider = TFunction<void(int32& OutShaderJobs, int32& OutAssetCompiles)>;
	void SetCompileProvider(FCompileProvider Provider);

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

	/** <Project>/Saved/UE_MCP_Bridge. */
	static FString StatusDir();

	/**
	 * The shared `status.json`, still written so a client older than #990 finds
	 * a snapshot where it has always looked.
	 */
	static FString StatusFilePath();

	/**
	 * `status.<pid>.json`, this process's own. One project directory can hold
	 * the status of several processes, and a single shared document describes
	 * only whichever of them wrote last (#990).
	 */
	static FString InstanceStatusFilePath();

	/**
	 * False in a commandlet. Nothing polls a commandlet's engine state, and a
	 * distributed build runs dozens of them against one project directory,
	 * where they used to fight over one file and turn a successful build into
	 * thousands of Error lines (#990).
	 */
	static bool ShouldPublishStatus();

private:
	struct FSlowTaskEntry
	{
		FString Name;
		float Fraction = 0.0f;
	};

	/** Serialise under the lock, then write atomically (temp file + move). */
	void FlushToDisk();

	/** Temp file plus rename, with the temp file named for this process. */
	static bool PublishAtomically(const FString& FinalPath, const FString& Contents);

	/** Drop `status.<pid>.json` files left by processes that are gone. */
	static void RemoveStaleInstanceStatusFiles();

	/** Does the shared status.json currently name this process? */
	static bool SharedStatusBelongsToThisProcess();

	mutable FCriticalSection Mutex;

	FString Phase = TEXT("config init");
	double LastCaptureSeconds = 0.0;
	double InstallSeconds = 0.0;

	bool bSlowTaskActive = false;
	FString SlowTaskName;
	float SlowTaskFraction = 0.0f;
	TArray<FSlowTaskEntry> SlowTaskStack;

	bool bModalActive = false;
	FString ModalTitle;
	FString ModalMessage;
	TArray<FString> ModalButtons;

	int32 RemainingShaderJobs = 0;
	int32 RemainingAssetCompiles = 0;

	FString HandlerMethod;
	double HandlerStartSeconds = 0.0;

	// Startup runs before there is a tick loop to stall, so "the game thread
	// has not ticked for 40 seconds" is true and useless until the engine loop
	// is actually running. Until then the snapshot reports the stall as null
	// and says so, and progress comes from module loading and slow-task
	// boundaries instead.
	bool bGameThreadTicking = false;
	int32 ModulesLoaded = 0;

	// Written on the game thread before the first capture that uses them, read
	// on the game thread only, so they need no lock of their own.
	FModalProvider ModalProvider;
	FCompileProvider CompileProvider;

	/** Bind the slow-task events on whichever feedback context GWarn is now. */
	void RebindFeedbackContextIfNeeded();

	/** Bound on UE 5.8+ only, where the heartbeat delegate exists. */
	FDelegateHandle HeartbeatHandle;
	FDelegateHandle ModulesChangedHandle;
	FDelegateHandle SlowTaskStartHandle;
	FDelegateHandle SlowTaskFinalizeHandle;
	/** The context the two handles above belong to. GWarn is swapped during startup. */
	class FFeedbackContext* BoundContext = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;

	FRunnableThread* WriterThread = nullptr;
	FThreadSafeBool bStopWriter{false};
	bool bInstalled = false;
};
