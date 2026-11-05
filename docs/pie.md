# PIE testing

The `pie` tool records, replays and observes Play-In-Editor sessions, drives input on any player, runs multiplayer acceptance scenarios and turns a recording into a test that verifies a fix. It is built into ue-mcp. Starting PIE itself is `editor(play_in_editor)`.

For step-by-step test scripts with automatic and manual verdicts, see [Test scripts](pie-scripts.md).

Three systems run independently and can all run in the same PIE session:

1. **Record** captures inputs and pawn state and writes a replayable sequence.
2. **Replay** plays a recorded sequence back through the real Enhanced Input pipeline, optionally from a spectator camera at reduced speed.
3. **Observe** attaches a reusable observation profile to any PIE session (recording, replay or manual play) and samples tracked values and actors. Changing what you watch needs no new recording.

## Determinism

Stock Unreal is not deterministic. Chaos physics does not reproduce across runs, animation and much gameplay depend on frame rate, and unseeded randomness is common.

- Input replay (`replay_arm`, `replay_run`) re-injects the recorded input and measures how far the world drifted. It reproduces bugs that are a function of input and a seed. Heavy drift usually means the bug depends on state the recording did not capture.
- `replay_state` replays recorded transforms with nothing simulated, so it is exact.
- The durable reproduction unit is a reproduction test (`test_scaffold`, `test_run`), not a recording.
- `fixed_timestep=true` on replay uses a fixed `1/pin_fps` delta. Runs become more reproducible, not fully deterministic.

What replay holds constant: the same Enhanced Input values at the same frame numbers, the same `t.MaxFPS` pin, the same `FMath::RandInit(seed)`, and CPU throttling suppressed for the session. What it does not: particles, Niagara, behavior trees, physics, and anything with its own `FRandomStream` or wall-clock time. `drift.json` reports how far a run diverged as numbers.

## Editor UI

A **Record** button sits in the Play toolbar. Record arms the recorder. Its menu offers Record + Play, Arm Recorder, Disarm Recorder, Stop Recording and Open PIE Studio Panel.

The **PIE Studio** tab, listed with the editor's tool tabs, is a dockable panel:

- **Recorder**: state, recording id, frame count and elapsed time, with Record + Play, Arm, Disarm and Stop.
- **Time Scale**: a slider from 1% to 400% and presets (1%, 10%, 25%, 50%, 100%, 200%). The panel reapplies the scale when PIE restarts. From an agent, use `editor(set_pie_time_scale)`.
- **Recordings**: every saved recording with Replay and Delete. Replay arms the replayer with the active observation profiles and starts PIE. With **Capture GIF** on it also captures frames and encodes a GIF, listed with Open and Delete.
- **Observation Profiles**: a checkbox marking each profile active, Edit, Delete, Create and Refresh.
- **Live Debug**: replay progress, drift maxima and observed values while a replay or observation runs.

Active states show in green. Lists refresh every 5 seconds.

## Runtime module

The parts that run inside game processes (input injection, the remote player control component, viewport capture) ship as the bridge's `UE_MCP_BridgeRuntime` module. It is a `DeveloperTool` module: it loads in the editor and in separate-process PIE clients and is never part of a shipping build.

## Quick start

```text
# Record: arm, press Play, play, stop PIE
pie(action="record_arm", sample_hz=60)

# Replay at 10% speed from a spectator camera
pie(action="replay_arm", recording_id="air-walk-bug", eject=true, time_scale=0.1)

# Observe with a profile, alongside either of the above
pie(action="observe_arm", profile="/Game/Observation/CombatDebug")
```

### Unattended replay

`replay_run` arms the replayer and starts PIE in one call, then ends PIE and writes `drift.json` when the run completes. No one has to touch the editor.

```text
pie(action="replay_run", recording_dir="C:/Proj/Saved/MCPRecordings/some-recording")
pie(action="replay_status")      # poll until pie_active is false; last_result names drift_report_path
pie(action="record_read", id="some-recording", file="drift")
```

`replay_run` takes the same parameters as `replay_arm` and defaults `auto_stop_pie=true`; pass `false` to leave PIE up for inspection. `recording_dir` alone names the source folder. `replay_arm` only arms, so PIE still has to be started separately, for example with `editor(play_in_editor)`.

### Debugging loop

