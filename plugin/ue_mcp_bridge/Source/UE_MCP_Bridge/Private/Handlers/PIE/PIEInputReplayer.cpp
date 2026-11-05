#include "Handlers/PIE/PIEInputReplayer.h"
#include "MCPPIEViewportCapture.h"
#include "Handlers/PIE/PIEGifEncoder.h"
#include "Handlers/PIE/PIEContactSheet.h"
#include "Handlers/PIE/PIESessionLog.h"
#include "MCPPIEInputInjector.h"
#include "Handlers/PIE/PIELog.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EditorWorldUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Math/UnrealMathUtility.h"
#include "InputAction.h"
#include "UnrealClient.h"
#include "UObject/UObjectGlobals.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/DebugCameraController.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "Async/Async.h"
#include "Handlers/PIE/PIEWorldResolver.h"

namespace MCPPIE
{
	namespace
	{
		UInputAction* LoadAction(const FString& Path)
		{
			if (Path.IsEmpty()) return nullptr;
			UInputAction* A = LoadObject<UInputAction>(nullptr, *Path);
			if (!A && !Path.Contains(TEXT(".")))
			{
				FString Name;
				Path.Split(TEXT("/"), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				A = LoadObject<UInputAction>(nullptr, *(Path + TEXT(".") + Name));
			}
			return A;
		}

		FInputActionValue VectorToActionValue(EInputActionValueType Type, const FVector& V)
		{
			switch (Type)
			{
			case EInputActionValueType::Boolean: return FInputActionValue(V.X != 0.0);
			case EInputActionValueType::Axis1D:  return FInputActionValue(static_cast<float>(V.X));
			case EInputActionValueType::Axis2D:  return FInputActionValue(FVector2D(V.X, V.Y));
			case EInputActionValueType::Axis3D:  return FInputActionValue(V);
			}
			return FInputActionValue();
		}

		// Strip anything that could escape the captures/ directory or break
		// the printf-built filename. Restricts to [A-Za-z0-9._-]; empty input
		// (or input that sanitizes to empty) becomes "capture".
		FString SanitizeCaptureName(const FString& In)
		{
			if (In.IsEmpty()) return TEXT("capture");
			FString Out;
			Out.Reserve(In.Len());
			for (TCHAR C : In)
			{
				if (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-') || C == TEXT('.'))
				{
					Out.AppendChar(C);
				}
				else
				{
					Out.AppendChar(TEXT('_'));
				}
			}
			Out.ReplaceInline(TEXT(".."), TEXT("__"));
			return Out.IsEmpty() ? FString(TEXT("capture")) : Out;
		}

		constexpr double ReplayerTargetResolveTimeoutSeconds = 10.0;
	}

	FPIEInputReplayer& FPIEInputReplayer::Get()
	{
		static FPIEInputReplayer Instance;
		return Instance;
	}

	void FPIEInputReplayer::Init()
	{
		if (BeginPIEHandle.IsValid()) return;
		BeginPIEHandle = FEditorDelegates::BeginPIE.AddLambda([this](bool bSim)
		{
			this->OnBeginPIE(bSim);
		});
		EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool bSim)
		{
			this->OnEndPIE(bSim);
		});
	}

