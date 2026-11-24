#include "MCPPIERemoteControl.h"

#include "UE_MCP_BridgeRuntimeModule.h"
#include "MCPPIEInputInjector.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/CoreDelegates.h"

namespace
{
	FInputActionValue ToActionValue(const UInputAction* Action, const FVector& V)
	{
		switch (Action->ValueType)
		{
		case EInputActionValueType::Boolean: return FInputActionValue(V.X != 0.0);
		case EInputActionValueType::Axis1D:  return FInputActionValue(static_cast<float>(V.X));
		case EInputActionValueType::Axis2D:  return FInputActionValue(FVector2D(V.X, V.Y));
		case EInputActionValueType::Axis3D:  return FInputActionValue(V);
		}
		return FInputActionValue();
	}

	UInputAction* LoadAction(const FSoftObjectPath& Path, FString& OutError)
	{
		UInputAction* Action = Cast<UInputAction>(Path.TryLoad());
		if (!Action)
		{
			OutError = FString::Printf(TEXT("InputAction '%s' did not load in this process"), *Path.ToString());
		}
		return Action;
	}
}

// ── Server-side report bookkeeping ──────────────────────────────────────────

void FMCPPIERemoteReports::Expect(const FString& Id, bool bFresh)
{
	if (bFresh)
	{
		Results.Remove(Id);
		Triggers.Remove(Id);
	}
	if (Expected.Contains(Id))
	{
		// A follow-up keeps a running request alive: it moves to the newest end, so the cap drops older ones first.
		Order.Remove(Id);
		Order.Add(Id);
		return;
	}
	while (Order.Num() >= MaxRequests)
	{
		const FString Oldest = Order[0];
		Forget(Oldest);
	}
	Order.Add(Id);
	Expected.Add(Id);
}

bool FMCPPIERemoteReports::AcceptReport(const FString& Id, bool bOk, bool bFinished, const FString& Error)
{
	if (!Expected.Contains(Id))
	{
		return false;
	}
	FMCPPIERemoteResult& Result = Results.FindOrAdd(Id);
	Result.Id = Id;
	Result.bOk = bOk;
	Result.bFinished = bFinished;
	Result.Error = Error.Left(MaxTextLength);
	return true;
}

bool FMCPPIERemoteReports::AcceptTrigger(const FString& Id, const FVector& Value)
{
	if (!Expected.Contains(Id))
	{
		return false;
	}
	FMCPPIERemoteTrigger& Trigger = Triggers.FindOrAdd(Id);
	Trigger.Id = Id;
	Trigger.Value = Value;
	return true;
}

void FMCPPIERemoteReports::Forget(const FString& Id)
{
	Order.Remove(Id);
	Expected.Remove(Id);
	Results.Remove(Id);
	Triggers.Remove(Id);
}

void FMCPPIERemoteReports::Reset()
{
	Order.Reset();
	Expected.Reset();
	Results.Reset();
	Triggers.Reset();
}

// ── Component ───────────────────────────────────────────────────────────────

UMCPPIERemoteControl::UMCPPIERemoteControl()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMCPPIERemoteControl::BeginPlay()
{
	Super::BeginPlay();

	// The owning client's copy: announce readiness and listen for this player's tapes finishing.
	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		TapeFinishedHandle = MCPPIE::FPIEInputInjector::OnTapeFinished().AddUObject(this, &UMCPPIERemoteControl::HandleTapeFinished);
		// The server adds this component at PostLogin, after PostLogin's client RPCs have sent the
		// controller, so it can reach a client whose controller has already begun play. The engine
		// then runs this BeginPlay while creating the component, before registering its NetGUID,
		// and an RPC sent now reaches the server as an unknown sub-object, which closes the
		// connection. The NetGUID is registered before the next tick.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &UMCPPIERemoteControl::AnnounceReady);
		}
	}
}

void UMCPPIERemoteControl::AnnounceReady()
{
	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		ServerReady(FPlatformOutputDevices::GetAbsoluteLogFilename());
	}
}

void UMCPPIERemoteControl::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TapeFinishedHandle.IsValid())
	{
		MCPPIE::FPIEInputInjector::OnTapeFinished().Remove(TapeFinishedHandle);
		TapeFinishedHandle.Reset();
	}
	for (const FString& Id : OwnedTapes)
	{
		MCPPIE::FPIEInputInjector::StopAny(Id);
	}
	OwnedTapes.Reset();
	Watches.Reset();
	UnbindWatchFrame();
	if (Capture.IsValid())
	{
		Capture->SetEnabled(false);
		Capture.Reset();
	}
	// Server side: the session or the player is gone, and nothing it reported is read again.
	Reports.Reset();
	bClientReady = false;
	ClientLogFile.Reset();
	Super::EndPlay(EndPlayReason);
}

// ── Server-side senders ─────────────────────────────────────────────────────

