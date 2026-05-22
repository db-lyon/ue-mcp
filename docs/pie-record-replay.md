# PIE Record / Replay

A first-class **input recording and replay** system for Play-In-Editor sessions, exposed via the `gameplay` category.

Use it to:

- Record every Enhanced Input value and pawn state at 60 Hz while you play
- Replay the same inputs through the real Enhanced Input pipeline
- Diff the replay against the source for a drift report - "did the bug actually reproduce"

## What it captures

Per frame, into `<ProjectSavedDir>/MCPRecordings/<id>/`:

| Field | Source |
|-------|--------|
| `pos_x/y/z`, `rot_yaw/pitch/roll`, `vel_x/y/z`, `speed2d` | First player pawn |
| `montage` (`Name:Section`) | `AnimInstance::GetCurrentActiveMontage` |
| Per-`UInputAction` value | `UEnhancedPlayerInput::GetActionValue` |
| `<action>_pressed` / `_released` edge events | Computed against `axis_threshold` |
| Dotted reflection paths you ask for | `track_values=["Hero.AbilitySystem.Health"]` |
| Tracked world actors (pos/rot/vel) | `track_actors=["BP_Hero_C", ...]`; writes `tracked.jsonl` |
| Labelled markers | `pie_mark(label=...)` while recording |

Artifacts:

- `manifest.json` - schema metadata, action list, markers, file pointers
- `sequence.json` - replay-ready step list (`input_tape` / `hold` / `mark` / `console` / `capture`)
- `recording.csv` - one row per frame, self-describing `#` comment header
- `tracked.jsonl` (optional) - per-frame state of `track_actors`; one JSON object per line keyed by user-supplied id
- `drift.json` (replay only) - per-frame deltas vs source, including `actor_drift` per tracked actor when both sides emitted `tracked.jsonl`

## Quick start

```text
# 1. Arm a recording for the next PIE session
gameplay(action="pie_record_arm", sample_hz=60,
         track_values=["Hero.AbilitySystem.Health"])

# 2. Press Play in the editor (or call editor(play_in_editor, pieAction="start"))
# 3. Play your scenario for as long as you want; stop PIE
# 4. List recordings
gameplay(action="pie_record_list")

# 5. Replay it (writes drift.json next to the recording)
gameplay(action="pie_replay_arm", recording_id="recording-20260521-143052-7af3")
# Press Play again; the inputs replay automatically.

# 6. Read the drift report
gameplay(action="pie_record_read", id="recording-20260521-143052-7af3", file="drift")
```

## Action surface

### Recording

| Action | Purpose |
|--------|---------|
| `pie_record_arm` | Arm a recording for the next BeginPIE (or current PIE) |
| `pie_record_disarm` | Cancel armed state. Errors if a recording is in flight |
| `pie_record_stop` | Finalize the in-flight recording immediately |
| `pie_record_status` | `{ state, id, current_frame, elapsed_seconds, tracked_action_count }` |
| `pie_record_list` | Enumerate `Saved/MCPRecordings/*` newest first |
| `pie_record_read` | Return one of `manifest` / `sequence` / `csv` / `drift` |
| `pie_record_delete` | Delete a recording dir (requires `confirm=true`) |
| `pie_mark` | Insert a labelled marker into the in-flight recording/replay |

#### `pie_record_arm` parameters

| Param | Default | Notes |
|-------|---------|-------|
| `actions` | `[]` (all bound) | Whitelist of `UInputAction` asset paths |
| `track_values` | `[]` | Dotted reflection paths sampled to doubles per frame |
| `track_actors` | `[]` | World actor ids (name, class name, or full path; first match wins) sampled per frame. Writes `tracked.jsonl` (one JSON object per line) with `{ frame, time, actors: { "<id>": { resolved, pos, rot, vel } } }`. Joined to `recording.csv` by frame index. |
| `axis_threshold` | `0.15` | Dead zone for axis edge detection |
| `sample_hz` | `60` | Sample rate. Also the default `pin_fps` |
| `pin_fps` | `= sample_hz` | `t.MaxFPS` pin during recording. `0` to skip |
| `capture_pawn_state` | `true` | Per-row location/rotation/velocity/Speed2D |
| `capture_montage` | `true` | Per-row `Montage:Section` |
| `rng_seed` | auto | Reapplied via `FMath::RandInit` after pawn attach |
| `run_gap_frames` | `6` | Gap tolerance for axis run extraction in `sequence.json` |
| `recording_dir` | `Saved/MCPRecordings/` | Override the recordings root |
| `id` | auto | Override the auto-generated `recording-<timestamp>-<short>` id |

