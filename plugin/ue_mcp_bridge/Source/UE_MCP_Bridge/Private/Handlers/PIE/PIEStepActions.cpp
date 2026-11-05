#include "Handlers/PIE/PIEStepActions.h"

#include "Handlers/PIE/PIELog.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "MCPPIEInputInjector.h"
#include "MCPPIERemoteControl.h"
#include "MCPPIEViewportCapture.h"
#include "SceneViewExtension.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Misc/Paths.h"

namespace MCPPIE
{
	namespace
	{
		FVector StepActionVector(const TSharedPtr<FJsonValue>& V)
		{
			FVector Out(0, 0, 0);
			if (!V.IsValid())
			{
				return FVector(1, 0, 0);
			}
			if (V->Type == EJson::Number)
			{
				Out.X = V->AsNumber();
			}
			else if (V->Type == EJson::Boolean)
			{
				Out.X = V->AsBool() ? 1.0 : 0.0;
			}
			else if (V->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
				if (A.Num() > 0) Out.X = A[0]->AsNumber();
				if (A.Num() > 1) Out.Y = A[1]->AsNumber();
				if (A.Num() > 2) Out.Z = A[2]->AsNumber();
			}
			return Out;
		}

		FInputActionValue StepActionValue(const UInputAction* Input, const FVector& Value)
		{
			switch (Input->ValueType)
			{
			case EInputActionValueType::Boolean: return FInputActionValue(Value.X != 0.0);
			case EInputActionValueType::Axis1D:  return FInputActionValue(static_cast<float>(Value.X));
			case EInputActionValueType::Axis2D:  return FInputActionValue(FVector2D(Value.X, Value.Y));
			default:                             return FInputActionValue(Value);
			}
		}

		double StepActionNow() { return FPlatformTime::Seconds(); }
	}

