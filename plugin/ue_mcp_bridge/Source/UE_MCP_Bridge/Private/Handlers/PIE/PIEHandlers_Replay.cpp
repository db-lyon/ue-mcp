// PIE replay handlers: pie_replay_arm / _disarm / _stop / _status.
// Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEInputReplayer.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	using namespace MCPPIE;

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
		// pie_active lets an unattended replay_run caller poll until the PIE
		// session has ended; when it flips false and last_result is present the
		// drift report on disk is finalized and safe to read.
		R->SetBoolField(TEXT("pie_active"), S.bPIEActive);
		if (!S.LastError.IsEmpty())
		{
			R->SetStringField(TEXT("last_error"), S.LastError);
		}
		if (S.bHasLastResult)
		{
			TSharedPtr<FJsonObject> Last = MakeShared<FJsonObject>();
			Last->SetStringField(TEXT("drift_report_path"), S.LastDriftReportPath);
			if (!S.LastGifPath.IsEmpty()) Last->SetStringField(TEXT("gif_path"), S.LastGifPath);
			Last->SetNumberField(TEXT("max_position_drift_cm"), S.LastMaxPositionDriftCm);
			Last->SetNumberField(TEXT("max_velocity_drift_cms"), S.LastMaxVelocityDriftCms);
			Last->SetNumberField(TEXT("frames_compared"), S.LastFramesCompared);
			// Item 1b: kept frames + labeled contact sheet the agent can view.
			if (!S.LastFrameDir.IsEmpty()) Last->SetStringField(TEXT("frame_dir"), S.LastFrameDir);
			if (S.LastFrameCount > 0) Last->SetNumberField(TEXT("frame_count"), S.LastFrameCount);
			if (!S.LastContactSheetPath.IsEmpty()) Last->SetStringField(TEXT("contact_sheet_path"), S.LastContactSheetPath);
			R->SetObjectField(TEXT("last_result"), Last);
		}
	}

	// Parse the shared replay arm-config from an MCP params object. Returns
	// false and sets OutErr (a ready-to-return MCPError) on a bad inline
	// sequence. Used by both replay_arm and replay_run so the two stay in
	// lockstep on every knob.
	bool ParseReplayArmConfig(const TSharedPtr<FJsonObject>& Params, FReplayerArmConfig& Cfg, TSharedPtr<FJsonValue>& OutErr)
	{
		int32 PIEInstance = INDEX_NONE;
		FString PIEInstanceError;
		if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError))
		{
			OutErr = MCPError(PIEInstanceError);
			return false;
		}
		Cfg.SourceRecordingId = OptionalString(Params, TEXT("recording_id"));
		Cfg.SequencePath = OptionalString(Params, TEXT("sequence_path"));
		Cfg.SourceDir = OptionalString(Params, TEXT("recording_dir"));

		// Convenience: `recording_dir` alone (no id, no explicit sequence) names
		// the recording folder directly - "replay this ./path/to/recording".
		// Derive the id from the leaf so drift.json lands beside the source.
		if (Cfg.SourceRecordingId.IsEmpty() && Cfg.SequencePath.IsEmpty() && !Cfg.SourceDir.IsEmpty())
		{
			FString Leaf = Cfg.SourceDir;
			Leaf.RemoveFromEnd(TEXT("/"));
			Leaf.RemoveFromEnd(TEXT("\\"));
			Cfg.SourceRecordingId = FPaths::GetCleanFilename(Leaf);
		}

		// Inline steps array - parse via the standard sequence reader by wrapping
		// in a minimal envelope.
		const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
		if (TryGetArrayParam(Params, TEXT("steps"), StepsArr) && StepsArr && StepsArr->Num() > 0)
		{
			TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
			Env->SetNumberField(TEXT("version"), kFormatVersion);
			Env->SetNumberField(TEXT("settle_ms"), OptionalInt(Params, TEXT("settle_ms"), 500));
			Env->SetNumberField(TEXT("sample_hz"), OptionalInt(Params, TEXT("sample_hz"), 60));
			Env->SetNumberField(TEXT("rng_seed"), OptionalNumber(Params, TEXT("rng_seed"), 0.0));
			Env->SetArrayField(TEXT("steps"), *StepsArr);
			FSequence S;
			FString Err;
			if (!SequenceFromJson(Env, S, Err)) { OutErr = MCPError(Err); return false; }
			Cfg.InlineSequence = S;
			Cfg.bInlineSequenceProvided = true;
		}

		if (HasParam(Params, TEXT("pin_fps")))
		{
			int32 Hz = 60;
			TryGetNumberParam(Params, TEXT("pin_fps"), Hz);
			Cfg.PinFPS = Hz;
		}
		if (HasParam(Params, TEXT("settle_ms")))
		{
			int32 Ms = 500;
			TryGetNumberParam(Params, TEXT("settle_ms"), Ms);
			Cfg.SettleMs = Ms;
		}
		Cfg.bApplyRngSeed = OptionalBool(Params, TEXT("apply_rng_seed"), true);
		Cfg.bRecordDrift  = OptionalBool(Params, TEXT("record_drift"), true);
		Cfg.bAutoStopPIE  = OptionalBool(Params, TEXT("auto_stop_pie"), false);
		Cfg.bEncodeGif    = OptionalBool(Params, TEXT("encode_gif"), false);
		Cfg.bFixedTimestep = OptionalBool(Params, TEXT("fixed_timestep"), false);
		Cfg.bEject        = OptionalBool(Params, TEXT("eject"), false);
		Cfg.TimeScale     = static_cast<float>(OptionalNumber(Params, TEXT("time_scale"), 1.0));
		const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("replay")).ToLower();
		Cfg.bMonitor = (Mode == TEXT("monitor"));

		if (HasParam(Params, TEXT("capture_frame_every")))
		{
			double D = 0;
			TryGetNumberParam(Params, TEXT("capture_frame_every"), D);
			Cfg.CaptureFrameEvery = FMath::Max(0, static_cast<int32>(D));
		}

		if (HasParam(Params, TEXT("client_id")))
		{
			double D = 0;
			TryGetNumberParam(Params, TEXT("client_id"), D);
			Cfg.ClientId = FMath::Max(0, static_cast<int32>(D));
		}
		Cfg.PIEInstance = PIEInstance;


		const TSharedPtr<FJsonObject>* Thr = nullptr;
		if (TryGetObjectParam(Params, TEXT("drift_thresholds"), Thr) && Thr)
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
						Cfg.TrackedThresholds.Add(FString(*KV.Key), static_cast<float>(TD));
					}
				}
			}
		}

		return true;
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieReplayArm(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FReplayerArmConfig Cfg;
	TSharedPtr<FJsonValue> ParseErr;
	if (!ParseReplayArmConfig(Params, Cfg, ParseErr)) return ParseErr;

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
	MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_replay_disarm"), Payload);
	return MCPResult(Result);
}