### Replay

| Action | Purpose |
|--------|---------|
| `pie_replay_arm` | Arm a replay for the next BeginPIE (or current PIE) |
| `pie_replay_disarm` | Cancel armed state |
| `pie_replay_stop` | Stop the running replay; finalize `drift.json` if applicable |
| `pie_replay_status` | `{ state, current_step, total_steps, elapsed_seconds, max_position_drift_cm, max_velocity_drift_cms }` |

#### `pie_replay_arm` parameters

`mode` accepts `replay` (default) or `monitor`. In `monitor` mode the replayer
skips input injection / step execution but keeps the per-frame drift sampler
running, so a human can play the same scenario manually and watch divergence
live via `pie_replay_status`. Combine with `pie_record_arm` on a separate run
to get a fresh recording while monitoring an existing reference.

Source (one required):

- `recording_id` - loads `<root>/<id>/sequence.json` + `recording.csv` (drift enabled)
- `sequence_path` - explicit path to a sequence.json
- `steps` - inline step array (same schema as sequence.json)

Tunables:

| Param | Default | Notes |
|-------|---------|-------|
| `settle_ms` | sequence value (500) | Delay after pawn attach before first step |
| `pin_fps` | sequence `sample_hz` | `t.MaxFPS` pin during replay. `0` to skip |
| `apply_rng_seed` | `true` | Reapply sequence `rng_seed` via `FMath::RandInit` |
| `record_drift` | `true` (when `recording_id`) | Emit `drift.json` |
| `auto_stop_pie` | `false` | Stop PIE on sequence completion |
| `drift_thresholds` | `{ position_cm: 5, rotation_deg: 2, velocity_cms: 25 }` | Cutoffs for the `frames_over_threshold` list. Also accepts `tracked_default` (scalar fallback applied to every tracked path) and `tracked: { "<path>": <threshold> }` for per-path overrides. `0` keeps tracked-value deltas out of `frames_over_threshold`. Max per-path deltas are always reported in `drift.json#tracked_value_max_deltas` regardless of thresholds. |

### Input injection primitives

Used internally by the replayer; exposed because they are useful on their own.

| Action | Purpose |
|--------|---------|
| `inject_input` | One-frame `InjectInputForAction` |
| `inject_input_start` | Begin a continuous hold, returns `injection_id` |
| `inject_input_update` | Change the value of a running hold |
| `inject_input_stop` | Release a hold or stop a tape |
| `inject_input_tape` | Play a per-frame value array, one entry per end-of-frame |

### Multi-client PIE

`pie_record_arm`, `pie_replay_arm`, and the `inject_input*` primitives accept a `client_id` parameter selecting which local player to sample / replay / inject into. `0` (default) is the first local player; `1+` selects subsequent local players in multi-client PIE sessions.

To record both clients in one session, call `pie_record_arm` twice with different `client_id` and `id` values - the recorder writes one recording directory per client. Replay each recording back with its matching `client_id`. The injector caches subsystems per client per tick so concurrent holds / tapes across clients don't trample each other.

The manifest's `client_id` field records which local player produced the recording.

### Take Recorder integration

Pass `take_record: true` to `pie_record_arm` to also drive Take Recorder `StartRecording` / `StopRecording` in lockstep with BeginPIE / EndPIE. The integration uses UFunction reflection so the bridge does not link against the Take Recorder plugin; if Take Recorder is not enabled or its panel is not open, the recorder logs a diagnostic and continues without it (the input recording itself is unaffected).

Workflow:

1. Enable the Take Recorder plugin (Edit > Plugins > "Take Recorder").
2. Open the Take Recorder panel (Window > Cinematics > Take Recorder) and configure sources (player pawn, actors of interest, presets).
3. Call `pie_record_arm(take_record: true, ...)`.
4. Press Play. The bridge starts the Take in lockstep with PIE; on EndPIE it calls `StopRecording`.
5. `pie_record_stop` returns `take_recorder_status` with the outcome.

The Take asset path is governed by Take Recorder's own settings (`/Game/Cinematics/Takes/...` by default) and is not written into our manifest.

### One-shot actor snapshot

