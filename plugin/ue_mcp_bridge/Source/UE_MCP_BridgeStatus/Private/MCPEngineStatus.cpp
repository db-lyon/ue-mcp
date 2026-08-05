#include "MCPEngineStatus.h"

#include "CoreGlobals.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SlowTask.h"
#include "Misc/SlowTaskStack.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY(LogMCPBridgeStatus);

namespace
{
	constexpr float StatusFlushIntervalSeconds = 0.25f;

	/** A slow task's most specific label: this frame's message, else the scope's. */
	FString SlowTaskLabel(const FSlowTask& Task)
	{
		const FString FrameMessage = Task.FrameMessage.ToString();
		return FrameMessage.IsEmpty() ? Task.DefaultMessage.ToString() : FrameMessage;
	}
}

FMCPEngineStatus& FMCPEngineStatus::Get()
{
	static FMCPEngineStatus Instance;
	return Instance;
}

void FMCPEngineStatus::Install()
{
	if (bInstalled)
	{
		return;
	}
	bInstalled = true;
	InstallSeconds = FPlatformTime::Seconds();
	LastCaptureSeconds = InstallSeconds;

	// Two Core-only capture hooks, which is all that exists this early:
	//
	//  - the core ticker covers ordinary frames;
	//  - the application heartbeat is broadcast by
	//    FFeedbackContext::ProgressReported on every slow-task progress frame,
	//    so an operation that advances its progress bar without ever returning
	//    to the tick loop still refreshes the percentage. Startup is almost
	//    entirely made of those.
	//
	// The bridge module adds Slate's pre-tick and the modal loop tick when it
	// loads at PostEngineInit.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float) -> bool
		{
			// The core ticker only runs once the engine loop is up, which is
			// exactly the point where a stall figure starts meaning something.
			{
				FScopeLock Lock(&Mutex);
				bGameThreadTicking = true;
			}
			CaptureNow();
			return true;
		}), 0.0f);

	HeartbeatHandle = FCoreDelegates::ApplicationHeartbeat.AddLambda([this]()
	{
		CaptureNow();
	});

	// Neither of the above fires during PreInit: there is no tick loop yet, and
	// the heartbeat rides on slow-task progress reporting that startup does not
	// always route through. These two do fire, and they are what makes the
	// window between PostConfigInit and the first frame legible:
	//
	//  - every module that finishes loading (hundreds of them, and the bulk of
	//    a cold start), which also proves the process is still moving;
	//  - every slow task scope opening or closing, which is where the names
	//    come from ("Loading Map", "Compiling Shaders").
	ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddLambda(
		[this](FName, EModuleChangeReason Reason)
		{
			if (Reason == EModuleChangeReason::ModuleLoaded)
			{
				++ModulesLoaded;
			}
			CaptureNow();
		});

	RebindFeedbackContextIfNeeded();

	bStopWriter = false;
	WriterThread = FRunnableThread::Create(this, TEXT("MCPEngineStatusWriter"), 0, TPri_BelowNormal);

	UE_LOG(LogMCPBridgeStatus, Log, TEXT("[UE-MCP] Engine status snapshot installed -> %s"), *StatusFilePath());
}

void FMCPEngineStatus::Shutdown()
{
	if (!bInstalled)
	{
		return;
	}
	bInstalled = false;

	bStopWriter = true;
	if (WriterThread)
	{
		WriterThread->Kill(true);
		delete WriterThread;
		WriterThread = nullptr;
	}

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (HeartbeatHandle.IsValid())
	{
		FCoreDelegates::ApplicationHeartbeat.Remove(HeartbeatHandle);
		HeartbeatHandle.Reset();
	}

	if (ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);
		ModulesChangedHandle.Reset();
	}

	if (BoundContext)
	{
		if (SlowTaskStartHandle.IsValid())
		{
			BoundContext->OnStartSlowTask().Remove(SlowTaskStartHandle);
			SlowTaskStartHandle.Reset();
		}
		if (SlowTaskFinalizeHandle.IsValid())
		{
			BoundContext->OnFinalizeSlowTask().Remove(SlowTaskFinalizeHandle);
			SlowTaskFinalizeHandle.Reset();
		}
		BoundContext = nullptr;
	}

	ModalProvider = nullptr;
	CompileProvider = nullptr;

	// Leave no stale file behind: a status.json from a dead editor claiming a
	// 40% slow task is worse than no file at all.
	IFileManager::Get().Delete(*StatusFilePath(), false, false, true);
}

void FMCPEngineStatus::RebindFeedbackContextIfNeeded()
{
	// GWarn is swapped from the bootstrap context to FFeedbackContextEditor
	// partway through startup, taking any bindings with it, so check identity
	// rather than binding once and assuming it holds.
	if (!GWarn || GWarn == BoundContext)
	{
		return;
	}

	if (BoundContext)
	{
		if (SlowTaskStartHandle.IsValid())
		{
			BoundContext->OnStartSlowTask().Remove(SlowTaskStartHandle);
		}
		if (SlowTaskFinalizeHandle.IsValid())
		{
			BoundContext->OnFinalizeSlowTask().Remove(SlowTaskFinalizeHandle);
		}
	}

	BoundContext = GWarn;
	SlowTaskStartHandle = GWarn->OnStartSlowTask().AddLambda([this](const FText&)
	{
		CaptureNow();
	});
	SlowTaskFinalizeHandle = GWarn->OnFinalizeSlowTask().AddLambda([this](const FText&, double)
	{
		CaptureNow();
	});
}