// One-shot unattended replay: arm the replayer and immediately start PIE, so
// an agent can drive a recording to completion without a human (or a separate
// editor(play_in_editor) call) touching the editor. Defaults auto_stop_pie to
// true so PIE tears itself down when the run finishes and drift.json lands on
// disk. Returns immediately (PIE runs across frames) - poll pie_replay_status
// until pie_active is false, then read the drift report.
TSharedPtr<FJsonValue> FPIEHandlers::PieReplayRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	if (!GEditor)
	{
		return MCPError(TEXT("pie_replay_run requires the editor (GEditor unavailable)"));
	}
	if (GEditor->PlayWorld != nullptr)
	{
		return MCPError(TEXT("A PIE session is already running; stop it before pie_replay_run"));
	}

	FReplayerArmConfig Cfg;
	TSharedPtr<FJsonValue> ParseErr;
	if (!ParseReplayArmConfig(Params, Cfg, ParseErr)) return ParseErr;
	// Unattended: default to ending PIE ourselves when the run completes. A
	// caller can still pass auto_stop_pie=false to leave PIE up for inspection.
	Cfg.bAutoStopPIE = OptionalBool(Params, TEXT("auto_stop_pie"), true);

	FString Err, Msg;
	if (!FPIEInputReplayer::Get().Arm(Cfg, Err, Msg))
	{
		return MCPError(Err);
	}

	// Kick off PIE. RequestPlaySession queues the session for the next editor
	// tick; the armed replayer attaches from its BeginPIE delegate.
	FRequestPlaySessionParams PlayParams;
	GEditor->RequestPlaySession(PlayParams);

	const FReplayerStatus S = FPIEInputReplayer::Get().GetStatus();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("started"), true);
	Result->SetBoolField(TEXT("auto_stop_pie"), Cfg.bAutoStopPIE);
	Result->SetStringField(TEXT("message"), Msg);
	Result->SetStringField(TEXT("poll"), TEXT("pie_replay_status until pie_active=false, then read drift via record_read(file=drift)"));
	WriteStatusFields(Result, S);
	MCPSetCreated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_replay_stop"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieReplayDisarm(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	const bool OK = FPIEInputReplayer::Get().Disarm(Err);
	if (!OK) return MCPError(Err);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("disarmed"), true);
	Result->SetBoolField(TEXT("changed"), true);
	MCPSetNoRollback(Result, TEXT("The disarmed replay's settings are not kept; arm it again with replay_arm"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieReplayStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FReplayerFinishResult F = FPIEInputReplayer::Get().ForceStop();
	if (!F.bSuccess && !F.Error.IsEmpty()) return MCPError(F.Error);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetNumberField(TEXT("executed_steps"), F.ExecutedSteps);
	Result->SetNumberField(TEXT("frames_captured"), F.FramesCaptured);
	if (F.FrameCount > 0)
	{
		Result->SetStringField(TEXT("frame_dir"), F.FrameDir);
		Result->SetNumberField(TEXT("frame_count"), F.FrameCount);
		if (!F.ContactSheetPath.IsEmpty())
		{
			Result->SetStringField(TEXT("contact_sheet_path"), F.ContactSheetPath);
		}
	}
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
	Result->SetBoolField(TEXT("alreadyStopped"), !F.bSuccess);
	MCPSetNoRollback(Result, TEXT("A stopped replay cannot be resumed; replay_run starts it again from the beginning"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieReplayStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FReplayerStatus S = FPIEInputReplayer::Get().GetStatus();
	auto Result = MCPSuccess();
	WriteStatusFields(Result, S);
	return MCPResult(Result);
}

// Item 1c: read a finalised drift.json and return the synthesised lead (first
// divergence, top channels, correlated errors) plus the images bracketing the
// divergence frame, so the agent gets a conclusion instead of a CSV.
TSharedPtr<FJsonValue> FPIEHandlers::PieReplayAnalyze(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));

	FString Folder;
	if (!RecDir.IsEmpty() && Id.IsEmpty())
	{
		Folder = RecDir;
	}
	else
	{
		const FString Root = RecDir.IsEmpty()
			? (FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPRecordings"))
			: RecDir;
		Folder = Root / Id;
	}
	Folder.RemoveFromEnd(TEXT("/"));
	Folder.RemoveFromEnd(TEXT("\\"));

	const FString DriftPath = Folder / TEXT("drift.json");
	FDriftReport D;
	FString Err;
	if (!LoadDrift(DriftPath, D, Err))
	{
		return MCPError(FString::Printf(TEXT("Could not read drift report at %s: %s"), *DriftPath, *Err));
	}

	// The serialized report already carries the summary block.
	TSharedRef<FJsonObject> Report = DriftToJson(D);
	Report->SetStringField(TEXT("drift_report_path"), DriftPath);

	// Images bracketing the divergence frame, if frames were captured.
	if (D.Summary.First.bFound)
	{
		const uint64 F = D.Summary.First.Frame;
		TArray<TSharedPtr<FJsonValue>> Bracket;
		for (int32 Off = -1; Off <= 1; ++Off)
		{
			const int64 N = static_cast<int64>(F) + Off;
			if (N < 0) continue;
			const FString P = Folder / TEXT("frames") / FString::Printf(TEXT("frame_%05lld.jpg"), N);
			if (FPaths::FileExists(P))
			{
				Bracket.Add(MakeShared<FJsonValueString>(P));
			}
		}
		if (Bracket.Num() > 0)
		{
			Report->SetArrayField(TEXT("divergence_frames"), Bracket);
		}
	}

	// Newest contact sheet, if present.
	{
		const FString CapturesDir = Folder / TEXT("captures");
		TArray<FString> Sheets;
		IFileManager::Get().FindFiles(Sheets, *(CapturesDir / TEXT("contact_*.jpg")), true, false);
		if (Sheets.Num() > 0)
		{
			Sheets.Sort();
			Report->SetStringField(TEXT("contact_sheet_path"), CapturesDir / Sheets.Last());
		}
	}

	return MCPResult(Report);
}