`pie_snapshot(target, recording_id?, name?, include_components?)` dumps the live state of a PIE actor to JSON in one call. Unlike `track_actors` (per-frame pos/rot/vel sampling), this captures every `BlueprintVisible` `UProperty` plus an optional component dump. Output lands at `<recording_dir>/<recording_id>/snapshots/<name>.json` when a `recording_id` is supplied, otherwise `Saved/MCPSnapshots/<name>.json`.

### Offline diff

`pie_record_diff(a_id, b_id, position_cm?, rotation_deg?, velocity_cms?, tracked_default?, tracked_thresholds?)` walks two `recording.csv` files in lockstep by frame index and emits a single drift summary. Reflection paths sampled in both recordings show up in `tracked_value_max_deltas` (max |delta| per path). Pass `tracked_default` or `tracked_thresholds` to fold tracked-value drift into `frames_over_threshold`. No PIE required.

## Step types in `sequence.json`

```json
{ "type": "input",      "delay_ms": 0,    "action": "/Game/Input/IA_Jump",   "value_x": 1.0 }
{ "type": "hold",       "delay_ms": 200,  "action": "/Game/Input/IA_Attack", "value_x": 1.0, "duration_ms": 100 }
{ "type": "input_tape", "delay_ms": 0,    "action": "/Game/Input/IA_Move",   "values": [[0.5, 0.3], [0.6, 0.3]] }
{ "type": "mark",       "delay_ms": 1500, "label": "enemy spawned" }
{ "type": "console",    "delay_ms": 6000, "command": "stat fps" }
{ "type": "capture",    "delay_ms": 5000, "name": "boss_intro" }
```

`capture` writes a viewport screenshot via `FScreenshotRequest::RequestScreenshot`. When replaying a known `recording_id` the file lands in `<recording_dir>/captures/<name>_frame<N>.png`; inline-steps replays write to `Saved/Screenshots/MCPReplay/`. The drift entry records the full path under the `capture:<name>:<path>` marker.

### Per-frame video capture

Pass `capture_frame_every: N` to `pie_replay_arm` to also write a viewport screenshot every Nth sampled frame to `<recording_dir>/frames/frame_<NNNNN>.png`. `pie_replay_stop` returns `capture_dir` plus ffmpeg hints for assembling the PNG sequence into a GIF or MP4:

```bash
# GIF
ffmpeg -framerate 30 -i <capture_dir>/frame_%05d.png -vf 'fps=30,scale=720:-1:flags=lanczos' replay.gif
# MP4
ffmpeg -framerate 60 -i <capture_dir>/frame_%05d.png -c:v libx264 -pix_fmt yuv420p replay.mp4
```

Override `-framerate` to match the replay's `pin_fps` when you used a non-default rate.

`delay_ms` is **cumulative from sequence start**. The replayer schedules each step against the elapsed time since pawn attach (after `settle_ms`).

`input_tape` values per element:

- `0.5` - scalar (Axis1D)
- `[0.5, 0.3]` - 2-tuple (Axis2D)
- `[0.1, 0.2, 0.3]` - 3-tuple (Axis3D)

The recorder writes the canonical arity that matches the action's value type, but the parser accepts any of the three.

## Determinism: what we promise, what we don't

**We promise:**

- Same Enhanced Input values delivered at the same frame numbers as the source
- Same `t.MaxFPS` pin on both record and replay
- Same `FMath::RandInit(seed)` applied at the same lifecycle point

**We do not promise:**

- Identical particles, Niagara behaviour, AI behaviour-tree outcomes, physics
- Anything that uses its own `FRandomStream`, reads real wall-clock, or branches on multi-thread ordering

**What we give you instead:** a per-frame `drift.json` so divergence is *visible*. The drift report lists frames whose position / rotation / velocity deltas exceeded your thresholds, with max-drift summary stats.

This sets correct expectations and turns "did my bug reproduce" from a guess into a number.

## Composition with flows

The flow engine auto-registers every action, so the canonical recipe composes built-ins with no new built-in flow files:

```yaml
tasks:
  arm:
    type: gameplay.pie_record_arm
    options:
      sample_hz: 60
      track_values: ["Hero.AbilitySystem.Health"]
  play:
    type: editor.play_in_editor
    options:
      pieAction: start
    needs: [arm]
```

For a full example see `plans/pie-record-replay.md` in the repo (gitignored design doc).

## Known limitations (Phase 1)

These are tracked as follow-ups, not present in the first ship:

