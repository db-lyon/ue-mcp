#include "PIEInputReplayer.h"
#include "PIEInputInjector.h"
#include "UE_MCP_BridgeModule.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EditorWorldUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Math/UnrealMathUtility.h"
#include "InputAction.h"
#include "UnrealClient.h"
#include "UObject/UObjectGlobals.h"

namespace UEMCPPIE
{
	namespace
	{
		FString ISOTimestampNow()
		{
			return FDateTime::Now().ToString(TEXT("%Y-%m-%dT%H:%M:%S"));
		}

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

		// Mirror of the recorder's actor lookup so replay can re-resolve the
		// same actor identifiers in the new PIE world.
		AActor* FindActorById(UWorld* World, const FString& Id)
		{
			if (!World || Id.IsEmpty()) return nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* A = *It;
				if (!A) continue;
				if (A->GetName() == Id) return A;
				const UClass* C = A->GetClass();
				if (C && (C->GetName() == Id || C->GetPathName() == Id)) return A;
				if (A->GetPathName().EndsWith(Id)) return A;
			}
			return nullptr;
		}
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
		ActiveHolds.Reset();
		DriftFrames.Reset();
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

	bool FPIEInputReplayer::Arm(const FReplayerArmConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (State == EReplayerState::Replaying || State == EReplayerState::WaitingForPawn)
		{
			OutError = TEXT("Replay already in flight; pie_replay_stop first.");
			return false;
		}

		Pending = Cfg;
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
		FramesCompared = 0;
		FramesMissingInReplay = 0;
		MaxTrackedDeltas.Reset();
		SourceTrackedPaths.Reset();
		SourceActorRows.Reset();
		SourceActorIds.Reset();
		ReplayActorCache.Reset();
		ActorDriftAccum.Reset();
		NextStepIndex = 0;
		ExecutedSteps = 0;
		ActiveHolds.Reset();

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
					UE_LOG(LogMCPBridge, Warning, TEXT("[PIE-REP] Drift disabled (%s)"), *Err);
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
						UE_LOG(LogMCPBridge, Warning, TEXT("[PIE-REP] tracked.jsonl read failed: %s"), *JErr);
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
		if (State == EReplayerState::Replaying || State == EReplayerState::WaitingForPawn)
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
		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REP] %s"), *Cmd);
	}

	void FPIEInputReplayer::OnBeginPIE(bool /*bIsSimulating*/)
	{
		if (!bArmed) return;
		bArmed = false;

		FPIEFrameSampler::FConfig SC;
		SC.AxisThreshold = 0.15f;
		SC.bCapturePawnState = true;
		SC.bCaptureMontage = true;
		// Replay drift compares tracked-value columns recovered from the
		// source CSV header. Telling the sampler about those paths makes
		// Row.TrackedValues populated on each sampled frame.
		SC.TrackedValuePaths = SourceTrackedPaths;
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

		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REP] Armed → BeginPIE, waiting for pawn (%d steps)"), ActiveSequence.Steps.Num());
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
					FPIEInputInjector::InjectOnce(Act, VectorToActionValue(Act->ValueType, FVector(S.ValueX, S.ValueY, S.ValueZ)), Err);
				}
				break;
			}
			case EStepType::Hold:
			{
				UInputAction* Act = LoadAction(S.Action);
				if (Act)
				{
					FString Err;
					const FString Id = FPIEInputInjector::StartHold(Act, VectorToActionValue(Act->ValueType, FVector(S.ValueX, S.ValueY, S.ValueZ)), FString(), Err);
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
					FPIEInputInjector::StartTape(Act, S.TapeValues, ActiveSequence.SampleHz, FString(), Err);
				}
				break;
			}
			case EStepType::Mark:
				Sampler.QueueMarker(S.Label);
				break;
			case EStepType::Console:
			{
				if (GEditor && GEditor->PlayWorld && GEngine && !S.Command.IsEmpty())
				{
					GEngine->Exec(GEditor->PlayWorld, *S.Command);
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
					Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MCPReplay");
				}
				IFileManager::Get().MakeDirectory(*Dir, true);
				const uint64 FrameIdx = static_cast<uint64>(FramesCompared);
				const FString Safe = S.CaptureName.IsEmpty() ? TEXT("capture") : S.CaptureName;
				const FString FullPath = Dir / FString::Printf(TEXT("%s_frame%05llu.png"), *Safe, FrameIdx);
				FScreenshotRequest::RequestScreenshot(FullPath, /*bShowUI*/false, /*bAddFilenameSuffix*/false);
				Sampler.QueueMarker(FString::Printf(TEXT("capture:%s:%s"), *Safe, *FullPath));
				UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REP] capture requested: %s"), *FullPath);
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
		UWorld* PIEWorld = GEditor ? GEditor->PlayWorld : nullptr;
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

			// Auto-stop when all steps consumed and all holds released. In
			// monitor mode the steps were never executed so we drive completion
			// purely off frame count against the source recording.
			const bool bStepsDone = Pending.bMonitor ? true : (NextStepIndex >= ActiveSequence.Steps.Num() && ActiveHolds.Num() == 0);
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

		R.bSuccess = true;
		R.ExecutedSteps = ExecutedSteps;

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
			FString Err;
			if (SaveDrift(CurrentDriftPath, D, Err))
			{
				R.DriftReportPath = CurrentDriftPath;
				R.Drift = D;
			}
			else
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[PIE-REP] drift write failed: %s"), *Err);
			}
		}

		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
			OnEndFrameHandle.Reset();
			bEndFrameBound = false;
		}
		State = EReplayerState::Idle;
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
		if (GEditor && GEditor->PlayWorld && AttachTime > 0.0)
		{
			S.ElapsedSeconds = GEditor->PlayWorld->GetTimeSeconds() - AttachTime;
		}
		S.MaxPositionDriftCm = MaxPosDriftCm;
		S.MaxVelocityDriftCms = MaxVelDriftCms;
		return S;
	}
}