void UMCPPIERemoteControl::SendInject(const FString& Id, const FSoftObjectPath& Action, const FVector& Value)
{
	Reports.Expect(Id, /*bFresh*/ true);
	ClientInject(Id, Action, Value);
}

void UMCPPIERemoteControl::SendStartHold(const FString& Id, const FSoftObjectPath& Action, const FVector& Value)
{
	Reports.Expect(Id, /*bFresh*/ true);
	ClientStartHold(Id, Action, Value);
}

void UMCPPIERemoteControl::SendUpdateHold(const FString& Id, const FVector& Value)
{
	Reports.Expect(Id, /*bFresh*/ false);
	ClientUpdateHold(Id, Value);
}

void UMCPPIERemoteControl::SendStop(const FString& Id)
{
	Reports.Expect(Id, /*bFresh*/ false);
	ClientStop(Id);
}

void UMCPPIERemoteControl::SendTape(const FString& Id, const FSoftObjectPath& Action, const TArray<FVector>& Values)
{
	Reports.Expect(Id, /*bFresh*/ true);
	TArray<FVector_NetQuantize100> Quantized;
	Quantized.Reserve(Values.Num());
	for (const FVector& V : Values)
	{
		Quantized.Add(V);
	}
	ClientTape(Id, Action, Quantized);
}

void UMCPPIERemoteControl::SendCapture(const FString& Id, const FString& OutputPath)
{
	Reports.Expect(Id, /*bFresh*/ true);
	ClientCapture(Id, OutputPath);
}

void UMCPPIERemoteControl::SendFace(const FString& Id, const FRotator& Rotation)
{
	Reports.Expect(Id, /*bFresh*/ true);
	ClientFace(Id, Rotation);
}

void UMCPPIERemoteControl::SendConsole(const FString& Id, const FString& Command)
{
	Reports.Expect(Id, /*bFresh*/ true);
	ClientConsole(Id, Command);
}

void UMCPPIERemoteControl::SendWatchTrigger(const FString& Id, const FSoftObjectPath& Action)
{
	Reports.Expect(Id, /*bFresh*/ true);
	ClientWatchTrigger(Id, Action);
}

void UMCPPIERemoteControl::SendStopWatch(const FString& Id)
{
	ClientStopWatch(Id);
}

// ── Client-side execution ───────────────────────────────────────────────────

ULocalPlayer* UMCPPIERemoteControl::ResolveOwnLocalPlayer(FString& OutError) const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		OutError = TEXT("This process has no local player for the controller");
	}
	return LocalPlayer;
}

void UMCPPIERemoteControl::Report(const FString& Id, bool bOk, bool bFinished, const FString& Error)
{
	if (!bOk)
	{
		UE_LOG(LogMCPPIERuntime, Warning, TEXT("[PIEStudio] request %s failed: %s"), *Id, *Error);
	}
	ServerReport(Id, bOk, bFinished, Error);
}

void UMCPPIERemoteControl::ClientInject_Implementation(const FString& Id, const FSoftObjectPath& Action, FVector_NetQuantize100 Value)
{
	FString Error;
	UInputAction* Loaded = LoadAction(Action, Error);
	ULocalPlayer* LocalPlayer = Loaded ? ResolveOwnLocalPlayer(Error) : nullptr;
	const bool bOk = LocalPlayer && MCPPIE::FPIEInputInjector::InjectOnce(Loaded, ToActionValue(Loaded, Value), Error, 0, INDEX_NONE, LocalPlayer);
	Report(Id, bOk, bOk, Error);
}

void UMCPPIERemoteControl::ClientStartHold_Implementation(const FString& Id, const FSoftObjectPath& Action, FVector_NetQuantize100 Value)
{
	FString Error;
	UInputAction* Loaded = LoadAction(Action, Error);
	ULocalPlayer* LocalPlayer = Loaded ? ResolveOwnLocalPlayer(Error) : nullptr;
	const bool bOk = LocalPlayer
		&& !MCPPIE::FPIEInputInjector::StartHold(Loaded, ToActionValue(Loaded, Value), Id, Error, 0, INDEX_NONE, LocalPlayer).IsEmpty();
	Report(Id, bOk, false, Error);
}

void UMCPPIERemoteControl::ClientUpdateHold_Implementation(const FString& Id, FVector_NetQuantize100 Value)
{
	TArray<MCPPIE::FInjectionStatus> Active = MCPPIE::FPIEInputInjector::List();
	const MCPPIE::FInjectionStatus* Hold = Active.FindByPredicate([&Id](const MCPPIE::FInjectionStatus& S) { return S.Id == Id; });
	FString Error;
	UInputAction* Loaded = Hold ? LoadAction(FSoftObjectPath(Hold->ActionPath), Error) : nullptr;
	const bool bOk = Loaded && MCPPIE::FPIEInputInjector::UpdateHold(Id, ToActionValue(Loaded, Value));
	if (!bOk && Error.IsEmpty())
	{
		Error = FString::Printf(TEXT("No hold '%s' is running in this process"), *Id);
	}
	Report(Id, bOk, false, Error);
}

void UMCPPIERemoteControl::ClientStop_Implementation(const FString& Id)
{
	OwnedTapes.Remove(Id);
	const bool bOk = MCPPIE::FPIEInputInjector::StopAny(Id);
	Report(Id, bOk, true, bOk ? FString() : FString::Printf(TEXT("No hold or tape '%s' is running in this process"), *Id));
}

void UMCPPIERemoteControl::ClientTape_Implementation(const FString& Id, const FSoftObjectPath& Action, const TArray<FVector_NetQuantize100>& Values)
{
	FString Error;
	UInputAction* Loaded = LoadAction(Action, Error);
	ULocalPlayer* LocalPlayer = Loaded ? ResolveOwnLocalPlayer(Error) : nullptr;
	bool bOk = false;
	if (LocalPlayer)
	{
		TArray<FVector> Frames;
		Frames.Reserve(Values.Num());
		for (const FVector_NetQuantize100& V : Values)
		{
			Frames.Add(V);
		}
		bOk = !MCPPIE::FPIEInputInjector::StartTape(Loaded, Frames, 60, Id, Error, 0, INDEX_NONE, LocalPlayer).IsEmpty();
		if (bOk)
		{
			OwnedTapes.Add(Id);
		}
	}
	Report(Id, bOk, false, Error);
}

void UMCPPIERemoteControl::ClientFace_Implementation(const FString& Id, FRotator Rotation)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		Report(Id, false, true, TEXT("this player has no pawn to turn"));
		return;
	}
	// The autonomous client owns its pawn's facing; a rotation set on the server is overwritten by the next move. A
	// teleport in place rather than a bare rotation, so movement that holds its own facing hears about it.
	PC->SetControlRotation(Rotation);
	Pawn->TeleportTo(Pawn->GetActorLocation(), FRotator(0.0, Rotation.Yaw, 0.0), /*bIsATest*/ false, /*bNoCheck*/ true);
	UE_LOG(LogMCPPIERuntime, Log, TEXT("[PIEStudio] %s faces yaw %.0f"), *Pawn->GetName(), Rotation.Yaw);
	Report(Id, true, true, FString());
}

void UMCPPIERemoteControl::ClientCapture_Implementation(const FString& Id, const FString& OutputPath)
{
	if (!Capture.IsValid())
	{
		Capture = FSceneViewExtensions::NewExtension<MCPPIE::FPIEViewportCapture>();
		Capture->SetOutputFormat(OutputPath.EndsWith(TEXT(".jpg")) || OutputPath.EndsWith(TEXT(".jpeg")), 85);
		Capture->SetEnabled(true);
	}
	// This player's own viewport: in one-process PIE every player's viewport lives in the same process.
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport)
	{
		Report(Id, false, true, TEXT("this player has no viewport to capture"));
		return;
	}
	Capture->SetTargetViewport(LocalPlayer->ViewportClient->Viewport);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), /*Tree*/ true);
	TWeakObjectPtr<UMCPPIERemoteControl> WeakThis(this);
	Capture->RequestCapture(OutputPath, [WeakThis, Id, OutputPath](bool bWritten)
	{
		if (UMCPPIERemoteControl* Self = WeakThis.Get())
		{
			Self->Report(Id, bWritten, true, bWritten ? FString() : FString::Printf(TEXT("the viewport could not be saved to %s"), *OutputPath));
		}
	});
}

void UMCPPIERemoteControl::ClientConsole_Implementation(const FString& Id, const FString& Command)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		Report(Id, false, true, TEXT("No PlayerController owns this component"));
		return;
	}
	PC->ConsoleCommand(Command, /*bWriteToLog*/ true);
	Report(Id, true, true, FString());
}

void UMCPPIERemoteControl::ClientWatchTrigger_Implementation(const FString& Id, const FSoftObjectPath& Action)
{
	FString Error;
	UInputAction* Loaded = LoadAction(Action, Error);
	ULocalPlayer* LocalPlayer = Loaded ? ResolveOwnLocalPlayer(Error) : nullptr;
	if (!LocalPlayer)
	{
		Report(Id, false, true, Error);
		return;
	}
	Watches.RemoveAll([&Id](const FMCPPIERemoteWatch& W) { return W.Id == Id; });
	FMCPPIERemoteWatch& Watch = Watches.AddDefaulted_GetRef();
	Watch.Id = Id;
	Watch.Path = Action;
	Watch.Action = Loaded;
	if (!WatchFrameHandle.IsValid())
	{
		WatchFrameHandle = FCoreDelegates::OnEndFrame.AddUObject(this, &UMCPPIERemoteControl::PollWatches);
	}
	Report(Id, true, false, FString());
}