void FMCPEngineStatus::SetPhase(const FString& InPhase)
{
	FScopeLock Lock(&Mutex);
	Phase = InPhase;
}

void FMCPEngineStatus::SetModalProvider(FModalProvider Provider)
{
	ModalProvider = MoveTemp(Provider);
}

void FMCPEngineStatus::SetCompileProvider(FCompileProvider Provider)
{
	CompileProvider = MoveTemp(Provider);
}

void FMCPEngineStatus::NoteHandlerBegin(const FString& Method)
{
	FScopeLock Lock(&Mutex);
	HandlerMethod = Method;
	HandlerStartSeconds = FPlatformTime::Seconds();
}

void FMCPEngineStatus::NoteHandlerEnd(const FString& Method)
{
	FScopeLock Lock(&Mutex);
	// Requests are dispatched one at a time per connection, but a second
	// connection can overlap; only clear the entry that is actually ours.
	if (HandlerMethod == Method)
	{
		HandlerMethod.Empty();
		HandlerStartSeconds = 0.0;
	}
}

void FMCPEngineStatus::CaptureNow()
{
	if (!IsInGameThread())
	{
		return;
	}

	RebindFeedbackContextIfNeeded();

	// Read everything first, then take the lock once. The modal provider walks
	// the Slate widget tree, which must not happen under a lock the socket
	// thread is waiting on.
	FString LocalSlowName;
	float LocalSlowFraction = 0.0f;
	TArray<FSlowTaskEntry> LocalStack;
	bool bLocalSlowActive = false;

	if (GWarn)
	{
		// The scope stack is what the editor's own progress dialog renders, so
		// this is literally the percentage the user is looking at.
		const FSlowTaskStack& Stack = GWarn->GetScopeStack();
		for (int32 Index = 0; Index < Stack.Num(); ++Index)
		{
			const FSlowTask* Task = Stack[Index];
			if (!Task)
			{
				continue;
			}
			FSlowTaskEntry Entry;
			Entry.Name = SlowTaskLabel(*Task);
			Entry.Fraction = Stack.GetProgressFraction(Index);
			LocalStack.Add(MoveTemp(Entry));
		}
		if (LocalStack.Num() > 0)
		{
			bLocalSlowActive = true;
			LocalSlowFraction = LocalStack[0].Fraction;
			// Report the innermost scope that actually has a label: outer
			// scopes are often unnamed wrappers.
			for (int32 Index = LocalStack.Num() - 1; Index >= 0; --Index)
			{
				if (!LocalStack[Index].Name.IsEmpty())
				{
					LocalSlowName = LocalStack[Index].Name;
					break;
				}
			}
		}
	}

	FString LocalModalTitle;
	FString LocalModalMessage;
	TArray<FString> LocalModalButtons;
	const bool bDescribedModal = ModalProvider
		? ModalProvider(LocalModalTitle, LocalModalMessage, LocalModalButtons)
		: false;

	// The editor's own slow-task progress window is an active modal window: it
	// carries the progress text ("Compiling Shaders (9)") but no title and no
	// buttons. Reporting it as a dialog would tell callers a human answer is
	// needed when none is, so require a title or a button - the two things
	// every prompt has and no progress window does. The progress text is
	// already reported, accurately, as a slow task.
	const bool bLocalModal = bDescribedModal
		&& (!LocalModalTitle.IsEmpty() || LocalModalButtons.Num() > 0);

	// A details-view dialog yields kilobytes of widget text. The snapshot is
	// rewritten to disk four times a second, so keep it to the part that
	// identifies the prompt; list_dialogs still returns the whole thing.
	constexpr int32 MaxModalMessageChars = 1500;
	if (LocalModalMessage.Len() > MaxModalMessageChars)
	{
		LocalModalMessage = LocalModalMessage.Left(MaxModalMessageChars) + TEXT("... (truncated; call list_dialogs for the full text)");
	}

	int32 LocalShaderJobs = 0;
	int32 LocalAssetCompiles = 0;
	if (CompileProvider)
	{
		CompileProvider(LocalShaderJobs, LocalAssetCompiles);
	}

	const double Now = FPlatformTime::Seconds();

	{
		FScopeLock Lock(&Mutex);
		LastCaptureSeconds = Now;
		bSlowTaskActive = bLocalSlowActive;
		SlowTaskName = MoveTemp(LocalSlowName);
		SlowTaskFraction = LocalSlowFraction;
		SlowTaskStack = MoveTemp(LocalStack);
		bModalActive = bLocalModal;
		ModalTitle = MoveTemp(LocalModalTitle);
		ModalMessage = MoveTemp(LocalModalMessage);
		ModalButtons = MoveTemp(LocalModalButtons);
		RemainingShaderJobs = LocalShaderJobs;
		RemainingAssetCompiles = LocalAssetCompiles;
	}
}