1. `replay_run(recording_dir=...)`, then poll `replay_status` until `pie_active=false`.
2. `replay_analyze(recording_dir=...)` returns the lead: first divergence (frame, channel, source and replay value), top channels, errors logged during the run, and the images bracketing the divergence. Read it instead of the CSV.
3. `session_errors()` returns the deduped errors and warnings of the session, Blueprint exceptions included, with callstacks.
4. Look at the contact sheet (`contact_sheet_path`) and the bracketing frames.
5. `test_scaffold`, then `test_run`, to lock the reproduction in and verify a fix.

## Actions

### Recording

| Action | Purpose |
|--------|---------|
| `record_arm` | Arm for the next PIE session, or the current one |
| `record_disarm` | Cancel the armed state |
| `record_stop` | Finalize immediately |
| `record_status` | State, id, frame count, elapsed time |
| `record_list` | List recordings, newest first |
| `record_read` | Read an artifact (`manifest`, `sequence`, `csv`, `drift`, `tracked`) |
| `record_delete` | Delete a recording (`confirm=true`) |
| `mark` | Insert a labelled marker into the active recording or replay |

Recording starts on BeginPIE and finalizes on EndPIE. It discovers Enhanced Input actions from event bindings and active mapping contexts, including actions added after the pawn attaches. Per frame it writes pawn location, rotation, velocity and `speed2d`, the active montage and section, every input action value with `_pressed` and `_released` edges, labelled markers, and per-frame performance (game, render and GPU ms, memory). Velocity comes from `UCharacterMovementComponent` when present, then `GetVelocity()`, physics velocity, and position delta as fallbacks, so any `APawn` works.

`record_arm` can also track reflection paths and subsystem values (`track_values`, with `sub:<Class>.<path>` for a subsystem), actor positions (`track_actors`), and skeletal bone or socket channels: `bone:<BoneOrSocket>.<channel>` in world space or `bonecs:<BoneOrSocket>.<channel>` in component space, with channels `x|y|z`, `pitch|yaw|roll` and `scalex|scaley|scalez`. Component space does not move when the actor does, which is what an animation assertion usually wants. A bone that the mesh does not have is absent from the row rather than sampled as zero.

