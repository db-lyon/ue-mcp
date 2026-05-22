// PIE replay handlers: pie_replay_arm / _disarm / _stop / _status.
// Members of FGameplayHandlers.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIESequenceFormat.h"

namespace
{
	using namespace UEMCPPIE;

	FString StateToString(EReplayerState S)
	{
		switch (S)
		{
		case EReplayerState::Idle:           return TEXT("idle");
		case EReplayerState::Armed:          return TEXT("armed");
		case EReplayerState::WaitingForPawn: return TEXT("waiting_for_pawn");
		case EReplayerState::Replaying:      return TEXT("replaying");
		case EReplayerState::Completed:      return TEXT("completed");
		}
		return TEXT("idle");
	}

	void WriteStatusFields(TSharedPtr<FJsonObject> R, const FReplayerStatus& S)
	{
		R->SetStringField(TEXT("state"), StateToString(S.State));
		R->SetStringField(TEXT("source_recording_id"), S.SourceRecordingId);
		R->SetNumberField(TEXT("current_step"), S.CurrentStep);
		R->SetNumberField(TEXT("total_steps"), S.TotalSteps);
		R->SetNumberField(TEXT("elapsed_seconds"), S.ElapsedSeconds);
		R->SetNumberField(TEXT("max_position_drift_cm"), S.MaxPositionDriftCm);
		R->SetNumberField(TEXT("max_velocity_drift_cms"), S.MaxVelocityDriftCms);
		R->SetNumberField(TEXT("frames_captured"), S.FramesCaptured);
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieReplayArm(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FReplayerArmConfig Cfg;
	Cfg.SourceRecordingId = OptionalString(Params, TEXT("recording_id"));
	Cfg.SequencePath = OptionalString(Params, TEXT("sequence_path"));
	Cfg.SourceDir = OptionalString(Params, TEXT("recording_dir"));

	// Inline steps array — parse via the standard sequence reader by wrapping
	// in a minimal envelope.
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("steps"), StepsArr) && StepsArr && StepsArr->Num() > 0)
	{
		TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
		Env->SetNumberField(TEXT("version"), kFormatVersion);
		Env->SetNumberField(TEXT("settle_ms"), OptionalInt(Params, TEXT("settle_ms"), 500));
		Env->SetNumberField(TEXT("sample_hz"), OptionalInt(Params, TEXT("sample_hz"), 60));
		Env->SetNumberField(TEXT("rng_seed"), OptionalNumber(Params, TEXT("rng_seed"), 0.0));
		Env->SetArrayField(TEXT("steps"), *StepsArr);
		FSequence S;
		FString Err;
		if (!SequenceFromJson(Env, S, Err)) return MCPError(Err);
		Cfg.InlineSequence = S;
		Cfg.bInlineSequenceProvided = true;
	}

