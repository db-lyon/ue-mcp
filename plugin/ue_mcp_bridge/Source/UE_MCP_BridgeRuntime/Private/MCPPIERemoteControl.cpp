#include "MCPPIERemoteControl.h"

#include "UE_MCP_BridgeRuntimeModule.h"
#include "MCPPIEInputInjector.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputActionValue.h"

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
	if (Capture.IsValid())
	{
		Capture->SetEnabled(false);
		Capture.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

// ── Server-side senders ─────────────────────────────────────────────────────

void UMCPPIERemoteControl::SendInject(const FString& Id, const FSoftObjectPath& Action, const FVector& Value)
{
	Results.Remove(Id);
	ClientInject(Id, Action, Value);
}

void UMCPPIERemoteControl::SendStartHold(const FString& Id, const FSoftObjectPath& Action, const FVector& Value)
{
	Results.Remove(Id);
	ClientStartHold(Id, Action, Value);
}

void UMCPPIERemoteControl::SendUpdateHold(const FString& Id, const FVector& Value)
{
	ClientUpdateHold(Id, Value);
}

void UMCPPIERemoteControl::SendStop(const FString& Id)
{
	ClientStop(Id);
}

void UMCPPIERemoteControl::SendTape(const FString& Id, const FSoftObjectPath& Action, const TArray<FVector>& Values)
{
	Results.Remove(Id);
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
	Results.Remove(Id);
	ClientCapture(Id, OutputPath);
}

void UMCPPIERemoteControl::SendFace(const FString& Id, const FRotator& Rotation)
{
	Results.Remove(Id);
	ClientFace(Id, Rotation);
}

void UMCPPIERemoteControl::SendConsole(const FString& Id, const FString& Command)
{
	Results.Remove(Id);
	ClientConsole(Id, Command);
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

void UMCPPIERemoteControl::HandleTapeFinished(const FString& Id)
{
	if (OwnedTapes.Remove(Id) > 0)
	{
		Report(Id, true, true, FString());
	}
}

// ── Server-side answers ─────────────────────────────────────────────────────

void UMCPPIERemoteControl::ServerReady_Implementation(const FString& LogFile)
{
	bClientReady = true;
	ClientLogFile = LogFile;
	UE_LOG(LogMCPPIERuntime, Log, TEXT("[PIEStudio] remote player %s is ready"), *GetNameSafe(GetOwner()));
}

void UMCPPIERemoteControl::ServerReport_Implementation(const FString& Id, bool bOk, bool bFinished, const FString& Error)
{
	FMCPPIERemoteResult& Result = Results.FindOrAdd(Id);
	Result.Id = Id;
	Result.bOk = bOk;
	Result.bFinished = bFinished;
	Result.Error = Error;
}
