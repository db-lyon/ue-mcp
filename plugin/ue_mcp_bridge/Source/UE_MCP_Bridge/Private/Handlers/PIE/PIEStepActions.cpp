#include "Handlers/PIE/PIEStepActions.h"

#include "HandlerParams.h"
#include "HandlerPathGuard.h"
#include "HandlerWorld.h"
#include "Handlers/PIE/PIEClientUI.h"
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
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

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
			else if (V->Type == EJson::Object)
			{
				ReadVec3Fields(V->AsObject(), Out);
			}
			return Out;
		}

		/** A place location or offset: [x, y, z] or {x, y, z}, three numbers either way. Anything else is
		 *  refused rather than read as the origin, which teleported the pawn to (0, 0, 0) with ok:true (#1813). */
		bool StepActionPoint(const TSharedPtr<FJsonValue>& V, const TCHAR* Field, FVector& Out, FString& Err)
		{
			bool bOk = false;
			if (V.IsValid() && V->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
				bOk = A.Num() == 3;
				for (int32 I = 0; bOk && I < 3; ++I)
				{
					bOk = A[I].IsValid() && A[I]->Type == EJson::Number;
					if (bOk) Out[I] = A[I]->AsNumber();
				}
			}
			else if (V.IsValid() && V->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				double X = 0, Y = 0, Z = 0;
				bOk = O.IsValid() && O->Values.Num() == 3
					&& O->TryGetNumberField(TEXT("x"), X) && O->TryGetNumberField(TEXT("y"), Y) && O->TryGetNumberField(TEXT("z"), Z);
				if (bOk) Out = FVector(X, Y, Z);
			}
			if (!bOk)
			{
				FString Given;
				if (V.IsValid())
				{
					const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Given);
					FJsonSerializer::Serialize(V, FString(), W);
				}
				Err = FString::Printf(TEXT("place %s must be [x, y, z] or {x, y, z} with three numbers, such as [-2500, 0, 200] or {\"x\": -2500, \"y\": 0, \"z\": 200}; got %s"),
					Field, Given.IsEmpty() ? TEXT("nothing") : *Given);
			}
			return bOk;
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
			double Hz = 60;
			if ((*Body)->TryGetNumberField(TEXT("hz"), Hz) && (Hz < 1 || Hz > 1000))
			{
				Err = FString::Printf(TEXT("a tape's hz must be 1 to 1000, not %g"), Hz);
				return false;
			}
			Out.Hz = FMath::RoundToInt(Hz);
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
				return !P->HasField(TEXT("offset")) || StepActionPoint(P->TryGetField(TEXT("offset")), TEXT("offset"), Out.Offset, Err);
			}
			if (P->HasField(TEXT("location")))
			{
				return StepActionPoint(P->TryGetField(TEXT("location")), TEXT("location"), Out.Value, Err);
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
		if (O->TryGetObjectField(TEXT("key"), Body))
		{
			Out.Kind = FStepAction::EKind::Key;
			Out.Verb = TEXT("key");
			(*Body)->TryGetStringField(TEXT("key"), Out.KeyName);
			(*Body)->TryGetStringField(TEXT("text"), Out.KeyText);
			(*Body)->TryGetStringField(TEXT("event"), Out.KeyEvent);
			if (Out.KeyName.IsEmpty() == Out.KeyText.IsEmpty())
			{
				Err = TEXT("a key action takes exactly one of key (an FKey name such as Right or Enter) or text");
				return false;
			}
			PIEClientUI::EKeyPhase Phase;
			if (!PIEClientUI::ParseKeyPhase(Out.KeyEvent, Phase))
			{
				Err = FString::Printf(TEXT("a key action's event must be press, down or up, not '%s'"), *Out.KeyEvent);
				return false;
			}
			return true;
		}
		Err = TEXT("an action must be one of tape, press, hold, inject, console, place, capture, key, wait_ms");
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
		LastTarget.Reset();
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
		if (A.Kind == FStepAction::EKind::Key)
		{
			if (A.Client > 0)
			{
				Err = FString::Printf(TEXT("key cannot target remote client %d: a remote player runs in another process with no Slate path from here. Aim the run at that client's window with pie_instance (script_run) and leave client off."), A.Client);
				return false;
			}
			PIEClientUI::FUIPlayer Player;
			if (!PIEClientUI::Resolve(OwnPIEInstance, OwnLocalPlayer, Player, Err))
			{
				return false;
			}
			LastTarget = Player.Description;
			PIEClientUI::EKeyPhase Phase = PIEClientUI::EKeyPhase::Press;
			PIEClientUI::ParseKeyPhase(A.KeyEvent, Phase);
			PIEClientUI::FKeyResult Sent;
			return A.KeyName.IsEmpty()
				? PIEClientUI::SendText(Player, A.KeyText, Sent, Err)
				: PIEClientUI::SendKey(Player, A.KeyName, Phase, Sent, Err);
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
			LastTarget = FString::Printf(TEXT("remote client %d"), A.Client);
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

		// The editor's own player, resolved to one exact local player: left to the injector, the input went to
		// whichever PIE world was active, which in a one-process session with a second client was the client (#1814).
		ULocalPlayer* Own = PIERemotePlayers::FindOwnLocalPlayer(OwnPIEInstance, OwnLocalPlayer, Err);
		if (!Own)
		{
			Err = OwnPIEInstance >= 0
				? FString::Printf(TEXT("%s: the run's pie_instance %d client_id %d has no local player (%s)"), *A.Verb, OwnPIEInstance, OwnLocalPlayer, *Err)
				: FString::Printf(TEXT("%s: client 0 has no local player (%s)"), *A.Verb, *Err);
			return false;
		}
		LastTarget = PIERemotePlayers::DescribeLocalPlayer(Own);
		switch (A.Kind)
		{
		case FStepAction::EKind::Tape:
		{
			const FString Id = FPIEInputInjector::StartTape(Input, A.Values, A.Hz, FString(), Err, 0, INDEX_NONE, Own);
			if (Id.IsEmpty())
			{
				return false;
			}
			LocalTapes.Add(Id);
			return true;
		}
		case FStepAction::EKind::Inject:
			return FPIEInputInjector::InjectOnce(Input, StepActionValue(Input, A.Value), Err, 0, INDEX_NONE, Own);
		case FStepAction::EKind::Hold:
		{
			// Timed by the wall clock, like a remote hold; a tape would stretch with the frame rate.
			const FString Id = FPIEInputInjector::StartHold(Input, StepActionValue(Input, A.Value), FString(), Err, 0, INDEX_NONE, Own);
			if (Id.IsEmpty())
			{
				return false;
			}
			PendingStops.Add({ StepActionNow() + A.Ms / 1000.0, 0, Id });
			return true;
		}
		case FStepAction::EKind::Console:
		{
			if (PIERemotePlayers::RunLocalConsole(Own->GetWorld(), A.Command).IsEmpty())
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
		FString Path;
		if (!MCPResolvePathInside(CaptureRoot / File, { CaptureRoot }, Path, Err)) return false;
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
		APlayerController* OwnClientPC = nullptr;
		UMCPPIERemoteControl* Remote = nullptr;
		if (A.Client > 0)
		{
			Remote = PIERemotePlayers::Find(A.Client, Err);
			PC = Remote ? Cast<APlayerController>(Remote->GetOwner()) : nullptr;
		}
		else if (ULocalPlayer* Own = PIERemotePlayers::FindOwnLocalPlayer(OwnPIEInstance, OwnLocalPlayer, Err))
		{
			// Client 0 is a client window when the server is dedicated: move the server's pawn for that player, as
			// for a remote client, and turn the client's own controller, which keeps its facing (#2010).
			PC = PIERemotePlayers::FindAuthorityController(Own->PlayerController);
			if (PC && PC != Own->PlayerController)
			{
				OwnClientPC = Own->PlayerController;
			}
			else if (!PC)
			{
				Err = TEXT("the server's copy of this player is not in the editor process");
			}
		}
		if (PC)
		{
			World = PC->GetWorld();
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
		if (OwnClientPC)
		{
			OwnClientPC->SetControlRotation(Rotation);
			if (APawn* ClientPawn = OwnClientPC->GetPawn())
			{
				// The client's copy follows now instead of on the next correction.
				ClientPawn->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);
			}
		}
		else if (Remote)
		{
			// The owning client keeps its own facing and moves from it; it has to turn itself.
			const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
			PIERemotePlayers::RememberId(Id, Remote);
			Remote->SendFace(Id, Rotation);
			Relayed.Add({ Id, A.Client, FString::Printf(TEXT("turning to yaw %.0f"), Rotation.Yaw) });
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-ACT] placed %s at %s yaw %.0f"), *Pawn->GetName(), *Location.ToCompactString(), Rotation.Yaw);
		LastTarget = FString::Printf(TEXT("%s in %s (%s)"), *Pawn->GetName(), *World->GetName(), *DescribePIENetMode(World));
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
			const bool bRan = Runner.Run(A, OutError);
			R.Target = Runner.LastTarget;
			if (!bRan)
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