	if (Params->HasField(TEXT("pin_fps")))
	{
		int32 Hz = 60;
		Params->TryGetNumberField(TEXT("pin_fps"), Hz);
		Cfg.PinFPS = Hz;
	}
	if (Params->HasField(TEXT("settle_ms")))
	{
		int32 Ms = 500;
		Params->TryGetNumberField(TEXT("settle_ms"), Ms);
		Cfg.SettleMs = Ms;
	}
	Cfg.bApplyRngSeed = OptionalBool(Params, TEXT("apply_rng_seed"), true);
	Cfg.bRecordDrift  = OptionalBool(Params, TEXT("record_drift"), true);
	Cfg.bAutoStopPIE  = OptionalBool(Params, TEXT("auto_stop_pie"), false);
	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("replay")).ToLower();
	Cfg.bMonitor = (Mode == TEXT("monitor"));

	if (Params->HasField(TEXT("capture_frame_every")))
	{
		int32 N = 0;
		Params->TryGetNumberField(TEXT("capture_frame_every"), N);
		Cfg.CaptureFrameEvery = FMath::Max(0, N);
	}

	if (Params->HasField(TEXT("client_id")))
	{
		int32 CId = 0;
		Params->TryGetNumberField(TEXT("client_id"), CId);
		Cfg.ClientId = FMath::Max(0, CId);
	}

	const TSharedPtr<FJsonObject>* Thr = nullptr;
	if (Params->TryGetObjectField(TEXT("drift_thresholds"), Thr) && Thr)
	{
		double D;
		if ((*Thr)->TryGetNumberField(TEXT("position_cm"), D)) Cfg.ThrPosCm = static_cast<float>(D);
		if ((*Thr)->TryGetNumberField(TEXT("rotation_deg"), D)) Cfg.ThrRotDeg = static_cast<float>(D);
		if ((*Thr)->TryGetNumberField(TEXT("velocity_cms"), D)) Cfg.ThrVelCms = static_cast<float>(D);
		if ((*Thr)->TryGetNumberField(TEXT("tracked_default"), D)) Cfg.ThrTrackedDefault = static_cast<float>(D);
		const TSharedPtr<FJsonObject>* Tracked = nullptr;
		if ((*Thr)->TryGetObjectField(TEXT("tracked"), Tracked) && Tracked)
		{
			for (const auto& KV : (*Tracked)->Values)
			{
				double TD = 0;
				if (KV.Value.IsValid() && KV.Value->TryGetNumber(TD))
				{
					Cfg.TrackedThresholds.Add(KV.Key, static_cast<float>(TD));
				}
			}
		}
	}

	FString Err, Msg;
	if (!FPIEInputReplayer::Get().Arm(Cfg, Err, Msg))
	{
		return MCPError(Err);
	}

	const FReplayerStatus S = FPIEInputReplayer::Get().GetStatus();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("armed"), true);
	Result->SetStringField(TEXT("message"), Msg);
	WriteStatusFields(Result, S);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_replay_disarm"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieReplayDisarm(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	const bool OK = FPIEInputReplayer::Get().Disarm(Err);
	if (!OK) return MCPError(Err);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("disarmed"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieReplayStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FReplayerFinishResult F = FPIEInputReplayer::Get().ForceStop();
	if (!F.bSuccess && !F.Error.IsEmpty()) return MCPError(F.Error);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetNumberField(TEXT("executed_steps"), F.ExecutedSteps);
	Result->SetNumberField(TEXT("frames_captured"), F.FramesCaptured);
	if (!F.CaptureDir.IsEmpty())
	{
		Result->SetStringField(TEXT("capture_dir"), F.CaptureDir);
		// Document the assembly recipe inline so consumers don't have to look
		// it up. Two common targets: GIF and MP4. Override -framerate to match
		// pin_fps if you used a non-default rate.
		Result->SetStringField(TEXT("ffmpeg_gif_hint"),
			FString::Printf(TEXT("ffmpeg -framerate 30 -i %s/frame_%%05d.png -vf 'fps=30,scale=720:-1:flags=lanczos' replay.gif"), *F.CaptureDir));
		Result->SetStringField(TEXT("ffmpeg_mp4_hint"),
			FString::Printf(TEXT("ffmpeg -framerate 60 -i %s/frame_%%05d.png -c:v libx264 -pix_fmt yuv420p replay.mp4"), *F.CaptureDir));
	}
	if (!F.DriftReportPath.IsEmpty())
	{
		Result->SetStringField(TEXT("drift_report_path"), F.DriftReportPath);
		Result->SetNumberField(TEXT("max_position_drift_cm"), F.Drift.MaxPositionDriftCm);
		Result->SetNumberField(TEXT("max_velocity_drift_cms"), F.Drift.MaxVelocityDriftCms);
		Result->SetNumberField(TEXT("frames_compared"), F.Drift.FramesCompared);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieReplayStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FReplayerStatus S = FPIEInputReplayer::Get().GetStatus();
	auto Result = MCPSuccess();
	WriteStatusFields(Result, S);
	return MCPResult(Result);
}
