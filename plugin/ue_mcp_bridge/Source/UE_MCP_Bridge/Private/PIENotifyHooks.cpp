#include "PIENotifyHooks.h"

#include "Editor.h"
#include "Misc/DateTime.h"

namespace
{
	FMCPPieNotifyHooks::FSink GPieNotifySink;
	FDelegateHandle GPieStartedHandle;
	FDelegateHandle GPieEndedHandle;
	FDelegateHandle GPiePausedHandle;
	FDelegateHandle GPieResumedHandle;

	void SendPieNotification(const TCHAR* State, bool bSimulate)
	{
		if (GPieNotifySink)
		{
			GPieNotifySink(FMCPPieNotifyHooks::Method(), FMCPPieNotifyHooks::MakeParams(State, bSimulate));
		}
	}
}

TSharedRef<FJsonObject> FMCPPieNotifyHooks::MakeParams(const TCHAR* State, bool bSimulate)
{
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("state"), State);
	Params->SetBoolField(TEXT("simulate"), bSimulate);
	Params->SetStringField(TEXT("at"), FDateTime::UtcNow().ToIso8601());
	return Params;
}

void FMCPPieNotifyHooks::Install(FSink Sink)
{
	Remove();
	GPieNotifySink = MoveTemp(Sink);
	// PostPIEStarted rather than BeginPIE: BeginPIE fires before the play world
	// exists, and a launch that fails after it never reaches EndPIE's pair.
	GPieStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda([](const bool bSimulate) { SendPieNotification(TEXT("started"), bSimulate); });
	GPieEndedHandle = FEditorDelegates::EndPIE.AddLambda([](const bool bSimulate) { SendPieNotification(TEXT("ended"), bSimulate); });
	GPiePausedHandle = FEditorDelegates::PausePIE.AddLambda([](const bool bSimulate) { SendPieNotification(TEXT("paused"), bSimulate); });
	GPieResumedHandle = FEditorDelegates::ResumePIE.AddLambda([](const bool bSimulate) { SendPieNotification(TEXT("resumed"), bSimulate); });
}

void FMCPPieNotifyHooks::Remove()
{
	if (GPieStartedHandle.IsValid()) FEditorDelegates::PostPIEStarted.Remove(GPieStartedHandle);
	if (GPieEndedHandle.IsValid()) FEditorDelegates::EndPIE.Remove(GPieEndedHandle);
	if (GPiePausedHandle.IsValid()) FEditorDelegates::PausePIE.Remove(GPiePausedHandle);
	if (GPieResumedHandle.IsValid()) FEditorDelegates::ResumePIE.Remove(GPieResumedHandle);
	GPieStartedHandle.Reset();
	GPieEndedHandle.Reset();
	GPiePausedHandle.Reset();
	GPieResumedHandle.Reset();
	GPieNotifySink = nullptr;
}