void UMCPPIERemoteControl::ClientStopWatch_Implementation(const FString& Id)
{
	Watches.RemoveAll([&Id](const FMCPPIERemoteWatch& W) { return W.Id == Id; });
	if (Watches.Num() == 0) UnbindWatchFrame();
}

void UMCPPIERemoteControl::PollWatches()
{
	FString Error;
	ULocalPlayer* LocalPlayer = ResolveOwnLocalPlayer(Error);
	for (int32 I = Watches.Num() - 1; I >= 0; --I)
	{
		FMCPPIERemoteWatch& Watch = Watches[I];
		const UInputAction* Action = Watch.Action.Get();
		if (!Action)
		{
			Action = Cast<UInputAction>(Watch.Path.TryLoad());
			Watch.Action = Action;
		}
		if (!Action)
		{
			Report(Watch.Id, false, true, FString::Printf(TEXT("the watched Input Action '%s' no longer loads"), *Watch.Path.ToString()));
			Watches.RemoveAt(I);
			continue;
		}
		bool bTriggered = false;
		FVector Value = FVector::ZeroVector;
		// A player between pawns has no input to read this frame; the watch waits for it.
		if (!LocalPlayer || !MCPPIE::ReadActionTriggerState(LocalPlayer, Action, bTriggered, Value, Error)) continue;
		if (Watch.Gate.Feed(bTriggered))
		{
			UE_LOG(LogMCPPIERuntime, Log, TEXT("[PIEStudio] watch %s: %s triggered"), *Watch.Id, *Action->GetName());
			ServerTriggered(Watch.Id, Value);
			Report(Watch.Id, true, true, FString());
			Watches.RemoveAt(I);
		}
	}
	if (Watches.Num() == 0) UnbindWatchFrame();
}

void UMCPPIERemoteControl::UnbindWatchFrame()
{
	if (WatchFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(WatchFrameHandle);
		WatchFrameHandle.Reset();
	}
}

void UMCPPIERemoteControl::HandleTapeFinished(const FString& Id)
{
	if (OwnedTapes.Remove(Id) > 0)
	{
		Report(Id, true, true, FString());
	}
}

// ── Server-side answers ─────────────────────────────────────────────────────

bool UMCPPIERemoteControl::IsRemotePlayerController(const AActor* Owner)
{
	// On the server a remote player's controller is driven by that player's connection, not by a local player.
	const APlayerController* PC = Cast<APlayerController>(Owner);
	return PC && PC->HasAuthority() && Cast<UNetConnection>(PC->Player) != nullptr && !PC->IsLocalController();
}

void UMCPPIERemoteControl::ServerReady_Implementation(const FString& LogFile)
{
	if (!IsRemotePlayerController(GetOwner()))
	{
		UE_LOG(LogMCPPIERuntime, Warning, TEXT("[PIEStudio] ServerReady refused: %s is not a remote player's controller"), *GetNameSafe(GetOwner()));
		return;
	}
	bClientReady = true;
	ClientLogFile = LogFile.Left(FMCPPIERemoteReports::MaxTextLength);
	UE_LOG(LogMCPPIERuntime, Log, TEXT("[PIEStudio] remote player %s is ready"), *GetNameSafe(GetOwner()));
}

void UMCPPIERemoteControl::ServerTriggered_Implementation(const FString& Id, FVector Value)
{
	if (!IsRemotePlayerController(GetOwner()))
	{
		UE_LOG(LogMCPPIERuntime, Warning, TEXT("[PIEStudio] ServerTriggered refused: %s is not a remote player's controller"), *GetNameSafe(GetOwner()));
		return;
	}
	if (!Reports.AcceptTrigger(Id, Value))
	{
		UE_LOG(LogMCPPIERuntime, Verbose, TEXT("[PIEStudio] %s reported a trigger for a request the server did not send or no longer holds"), *GetNameSafe(GetOwner()));
	}
}

void UMCPPIERemoteControl::ServerReport_Implementation(const FString& Id, bool bOk, bool bFinished, const FString& Error)
{
	if (!IsRemotePlayerController(GetOwner()))
	{
		UE_LOG(LogMCPPIERuntime, Warning, TEXT("[PIEStudio] ServerReport refused: %s is not a remote player's controller"), *GetNameSafe(GetOwner()));
		return;
	}
	if (!Reports.AcceptReport(Id, bOk, bFinished, Error))
	{
		// The client chose this id: it is not echoed back, as it can be any length.
		UE_LOG(LogMCPPIERuntime, Verbose, TEXT("[PIEStudio] %s reported on a request the server did not send or no longer holds"), *GetNameSafe(GetOwner()));
	}
}
