#pragma once

#include "CoreMinimal.h"
#include "Handlers/PIE/PIEFrameSampler.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UMCPPIEObservationProfile;
class UWorld;
class AActor;
class UWidget;
class UClass;

namespace MCPPIE
{
	enum class EObserverState : uint8
	{
		Idle,
		Armed,
		WaitingForPawn,
		Observing,
		Completed
	};

	struct FObserverArmConfig
	{
		FString ProfilePath;
		FString OutputDir;
		int32 SampleHz = 60;
		int32 PinFPS = -1;
		// Local-player index inside selected PIEInstance.
		int32 ClientId = 0;
		int32 PIEInstance = INDEX_NONE;
		// Observed on top of the profile. With any of these set the profile is optional.
		TArray<FString> ExtraTrackedValues;
		TArray<FString> ExtraTrackedActors;
		TArray<FString> ExtraTrackedWidgets;
		// Run id to use instead of obs_<profile>_<stamp>; also names the output folder.
		FString RunId;
	};

	// One frame of widget visibility, keyed by the tracked widget spec.
	struct FTrackedWidgetRow
	{
		uint64 Frame = 0;
		double Time = 0.0;
		TMap<FString, bool> Visible;
	};

	// Latest values of one observation run, for the script runner.
	struct FObserverRunClock
	{
		EObserverState State = EObserverState::Idle;
		int32 FramesSampled = 0;
		uint64 LastFrame = 0;
		// Game seconds since the pawn attached (the run's first observed frame).
		double ElapsedSeconds = 0.0;
	};

	// True when the widget and every ancestor are visible and it is rooted in the viewport or a Slate parent.
	bool IsWidgetOnScreen(const UWidget* Widget);

	// Tracked value lookup on a sampled row: exact path first, then a case-insensitive
	// match on the path or on its last dotted segment.
	bool FindTrackedValueInRow(const TMap<FString, double>& Values, const FString& Name, double& OutValue);

	// Name equals Path, or Path's last dotted segment, ignoring case.
	bool TrackedValuePathMatches(const FString& Path, const FString& Name);

	struct FObserverStatus
	{
		EObserverState State = EObserverState::Idle;
		FString RunId;
		FString ProfilePath;
		int32 FramesSampled = 0;
		double ElapsedSeconds = 0.0;
		FString LastError;
	};

	struct FObserverFinishResult
	{
		bool bSuccess = false;
		FString Error;
		FString RunId;
		FString OutputDir;
		FString CSVPath;
		FString TrackedActorsPath;
		FString TrackedWidgetsPath;
		int32 FramesSampled = 0;
		double DurationSeconds = 0.0;
	};

	struct FObservationSession
	{
		FObserverArmConfig Config;
		EObserverState State = EObserverState::Armed;
		FString RunId;
		FString OutputDir;
		FString ProfilePath;

		TWeakObjectPtr<UWorld> TargetWorld;
		int32 ResolvedPIEInstance = INDEX_NONE;
		bool bTargetResolved = false;
		double TargetResolveStartSeconds = 0.0;
		FString TargetResolveError;
		FPIEFrameSampler Sampler;
		FCSVHeader CSVHdr;
		FString CSVHeaderStr;
		FString CSVBody;

		TArray<FString> TrackedValuePaths;
		TArray<FString> TrackedActorIds;
		TArray<FTrackedActorRow> ActorRows;
		TMap<FString, TWeakObjectPtr<AActor>> ActorCache;

		TArray<FString> TrackedWidgetSpecs;
		TArray<FTrackedWidgetRow> WidgetRows;
		TMap<FString, TArray<TWeakObjectPtr<UWidget>>> WidgetCache;
		TMap<FString, TWeakObjectPtr<UClass>> WidgetClassCache;
		int32 WidgetResolveCountdown = 0;

		double AttachTime = 0.0;
		int32 FramesSampled = 0;
		FString StartedAt;

		bool bCapturePawnState = true;
		bool bCaptureMontage = true;
		float ThrPosCm = 5.f;
		float ThrRotDeg = 2.f;
		float ThrVelCms = 25.f;
		float ThrTrackedDefault = 0.f;
		TMap<FString, float> TrackedThresholds;

		FCSVRow LastRow;
		FCSVRow PrevRow;
		FTrackedActorRow LastActorRow;
		FTrackedWidgetRow LastWidgetRow;
	};

	struct FLiveObservationSnapshot
	{
		FString ProfileName;
		FString RunId;
		EObserverState State = EObserverState::Idle;
		int32 FramesSampled = 0;
		double ElapsedSeconds = 0.0;

		FCSVRow LastRow;
		FCSVRow PrevRow;

		TArray<FString> TrackedActorIds;
		FTrackedActorRow LastActorRow;
	};

	class FPIEObserver
	{
	public:
		static FPIEObserver& Get();

		void Init();
		void Shutdown();

		bool Arm(const FObserverArmConfig& Cfg, FString& OutError, FString& OutMessage);
		bool Disarm(FString& OutError);
		FObserverFinishResult ForceStop();
		FObserverStatus GetStatus() const;
		bool IsActive() const;
		TArray<FLiveObservationSnapshot> GetLiveSnapshots() const;

		int32 NumSessions() const { return Sessions.Num(); }

		// Per-run access for the script runner. Each returns false when the run is not
		// live or has no sample for the name yet.
		bool HasRun(const FString& RunId) const;
		bool GetRunClock(const FString& RunId, FObserverRunClock& Out) const;
		bool ReadTrackedValue(const FString& RunId, const FString& Name, double& OutValue) const;
		bool ReadActorResolved(const FString& RunId, const FString& ActorId, bool& OutResolved) const;
		bool ReadWidgetVisible(const FString& RunId, const FString& Widget, bool& OutVisible) const;
		// Finalise one run (files flush) and drop it, leaving other sessions running.
		FObserverFinishResult StopRun(const FString& RunId);

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();
		FObserverFinishResult FinaliseSession(FObservationSession& S);
		const FObservationSession* FindSession(const FString& RunId) const;
		void BindEndFrame();
		void UnbindEndFrame();

		TArray<FObservationSession> Sessions;
		FString LastError;

		FDelegateHandle BeginPIEHandle;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle OnEndFrameHandle;
		bool bEndFrameBound = false;
	};
}
