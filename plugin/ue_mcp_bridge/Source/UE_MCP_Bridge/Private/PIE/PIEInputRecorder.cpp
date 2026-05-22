#include "PIEInputRecorder.h"
#include "UE_MCP_BridgeModule.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Math/UnrealMathUtility.h"
#include "Containers/StringConv.h"

namespace UEMCPPIE
{
	namespace
	{
		FString ISOTimestampNow()
		{
			// Approximate ISO 8601 UTC. UE's FDateTime has no timezone awareness,
			// so we treat it as local and append the local offset.
			return FDateTime::Now().ToString(TEXT("%Y-%m-%dT%H:%M:%S"));
		}
	}

	FPIEInputRecorder& FPIEInputRecorder::Get()
	{
		static FPIEInputRecorder Instance;
		return Instance;
	}

	void FPIEInputRecorder::Init()
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

	void FPIEInputRecorder::Shutdown()
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
		State = ERecorderState::Idle;
		bArmed = false;
		Rows.Reset();
		Markers.Reset();
	}

	bool FPIEInputRecorder::Arm(const FRecorderArmConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (State == ERecorderState::Recording || State == ERecorderState::WaitingForPawn)
		{
			OutError = TEXT("Recording already in flight; call pie_record_stop or wait for EndPIE.");
			return false;
		}

		Pending = Cfg;
		if (!Pending.bUserSuppliedSeed || Pending.RngSeed == 0)
		{
			Pending.RngSeed = FDateTime::Now().GetTicks() & 0x7FFFFFFF;
		}
		CurrentId = MakeRecordingId(Pending.Id);
		CurrentDir = MakeRecordingDir(Pending.RecordingsRoot, CurrentId);

		bArmed = true;
		State = ERecorderState::Armed;
		OutMessage = FString::Printf(TEXT("Armed: id=%s dir=%s seed=%lld"),
			*CurrentId, *CurrentDir, static_cast<long long>(Pending.RngSeed));

		// If PIE is already running, transition straight to WaitingForPawn so
		// the next end-of-frame begins sampling without a fresh BeginPIE.
		if (GEditor && GEditor->PlayWorld)
		{
			OnBeginPIE(false);
		}

		return true;
	}

	bool FPIEInputRecorder::Disarm(FString& OutError)
	{
		if (State == ERecorderState::Recording || State == ERecorderState::WaitingForPawn)
		{
			OutError = TEXT("Recording is in flight; pie_record_stop to finalize.");
			return false;
		}
		bArmed = false;
		State = ERecorderState::Idle;
		Pending = FRecorderArmConfig();
		CurrentId.Reset();
		CurrentDir.Reset();
		return true;
	}

	void FPIEInputRecorder::OnBeginPIE(bool /*bIsSimulating*/)
	{
		if (!bArmed) return;
		bArmed = false;

		FPIEFrameSampler::FConfig SC;
		SC.ActionPaths       = Pending.ActionPaths;
		SC.TrackedValuePaths = Pending.TrackedValuePaths;
		SC.AxisThreshold     = Pending.AxisThreshold;
		SC.bCapturePawnState = Pending.bCapturePawnState;
		SC.bCaptureMontage   = Pending.bCaptureMontage;
		Sampler.Reset();
		Sampler.SetConfig(SC);

		Rows.Reset();
		Markers.Reset();
		StartTime = FPlatformTime::Seconds();
		StartedAt = ISOTimestampNow();

		State = ERecorderState::WaitingForPawn;

		if (!bEndFrameBound)
		{
			OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([this]()
			{
				this->OnEndFrame();
			});
			bEndFrameBound = true;
		}

		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REC] Armed → BeginPIE: id=%s, waiting for pawn"), *CurrentId);
	}

	void FPIEInputRecorder::ApplyFPSPin(UWorld* PIEWorld)
	{
		if (Pending.PinFPS <= 0) return;
		if (!PIEWorld || !GEngine) return;
		const FString Cmd = FString::Printf(TEXT("t.MaxFPS %d"), Pending.PinFPS);
		GEngine->Exec(PIEWorld, *Cmd);
		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REC] %s"), *Cmd);
	}

	void FPIEInputRecorder::OnEndFrame()
	{
		if (State == ERecorderState::Idle) return;
		UWorld* PIEWorld = nullptr;
		if (GEditor) PIEWorld = GEditor->PlayWorld;
		if (!PIEWorld) return;

		if (State == ERecorderState::WaitingForPawn)
		{
			if (Sampler.AttachToPIE(PIEWorld))
			{
				// Apply RNG seed and FPS pin once attached.
				FMath::RandInit(static_cast<int32>(Pending.RngSeed));
				ApplyFPSPin(PIEWorld);

				CSVHdr = FCSVHeader();
				CSVHdr.RecordingId = CurrentId;
				CSVHdr.SampleHz = Pending.SampleHz;
				CSVHdr.RngSeed = Pending.RngSeed;
				CSVHdr.Actions = Sampler.GetActions();
				CSVHdr.TrackedValues = Sampler.GetTrackedValues();
				CSVHeader = BuildCSVHeader(CSVHdr);
				CSVBody.Reset();

				State = ERecorderState::Recording;
			}
			return;
		}

		if (State == ERecorderState::Recording)
		{
			const double GameTime = PIEWorld->GetTimeSeconds();
			const double Dt = PIEWorld->GetDeltaSeconds();
			const uint64 FrameNum = static_cast<uint64>(Rows.Num());
			FCSVRow Row = Sampler.SampleFrame(PIEWorld, FrameNum, GameTime, Dt);

			// Lift mark:* edge events into the manifest markers list.
			for (const FString& E : Row.EdgeEvents)
			{
				if (E.StartsWith(TEXT("mark:")))
				{
					FMarker M;
					M.Frame = FrameNum;
					M.Time = GameTime;
					M.Label = E.RightChop(5);
					Markers.Add(M);
				}
			}

			AppendCSVRow(CSVBody, Row, CSVHdr);
			Rows.Add(MoveTemp(Row));
		}
	}

	void FPIEInputRecorder::OnEndPIE(bool /*bIsSimulating*/)
	{
		if (State == ERecorderState::Idle) return;
		FinaliseCurrent();
	}

	void FPIEInputRecorder::BuildSequenceSteps(FSequence& Out) const
	{
		Out.Version = kFormatVersion;
		Out.SourceRecordingId = CurrentId;
		Out.SettleMs = 500;
		Out.SampleHz = Pending.SampleHz;
		Out.RngSeed = Pending.RngSeed;

		if (Rows.Num() == 0) return;
		const double BaseTime = Rows[0].Time;
		const int32 GapTol = FMath::Max(0, Pending.RunGapFrames);

		// Group rows by action.
		for (const FActionSpec& A : Sampler.GetActions())
		{
			if (A.ValueType == EActionValueType::Boolean)
			{
				// Press / release pairs from edge events.
				const FString PressEvt = A.Name + TEXT("_pressed");
				const FString ReleaseEvt = A.Name + TEXT("_released");
				for (int32 i = 0; i < Rows.Num(); ++i)
				{
					if (!Rows[i].EdgeEvents.Contains(PressEvt)) continue;
					int32 ReleaseIdx = -1;
					for (int32 j = i + 1; j < Rows.Num(); ++j)
					{
						if (Rows[j].EdgeEvents.Contains(ReleaseEvt)) { ReleaseIdx = j; break; }
					}
					double DurMs;
					if (ReleaseIdx >= 0)
					{
						DurMs = (Rows[ReleaseIdx].Time - Rows[i].Time) * 1000.0;
					}
					else
					{
						DurMs = 100.0;
					}
					const double FrameMs = 1000.0 / FMath::Max(1, Pending.SampleHz);
					if (DurMs < FrameMs) DurMs = FrameMs;

					FStep S;
					S.Type = EStepType::Hold;
					S.Action = A.Path;
					S.DelayMs = static_cast<int32>(FMath::RoundToInt((Rows[i].Time - BaseTime) * 1000.0));
					S.ValueX = 1.0;
					S.DurationMs = static_cast<int32>(FMath::RoundToInt(DurMs));
					Out.Steps.Add(S);
				}
			}
			else
			{
				// Axis: extract runs of activity with gap tolerance.
				TArray<int32> ActiveIdx;
				for (int32 i = 0; i < Rows.Num(); ++i)
				{
					const FVector* V = Rows[i].ActionValues.Find(A.Name);
					if (V && V->Size() > Pending.AxisThreshold) ActiveIdx.Add(i);
				}
				if (ActiveIdx.Num() == 0) continue;

				int32 RunStart = ActiveIdx[0];
				int32 RunEnd = RunStart;
				for (int32 k = 1; k < ActiveIdx.Num(); ++k)
				{
					const int32 Idx = ActiveIdx[k];
					if (Idx - RunEnd > GapTol)
					{
						FStep S;
						S.Type = EStepType::InputTape;
						S.Action = A.Path;
						S.DelayMs = static_cast<int32>(FMath::RoundToInt((Rows[RunStart].Time - BaseTime) * 1000.0));
						for (int32 i = RunStart; i <= RunEnd; ++i)
						{
							const FVector* V = Rows[i].ActionValues.Find(A.Name);
							S.TapeValues.Add(V ? *V : FVector::ZeroVector);
						}
						Out.Steps.Add(S);
						RunStart = Idx;
					}
					RunEnd = Idx;
				}
				// Tail run.
				FStep S;
				S.Type = EStepType::InputTape;
				S.Action = A.Path;
				S.DelayMs = static_cast<int32>(FMath::RoundToInt((Rows[RunStart].Time - BaseTime) * 1000.0));
				for (int32 i = RunStart; i <= RunEnd; ++i)
				{
					const FVector* V = Rows[i].ActionValues.Find(A.Name);
					S.TapeValues.Add(V ? *V : FVector::ZeroVector);
				}
				Out.Steps.Add(S);
			}
		}

		// Mark steps.
		for (const FMarker& M : Markers)
		{
			FStep S;
			S.Type = EStepType::Mark;
			S.DelayMs = static_cast<int32>(FMath::RoundToInt((M.Time - BaseTime) * 1000.0));
			S.Label = M.Label;
			Out.Steps.Add(S);
		}

		Out.Steps.Sort([](const FStep& A, const FStep& B) { return A.DelayMs < B.DelayMs; });
	}

	FRecorderFinishResult FPIEInputRecorder::FinaliseCurrent()
	{
		FRecorderFinishResult R;
		if (State == ERecorderState::Idle)
		{
			R.Error = TEXT("Not recording");
			return R;
		}
		const bool bHadData = Rows.Num() > 0 && Sampler.IsAttached();
		R.Id = CurrentId;
		R.RecordingDir = CurrentDir;

		// Even if zero frames were recorded (PIE ended before pawn appeared),
		// we still tear down state cleanly and report what happened.
		if (!bHadData)
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REC] EndPIE without samples (id=%s)"), *CurrentId);
			R.bSuccess = true;
			R.TotalFrames = 0;
			R.DurationSeconds = 0.0;

			if (bEndFrameBound && OnEndFrameHandle.IsValid())
			{
				FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
				OnEndFrameHandle.Reset();
				bEndFrameBound = false;
			}
			State = ERecorderState::Idle;
			CurrentId.Reset();
			CurrentDir.Reset();
			Rows.Reset();
			Markers.Reset();
			return R;
		}

		IFileManager::Get().MakeDirectory(*CurrentDir, true);

		const FString CSVPath = CurrentDir / TEXT("recording.csv");
		const FString SeqPath = CurrentDir / TEXT("sequence.json");
		const FString ManPath = CurrentDir / TEXT("manifest.json");

		FString WriteErr;
		const FString FullCSV = CSVHeader + CSVBody;
		if (!SaveCSV(CSVPath, FullCSV, WriteErr))
		{
			R.Error = WriteErr;
			State = ERecorderState::Idle;
			return R;
		}

		FSequence Seq;
		BuildSequenceSteps(Seq);
		if (!SaveSequence(SeqPath, Seq, WriteErr))
		{
			R.Error = WriteErr;
			State = ERecorderState::Idle;
			return R;
		}

		FManifest M;
		M.Id = CurrentId;
		M.StartedAt = StartedAt;
		M.EndedAt = ISOTimestampNow();
		M.DurationSeconds = Rows.Num() >= 2 ? (Rows.Last().Time - Rows[0].Time) : 0.0;
		M.TotalFrames = Rows.Num();
		M.SampleHz = Pending.SampleHz;
		M.PinMaxFPS = Pending.PinFPS;
		M.RngSeed = Pending.RngSeed;
		M.PIEWorld = Sampler.GetPIEWorldPath();
		M.PawnClass = Sampler.GetPawnClassPath();
		M.AxisThreshold = Pending.AxisThreshold;
		M.Actions = Sampler.GetActions();
		M.TrackedValues = Sampler.GetTrackedValues();
		M.Markers = Markers;
		M.CSVFile = TEXT("recording.csv");
		M.SequenceFile = TEXT("sequence.json");

		if (!SaveManifest(ManPath, M, WriteErr))
		{
			R.Error = WriteErr;
			State = ERecorderState::Idle;
			return R;
		}

		R.bSuccess = true;
		R.ManifestPath = ManPath;
		R.CSVPath = CSVPath;
		R.SequencePath = SeqPath;
		R.TotalFrames = M.TotalFrames;
		R.DurationSeconds = M.DurationSeconds;
		for (const FActionSpec& A : M.Actions)
		{
			R.DiscoveredActions.Add(FString::Printf(TEXT("%s (%s)"), *A.Name, *ActionValueTypeToString(A.ValueType)));
		}
		R.Markers = M.Markers;

		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REC] Recorded %d frames (%.2fs) → %s"),
			M.TotalFrames, M.DurationSeconds, *CurrentDir);

		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
			OnEndFrameHandle.Reset();
			bEndFrameBound = false;
		}
		State = ERecorderState::Idle;
		CurrentId.Reset();
		CurrentDir.Reset();
		Rows.Reset();
		Markers.Reset();
		return R;
	}

	FRecorderFinishResult FPIEInputRecorder::ForceStop()
	{
		return FinaliseCurrent();
	}

	FRecorderStatus FPIEInputRecorder::GetStatus() const
	{
		FRecorderStatus S;
		S.State = State;
		S.Id = CurrentId;
		S.RecordingDir = CurrentDir;
		S.CurrentFrame = Rows.Num();
		S.ElapsedSeconds = (Rows.Num() >= 2) ? (Rows.Last().Time - Rows[0].Time) : 0.0;
		S.TrackedActionCount = Sampler.GetActions().Num();
		return S;
	}

	bool FPIEInputRecorder::Mark(const FString& Label, FRecorderStatus& OutStatus)
	{
		OutStatus = GetStatus();
		if (State != ERecorderState::Recording) return false;
		Sampler.QueueMarker(Label);
		return true;
	}
}
