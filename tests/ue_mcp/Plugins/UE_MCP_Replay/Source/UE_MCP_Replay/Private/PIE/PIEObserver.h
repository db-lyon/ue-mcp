#pragma once

#include "CoreMinimal.h"
#include "PIEFrameSampler.h"
#include "PIESequenceFormat.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UMCPObservationProfile;
class UWorld;
class AActor;

namespace UEMCPPIE
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
		int32 ClientId = 0;
	};

	struct FObserverStatus
	{
		EObserverState State = EObserverState::Idle;
		FString RunId;
		FString ProfilePath;
		int32 FramesSampled = 0;
		double ElapsedSeconds = 0.0;
	};

	struct FObserverFinishResult
	{
		bool bSuccess = false;
		FString Error;
		FString RunId;
		FString OutputDir;
		FString CSVPath;
		FString TrackedActorsPath;
		int32 FramesSampled = 0;
		double DurationSeconds = 0.0;
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
		bool IsActive() const { return State != EObserverState::Idle && State != EObserverState::Completed; }

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();
		FObserverFinishResult FinaliseCurrent();

		FObserverArmConfig Pending;
		EObserverState State = EObserverState::Idle;
		bool bArmed = false;

		FString CurrentRunId;
		FString CurrentOutputDir;
		FString CurrentProfilePath;

		FPIEFrameSampler Sampler;
		FCSVHeader CSVHdr;
		FString CSVHeaderStr;
		FString CSVBody;

		TArray<FString> TrackedActorIds;
		TArray<FTrackedActorRow> ActorRows;
		TMap<FString, TWeakObjectPtr<AActor>> ActorCache;

		double AttachTime = 0.0;
		int32 FramesSampled = 0;
		FString StartedAt;

		// Profile config extracted at arm time
		TArray<FString> TrackedValuePaths;
		bool bCapturePawnState = true;
		bool bCaptureMontage = true;
		float ThrPosCm = 5.f;
		float ThrRotDeg = 2.f;
		float ThrVelCms = 25.f;
		float ThrTrackedDefault = 0.f;
		TMap<FString, float> TrackedThresholds;

		FDelegateHandle BeginPIEHandle;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle OnEndFrameHandle;
		bool bEndFrameBound = false;
	};
}
