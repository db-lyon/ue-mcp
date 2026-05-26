# PIE Record / Replay / Observe

Three independent systems for PIE sessions, each with its own domain:

1. **Record** - capture inputs and pawn state. Writes a replayable sequence.
2. **Replay** - play back a recorded sequence through the real Enhanced Input pipeline. Optionally eject to a spectator camera and slow time to 1%.
3. **Observe** - attach a reusable observation profile to any PIE session (recording, replay, or manual play) to sample tracked values and actors. Change what you watch without re-recording.

All three can run simultaneously in the same PIE session.

## Editor UI

### PIE toolbar

A **Record** button sits next to Play/Stop in the editor transport bar. Click it to arm the recorder and start PIE in one action.

### MCP PIE panel

Open **Window > Tools > MCP PIE** for a dockable panel with:

- **Recorder** - Record + Play, Arm, Disarm, Stop
- **Replayer** - Disarm, Stop
- **Observer** - Disarm, Stop
- **Time Scale** - slider (1% to 400%) and preset buttons (1%, 10%, 25%, 50%, 100%, 200%). Works during recording, replay, or manual play.
- **Recordings** - recordings list with one-click Replay
- **Observation Profiles** - profile list with one-click Observe

State labels turn green when active. Lists auto-refresh every 5 seconds.

## Quick start

```text
# Record
gameplay(action="pie_record_arm", sample_hz=60)
# Press Play, do your thing, stop PIE

# Replay at 10% speed with spectator camera
gameplay(action="pie_replay_arm",
         recording_id="air-walk-bug",
         eject=true,
         time_scale=0.1)
# Press Play - watch from a free camera while inputs replay in slow motion

# Observe with a debug profile (works with any of the above)
gameplay(action="pie_observe_arm",
         profile="/Game/Observation/CombatDebug")
```

## Recording

Records Enhanced Input actions and pawn state at a configurable sample rate. Discovers actions from both event bindings and active IMC mappings, including actions added after the initial pawn attach.

### What it captures

Per frame, into `<ProjectSavedDir>/MCPRecordings/<id>/`:

| Field | Source |
|-------|--------|
| `pos_x/y/z`, `rot_yaw/pitch/roll`, `vel_x/y/z`, `speed2d` | Player pawn. Velocity uses `UCharacterMovementComponent` when available, then `GetVelocity()`, physics linear velocity, and position-delta-over-dt as fallbacks. Works with any `APawn` subclass. |
| `montage` (`Name:Section`) | `AnimInstance::GetCurrentActiveMontage` (ACharacter only) |
| Per-`UInputAction` value | All actions in active IMCs, whether bound via `BindAction` or polled via `GetActionValue` |
| `<action>_pressed` / `_released` | Edge events computed against `axis_threshold` |
| Labelled markers | `pie_mark(label=...)` during recording |

### Artifacts

- `manifest.json` - metadata, action list, markers, file pointers
- `sequence.json` - replay-ready step list
- `recording.csv` - one row per frame

### Actions

| Action | Purpose |
|--------|---------|
| `pie_record_arm` | Arm for the next PIE session (or current) |
| `pie_record_disarm` | Cancel armed state |
| `pie_record_stop` | Finalize immediately |
| `pie_record_status` | State, id, frame count, elapsed time |
| `pie_record_list` | List recordings (newest first) |
| `pie_record_read` | Read an artifact (manifest, sequence, csv) |
| `pie_record_delete` | Delete a recording (requires `confirm=true`) |
| `pie_mark` | Insert a labelled marker into the active recording or replay |

### `pie_record_arm` parameters

