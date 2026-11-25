#include "Handlers/PIE/PIEClipPlayer.h"
#include "Handlers/PIE/MCPPIEPlaytestClip.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/PIEScenario.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIEReplayInputLock.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIELog.h"
#include "MCPPIEActionTrigger.h"
#include "HandlerWorld.h"
#include "HandlerPlaySession.h"
#include "HandlerPackageSave.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace MCPPIE
{
	namespace
	{
		FString ClipNowIso() { return FDateTime::UtcNow().ToIso8601(); }

		const TCHAR* const ClipRunStateNames[] = { TEXT("idle"), TEXT("armed"), TEXT("running"), TEXT("finished"), TEXT("stopped") };

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

		FInputActionValue ToActionValue(const UInputAction* Action, const FVector& V)
		{
			switch (Action->ValueType)
			{
			case EInputActionValueType::Boolean: return FInputActionValue(V.X != 0.0);
			case EInputActionValueType::Axis1D: return FInputActionValue(static_cast<float>(V.X));
			case EInputActionValueType::Axis2D: return FInputActionValue(FVector2D(V.X, V.Y));
			default: return FInputActionValue(V);
			}
		}

		UEnhancedInputLocalPlayerSubsystem* InputOf(ULocalPlayer* LocalPlayer)
		{
			return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		}

		APawn* PawnOf(ULocalPlayer* LocalPlayer)
		{
			APlayerController* PC = LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr;
			return PC ? PC->GetPawn() : nullptr;
		}
	}

	FString ClipRunStateToString(EClipRunState State) { return ClipRunStateNames[static_cast<int32>(State)]; }

	FPIEClipPlayer& FPIEClipPlayer::Get()
	{
		static FPIEClipPlayer Instance;
		return Instance;
	}

	void FPIEClipPlayer::Init()
	{
		if (EndPIEHandle.IsValid()) return;
		EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FPIEClipPlayer::OnEndPIE);
	}

	void FPIEClipPlayer::Shutdown()
	{
		if (EndPIEHandle.IsValid()) FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
		if (PreActorTickHandle.IsValid()) FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
		PreActorTickHandle.Reset();
		if (EndFrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();
		RestoreFixedTimestep();
		FPSPin.Restore();
		Taps.Remove();
		Runner.Reset();
		bHasRun = false;
	}

	bool FPIEClipPlayer::IsActive() const
	{
		return bHasRun && (Run.State == EClipRunState::Armed || Run.State == EClipRunState::Running);
	}

	ULocalPlayer* FPIEClipPlayer::TargetLocalPlayer() const
	{
		FString Err;
		return PIERemotePlayers::FindOwnLocalPlayer(Config.PIEInstance, Config.ClientId, Err);
	}

	double FPIEClipPlayer::HoldFrame(const FClipCheck& Check) const
	{
		// A recorded hold waits where the person satisfied it, not before: the input up to there is theirs.
		return Check.Recorded.bValid ? FMath::Max(Check.Start, Check.Recorded.Frame) : Check.Start;
	}

	bool FPIEClipPlayer::Start(const FClipPlayConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (IsActive())
		{
			OutError = FString::Printf(TEXT("Clip run %s is active; pie_clip_stop it first"), *Run.RunId);
			return false;
		}
		if (!GEditor)
		{
			OutError = TEXT("Playing a clip needs the editor");
			return false;
		}
		if (!Cfg.RegionId.IsEmpty() && !Cfg.Clip.Regions.ContainsByPredicate([&Cfg](const FClipRegion& R) { return R.Id == Cfg.RegionId; }))
		{
			TArray<FString> Ids;
			for (const FClipRegion& R : Cfg.Clip.Regions) Ids.Add(R.Id);
			OutError = FString::Printf(TEXT("The clip has no region '%s' (regions: %s)"), *Cfg.RegionId, Ids.Num() ? *FString::Join(Ids, TEXT(", ")) : TEXT("none"));
			return false;
		}
		if (Cfg.bRecord && GEditor->PlayWorld)
		{
			OutError = TEXT("Recording into a clip starts its own PIE session from frame 0; stop the running session first. ") + MCPStopPlayInEditorStep();
			return false;
		}
		if (!Cfg.Clip.Map.IsEmpty() && !MCPLoadMapIfNeeded(Cfg.Clip.Map, OutError))
		{
			OutError = FString::Printf(TEXT("The clip plays in %s: %s"), *Cfg.Clip.Map, *OutError);
			return false;
		}

		Config = Cfg;
		Run = FClipRun();
		Run.Clip = Cfg.Clip;
		Run.ClipPath = Cfg.ClipPath.IsEmpty() ? FString(TEXT("inline")) : Cfg.ClipPath;
		Run.ClipName = Cfg.Name.IsEmpty() ? FString(TEXT("inline")) : Cfg.Name;
		Run.RegionId = Cfg.RegionId;
		Run.RunId = MakeScriptRunId(Cfg.bRecord ? TEXT("clip_take") : TEXT("clip"), Run.ClipName);
		Run.OutputDir = DefaultScriptObservationsRoot() / Run.RunId;
		// Not results.json: that name is a step-list run's, and script_runs lists every one it finds.
		Run.ResultsPath = Run.OutputDir / TEXT("clip_results.json");
		// A recording replays its lead-in the way clip_run would, then runs on variable delta time from the punch-in,
		// so the person's game does not slow down when frames drop.
		Run.bFixedTimestep = Cfg.bRecord ? Cfg.PunchIn > 0.0 && Cfg.Clip.bFixedTimestep : (Cfg.FixedTimestep < 0 ? Cfg.Clip.bFixedTimestep : Cfg.FixedTimestep != 0);
		Run.bKeepPIE = Cfg.bKeepPIE;
		Run.bRecording = Cfg.bRecord;
		Run.PunchIn = Cfg.PunchIn;
		Run.PunchOut = Cfg.PunchOut;
		Run.TakeId = Run.RunId;
		for (const FClipCheck& C : Run.Clip.Checks)
		{
			FClipCheckRun CR;
			CR.Def = C;
			if (!Run.RegionId.IsEmpty())
			{
				const FClipRegion* R = Run.Clip.Regions.FindByPredicate([this](const FClipRegion& X) { return X.Id == Run.RegionId; });
				const bool bIn = R && C.Start >= R->Start - 1e-6 && C.Start <= R->End + 1e-6;
				if (!bIn)
				{
					CR.Status = EClipCheckStatus::Skipped;
					CR.Reason = FString::Printf(TEXT("outside region %s, the one played"), *Run.RegionId);
				}
			}
			Run.Checks.Add(CR);
		}

		// Values the checks read, observed on top of the clip's profile.
		TArray<FString> ProfileValues;
		if (!Run.Clip.Profile.IsEmpty())
		{
			if (const UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *Run.Clip.Profile))
			{
				for (const FMCPPIETrackedValueEntry& E : Profile->TrackedValues) ProfileValues.Add(E.Path);
			}
		}
		TArray<FScriptStepDef> Steps;
		for (const FClipCheck& C : Run.Clip.Checks) if (!C.bManual) Steps.Add(ClipCheckToStep(C));
		FObserverArmConfig ObsCfg;
		ObsCfg.ProfilePath = Run.Clip.Profile;
		ObsCfg.RunId = Run.RunId;
		ObsCfg.ClientId = Cfg.ClientId;
		ObsCfg.PIEInstance = Cfg.PIEInstance;
		ObsCfg.SampleHz = Run.Clip.Rate;
		CollectScriptObservedNames(Steps, ProfileValues, ObsCfg.ExtraTrackedValues, ObsCfg.ExtraTrackedActors, ObsCfg.ExtraTrackedWidgets);
		FString ObsMsg;
		if (!FPIEObserver::Get().Arm(ObsCfg, OutError, ObsMsg))
		{
			return false;
		}

		Actions.Reset();
		TriggerWatches.Reset();
		Reference.Reset();
		LoadReference();
		Runner = MakeShared<FStepActionRunner>();
		Runner->OwnPIEInstance = Cfg.PIEInstance;
		Runner->OwnLocalPlayer = Cfg.ClientId;
		Runner->CaptureRoot = Run.OutputDir;
		Run.State = EClipRunState::Armed;
		bHasRun = true;

		if ((Cfg.bStartPIE || Cfg.bRecord) && !GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress())
		{
			MCPPlaySession::Request(/*bNeedsEditorWorld=*/true);
			Run.bStartedPIE = true;
		}
		if (!PreActorTickHandle.IsValid()) PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddRaw(this, &FPIEClipPlayer::OnPreActorTick);
		if (!EndFrameHandle.IsValid()) EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FPIEClipPlayer::OnEndFrame);

		OutMessage = FString::Printf(TEXT("Clip run %s armed: %d tracks, %d events, %d checks, %s frames at %d fps%s"),
			*Run.RunId, Run.Clip.Tracks.Num(), Run.Clip.Events.Num(), Run.Clip.Checks.Num(),
			*FString::SanitizeFloat(Run.Clip.EffectiveLength()), Run.Clip.Rate, Run.bRecording ? TEXT(", recording") : TEXT(""));
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-CLIP] %s"), *OutMessage);
		return true;
	}

	void FPIEClipPlayer::LoadReference()
	{
		Run.bHasReference = false;
		if (Run.Clip.Reference.IsEmpty()) return;
		const FString Path = DefaultScriptObservationsRoot() / Run.Clip.Reference / TEXT("track.json");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Run.ReferenceError = FString::Printf(TEXT("the reference run's track is not on disk: %s"), *Path);
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Rows;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Rows))
		{
			Run.ReferenceError = FString::Printf(TEXT("the reference run's track does not parse: %s"), *Path);
			return;
		}
		for (const TSharedPtr<FJsonValue>& R : Rows)
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!R->TryGetArray(A) || !A || A->Num() < 7) continue;
			FClipRun::FPawnSample S;
			S.Frame = (*A)[0]->AsNumber();
			S.Location = FVector((*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber());
			S.Velocity = FVector((*A)[4]->AsNumber(), (*A)[5]->AsNumber(), (*A)[6]->AsNumber());
			Reference.Add(S);
		}
		Run.bHasReference = Reference.Num() > 0;
	}

	void FPIEClipPlayer::ApplyFixedTimestep()
	{
		if (bFixedApplied || !Run.bFixedTimestep) return;
		bSavedUseFixed = FApp::UseFixedTimeStep();
		SavedFixedDelta = FApp::GetFixedDeltaTime();
		FApp::SetFixedDeltaTime(1.0 / FMath::Max(1, Run.Clip.Rate));
		FApp::SetUseFixedTimeStep(true);
		bFixedApplied = true;
	}

	void FPIEClipPlayer::RestoreFixedTimestep()
	{
		if (!bFixedApplied) return;
		FApp::SetUseFixedTimeStep(bSavedUseFixed);
		FApp::SetFixedDeltaTime(SavedFixedDelta);
		bFixedApplied = false;
	}

	void FPIEClipPlayer::SetPaused(UWorld* World, bool bPaused)
	{
		if (!World) return;
		UGameplayStatics::SetGamePaused(World, bPaused);
		Run.bPausedForVerdict = bPaused;
	}

	void FPIEClipPlayer::StartClock(UWorld* World)
	{
		Run.bClockStarted = true;
		Run.StartedAt = ClipNowIso();
		Run.State = EClipRunState::Running;
		Run.StartGameSeconds = World->GetTimeSeconds();
		Run.LastGameSeconds = Run.StartGameSeconds;
		Run.Position = 0.0;
		Run.GameMs = 0.0;
		ApplyFixedTimestep();
		if (Run.Clip.Seed != 0) FMath::RandInit(static_cast<int32>(Run.Clip.Seed));
		ULocalPlayer* LP = TargetLocalPlayer();
		APawn* Pawn = PawnOf(LP);
		if (Run.Clip.bHasStartPose && Pawn && !Run.bRecording)
		{
			FString Err;
			PIERemotePlayers::TeleportOnAuthority(LP->PlayerController, Pawn, Run.Clip.StartLocation, Run.Clip.StartRotation, Err);
		}
		if (Run.bRecording && Pawn && Run.PunchIn <= 0.0)
		{
			// A take from frame 0 sets where the clip starts.
			Run.Clip.bHasStartPose = true;
			Run.Clip.StartLocation = Pawn->GetActorLocation();
			Run.Clip.StartRotation = Pawn->GetActorRotation();
		}
		if (Config.bLockInput && !(Run.bRecording && Run.PunchIn <= 0.0)) ReplayInputLock::Start(World);
		if (Run.bRecording && Run.PunchIn <= 0.0) PunchIn(World);
		FireEvents(World, 0.0);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-CLIP] %s started at game %.3f s%s"), *Run.RunId, Run.StartGameSeconds, Run.bFixedTimestep ? TEXT(", fixed timestep") : TEXT(""));
	}

	void FPIEClipPlayer::PunchIn(UWorld* World)
	{
		Run.bPunchedIn = true;
		ReplayInputLock::Stop();
		RestoreFixedTimestep();
		FPSPin.Apply(Run.Clip.Rate);
		ULocalPlayer* LP = TargetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Sub = InputOf(LP);
		UEnhancedPlayerInput* PlayerInput = Sub ? Sub->GetPlayerInput() : nullptr;
		Run.Armed.Reset();
		Run.ArmedTypes.Reset();
		auto Arm = [this](const UInputAction* A)
		{
			const FString Path = A->GetPathName();
			Run.Armed.AddUnique(Path);
			Run.ArmedTypes.Add(Path, ClipTypeOf(A));
			Actions.Add(Path, const_cast<UInputAction*>(A));
		};
		if (Config.Armed.Num() > 0)
		{
			for (const FString& P : Config.Armed)
			{
				if (const UInputAction* A = LoadObject<UInputAction>(nullptr, *NormalizeScriptInputActionPath(P))) Arm(A);
			}
		}
		else if (PlayerInput)
		{
			// Every action the player has instance data for: the actions its mapping contexts map.
			for (TObjectIterator<UInputAction> It; It; ++It)
			{
				if (*It && PlayerInput->FindActionInstanceData(*It)) Arm(*It);
			}
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-CLIP] %s punched in at frame %.1f, recording %d actions"), *Run.RunId, Run.Position, Run.Armed.Num());
	}

	void FPIEClipPlayer::FireEvents(UWorld* World, double UpTo)
	{
		while (Run.NextEvent < Run.Clip.Events.Num() && Run.Clip.Events[Run.NextEvent].At <= UpTo + 1e-6)
		{
			const FClipEvent& E = Run.Clip.Events[Run.NextEvent++];
			FClipFiredEvent F;
			F.Id = E.Id;
			F.Frame = Run.Position;
			F.GameMs = Run.GameMs;
			FString Err;
			if (E.IsArrange())
			{
				FScenario S;
				S.Arrange.Add(MakeShared<FJsonValueObject>(E.Do));
				int32 Applied = 0;
				F.bOk = FPIEScenario::ApplyArrange(StepActionWorld(), S, Applied, Err);
			}
			else
			{
				FStepAction A;
				F.bOk = ParseStepAction(E.Do, A, Err) && Runner.IsValid() && Runner->Run(A, Err);
			}
			F.Error = Err;
			if (!F.bOk) UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-CLIP] event %s at frame %.1f failed: %s"), *E.Id, Run.Position, *Err);
			Run.FiredEvents.Add(F);
		}
	}

	void FPIEClipPlayer::FeedInput(UWorld* World)
	{
		UEnhancedInputLocalPlayerSubsystem* Sub = InputOf(TargetLocalPlayer());
		if (!Sub) return;
		for (const FClipTrack& T : Run.Clip.Tracks)
		{
			if (Run.bPunchedIn && Run.PunchedOutAt < 0.0 && Run.Armed.Contains(T.Action)) continue;
			bool bActive = false;
			const FVector V = EvaluateClipTrack(T, Run.Position, bActive);
			if (!bActive) continue;
			TWeakObjectPtr<UInputAction>& Weak = Actions.FindOrAdd(T.Action);
			UInputAction* A = Weak.Get();
			if (!A)
			{
				A = LoadObject<UInputAction>(nullptr, *T.Action);
				Weak = A;
			}
			if (A) Sub->InjectInputForAction(A, ToActionValue(A, V), {}, {});
		}
	}

	void FPIEClipPlayer::OnPreActorTick(UWorld* World, ELevelTick /*TickType*/, float /*DeltaSeconds*/)
	{
		if (!IsActive() || !Run.bClockStarted || !World) return;
		ULocalPlayer* LP = TargetLocalPlayer();
		if (!LP || LP->GetWorld() != World) return;

		// Game time is already this frame's here. Paused, it does not move, and neither does the clip.
		const double Now = World->GetTimeSeconds();
		const double Dt = FMath::Max(0.0, Now - Run.LastGameSeconds);
		Run.LastGameSeconds = Now;
		Run.GameMs = (Now - Run.StartGameSeconds) * 1000.0;
		if (Dt <= 0.0) return;
		Run.Frames++;

		if (Run.HoldingCheck != INDEX_NONE)
		{
			Run.HeldMs += Dt * 1000.0;
			Run.Checks[Run.HoldingCheck].HeldMs += Dt * 1000.0;
		}
		else
		{
			double Next = Run.Position + Dt * Run.Clip.Rate;
			// Stop at the first unresolved hold check this step would pass.
			for (int32 i = 0; i < Run.Checks.Num(); ++i)
			{
				const FClipCheckRun& C = Run.Checks[i];
				if (C.Def.Mode != EClipCheckMode::Hold) continue;
				if (C.Status == EClipCheckStatus::Passed || C.Status == EClipCheckStatus::Failed || C.Status == EClipCheckStatus::Skipped) continue;
				const double At = C.Def.bManual ? C.Def.Start : HoldFrame(C.Def);
				// A take's lead-in is a replay and holds as one; from the punch-in the person plays and nothing holds.
				const bool bLive = Run.bRecording && (Run.bPunchedIn || At >= Run.PunchIn - 1e-6);
				if (At > Run.Position - 1e-6 && At <= Next + 1e-6 && !bLive)
				{
					Next = At;
					Run.HoldingCheck = i;
					Run.HoldStartMs = Run.GameMs;
					break;
				}
			}
			// A fixed step aimed at a recorded frame lands on it, give or take float rounding: land on it exactly, so
			// a note recorded to start on that frame starts on it.
			Run.Position = Run.HoldingCheck == INDEX_NONE ? Run.Clip.SnapToFrameTime(Next) : Next;
		}

		// Recording: the punch-in and punch-out frames.
		if (Run.bRecording && !Run.bPunchedIn && Run.Position >= Run.PunchIn - 1e-6) PunchIn(World);
		if (Run.bRecording && Run.bPunchedIn && Run.PunchedOutAt < 0.0 && Run.PunchOut >= 0.0 && Run.Position >= Run.PunchOut - 1e-6)
		{
			Run.PunchedOutAt = Run.Position;
			FinishTake();
		}

		FireEvents(World, Run.Position);
		FeedInput(World);
		if (Runner.IsValid()) Runner->TickStops(FPlatformTime::Seconds());
	}

	bool FPIEClipPlayer::ReadCheckValue(int32 Index, double& OutValue)
	{
		const FClipCheckRun& C = Run.Checks[Index];
		const FScriptExpect& E = C.Def.Expect;
		const FPIEObserver& Obs = FPIEObserver::Get();
		if (IsScriptTriggerOp(E.Op))
		{
			// A fresh trigger since the check started: the action had to be seen not triggering first.
			FTriggerWatch& W = TriggerWatches.FindOrAdd(Index);
			const UInputAction* Action = W.Action.Get();
			if (!Action)
			{
				Action = LoadObject<UInputAction>(nullptr, *E.Value);
				W.Action = Action;
			}
			bool bTriggered = false;
			FVector Value;
			FString Err;
			if (!Action || !ReadActionTriggerState(TargetLocalPlayer(), Action, bTriggered, Value, Err)) return false;
			if (!bTriggered) W.bReleased = true;
			OutValue = (bTriggered && W.bReleased) ? 1.0 : 0.0;
			return true;
		}
		if (E.Op == EScriptExpectOp::Visible)
		{
			bool bVisible = false;
			if (!Obs.ReadWidgetVisible(Run.RunId, E.Value, bVisible)) return false;
			OutValue = bVisible ? 1.0 : 0.0;
			return true;
		}
		if (E.Op == EScriptExpectOp::Exists)
		{
			bool bResolved = false;
			if (!Obs.ReadActorResolved(Run.RunId, E.Value, bResolved)) return false;
			OutValue = bResolved ? 1.0 : 0.0;
			return true;
		}
		if (IsPawnChannelName(E.Value))
		{
			// Read off the pawn the player possesses now: the observer keeps the pawn it attached to, and a level that
			// hands the player another pawn once play starts would read the first one's last state forever.
			APawn* Pawn = PawnOf(TargetLocalPlayer());
			if (!Pawn) return false;
			FCSVRow Row;
			Row.PawnLocation = Pawn->GetActorLocation();
			Row.PawnRotation = Pawn->GetActorRotation();
			Row.PawnVelocity = Pawn->GetVelocity();
			if (const ACharacter* Ch = Cast<ACharacter>(Pawn))
			{
				if (const UCharacterMovementComponent* Move = Ch->GetCharacterMovement()) Row.PawnVelocity = Move->Velocity;
			}
			Row.Speed2D = static_cast<float>(Row.PawnVelocity.Size2D());
			return ReadPawnChannel(Row, E.Value, OutValue);
		}
		return Obs.ReadTrackedValue(Run.RunId, E.Value, OutValue);
	}

	void FPIEClipPlayer::JudgeChecks(UWorld* World)
	{
		for (int32 i = 0; i < Run.Checks.Num(); ++i)
		{
			FClipCheckRun& C = Run.Checks[i];
			if (C.Status == EClipCheckStatus::Passed || C.Status == EClipCheckStatus::Failed || C.Status == EClipCheckStatus::Skipped) continue;
			FClipCheckFrame F;
			F.Frame = Run.Position;
			F.GameMs = Run.GameMs;
			if (!C.Def.bManual) F.bObserved = ReadCheckValue(i, F.Value);
			const EClipCheckStatus Before = C.Status;
			AdvanceClipCheck(C, F);

			if (C.Def.Mode == EClipCheckMode::Hold)
			{
				if (C.Def.bManual && C.Status == EClipCheckStatus::AwaitingVerdict && Before == EClipCheckStatus::Pending)
				{
					// Playback stands at the check with the game paused until a verdict comes.
					Run.HoldingCheck = i;
					Run.HoldStartMs = Run.GameMs;
					SetPaused(World, true);
					UE_LOG(LogMCPPIE, Log, TEXT("[PIE-CLIP] %s awaits a verdict on %s at frame %.1f"), *Run.RunId, *C.Def.Id, Run.Position);
				}
				if (Run.bRecording && C.Status == EClipCheckStatus::Passed && Before != EClipCheckStatus::Passed && !C.Def.bManual)
				{
					// How the hold resolved in the take: replay waits there, and reports drift against it.
					FClipCheck* Def = Run.Clip.Checks.FindByPredicate([&C](const FClipCheck& X) { return X.Id == C.Def.Id; });
					if (Def)
					{
						Def->Recorded.bValid = true;
						Def->Recorded.Frame = Run.Position;
						Def->Recorded.GameMs = Run.GameMs;
						Def->Recorded.Value = C.Last;
					}
				}
				if (Run.HoldingCheck == i && C.Status == EClipCheckStatus::Running)
				{
					const double Timeout = (C.Def.Length > 0.0 ? C.Def.Length : Run.Clip.HoldTimeout) * 1000.0 / FMath::Max(1, Run.Clip.Rate);
					if (C.HeldMs >= Timeout)
					{
						C.Status = EClipCheckStatus::Failed;
						C.EndedFrame = Run.Position;
						C.EndedMs = Run.GameMs;
						C.Reason = FString::Printf(TEXT("%s %s did not become true within %.0f ms of game time held at frame %s"),
							*C.Def.Expect.Value, *ScriptExpectOpToString(C.Def.Expect.Op), Timeout, *FString::SanitizeFloat(Run.Position));
					}
				}
				if (Run.HoldingCheck == i && C.Status != EClipCheckStatus::Running && C.Status != EClipCheckStatus::AwaitingVerdict)
				{
					Run.HoldingCheck = INDEX_NONE;
				}
			}
		}
	}

	void FPIEClipPlayer::SamplePawnAndDrift(UWorld* World)
	{
		APawn* Pawn = PawnOf(TargetLocalPlayer());
		if (!Pawn) return;
		FClipRun::FPawnSample S;
		S.Frame = Run.Position;
		S.Location = Pawn->GetActorLocation();
		S.Velocity = Pawn->GetVelocity();
		if (const ACharacter* Ch = Cast<ACharacter>(Pawn))
		{
			if (const UCharacterMovementComponent* Move = Ch->GetCharacterMovement()) S.Velocity = Move->Velocity;
		}
		// While holding, the clip stands still and the world does not: those frames are not compared.
		if (Run.HoldingCheck == INDEX_NONE || Run.PawnTrack.Num() == 0 || !FMath::IsNearlyEqual(Run.PawnTrack.Last().Frame, S.Frame, 1e-6))
		{
			Run.PawnTrack.Add(S);
			if (Run.bHasReference && Run.HoldingCheck == INDEX_NONE)
			{
				const FClipRun::FPawnSample* Best = nullptr;
				for (const FClipRun::FPawnSample& R : Reference)
				{
					if (!Best || FMath::Abs(R.Frame - S.Frame) < FMath::Abs(Best->Frame - S.Frame)) Best = &R;
					if (R.Frame > S.Frame + 1.0) break;
				}
				if (Best && FMath::Abs(Best->Frame - S.Frame) <= 0.5)
				{
					FClipDriftSample D;
					D.Frame = S.Frame;
					D.PositionCm = static_cast<float>((S.Location - Best->Location).Size());
					D.VelocityCms = static_cast<float>((S.Velocity - Best->Velocity).Size());
					Run.Drift.Add(D);
				}
			}
		}
	}

	void FPIEClipPlayer::SampleTake(UWorld* World)
	{
		if (!Run.bRecording || !Run.bPunchedIn || Run.PunchedOutAt >= 0.0) return;
		UEnhancedInputLocalPlayerSubsystem* Sub = InputOf(TargetLocalPlayer());
		UEnhancedPlayerInput* PlayerInput = Sub ? Sub->GetPlayerInput() : nullptr;
		if (!PlayerInput) return;
		FClipTakeSample S;
		// To a thousandth of a frame, the precision the clip stores frame times in, so a note starts exactly on the
		// frame time a replay lands on.
		S.Frame = FMath::RoundToDouble(Run.Position * 1000.0) / 1000.0;
		for (const FString& Path : Run.Armed)
		{
			UInputAction* A = Actions.FindRef(Path).Get();
			if (!A) continue;
			S.Values.Add(Path, Taps.Read(A));
			Taps.Ensure(PlayerInput, A);
		}
		Run.Samples.Add(MoveTemp(S));
	}

	void FPIEClipPlayer::FinishTake()
	{
		if (!Run.bRecording || Run.bTakeSaved) return;
		Taps.Remove();
		const double End = Run.PunchedOutAt >= 0.0 ? Run.PunchedOutAt : Run.Position;
		CompClipTake(Run.Clip, Run.Armed, Run.ArmedTypes, Run.PunchIn, End, Run.Samples, Run.TakeId, ClipNowIso());
		// The take's run is the clip as it now stands, played and recorded: the replayed lead-in, the take, and what
		// played after the punch-out. Its pawn track is what later runs are measured against.
		Run.Clip.Reference = Run.RunId;
		Run.bTakeSaved = true;
		if (Run.ClipPath == TEXT("inline")) return;
		UMCPPIEPlaytestClip* Asset = LoadObject<UMCPPIEPlaytestClip>(nullptr, *Run.ClipPath);
		if (!Asset)
		{
			Run.TakeError = FString::Printf(TEXT("the clip asset %s did not load, so the take was not saved"), *Run.ClipPath);
			return;
		}
		FString Json;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(ClipToJson(Run.Clip), W);
		Asset->ClipJson = Json;
		FString SaveError;
		if (!SaveAssetPackageChecked(Asset, SaveError))
		{
			Run.TakeError = FString::Printf(TEXT("the take is in the run's results but %s did not save: %s"), *Run.ClipPath, *SaveError);
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-CLIP] take %s saved into %s (%d samples, frames %.1f..%.1f)"), *Run.TakeId, *Run.ClipPath, Run.Samples.Num(), Run.PunchIn, End);
	}

	void FPIEClipPlayer::OnEndFrame()
	{
		if (!IsActive()) return;
		FObserverRunClock Clock;
		if (!FPIEObserver::Get().GetRunClock(Run.RunId, Clock))
		{
			Finish(EClipRunState::Finished, TEXT("observation ended"));
			return;
		}
		if (Clock.State == EObserverState::Completed)
		{
			const FString Err = FPIEObserver::Get().GetStatus().LastError;
			Finish(EClipRunState::Finished, Err.IsEmpty() ? FString(TEXT("observation ended")) : Err);
			return;
		}
		Run.WaitingFor = Clock.WaitingFor;
		if (Clock.State != EObserverState::Observing || Clock.FramesSampled == 0) return;
		ULocalPlayer* LP = TargetLocalPlayer();
		UWorld* World = LP ? LP->GetWorld() : nullptr;
		if (!World) return;
		if (!Run.bClockStarted)
		{
			// Frame 0 comes after PIE's start-up hitch (a first frame clamped to 0.4 s), in a take and a replay alike,
			// so neither has it on the clip's clock and the world is as old at frame 0 in both.
			if (Clock.FramesSampled < 2 || World->GetDeltaSeconds() > 0.1f) return;
			StartClock(World);
		}
		JudgeChecks(World);
		SamplePawnAndDrift(World);
		SampleTake(World);
		if (Run.bRecording && Run.PunchedOutAt >= 0.0)
		{
			// The take ends at the punch-out. What follows was recorded for a different state at the cut, and the
			// take's run is the reference later runs are measured against, so it stops where the take does.
			Finish(EClipRunState::Finished, TEXT("take recorded"));
			return;
		}
		if (bFixedApplied)
		{
			// The next frame lands on the next recorded frame where the clip has them, else 1/rate later.
			FApp::SetFixedDeltaTime(Run.HoldingCheck != INDEX_NONE ? 1.0 / FMath::Max(1, Run.Clip.Rate) : Run.Clip.NextFrameDelta(Run.Position));
		}

		const FClipRegion* Region = Run.RegionId.IsEmpty() ? nullptr : Run.Clip.Regions.FindByPredicate([this](const FClipRegion& R) { return R.Id == Run.RegionId; });
		const double EndFrame = Region ? Region->End : Run.Clip.EffectiveLength();
		const bool bChecksOpen = Run.Checks.ContainsByPredicate([](const FClipCheckRun& C)
		{
			return C.Status == EClipCheckStatus::Running || C.Status == EClipCheckStatus::AwaitingVerdict;
		});
		const bool bStillRecording = Run.bRecording && Run.bPunchedIn && Run.PunchedOutAt < 0.0 && Run.PunchOut < 0.0;
		if (Run.Position >= EndFrame - 1e-6 && !bChecksOpen && Run.HoldingCheck == INDEX_NONE && !bStillRecording)
		{
			Finish(EClipRunState::Finished, Region ? FString::Printf(TEXT("region %s played"), *Region->Id) : FString(TEXT("clip played")));
		}
	}

	void FPIEClipPlayer::OnEndPIE(bool /*bIsSimulating*/)
	{
		if (!IsActive()) return;
		Run.bStartedPIE = false;
		Finish(EClipRunState::Finished, TEXT("PIE ended"));
	}

	bool FPIEClipPlayer::Stop(const FString& Reason, FString& OutError)
	{
		if (!IsActive())
		{
			OutError = TEXT("No clip run is active");
			return false;
		}
		Finish(EClipRunState::Stopped, Reason.IsEmpty() ? FString(TEXT("stopped")) : Reason);
		return true;
	}

	bool FPIEClipPlayer::StopRecording(FString& OutError)
	{
		if (!IsActive() || !Run.bRecording)
		{
			OutError = TEXT("No clip is recording");
			return false;
		}
		if (!Run.bPunchedIn)
		{
			OutError = FString::Printf(TEXT("The recording has not reached its punch-in at frame %s yet (now at %s)"),
				*FString::SanitizeFloat(Run.PunchIn), *FString::SanitizeFloat(Run.Position));
			return false;
		}
		Run.PunchedOutAt = Run.Position;
		FinishTake();
		Finish(EClipRunState::Finished, TEXT("recording stopped"));
		return true;
	}

	bool FPIEClipPlayer::Verdict(const FString& CheckId, const FString& InVerdict, const FString& Note, const FString& Source,
		const FString& EvidenceNote, const TArray<FString>& Captures, FString& OutError)
	{
		if (!bHasRun)
		{
			OutError = TEXT("No clip run to give a verdict on");
			return false;
		}
		const int32 Index = Run.Checks.IndexOfByPredicate([&CheckId](const FClipCheckRun& C) { return C.Def.Id == CheckId; });
		if (Index == INDEX_NONE)
		{
			TArray<FString> Ids;
			for (const FClipCheckRun& C : Run.Checks) if (C.Def.bManual) Ids.Add(C.Def.Id);
			OutError = FString::Printf(TEXT("The run has no check '%s' (manual checks: %s)"), *CheckId, Ids.Num() ? *FString::Join(Ids, TEXT(", ")) : TEXT("none"));
			return false;
		}
		FClipCheckRun& C = Run.Checks[Index];
		if (!C.Def.bManual)
		{
			OutError = FString::Printf(TEXT("Check %s is judged from the observed values, not a verdict"), *CheckId);
			return false;
		}
		if (C.Status != EClipCheckStatus::AwaitingVerdict)
		{
			OutError = FString::Printf(TEXT("Check %s is %s, not awaiting a verdict"), *CheckId, *ClipCheckStatusToString(C.Status));
			return false;
		}
		const FString VerdictSource = NormalizeScriptVerdictSource(Source);
		if (VerdictSource.IsEmpty())
		{
			OutError = FString::Printf(TEXT("source must be human or agent, got '%s'"), *Source);
			return false;
		}
		if (VerdictSource == TEXT("agent") && EvidenceNote.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("an agent verdict needs evidence: {note, captures?}, a note saying what was observed");
			return false;
		}
		if (VerdictSource == TEXT("agent") && !C.Def.bAgentJudgeable)
		{
			OutError = FString::Printf(TEXT("check %s is agent_judgeable false; only a person can give its verdict"), *CheckId);
			return false;
		}
		const FString V = InVerdict.ToLower();
		if (V == TEXT("pass")) C.Status = EClipCheckStatus::Passed;
		else if (V == TEXT("fail")) C.Status = EClipCheckStatus::Failed;
		else if (V == TEXT("skip")) C.Status = EClipCheckStatus::Skipped;
		else
		{
			OutError = FString::Printf(TEXT("verdict must be pass, fail or skip, got '%s'"), *InVerdict);
			return false;
		}
		C.Note = Note;
		C.VerdictSource = VerdictSource;
		C.EvidenceNote = EvidenceNote.TrimStartAndEnd();
		C.EvidenceCaptures = Captures;
		C.EndedFrame = Run.Position;
		C.EndedMs = Run.GameMs;
		if (Run.HoldingCheck == Index)
		{
			Run.HoldingCheck = INDEX_NONE;
			ULocalPlayer* LP = TargetLocalPlayer();
			SetPaused(LP ? LP->GetWorld() : nullptr, false);
		}
		if (!IsActive()) WriteResults();
		return true;
	}

	void FPIEClipPlayer::Finish(EClipRunState FinalState, const FString& Reason)
	{
		if (!IsActive()) return;
		if (Run.bRecording && Run.bPunchedIn && !Run.bTakeSaved)
		{
			if (Run.PunchedOutAt < 0.0) Run.PunchedOutAt = Run.Position;
			FinishTake();
		}
		if (Run.bPausedForVerdict)
		{
			ULocalPlayer* LP = TargetLocalPlayer();
			SetPaused(LP ? LP->GetWorld() : nullptr, false);
		}
		for (FClipCheckRun& C : Run.Checks)
		{
			SettleClipCheckAtEnd(C, FString::Printf(TEXT("the run ended before the check settled (%s)"), *Reason));
		}
		ReplayInputLock::Stop();
		RestoreFixedTimestep();
		FPSPin.Restore();
		Taps.Remove();
		if (Runner.IsValid()) Runner->Reset();
		FPIEObserver::Get().StopRun(Run.RunId);
		if (PreActorTickHandle.IsValid()) FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
		PreActorTickHandle.Reset();
		if (EndFrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();

		Run.State = FinalState;
		Run.StopReason = Reason;
		Run.EndedAt = ClipNowIso();
		if (Run.StartedAt.IsEmpty()) Run.StartedAt = Run.EndedAt;
		Run.WaitingFor.Reset();
		WriteResults();

		if (Run.bStartedPIE && !Run.bKeepPIE && GEditor)
		{
			if (GEditor->PlayWorld) GEditor->RequestEndPlayMap();
			else if (GEditor->IsPlaySessionRequestQueued()) GEditor->CancelRequestPlaySession();
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-CLIP] %s %s: %s -> %s"), *Run.RunId, *ClipRunStateToString(FinalState), *Reason, *Run.ResultsPath);
	}

	TSharedRef<FJsonObject> FPIEClipPlayer::RunToJson(bool bLive) const
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("version"), 1);
		O->SetStringField(TEXT("run_id"), Run.RunId);
		O->SetStringField(TEXT("clip"), Run.ClipPath);
		O->SetStringField(TEXT("clip_name"), Run.ClipName);
		O->SetStringField(TEXT("state"), ClipRunStateToString(Run.State));
		O->SetStringField(TEXT("mode"), Run.bRecording ? TEXT("record") : TEXT("play"));
		if (!Run.StopReason.IsEmpty()) O->SetStringField(TEXT("stop_reason"), Run.StopReason);
		if (!Run.WaitingFor.IsEmpty()) O->SetStringField(TEXT("waiting_for"), Run.WaitingFor);
		if (!Run.RegionId.IsEmpty()) O->SetStringField(TEXT("region"), Run.RegionId);
		O->SetStringField(TEXT("started_at"), Run.StartedAt);
		O->SetStringField(TEXT("ended_at"), Run.EndedAt);
		O->SetNumberField(TEXT("rate"), Run.Clip.Rate);
		O->SetBoolField(TEXT("fixed_timestep"), Run.bFixedTimestep);
		O->SetNumberField(TEXT("position"), Run.Position);
		O->SetNumberField(TEXT("length"), Run.Clip.EffectiveLength());
		O->SetNumberField(TEXT("game_ms"), Run.GameMs);
		O->SetNumberField(TEXT("held_ms"), Run.HeldMs);
		O->SetNumberField(TEXT("frames"), Run.Frames);
		if (Run.HoldingCheck != INDEX_NONE && Run.Checks.IsValidIndex(Run.HoldingCheck))
		{
			O->SetStringField(TEXT("holding"), Run.Checks[Run.HoldingCheck].Def.Id);
		}
		O->SetStringField(TEXT("output_dir"), Run.OutputDir);
		O->SetStringField(TEXT("results_path"), Run.ResultsPath);

		int32 Passed = 0, Failed = 0, Skipped = 0, Pending = 0, Awaiting = 0;
		TArray<TSharedPtr<FJsonValue>> Checks;
		TArray<TSharedPtr<FJsonValue>> AwaitingIds;
		for (const FClipCheckRun& C : Run.Checks)
		{
			Checks.Add(MakeShared<FJsonValueObject>(ClipCheckRunToJson(C)));
			switch (C.Status)
			{
			case EClipCheckStatus::Passed: ++Passed; break;
			case EClipCheckStatus::Failed: ++Failed; break;
			case EClipCheckStatus::Skipped: ++Skipped; break;
			case EClipCheckStatus::AwaitingVerdict: ++Awaiting; ++Pending; AwaitingIds.Add(MakeShared<FJsonValueString>(C.Def.Id)); break;
			default: ++Pending; break;
			}
		}
		O->SetArrayField(TEXT("checks"), Checks);
		O->SetArrayField(TEXT("awaiting"), AwaitingIds);
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("total"), Run.Checks.Num());
		Summary->SetNumberField(TEXT("passed"), Passed);
		Summary->SetNumberField(TEXT("failed"), Failed);
		Summary->SetNumberField(TEXT("skipped"), Skipped);
		Summary->SetNumberField(TEXT("pending"), Pending);
		Summary->SetNumberField(TEXT("awaiting"), Awaiting);
		O->SetObjectField(TEXT("summary"), Summary);
		O->SetBoolField(TEXT("passed"), Run.State == EClipRunState::Finished && Failed == 0 && Pending == 0 && Passed > 0);

		const float ThrPos = 5.f, ThrVel = 25.f;
		TArray<TSharedPtr<FJsonValue>> Regions;
		for (const FClipRegionResult& R : SummarizeClipRegions(Run.Clip, Run.Checks, Run.Drift, ThrPos, ThrVel))
		{
			TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
			RO->SetStringField(TEXT("id"), R.Def.Id);
			RO->SetStringField(TEXT("name"), R.Def.Name);
			RO->SetNumberField(TEXT("start"), R.Def.Start);
			RO->SetNumberField(TEXT("end"), R.Def.End);
			RO->SetStringField(TEXT("status"), R.Status);
			TArray<TSharedPtr<FJsonValue>> Ids;
			for (const FString& Id : R.CheckIds) Ids.Add(MakeShared<FJsonValueString>(Id));
			RO->SetArrayField(TEXT("checks"), Ids);
			if (R.bHasDrift)
			{
				TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
				D->SetNumberField(TEXT("max_position_cm"), R.MaxPositionCm);
				D->SetNumberField(TEXT("max_velocity_cms"), R.MaxVelocityCms);
				D->SetNumberField(TEXT("frames_compared"), R.FramesCompared);
				if (R.FirstOverFrame >= 0.0) D->SetNumberField(TEXT("first_over_frame"), R.FirstOverFrame);
				else D->SetField(TEXT("first_over_frame"), MakeShared<FJsonValueNull>());
				RO->SetObjectField(TEXT("drift"), D);
			}
			Regions.Add(MakeShared<FJsonValueObject>(RO));
		}
		O->SetArrayField(TEXT("regions"), Regions);

		TSharedRef<FJsonObject> Drift = MakeShared<FJsonObject>();
		if (!Run.Clip.Reference.IsEmpty()) Drift->SetStringField(TEXT("reference"), Run.Clip.Reference);
		Drift->SetBoolField(TEXT("compared"), Run.bHasReference);
		if (!Run.ReferenceError.IsEmpty()) Drift->SetStringField(TEXT("error"), Run.ReferenceError);
		float MaxPos = 0.f, MaxVel = 0.f;
		double FirstOver = -1.0;
		for (const FClipDriftSample& D : Run.Drift)
		{
			MaxPos = FMath::Max(MaxPos, D.PositionCm);
			MaxVel = FMath::Max(MaxVel, D.VelocityCms);
			if (FirstOver < 0.0 && (D.PositionCm > ThrPos || D.VelocityCms > ThrVel)) FirstOver = D.Frame;
		}
		Drift->SetNumberField(TEXT("frames_compared"), Run.Drift.Num());
		Drift->SetNumberField(TEXT("max_position_cm"), MaxPos);
		Drift->SetNumberField(TEXT("max_velocity_cms"), MaxVel);
		if (FirstOver >= 0.0) Drift->SetNumberField(TEXT("first_over_frame"), FirstOver);
		else Drift->SetField(TEXT("first_over_frame"), MakeShared<FJsonValueNull>());
		O->SetObjectField(TEXT("drift"), Drift);

		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FClipFiredEvent& E : Run.FiredEvents)
		{
			TSharedRef<FJsonObject> EO = MakeShared<FJsonObject>();
			EO->SetStringField(TEXT("id"), E.Id);
			EO->SetNumberField(TEXT("frame"), E.Frame);
			EO->SetNumberField(TEXT("game_ms"), E.GameMs);
			EO->SetBoolField(TEXT("ok"), E.bOk);
			if (!E.Error.IsEmpty()) EO->SetStringField(TEXT("error"), E.Error);
			Events.Add(MakeShared<FJsonValueObject>(EO));
		}
		O->SetArrayField(TEXT("events"), Events);

		if (Run.bRecording)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("take_id"), Run.TakeId);
			R->SetNumberField(TEXT("punch_in"), Run.PunchIn);
			if (Run.PunchOut >= 0.0) R->SetNumberField(TEXT("punch_out"), Run.PunchOut);
			R->SetBoolField(TEXT("punched_in"), Run.bPunchedIn);
			if (Run.PunchedOutAt >= 0.0) R->SetNumberField(TEXT("ended_at_frame"), Run.PunchedOutAt);
			TArray<TSharedPtr<FJsonValue>> Armed;
			for (const FString& A : Run.Armed) Armed.Add(MakeShared<FJsonValueString>(A));
			R->SetArrayField(TEXT("armed"), Armed);
			R->SetNumberField(TEXT("samples"), Run.Samples.Num());
			if (bLive && Run.bPunchedIn && !Run.bTakeSaved)
			{
				// The newest samples of the take in flight, so a client can draw notes and keys while the person plays.
				// Sample indices are absolute: a poller that keeps `from + frames.Num()` knows what it has already seen.
				constexpr int32 LiveWindow = 240;
				const int32 Total = Run.Samples.Num();
				const int32 From = FMath::Max(0, Total - LiveWindow);
				TSharedRef<FJsonObject> Live = MakeShared<FJsonObject>();
				Live->SetNumberField(TEXT("from"), From);
				TArray<TSharedPtr<FJsonValue>> Frames;
				for (int32 i = From; i < Total; ++i) Frames.Add(MakeShared<FJsonValueNumber>(Run.Samples[i].Frame));
				Live->SetArrayField(TEXT("frames"), Frames);
				TArray<TSharedPtr<FJsonValue>> Tracks;
				for (const FString& Path : Run.Armed)
				{
					const EClipValueType* Type = Run.ArmedTypes.Find(Path);
					if (!Type) continue;
					const int32 Dims = ClipValueDims(*Type);
					TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
					T->SetStringField(TEXT("action"), Path);
					T->SetStringField(TEXT("type"), ClipValueTypeToString(*Type));
					TArray<TSharedPtr<FJsonValue>> Values;
					for (int32 i = From; i < Total; ++i)
					{
						const FVector* V = Run.Samples[i].Values.Find(Path);
						const FVector Value = V ? *V : FVector::ZeroVector;
						if (Dims <= 1)
						{
							Values.Add(MakeShared<FJsonValueNumber>(Value.X));
							continue;
						}
						TArray<TSharedPtr<FJsonValue>> Vec;
						for (int32 d = 0; d < Dims; ++d) Vec.Add(MakeShared<FJsonValueNumber>(Value[d]));
						Values.Add(MakeShared<FJsonValueArray>(Vec));
					}
					T->SetArrayField(TEXT("values"), Values);
					Tracks.Add(MakeShared<FJsonValueObject>(T));
				}
				Live->SetArrayField(TEXT("tracks"), Tracks);
				R->SetObjectField(TEXT("live"), Live);
			}
			R->SetBoolField(TEXT("saved"), Run.bTakeSaved && Run.TakeError.IsEmpty());
			if (!Run.TakeError.IsEmpty()) R->SetStringField(TEXT("error"), Run.TakeError);
			O->SetObjectField(TEXT("take"), R);
		}
		O->SetBoolField(TEXT("live"), bLive);
		return O;
	}

	bool FPIEClipPlayer::WriteResults() const
	{
		IFileManager::Get().MakeDirectory(*Run.OutputDir, true);
		FString Json;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(RunToJson(false), W);
		bool bOk = FFileHelper::SaveStringToFile(Json, *Run.ResultsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		// The pawn's track, the drift reference a later run can name.
		FString Track = TEXT("[");
		for (int32 i = 0; i < Run.PawnTrack.Num(); ++i)
		{
			const FClipRun::FPawnSample& S = Run.PawnTrack[i];
			Track += FString::Printf(TEXT("%s[%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]"), i ? TEXT(",") : TEXT(""),
				S.Frame, S.Location.X, S.Location.Y, S.Location.Z, S.Velocity.X, S.Velocity.Y, S.Velocity.Z);
		}
		Track += TEXT("]");
		bOk &= FFileHelper::SaveStringToFile(Track, *(Run.OutputDir / TEXT("track.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return bOk;
	}
}
