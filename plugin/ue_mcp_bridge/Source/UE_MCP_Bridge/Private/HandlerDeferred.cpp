#include "HandlerDeferred.h"
#include "BridgeRequestContext.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

FMCPDeferredCall::FMCPDeferredCall()
	: DoneEvent(FPlatformProcess::GetSynchEventFromPool(true))
{
}

FMCPDeferredCall::~FMCPDeferredCall()
{
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
}

void FMCPDeferredCall::Complete(const TSharedPtr<FJsonValue>& InResult)
{
	FScopeLock Guard(&Lock);
	if (bComplete || bCancelled) return;
	Result = InResult;
	bComplete = true;
	CancelHook = nullptr;
	DoneEvent->Trigger();
}

void FMCPDeferredCall::ReportProgress(double InProgress, double InTotal, const FString& Message)
{
	FScopeLock Guard(&Lock);
	Progress = InProgress;
	Total = InTotal;
	ProgressMessage = Message;
	bProgressPending = true;
}

namespace
{
	// The hook touches engine state, so it runs on the game thread whatever thread cancelled.
	void MCPRunCancelHookOnGameThread(TFunction<void(const FString&)>&& Hook, const FString& Reason)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Hook = MoveTemp(Hook), Reason](float) -> bool
		{
			Hook(Reason);
			return false;
		}));
	}
}

void FMCPDeferredCall::OnCancel(TFunction<void(const FString& Reason)> Hook)
{
	{
		FScopeLock Guard(&Lock);
		if (!bCancelled)
		{
			CancelHook = MoveTemp(Hook);
			return;
		}
	}
	// Cancelled before the handler set its hook: a cancel that raced the start.
	MCPRunCancelHookOnGameThread(MoveTemp(Hook), CancelReason);
}

void FMCPDeferredCall::Cancel(const FString& Reason)
{
	TFunction<void(const FString&)> Hook;
	{
		FScopeLock Guard(&Lock);
		if (bComplete || bCancelled) return;
		bCancelled = true;
		CancelReason = Reason;
		Hook = MoveTemp(CancelHook);
		CancelHook = nullptr;
		DoneEvent->Trigger();
	}
	if (Hook) MCPRunCancelHookOnGameThread(MoveTemp(Hook), Reason);
}

bool FMCPDeferredCall::IsCancelled() const
{
	FScopeLock Guard(&Lock);
	return bCancelled;
}

bool FMCPDeferredCall::IsComplete() const
{
	FScopeLock Guard(&Lock);
	return bComplete;
}

void FMCPDeferredCall::SetCancelNote(const FString& Note)
{
	FScopeLock Guard(&Lock);
	CancelNote = Note;
}

FString FMCPDeferredCall::GetCancelNote() const
{
	FScopeLock Guard(&Lock);
	return CancelNote;
}

bool FMCPDeferredCall::Wait(uint32 Ms)
{
	return DoneEvent->Wait(Ms);
}

TSharedPtr<FJsonValue> FMCPDeferredCall::GetResult() const
{
	FScopeLock Guard(&Lock);
	return Result;
}

bool FMCPDeferredCall::TakeProgress(double& OutProgress, double& OutTotal, FString& OutMessage)
{
	FScopeLock Guard(&Lock);
	if (!bProgressPending) return false;
	bProgressPending = false;
	OutProgress = Progress;
	OutTotal = Total;
	OutMessage = ProgressMessage;
	return true;
}

// ── Request context ───────────────────────────────────────────────────────────

namespace
{
	// Game thread only: handlers run there one at a time.
	TSharedPtr<FMCPRequestContext, ESPMode::ThreadSafe> GMCPCurrentRequest;

	FCriticalSection GMCPRequestRegistryLock;
	TMap<FString, TWeakPtr<FMCPRequestContext, ESPMode::ThreadSafe>> GMCPRequestRegistry;

	const TCHAR* MCPDeferredMarkerField = TEXT("__mcpDeferred");
}

FMCPDeferredCallPtr FMCPRequestContext::GetDeferred() const
{
	FScopeLock Guard(&Lock);
	return Deferred;
}

FMCPDeferredCallRef FMCPRequestContext::MakeDeferred()
{
	FMCPDeferredCallRef Made = MakeShared<FMCPDeferredCall, ESPMode::ThreadSafe>();
	{
		FScopeLock Guard(&Lock);
		Deferred = Made;
	}
	return Made;
}

FString FMCPRequestContext::RequestCancel()
{
	bCancelRequested = true;
	if (FMCPDeferredCallPtr Pending = GetDeferred())
	{
		Pending->Cancel(TEXT("cancelled by the client"));
		return TEXT("deferred");
	}
	return TEXT("running");
}

MCPRequestContext::FScope::FScope(const TSharedPtr<FMCPRequestContext, ESPMode::ThreadSafe>& Context)
	: Previous(GMCPCurrentRequest)
{
	GMCPCurrentRequest = Context;
}

MCPRequestContext::FScope::~FScope()
{
	GMCPCurrentRequest = Previous;
}

void MCPRequestContext::Register(const FMCPRequestContextRef& Context)
{
	if (Context->CancelToken.IsEmpty()) return;
	FScopeLock Guard(&GMCPRequestRegistryLock);
	GMCPRequestRegistry.Add(Context->CancelToken, Context);
}

void MCPRequestContext::Unregister(const FMCPRequestContextRef& Context)
{
	if (Context->CancelToken.IsEmpty()) return;
	FScopeLock Guard(&GMCPRequestRegistryLock);
	const TWeakPtr<FMCPRequestContext, ESPMode::ThreadSafe>* Found = GMCPRequestRegistry.Find(Context->CancelToken);
	if (Found && Found->Pin() == Context) GMCPRequestRegistry.Remove(Context->CancelToken);
}

FString MCPRequestContext::Cancel(const FString& CancelToken)
{
	TSharedPtr<FMCPRequestContext, ESPMode::ThreadSafe> Context;
	{
		FScopeLock Guard(&GMCPRequestRegistryLock);
		if (const TWeakPtr<FMCPRequestContext, ESPMode::ThreadSafe>* Found = GMCPRequestRegistry.Find(CancelToken)) Context = Found->Pin();
	}
	return Context.IsValid() ? Context->RequestCancel() : FString(TEXT("unknown"));
}

FMCPDeferredCallPtr MCPDeferRequest()
{
	check(IsInGameThread());
	if (!GMCPCurrentRequest.IsValid()) return nullptr;
	FMCPDeferredCallRef Deferred = GMCPCurrentRequest->MakeDeferred();
	// A cancel that landed while the handler was still starting the work.
	if (GMCPCurrentRequest->IsCancelRequested()) Deferred->Cancel(TEXT("cancelled by the client"));
	return Deferred;
}

TSharedPtr<FJsonValue> MCPDeferredMarker()
{
	TSharedPtr<FJsonObject> Marker = MakeShared<FJsonObject>();
	Marker->SetBoolField(MCPDeferredMarkerField, true);
	return MakeShared<FJsonValueObject>(Marker);
}

bool MCPIsDeferredMarker(const TSharedPtr<FJsonValue>& Value)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	bool bMarked = false;
	return Value.IsValid() && Value->TryGetObject(Obj) && Obj && (*Obj)->TryGetBoolField(MCPDeferredMarkerField, bMarked) && bMarked;
}
