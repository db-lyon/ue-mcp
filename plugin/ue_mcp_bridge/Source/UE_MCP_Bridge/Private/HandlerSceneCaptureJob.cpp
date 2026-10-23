#include "HandlerSceneCaptureJob.h"
#include "HandlerSceneCapture.h"
#include "HandlerUtils.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Runtime/Launch/Resources/Version.h"

namespace UEMCP::SceneCapture
{
	void SubmitWarmStreamingView(const FVector& Location, int32 Width, float Fov, UWorld* World)
	{
		const float ScreenSize = static_cast<float>(Width);
		const float FovScreenSize = ScreenSize / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f));
		auto& Streaming = IStreamingManager::Get();
		// Unlike a zero-duration pending view, this expires without requiring
		// another viewport to submit a view after the capture finishes.
		// SetupViewInfos copies the view into the current update before expiry.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		Streaming.AddViewInformation(Location, ScreenSize, FovScreenSize, 1.0f, false, WarmStreamingViewDurationSeconds, nullptr, World);
#else
		Streaming.AddViewInformation(Location, ScreenSize, FovScreenSize, 1.0f, false, WarmStreamingViewDurationSeconds);
#endif
		// Legal multiple times per frame; preserve normal budgets and async IO.
		// The zero delta leaves the view available to the next engine tick too.
		Streaming.UpdateResourceStreaming(0.0f, false);
	}

	namespace WarmCapture
	{
		struct FJob
		{
			FTransientCapture Rig;
			TWeakObjectPtr<UWorld> World;
			FString Id, Path, Status = TEXT("warming"), Error;
			TSharedPtr<FJsonObject> Metadata;
			double Started = 0, Finished = 0, Timeout = 0;
			int32 RequestedFrames = 0, WarmedFrames = 0;
			uint64 LastFrame = MAX_uint64;
			int64 SizeBytes = -1;
			bool bOverwrote = false;
			bool bStreamingEnabled = false, bTextureStreamingEnabled = false;
			bool IsRunning() const { return Status == TEXT("warming"); }
		};

		static TUniquePtr<FJob> Job;
		static FTSTicker::FDelegateHandle Ticker;
		static FDelegateHandle WorldCleanup;

		static TSharedPtr<FJsonValue> Snapshot()
		{
			auto Result = MCPSuccess();
			const bool bRunning = Job->IsRunning();
			Result->SetBoolField(TEXT("success"), Job->Error.IsEmpty());
			Result->SetStringField(TEXT("captureId"), Job->Id);
			Result->SetStringField(TEXT("status"), Job->Status);
			Result->SetBoolField(TEXT("complete"), !bRunning);
			Result->SetBoolField(TEXT("fileWritten"), Job->Status == TEXT("completed"));
			Result->SetStringField(TEXT("path"), Job->Path);
			Result->SetNumberField(TEXT("warmupFramesRequested"), Job->RequestedFrames);
			Result->SetNumberField(TEXT("warmupFramesRendered"), Job->WarmedFrames);
			Result->SetNumberField(TEXT("elapsedSeconds"), (bRunning ? FPlatformTime::Seconds() : Job->Finished) - Job->Started);
			Result->SetNumberField(TEXT("warmupTimeoutSeconds"), Job->Timeout);
			Result->SetBoolField(TEXT("captureActorRemoved"), !Job->Rig.IsValid());
			Result->SetBoolField(TEXT("changed"), Job->Status == TEXT("completed"));
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"), TEXT("An active capture can be cancelled using captureId and cancel=true; a written PNG cannot be restored or removed by the bridge."));
			Result->SetObjectField(TEXT("captureMetadata"), MakeShared<FJsonObject>(*Job->Metadata));
			Result->SetStringField(TEXT("streamingMode"), TEXT("normal_capture_view"));
			Result->SetBoolField(TEXT("streamingEnabled"), Job->bStreamingEnabled);
			Result->SetBoolField(TEXT("textureStreamingEnabled"), Job->bTextureStreamingEnabled);
			Result->SetNumberField(TEXT("streamingViewBoost"), 1.0);
			Result->SetNumberField(TEXT("streamingViewDurationSeconds"), WarmStreamingViewDurationSeconds);
			Result->SetBoolField(TEXT("textureResidencyVerified"), false);
			if (!Job->Error.IsEmpty()) Result->SetStringField(TEXT("error"), Job->Error);
			if (Job->Status == TEXT("completed"))
			{
				Result->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Job->SizeBytes));
				Result->SetBoolField(TEXT("overwrote"), Job->bOverwrote);
			}
			return MCPResult(Result);
		}

		static void Finish(const FString& Status, const FString& Error = FString())
		{
			if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
			Ticker.Reset();
			if (WorldCleanup.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(WorldCleanup);
			WorldCleanup.Reset();
			Job->Rig.Destroy();
			Job->Status = Status;
			Job->Error = Error;
			Job->Finished = FPlatformTime::Seconds();
		}

		static bool Tick(float /*DeltaTime*/)
		{
			if (!Job || !Job->IsRunning()) return false;
			if (FPlatformTime::Seconds() - Job->Started >= Job->Timeout)
			{
				Finish(TEXT("timed_out"), TEXT("Capture warmup deadline exceeded; no PNG was written"));
				return false;
			}
			UWorld* World = Job->World.Get();
			if (!World || World->bIsTearingDown || !Job->Rig.IsValid())
			{
				Finish(TEXT("failed"), TEXT("Capture world or rig is no longer available"));
				return false;
			}
			// A ticker can be pumped more than once during a single engine frame.
			if (Job->LastFrame == GFrameCounter) return true;
			Job->LastFrame = GFrameCounter;
			auto* Comp = Job->Rig.GetComponent();
			auto* Target = Job->Rig.GetTarget();
			auto& Streaming = IStreamingManager::Get();
			Job->bStreamingEnabled = Streaming.IsStreamingEnabled();
			Job->bTextureStreamingEnabled = Streaming.IsTextureStreamingEnabled();
			SubmitWarmStreamingView(Comp->GetComponentLocation(), Target->SizeX, Comp->FOVAngle, World);
			Job->Rig.Capture(false);
			if (Job->WarmedFrames < Job->RequestedFrames)
			{
				++Job->WarmedFrames;
				return true;
			}
			// The final capture follows N completed warm frames on distinct ticks.
			if (FPlatformTime::Seconds() - Job->Started >= Job->Timeout)
			{
				Finish(TEXT("timed_out"), TEXT("Capture deadline exceeded before export; no PNG was written"));
				return false;
			}
			Job->Metadata->SetNumberField(TEXT("worldTimeSeconds"), World->GetTimeSeconds());
			Job->Metadata->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Comp->GetComponentLocation()));
			Job->Metadata->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Comp->GetComponentRotation()));
			Job->Metadata->SetObjectField(TEXT("forward"), MCPVec3ToJsonObject(Comp->GetForwardVector()));
			Job->Metadata->SetObjectField(TEXT("right"), MCPVec3ToJsonObject(Comp->GetRightVector()));
			Job->Metadata->SetObjectField(TEXT("up"), MCPVec3ToJsonObject(Comp->GetUpVector()));
			Job->Metadata->SetNumberField(TEXT("fovDegrees"), Comp->FOVAngle);
			Job->bOverwrote = IFileManager::Get().FileExists(*Job->Path);
			FString Error;
			if (!Job->Rig.ExportPng(Job->Path, Job->SizeBytes, Error)) Finish(TEXT("failed"), Error);
			else Finish(TEXT("completed"));
			return false;
		}
	}

	TSharedPtr<FJsonValue> StartWarmCapture(UWorld* World, const FVector& Location, const FRotator& Rotation,
		int32 Width, int32 Height, float Fov, int32 WarmupFrames, double TimeoutSeconds,
		const FString& OutputPath, const TSharedPtr<FJsonObject>& FocusMetadata)
	{
		using namespace WarmCapture;
		if (Job && Job->IsRunning()) return MCPError(FString::Printf(TEXT("Capture %s is still warming; query or cancel it before starting another"), *Job->Id));
		const float FovScreenSize = static_cast<float>(Width) / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f));
		if (!FMath::IsFinite(FovScreenSize) || FovScreenSize <= 0.0f)
			return MCPError(TEXT("Capture FOV does not produce a finite positive streaming screen size"));
		if (FMath::Abs(Location.X) >= 1.0e20 || FMath::Abs(Location.Y) >= 1.0e20 || FMath::Abs(Location.Z) >= 1.0e20)
			return MCPError(TEXT("Capture location is outside the streaming manager's supported coordinate range"));
		auto NewJob = MakeUnique<FJob>();
		FString Error;
		if (!NewJob->Rig.Spawn(World, Location, Rotation, Width, Height, Fov, Error)) return MCPError(Error);
		NewJob->Rig.GetComponent()->bAlwaysPersistRenderingState = true;
		NewJob->World = World;
		NewJob->Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		NewJob->Path = ResolveOutputPath(OutputPath);
		if (!NewJob->Path.EndsWith(TEXT(".png"))) NewJob->Path += TEXT(".png");
		NewJob->RequestedFrames = WarmupFrames;
		NewJob->Timeout = TimeoutSeconds;
		NewJob->Started = FPlatformTime::Seconds();
		NewJob->bStreamingEnabled = IStreamingManager::Get().IsStreamingEnabled();
		NewJob->bTextureStreamingEnabled = IStreamingManager::Get().IsTextureStreamingEnabled();
		auto Metadata = MakeShared<FJsonObject>();
		const auto* Comp = NewJob->Rig.GetComponent();
		Metadata->SetStringField(TEXT("worldPath"), World->GetPathName());
		Metadata->SetStringField(TEXT("world"), World->IsGameWorld() ? TEXT("pie") : TEXT("editor"));
		if (const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World))
		{
			if (Context->WorldType == EWorldType::PIE) Metadata->SetNumberField(TEXT("pieInstance"), Context->PIEInstance);
		}
		Metadata->SetStringField(TEXT("projection"), TEXT("perspective"));
		Metadata->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Comp->GetComponentLocation()));
		Metadata->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Comp->GetComponentRotation()));
		Metadata->SetObjectField(TEXT("forward"), MCPVec3ToJsonObject(Comp->GetForwardVector()));
		Metadata->SetObjectField(TEXT("right"), MCPVec3ToJsonObject(Comp->GetRightVector()));
		Metadata->SetObjectField(TEXT("up"), MCPVec3ToJsonObject(Comp->GetUpVector()));
		Metadata->SetNumberField(TEXT("fovDegrees"), Fov);
		Metadata->SetNumberField(TEXT("width"), Width);
		Metadata->SetNumberField(TEXT("height"), Height);
		Metadata->SetBoolField(TEXT("fullyLoadTextures"), false);
		if (FocusMetadata) Metadata->SetObjectField(TEXT("focus"), FocusMetadata);
		NewJob->Metadata = Metadata;
		Job = MoveTemp(NewJob);
		WorldCleanup = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* CleaningWorld, bool, bool)
		{
			if (Job && Job->IsRunning() && Job->World.Get() == CleaningWorld)
				Finish(TEXT("failed"), TEXT("Capture world was cleaned up; no PNG was written"));
		});
		Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
		return Snapshot();
	}

	TSharedPtr<FJsonValue> ReadWarmCapture(const FString& CaptureId, bool bCancel)
	{
		using namespace WarmCapture;
		if (!Job || Job->Id != CaptureId) return MCPError(TEXT("Unknown captureId; only the latest warm capture is retained"));
		if (bCancel && Job->IsRunning()) Finish(TEXT("cancelled"));
		return Snapshot();
	}

	void ShutdownWarmCapture()
	{
		if (WarmCapture::Job && WarmCapture::Job->IsRunning()) WarmCapture::Finish(TEXT("cancelled"));
		WarmCapture::Job.Reset();
	}
}