TSharedPtr<FJsonObject> FMCPEngineStatus::Snapshot() const
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	const double Now = FPlatformTime::Seconds();

	FScopeLock Lock(&Mutex);

	Out->SetStringField(TEXT("phase"), Phase);
	Out->SetNumberField(TEXT("uptimeSeconds"), Now - InstallSeconds);
	Out->SetNumberField(TEXT("modulesLoaded"), ModulesLoaded);

	// The single most useful number here: how long the game thread has gone
	// without reaching any of the capture hooks. Everything below is as old as
	// this value says it is.
	//
	// It is meaningless before the engine loop exists, though, and reporting
	// "stalled for 40 seconds" during a normal cold start is a false alarm, so
	// say plainly that the game thread is not ticking yet instead.
	Out->SetBoolField(TEXT("gameThreadTicking"), bGameThreadTicking);
	if (bGameThreadTicking)
	{
		Out->SetNumberField(TEXT("gameThreadStalledSeconds"), Now - LastCaptureSeconds);
	}
	else
	{
		Out->SetField(TEXT("gameThreadStalledSeconds"), MakeShared<FJsonValueNull>());
	}

	if (bSlowTaskActive)
	{
		TSharedPtr<FJsonObject> SlowTask = MakeShared<FJsonObject>();
		SlowTask->SetStringField(TEXT("name"), SlowTaskName);
		SlowTask->SetNumberField(TEXT("fraction"), SlowTaskFraction);

		TArray<TSharedPtr<FJsonValue>> StackJson;
		for (const FSlowTaskEntry& Entry : SlowTaskStack)
		{
			TSharedPtr<FJsonObject> EntryJson = MakeShared<FJsonObject>();
			EntryJson->SetStringField(TEXT("name"), Entry.Name);
			EntryJson->SetNumberField(TEXT("fraction"), Entry.Fraction);
			StackJson.Add(MakeShared<FJsonValueObject>(EntryJson));
		}
		SlowTask->SetArrayField(TEXT("stack"), StackJson);
		Out->SetObjectField(TEXT("slowTask"), SlowTask);
	}
	else
	{
		Out->SetField(TEXT("slowTask"), MakeShared<FJsonValueNull>());
	}

	if (bModalActive)
	{
		TSharedPtr<FJsonObject> Modal = MakeShared<FJsonObject>();
		Modal->SetStringField(TEXT("title"), ModalTitle);
		Modal->SetStringField(TEXT("message"), ModalMessage);
		TArray<TSharedPtr<FJsonValue>> ButtonsJson;
		for (const FString& Button : ModalButtons)
		{
			ButtonsJson.Add(MakeShared<FJsonValueString>(Button));
		}
		Modal->SetArrayField(TEXT("buttons"), ButtonsJson);
		Out->SetObjectField(TEXT("modal"), Modal);
	}
	else
	{
		Out->SetField(TEXT("modal"), MakeShared<FJsonValueNull>());
	}

	TSharedPtr<FJsonObject> Compiling = MakeShared<FJsonObject>();
	Compiling->SetNumberField(TEXT("shaders"), RemainingShaderJobs);
	Compiling->SetNumberField(TEXT("assets"), RemainingAssetCompiles);
	Out->SetObjectField(TEXT("compiling"), Compiling);

	if (!HandlerMethod.IsEmpty())
	{
		TSharedPtr<FJsonObject> Handler = MakeShared<FJsonObject>();
		Handler->SetStringField(TEXT("method"), HandlerMethod);
		Handler->SetNumberField(TEXT("elapsedSeconds"), Now - HandlerStartSeconds);
		Out->SetObjectField(TEXT("handler"), Handler);
	}
	else
	{
		Out->SetField(TEXT("handler"), MakeShared<FJsonValueNull>());
	}

	return Out;
}

FString FMCPEngineStatus::StatusFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP_Bridge"), TEXT("status.json"));
}

void FMCPEngineStatus::FlushToDisk()
{
	TSharedPtr<FJsonObject> Json = Snapshot();
	Json->SetStringField(TEXT("writtenAt"), FDateTime::UtcNow().ToIso8601());

	FString Serialised;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialised);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

	// Write then move: a reader polling at the same rate would otherwise catch
	// a half-written file and report "no snapshot" at the exact moment the
	// snapshot matters.
	const FString Final = StatusFilePath();
	const FString Temp = Final + TEXT(".tmp");
	if (FFileHelper::SaveStringToFile(Serialised, *Temp))
	{
		IFileManager::Get().Move(*Final, *Temp, true, true);
	}
}

uint32 FMCPEngineStatus::Run()
{
	// Deliberately its own thread: the whole point is to keep publishing while
	// the game thread is inside a dialog, a slow task, or a hang.
	while (!bStopWriter)
	{
		FlushToDisk();
		FPlatformProcess::Sleep(StatusFlushIntervalSeconds);
	}
	return 0;
}

void FMCPEngineStatus::Stop()
{
	bStopWriter = true;
}
