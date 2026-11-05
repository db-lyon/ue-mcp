#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIELog.h"
#include "Handlers/PIE/PIEInputRecorder.h"
#include "Handlers/PIE/PIEInputReplayer.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIEScriptRunner.h"
#include "Handlers/PIE/PIESessionLog.h"
#include "Handlers/PIE/PIEUatRunner.h"
#include "Handlers/PIE/PIEWorldResolver.h"
#include "Handlers/PIE/SMCPPIEStudioPanel.h"
#include "MCPPIEInputInjector.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/LocalPlayer.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/CoreRedirects.h"

DEFINE_LOG_CATEGORY(LogMCPPIE);

namespace
{
	// The pie-studio plugin this category replaces. While it is still enabled its own panel and toolbar stay,
	// so the editor never shows two Record buttons.
	const TCHAR* const SupersededPluginName = TEXT("PIE_Studio");

	bool GSystemsStarted = false;
	bool GPanelRegistered = false;
	FTSTicker::FDelegateHandle GThrottleTicker;

	bool IsSupersededPluginEnabled()
	{
		return IPluginManager::Get().FindEnabledPlugin(SupersededPluginName).IsValid();
	}

	// Observation profiles saved by pie-studio load as the core class once the plugin is gone.
	void AddProfileRedirects()
	{
		TArray<FCoreRedirect> Redirects;
		Redirects.Emplace(ECoreRedirectFlags::Type_Class,
			TEXT("/Script/PIE_Studio.MCPObservationProfile"), TEXT("/Script/UE_MCP_Bridge.MCPPIEObservationProfile"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Struct,
			TEXT("/Script/PIE_Studio.MCPTrackedValueEntry"), TEXT("/Script/UE_MCP_Bridge.MCPPIETrackedValueEntry"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Struct,
			TEXT("/Script/PIE_Studio.MCPTrackedActorEntry"), TEXT("/Script/UE_MCP_Bridge.MCPPIETrackedActorEntry"));
		FCoreRedirects::AddRedirectList(Redirects, TEXT("UE_MCP_Bridge"));
	}
}

void FPIEHandlers::StartupSystems()
{
	if (GSystemsStarted) return;
	GSystemsStarted = true;

	// The injector lives in UE_MCP_BridgeRuntime (separate-process clients run it too); in the editor it resolves
	// (pie_instance, client_id) through the PIE world resolver.
	MCPPIE::FPIEInputInjector::SetPlayerResolver([](int32 PIEInstance, int32 ClientIndex, FString& OutError) -> ULocalPlayer*
	{
		MCPPIE::PIEWorldResolver::FPlayerTarget Target;
		return MCPPIE::PIEWorldResolver::ResolvePlayer(PIEInstance, ClientIndex, Target, OutError) ? Target.LocalPlayer : nullptr;
	});
	MCPPIE::PIERemotePlayers::Init();
	MCPPIE::FPIEInputRecorder::Get().Init();
	MCPPIE::FPIEInputReplayer::Get().Init();
	MCPPIE::FPIEObserver::Get().Init();
	MCPPIE::FPIESessionLog::Get().Init();
	MCPPIE::FPIEScriptRunner::Get().Init();

	if (IsSupersededPluginEnabled())
	{
		UE_LOG(LogMCPPIE, Warning,
			TEXT("[UE-MCP] The PIE_Studio plugin is enabled, but PIE Studio is built into the UE MCP Bridge now. Its panel and Record button are left to the plugin until it is removed: run `npx ue-mcp plugin remove pie-studio`."));
	}
	else
	{
		AddProfileRedirects();
		SMCPPIEStudioPanel::RegisterTab();
		SMCPPIEStudioPanel::RegisterToolbarButton();
		GPanelRegistered = true;
	}

	FEditorDelegates::EndPIE.AddLambda([](bool)
	{
		MCPPIE::FPIEInputInjector::OnPIEEnded();
	});

	// The editor throttles an unfocused window to a few frames a second, which starves a recording, replay,
	// observation or script run of frames. Bound once the editor has a world.
	GThrottleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
	{
		if (!GEditor) return true;
		bool bHasWorld = false;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World()) { bHasWorld = true; break; }
		}
		if (!bHasWorld) return true;

		UEditorEngine::FShouldDisableCPUThrottling Suppress;
		Suppress.BindLambda([]() -> bool
		{
			return MCPPIE::FPIEInputRecorder::Get().IsActive()
				|| MCPPIE::FPIEInputReplayer::Get().IsActive()
				|| MCPPIE::FPIEObserver::Get().IsActive()
				|| MCPPIE::FPIEUatRunner::Get().IsRunning()
				|| MCPPIE::FPIEScriptRunner::Get().IsActive();
		});
		GEditor->ShouldDisableCPUThrottlingDelegates.Add(Suppress);
		GThrottleTicker.Reset();
		return false;
	}));
}

