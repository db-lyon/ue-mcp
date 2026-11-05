#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIEWorldResolver.h"
#include "Handlers/PIE/PIELog.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "HandlerClassResolve.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SWidget.h"
#include "UObject/UObjectHash.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace MCPPIE
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

		// Re-resolve tracked widgets every N sampled frames; visibility is read every frame.
		constexpr int32 ObserverWidgetResolveEveryFrames = 15;

		void ResolveTrackedWidgets(UWorld* World,
		                           const FString& Spec,
		                           TMap<FString, TWeakObjectPtr<UClass>>& ClassCache,
		                           TArray<TWeakObjectPtr<UWidget>>& OutCandidates)
		{
			OutCandidates.Reset();
			UClass* WidgetClass = nullptr;
			if (TWeakObjectPtr<UClass>* Cached = ClassCache.Find(Spec))
			{
				WidgetClass = Cached->Get();
			}
			if (!WidgetClass)
			{
				WidgetClass = MCPResolveClassOfType(Spec, UWidget::StaticClass(), /*bAllowLoad=*/false);
				if (WidgetClass) ClassCache.Add(Spec, WidgetClass);
			}

			TArray<UObject*> Objects;
			GetObjectsOfClass(WidgetClass ? WidgetClass : UWidget::StaticClass(), Objects, true, RF_ClassDefaultObject);
			for (UObject* Obj : Objects)
			{
				UWidget* W = Cast<UWidget>(Obj);
				if (!W || !IsValid(W) || W->GetWorld() != World) continue;
				if (!WidgetClass && !W->GetName().Equals(Spec, ESearchCase::IgnoreCase)) continue;
				OutCandidates.Add(W);
			}
		}

		void SampleTrackedWidgets(UWorld* World, FObservationSession& S, FTrackedWidgetRow& OutRow)
		{
			const bool bResolve = S.WidgetResolveCountdown <= 0;
			S.WidgetResolveCountdown = bResolve ? ObserverWidgetResolveEveryFrames : S.WidgetResolveCountdown - 1;
			for (const FString& Spec : S.TrackedWidgetSpecs)
			{
				TArray<TWeakObjectPtr<UWidget>>& Candidates = S.WidgetCache.FindOrAdd(Spec);
				if (bResolve)
				{
					ResolveTrackedWidgets(World, Spec, S.WidgetClassCache, Candidates);
				}
				bool bVisible = false;
				for (const TWeakObjectPtr<UWidget>& Weak : Candidates)
				{
					if (IsWidgetOnScreen(Weak.Get()))
					{
						bVisible = true;
						break;
					}
				}
				OutRow.Visible.Add(Spec, bVisible);
			}
		}

		bool SaveTrackedWidgetsJSONL(const FString& Path, const TArray<FTrackedWidgetRow>& Rows)
		{
			FString Out;
			for (const FTrackedWidgetRow& Row : Rows)
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("frame"), static_cast<double>(Row.Frame));
				O->SetNumberField(TEXT("time"), Row.Time);
				TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
				for (const TPair<FString, bool>& KV : Row.Visible)
				{
					W->SetBoolField(KV.Key, KV.Value);
				}
				O->SetObjectField(TEXT("widgets"), W);
				FString Line;
				TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
					TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
				FJsonSerializer::Serialize(O, Writer);
				Out += Line;
				Out += TEXT("\n");
			}
			return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		constexpr double ObserverTargetResolveTimeoutSeconds = 10.0;
	}

	bool IsWidgetOnScreen(const UWidget* Widget)
	{
		if (!Widget || !IsValid(Widget) || !Widget->GetCachedWidget().IsValid())
		{
			return false;
		}
		const UWidget* Cur = Widget;
		for (int32 Depth = 0; Cur && Depth < 128; ++Depth)
		{
			if (!Cur->IsVisible() || Cur->GetRenderOpacity() <= 0.f)
			{
				return false;
			}
			if (Cur->IsInViewport())
			{
				return true;
			}
			if (const UPanelWidget* Parent = Cur->GetParent())
			{
				Cur = Parent;
				continue;
			}
			// The root of a user widget's tree: continue from the user widget that owns it.
			const UUserWidget* Owner = Cur->GetTypedOuter<UUserWidget>();
			if (Owner && Owner != Cur)
			{
				Cur = Owner;
				continue;
			}
			const TSharedPtr<SWidget> Slate = Cur->GetCachedWidget();
			return Slate.IsValid() && Slate->GetParentWidget().IsValid();
		}
		return false;
	}

	bool FindTrackedValueInRow(const TMap<FString, double>& Values, const FString& Name, double& OutValue)
	{
		if (const double* Exact = Values.Find(Name))
		{
			OutValue = *Exact;
			return true;
		}
		for (const TPair<FString, double>& KV : Values)
		{
			if (TrackedValuePathMatches(KV.Key, Name))
			{
				OutValue = KV.Value;
				return true;
			}
		}
		return false;
	}

	bool TrackedValuePathMatches(const FString& Path, const FString& Name)
	{
		if (Path.Equals(Name, ESearchCase::IgnoreCase))
		{
			return true;
		}
		int32 Dot = INDEX_NONE;
		return Path.FindLastChar(TEXT('.'), Dot)
			&& Path.RightChop(Dot + 1).Equals(Name, ESearchCase::IgnoreCase);
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
		UnbindEndFrame();
		Sessions.Reset();
		LastError.Reset();
	}

	void FPIEObserver::BindEndFrame()
	{
		if (!bEndFrameBound)
		{
			OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([this]()
			{
				this->OnEndFrame();
			});
			bEndFrameBound = true;
		}
	}

	void FPIEObserver::UnbindEndFrame()
	{
		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
			OnEndFrameHandle.Reset();
			bEndFrameBound = false;
		}
	}

	bool FPIEObserver::Arm(const FObserverArmConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		const bool bHasExtras = Cfg.ExtraTrackedValues.Num() > 0
			|| Cfg.ExtraTrackedActors.Num() > 0
			|| Cfg.ExtraTrackedWidgets.Num() > 0;
		UMCPPIEObservationProfile* Profile = nullptr;
		if (!Cfg.ProfilePath.IsEmpty())
		{
			Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *Cfg.ProfilePath);
			if (!Profile)
			{
				OutError = FString::Printf(TEXT("Profile not found: %s"), *Cfg.ProfilePath);
				return false;
			}
		}
		else if (!bHasExtras && Cfg.RunId.IsEmpty())
		{
			OutError = TEXT("A profile is required unless extra tracked values, actors or widgets, or a run id, are given.");
			return false;
		}
		if (!Cfg.RunId.IsEmpty() && HasRun(Cfg.RunId))
		{
			OutError = FString::Printf(TEXT("Observation run already active: %s"), *Cfg.RunId);
			return false;
		}

		if (!IsActive())
		{
			LastError.Reset();
		}
		FObservationSession S;
		S.Config = Cfg;
		S.ProfilePath = Cfg.ProfilePath;

		if (Profile)
		{
			for (const FMCPPIETrackedValueEntry& E : Profile->TrackedValues)
			{
				S.TrackedValuePaths.Add(E.Path);
				if (E.DriftThreshold > 0.f)
				{
					S.TrackedThresholds.Add(E.Path, E.DriftThreshold);
				}
			}

			for (const FMCPPIETrackedActorEntry& E : Profile->TrackedActors)
			{
				S.TrackedActorIds.Add(E.ActorId);
			}

			for (const FMCPPIETrackedWidgetEntry& E : Profile->TrackedWidgets)
			{
				if (!E.Widget.IsEmpty()) S.TrackedWidgetSpecs.AddUnique(E.Widget);
			}

			S.bCapturePawnState = Profile->bCapturePawnState;
			S.bCaptureMontage = Profile->bCaptureMontage;
			S.ThrPosCm = Profile->PositionThresholdCm;
			S.ThrRotDeg = Profile->RotationThresholdDeg;
			S.ThrVelCms = Profile->VelocityThresholdCms;
			S.ThrTrackedDefault = Profile->TrackedValueDefaultThreshold;
		}

		for (const FString& V : Cfg.ExtraTrackedValues)
		{
			if (!V.IsEmpty()) S.TrackedValuePaths.AddUnique(V);
		}
		for (const FString& A : Cfg.ExtraTrackedActors)
		{
			if (!A.IsEmpty()) S.TrackedActorIds.AddUnique(A);
		}
		for (const FString& W : Cfg.ExtraTrackedWidgets)
		{
			if (!W.IsEmpty()) S.TrackedWidgetSpecs.AddUnique(W);
		}

		S.RunId = !Cfg.RunId.IsEmpty()
			? Cfg.RunId
			: FString::Printf(TEXT("obs_%s_%s"),
				Cfg.ProfilePath.IsEmpty() ? TEXT("adhoc") : *FPaths::GetBaseFilename(Cfg.ProfilePath),
				*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		const FString Root = Cfg.OutputDir.IsEmpty()
			? (FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPObservations"))
			: Cfg.OutputDir;
		S.OutputDir = Root / S.RunId;

		S.State = EObserverState::Armed;

		OutMessage = FString::Printf(TEXT("Armed: profile=%s run=%s values=%d actors=%d widgets=%d"),
			Cfg.ProfilePath.IsEmpty() ? TEXT("(none)") : *FPaths::GetBaseFilename(Cfg.ProfilePath),
			*S.RunId,
			S.TrackedValuePaths.Num(),
			S.TrackedActorIds.Num(),
			S.TrackedWidgetSpecs.Num());

		Sessions.Add(MoveTemp(S));

		if (GEditor && GEditor->PlayWorld)
		{
			OnBeginPIE(false);
		}
		return true;
	}

	bool FPIEObserver::Disarm(FString& OutError)
	{
		int32 Removed = Sessions.RemoveAll([](const FObservationSession& S)
		{
			return S.State == EObserverState::Armed;
		});
		if (Removed == 0)
		{
			OutError = TEXT("No armed sessions to disarm.");
			return false;
		}
		return true;
	}

	void FPIEObserver::OnBeginPIE(bool /*bIsSimulating*/)
	{
		bool bAnyStarted = false;
		for (FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Armed)
			{
				continue;
			}
			S.TargetWorld.Reset();
			S.ResolvedPIEInstance = INDEX_NONE;
			S.bTargetResolved = false;
			S.TargetResolveStartSeconds = FPlatformTime::Seconds();
			S.TargetResolveError.Reset();
			FPIEFrameSampler::FConfig SC;
			SC.AxisThreshold = 0.15f;
			SC.bCapturePawnState = S.bCapturePawnState;
			SC.bCaptureMontage = S.bCaptureMontage;
			SC.TrackedValuePaths = S.TrackedValuePaths;
			SC.ClientIndex = S.Config.ClientId;
			S.Sampler.Reset();
			S.Sampler.SetConfig(SC);

			S.State = EObserverState::WaitingForPawn;
			S.AttachTime = 0.0;
			S.StartedAt = ISOTimestampNow();

			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-OBS] Armed -> BeginPIE: profile=%s run=%s"),
				*FPaths::GetBaseFilename(S.ProfilePath), *S.RunId);
			bAnyStarted = true;
		}

		if (bAnyStarted)
		{
			BindEndFrame();
		}
	}

	void FPIEObserver::OnEndFrame()
	{
		for (FObservationSession& S : Sessions)
		{
			if (S.State == EObserverState::Idle || S.State == EObserverState::Completed)
			{
				continue;
			}
			UWorld* PIEWorld = S.TargetWorld.Get();
			if (!S.bTargetResolved)
			{
				PIEWorldResolver::FPlayerTarget Target;
				FString Error;
				if (!PIEWorldResolver::ResolvePlayer(
					S.Config.PIEInstance, S.Config.ClientId, Target, Error))
				{
					S.TargetResolveError = Error;
					if (S.TargetResolveStartSeconds > 0.0
						&& FPlatformTime::Seconds() - S.TargetResolveStartSeconds >= ObserverTargetResolveTimeoutSeconds)
					{
						const FObserverFinishResult Finish = FinaliseSession(S);
						if (!Finish.Error.IsEmpty())
						{
							LastError = Finish.Error;
						}
					}
					continue;
				}
				S.TargetWorld = Target.World;
				S.ResolvedPIEInstance = Target.PIEInstance;
				S.bTargetResolved = true;
				S.TargetResolveError.Reset();
				PIEWorld = Target.World;
			}

			if (!PIEWorld)
			{
				continue;
			}

			if (S.State == EObserverState::WaitingForPawn)
			{
				if (S.Sampler.AttachToPIE(PIEWorld))
				{
					S.AttachTime = PIEWorld->GetTimeSeconds();

					S.CSVHdr = FCSVHeader();
					S.CSVHdr.RecordingId = S.RunId;
					S.CSVHdr.SampleHz = S.Config.SampleHz > 0 ? S.Config.SampleHz : 60;
					S.CSVHdr.Actions = S.Sampler.GetActions();
					S.CSVHdr.TrackedValues = S.Sampler.GetTrackedValues();
					S.CSVHeaderStr = BuildCSVHeader(S.CSVHdr);
					S.CSVBody.Reset();

					S.State = EObserverState::Observing;
					UE_LOG(LogMCPPIE, Log, TEXT("[PIE-OBS] Pawn attached, observing: %s"), *S.RunId);
				}
				continue;
			}

			if (S.State == EObserverState::Observing)
			{
				const double GameTime = PIEWorld->GetTimeSeconds();
				const double Dt = PIEWorld->GetDeltaSeconds();
				const uint64 FrameNum = static_cast<uint64>(S.FramesSampled);

				S.PrevRow = S.LastRow;
				FCSVRow Row = S.Sampler.SampleFrame(PIEWorld, FrameNum, GameTime, Dt);
				S.LastRow = Row;

				if (S.TrackedActorIds.Num() > 0)
				{
					FTrackedActorRow AR;
					AR.Frame = FrameNum;
					AR.Time = GameTime;
					SampleActors(PIEWorld, S.TrackedActorIds, S.ActorCache, AR);
					S.LastActorRow = AR;
					S.ActorRows.Add(MoveTemp(AR));
				}

				if (S.TrackedWidgetSpecs.Num() > 0)
				{
					FTrackedWidgetRow WR;
					WR.Frame = FrameNum;
					WR.Time = GameTime;
					SampleTrackedWidgets(PIEWorld, S, WR);
					S.LastWidgetRow = WR;
					S.WidgetRows.Add(MoveTemp(WR));
				}

				AppendCSVRow(S.CSVBody, Row, S.CSVHdr);
				S.FramesSampled++;
			}
		}
		if (!IsActive())
		{
			UnbindEndFrame();
		}
	}

	void FPIEObserver::OnEndPIE(bool /*bIsSimulating*/)
	{
		for (FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
			{
				FinaliseSession(S);
			}
		}
		Sessions.Reset();
		UnbindEndFrame();
	}

	FObserverFinishResult FPIEObserver::FinaliseSession(FObservationSession& S)
	{
		FObserverFinishResult R;
		R.RunId = S.RunId;
		R.OutputDir = S.OutputDir;
		R.FramesSampled = S.FramesSampled;

		if (S.FramesSampled == 0)
		{
			const bool bTargetFailed = !S.bTargetResolved && !S.TargetResolveError.IsEmpty();
			R.bSuccess = !bTargetFailed;
			if (bTargetFailed)
			{
				R.Error = S.TargetResolveError;
				LastError = R.Error;
			}
			S.State = EObserverState::Completed;
			return R;
		}

		IFileManager::Get().MakeDirectory(*S.OutputDir, true);

		{
			const FString FullCSV = S.CSVHeaderStr + S.CSVBody;
			FString Err;
			if (SaveCSV(S.OutputDir / TEXT("observation.csv"), FullCSV, Err))
			{
				R.CSVPath = S.OutputDir / TEXT("observation.csv");
			}
			else
			{
				UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-OBS] CSV write failed: %s"), *Err);
			}
		}

		if (S.ActorRows.Num() > 0)
		{
			FString Err;
			if (SaveTrackedActorsJSONL(S.OutputDir / TEXT("tracked.jsonl"), S.ActorRows, Err))
			{
				R.TrackedActorsPath = S.OutputDir / TEXT("tracked.jsonl");
			}
			else
			{
				UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-OBS] tracked.jsonl write failed: %s"), *Err);
			}
		}

		if (S.WidgetRows.Num() > 0)
		{
			if (SaveTrackedWidgetsJSONL(S.OutputDir / TEXT("widgets.jsonl"), S.WidgetRows))
			{
				R.TrackedWidgetsPath = S.OutputDir / TEXT("widgets.jsonl");
			}
			else
			{
				UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-OBS] widgets.jsonl write failed: %s"), *S.OutputDir);
			}
		}

		{
			TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetNumberField(TEXT("version"), kFormatVersion);
			M->SetStringField(TEXT("type"), TEXT("observation"));
			M->SetStringField(TEXT("run_id"), S.RunId);
			M->SetStringField(TEXT("profile"), S.ProfilePath);
			M->SetStringField(TEXT("started_at"), S.StartedAt);
			M->SetStringField(TEXT("ended_at"), ISOTimestampNow());
			M->SetNumberField(TEXT("frames_sampled"), S.FramesSampled);
			M->SetNumberField(TEXT("sample_hz"), S.CSVHdr.SampleHz);
			if (S.ResolvedPIEInstance >= 0)
			{
				M->SetNumberField(TEXT("pie_instance"), S.ResolvedPIEInstance);
			}

			TArray<TSharedPtr<FJsonValue>> Vals;
			for (const FString& P : S.TrackedValuePaths)
			{
				Vals.Add(MakeShared<FJsonValueString>(P));
			}
			M->SetArrayField(TEXT("tracked_values"), Vals);

			TArray<TSharedPtr<FJsonValue>> Acts;
			for (const FString& A : S.TrackedActorIds)
			{
				Acts.Add(MakeShared<FJsonValueString>(A));
			}
			M->SetArrayField(TEXT("tracked_actors"), Acts);

			if (S.TrackedWidgetSpecs.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Wids;
				for (const FString& W : S.TrackedWidgetSpecs)
				{
					Wids.Add(MakeShared<FJsonValueString>(W));
				}
				M->SetArrayField(TEXT("tracked_widgets"), Wids);
			}

			TSharedRef<FJsonObject> Thr = MakeShared<FJsonObject>();
			Thr->SetNumberField(TEXT("position_cm"), S.ThrPosCm);
			Thr->SetNumberField(TEXT("rotation_deg"), S.ThrRotDeg);
			Thr->SetNumberField(TEXT("velocity_cms"), S.ThrVelCms);
			Thr->SetNumberField(TEXT("tracked_default"), S.ThrTrackedDefault);
			if (S.TrackedThresholds.Num() > 0)
			{
				TSharedRef<FJsonObject> PerPath = MakeShared<FJsonObject>();
				for (const TPair<FString, float>& KV : S.TrackedThresholds)
				{
					PerPath->SetNumberField(KV.Key, KV.Value);
				}
				Thr->SetObjectField(TEXT("tracked"), PerPath);
			}
			M->SetObjectField(TEXT("thresholds"), Thr);

			FString JsonStr;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(M, W);
			FFileHelper::SaveStringToFile(JsonStr, *(S.OutputDir / TEXT("manifest.json")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		R.bSuccess = true;
		S.State = EObserverState::Completed;

		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-OBS] Finalized: %s %d frames -> %s"),
			*FPaths::GetBaseFilename(S.ProfilePath), S.FramesSampled, *S.OutputDir);
		return R;
	}

	const FObservationSession* FPIEObserver::FindSession(const FString& RunId) const
	{
		for (const FObservationSession& S : Sessions)
		{
			if (S.RunId == RunId) return &S;
		}
		return nullptr;
	}

	bool FPIEObserver::HasRun(const FString& RunId) const
	{
		const FObservationSession* S = FindSession(RunId);
		return S && S->State != EObserverState::Idle && S->State != EObserverState::Completed;
	}

	bool FPIEObserver::GetRunClock(const FString& RunId, FObserverRunClock& Out) const
	{
		const FObservationSession* S = FindSession(RunId);
		if (!S) return false;
		Out.State = S->State;
		Out.FramesSampled = S->FramesSampled;
		Out.LastFrame = S->LastRow.Frame;
		Out.ElapsedSeconds = 0.0;
		if (S->State == EObserverState::Observing && S->FramesSampled > 0)
		{
			Out.ElapsedSeconds = FMath::Max(0.0, S->LastRow.Time - S->AttachTime);
		}
		return true;
	}

	bool FPIEObserver::ReadTrackedValue(const FString& RunId, const FString& Name, double& OutValue) const
	{
		const FObservationSession* S = FindSession(RunId);
		if (!S || S->State != EObserverState::Observing || S->FramesSampled == 0) return false;
		return FindTrackedValueInRow(S->LastRow.TrackedValues, Name, OutValue);
	}

	bool FPIEObserver::ReadActorResolved(const FString& RunId, const FString& ActorId, bool& OutResolved) const
	{
		const FObservationSession* S = FindSession(RunId);
		if (!S || S->State != EObserverState::Observing || S->FramesSampled == 0) return false;
		for (const TPair<FString, FActorState>& KV : S->LastActorRow.Actors)
		{
			if (KV.Key.Equals(ActorId, ESearchCase::IgnoreCase))
			{
				OutResolved = KV.Value.bResolved;
				return true;
			}
		}
		return false;
	}

	bool FPIEObserver::ReadWidgetVisible(const FString& RunId, const FString& Widget, bool& OutVisible) const
	{
		const FObservationSession* S = FindSession(RunId);
		if (!S || S->State != EObserverState::Observing || S->FramesSampled == 0) return false;
		for (const TPair<FString, bool>& KV : S->LastWidgetRow.Visible)
		{
			if (KV.Key.Equals(Widget, ESearchCase::IgnoreCase))
			{
				OutVisible = KV.Value;
				return true;
			}
		}
		return false;
	}

	FObserverFinishResult FPIEObserver::StopRun(const FString& RunId)
	{
		FObserverFinishResult Result;
		const int32 Index = Sessions.IndexOfByPredicate([&RunId](const FObservationSession& S)
		{
			return S.RunId == RunId;
		});
		if (Index == INDEX_NONE)
		{
			Result.Error = FString::Printf(TEXT("No observation run %s"), *RunId);
			return Result;
		}
		FObservationSession& S = Sessions[Index];
		if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
		{
			Result = FinaliseSession(S);
		}
		else
		{
			Result.bSuccess = true;
			Result.RunId = S.RunId;
			Result.OutputDir = S.OutputDir;
		}
		Sessions.RemoveAt(Index);
		if (!IsActive())
		{
			UnbindEndFrame();
		}
		return Result;
	}

	FObserverFinishResult FPIEObserver::ForceStop()
	{
		FObserverFinishResult LastResult;
		for (FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
			{
				LastResult = FinaliseSession(S);
			}
		}
		Sessions.Reset();
		UnbindEndFrame();
		return LastResult;
	}

	FObserverStatus FPIEObserver::GetStatus() const
	{
		FObserverStatus Out;
		Out.State = EObserverState::Idle;

		for (const FObservationSession& S : Sessions)
		{
			if (S.State == EObserverState::Observing)
			{
				Out.State = EObserverState::Observing;
				Out.RunId = S.RunId;
				Out.ProfilePath = S.ProfilePath;
				Out.FramesSampled += S.FramesSampled;
				if (S.TargetWorld.IsValid() && S.AttachTime > 0.0)
				{
					Out.ElapsedSeconds = FMath::Max(Out.ElapsedSeconds,
						S.TargetWorld->GetTimeSeconds() - S.AttachTime);
				}
			}
			else if (S.State == EObserverState::WaitingForPawn && Out.State == EObserverState::Idle)
			{
				Out.State = EObserverState::WaitingForPawn;
			}
			else if (S.State == EObserverState::Armed && Out.State == EObserverState::Idle)
			{
				Out.State = EObserverState::Armed;
			}
		}

		if (Out.State == EObserverState::Observing && Sessions.Num() > 1)
		{
			Out.RunId = FString::Printf(TEXT("%d sessions"), Sessions.Num());
		}
		Out.LastError = LastError;

		return Out;
	}

	bool FPIEObserver::IsActive() const
	{
		for (const FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
			{
				return true;
			}
		}
		return false;
	}

	TArray<FLiveObservationSnapshot> FPIEObserver::GetLiveSnapshots() const
	{
		TArray<FLiveObservationSnapshot> Out;
		for (const FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Observing) continue;

			FLiveObservationSnapshot Snap;
			Snap.ProfileName = FPaths::GetBaseFilename(S.ProfilePath);
			Snap.RunId = S.RunId;
			Snap.State = S.State;
			Snap.FramesSampled = S.FramesSampled;
			Snap.LastRow = S.LastRow;
			Snap.PrevRow = S.PrevRow;
			Snap.TrackedActorIds = S.TrackedActorIds;
			Snap.LastActorRow = S.LastActorRow;

			if (S.TargetWorld.IsValid() && S.AttachTime > 0.0)
			{
				Snap.ElapsedSeconds = S.TargetWorld->GetTimeSeconds() - S.AttachTime;
			}
			Out.Add(MoveTemp(Snap));
		}
		return Out;
	}
}
