#include "PIEObserver.h"
#include "MCPObservationProfile.h"
#include "PIESequenceFormat.h"
#include "UE_MCP_BridgeModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UEMCPPIE
{
	namespace
	{
		void SampleActors(UWorld* World,
		                  const TArray<FString>& Ids,
		                  TMap<FString, TWeakObjectPtr<AActor>>& Cache,
		                  FTrackedActorRow& OutRow)
		{
			for (const FString& Id : Ids)
			{
				FActorState AS;
				AActor* A = nullptr;
				if (TWeakObjectPtr<AActor>* Cached = Cache.Find(Id))
				{
					A = Cached->Get();
				}
				if (!A)
				{
					A = FindActorById(World, Id);
					if (A) Cache.Add(Id, A);
				}
				if (A)
				{
					AS.Location = A->GetActorLocation();
					AS.Rotation = A->GetActorRotation();
					AS.Velocity = A->GetVelocity();
					AS.bResolved = true;
				}
				OutRow.Actors.Add(Id, AS);
			}
		}
	}

	FPIEObserver& FPIEObserver::Get()
	{
		static FPIEObserver Instance;
		return Instance;
	}

	void FPIEObserver::Init()
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

	void FPIEObserver::Shutdown()
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
		State = EObserverState::Idle;
		bArmed = false;
	}

	bool FPIEObserver::Arm(const FObserverArmConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (State == EObserverState::Observing || State == EObserverState::WaitingForPawn)
		{
			OutError = TEXT("Observation already in flight; pie_observe_stop first.");
			return false;
		}

		UMCPObservationProfile* Profile = LoadObject<UMCPObservationProfile>(
			nullptr, *Cfg.ProfilePath);
		if (!Profile)
		{
			OutError = FString::Printf(TEXT("Profile not found: %s"), *Cfg.ProfilePath);
			return false;
		}

		Pending = Cfg;

		TrackedValuePaths.Reset();
		TrackedThresholds.Reset();
		for (const FMCPTrackedValueEntry& E : Profile->TrackedValues)
		{
			TrackedValuePaths.Add(E.Path);
			if (E.DriftThreshold > 0.f)
			{
				TrackedThresholds.Add(E.Path, E.DriftThreshold);
			}
		}

		TrackedActorIds.Reset();
		for (const FMCPTrackedActorEntry& E : Profile->TrackedActors)
		{
			TrackedActorIds.Add(E.ActorId);
		}

		bCapturePawnState = Profile->bCapturePawnState;
		bCaptureMontage = Profile->bCaptureMontage;
		ThrPosCm = Profile->PositionThresholdCm;
		ThrRotDeg = Profile->RotationThresholdDeg;
		ThrVelCms = Profile->VelocityThresholdCms;
		ThrTrackedDefault = Profile->TrackedValueDefaultThreshold;
		CurrentProfilePath = Cfg.ProfilePath;

		CurrentRunId = FString::Printf(TEXT("obs_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		const FString Root = Cfg.OutputDir.IsEmpty()
			? (FPaths::ProjectSavedDir() / TEXT("MCPObservations"))
			: Cfg.OutputDir;
		CurrentOutputDir = Root / CurrentRunId;

		ActorRows.Reset();
		ActorCache.Reset();
		CSVBody.Reset();
		FramesSampled = 0;

		bArmed = true;
		State = EObserverState::Armed;
		OutMessage = FString::Printf(TEXT("Armed: profile=%s run=%s values=%d actors=%d"),
			*FPaths::GetBaseFilename(Cfg.ProfilePath),
			*CurrentRunId,
			TrackedValuePaths.Num(),
			TrackedActorIds.Num());

		if (GEditor && GEditor->PlayWorld)
		{
			OnBeginPIE(false);
		}
		return true;
	}

	bool FPIEObserver::Disarm(FString& OutError)
	{
		if (State == EObserverState::Observing || State == EObserverState::WaitingForPawn)
		{
			OutError = TEXT("Observation is in flight; pie_observe_stop to finalize.");
			return false;
		}
		bArmed = false;
		State = EObserverState::Idle;
		return true;
	}

	void FPIEObserver::OnBeginPIE(bool /*bIsSimulating*/)
	{
		if (!bArmed) return;
		bArmed = false;

		FPIEFrameSampler::FConfig SC;
		SC.AxisThreshold = 0.15f;
		SC.bCapturePawnState = bCapturePawnState;
		SC.bCaptureMontage = bCaptureMontage;
		SC.TrackedValuePaths = TrackedValuePaths;
		SC.ClientIndex = Pending.ClientId;
		Sampler.Reset();
		Sampler.SetConfig(SC);

		State = EObserverState::WaitingForPawn;
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

		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-OBS] Armed -> BeginPIE: profile=%s run=%s"),
			*FPaths::GetBaseFilename(CurrentProfilePath), *CurrentRunId);
	}

	void FPIEObserver::OnEndFrame()
	{
		if (State == EObserverState::Idle || State == EObserverState::Completed) return;
		UWorld* PIEWorld = nullptr;
		if (GEditor) PIEWorld = GEditor->PlayWorld;
		if (!PIEWorld) return;

		if (State == EObserverState::WaitingForPawn)
		{
			if (Sampler.AttachToPIE(PIEWorld))
			{
				AttachTime = PIEWorld->GetTimeSeconds();

				CSVHdr = FCSVHeader();
				CSVHdr.RecordingId = CurrentRunId;
				CSVHdr.SampleHz = Pending.SampleHz > 0 ? Pending.SampleHz : 60;
				CSVHdr.Actions = Sampler.GetActions();
				CSVHdr.TrackedValues = Sampler.GetTrackedValues();
				CSVHeaderStr = BuildCSVHeader(CSVHdr);
				CSVBody.Reset();

				State = EObserverState::Observing;
				UE_LOG(LogMCPBridge, Log, TEXT("[PIE-OBS] Pawn attached, observing"));
			}
			return;
		}

		if (State == EObserverState::Observing)
		{
			const double GameTime = PIEWorld->GetTimeSeconds();
			const double Dt = PIEWorld->GetDeltaSeconds();
			const uint64 FrameNum = static_cast<uint64>(FramesSampled);
			FCSVRow Row = Sampler.SampleFrame(PIEWorld, FrameNum, GameTime, Dt);

			if (TrackedActorIds.Num() > 0)
			{
				FTrackedActorRow AR;
				AR.Frame = FrameNum;
				AR.Time = GameTime;
				SampleActors(PIEWorld, TrackedActorIds, ActorCache, AR);
				ActorRows.Add(MoveTemp(AR));
			}

			AppendCSVRow(CSVBody, Row, CSVHdr);
			FramesSampled++;
		}
	}

	void FPIEObserver::OnEndPIE(bool /*bIsSimulating*/)
	{
		if (State == EObserverState::Idle) return;
		FinaliseCurrent();
	}

	FObserverFinishResult FPIEObserver::FinaliseCurrent()
	{
		FObserverFinishResult R;
		if (State == EObserverState::Idle)
		{
			R.Error = TEXT("Not observing");
			return R;
		}

		R.RunId = CurrentRunId;
		R.OutputDir = CurrentOutputDir;
		R.FramesSampled = FramesSampled;

		if (FramesSampled == 0)
		{
			R.bSuccess = true;
			State = EObserverState::Idle;
			return R;
		}

		IFileManager::Get().MakeDirectory(*CurrentOutputDir, true);

		// Write observation CSV
		{
			const FString FullCSV = CSVHeaderStr + CSVBody;
			FString Err;
			if (SaveCSV(CurrentOutputDir / TEXT("observation.csv"), FullCSV, Err))
			{
				R.CSVPath = CurrentOutputDir / TEXT("observation.csv");
			}
			else
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[PIE-OBS] CSV write failed: %s"), *Err);
			}
		}

		// Write tracked actors
		if (ActorRows.Num() > 0)
		{
			FString Err;
			if (SaveTrackedActorsJSONL(CurrentOutputDir / TEXT("tracked.jsonl"), ActorRows, Err))
			{
				R.TrackedActorsPath = CurrentOutputDir / TEXT("tracked.jsonl");
			}
			else
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[PIE-OBS] tracked.jsonl write failed: %s"), *Err);
			}
		}

		// Write manifest
		{
			TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetNumberField(TEXT("version"), kFormatVersion);
			M->SetStringField(TEXT("type"), TEXT("observation"));
			M->SetStringField(TEXT("run_id"), CurrentRunId);
			M->SetStringField(TEXT("profile"), CurrentProfilePath);
			M->SetStringField(TEXT("started_at"), StartedAt);
			M->SetStringField(TEXT("ended_at"), ISOTimestampNow());
			M->SetNumberField(TEXT("frames_sampled"), FramesSampled);
			M->SetNumberField(TEXT("sample_hz"), CSVHdr.SampleHz);

			TArray<TSharedPtr<FJsonValue>> Vals;
			for (const FString& P : TrackedValuePaths)
			{
				Vals.Add(MakeShared<FJsonValueString>(P));
			}
			M->SetArrayField(TEXT("tracked_values"), Vals);

			TArray<TSharedPtr<FJsonValue>> Acts;
			for (const FString& A : TrackedActorIds)
			{
				Acts.Add(MakeShared<FJsonValueString>(A));
			}
			M->SetArrayField(TEXT("tracked_actors"), Acts);

			TSharedRef<FJsonObject> Thr = MakeShared<FJsonObject>();
			Thr->SetNumberField(TEXT("position_cm"), ThrPosCm);
			Thr->SetNumberField(TEXT("rotation_deg"), ThrRotDeg);
			Thr->SetNumberField(TEXT("velocity_cms"), ThrVelCms);
			Thr->SetNumberField(TEXT("tracked_default"), ThrTrackedDefault);
			if (TrackedThresholds.Num() > 0)
			{
				TSharedRef<FJsonObject> PerPath = MakeShared<FJsonObject>();
				for (const TPair<FString, float>& KV : TrackedThresholds)
				{
					PerPath->SetNumberField(KV.Key, KV.Value);
				}
				Thr->SetObjectField(TEXT("tracked"), PerPath);
			}
			M->SetObjectField(TEXT("thresholds"), Thr);

			FString JsonStr;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(M, W);
			FFileHelper::SaveStringToFile(JsonStr, *(CurrentOutputDir / TEXT("manifest.json")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		R.bSuccess = true;
		R.DurationSeconds = 0.0;

		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
			OnEndFrameHandle.Reset();
			bEndFrameBound = false;
		}
		State = EObserverState::Idle;

		UE_LOG(LogMCPBridge, Log, TEXT("[PIE-OBS] Finalized: %d frames -> %s"),
			FramesSampled, *CurrentOutputDir);
		return R;
	}

	FObserverFinishResult FPIEObserver::ForceStop()
	{
		return FinaliseCurrent();
	}

	FObserverStatus FPIEObserver::GetStatus() const
	{
		FObserverStatus S;
		S.State = State;
		S.RunId = CurrentRunId;
		S.ProfilePath = CurrentProfilePath;
		S.FramesSampled = FramesSampled;
		if (GEditor && GEditor->PlayWorld && AttachTime > 0.0)
		{
			S.ElapsedSeconds = GEditor->PlayWorld->GetTimeSeconds() - AttachTime;
		}
		return S;
	}
}