void FPIEHandlers::ShutdownSystems()
{
	if (!GSystemsStarted) return;
	GSystemsStarted = false;

	if (GThrottleTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GThrottleTicker);
		GThrottleTicker.Reset();
	}
	if (GPanelRegistered)
	{
		SMCPPIEStudioPanel::UnregisterToolbarButton();
		SMCPPIEStudioPanel::UnregisterTab();
		GPanelRegistered = false;
	}
	MCPPIE::FPIEScriptRunner::Get().Shutdown();
	MCPPIE::FPIESessionLog::Get().Shutdown();
	MCPPIE::FPIEObserver::Get().Shutdown();
	MCPPIE::FPIEInputReplayer::Get().Shutdown();
	MCPPIE::FPIEInputRecorder::Get().Shutdown();
	// The injector belongs to UE_MCP_BridgeRuntime and shuts down with it; only the editor's resolver is withdrawn.
	MCPPIE::FPIEInputInjector::SetPlayerResolver(nullptr);
	MCPPIE::PIERemotePlayers::Shutdown();
	MCPPIE::FPIEUatRunner::Get().Shutdown();
}

void FPIEHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("pie"));
	using EType = EMCPParamType;

	const TCHAR* const PieInstanceDoc = TEXT("FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back");
	const TCHAR* const ClientIdDoc = TEXT("Local-player index inside the selected PIE world (default 0)");
	const TCHAR* const RemoteClientDoc = TEXT("1..N relays to that remote player (join order) over the game connection, including separate-process clients; 0 or absent uses the local player");
	const TCHAR* const RecordingDirDoc = TEXT("Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)");
	const TCHAR* const RecordingIdDoc = TEXT("Recording id under the recordings root");
	const TCHAR* const ValueXDoc = TEXT("Axis or bool value X (default 1)");
	const TCHAR* const ValueYDoc = TEXT("Axis value Y (default 0)");
	const TCHAR* const ValueZDoc = TEXT("Axis value Z (default 0)");

	// Input injection
	Registry.RegisterHandler(TEXT("pie_inject_input"), &InjectInput, {
		MCPParam::Required(TEXT("action_path"), EType::String, TEXT("InputAction asset path")),
		MCPParam::Optional(TEXT("value_x"), EType::Number, ValueXDoc),
		MCPParam::Optional(TEXT("value_y"), EType::Number, ValueYDoc),
		MCPParam::Optional(TEXT("value_z"), EType::Number, ValueZDoc),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client"), EType::Integer, RemoteClientDoc).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Drives input into the running PIE session")));
	Registry.RegisterHandler(TEXT("pie_inject_input_start"), &InjectInputStart, {
		MCPParam::Required(TEXT("action_path"), EType::String, TEXT("InputAction asset path")),
		MCPParam::Optional(TEXT("value_x"), EType::Number, ValueXDoc),
		MCPParam::Optional(TEXT("value_y"), EType::Number, ValueYDoc),
		MCPParam::Optional(TEXT("value_z"), EType::Number, ValueZDoc),
		MCPParam::Optional(TEXT("injection_id"), EType::String, TEXT("Caller-chosen hold id (minted when absent)")),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client"), EType::Integer, RemoteClientDoc).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Starts holding an input action in the running PIE session")));
	Registry.RegisterHandler(TEXT("pie_inject_input_update"), &InjectInputUpdate, {
		MCPParam::Required(TEXT("injection_id"), EType::String, TEXT("Hold id from inject_input_start")),
		MCPParam::Optional(TEXT("value_x"), EType::Number, ValueXDoc),
		MCPParam::Optional(TEXT("value_y"), EType::Number, ValueYDoc),
		MCPParam::Optional(TEXT("value_z"), EType::Number, ValueZDoc),
	}, MCPSpec::ContractExempt(TEXT("Changes the value of a running input hold")));
	Registry.RegisterHandler(TEXT("pie_inject_input_stop"), &InjectInputStop, {
		MCPParam::Required(TEXT("injection_id"), EType::String, TEXT("Hold or tape id to stop")),
	}, MCPSpec::ContractExempt(TEXT("Stops a running input hold or tape")));
	Registry.RegisterHandler(TEXT("pie_inject_input_tape"), &InjectInputTape, {
		MCPParam::Required(TEXT("action_path"), EType::String, TEXT("InputAction asset path")),
		MCPParam::Required(TEXT("values"), EType::Array, TEXT("Per-frame values: numbers or [x,y,z] arrays")),
		MCPParam::Optional(TEXT("hz"), EType::Number, TEXT("Playback rate (default 60)")),
		MCPParam::Optional(TEXT("injection_id"), EType::String, TEXT("Caller-chosen tape id (minted when absent)")),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client"), EType::Integer, RemoteClientDoc).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Plays a value tape into the running PIE session")));

	// Multiplayer players
	Registry.RegisterHandler(TEXT("pie_clients"), &PieClients, {});
	Registry.RegisterHandler(TEXT("pie_console"), &PieConsole, {
		MCPParam::Required(TEXT("command"), EType::String, TEXT("Console command")),
		MCPParam::Required(TEXT("client"), EType::Integer, TEXT("Remote player 1..N (join order)")).Min(1),
	}, MCPSpec::ContractExempt(TEXT("Runs a console command in a remote player's process")));
	Registry.RegisterHandler(TEXT("pie_remote_result"), &PieRemoteResult, {
		MCPParam::Required(TEXT("request_id"), EType::String, TEXT("injection_id or request_id a relayed call returned")).Alias(TEXT("injection_id")),
	});

	// Multiplayer acceptance runs
	Registry.RegisterHandler(TEXT("pie_uat_run"), &PieUatRun, {
		MCPParam::Optional(TEXT("scenario"), EType::Object, TEXT("Scenario object: {name, map, pie:{players, net_mode, one_process, windowless, client_fps}, join_timeout_s, settle_ms, steps:[{name, do[], expect[], forbid[], window_ms}]}; see docs/pie.md")),
		MCPParam::Optional(TEXT("path"), EType::String, TEXT("Scenario JSON file, relative to the project directory")),
	}, MCPSpec::ExactlyOne({ { TEXT("scenario") }, { TEXT("path") } })
		.ContractExempt(TEXT("Opens the scenario map and starts a multiplayer PIE session")));
	Registry.RegisterHandler(TEXT("pie_uat_status"), &PieUatStatus, {});
	Registry.RegisterHandler(TEXT("pie_uat_abort"), &PieUatAbort, {
		MCPParam::Optional(TEXT("reason"), EType::String, TEXT("Why the run is ended, recorded in the report")),
	}, MCPSpec::ContractExempt(TEXT("Ends the acceptance run and stops PIE")));

	// Recording
	Registry.RegisterHandler(TEXT("pie_record_arm"), &PieRecordArm, {
		MCPParam::Optional(TEXT("actions"), EType::Array, TEXT("InputAction paths to record (default: every action bound on the pawn)")).Items(EType::String),
		MCPParam::Optional(TEXT("track_values"), EType::Array, TEXT("Dotted value paths sampled each frame; sub:<Class>.<path> reads a subsystem, bone:/bonecs:<Bone>.<channel> a bone")).Items(EType::String),
		MCPParam::Optional(TEXT("track_actors"), EType::Array, TEXT("Actor ids sampled for position, rotation and velocity")).Items(EType::String),
		MCPParam::Optional(TEXT("axis_threshold"), EType::Number, TEXT("Axis change deadzone (default 0.15)")),
		MCPParam::Optional(TEXT("sample_hz"), EType::Integer, TEXT("Sample rate (default 60)")).Min(1),
		MCPParam::Optional(TEXT("pin_fps"), EType::Integer, TEXT("Pin PIE fps for reproducibility (default sample_hz)")),
		MCPParam::Optional(TEXT("capture_pawn_state"), EType::Boolean, TEXT("Sample the pawn transform and movement (default true)")),
		MCPParam::Optional(TEXT("capture_montage"), EType::Boolean, TEXT("Sample the active montage (default true)")),
		MCPParam::Optional(TEXT("take_record"), EType::Boolean, TEXT("Also drive Take Recorder (default false)")),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("rng_seed"), EType::Number, TEXT("Seed the stream RNG for reproducible runs")),
		MCPParam::Optional(TEXT("run_gap_frames"), EType::Integer, TEXT("Idle frames that split input runs (default 6)")),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, TEXT("Recordings root (default Saved/MCPRecordings)")),
		MCPParam::Optional(TEXT("id"), EType::String, TEXT("Recording id (default: a timestamp)")),
	}, MCPSpec::ContractExempt(TEXT("Arms the recorder, which starts on the next PIE session")));
	Registry.RegisterHandler(TEXT("pie_record_disarm"), &PieRecordDisarm, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and cancels an armed recording")));
	Registry.RegisterHandler(TEXT("pie_record_stop"), &PieRecordStop, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and finalizes the recording in flight")));
	Registry.RegisterHandler(TEXT("pie_record_status"), &PieRecordStatus, {});
	Registry.RegisterHandler(TEXT("pie_record_list"), &PieRecordList, {
		MCPParam::Optional(TEXT("recording_dir"), EType::String, TEXT("Recordings root (default Saved/MCPRecordings)")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most rows (default 200)")).Min(1),
		MCPParam::Optional(TEXT("offset"), EType::Integer, TEXT("Rows to skip")).Min(0),
	});
	Registry.RegisterHandler(TEXT("pie_record_read"), &PieRecordRead, {
		MCPParam::Required(TEXT("id"), EType::String, TEXT("Recording id")),
		MCPParam::Optional(TEXT("file"), EType::String, TEXT("Artifact to read (default manifest)")).Enum({ TEXT("manifest"), TEXT("sequence"), TEXT("csv"), TEXT("drift"), TEXT("tracked") }),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, TEXT("Recordings root (default Saved/MCPRecordings)")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("csv: most rows (default 1000)")).Min(1),
		MCPParam::Optional(TEXT("offset"), EType::Integer, TEXT("csv: first row")).Min(0),
	});
	Registry.RegisterHandler(TEXT("pie_record_delete"), &PieRecordDelete, {
		MCPParam::Required(TEXT("id"), EType::String, TEXT("Recording id")),
		MCPParam::Optional(TEXT("confirm"), EType::Boolean, TEXT("Must be true to delete")),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, TEXT("Recordings root (default Saved/MCPRecordings)")),
	}, MCPSpec::ContractExempt(TEXT("Deletes a recording folder from disk")));
	Registry.RegisterHandler(TEXT("pie_mark"), &PieMark, {
		MCPParam::Required(TEXT("label"), EType::String, TEXT("Marker label")),
	}, MCPSpec::ContractExempt(TEXT("Writes a marker into the recording in flight")));

	// Replay. replay_arm and replay_run share one parser, so their params stay identical.
	const TArray<FMCPParamSpec> ReplayParams = {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("sequence_path"), EType::String, TEXT("Explicit sequence.json path")),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
		MCPParam::Optional(TEXT("steps"), EType::Array, TEXT("Inline step array, instead of a recording")).Items(EType::Object),
		MCPParam::Optional(TEXT("settle_ms"), EType::Integer, TEXT("Settle delay before playback (default 500)")).Min(0),
		MCPParam::Optional(TEXT("sample_hz"), EType::Integer, TEXT("Inline steps: sample rate (default 60)")).Min(1),
		MCPParam::Optional(TEXT("rng_seed"), EType::Number, TEXT("Inline steps: RNG seed")),
		MCPParam::Optional(TEXT("pin_fps"), EType::Integer, TEXT("Pin PIE fps during replay")),
		MCPParam::Optional(TEXT("apply_rng_seed"), EType::Boolean, TEXT("Re-apply the recorded seed (default true)")),
		MCPParam::Optional(TEXT("record_drift"), EType::Boolean, TEXT("Record drift against the source (default true)")),
		MCPParam::Optional(TEXT("auto_stop_pie"), EType::Boolean, TEXT("End PIE when the replay completes (replay_arm default false, replay_run default true)")),
		MCPParam::Optional(TEXT("eject"), EType::Boolean, TEXT("Eject to a spectator camera during replay")),
		MCPParam::Optional(TEXT("time_scale"), EType::Number, TEXT("Playback speed multiplier (default 1)")),
		MCPParam::Optional(TEXT("mode"), EType::String, TEXT("replay drives input; monitor only samples (default replay)")).Enum({ TEXT("replay"), TEXT("monitor") }),
		MCPParam::Optional(TEXT("capture_frame_every"), EType::Integer, TEXT("Capture a viewport JPEG every N steps and build a contact sheet")).Min(0),
		MCPParam::Optional(TEXT("encode_gif"), EType::Boolean, TEXT("Also encode an animated GIF (default false)")),
		MCPParam::Optional(TEXT("fixed_timestep"), EType::Boolean, TEXT("Fixed 1/pin_fps delta during replay (default false)")),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("drift_thresholds"), EType::Object, TEXT("{position_cm, rotation_deg, velocity_cms, tracked_default, tracked:{path: threshold}}")),
	};
	Registry.RegisterHandler(TEXT("pie_replay_arm"), &PieReplayArm, ReplayParams,
		MCPSpec::ContractExempt(TEXT("Arms the replayer, which drives the next PIE session")));
	Registry.RegisterHandler(TEXT("pie_replay_run"), &PieReplayRun, ReplayParams,
		MCPSpec::ContractExempt(TEXT("Arms the replayer and starts PIE")));
	Registry.RegisterHandler(TEXT("pie_replay_disarm"), &PieReplayDisarm, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and cancels an armed replay")));
	Registry.RegisterHandler(TEXT("pie_replay_stop"), &PieReplayStop, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and stops the replay in flight")));
	Registry.RegisterHandler(TEXT("pie_replay_status"), &PieReplayStatus, {});
	Registry.RegisterHandler(TEXT("pie_replay_analyze"), &PieReplayAnalyze, {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
	});
	Registry.RegisterHandler(TEXT("pie_replay_state"), &PieReplayState, {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
		MCPParam::Optional(TEXT("at_time"), EType::Number, TEXT("Seconds to sample, interpolated (default: the last frame)")),
		MCPParam::Optional(TEXT("at_frame"), EType::Integer, TEXT("Frame index to sample, instead of at_time")).Min(0),
		MCPParam::Optional(TEXT("apply"), EType::Boolean, TEXT("Teleport the live PIE pawn to the recorded pose (default false)")),
	}, MCPSpec::ContractExempt(TEXT("With apply it moves the live PIE pawn")));

	// Reproduction tests and assertions
	Registry.RegisterHandler(TEXT("pie_test_scaffold"), &PieTestScaffold, {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Test name (default repro)")),
		MCPParam::Optional(TEXT("max_position_drift_cm"), EType::Number, TEXT("Most position drift allowed (default 10)")),
		MCPParam::Optional(TEXT("max_velocity_drift_cms"), EType::Number, TEXT("Most velocity drift allowed (default 50)")),
		MCPParam::Optional(TEXT("max_rotation_drift_deg"), EType::Number, TEXT("Most rotation drift allowed (default 5)")),
		MCPParam::Optional(TEXT("max_errors"), EType::Integer, TEXT("Most errors logged during the run (default 0)")).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Writes test.json beside the recording")));
	Registry.RegisterHandler(TEXT("pie_test_run"), &PieTestRun, {
		MCPParam::Optional(TEXT("test_path"), EType::String, TEXT("Explicit test.json path")),
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Test name (default repro)")),
		MCPParam::Optional(TEXT("auto_run"), EType::Boolean, TEXT("Drive the replay when no drift.json exists yet (default true)")),
	}, MCPSpec::ContractExempt(TEXT("Replays the recording in PIE")));
	Registry.RegisterHandler(TEXT("pie_test_list"), &PieTestList, {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
	});
	Registry.RegisterHandler(TEXT("pie_assert_eval"), &PieAssertEval, {
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Observation run id (resolves observation.csv under output_dir)")),
		MCPParam::Optional(TEXT("output_dir"), EType::String, TEXT("Observation root (default Saved/MCPObservations)")),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, TEXT("Recording or replay folder (resolves recording.csv)")),
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("csv_path"), EType::String, TEXT("Explicit per-frame series CSV")),
		MCPParam::Optional(TEXT("assertions"), EType::Array, TEXT("Predicates {name, channel|event, op, value|min+max|tol, hold, from_s, to_s, within_s, after_event, severity}; omitted reads test_path's predicates")).Items(EType::Object),
		MCPParam::Optional(TEXT("test_path"), EType::String, TEXT("Scenario json whose predicates block is evaluated when assertions is omitted")),
	}, MCPSpec::ContractExempt(TEXT("Writes verdict.json beside the series it evaluates")));
	Registry.RegisterHandler(TEXT("pie_record_diff"), &PieRecordDiff, {
		MCPParam::Required(TEXT("a_id"), EType::String, TEXT("First recording id")),
		MCPParam::Required(TEXT("b_id"), EType::String, TEXT("Second recording id")),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, TEXT("Recordings root (default Saved/MCPRecordings)")),
		MCPParam::Optional(TEXT("position_cm"), EType::Number, TEXT("Position drift threshold (default 5)")),
		MCPParam::Optional(TEXT("rotation_deg"), EType::Number, TEXT("Rotation drift threshold (default 2)")),
		MCPParam::Optional(TEXT("velocity_cms"), EType::Number, TEXT("Velocity drift threshold (default 25)")),
		MCPParam::Optional(TEXT("tracked_default"), EType::Number, TEXT("Default tracked-value threshold")),
		MCPParam::Optional(TEXT("tracked_thresholds"), EType::Object, TEXT("Per-path tracked-value thresholds {path: threshold}")),
	});

	// Declarative scenarios
	Registry.RegisterHandler(TEXT("pie_scenario_scaffold"), &PieScenarioScaffold, {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Scenario name (default scenario)")),
		MCPParam::Optional(TEXT("max_errors"), EType::Integer, TEXT("no-errors predicate threshold (default 0)")).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Writes scenario.json beside the recording")));
	Registry.RegisterHandler(TEXT("pie_scenario_validate"), &PieScenarioValidate, {
		MCPParam::Optional(TEXT("scenario_path"), EType::String, TEXT("Path to a scenario.json")),
		MCPParam::Optional(TEXT("scenario"), EType::Object, TEXT("Inline scenario object")),
	}, MCPSpec::ExactlyOne({ { TEXT("scenario_path") }, { TEXT("scenario") } }));

	// Observation profiles
	const TCHAR* const TrackedValuesDoc = TEXT("Value paths, or {path, drift_threshold} objects");
	Registry.RegisterHandler(TEXT("pie_profile_create"), &PieProfileCreate, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Asset name")),
		MCPParam::Optional(TEXT("package_path"), EType::String, TEXT("Content folder (default /Game/Observation)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("What to do when the asset exists (default skip)")).Enum({ TEXT("skip"), TEXT("replace") }),
		MCPParam::Optional(TEXT("tracked_values"), EType::Array, TrackedValuesDoc),
		MCPParam::Optional(TEXT("tracked_actors"), EType::Array, TEXT("Actor ids to track")).Items(EType::String),
		MCPParam::Optional(TEXT("tracked_widgets"), EType::Array, TEXT("Widget class names or paths, or widget names, sampled as visible true or false")).Items(EType::String),
		MCPParam::Optional(TEXT("capture_pawn_state"), EType::Boolean, TEXT("Sample the pawn transform and movement")),
		MCPParam::Optional(TEXT("capture_montage"), EType::Boolean, TEXT("Sample the active montage")),
		MCPParam::Optional(TEXT("position_threshold_cm"), EType::Number, TEXT("Position drift threshold")),
		MCPParam::Optional(TEXT("rotation_threshold_deg"), EType::Number, TEXT("Rotation drift threshold")),
		MCPParam::Optional(TEXT("velocity_threshold_cms"), EType::Number, TEXT("Velocity drift threshold")),
		MCPParam::Optional(TEXT("tracked_value_default_threshold"), EType::Number, TEXT("Default tracked-value threshold")),
	}, MCPSpec::ContractExempt(TEXT("Creates an observation profile asset")));
	Registry.RegisterHandler(TEXT("pie_profile_read"), &PieProfileRead, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("Profile asset path")),
	});
	Registry.RegisterHandler(TEXT("pie_profile_update"), &PieProfileUpdate, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("Profile asset path")),
		MCPParam::Optional(TEXT("tracked_values"), EType::Array, TEXT("Replaces the tracked values")),
		MCPParam::Optional(TEXT("tracked_actors"), EType::Array, TEXT("Replaces the tracked actors")).Items(EType::String),
		MCPParam::Optional(TEXT("tracked_widgets"), EType::Array, TEXT("Replaces the tracked widgets")).Items(EType::String),
		MCPParam::Optional(TEXT("capture_pawn_state"), EType::Boolean, TEXT("Sample the pawn transform and movement")),
		MCPParam::Optional(TEXT("capture_montage"), EType::Boolean, TEXT("Sample the active montage")),
		MCPParam::Optional(TEXT("position_threshold_cm"), EType::Number, TEXT("Position drift threshold")),
		MCPParam::Optional(TEXT("rotation_threshold_deg"), EType::Number, TEXT("Rotation drift threshold")),
		MCPParam::Optional(TEXT("velocity_threshold_cms"), EType::Number, TEXT("Velocity drift threshold")),
		MCPParam::Optional(TEXT("tracked_value_default_threshold"), EType::Number, TEXT("Default tracked-value threshold")),
	}, MCPSpec::ContractExempt(TEXT("Writes and saves the profile asset")));
	Registry.RegisterHandler(TEXT("pie_profile_delete"), &PieProfileDelete, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("Profile asset path")),
		MCPParam::Optional(TEXT("confirm"), EType::Boolean, TEXT("Must be true to delete")),
	}, MCPSpec::ContractExempt(TEXT("Deletes the profile asset")));
	Registry.RegisterHandler(TEXT("pie_profile_list"), &PieProfileList, {
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content path prefix (default /Game)")),
	});

	// Observation runs
	Registry.RegisterHandler(TEXT("pie_observe_arm"), &PieObserveArm, {
		MCPParam::Required(TEXT("profile"), EType::String, TEXT("Observation profile asset path")),
		MCPParam::Optional(TEXT("output_dir"), EType::String, TEXT("Observation root (default Saved/MCPObservations)")),
		MCPParam::Optional(TEXT("sample_hz"), EType::Integer, TEXT("Sample rate (default 60)")).Min(1),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("pin_fps"), EType::Integer, TEXT("Pin PIE fps while observing")),
	}, MCPSpec::ContractExempt(TEXT("Arms the observer, which starts on the next PIE session")));
	Registry.RegisterHandler(TEXT("pie_observe_disarm"), &PieObserveDisarm, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and cancels armed observations")));
	Registry.RegisterHandler(TEXT("pie_observe_stop"), &PieObserveStop, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and finalizes the observation in flight")));
	Registry.RegisterHandler(TEXT("pie_observe_status"), &PieObserveStatus, {});
	Registry.RegisterHandler(TEXT("pie_observe_list"), &PieObserveList, {
		MCPParam::Optional(TEXT("output_dir"), EType::String, TEXT("Observation root (default Saved/MCPObservations)")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most rows (default 200)")).Min(1),
	});
	Registry.RegisterHandler(TEXT("pie_observe_read"), &PieObserveRead, {
		MCPParam::Required(TEXT("run_id"), EType::String, TEXT("Observation run id")),
		MCPParam::Optional(TEXT("output_dir"), EType::String, TEXT("Observation root (default Saved/MCPObservations)")),
		MCPParam::Optional(TEXT("file"), EType::String, TEXT("Artifact to read; series returns per-channel time series (default manifest)")).Enum({ TEXT("manifest"), TEXT("csv"), TEXT("tracked"), TEXT("series"), TEXT("widgets") }),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("csv/tracked: most rows (default 1000)")).Min(1),
		MCPParam::Optional(TEXT("offset"), EType::Integer, TEXT("csv/tracked: first row")).Min(0),
		MCPParam::Optional(TEXT("channels"), EType::String, TEXT("series: comma-separated columns (default: every t:* plus speed2d, gpu_ms, game_ms)")),
		MCPParam::Optional(TEXT("threshold"), EType::Number, TEXT("series: report the first frame where |value| exceeds this")),
		MCPParam::Optional(TEXT("include_values"), EType::Boolean, TEXT("series: include the downsampled values (default false)")),
		MCPParam::Optional(TEXT("max_points"), EType::Integer, TEXT("series: most values per channel (default 500)")).Min(1),
	});

	// Live PIE introspection
	Registry.RegisterHandler(TEXT("pie_anim_state"), &GetPieAnimState, {
		MCPParam::Required(TEXT("actorLabel"), EType::String, TEXT("Actor label or name in PIE")),
	});
	Registry.RegisterHandler(TEXT("pie_anim_properties"), &GetPieAnimProperties, {
		MCPParam::Required(TEXT("actorLabel"), EType::String, TEXT("Actor label or name in PIE")),
		MCPParam::Optional(TEXT("propertyNames"), EType::Array, TEXT("Property names to read (default: all)")).Items(EType::String),
	});

	// Session log
	const TCHAR* const SessionDoc = TEXT("Session id under Saved/MCPSessions (default: the live or most recent session)");
	const TCHAR* const SessionDirDoc = TEXT("Absolute session folder, overriding session");
	Registry.RegisterHandler(TEXT("pie_session_errors"), &PieSessionErrors, {
		MCPParam::Optional(TEXT("session"), EType::String, SessionDoc),
		MCPParam::Optional(TEXT("session_dir"), EType::String, SessionDirDoc),
		MCPParam::Optional(TEXT("min_verbosity"), EType::String, TEXT("Lowest severity kept (default warning)")).Enum({ TEXT("error"), TEXT("warning") }),
	});
	Registry.RegisterHandler(TEXT("pie_session_log"), &PieSessionLog, {
		MCPParam::Optional(TEXT("session"), EType::String, SessionDoc),
		MCPParam::Optional(TEXT("session_dir"), EType::String, SessionDirDoc),
		MCPParam::Optional(TEXT("min_verbosity"), EType::String, TEXT("Lowest severity kept (default log)")).Enum({ TEXT("error"), TEXT("warning"), TEXT("display"), TEXT("log"), TEXT("verbose") }),
		MCPParam::Optional(TEXT("category"), EType::String, TEXT("Exact log category, such as LogTemp")),
		MCPParam::Optional(TEXT("contains"), EType::String, TEXT("Substring the message must contain")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most rows (default 200)")).Min(1),
		MCPParam::Optional(TEXT("offset"), EType::Integer, TEXT("First row")).Min(0),
	});

	// Profiling
	Registry.RegisterHandler(TEXT("pie_perf_summary"), &PiePerfSummary, {
		MCPParam::Optional(TEXT("recording_id"), EType::String, RecordingIdDoc),
		MCPParam::Optional(TEXT("recording_dir"), EType::String, RecordingDirDoc),
	});

	RegisterScriptHandlers(Registry);
}