	bool ParseStepAction(const TSharedPtr<FJsonObject>& O, FStepAction& Out, FString& Err)
	{
		if (!O.IsValid())
		{
			Err = TEXT("an action must be an object");
			return false;
		}
		double N = 0;
		if (O->TryGetNumberField(TEXT("client"), N)) Out.Client = static_cast<int32>(N);

		if (O->TryGetNumberField(TEXT("wait_ms"), N))
		{
			Out.Kind = FStepAction::EKind::Wait;
			Out.Verb = TEXT("wait_ms");
			Out.Ms = static_cast<float>(N);
			return true;
		}
		FString Cmd;
		if (O->TryGetStringField(TEXT("console"), Cmd))
		{
			Out.Kind = FStepAction::EKind::Console;
			Out.Verb = TEXT("console");
			Out.Command = Cmd;
			return true;
		}

		const TSharedPtr<FJsonObject>* Body = nullptr;
		auto ActionOf = [&Out, &Err](const TSharedPtr<FJsonObject>& B) -> bool
		{
			if (!B->TryGetStringField(TEXT("action"), Out.ActionPath) || Out.ActionPath.IsEmpty())
			{
				Err = TEXT("an input action entry has no action path");
				return false;
			}
			return true;
		};

		if (O->TryGetObjectField(TEXT("tape"), Body))
		{
			Out.Kind = FStepAction::EKind::Tape;
			Out.Verb = TEXT("tape");
			if (!ActionOf(*Body)) return false;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!(*Body)->TryGetArrayField(TEXT("values"), Values) || Values->Num() == 0)
			{
				Err = TEXT("a tape has no values");
				return false;
			}
			for (const TSharedPtr<FJsonValue>& V : *Values) { Out.Values.Add(StepActionVector(V)); }
			return true;
		}
		if (O->TryGetObjectField(TEXT("press"), Body))
		{
			Out.Kind = FStepAction::EKind::Tape;
			Out.Verb = TEXT("press");
			if (!ActionOf(*Body)) return false;
			double Frames = 4;
			(*Body)->TryGetNumberField(TEXT("frames"), Frames);
			for (int32 i = 0; i < FMath::Max(1, static_cast<int32>(Frames)); ++i) { Out.Values.Add(FVector(1, 0, 0)); }
			Out.Values.Add(FVector::ZeroVector);
			return true;
		}
		if (O->TryGetObjectField(TEXT("hold"), Body))
		{
			Out.Kind = FStepAction::EKind::Hold;
			Out.Verb = TEXT("hold");
			if (!ActionOf(*Body)) return false;
			Out.Value = StepActionVector((*Body)->TryGetField(TEXT("value")));
			double Ms = 0;
			if (!(*Body)->TryGetNumberField(TEXT("ms"), Ms) || Ms <= 0)
			{
				Err = TEXT("a hold needs ms > 0");
				return false;
			}
			Out.Ms = static_cast<float>(Ms);
			return true;
		}
		if (O->TryGetObjectField(TEXT("inject"), Body))
		{
			Out.Kind = FStepAction::EKind::Inject;
			Out.Verb = TEXT("inject");
			if (!ActionOf(*Body)) return false;
			Out.Value = StepActionVector((*Body)->TryGetField(TEXT("value")));
			return true;
		}
		if (O->TryGetObjectField(TEXT("place"), Body))
		{
			Out.Kind = FStepAction::EKind::Place;
			Out.Verb = TEXT("place");
			const TSharedPtr<FJsonObject>& P = *Body;
			double Yaw = 0;
			if (P->TryGetNumberField(TEXT("yaw"), Yaw)) Out.Yaw = Yaw;
			P->TryGetBoolField(TEXT("face"), Out.bFace);
			if (P->TryGetStringField(TEXT("at"), Out.At) && !Out.At.IsEmpty())
			{
				if (P->HasField(TEXT("offset"))) Out.Offset = StepActionVector(P->TryGetField(TEXT("offset")));
				return true;
			}
			if (P->HasField(TEXT("location")))
			{
				Out.Value = StepActionVector(P->TryGetField(TEXT("location")));
				return true;
			}
			Err = TEXT("place needs at (an actor name or label) or location");
			return false;
		}
		if (const TSharedPtr<FJsonValue> Capture = O->TryGetField(TEXT("capture")))
		{
			Out.Kind = FStepAction::EKind::Capture;
			Out.Verb = TEXT("capture");
			if (Capture->Type == EJson::String)
			{
				Out.Command = FPaths::MakeValidFileName(Capture->AsString());
			}
			else if (Capture->Type != EJson::Boolean || !Capture->AsBool())
			{
				Err = TEXT("capture takes a label (string) or true");
				return false;
			}
			return true;
		}
		Err = TEXT("an action must be one of tape, press, hold, inject, console, place, capture, wait_ms");
		return false;
	}

	UWorld* StepActionWorld()
	{
		UWorld* World = PIERemotePlayers::FindServerWorld();
		return World ? World : (GEditor ? GEditor->PlayWorld.Get() : nullptr);
	}

	// ── Runner ──────────────────────────────────────────────────────────────

	FStepActionRunner::FStepActionRunner() = default;
	FStepActionRunner::~FStepActionRunner() { Reset(); }

	bool FStepActionRunner::Run(const FStepAction& A, FString& Err)
	{
		UInputAction* Input = nullptr;
		if (A.UsesInputAction())
		{
			Input = Cast<UInputAction>(FSoftObjectPath(A.ActionPath).TryLoad());
			if (!Input)
			{
				Err = FString::Printf(TEXT("InputAction %s not found"), *A.ActionPath);
				return false;
			}
		}

		if (A.Kind == FStepAction::EKind::Place)
		{
			return Place(A, Err);
		}
		if (A.Kind == FStepAction::EKind::Capture)
		{
			return RequestCapture(A, Err);
		}

		if (A.Client > 0)
		{
			UMCPPIERemoteControl* Remote = PIERemotePlayers::Find(A.Client, Err);
			if (!Remote)
			{
				return false;
			}
			const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
			PIERemotePlayers::RememberId(Id, Remote);
			FString What;
			switch (A.Kind)
			{
			case FStepAction::EKind::Tape:
				Remote->SendTape(Id, FSoftObjectPath(Input), A.Values);
				What = FString::Printf(TEXT("a tape on %s"), *Input->GetName());
				break;
			case FStepAction::EKind::Inject:
				Remote->SendInject(Id, FSoftObjectPath(Input), A.Value);
				What = FString::Printf(TEXT("an inject on %s"), *Input->GetName());
				break;
			case FStepAction::EKind::Console:
				Remote->SendConsole(Id, A.Command);
				What = FString::Printf(TEXT("console '%s'"), *A.Command);
				break;
			case FStepAction::EKind::Hold:
				Remote->SendStartHold(Id, FSoftObjectPath(Input), A.Value);
				PendingStops.Add({ StepActionNow() + A.Ms / 1000.0, A.Client, Id });
				What = FString::Printf(TEXT("a hold on %s"), *Input->GetName());
				break;
			default: break;
			}
			Relayed.Add({ Id, A.Client, What });
			return true;
		}

		// The editor's own player.
		switch (A.Kind)
		{
		case FStepAction::EKind::Tape:
		{
			const FString Id = FPIEInputInjector::StartTape(Input, A.Values, 60, FString(), Err);
			if (Id.IsEmpty())
			{
				return false;
			}
			LocalTapes.Add(Id);
			return true;
		}
		case FStepAction::EKind::Inject:
			return FPIEInputInjector::InjectOnce(Input, StepActionValue(Input, A.Value), Err);
		case FStepAction::EKind::Hold:
		{
			// Timed by the wall clock, like a remote hold; a tape would stretch with the frame rate.
			const FString Id = FPIEInputInjector::StartHold(Input, StepActionValue(Input, A.Value), FString(), Err);
			if (Id.IsEmpty())
			{
				return false;
			}
			PendingStops.Add({ StepActionNow() + A.Ms / 1000.0, 0, Id });
			return true;
		}
		case FStepAction::EKind::Console:
		{
			UWorld* World = PIERemotePlayers::FindLocalPlayerWorld();
			if (PIERemotePlayers::RunLocalConsole(World, A.Command).IsEmpty())
			{
				Err = FString::Printf(TEXT("console '%s' found no local player to run on"), *A.Command);
				return false;
			}
			return true;
		}
		default:
			return true;
		}
	}

	void FStepActionRunner::TickStops(double Now)
	{
		for (int32 i = PendingStops.Num() - 1; i >= 0; --i)
		{
			if (Now >= PendingStops[i].At)
			{
				if (PendingStops[i].Client == 0)
				{
					FPIEInputInjector::StopHold(PendingStops[i].Id);
				}
				else if (UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(PendingStops[i].Id))
				{
					Remote->SendStop(PendingStops[i].Id);
				}
				PendingStops.RemoveAt(i);
			}
		}
		LocalTapes.RemoveAll([](const FString& Id) { return !FPIEInputInjector::IsActive(Id); });
	}

	void FStepActionRunner::BeginStep()
	{
		Relayed.Reset();
		Captures.Reset();
	}

	bool FStepActionRunner::RelayedSettled() const
	{
		for (const FRelayed& Req : Relayed)
		{
			const UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(Req.Id);
			if (!Remote || !Remote->FindResult(Req.Id)) return false;
		}
		return true;
	}

	bool FStepActionRunner::HostCapturesWritten() const
	{
		for (const FCapture& C : Captures)
		{
			if (C.Written.IsValid() && *C.Written == 0) return false;
		}
		return true;
	}

	bool FStepActionRunner::InputIdle() const
	{
		return PendingStops.Num() == 0 && LocalTapes.Num() == 0;
	}

	void FStepActionRunner::Reset()
	{
		for (const FPendingStop& S : PendingStops)
		{
			if (S.Client == 0)
			{
				FPIEInputInjector::StopHold(S.Id);
			}
			else if (UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(S.Id))
			{
				Remote->SendStop(S.Id);
			}
		}
		PendingStops.Reset();
		for (const FString& Id : LocalTapes)
		{
			FPIEInputInjector::StopAny(Id);
		}
		LocalTapes.Reset();
		if (HostCapture.IsValid())
		{
			HostCapture->SetEnabled(false);
			HostCapture.Reset();
		}
	}

	/** Saves a player's view into the captures folder; the caller checks it was written. */
	bool FStepActionRunner::RequestCapture(const FStepAction& A, FString& Err)
	{
		const FString Label = A.Command.IsEmpty() ? FString::FromInt(Captures.Num() + 1) : A.Command;
		const FString File = FString::Printf(TEXT("captures/step%02d_client%d_%s.jpg"), StepNumber, A.Client, *Label);
		const FString Path = CaptureRoot / File;
		if (A.Client > 0)
		{
			UMCPPIERemoteControl* Remote = PIERemotePlayers::Find(A.Client, Err);
			if (!Remote)
			{
				return false;
			}
			const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
			PIERemotePlayers::RememberId(Id, Remote);
			Remote->SendCapture(Id, Path);
			Relayed.Add({ Id, A.Client, FString::Printf(TEXT("capture %s"), *File) });
			Captures.Add({ A.Client, File, nullptr });
			return true;
		}
		UWorld* Server = PIERemotePlayers::FindServerWorld();
		if (!Server && GEditor) Server = GEditor->PlayWorld;
		const FWorldContext* HostContext = Server ? GEditor->GetWorldContextFromWorld(Server) : nullptr;
		if (!HostContext || !HostContext->GameViewport || !HostContext->GameViewport->Viewport)
		{
			Err = TEXT("capture: the host player has no viewport");
			return false;
		}
		if (!HostCapture.IsValid())
		{
			HostCapture = FSceneViewExtensions::NewExtension<FPIEViewportCapture>();
			HostCapture->SetOutputFormat(/*bJpeg*/ true, 85);
			HostCapture->SetEnabled(true);
		}
		HostCapture->SetTargetViewport(HostContext->GameViewport->Viewport);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		TSharedPtr<int32> Written = MakeShared<int32>(0);
		HostCapture->RequestCapture(Path, [Written](bool bOk) { *Written = bOk ? 1 : 2; });
		Captures.Add({ 0, File, Written });
		return true;
	}

	/** Teleports a player's pawn on the server; the owning client follows through movement correction. */
	bool FStepActionRunner::Place(const FStepAction& A, FString& Err)
	{
		UWorld* World = StepActionWorld();
		if (!World)
		{
			Err = TEXT("place: no PIE world");
			return false;
		}
		APlayerController* PC = nullptr;
		UMCPPIERemoteControl* Remote = nullptr;
		if (A.Client > 0)
		{
			Remote = PIERemotePlayers::Find(A.Client, Err);
			PC = Remote ? Cast<APlayerController>(Remote->GetOwner()) : nullptr;
		}
		else
		{
			PC = World->GetFirstPlayerController();
			PC = PC && PC->IsLocalController() ? PC : nullptr;
		}
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			Err = FString::Printf(TEXT("place: client %d has no pawn%s"), A.Client, Err.IsEmpty() ? TEXT("") : *(TEXT(" (") + Err + TEXT(")")));
			return false;
		}

		const FVector Requested = A.Value;
		AActor* Target = nullptr;
		if (!A.At.IsEmpty())
		{
			for (TActorIterator<AActor> It(World); It && !Target; ++It)
			{
				if (It->GetName() == A.At || It->GetActorLabel() == A.At)
				{
					Target = *It;
				}
			}
			if (!Target)
			{
				Err = FString::Printf(TEXT("place: no actor named or labelled %s in %s"), *A.At, *World->GetName());
				return false;
			}
		}
		const FVector Wanted = Target ? Target->GetActorTransform().TransformPosition(A.Offset) : Requested;

		// The spot asked for, else the nearest free one around it: in a level with props an exact spot is often
		// taken. Each candidate keeps facing the target when there is one.
		FVector Location = Wanted;
		FRotator Rotation(0, A.Yaw.Get(Pawn->GetActorRotation().Yaw), 0);
		bool bPlaced = false;
		for (int32 Ring = 0; Ring <= 6 && !bPlaced; ++Ring)
		{
			const int32 Count = Ring == 0 ? 1 : 12;
			for (int32 i = 0; i < Count && !bPlaced; ++i)
			{
				const double Angle = 2.0 * PI * i / Count;
				Location = Wanted + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (60.0 * Ring);
				if (Target && A.bFace && !A.Yaw.IsSet())
				{
					Rotation.Yaw = (Target->GetActorLocation() - Location).Rotation().Yaw;
				}
				bPlaced = Pawn->TeleportTo(Location, Rotation);
			}
		}
		if (!bPlaced)
		{
			Err = FString::Printf(TEXT("place: %s fits nowhere within 360 units of %s"), *Pawn->GetName(), *Wanted.ToCompactString());
			return false;
		}
		PC->SetControlRotation(Rotation);
		if (Remote)
		{
			// The owning client keeps its own facing and moves from it; it has to turn itself.
			const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
			PIERemotePlayers::RememberId(Id, Remote);
			Remote->SendFace(Id, Rotation);
			Relayed.Add({ Id, A.Client, FString::Printf(TEXT("turning to yaw %.0f"), Rotation.Yaw) });
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-ACT] placed %s at %s yaw %.0f"), *Pawn->GetName(), *Location.ToCompactString(), Rotation.Yaw);
		return true;
	}

	// ── Sequence ────────────────────────────────────────────────────────────

	void FStepActionSequence::Begin(const TArray<FStepAction>& InActions)
	{
		Actions = InActions;
		Next = 0;
		WaitUntil = 0.0;
		FailedIndex = INDEX_NONE;
		Records.Reset();
	}

	FStepActionSequence::EOutcome FStepActionSequence::Tick(FStepActionRunner& Runner, double Now, FString& OutError)
	{
		if (FailedIndex != INDEX_NONE)
		{
			return EOutcome::Failed;
		}
		if (Now < WaitUntil)
		{
			return EOutcome::Running;
		}
		while (Next < Actions.Num())
		{
			const int32 Index = Next++;
			const FStepAction& A = Actions[Index];
			FRecord& R = Records.AddDefaulted_GetRef();
			R.Index = Index;
			R.Verb = A.Verb;
			if (A.Kind == FStepAction::EKind::Wait)
			{
				WaitUntil = Now + A.Ms / 1000.0;
				return EOutcome::Running;
			}
			if (!Runner.Run(A, OutError))
			{
				R.bOk = false;
				R.Error = OutError;
				FailedIndex = Index;
				return EOutcome::Failed;
			}
		}
		return EOutcome::Done;
	}
}