	void FPIEInputReplayer::Shutdown()
	{
		if (BeginPIEHandle.IsValid()) FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
		if (EndPIEHandle.IsValid())   FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		BeginPIEHandle.Reset();
		EndPIEHandle.Reset();
		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
		}
		OnEndFrameHandle.Reset();
		bEndFrameBound = false;
		State = EReplayerState::Idle;
		bArmed = false;
		TargetWorld.Reset();
		TargetPlayerController.Reset();
		TargetLocalPlayer.Reset();
		ResolvedPIEInstance = INDEX_NONE;
		bTargetResolved = false;
		TargetResolveStartSeconds = 0.0;
		TargetResolveError.Reset();
		for (const FHoldHandle& H : ActiveHolds)
		{
			FPIEInputInjector::StopAny(H.InjectionId);
		}
		for (const FString& Id : ActiveTapes)
		{
			FPIEInputInjector::StopAny(Id);
		}
		ActiveHolds.Reset();
		ActiveTapes.Reset();
	}

	bool FPIEInputReplayer::LoadSourceFrames(const FString& CSVPath, FString& OutError)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *CSVPath))
		{
			OutError = FString::Printf(TEXT("Cannot read source CSV: %s"), *CSVPath);
			return false;
		}
		TArray<FString> Lines;
		Raw.ParseIntoArrayLines(Lines, false);
		if (Lines.Num() < 2)
		{
			OutError = TEXT("Source CSV has no rows");
			return false;
		}

		// Find header row (first non-# line).
		int32 HeaderIdx = 0;
		while (HeaderIdx < Lines.Num() && Lines[HeaderIdx].StartsWith(TEXT("#"))) HeaderIdx++;
		if (HeaderIdx >= Lines.Num()) return false;

		TArray<FString> Headers;
		Lines[HeaderIdx].ParseIntoArray(Headers, TEXT(","), false);
		auto FindCol = [&Headers](const TCHAR* Name) -> int32
		{
			for (int32 i = 0; i < Headers.Num(); ++i) if (Headers[i] == Name) return i;
			return -1;
		};

		const int32 ColFrame = FindCol(TEXT("frame"));
		const int32 ColTime  = FindCol(TEXT("time"));
		const int32 ColPx    = FindCol(TEXT("pos_x"));
		const int32 ColPy    = FindCol(TEXT("pos_y"));
		const int32 ColPz    = FindCol(TEXT("pos_z"));
		const int32 ColRy    = FindCol(TEXT("rot_yaw"));
		const int32 ColRp    = FindCol(TEXT("rot_pitch"));
		const int32 ColRr    = FindCol(TEXT("rot_roll"));
		const int32 ColVx    = FindCol(TEXT("vel_x"));
		const int32 ColVy    = FindCol(TEXT("vel_y"));
		const int32 ColVz    = FindCol(TEXT("vel_z"));
		const int32 ColS2    = FindCol(TEXT("speed2d"));
		const int32 ColMo    = FindCol(TEXT("montage"));

		// Tracked reflection paths live in columns prefixed with "t:". Map
		// each column index to the path so per-frame deltas can be computed.
		TArray<TPair<int32, FString>> TrackedCols;
		SourceTrackedPaths.Reset();
		for (int32 i = 0; i < Headers.Num(); ++i)
		{
			if (Headers[i].StartsWith(TEXT("t:")))
			{
				const FString Path = Headers[i].Mid(2);
				TrackedCols.Add(TPair<int32, FString>(i, Path));
				SourceTrackedPaths.Add(Path);
			}
		}

		auto ReadCol = [](const TArray<FString>& Cols, int32 Idx) -> double
		{
			if (Idx < 0 || Idx >= Cols.Num()) return 0.0;
			return FCString::Atod(*Cols[Idx]);
		};

		for (int32 i = HeaderIdx + 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty()) continue;
			TArray<FString> Cols;
			Lines[i].ParseIntoArray(Cols, TEXT(","), false);
			FSourceFrame F;
			F.Frame = static_cast<uint64>(ReadCol(Cols, ColFrame));
			F.Time = ReadCol(Cols, ColTime);
			F.PawnLocation = FVector(ReadCol(Cols, ColPx), ReadCol(Cols, ColPy), ReadCol(Cols, ColPz));
			F.PawnRotation = FRotator(ReadCol(Cols, ColRp), ReadCol(Cols, ColRy), ReadCol(Cols, ColRr));
			F.PawnVelocity = FVector(ReadCol(Cols, ColVx), ReadCol(Cols, ColVy), ReadCol(Cols, ColVz));
			F.Speed2D = static_cast<float>(ReadCol(Cols, ColS2));
			if (ColMo >= 0 && ColMo < Cols.Num()) F.MontageSection = Cols[ColMo];
			for (const TPair<int32, FString>& TC : TrackedCols)
			{
				F.TrackedValues.Add(TC.Value, ReadCol(Cols, TC.Key));
			}
			SourceFrames.Add(F);
		}
		return true;
	}

	void FPIEInputReplayer::TeleportPawnToStart(APawn* Pawn)
	{
		if (!Pawn || SourceFrames.Num() == 0) return;
		const FSourceFrame& F0 = SourceFrames[0];
		Pawn->SetActorLocationAndRotation(F0.PawnLocation, F0.PawnRotation, false, nullptr, ETeleportType::ResetPhysics);
		if (AController* C = Pawn->GetController())
		{
			C->SetControlRotation(F0.PawnRotation);
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Teleported pawn to frame 0: %s %s"),
			*F0.PawnLocation.ToString(), *F0.PawnRotation.ToString());
	}

	void FPIEInputReplayer::EjectPlayer(UWorld* PIEWorld)
	{
		if (bEjected || !PIEWorld) return;
		APlayerController* PC = TargetPlayerController.Get();
		if (!PC || PC->GetWorld() != PIEWorld)
		{
			return;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn) return;
		EjectedPC = PC;
		EjectedPawn = Pawn;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASpectatorPawn* Spectator = PIEWorld->SpawnActor<ASpectatorPawn>(
			Pawn->GetActorLocation() + FVector(0, 0, 300),
			Pawn->GetActorRotation(),
			SpawnParams);
		if (Spectator)
		{
			PC->UnPossess();
			PC->Possess(Spectator);
			bEjected = true;
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Ejected to spectator"));
		}
	}

	void FPIEInputReplayer::RepossessPlayer()
	{
		if (!bEjected) return;
		APlayerController* PC = EjectedPC.Get();
		APawn* Pawn = EjectedPawn.Get();
		if (PC && Pawn)
		{
			APawn* Spectator = PC->GetPawn();
			PC->UnPossess();
			PC->Possess(Pawn);
			if (Spectator && Spectator->IsA<ASpectatorPawn>())
			{
				Spectator->Destroy();
			}
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Re-possessed original pawn"));
		}
		bEjected = false;
		EjectedPC.Reset();
		EjectedPawn.Reset();
	}

	bool FPIEInputReplayer::Arm(const FReplayerArmConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (State == EReplayerState::Replaying
			|| State == EReplayerState::WaitingForPawn
			|| State == EReplayerState::Completed)
		{
			ForceStop();
		}

		Pending = Cfg;
		TargetWorld.Reset();
		TargetPlayerController.Reset();
		TargetLocalPlayer.Reset();
		ResolvedPIEInstance = INDEX_NONE;
		bTargetResolved = false;
		TargetResolveStartSeconds = 0.0;
		TargetResolveError.Reset();
		ActiveSequence = FSequence();
		SourceFrames.Reset();
		CurrentSourceCSV.Reset();
		CurrentDriftPath.Reset();
		DriftFrames.Reset();
		MaxPosDriftCm = 0.f;
		MaxPosDriftFrame = 0;
		MaxVelDriftCms = 0.f;
		MaxRotDriftDeg = 0.f;
		MontageMismatches = 0;
		FirstDivergence = FDriftDivergence();
		FramesCompared = 0;
		FramesMissingInReplay = 0;
		MaxTrackedDeltas.Reset();
		SourceTrackedPaths.Reset();
		SourceActorRows.Reset();
		SourceActorIds.Reset();
		ReplayActorCache.Reset();
		ActorDriftAccum.Reset();
		FramesCaptured = 0;
		CaptureFrameCounter = 0;
		CaptureDir.Reset();
		NextStepIndex = 0;
		ExecutedSteps = 0;
		for (const FHoldHandle& H : ActiveHolds)
		{
			FPIEInputInjector::StopAny(H.InjectionId);
		}
		for (const FString& Id : ActiveTapes)
		{
			FPIEInputInjector::StopAny(Id);
		}
		ActiveHolds.Reset();
		ActiveTapes.Reset();
		bEndPIERequested = false;

		FString Err;
		if (Cfg.bInlineSequenceProvided)
		{
			ActiveSequence = Cfg.InlineSequence;
		}
		else if (!Cfg.SequencePath.IsEmpty())
		{
			if (!LoadSequence(Cfg.SequencePath, ActiveSequence, Err))
			{
				OutError = Err;
				return false;
			}
		}
		else if (!Cfg.SourceRecordingId.IsEmpty())
		{
			const FString Dir = Cfg.SourceDir.IsEmpty()
				? (DefaultRecordingsRoot() / Cfg.SourceRecordingId)
				: Cfg.SourceDir;
			if (!LoadSequence(Dir / TEXT("sequence.json"), ActiveSequence, Err))
			{
				OutError = Err;
				return false;
			}
			if (Cfg.bRecordDrift)
			{
				CurrentSourceCSV = Dir / TEXT("recording.csv");
				CurrentDriftPath = Dir / TEXT("drift.json");
				if (!LoadSourceFrames(CurrentSourceCSV, Err))
				{
					// Non-fatal: just disable drift comparison this replay.
					UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-REP] Drift disabled (%s)"), *Err);
					SourceFrames.Reset();
				}
				// Tracked-actor sidecar is optional and silently disabled
				// when the recording didn't write one.
				const FString JsonlPath = Dir / TEXT("tracked.jsonl");
				if (FPaths::FileExists(JsonlPath))
				{
					FString JErr;
					if (!LoadTrackedActorsJSONL(JsonlPath, SourceActorRows, JErr))
					{
						UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-REP] tracked.jsonl read failed: %s"), *JErr);
						SourceActorRows.Reset();
					}
					else
					{
						FManifest M;
						FString MErr;
						if (LoadManifest(Dir / TEXT("manifest.json"), M, MErr))
						{
							SourceActorIds = M.TrackedActorIds;
						}
					}
				}
			}
		}
		else
		{
			OutError = TEXT("pie_replay_arm needs one of recording_id, sequence_path, or inline steps");
			return false;
		}

		bArmed = true;
		State = EReplayerState::Armed;
		OutMessage = FString::Printf(TEXT("Armed: source=%s steps=%d drift=%s"),
			*Pending.SourceRecordingId,
			ActiveSequence.Steps.Num(),
			SourceFrames.Num() > 0 ? TEXT("enabled") : TEXT("disabled"));

		if (GEditor && GEditor->PlayWorld)
		{
			OnBeginPIE(false);
		}
		return true;
	}

	bool FPIEInputReplayer::Disarm(FString& OutError)
	{
		if (State == EReplayerState::Completed)
		{
			FinaliseCurrent();
		}
		else if (State == EReplayerState::Replaying || State == EReplayerState::WaitingForPawn)
		{
			OutError = TEXT("Replay is in flight; pie_replay_stop to finalize.");
			return false;
		}
		bArmed = false;
		State = EReplayerState::Idle;
		return true;
	}

	void FPIEInputReplayer::ApplyFPSPin(UWorld* PIEWorld, int32 Hz)
	{
		if (!PIEWorld || !GEngine || Hz <= 0) return;
		const FString Cmd = FString::Printf(TEXT("t.MaxFPS %d"), Hz);
		GEngine->Exec(PIEWorld, *Cmd);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] %s"), *Cmd);

		// Item X2: opt-in fixed timestep for stronger reproducibility.
		if (Pending.bFixedTimestep)
		{
			ApplyFixedTimestep(Hz);
		}
	}

	void FPIEInputReplayer::ApplyFixedTimestep(int32 Hz)
	{
		if (bFixedTimestepApplied || Hz <= 0) return;
		bSavedUseFixedTimestep = FApp::UseFixedTimeStep();
		bSavedFixedDeltaTime = FApp::GetFixedDeltaTime();
		FApp::SetFixedDeltaTime(1.0 / static_cast<double>(Hz));
		FApp::SetUseFixedTimeStep(true);
		bFixedTimestepApplied = true;
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Fixed timestep on: %.4f s (%d Hz)"), 1.0 / Hz, Hz);
	}

	void FPIEInputReplayer::RestoreFixedTimestep()
	{
		if (!bFixedTimestepApplied) return;
		FApp::SetUseFixedTimeStep(bSavedUseFixedTimestep);
		FApp::SetFixedDeltaTime(bSavedFixedDeltaTime);
		bFixedTimestepApplied = false;
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Fixed timestep restored"));
	}

	void FPIEInputReplayer::OnBeginPIE(bool /*bIsSimulating*/)
	{
		if (!bArmed) return;
		bArmed = false;
		TargetWorld.Reset();
		TargetPlayerController.Reset();
		TargetLocalPlayer.Reset();
		ResolvedPIEInstance = INDEX_NONE;
		bTargetResolved = false;
		TargetResolveStartSeconds = FPlatformTime::Seconds();
		TargetResolveError.Reset();

		FPIEFrameSampler::FConfig SC;
		SC.AxisThreshold = 0.15f;
		SC.bCapturePawnState = true;
		SC.bCaptureMontage = true;
		SC.TrackedValuePaths = SourceTrackedPaths;
		SC.ClientIndex = Pending.ClientId;
		Sampler.Reset();
		Sampler.SetConfig(SC);

		State = EReplayerState::WaitingForPawn;
		AttachTime = 0.0;
		StartedAt = ISOTimestampNow();

		if (!bEndFrameBound)
		{
			OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([this]()
			{
				this->OnEndFrame();
			});
			bEndFrameBound = true;
		}

		if (Pending.CaptureFrameEvery > 0 && !ViewportCapture.IsValid())
		{
			ViewportCapture = FSceneViewExtensions::NewExtension<FPIEViewportCapture>();
			ViewportCapture->SetOutputFormat(/*bJpeg*/true, /*Quality*/80);
			ViewportCapture->SetEnabled(true);
		}

		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Armed → BeginPIE, waiting for pawn (%d steps)"), ActiveSequence.Steps.Num());
	}

	void FPIEInputReplayer::ExecutePendingSteps(double ElapsedMs)
	{
		while (NextStepIndex < ActiveSequence.Steps.Num())
		{
			const FStep& S = ActiveSequence.Steps[NextStepIndex];
			if (S.DelayMs > ElapsedMs) break;

			switch (S.Type)
			{
			case EStepType::Input:
			{
				UInputAction* Act = LoadAction(S.Action);
				if (Act)
				{
					FString Err;
					FPIEInputInjector::InjectOnce(Act,
						VectorToActionValue(Act->ValueType, FVector(S.ValueX, S.ValueY, S.ValueZ)),
						Err, Pending.ClientId, ResolvedPIEInstance, TargetLocalPlayer.Get());
				}
				break;
			}
			case EStepType::Hold:
			{
				UInputAction* Act = LoadAction(S.Action);
				if (Act)
				{
					FString Err;
					const FString Id = FPIEInputInjector::StartHold(Act,
						VectorToActionValue(Act->ValueType, FVector(S.ValueX, S.ValueY, S.ValueZ)),
						FString(), Err, Pending.ClientId, ResolvedPIEInstance, TargetLocalPlayer.Get());
					if (!Id.IsEmpty())
					{
						FHoldHandle H;
						H.StepIndex = NextStepIndex;
						H.StopAtMs = ElapsedMs + S.DurationMs;
						H.InjectionId = Id;
						ActiveHolds.Add(H);
					}
				}
				break;
			}
			case EStepType::InputTape:
			{
				UInputAction* Act = LoadAction(S.Action);
				if (Act)
				{
					FString Err;
					const FString Id = FPIEInputInjector::StartTape(Act, S.TapeValues,
						ActiveSequence.SampleHz, FString(), Err, Pending.ClientId,
						ResolvedPIEInstance, TargetLocalPlayer.Get());
					if (!Id.IsEmpty())
					{
						ActiveTapes.Add(Id);
					}
				}
				break;
			}
			case EStepType::Mark:
				Sampler.QueueMarker(S.Label);
				break;
			case EStepType::Console:
			{
				if (TargetWorld.IsValid() && GEngine && !S.Command.IsEmpty())
				{
					GEngine->Exec(TargetWorld.Get(), *S.Command);
				}
				break;
			}
			case EStepType::Capture:
			{
				// Request a viewport screenshot for the next renderable frame.
				// When replaying a known recording_id we land the file next to
				// drift.json (<recording_dir>/captures/), otherwise fall back
				// to Saved/Screenshots/MCPReplay/.
				FString Dir;
				if (!CurrentDriftPath.IsEmpty())
				{
					Dir = FPaths::GetPath(CurrentDriftPath) / TEXT("captures");
				}
				else
				{
					Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Screenshots") / TEXT("MCPReplay");
				}
				IFileManager::Get().MakeDirectory(*Dir, true);
				// Source-backed replays use the drift frame index so the
				// filename lines up with recording.csv. Inline-step replays
				// have no drift sampling (FramesCompared stays at 0), so
				// fall back to the step's own sequence index to avoid every
				// capture collapsing onto the same filename.
				const uint64 FrameIdx = (SourceFrames.Num() > 0)
					? static_cast<uint64>(FramesCompared)
					: static_cast<uint64>(NextStepIndex);
				const FString Safe = SanitizeCaptureName(S.CaptureName);
				const FString FullPath = Dir / FString::Printf(TEXT("%s_frame%05llu.png"), *Safe, FrameIdx);
				FScreenshotRequest::RequestScreenshot(FullPath, /*bShowUI*/false, /*bAddFilenameSuffix*/false);
				Sampler.QueueMarker(FString::Printf(TEXT("capture:%s:%s"), *Safe, *FullPath));
				UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] capture requested: %s"), *FullPath);
				break;
			}
			}
			NextStepIndex++;
			ExecutedSteps++;
		}

		// Stop holds whose duration has elapsed.
		for (int32 i = ActiveHolds.Num() - 1; i >= 0; --i)
		{
			if (ElapsedMs >= ActiveHolds[i].StopAtMs)
			{
				FPIEInputInjector::StopAny(ActiveHolds[i].InjectionId);
				ActiveHolds.RemoveAt(i);
			}
		}
	}

	void FPIEInputReplayer::OnEndFrame()
	{
		if (State == EReplayerState::Idle || State == EReplayerState::Completed) return;

		UWorld* PIEWorld = TargetWorld.Get();
		if (!bTargetResolved)
		{
			PIEWorldResolver::FPlayerTarget Target;
			FString Error;
			if (!PIEWorldResolver::ResolvePlayer(Pending.PIEInstance, Pending.ClientId, Target, Error))
			{
				TargetResolveError = Error;
				if (TargetResolveStartSeconds > 0.0
					&& FPlatformTime::Seconds() - TargetResolveStartSeconds >= ReplayerTargetResolveTimeoutSeconds)
				{
					if (Pending.bAutoStopPIE && !bEndPIERequested)
					{
						bEndPIERequested = true;
						if (GEditor)
						{
							UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Target resolution timed out; requesting PIE shutdown"));
							GEditor->RequestEndPlayMap();
						}
					}
					FinaliseCurrent();
				}
				return;
			}
			TargetWorld = Target.World;
			TargetPlayerController = Target.PlayerController;
			TargetLocalPlayer = Target.LocalPlayer;
			ResolvedPIEInstance = Target.PIEInstance;
			bTargetResolved = true;
			TargetResolveError.Reset();
			PIEWorld = Target.World;
		}

		if (!PIEWorld) return;

		if (State == EReplayerState::WaitingForPawn)
		{
			if (Sampler.AttachToPIE(PIEWorld))
			{
				if (Pending.bApplyRngSeed)
				{
					FMath::RandInit(static_cast<int32>(ActiveSequence.RngSeed));
				}
				const int32 Hz = (Pending.PinFPS == 0)
					? 0
					: (Pending.PinFPS > 0 ? Pending.PinFPS : ActiveSequence.SampleHz);
				ApplyFPSPin(PIEWorld, Hz);

				APlayerController* PC = TargetPlayerController.Get();
				if (PC && PC->GetWorld() == PIEWorld)
				{
					APawn* Pawn = PC->GetPawn();
					if (Pawn)
					{
						TeleportPawnToStart(Pawn);
					}
				}

				if (Pending.bEject)
				{
					EjectPlayer(PIEWorld);
				}

				if (Pending.TimeScale != 1.0f)
				{
					AWorldSettings* WS = PIEWorld->GetWorldSettings();
					if (WS)
					{
						WS->MaxGlobalTimeDilation = FMath::Max(WS->MaxGlobalTimeDilation, 1000.f);
						WS->MinGlobalTimeDilation = FMath::Min(WS->MinGlobalTimeDilation, 0.0001f);
						UGameplayStatics::SetGlobalTimeDilation(PIEWorld, Pending.TimeScale);
						UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Time scale: %.2f"), Pending.TimeScale);
					}
				}

				AttachTime = PIEWorld->GetTimeSeconds();
				State = EReplayerState::Replaying;
			}
			return;
		}

		if (State == EReplayerState::Replaying)
		{
			const double Now = PIEWorld->GetTimeSeconds();
			// Settle delay before step processing.
			const int32 Settle = (Pending.SettleMs >= 0) ? Pending.SettleMs : ActiveSequence.SettleMs;
			const double ElapsedMs = (Now - AttachTime) * 1000.0;
			if (ElapsedMs >= Settle && !Pending.bMonitor)
			{
				ExecutePendingSteps(ElapsedMs - Settle);
			}

			// Per-frame viewport capture (off by default). Counted off the
			// drift sampler's frame index so frame_<NNNNN>.png aligns with
			// recording.csv rows.
			if (Pending.CaptureFrameEvery > 0 && ViewportCapture.IsValid())
			{
				if (CaptureDir.IsEmpty())
				{
					if (!CurrentDriftPath.IsEmpty())
					{
						CaptureDir = FPaths::GetPath(CurrentDriftPath) / TEXT("frames");
					}
					else
					{
						CaptureDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Screenshots") / TEXT("MCPReplay");
					}
					IFileManager::Get().MakeDirectory(*CaptureDir, true);
				}
				FramesCaptured = ViewportCapture->GetCapturedCount();
				const uint64 FrameIdx = static_cast<uint64>(CaptureFrameCounter);
				if ((FrameIdx % static_cast<uint64>(Pending.CaptureFrameEvery)) == 0)
				{
					const FString FullPath = CaptureDir / FString::Printf(TEXT("frame_%05llu.jpg"), FrameIdx);
					ViewportCapture->RequestCapture(FullPath);
				}
				CaptureFrameCounter++;
			}

			// Drift sampling: sample current pawn state + compare to source by
			// frame index since attach.
			if (SourceFrames.Num() > 0)
			{
				const uint64 ReplayFrame = static_cast<uint64>(FramesCompared);
				if (ReplayFrame < static_cast<uint64>(SourceFrames.Num()))
				{
					FCSVRow Row = Sampler.SampleFrame(PIEWorld, ReplayFrame, Now, PIEWorld->GetDeltaSeconds());
					const FSourceFrame& Src = SourceFrames[static_cast<int32>(ReplayFrame)];
					FDriftFrameEntry E;
					E.Frame = ReplayFrame;
					E.PositionDeltaCm = static_cast<float>((Row.PawnLocation - Src.PawnLocation).Size());
					E.VelocityDeltaCms = static_cast<float>((Row.PawnVelocity - Src.PawnVelocity).Size());
					E.RotationDeltaDeg = static_cast<float>(FMath::Abs((Row.PawnRotation - Src.PawnRotation).Euler().Size()));
					if (E.PositionDeltaCm > MaxPosDriftCm) { MaxPosDriftCm = E.PositionDeltaCm; MaxPosDriftFrame = ReplayFrame; }
					if (E.VelocityDeltaCms > MaxVelDriftCms) MaxVelDriftCms = E.VelocityDeltaCms;
					if (E.RotationDeltaDeg > MaxRotDriftDeg) MaxRotDriftDeg = E.RotationDeltaDeg;
					if (Row.MontageSection != Src.MontageSection) MontageMismatches++;

					// Tracked-value drift: walk every path present in the source
					// row and accumulate max |delta|. Per-path threshold falls
					// back to ThrTrackedDefault; <= 0 disables tripping the
					// over-threshold list from tracked values.
					bool bTrackedOver = false;
					for (const TPair<FString, double>& KV : Src.TrackedValues)
					{
						const double* Cur = Row.TrackedValues.Find(KV.Key);
						const double Delta = FMath::Abs((Cur ? *Cur : 0.0) - KV.Value);
						const float DeltaF = static_cast<float>(Delta);
						float& Max = MaxTrackedDeltas.FindOrAdd(KV.Key, 0.f);
						if (DeltaF > Max) Max = DeltaF;
						const float* PerPath = Pending.TrackedThresholds.Find(KV.Key);
						const float Thr = PerPath ? *PerPath : Pending.ThrTrackedDefault;
						if (Thr > 0.f && DeltaF > Thr)
						{
							bTrackedOver = true;
						}
					}

					if (E.PositionDeltaCm > Pending.ThrPosCm ||
					    E.VelocityDeltaCms > Pending.ThrVelCms ||
					    E.RotationDeltaDeg > Pending.ThrRotDeg ||
					    bTrackedOver)
					{
						DriftFrames.Add(E);

						// First divergence (item 1c): pick the channel that crossed
						// by the largest ratio on this, the earliest, over-threshold
						// frame. Retain the exact source/replay scalars.
						if (!FirstDivergence.bFound)
						{
							const double PosR = (Pending.ThrPosCm > 0.f) ? E.PositionDeltaCm / Pending.ThrPosCm : 0.0;
							const double VelR = (Pending.ThrVelCms > 0.f) ? E.VelocityDeltaCms / Pending.ThrVelCms : 0.0;
							const double RotR = (Pending.ThrRotDeg > 0.f) ? E.RotationDeltaDeg / Pending.ThrRotDeg : 0.0;

							FDriftDivergence& Fd = FirstDivergence;
							Fd.bFound = true;
							Fd.Frame = ReplayFrame;
							Fd.Time = (ActiveSequence.SampleHz > 0)
								? static_cast<double>(ReplayFrame) / ActiveSequence.SampleHz
								: 0.0;

							if (VelR >= PosR && VelR >= RotR && VelR > 0.0)
							{
								Fd.Channel = TEXT("velocity");
								Fd.Delta = E.VelocityDeltaCms;
								Fd.Threshold = Pending.ThrVelCms;
								Fd.SourceValue = Src.PawnVelocity.Size();
								Fd.ReplayValue = Row.PawnVelocity.Size();
							}
							else if (RotR >= PosR && RotR > 0.0)
							{
								Fd.Channel = TEXT("rotation");
								Fd.Delta = E.RotationDeltaDeg;
								Fd.Threshold = Pending.ThrRotDeg;
								Fd.SourceValue = Src.PawnRotation.Euler().Size();
								Fd.ReplayValue = Row.PawnRotation.Euler().Size();
							}
							else if (PosR > 0.0)
							{
								Fd.Channel = TEXT("position");
								Fd.Delta = E.PositionDeltaCm;
								Fd.Threshold = Pending.ThrPosCm;
								Fd.SourceValue = Src.PawnLocation.Size();
								Fd.ReplayValue = Row.PawnLocation.Size();
							}
							else
							{
								// A tracked reflection value crossed first.
								Fd.Channel = TEXT("tracked");
								double BestRatio = 0.0;
								for (const TPair<FString, double>& KV : Src.TrackedValues)
								{
									const double* Cur = Row.TrackedValues.Find(KV.Key);
									const double Dl = FMath::Abs((Cur ? *Cur : 0.0) - KV.Value);
									const float* PerPath = Pending.TrackedThresholds.Find(KV.Key);
									const float Thr = PerPath ? *PerPath : Pending.ThrTrackedDefault;
									if (Thr > 0.f && Dl > Thr && (Dl / Thr) > BestRatio)
									{
										BestRatio = Dl / Thr;
										Fd.Channel = KV.Key;
										Fd.Delta = Dl;
										Fd.Threshold = Thr;
										Fd.SourceValue = KV.Value;
										Fd.ReplayValue = Cur ? *Cur : 0.0;
									}
								}
							}
						}
					}

					// Per-tracked-actor drift: walk the same frame index in
					// the source tracked.jsonl, re-resolve each id in the
					// replay world, and accumulate max |delta| per id.
					if (SourceActorRows.Num() > 0 && static_cast<int32>(ReplayFrame) < SourceActorRows.Num())
					{
						const FTrackedActorRow& SrcRow = SourceActorRows[static_cast<int32>(ReplayFrame)];
						for (const TPair<FString, FActorState>& KV : SrcRow.Actors)
						{
							FActorDrift& Acc = ActorDriftAccum.FindOrAdd(KV.Key);
							if (!KV.Value.bResolved)
							{
								Acc.FramesUnresolvedInSource++;
								continue;
							}
							AActor* A = nullptr;
							if (TWeakObjectPtr<AActor>* Cached = ReplayActorCache.Find(KV.Key))
							{
								A = Cached->Get();
							}
							if (!A)
							{
								A = FindActorById(PIEWorld, KV.Key);
								if (A) ReplayActorCache.Add(KV.Key, A);
							}
							if (!A)
							{
								Acc.FramesUnresolvedInReplay++;
								continue;
							}
							const float DPos = static_cast<float>((A->GetActorLocation() - KV.Value.Location).Size());
							const float DVel = static_cast<float>((A->GetVelocity() - KV.Value.Velocity).Size());
							const float DRot = static_cast<float>(FMath::Abs((A->GetActorRotation() - KV.Value.Rotation).Euler().Size()));
							if (DPos > Acc.MaxPositionCm) Acc.MaxPositionCm = DPos;
							if (DVel > Acc.MaxVelocityCms) Acc.MaxVelocityCms = DVel;
							if (DRot > Acc.MaxRotationDeg) Acc.MaxRotationDeg = DRot;
						}
					}
					FramesCompared++;
				}
				else
				{
					FramesMissingInReplay++;
				}
			}

			for (int32 i = ActiveTapes.Num() - 1; i >= 0; --i)
			{
				if (!FPIEInputInjector::IsActive(ActiveTapes[i]))
				{
					ActiveTapes.RemoveAt(i);
				}
			}

			// Auto-stop when all steps consumed and all holds released. In
			// monitor mode the steps were never executed so we drive completion
			// purely off frame count against the source recording.
			const bool bStepsDone = Pending.bMonitor ? true : (NextStepIndex >= ActiveSequence.Steps.Num() && ActiveHolds.Num() == 0 && ActiveTapes.Num() == 0);
			if (bStepsDone)
			{
				// Stay in Replaying for one more sample frame to let drift catch
				// any straggler frames the source had after the last step.
				if (SourceFrames.Num() == 0 || FramesCompared >= SourceFrames.Num())
				{
					State = EReplayerState::Completed;
				}
			}
		}

		// Unattended replay: once the run is Completed, end PIE ourselves so a
		// caller who never touches the editor (replay_run) gets a finalized
		// drift.json without a human clicking Stop. FinaliseCurrent runs from
		// the resulting EndPIE. Guarded so we ask exactly once.
		if (State == EReplayerState::Completed && Pending.bAutoStopPIE && !bEndPIERequested)
		{
			bEndPIERequested = true;
			if (GEditor)
			{
				UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Replay complete → auto-stopping PIE"));
				GEditor->RequestEndPlayMap();
			}
		}
	}

	void FPIEInputReplayer::OnEndPIE(bool /*bIsSimulating*/)
	{
		if (State == EReplayerState::Idle) return;
		FinaliseCurrent();
	}

	FReplayerFinishResult FPIEInputReplayer::FinaliseCurrent()
	{
		FReplayerFinishResult R;
		if (State == EReplayerState::Idle)
		{
			R.Error = TEXT("Not replaying");
			return R;
		}

		// Stop any holds that the sequence did not finish.
		for (const FHoldHandle& H : ActiveHolds)
		{
			FPIEInputInjector::StopAny(H.InjectionId);
		}
		ActiveHolds.Reset();
		for (const FString& Id : ActiveTapes)
		{
			FPIEInputInjector::StopAny(Id);
		}
		ActiveTapes.Reset();

		const bool bTargetFailed = !bTargetResolved && !TargetResolveError.IsEmpty();
		R.bSuccess = !bTargetFailed;
		if (bTargetFailed)
		{
			R.Error = TargetResolveError;
		}
		R.ExecutedSteps = ExecutedSteps;
		R.FramesCaptured = FramesCaptured;
		R.CaptureDir = CaptureDir;

		// Write drift.json when we had source frames to compare against.
		if (SourceFrames.Num() > 0 && !CurrentDriftPath.IsEmpty())
		{
			FDriftReport D;
			D.SourceRecordingId = Pending.SourceRecordingId.IsEmpty() ? ActiveSequence.SourceRecordingId : Pending.SourceRecordingId;
			D.ReplayStartedAt = StartedAt;
			D.FramesCompared = FramesCompared;
			D.FramesMissingInReplay = FramesMissingInReplay;
			D.MaxPositionDriftCm = MaxPosDriftCm;
			D.MaxPositionDriftFrame = MaxPosDriftFrame;
			D.MaxVelocityDriftCms = MaxVelDriftCms;
			D.MaxRotationDriftDeg = MaxRotDriftDeg;
			D.MontageSectionMismatches = MontageMismatches;
			D.TrackedValueMaxDeltas = MaxTrackedDeltas;
			D.ActorDrift = ActorDriftAccum;
			D.FramesOverThreshold = DriftFrames;

			// Item 1c: synthesise the lead the agent reads instead of the CSV.
			D.Summary.bValid = true;
			D.Summary.First = FirstDivergence;
			{
				TArray<TPair<FString, float>> Top;
				Top.Add(TPair<FString, float>(TEXT("position_cm"), MaxPosDriftCm));
				Top.Add(TPair<FString, float>(TEXT("velocity_cms"), MaxVelDriftCms));
				Top.Add(TPair<FString, float>(TEXT("rotation_deg"), MaxRotDriftDeg));
				for (const TPair<FString, float>& KV : MaxTrackedDeltas)
				{
					Top.Add(KV);
				}
				Top.Sort([](const TPair<FString, float>& A, const TPair<FString, float>& B) { return A.Value > B.Value; });
				if (Top.Num() > 5) Top.SetNum(5);
				D.Summary.TopChannels = MoveTemp(Top);
			}
			{
				FPIESessionLog& SLog = FPIESessionLog::Get();
				D.Summary.SessionDir = SLog.GetLastDir();
				const TSharedPtr<FJsonObject> ES = SLog.BuildErrorSummary(ELogVerbosity::Warning);
				if (ES.IsValid())
				{
					int32 EC = 0, WC = 0;
					ES->TryGetNumberField(TEXT("error_count"), EC);
					ES->TryGetNumberField(TEXT("warning_count"), WC);
					D.Summary.ErrorsDuringRun = EC;
					D.Summary.WarningsDuringRun = WC;
					const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
					if (ES->TryGetArrayField(TEXT("issues"), Issues) && Issues && Issues->Num() > 0)
					{
						const TSharedPtr<FJsonObject>& I0 = (*Issues)[0]->AsObject();
						if (I0.IsValid())
						{
							I0->TryGetStringField(TEXT("message"), D.Summary.TopError);
							double FF = 0;
							I0->TryGetNumberField(TEXT("first_frame"), FF);
							D.Summary.TopErrorFrame = static_cast<uint64>(FF);
						}
					}
				}
			}

			FString Err;
			if (SaveDrift(CurrentDriftPath, D, Err))
			{
				R.DriftReportPath = CurrentDriftPath;
				R.Drift = D;
			}
			else
			{
				UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-REP] drift write failed: %s"), *Err);
			}
		}

		RepossessPlayer();
		RestoreFixedTimestep();

		if (ViewportCapture.IsValid())
		{
			FramesCaptured = ViewportCapture->GetCapturedCount();
			R.FramesCaptured = FramesCaptured;
			ViewportCapture->SetEnabled(false);
			ViewportCapture.Reset();
		}

		if (!CaptureDir.IsEmpty() && FramesCaptured > 0)
		{
			TArray<FString> Frames;
			IFileManager::Get().FindFiles(Frames, *(CaptureDir / TEXT("frame_*.jpg")), true, false);
			Frames.Sort();
			for (FString& F : Frames) { F = CaptureDir / F; }

			if (Frames.Num() > 0)
			{
				R.FrameDir = CaptureDir;
				R.FrameCount = Frames.Num();

				const FString RecordingDir = FPaths::GetPath(CaptureDir);
				const FString CapturesDir = RecordingDir / TEXT("captures");
				IFileManager::Get().MakeDirectory(*CapturesDir, true);
				const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

				// Always compose a labeled contact sheet: one JPEG the agent can
				// read at a glance. Labels carry the frame index so a divergence
				// frame can be located visually. Frames are KEPT on disk (unlike
				// the old pipeline, which deleted them after GIF encoding).
				TArray<FString> Labels;
				Labels.Reserve(Frames.Num());
				for (int32 i = 0; i < Frames.Num(); ++i)
				{
					Labels.Add(FString::Printf(TEXT("F%d"), i));
				}
				const FString SheetPath = CapturesDir / FString::Printf(TEXT("contact_%s.jpg"), *Stamp);
				FContactSheetParams CP;
				CP.CellWidth = 300;
				CP.MaxCells = 25;
				if (ComposeContactSheet(Frames, SheetPath, CP, &Labels))
				{
					R.ContactSheetPath = SheetPath;
				}

				// GIF is now opt-in (capture_frame_every gives frames; encode_gif
				// asks for the animation on top). A vision model cannot parse GIF
				// animation, so it is off by default.
				if (Pending.bEncodeGif)
				{
					const FString GifPath = CapturesDir / FString::Printf(TEXT("replay_%s.gif"), *Stamp);
					FGifEncodeParams GP;
					GP.DelayCs = 3;
					GP.MaxWidth = 720;
					if (EncodeAnimatedGif(Frames, GifPath, GP))
					{
						R.GifPath = GifPath;
					}
				}
			}
		}

		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
			OnEndFrameHandle.Reset();
			bEndFrameBound = false;
		}
		TargetWorld.Reset();
		TargetPlayerController.Reset();
		TargetLocalPlayer.Reset();
		ResolvedPIEInstance = INDEX_NONE;
		bTargetResolved = false;
		TargetResolveStartSeconds = 0.0;
		TargetResolveError.Reset();
		State = EReplayerState::Idle;
		bEndPIERequested = false;
		// Retain the outcome so a poller (replay_status) can read the drift
		// report after we have dropped back to Idle.
		LastFinish = R;
		bHasLastFinish = true;
		return R;
	}

	FReplayerFinishResult FPIEInputReplayer::ForceStop()
	{
		return FinaliseCurrent();
	}

	FReplayerStatus FPIEInputReplayer::GetStatus() const
	{
		FReplayerStatus S;
		S.State = State;
		S.SourceRecordingId = Pending.SourceRecordingId;
		S.CurrentStep = NextStepIndex;
		S.TotalSteps = ActiveSequence.Steps.Num();
		S.ElapsedSeconds = 0.0;
		if (TargetWorld.IsValid() && AttachTime > 0.0)
		{
			S.ElapsedSeconds = TargetWorld->GetTimeSeconds() - AttachTime;
		}
		S.MaxPositionDriftCm = MaxPosDriftCm;
		S.MaxVelocityDriftCms = MaxVelDriftCms;
		S.FramesCaptured = FramesCaptured;
		bool bPIEActive = false;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				const bool bRuntimeWorld = Context.WorldType == EWorldType::PIE
					|| Context.WorldType == EWorldType::Game;
				if (bRuntimeWorld && Context.World())
				{
					bPIEActive = true;
					break;
				}
			}
		}
		S.bPIEActive = bPIEActive;
		S.LastError = TargetResolveError;
		if (S.LastError.IsEmpty() && State == EReplayerState::Idle && bHasLastFinish)
		{
			S.LastError = LastFinish.Error;
		}
		if (bHasLastFinish)
		{
			S.bHasLastResult = true;
			S.LastDriftReportPath = LastFinish.DriftReportPath;
			S.LastGifPath = LastFinish.GifPath;
			S.LastMaxPositionDriftCm = LastFinish.Drift.MaxPositionDriftCm;
			S.LastMaxVelocityDriftCms = LastFinish.Drift.MaxVelocityDriftCms;
			S.LastFramesCompared = LastFinish.Drift.FramesCompared;
			S.LastFrameDir = LastFinish.FrameDir;
			S.LastFrameCount = LastFinish.FrameCount;
			S.LastContactSheetPath = LastFinish.ContactSheetPath;
		}
		return S;
	}

	FLiveReplaySnapshot FPIEInputReplayer::GetLiveSnapshot() const
	{
		FLiveReplaySnapshot S;
		S.State = State;
		S.SourceRecordingId = Pending.SourceRecordingId;
		S.CurrentStep = NextStepIndex;
		S.TotalSteps = ActiveSequence.Steps.Num();
		S.FramesCompared = FramesCompared;
		S.MaxPositionDriftCm = MaxPosDriftCm;
		S.MaxVelocityDriftCms = MaxVelDriftCms;
		S.MaxRotationDriftDeg = MaxRotDriftDeg;
		S.MontageMismatches = MontageMismatches;
		S.MaxTrackedDeltas = MaxTrackedDeltas;
		if (TargetWorld.IsValid() && AttachTime > 0.0)
		{
			S.ElapsedSeconds = TargetWorld->GetTimeSeconds() - AttachTime;
		}
		return S;
	}
}