| Param | Default | Notes |
|-------|---------|-------|
| `actions` | `[]` (all) | Whitelist of `UInputAction` asset paths. Empty records all discovered actions. |
| `axis_threshold` | `0.15` | Dead zone for edge detection |
| `sample_hz` | `60` | Sample rate. Also the default `pin_fps`. |
| `pin_fps` | `= sample_hz` | `t.MaxFPS` pin. `0` to skip. |
| `capture_pawn_state` | `true` | Per-row location/rotation/velocity/Speed2D |
| `capture_montage` | `true` | Per-row `Montage:Section` |
| `client_id` | `0` | Which local player to sample. See [Multi-client PIE](#multi-client-pie). |
| `take_record` | `false` | Drive Take Recorder in lockstep. See [Take Recorder](#take-recorder-integration). |
| `rng_seed` | auto | Applied via `FMath::RandInit` after pawn attach |
| `run_gap_frames` | `6` | Gap tolerance for axis run extraction |
| `recording_dir` | `Saved/MCPRecordings/` | Override recordings root |
| `id` | auto | Override the auto-generated recording id |

## Replay

Drives a recorded sequence through `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction`. Teleports the pawn to its frame-0 position before starting. Re-arming during the same PIE session auto-stops the previous replay, re-teleports, and restarts.

### Actions

| Action | Purpose |
|--------|---------|
| `pie_replay_arm` | Arm for the next PIE session (or current) |
| `pie_replay_disarm` | Cancel armed state |
| `pie_replay_stop` | Stop replay; finalize drift report if applicable |
| `pie_replay_status` | State, step progress, elapsed time, drift maxima |

### Source (one required)

- `recording_id` - loads sequence + CSV for drift comparison
- `sequence_path` - explicit path to a sequence.json
- `steps` - inline step array

### `pie_replay_arm` parameters

| Param | Default | Notes |
|-------|---------|-------|
| `eject` | `false` | Eject to a spectator pawn. Fly around freely while inputs drive the original pawn. Re-possesses on stop. |
| `time_scale` | `1.0` | Global time dilation. `0.01` for 1% speed, `0.1` for 10%. Caps auto-raised. Also controllable live from the panel slider. |
| `settle_ms` | sequence (500) | Delay after pawn attach before first step |
| `pin_fps` | sequence `sample_hz` | `t.MaxFPS` pin. `0` to skip. |
| `apply_rng_seed` | `true` | Reapply `FMath::RandInit` from the sequence |
| `record_drift` | `true` | Emit `drift.json` when replaying a known recording |
| `auto_stop_pie` | `false` | Stop PIE when sequence completes |
| `mode` | `"replay"` | `"monitor"` skips input injection, keeps drift sampling. Play manually against a reference. |
| `capture_frame_every` | `0` | Write a viewport PNG every Nth frame. See [Video capture](#per-frame-video-capture). |
| `client_id` | `0` | Which local player to drive |
| `drift_thresholds` | `{ position_cm: 5, rotation_deg: 2, velocity_cms: 25 }` | Cutoffs for `frames_over_threshold`. Also accepts `tracked_default` and per-path `tracked` map. |

## Observation profiles

Observation is decoupled from recording and replay. Define what to watch in a `UMCPObservationProfile` data asset, then attach it to any PIE session. The observer runs independently, owns its own frame sampler, and writes its own output.

### Why profiles

- Replay the same recording with different profiles to investigate different systems
- Change what you track without re-recording
- Edit in the content browser Details panel
- Share across team members via version control

### Creating a profile

**In the editor:** Right-click in Content Browser > Miscellaneous > Data Asset > `MCPObservationProfile`. Configure tracked values, actors, and thresholds in the Details panel.

**Through MCP:**

```text
gameplay(action="pie_profile_create",
         name="CombatDebug",
         package_path="/Game/Observation",
         tracked_values=[
           {"path": "Hero.AbilitySystem.Health", "drift_threshold": 5.0},
           "Hero.AbilitySystem.Stamina"
         ],
         tracked_actors=["BP_Hero_C", "BP_Boss_C"])
```

### Attaching to a session

```text
gameplay(action="pie_observe_arm",
         profile="/Game/Observation/CombatDebug")
# Press Play - observer samples alongside any recording or replay
```

Output goes to `Saved/MCPObservations/<run_id>/`:

- `manifest.json` - profile path, timing, tracked paths/actors
- `observation.csv` - per-frame samples
- `tracked.jsonl` - per-frame actor state (when tracked actors configured)

### Profile actions

| Action | Purpose |
|--------|---------|
| `pie_profile_create` | Create a profile data asset |
| `pie_profile_read` | Read profile config |
| `pie_profile_update` | Update an existing profile |
| `pie_profile_delete` | Delete (requires `confirm=true`) |
| `pie_profile_list` | List profiles |

### Observer actions

| Action | Purpose |
|--------|---------|
| `pie_observe_arm` | Attach a profile to the next PIE session |
| `pie_observe_disarm` | Cancel armed state |
| `pie_observe_stop` | Stop and write output |
| `pie_observe_status` | State, run id, frames sampled |
| `pie_observe_list` | List observation runs |
| `pie_observe_read` | Read an observation artifact (manifest, csv, tracked) |

## Drift and comparison

### Replay drift

When replaying a known `recording_id`, the replayer samples pawn state each frame and compares it to the source recording. The result is `drift.json` with per-frame deltas, max-drift stats, and a list of frames exceeding your thresholds.

### Offline diff

`pie_record_diff` compares two recordings without running PIE. Walks both CSVs in lockstep and emits position/rotation/velocity deltas plus `tracked_value_max_deltas` for reflection paths present in both.

```text
gameplay(action="pie_record_diff", a_id="run-1", b_id="run-2",
         position_cm=5, rotation_deg=2)
```

### One-shot snapshot

`pie_snapshot` dumps a live PIE actor's full UProperty state to JSON. Complements per-frame tracking with a deep one-time capture including components.

## Input injection

The replay system uses these internally. They are also useful on their own for scripting PIE inputs.

| Action | Purpose |
|--------|---------|
| `inject_input` | Single-frame injection |
| `inject_input_start` | Begin a continuous hold, returns `injection_id` |
| `inject_input_update` | Change the value of a running hold |
| `inject_input_stop` | Release a hold or stop a tape |
| `inject_input_tape` | Play a per-frame value array at a given Hz |

## Step types in `sequence.json`

```json
{ "type": "input",      "delay_ms": 0,    "action": "/Game/Input/IA_Jump",   "value_x": 1.0 }
{ "type": "hold",       "delay_ms": 200,  "action": "/Game/Input/IA_Attack", "value_x": 1.0, "duration_ms": 100 }
{ "type": "input_tape", "delay_ms": 0,    "action": "/Game/Input/IA_Move",   "values": [[0.5, 0.3], [0.6, 0.3]] }
{ "type": "mark",       "delay_ms": 1500, "label": "enemy spawned" }
{ "type": "console",    "delay_ms": 6000, "command": "stat fps" }
{ "type": "capture",    "delay_ms": 5000, "name": "boss_intro" }
```

`delay_ms` is cumulative from sequence start. The replayer schedules each step against elapsed time since pawn attach (after `settle_ms`).

`input_tape` values: `0.5` (Axis1D), `[0.5, 0.3]` (Axis2D), `[0.1, 0.2, 0.3]` (Axis3D). The parser accepts any arity.

## Per-frame video capture

Pass `capture_frame_every: N` to `pie_replay_arm`. Captures use direct `FViewport::ReadPixels()` with async PNG encoding on a background thread. Output goes to `<recording_dir>/frames/frame_<NNNNN>.png`.

Assemble with ffmpeg:

```bash
ffmpeg -framerate 30 -i <capture_dir>/frame_%05d.png -vf 'fps=30,scale=720:-1:flags=lanczos' replay.gif
ffmpeg -framerate 60 -i <capture_dir>/frame_%05d.png -c:v libx264 -pix_fmt yuv420p replay.mp4
```

## Multi-client PIE

`pie_record_arm`, `pie_replay_arm`, and `inject_input*` accept `client_id` (0 = first local player, 1+ = subsequent). Record multiple clients in one session by calling `pie_record_arm` once per `client_id` with different `id` values.

## Take Recorder integration

Pass `take_record: true` to `pie_record_arm` to drive Take Recorder in lockstep with PIE. Uses UFunction reflection so no link-time dependency on the Take Recorder plugin. If Take Recorder is unavailable, recording continues without it.

## Determinism

**Guaranteed:** same Enhanced Input values at the same frame numbers, same `t.MaxFPS` pin, same `FMath::RandInit(seed)`, CPU throttle suppressed for the session duration.

**Not guaranteed:** particles, Niagara, AI behavior trees, physics, anything using its own `FRandomStream` or real wall-clock time.

**What you get instead:** `drift.json` turns "did the bug reproduce" from a guess into a number.

## Composition with flows

Every action is available as a flow task. See [Flows](flows.md).

```yaml
flows:
  record:
    description: Record at 60Hz and start PIE
    steps:
      1:
        task: gameplay.pie_record_arm
        options:
          sample_hz: 60
      2:
        task: editor.play_in_editor
        options:
          pieAction: start

  slow_replay:
    description: Replay at 1% speed with spectator camera
    steps:
      1:
        task: gameplay.pie_replay_arm
        options:
          recording_id: air-walk-bug
          eject: true
          time_scale: 0.01
          auto_stop_pie: true
      2:
        task: editor.play_in_editor
        options:
          pieAction: start
```