| Param | Default | Notes |
|-------|---------|-------|
| `actions` | `[]` (all) | Whitelist of `UInputAction` asset paths |
| `axis_threshold` | `0.15` | Dead zone for edge detection |
| `sample_hz` | `60` | Sample rate, and the default `pin_fps` |
| `pin_fps` | `sample_hz` | `t.MaxFPS` pin; `0` skips it |
| `capture_pawn_state` | `true` | Per-row location, rotation, velocity, `speed2d` |
| `capture_montage` | `true` | Per-row `Montage:Section` |
| `client_id` | `0` | Local player index in the selected world |
| `pie_instance` | auto | See [Multiplayer targeting](#multiplayer-targeting) |
| `take_record` | `false` | Drive Take Recorder in lockstep |
| `rng_seed` | auto | Applied with `FMath::RandInit` after the pawn attaches |
| `run_gap_frames` | `6` | Gap tolerance for axis run extraction |
| `recording_dir` | `Saved/MCPRecordings/` | Recordings root |
| `id` | auto | Recording id |

`take_record=true` drives Take Recorder through reflection, so there is no link-time dependency on it. Without Take Recorder the recording continues on its own.

### Replay

| Action | Purpose |
|--------|---------|
| `replay_arm` | Arm for the next PIE session, or the current one |
| `replay_run` | Arm, start PIE, stop PIE when done |
| `replay_disarm` | Cancel the armed state |
| `replay_stop` | Stop and finalize the drift report |
| `replay_status` | State, step progress, elapsed time, drift maxima, `last_result` |

Replay drives the sequence through `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction` and teleports the pawn to its frame 0 position first. Re-arming in the same session stops the running replay, teleports again and restarts. The source is one of `recording_id`, `sequence_path` or inline `steps`.

| Param | Default | Notes |
|-------|---------|-------|
| `eject` | `false` | Fly a spectator pawn while inputs drive the original; possession returns on stop |
| `time_scale` | `1.0` | Global time dilation; `0.01` is 1% speed |
| `settle_ms` | sequence (500) | Delay after pawn attach before the first step |
| `pin_fps` | sequence `sample_hz` | `t.MaxFPS` pin; `0` skips it |
| `fixed_timestep` | `false` | Fixed `1/pin_fps` delta |
| `apply_rng_seed` | `true` | Reapply the sequence's seed |
| `record_drift` | `true` | Write `drift.json` when replaying a known recording |
| `auto_stop_pie` | `false` (`true` for `replay_run`) | Stop PIE when the sequence completes |
| `mode` | `"replay"` | `"monitor"` skips injection and keeps drift sampling, for playing by hand against a reference |
| `capture_frame_every` | `0` | Save a viewport frame every Nth frame |
| `encode_gif` | `false` | Also encode an animated GIF |
| `client_id`, `pie_instance` | `0`, auto | Which player to drive |
| `drift_thresholds` | `{ position_cm: 5, rotation_deg: 2, velocity_cms: 25 }` | Cutoffs for `frames_over_threshold`; also `tracked_default` and a per-path `tracked` map |

With `capture_frame_every` set, replay saves JPEG frames under `<recording>/frames/` and composes a labelled contact sheet at `<recording>/captures/contact_<timestamp>.jpg`, a grid of keyframes with the frame index on each cell. `replay_status.last_result` returns `frame_dir`, `frame_count` and `contact_sheet_path`. GIF output is opt-in because a vision model cannot read GIF animation. For a frame of the live PIE viewport on demand, use `editor(capture_screenshot, target="pie")` or `editor(capture_scene_png, world="pie")`.

Steps in `sequence.json`:

```json
{ "type": "input",      "delay_ms": 0,    "action": "/Game/Input/IA_Jump",   "value_x": 1.0 }
{ "type": "hold",       "delay_ms": 200,  "action": "/Game/Input/IA_Attack", "value_x": 1.0, "duration_ms": 100 }
{ "type": "input_tape", "delay_ms": 0,    "action": "/Game/Input/IA_Move",   "values": [[0.5, 0.3], [0.6, 0.3]] }
{ "type": "mark",       "delay_ms": 1500, "label": "enemy spawned" }
{ "type": "console",    "delay_ms": 6000, "command": "stat fps" }
{ "type": "capture",    "delay_ms": 5000, "name": "boss_intro" }
```

`delay_ms` is cumulative from sequence start, scheduled against time since pawn attach plus `settle_ms`. Tape values are `0.5` (Axis1D), `[0.5, 0.3]` (Axis2D) or `[0.1, 0.2, 0.3]` (Axis3D).

### Analysis and comparison

| Action | Purpose |
|--------|---------|
| `replay_analyze` | The lead from a finalized `drift.json`: first divergence, top channels, errors, bracketing images |
| `replay_state` | The exact recorded pawn pose at `at_time` or `at_frame`, interpolated; `apply=true` teleports the live pawn to it |
| `record_diff` | Compare two recordings without running PIE: position, rotation and velocity deltas plus `tracked_value_max_deltas` |
| `perf_summary` | Frametime avg, p50, p99 and max, game, render and GPU ms, peak memory, and the worst hitches with timestamps |

```text
pie(action="record_diff", a_id="run-1", b_id="run-2", position_cm=5, rotation_deg=2)
```

Unreal Insights traces are `editor(start_trace)` and `editor(stop_trace)`.

### Reproduction tests and scenarios

| Action | Purpose |
|--------|---------|
| `test_scaffold` | Write a `test.json` beside a recording: max drift and error assertions plus derived predicates |
| `test_run` | Replay the recording, then check drift, error count and predicates; returns `passed`, `failures` and the contact sheet |
| `test_list` | List scaffolded tests |
| `assert_eval` | Evaluate predicates over a finished run's series, errors and markers; writes `verdict.json` with a witness per assertion |
| `scenario_scaffold` | Generate an arrange, act, assert `scenario.json` from a recording |
| `scenario_validate` | Check a scenario's structure and predicates without running it |

A predicate binds a channel or event to an operator under a temporal quantifier: `{name, channel|event, op, value|min+max|tol, hold, from_s, to_s, within_s, after_event, severity}`. Operators are `eq`, `ne`, `lt`, `lte`, `gt`, `gte`, `approx`, `between`, `changed`, `increased`, `decreased` and `crossed`. `hold` is `always`, `never`, `eventually`, `at_end` or `once_then_always`. Channels are series columns (`pos_z`, `speed2d`, `game_ms`), tracked values by path (`t:Boss.Health`) and the pseudo-channels `$errors`, `$warnings`, `$frame` and `$time`. Events are `montage`, `marker` and `error`.

A scenario file's `act` list (kinds `replay`, `inject_tape`, `drive`, `goto`, `face`, `follow` and `call`) is written by `scenario_scaffold` and checked by `scenario_validate`, but no action runs it. Its arrange entries (`spawn`, `set`, `call`) run as a test script step's `arrange`, and `uat_run`'s step actions as its `act`. See [Arrange and act](pie-scripts.md#arrange-and-act).

### Observation profiles

| Action | Purpose |
|--------|---------|
| `profile_create` | Create a profile: tracked values, actors and widgets, pawn and montage sampling, drift thresholds |
| `profile_read` | Read a profile |
| `profile_update` | Update a profile |
| `profile_delete` | Delete a profile (`confirm=true`) |
| `profile_list` | List profiles |
| `observe_arm` | Attach a profile to the next PIE session |
| `observe_disarm` | Cancel the armed state |
| `observe_stop` | Stop and write output |
| `observe_status` | State, run id, frames sampled |
| `observe_list` | List observation runs |
| `observe_read` | Read an artifact (`manifest`, `csv`, `tracked`, `series`, `widgets`); `series` returns per-channel time series with min, max, first, last and `first_frame_over` |

A profile is an observation profile data asset holding tracked values (each with an optional drift threshold), tracked actors, pawn state and montage capture flags, and drift thresholds. Create one in the Content Browser (Miscellaneous > Data Asset), from the panel, or through the tool:

```text
pie(action="profile_create",
    name="CombatDebug",
    package_path="/Game/Observation",
    tracked_values=[{"path": "Hero.AbilitySystem.Health", "drift_threshold": 5.0}, "Hero.AbilitySystem.Stamina"],
    tracked_actors=["BP_Hero_C", "BP_Boss_C"])
```

Observation output goes to `Saved/MCPObservations/<run_id>/`: `manifest.json`, `observation.csv`, and `tracked.jsonl` when actors are tracked. One recording replayed with different profiles investigates different systems.

### Input injection

| Action | Purpose |
|--------|---------|
| `inject_input` | Inject one frame of an input action |
| `inject_input_start` | Begin a continuous hold; returns `injection_id` |
| `inject_input_update` | Change the value of a running hold |
| `inject_input_stop` | Release a hold or stop a tape |
| `inject_input_tape` | Play a per-frame value array at a given rate |

### Remote players and acceptance runs

| Action | Purpose |
|--------|---------|
| `clients` | List the session's players: net mode, the host player, and each remote player as `client` 1..N with `ready` and `log_file` |
| `console` | Run a console command in a remote player's process (`client` 1..N) |
| `remote_result` | Whether a relayed request was applied and, for tapes, finished |
| `uat_run` | Run a multiplayer acceptance scenario unattended |
| `uat_status` | State, current step, per-step results, verdict and `report_dir` |
| `uat_abort` | End the run early |

With Run Under One Process off, every client is a separate process the editor cannot reach directly. The tool attaches a replicated control component to each remote player's controller on the server and relays commands over the game connection. `inject_input`, `inject_input_start` and `inject_input_tape` take `client` 1..N; `inject_input_update` and `inject_input_stop` route by the returned id. `console` takes `client` 1..N only. The editor's own player console is `editor(execute_command, world="pie")`.

`uat_run` opens the scenario's map, starts PIE with its players, waits for every remote player to join, and runs each step: drive input or console on any player, then judge the step from the host log and each client's own log file. A dedicated server needs `one_process: true`, its default. When the run ends it stops PIE, restores the play settings and writes `report.md` and `report.json` to `report_dir`.

```json
{
  "name": "coop-attack",
  "map": "/Game/Maps/Test/L_CombatTest",
  "pie": { "players": 2, "net_mode": "listen", "one_process": false },
  "steps": [
    {
      "name": "P2 attacks",
      "do": [ { "client": 1, "press": { "action": "/Game/Input/IA_Primary.IA_Primary" } } ],
      "expect": [
        { "log": "client:1", "pattern": "\\[Attack\\]" },
        { "log": "host", "pattern": "plays Light01" }
      ],
      "forbid": [ { "log": "host", "pattern": "Error" } ],
      "window_ms": 4000
    }
  ]
}
```

Step actions, each with `client`: `tape {action, values}`, `press {action, frames}`, `hold {action, value, ms}`, `inject {action, value}`, `console "cmd"`, `place {at, offset, face}` or `place {location, yaw}`, and `capture` (a label or `true`); plus `wait_ms`. `place` teleports the player's pawn on the server, to the nearest free spot within 360 units when the exact one is taken, and turns it on its own machine; the step fails if the client never confirms the turn. `capture` saves that player's view as a JPEG, embedded next to its step in `report.md`.

`expect` rules take `min` and `max` match counts, and `since: "run"` judges every line since PIE started instead of only the step's own lines. A step passes when every expectation is met and no `forbid` pattern matched in its window. It ends early once its expectations are met unless it forbids something, in which case it watches the whole window.

`pie.windowless: true` launches separate-process clients with `-RenderOffscreen`: they render every frame as in a window but open no window and never take focus. Their frame rate is pinned to `pie.client_fps` (default 60).

### PIE inspection and session log

| Action | Purpose |
|--------|---------|
| `anim_state` | A PIE actor's anim instance state: montage and state machines |
| `anim_properties` | UPROPERTY values on a PIE actor's AnimInstance |
| `session_errors` | Deduped errors and warnings from the Output Log, Blueprint exceptions included |
| `session_log` | The paged raw log, filtered by verbosity, category or substring |

Both session actions default to the live or most recent session; pass `session` to read a finished one from `Saved/MCPSessions/`. Actor and subsystem properties are `editor(get_object_properties, world="pie")` (`target="subsystem"` with `subsystemClass`) and `editor(get_runtime_values)`.

## Multiplayer targeting

Actions that inject, record, replay or observe accept `pie_instance`, the exact `FWorldContext::PIEInstance`. `client_id` is the local player index inside that world. An explicit `pie_instance` fails when the instance is missing, is dedicated-server only, or lacks the requested local player; it never falls back. Values must be non-negative integers within the int32 range.

Without `pie_instance`, the tool prefers an eligible `GEditor->PlayWorld`, then the first PIE or Game world that has the requested local player, which avoids picking a dedicated server first. The selected world and player stay fixed for each recording, replay, observation, hold and tape. Unresolved recorder, replay and observation targets are retried for 10 seconds from BeginPIE, then finalize as failures with `last_error` in their status.

## Data layout

```text
<Project>/Saved/MCPRecordings/<recording-id>/
  manifest.json        recording metadata
  sequence.json        input sequence
  recording.csv        per-frame state
  drift.json           replay drift report
  test.json            reproduction test, after test_scaffold
  frames/              per-frame viewport JPEGs
  captures/            contact sheets, and GIFs when encode_gif=true

<Project>/Saved/MCPSessions/<timestamp>/
  session_log.jsonl    captured Output Log
  session_errors.json  deduped errors and warnings

<Project>/Saved/MCPObservations/<run_id>/
  manifest.json
  observation.csv
  tracked.jsonl
  results.json         test script results, for a script run
```

## Flows

Every action is a flow task named `pie.<action>`. See [Flows](flows.md).

```yaml
flows:
  slow_replay:
    description: Replay at 1% speed from a spectator camera
    steps:
      1:
        task: pie.replay_run
        options:
          recording_id: air-walk-bug
          eject: true
          time_scale: 0.01
```

## Migrating from pie-studio

The `pie` tool replaces the pie-studio plugin. When `ue-mcp.yml` still lists `pie-studio`, the server does not load it and logs a warning; `ue-mcp plugin install pie-studio` is refused. Remove it once, then rebuild the project:

```bash
npx ue-mcp plugin remove pie-studio
```

This uninstalls the npm package, deletes `Plugins/PIE_Studio` and removes the entry from `ue-mcp.yml`. Saved observation profiles keep loading through a class redirect, and recordings under `Saved/MCPRecordings` keep working. Action names and parameters are unchanged except these, which moved to core actions:

| pie-studio action | Core action |
|---|---|
| `trace_start`, `trace_stop` | `editor(start_trace)`, `editor(stop_trace)` |
| `capture` | `editor(capture_screenshot, target="pie")`, `editor(capture_scene_png, world="pie")` |
| `actor_spawn`, `actor_destroy` | `level(spawn_transient_actor, world="pie")`, `level(destroy_transient_actor)` |
| `actor_set`, `actor_call` | `editor(set_object_property)`, `editor(invoke_object_function)` |
| `snapshot`, `subsystem_state` | `editor(get_object_properties)` (`target="subsystem"` with `subsystemClass`), `editor(get_runtime_values)` |
| `console` on client 0 | `editor(execute_command, world="pie")` |
