#include "Handlers/PIE/PIEArrangementPlayer.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIEReplayInputLock.h"
#include "Handlers/PIE/PIELog.h"
#include "MCPPIEActionTrigger.h"
#include "HandlerWorld.h"
#include "HandlerPlaySession.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectIterator.h"

namespace MCPPIE
{
	namespace
	{
		const TCHAR* const ArrangementStateNames[] = { TEXT("idle"), TEXT("starting"), TEXT("running"), TEXT("awaiting_verdict"), TEXT("finished"), TEXT("stopped"), TEXT("error") };
		constexpr double PlayerEps = 1e-6;

		EClipValueType ClipTypeOf(const UInputAction* Action)
		{
			switch (Action->ValueType)
			{
			case EInputActionValueType::Axis1D: return EClipValueType::Axis1D;
			case EInputActionValueType::Axis2D: return EClipValueType::Axis2D;
			case EInputActionValueType::Axis3D: return EClipValueType::Axis3D;
			default: return EClipValueType::Bool;
			}
		}

		APawn* PawnOf(ULocalPlayer* LocalPlayer)
		{
			APlayerController* PC = LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr;
			return PC ? PC->GetPawn() : nullptr;
		}

		// A folder-safe observer run id for a hub run id.
		FString ObserverIdFor(const FString& RunId)
		{
			FString Out = TEXT("hub_");
			for (TCHAR C : RunId) Out.AppendChar(FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_') ? C : TEXT('_'));
			return Out.Left(96);
		}

		FCSVRow RowFor(const AActor* Actor)
		{
			FCSVRow Row;
			Row.PawnLocation = Actor->GetActorLocation();
			Row.PawnRotation = Actor->GetActorRotation();
			Row.PawnVelocity = Actor->GetVelocity();
			if (const ACharacter* Ch = Cast<ACharacter>(Actor))
			{
				if (const UCharacterMovementComponent* Move = Ch->GetCharacterMovement()) Row.PawnVelocity = Move->Velocity;
			}
			Row.Speed2D = static_cast<float>(Row.PawnVelocity.Size2D());
			return Row;
		}
	}

	FString ArrangementStateToString(EArrangementState State) { return ArrangementStateNames[static_cast<int32>(State)]; }

	FPIEArrangementPlayer& FPIEArrangementPlayer::Get()
	{
		static FPIEArrangementPlayer Instance;
		return Instance;
	}

	void FPIEArrangementPlayer::Init()
	{
		if (!EndPIEHandle.IsValid()) EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FPIEArrangementPlayer::OnEndPIE);
	}

	void FPIEArrangementPlayer::Shutdown()
	{
		if (IsActive()) Finish(EArrangementState::Stopped, TEXT("the editor is shutting down"));
		if (EndPIEHandle.IsValid()) FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
	}

	bool FPIEArrangementPlayer::IsActive() const
	{
		return State == EArrangementState::Starting || State == EArrangementState::Running || State == EArrangementState::AwaitingVerdict;
	}

	ULocalPlayer* FPIEArrangementPlayer::OwnLocalPlayer() const
	{
		FString Err;
		return PIERemotePlayers::FindOwnLocalPlayer(INDEX_NONE, 0, Err);
	}

	void FPIEArrangementPlayer::Begin(bool bStartPIE)
	{
		Drivers.Reset();
		TriggerWatches.Reset();
		LaneErrors.Reset();
		LaneNext.Init(0, Timeline.Plan.Lanes.Num());
		Error.Reset();
		bClockStarted = false;
		bPaused = false;
		SteadyFrames = 0;
		State = EArrangementState::Starting;
		bStartedPIE = false;
		if (bStartPIE && !GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress())
		{
			MCPPlaySession::Request(/*bNeedsEditorWorld=*/true);
			bStartedPIE = true;
		}
		if (!PreActorTickHandle.IsValid()) PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddRaw(this, &FPIEArrangementPlayer::OnPreActorTick);
		if (!EndFrameHandle.IsValid()) EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FPIEArrangementPlayer::OnEndFrame);
	}

	bool FPIEArrangementPlayer::Play(const FArrangementPlayConfig& Cfg, bool& bOutStartedPIE, FString& OutError)
	{
		bOutStartedPIE = false;
		if (IsActive())
		{
			OutError = bRecording ? FString(TEXT("a clip is recording; stop it with clip_record_stop first"))
				: FString::Printf(TEXT("run %s is playing; stop it with arrangement_stop first"), *RunId);
			return false;
		}
		if (!GEditor)
		{
			OutError = TEXT("playing an arrangement needs the editor");
			return false;
		}
		if (!Cfg.Plan.Level.IsEmpty() && !GEditor->PlayWorld && !MCPLoadMapIfNeeded(Cfg.Plan.Level, OutError))
		{
			OutError = FString::Printf(TEXT("the arrangement plays in %s: %s"), *Cfg.Plan.Level, *OutError);
			return false;
		}
		RunId = Cfg.RunId;
		bRecording = false;
		Timeline = FArrangementTimeline();
		Timeline.Plan = Cfg.Plan;

		// The values the checks read, observed for the run.
		FObserverArmConfig Obs;
		ObserverRunId = ObserverIdFor(RunId);
		Obs.RunId = ObserverRunId;
		Obs.SampleHz = FMath::Clamp(FMath::RoundToInt(Timeline.Plan.Rate), 1, 240);
		for (const FHubCheckRun& C : Timeline.Plan.Checks)
		{
			if (C.Def.Mode == EHubCheckMode::Manual || C.IsSettled()) continue;
			const FHubExpect& E = C.Def.Expect;
			if (E.Op == EHubExpectOp::Visible) Obs.ExtraTrackedWidgets.AddUnique(E.Value);
			else if (E.Op == EHubExpectOp::Exists) Obs.ExtraTrackedActors.AddUnique(E.Path());
			else if (E.Op != EHubExpectOp::Triggered && !IsPawnChannelName(E.Value)) Obs.ExtraTrackedValues.AddUnique(E.Path());
		}
		// The observer samples the editor's own player; with nothing to read it still gives the run its clock.
		if (Obs.ExtraTrackedValues.Num() + Obs.ExtraTrackedActors.Num() + Obs.ExtraTrackedWidgets.Num() == 0) Obs.ExtraTrackedValues.Add(TEXT("pos_x"));
		FString ObsMsg;
		if (!FPIEObserver::Get().Arm(Obs, OutError, ObsMsg)) return false;

		Begin(/*bStartPIE=*/true);
		bOutStartedPIE = bStartedPIE;
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-HUB] %s armed: %s, %d input lanes, %d lanes, %d checks, %s frames at %s fps%s"),
			*RunId, *Timeline.Plan.Id, Timeline.Plan.Input.Num(), Timeline.Plan.Lanes.Num(), Timeline.Plan.Checks.Num(),
			*FString::SanitizeFloat(Timeline.Plan.End), *FString::SanitizeFloat(Timeline.Plan.Rate),
			Timeline.Plan.RegionId.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", region %s"), *Timeline.Plan.RegionId));
		return true;
	}

	bool FPIEArrangementPlayer::StartRecording(const FClipRecordConfig& Cfg, FString& OutError)
	{
		if (IsActive())
		{
			OutError = bRecording ? FString(TEXT("a clip is already recording; stop it with clip_record_stop first"))
				: FString::Printf(TEXT("run %s is playing; stop it with arrangement_stop first"), *RunId);
			return false;
		}
		if (!GEditor)
		{
			OutError = TEXT("recording needs the editor");
			return false;
		}
		if (Cfg.Performer.Kind == EClipPerformerKind::Player && Cfg.Performer.Client != 0)
		{
			OutError = TEXT("a recording reads the editor's own player (client 0); record a remote client from its own editor");
			return false;
		}
		if (Cfg.bPunch && (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress()))
		{
			OutError = TEXT("a punch-in plays the clip from frame 0 in its own PIE session; stop the running session first");
			return false;
		}
		RecordCfg = Cfg;
		bRecording = true;
		bPunchedIn = false;
		bTakeDone = false;
		PunchedOutAt = -1.0;
		Samples.Reset();
		LaneRecordings.Reset();
		Armed.Reset();
		ArmedTypes.Reset();
		ArmedActions.Reset();
		RunId = TEXT("recording");
		ObserverRunId.Reset();

		// The take starts as the punched clip, or empty; the lead-in plays the punched clip as an arrangement would.
		Take = Cfg.bPunch ? Cfg.Clip : FClip();
		Take.Rate = Cfg.bPunch ? Cfg.Clip.Rate : Cfg.Rate;
		Timeline = FArrangementTimeline();
		Timeline.Plan.Rate = Take.Rate;
		Timeline.Plan.Performers.Add(Cfg.Performer);
		Timeline.Plan.End = Cfg.bPunch ? Cfg.PunchTo : TNumericLimits<double>::Max();
		if (Cfg.bPunch)
		{
			for (const FClipTrack& T : Take.Input)
			{
				FPlacedInput P;
				P.Track = T;
				P.Track.Client = Cfg.Performer.Client;
				P.To = Take.EffectiveLength();
				Timeline.Plan.Input.Add(MoveTemp(P));
			}
			for (FClipLane L : Take.Lanes)
			{
				L.Performer = Cfg.Performer;
				Timeline.Plan.Lanes.Add(MoveTemp(L));
			}
		}
		else if (Cfg.Performer.Kind == EClipPerformerKind::Actor)
		{
			FClipLane Move;
			Move.Id = TEXT("move");
			Move.Capability = EClipLaneCapability::Move;
			Take.Lanes.Add(Move);
		}
		Begin(/*bStartPIE=*/true);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-HUB] recording %s%s"), *Cfg.Performer.Label(),
			Cfg.bPunch ? *FString::Printf(TEXT(", punching frames %s..%s"), *FString::SanitizeFloat(Cfg.PunchFrom), *FString::SanitizeFloat(Cfg.PunchTo)) : TEXT(""));
		return true;
	}

	void FPIEArrangementPlayer::ApplyFixedTimestep()
	{
		if (bFixedApplied) return;
		bSavedUseFixed = FApp::UseFixedTimeStep();
		SavedFixedDelta = FApp::GetFixedDeltaTime();
		FApp::SetFixedDeltaTime(1.0 / FMath::Max(1.0, Timeline.Plan.Rate));
		FApp::SetUseFixedTimeStep(true);
		bFixedApplied = true;
	}

	void FPIEArrangementPlayer::RestoreFixedTimestep()
	{
		if (!bFixedApplied) return;
		FApp::SetUseFixedTimeStep(bSavedUseFixed);
		FApp::SetFixedDeltaTime(SavedFixedDelta);
		bFixedApplied = false;
	}

	void FPIEArrangementPlayer::SetPaused(UWorld* World, bool bInPaused)
	{
		if (!World || bPaused == bInPaused) return;
		UGameplayStatics::SetGamePaused(World, bInPaused);
		bPaused = bInPaused;
	}

	void FPIEArrangementPlayer::StartClock(UWorld* World)
	{
		bClockStarted = true;
		State = EArrangementState::Running;
		LastGameSeconds = World->GetTimeSeconds();
		Timeline.Position = 0.0;
		const bool bLiveFromStart = bRecording && (!RecordCfg.bPunch || RecordCfg.PunchFrom <= 0.0);
		// A play and a punch's lead-in run on a fixed 1/rate step; the person plays a take on variable time.
		if (!bLiveFromStart) ApplyFixedTimestep();
		if (!bLiveFromStart) ReplayInputLock::Start(World);
		if (bLiveFromStart) PunchIn(World);
		Drive(World);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-HUB] %s started at game %.3f s"), *RunId, LastGameSeconds);
	}

	FClipPerformerDriver* FPIEArrangementPlayer::DriverFor(const FClipPerformer& Performer)
	{
		const FString Key = Performer.Key();
		if (TUniquePtr<FClipPerformerDriver>* Found = Drivers.Find(Key)) return Found->Get();
		FClipDriverContext Ctx;
		TUniquePtr<FClipPerformerDriver> Driver = MakeClipPerformerDriver(Performer, Ctx);
		FClipPerformerDriver* Raw = Driver.Get();
		Drivers.Add(Key, MoveTemp(Driver));
		return Raw;
	}

	void FPIEArrangementPlayer::LaneError(const FString& LaneId, const FString& InError)
	{
		const FString Line = FString::Printf(TEXT("lane %s: %s"), *LaneId, *InError);
		// The first few distinct ones: a lane that fails every frame says so once.
		if (LaneErrors.Num() >= 10 || LaneErrors.Contains(Line)) return;
		LaneErrors.Add(Line);
		UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-HUB] %s"), *Line);
	}

	void FPIEArrangementPlayer::Drive(UWorld* World)
	{
		const double Pos = Timeline.Position;
		const bool bTaking = bRecording && bPunchedIn && PunchedOutAt < 0.0;
		// Input: one value per player and action, from the instance that covers the position.
		struct FFeed { const FClipTrack* Track = nullptr; bool bActive = false; FVector Value = FVector::ZeroVector; };
		TMap<FString, FFeed> Feeds;
		for (const FPlacedInput& P : Timeline.Plan.Input)
		{
			const FString Key = FString::Printf(TEXT("%d|%s"), P.Track.Client, *P.Track.Action);
			FFeed& F = Feeds.FindOrAdd(Key);
			if (!F.Track) F.Track = &P.Track;
			if (F.bActive || Pos < P.From - PlayerEps || Pos >= P.To - PlayerEps) continue;
			bool bActive = false;
			const FVector V = EvaluateClipTrack(P.Track, Pos, bActive);
			if (bActive) { F.Track = &P.Track; F.bActive = true; F.Value = V; }
		}
		for (const TPair<FString, FFeed>& F : Feeds)
		{
			// An armed action is the person's from the punch-in.
			if (bTaking && Armed.Contains(F.Value.Track->Action)) continue;
			if (FClipPerformerDriver* D = DriverFor(FClipPerformer::Player(F.Value.Track->Client))) D->FeedInput(*F.Value.Track, F.Value.bActive, F.Value.Value);
		}
		// Move, action and value lanes, each to its performer's driver.
		for (int32 L = 0; L < Timeline.Plan.Lanes.Num(); ++L)
		{
			const FClipLane& Lane = Timeline.Plan.Lanes[L];
			if (bTaking && LaneRecordings.ContainsByPredicate([&Lane](const FClipLaneRecording& R) { return R.LaneId == Lane.Id; })) continue;
			FClipPerformerDriver* Driver = DriverFor(Lane.Performer);
			if (!Driver)
			{
				LaneError(Lane.Id, FString::Printf(TEXT("no driver plays a %s performer"), *ClipPerformerKindToString(Lane.Performer.Kind)));
				continue;
			}
			FString Err;
			int32& Next = LaneNext[L];
			if (Lane.Capability == EClipLaneCapability::Move)
			{
				// The newest point passed this frame: a frame that skips two points walks to the second.
				int32 Due = INDEX_NONE;
				while (Next < Lane.Points.Num() && Lane.Points[Next].Time <= Pos + PlayerEps) Due = Next++;
				if (Due != INDEX_NONE && !Driver->MoveTo(World, Lane.Points[Due], Err)) LaneError(Lane.Id, FString::Printf(TEXT("point %s: %s"), *Lane.Points[Due].Id, *Err));
			}
			else if (Lane.Capability == EClipLaneCapability::Action)
			{
				while (Next < Lane.Calls.Num() && Lane.Calls[Next].Time <= Pos + PlayerEps)
				{
					const FClipActionCall& C = Lane.Calls[Next++];
					if (!Driver->Call(World, C, Err)) LaneError(Lane.Id, FString::Printf(TEXT("call %s (%s): %s"), *C.Id, *C.Function, *Err));
				}
			}
			else if (Lane.Capability == EClipLaneCapability::Value)
			{
				bool bActive = false;
				const double V = EvaluateClipValueLane(Lane, Pos, bActive);
				if (bActive && !Driver->SetValue(World, Lane, V, Err)) LaneError(Lane.Id, FString::Printf(TEXT("%s: %s"), *Lane.Target, *Err));
			}
		}
		FClipCallRecorder::Get().SetFrame(FMath::RoundToDouble(Pos * 1000.0) / 1000.0);
	}

	void FPIEArrangementPlayer::OnPreActorTick(UWorld* World, ELevelTick /*TickType*/, float /*DeltaSeconds*/)
	{
		if (!IsActive() || !bClockStarted || !World) return;
		ULocalPlayer* LP = OwnLocalPlayer();
		if (!LP || LP->GetWorld() != World) return;
		// Game time is already this frame's here. Paused, it does not move, and neither does the timeline.
		const double Now = World->GetTimeSeconds();
		const double Dt = FMath::Max(0.0, Now - LastGameSeconds);
		LastGameSeconds = Now;
		if (Dt <= 0.0) return;
		Timeline.Advance(Dt * Timeline.Plan.Rate);
		if (bRecording && !bPunchedIn && Timeline.Position >= RecordCfg.PunchFrom - PlayerEps) PunchIn(World);
		if (bRecording && bPunchedIn && PunchedOutAt < 0.0 && RecordCfg.bPunch && Timeline.Position >= RecordCfg.PunchTo - PlayerEps)
		{
			PunchedOutAt = RecordCfg.PunchTo;
			FinishTake();
		}
		Drive(World);
	}

	bool FPIEArrangementPlayer::ReadCheck(const FHubCheckRun& Check, double& OutValue)
	{
		const FHubExpect& E = Check.Def.Expect;
		if (E.Op == EHubExpectOp::Triggered)
		{
			// A fresh trigger since the check started: the action had to be seen not triggering first.
			FTriggerWatch& W = TriggerWatches.FindOrAdd(Check.Def.Id);
			const UInputAction* Action = W.Action.Get();
			if (!Action) { Action = LoadObject<UInputAction>(nullptr, *E.Value); W.Action = Action; }
			bool bTriggered = false;
			FVector Value;
			FString Err;
			if (!Action || !ReadActionTriggerState(OwnLocalPlayer(), Action, bTriggered, Value, Err)) return false;
			if (!bTriggered) W.bReleased = true;
			OutValue = (bTriggered && W.bReleased) ? 1.0 : 0.0;
			return true;
		}
		const FPIEObserver& Obs = FPIEObserver::Get();
		if (E.Op == EHubExpectOp::Visible)
		{
			bool bVisible = false;
			if (!Obs.ReadWidgetVisible(ObserverRunId, E.Value, bVisible)) return false;
			OutValue = bVisible ? 1.0 : 0.0;
			return true;
		}
		if (E.Op == EHubExpectOp::Exists)
		{
			bool bResolved = false;
			if (!Obs.ReadActorResolved(ObserverRunId, E.Path(), bResolved)) return false;
			OutValue = bResolved ? 1.0 : 0.0;
			return true;
		}
		if (IsPawnChannelName(E.Value))
		{
			// Off the pawn the player possesses now, or the subject actor, found as an actor track finds it.
			ULocalPlayer* LP = OwnLocalPlayer();
			const AActor* Actor = PawnOf(LP);
			if (!E.Subject.IsEmpty())
			{
				FString Err;
				FClipPerformerDriver* D = DriverFor(FClipPerformer::ForActor(E.Subject));
				Actor = D && LP ? D->ResolveActor(LP->GetWorld(), Err) : nullptr;
			}
			return Actor && ReadPawnChannel(RowFor(Actor), E.Value, OutValue);
		}
		return Obs.ReadTrackedValue(ObserverRunId, E.Path(), OutValue);
	}

	void FPIEArrangementPlayer::PunchIn(UWorld* World)
	{
		bPunchedIn = true;
		ReplayInputLock::Stop();
		RestoreFixedTimestep();
		FPSPin.Apply(FMath::RoundToInt(Take.Rate));
		if (RecordCfg.Performer.Kind == EClipPerformerKind::Player)
		{
			ULocalPlayer* LP = OwnLocalPlayer();
			UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
			UEnhancedPlayerInput* PlayerInput = Sub ? Sub->GetPlayerInput() : nullptr;
			auto Arm = [this](const UInputAction* A)
			{
				const FString Path = A->GetPathName();
				Armed.AddUnique(Path);
				ArmedTypes.Add(Path, ClipTypeOf(A));
				ArmedActions.Add(Path, const_cast<UInputAction*>(A));
			};
			// The punched clip's actions, or every action the player's mapping contexts map.
			for (const FClipTrack& T : Take.Input) if (const UInputAction* A = LoadObject<UInputAction>(nullptr, *T.Action)) Arm(A);
			if (Armed.Num() == 0 && PlayerInput)
			{
				for (TObjectIterator<UInputAction> It; It; ++It) if (*It && PlayerInput->FindActionInstanceData(*It)) Arm(*It);
			}
		}
		// Lanes the take records: from here their performer acts on its own, and the take reads what it did.
		for (const FClipLane& Lane : Take.Lanes)
		{
			FClipLaneRecording Rec;
			Rec.LaneId = Lane.Id;
			LaneRecordings.Add(Rec);
			if (Lane.Capability != EClipLaneCapability::Action) continue;
			FString Err;
			FClipPerformerDriver* Driver = DriverFor(RecordCfg.Performer);
			AActor* Actor = Driver ? Driver->ResolveActor(World, Err) : nullptr;
			if (!Actor) { LaneError(Lane.Id, FString::Printf(TEXT("the take records nothing: %s"), *Err)); continue; }
			TArray<FString> Fns = Lane.Watch;
			for (const FClipActionCall& C : Lane.Calls) Fns.AddUnique(C.Function);
			for (const FString& Missing : FClipCallRecorder::Get().Watch(Lane.Id, Actor, Fns)) LaneError(Lane.Id, FString::Printf(TEXT("%s has no function %s to record"), *Actor->GetName(), *Missing));
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-HUB] punched in at frame %.1f, recording %d actions and %d lanes"), Timeline.Position, Armed.Num(), LaneRecordings.Num());
	}

	void FPIEArrangementPlayer::SampleTake(UWorld* World)
	{
		if (!bRecording || !bPunchedIn || PunchedOutAt >= 0.0) return;
		// To a thousandth of a frame, the precision a clip keeps its times in.
		const double At = FMath::RoundToDouble(Timeline.Position * 1000.0) / 1000.0;
		FClipPerformerDriver* Driver = DriverFor(RecordCfg.Performer);
		for (FClipLaneRecording& Rec : LaneRecordings)
		{
			const FClipLane* Lane = Take.FindLane(Rec.LaneId);
			if (!Lane || Lane->Capability == EClipLaneCapability::Action || !Driver) continue;
			FClipLaneSample S;
			S.Frame = At;
			FString Err;
			if (Lane->Capability == EClipLaneCapability::Move) S.bHasLocation = Driver->ReadLocation(World, S.Location, Err);
			else S.bHasValue = Driver->ReadValue(World, *Lane, S.Value, Err);
			if (!S.bHasLocation && !S.bHasValue) { LaneError(Lane->Id, FString::Printf(TEXT("the take could not read it: %s"), *Err)); continue; }
			if (Rec.Samples.Num() == 0 || !FMath::IsNearlyEqual(Rec.Samples.Last().Frame, S.Frame, PlayerEps)) Rec.Samples.Add(S);
		}
		if (Armed.Num() == 0) return;
		ULocalPlayer* LP = OwnLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UEnhancedPlayerInput* PlayerInput = Sub ? Sub->GetPlayerInput() : nullptr;
		if (!PlayerInput) return;
		FClipTakeSample S;
		S.Frame = At;
		for (const FString& Path : Armed)
		{
			UInputAction* A = ArmedActions.FindRef(Path).Get();
			if (!A) continue;
			S.Values.Add(Path, Taps.Read(A));
			Taps.Ensure(PlayerInput, A);
		}
		Samples.Add(MoveTemp(S));
	}

	void FPIEArrangementPlayer::FinishTake()
	{
		if (!bRecording || bTakeDone) return;
		bTakeDone = true;
		Taps.Remove();
		const double Start = RecordCfg.bPunch ? RecordCfg.PunchFrom : 0.0;
		const double End = PunchedOutAt >= 0.0 ? PunchedOutAt : Timeline.Position;
		if (!bPunchedIn) return;
		PunchClipInput(Take, Armed, ArmedTypes, Start, End, Samples);
		if (FClipCallRecorder::Get().IsWatching())
		{
			TMap<FString, TArray<FClipActionCall>> Calls = FClipCallRecorder::Get().Finish();
			for (FClipLaneRecording& Rec : LaneRecordings) if (TArray<FClipActionCall>* Seen = Calls.Find(Rec.LaneId)) Rec.Calls = MoveTemp(*Seen);
		}
		PunchClipLanes(Take, LaneRecordings, Start, End);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-HUB] take recorded: %d samples, frames %.1f..%.1f"), Samples.Num(), Start, End);
	}

	void FPIEArrangementPlayer::OnEndFrame()
	{
		if (!IsActive()) return;
		ULocalPlayer* LP = OwnLocalPlayer();
		UWorld* World = LP ? LP->GetWorld() : nullptr;
		if (!bClockStarted)
		{
			if (!World || !PawnOf(LP)) return;
			if (!ObserverRunId.IsEmpty())
			{
				FObserverRunClock Clock;
				if (!FPIEObserver::Get().GetRunClock(ObserverRunId, Clock) || Clock.State != EObserverState::Observing || Clock.FramesSampled == 0) return;
			}
			// Frame 0 comes after PIE's start-up hitch, so the world is as old at frame 0 in every play.
			if (World->GetDeltaSeconds() > 0.1f || ++SteadyFrames < 2) return;
			StartClock(World);
		}
		if (!World) return;
		if (!bRecording)
		{
			Timeline.Judge([this](const FHubCheckRun& C, double& V) { return ReadCheck(C, V); });
			const bool bAwaiting = Timeline.AwaitingVerdict();
			SetPaused(World, bAwaiting);
			State = bAwaiting ? EArrangementState::AwaitingVerdict : EArrangementState::Running;
			if (Timeline.Done())
			{
				Finish(EArrangementState::Finished, Timeline.Plan.RegionId.IsEmpty() ? FString(TEXT("arrangement played")) : FString::Printf(TEXT("region %s played"), *Timeline.Plan.RegionId));
			}
			return;
		}
		SampleTake(World);
		if (bTakeDone) Finish(EArrangementState::Finished, TEXT("take recorded"));
	}

	void FPIEArrangementPlayer::OnEndPIE(bool /*bIsSimulating*/)
	{
		if (!IsActive()) return;
		bStartedPIE = false;
		if (bRecording) Finish(EArrangementState::Finished, TEXT("PIE ended"));
		else Finish(EArrangementState::Error, TEXT("PIE ended before the arrangement finished"));
	}

	bool FPIEArrangementPlayer::Stop(const FString& InRunId, const FString& Reason, FString& OutError)
	{
		if (InRunId != RunId || bRecording)
		{
			OutError = FString::Printf(TEXT("no run %s is playing"), *InRunId);
			return false;
		}
		if (IsActive()) Finish(EArrangementState::Stopped, Reason.IsEmpty() ? FString(TEXT("stopped")) : Reason);
		return true;
	}

	bool FPIEArrangementPlayer::Verdict(const FString& InRunId, const FString& CheckId, const FString& InVerdict, const FString& Note, FString& OutError)
	{
		if (InRunId != RunId || bRecording || !IsActive())
		{
			OutError = FString::Printf(TEXT("no run %s is playing"), *InRunId);
			return false;
		}
		if (!Timeline.Verdict(CheckId, InVerdict, Note, OutError)) return false;
		if (!Timeline.AwaitingVerdict())
		{
			ULocalPlayer* LP = OwnLocalPlayer();
			SetPaused(LP ? LP->GetWorld() : nullptr, false);
			State = EArrangementState::Running;
		}
		return true;
	}

	TSharedPtr<FJsonObject> FPIEArrangementPlayer::Status(const FString& InRunId) const
	{
		if (InRunId != RunId || bRecording || State == EArrangementState::Idle) return nullptr;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("runId"), RunId);
		O->SetStringField(TEXT("state"), ArrangementStateToString(State));
		if (bClockStarted) O->SetNumberField(TEXT("frame"), FMath::RoundToDouble(Timeline.Position * 1000.0) / 1000.0);
		TArray<TSharedPtr<FJsonValue>> Checks;
		for (const FHubCheckRun& C : Timeline.Plan.Checks) Checks.Add(MakeShared<FJsonValueObject>(HubCheckRunToJson(C)));
		O->SetArrayField(TEXT("checks"), Checks);
		FString Err = Error;
		if (LaneErrors.Num() > 0 && State == EArrangementState::Error) Err += TEXT(" (") + FString::Join(LaneErrors, TEXT("; ")) + TEXT(")");
		if (!Err.IsEmpty()) O->SetStringField(TEXT("error"), Err);
		if (LaneErrors.Num() > 0) O->SetStringField(TEXT("laneErrors"), FString::Join(LaneErrors, TEXT("; ")));
		if (bStartedPIE) O->SetBoolField(TEXT("pieStarted"), true);
		return O;
	}

	bool FPIEArrangementPlayer::StopRecording(FClip& OutClip, FString& OutError)
	{
		if (!bRecording || State == EArrangementState::Idle)
		{
			OutError = TEXT("nothing is recording; start with clip_record_start");
			return false;
		}
		if (IsActive())
		{
			if (!bPunchedIn)
			{
				OutError = FString::Printf(TEXT("the recording has not reached its punch-in at frame %s yet (now at %s)"),
					*FString::SanitizeFloat(RecordCfg.PunchFrom), *FString::SanitizeFloat(Timeline.Position));
				Finish(EArrangementState::Stopped, TEXT("stopped before the punch-in"));
				bRecording = false;
				return false;
			}
			if (PunchedOutAt < 0.0) PunchedOutAt = Timeline.Position;
			Finish(EArrangementState::Finished, TEXT("recording stopped"));
		}
		bRecording = false;
		State = EArrangementState::Idle;
		OutClip = Take;
		return true;
	}

	void FPIEArrangementPlayer::Finish(EArrangementState FinalState, const FString& Reason)
	{
		if (!IsActive()) return;
		if (bRecording && bPunchedIn && !bTakeDone)
		{
			if (PunchedOutAt < 0.0) PunchedOutAt = Timeline.Position;
			FinishTake();
		}
		ULocalPlayer* LP = OwnLocalPlayer();
		SetPaused(LP ? LP->GetWorld() : nullptr, false);
		if (!bRecording) Timeline.SettleAtEnd(FString::Printf(TEXT("the run ended before the check settled (%s)"), *Reason));
		ReplayInputLock::Stop();
		RestoreFixedTimestep();
		FPSPin.Restore();
		Taps.Remove();
		for (TPair<FString, TUniquePtr<FClipPerformerDriver>>& D : Drivers) if (D.Value.IsValid()) D.Value->Stop();
		Drivers.Reset();
		if (FClipCallRecorder::Get().IsWatching()) FClipCallRecorder::Get().Finish();
		if (!ObserverRunId.IsEmpty()) FPIEObserver::Get().StopRun(ObserverRunId);
		if (PreActorTickHandle.IsValid()) FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
		PreActorTickHandle.Reset();
		if (EndFrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();
		State = FinalState;
		if (FinalState == EArrangementState::Error) Error = Reason;
		// A run ends the PIE session it started; a stopped one too.
		if (bStartedPIE && GEditor)
		{
			if (GEditor->PlayWorld) GEditor->RequestEndPlayMap();
			else if (GEditor->IsPlaySessionRequestQueued()) GEditor->CancelRequestPlaySession();
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-HUB] %s %s: %s"), *RunId, *ArrangementStateToString(FinalState), *Reason);
	}
}
