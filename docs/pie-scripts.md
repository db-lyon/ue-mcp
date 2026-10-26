# Test scripts

A test script is an ordered list of steps run against a Play In Editor session. Each step is an instruction for the person playing, and either an expectation the bridge checks against observed values (`judge: "auto"`) or a question the person answers (`judge: "manual"`). A run arms the observer and the recorder under one run id, so a finished run can be replayed later and its auto steps checked again against the same input.

Every action on this page is in the `pie` category.

## Steps

```json
{
  "instruction": "Walk up to the door",
  "judge": "auto",
  "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 }
}
```

| Field | Type | Meaning |
|---|---|---|
| `instruction` | string | Required. What the person does, or what they check. Shown on the verdict form for a manual step. |
| `judge` | `auto` or `manual` | Default `auto`. `auto`: the bridge decides from `expect`. `manual`: the step waits for a verdict. |
| `agent_judgeable` | boolean | Manual steps only, default `true`. `false` means only a person can give the verdict: `verdict` refuses `source: "agent"` for the step. Refused on an auto step. |
| `resolve_in_pie` | boolean | Default `false`, and only on a step with `expect`, `arrange` or `act`. `true` means `expect.value`, and the actors `arrange` and `act` name, only exist once PIE is running, such as an actor spawned during play, so `script_create` and `script_update` do not check them against the level. |
| `arrange` | array | Optional. Run when the step starts: a scenario's arrange entries, `{kind: "spawn", class, at?, rotation?, scale?}`, `{kind: "set", target, path, value}` or `{kind: "call", target, func, args?}`. |
| `act` | array | Optional. Run after `arrange`: the step actions `uat_run` takes in a step's `do` (see [Arrange and act](#arrange-and-act)). |
| `expect.value` | string | Required in `expect`. For `decreasing`, `increasing`, `eq`, `gt` and `lt`: a tracked value path, or its last segment. For `exists`: an actor id. For `visible`: a widget class or name. |
| `expect.op` | string | Required in `expect`. One of the ops below. |
| `expect.target` | number or boolean | Required for `eq`, `gt` and `lt`. Optional for `visible` and `exists`, default `true`; `false` asks for the opposite. Refused for `decreasing` and `increasing`. A boolean given to `eq`, `gt` or `lt` is read as 1 or 0. |
| `expect.within_ms` | integer | How long the step has to meet its expectation, in game milliseconds from the moment it starts, or from the end of its `act` when it has one. 0 or absent uses the run's `default_within_ms`. |

A manual step takes no `expect`, and a step that has one is refused. An auto step without `expect` passes as soon as it starts (once its `act` ends, when it has one), with `evidence.reason` set to `no expectation`.

A value the profile does not track is added to the observer for the run, so a script can name values, actors and widgets that no profile lists.

### Ops

An auto step is checked on every observed frame from the frame it starts. It passes on the first frame its condition holds, and fails on the first frame that is `within_ms` or more after it started. A frame where the value cannot be read does not count as holding.

| Op | Holds when |
|---|---|
| `decreasing` | The value is lower than the first value observed in the step. |
| `increasing` | The value is higher than the first value observed in the step. |
| `eq` | The value is within 0.0001 of `target`, or within 0.01% of the larger of the two magnitudes when that is more. |
| `gt` | The value is greater than `target`. |
| `lt` | The value is less than `target`. |
| `visible` | The widget is visible (or not, with `target: false`). |
| `exists` | The actor resolves in the PIE world (or does not, with `target: false`). |

Auto steps run one at a time. The next step starts on the frame the previous one settled, so the baseline of a `decreasing` or `increasing` step is the value at that frame, or at the frame its `act` ended.

### Arrange and act

A step can set up and drive what it then checks, so a script runs end to end with nobody at the controls:

```json
{
  "instruction": "Open the door",
  "arrange": [{ "kind": "set", "target": "Door", "path": "OpenAngle", "value": 0 }],
  "act": [
    { "place": { "at": "Door", "offset": [-150, 0, 0] } },
    { "press": { "action": "/Game/Input/IA_Interact.IA_Interact" } }
  ],
  "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80, "within_ms": 3000 }
}
```

- **Order.** `arrange`, then `act`, then the `expect` window opens. `within_ms` counts from the end of `act`. The run holds at the step until its act ends, so the next step never starts in the middle of one.
- **arrange** entries are a scenario's, applied by the same code `scenario_validate` checks them with: `spawn` a class (native or a Blueprint path, with or without `_C`), `set` a property on an actor by dotted path, `call` a function on an actor. An actor is named by label, name or class.
- **act** entries are the step actions `uat_run` runs, through the same executor: `place` (`{at, offset?, face?}` next to an actor, facing it, or `{location, yaw?}`), `press` (`{action, frames?}`), `inject` (`{action, value}`), `hold` (`{action, value, ms}`), `tape` (`{action, values}`), `console`, `capture` (a label or `true`; the file lands under `<output_dir>/captures/`) and `wait_ms`. `client` (default 0, the editor's own player) sends an entry to a remote player. An act ends once every entry ran, every hold and tape on the editor's player played out, and every remote player confirmed what it was sent.
- **A manual step may carry `act`**: the engine sets the scene, then the step asks for its verdict. A manual step with `act` starts its own block of manual steps.
- **An act that fails fails its step**, with the reason in `evidence.reason`, and the run moves on.
- `script_create`, `script_update` and `script_validate` resolve the actor every `set`, `call` and `place` names, the class every `spawn` names and the Input Action every `press`, `inject`, `hold` and `tape` names, and refuse one that does not exist with the field at fault, such as `act[1].press.action` or `arrange[0].target`, and candidates. `resolve_in_pie: true` lets an actor that only exists in PIE through; classes and Input Actions are always checked.
- A [manual run](#manual-runs) has no PIE and a [replay](#replay-regression) replays the recorded input, so neither runs `arrange` or `act`. The step's `act_result` says so.
- What the act did is in the step's `act_result` and on its `step_acted` event: `{status, reason?, arranged, actions: [{index, action, ok, error?, at_ms}], started_at_ms, ended_at_ms, duration_ms, wall_ms, captures?}`. `status` is `running`, `done`, `failed` or `skipped`. Times are game milliseconds, except `wall_ms`.

## Storing scripts

Scripts are data assets, saved under `/Game/PIEStudio/Scripts` unless you give another package path. A script holds `steps`, an optional `description`, an optional `profile` (the observation profile asset the run arms), an optional `map` (the level it plays in) and `default_within_ms`.

| Action | Params |
|---|---|
| `script_create` | `name`, `steps`, `package_path?`, `description?`, `profile?`, `map?`, `default_within_ms?` (default 5000), `manual_only?` (default `false`), `onConflict?` (`skip` (default), `update`, `replace` or `error`) |
| `script_read` | `path`. Returns `path`, `name`, `description`, `profile`, `map`, `default_within_ms`, `manual_only`, `steps` and `step_count`. |
| `script_update` | `path`, and any of `steps`, `description`, `profile` (empty clears it), `map` (empty clears it), `default_within_ms`, `manual_only` |
| `script_validate` | `steps`, `profile?`, `map?`, `manual_only?`. The checks `script_create` runs, without saving: `valid`, `errors` in the refusal's shape, `summary` and the parsed `steps`. |
| `script_values` | `target?`, `profile?`, `limit?` (default 400). What a script can name in the open level: `pawn` (`class`, `source`, `components`), `values` (`path`, `kind` number, integer, boolean or enum, `type`, and an enum's `values`), `actors`, `spawned_classes`, `widgets`, `profile_values`, and with `target` a `target` object (`found`, `class`, `is_player_pawn`, `actors` of that class). |
| `script_delete` | `path`, `confirm=true` |
| `script_list` | `directory?` (default `/Game`). Returns `scripts` (`path`, `object_path`, `name`, `map`, `last_run`) and `count`. `last_run` is the script's newest run as [`script_runs`](#listing-runs) lists it, without its steps, or `null` when it never ran. |

A profile that does not exist is refused, and so is a `map` that is not a level asset (`field: "map"`, with the closest levels as candidates). `map` takes a package path such as `/Game/Maps/L_Arena` or its object path `/Game/Maps/L_Arena.L_Arena`. `steps` must hold at least one step.

### Checks when a script is saved

`script_create` and `script_update` refuse a step that a run could never check, so a typo in a value name is caught when the script is written rather than as a failed step later. `script_update` checks the script as it would stand after the update whenever `steps`, `profile`, `map` or `manual_only` changes. When the script's `map` is not the level open in the editor, the actors its steps name are left for the run to resolve, since they live in that level.

- **The value has to resolve**, the way the observer reads it, against the level open in the editor and the script's profile. A numeric op reads a profile tracked value (its path or last segment), a property path on the player pawn (the pawn player 0 possesses in the level, or the game mode's `DefaultPawnClass`), a component's property (`CollisionCylinder.CapsuleHalfHeight`), `sub:<Subsystem>.<path>`, or `bone:<bone>.<channel>`. `exists` needs an actor in the level, or a class the game mode spawns. `visible` needs a widget class, a Widget Blueprint, or a widget inside one.
- **The op and target have to suit the value's type.** A bool takes only `eq` with `true` or `false`, an enum only `eq` with one of its values, and a number a number target. An integer is refused an `eq` target it cannot hold, and a `gt` or `lt` target it can never pass.
- **A script needs an auto step with an `expect`.** A script judged entirely by verdicts says so with `manual_only: true`.

- **What arrange and act name has to exist**: the actors, actor classes and Input Actions ([Arrange and act](#arrange-and-act)).

A value that only exists once PIE is running, such as an actor spawned during play, is let through by `resolve_in_pie: true` on its step. The step's op and target are still checked as far as they can be without a type.

The refusal lists every problem, each with the step index, the field, a message and the closest valid candidates:

```json
{
  "success": false,
  "errorCode": "INVALID_SCRIPT_STEPS",
  "error": "The script cannot be checked as written (1 problem): steps[1].expect.value: 'BaseEyeHieght' cannot be read: DefaultPawn has no property or component named BaseEyeHieght (the observer reads it from the player pawn, DefaultPawn, the DefaultPawnClass of GameModeBase). If it only exists once PIE is running, set resolve_in_pie: true on the step.",
  "errors": [
    { "step": 1, "field": "expect.value", "message": "steps[1].expect.value: 'BaseEyeHieght' cannot be read: DefaultPawn has no property or component named BaseEyeHieght (the observer reads it from the player pawn, DefaultPawn, the DefaultPawnClass of GameModeBase). If it only exists once PIE is running, set resolve_in_pie: true on the step.", "candidates": ["BaseEyeHeight", "BaseTurnRate", "BaseLookUpRate", "bHidden", "bHiddenEd"] }
  ]
}
```

`step` is `null` for a problem with the whole script, such as no auto step (`field: "steps"`, candidates `["manual_only"]`) or a missing profile (`field: "profile"`). Inline steps given to `script_run` are not checked this way.

`script_validate` runs the same checks and answers instead of refusing, so a client can draft in a loop, and `script_values` lists the values the checks accept. The built-in `playtest` flow drafts a script with a model this way; see [Flows](flows.md#playtest).

```text
pie(action="script_create", name="DoorOpens", profile="/Game/PIEStudio/Profiles/Door",
    steps=[
      {"instruction": "Walk up to the door", "judge": "auto",
       "expect": {"value": "Door.Distance", "op": "decreasing", "within_ms": 3000}},
      {"instruction": "Open the door", "judge": "auto",
       "expect": {"value": "Door.OpenAngle", "op": "gt", "target": 80}},
      {"instruction": "The door opens without a visible pop", "judge": "manual"}
    ])
```

## Running a script

```text
pie(action="script_run", script="/Game/PIEStudio/Scripts/DoorOpens", start_pie=true, wait=true)
```

`script_run` takes a stored script (`script`) or inline `steps`, exactly one of them. The editor runs one script at a time; `script_run` is refused while another run is active.

| Param | Default | Meaning |
|---|---|---|
| `script` or `steps` | | The script asset path, or the steps themselves. |
| `name` | the asset name, or `inline` | The run's name. Also overrides the asset name for a stored script. |
| `profile` | the script's | The observation profile to arm. |
| `map` | the script's | The level to play in. When another level is open, the run opens this one before it arms anything, and is refused when the open level has unsaved changes or PIE is running in another level. With no map the run plays whatever level is open. `mode=manual` leaves the open level as it is. |
| `record` | `true` | Arm the recorder too, so the run can be replayed. With `record=false` the run has no recording and cannot be replayed. |
| `start_pie` | `false` | Start PIE when it is not running. Otherwise the run waits for the next PIE session. |
| `block_on_manual` | `true` | Hold the run at a manual step until it has a verdict. Consecutive manual steps are put up together and the run holds until each has one. `false` starts each manual step and moves on. |
| `mode` | `full` | `manual` judges the script by its manual steps with no PIE. See [Manual runs](#manual-runs). |
| `default_within_ms` | the script's, or `5000` | The window for an auto step that names none. |
| `pie_instance` | the active PIE world | Which PIE world to observe. |
| `client_id` | `0` | Which local player in that world. |
| `wait` | `false` | Hold the answer until the run finishes, stops, or needs a verdict that nobody was asked for. |
| `timeout_seconds` | `600` | How long `wait` holds the answer. The run keeps going after it. |

### Run ids and files

A run id is `script_<name>_<YYYYMMDD_HHMMSS>`, from the editor's local time, for example `script_DoorOpens_20261023_100000`. Characters other than letters, digits, `-` and `_` in the name become `_`. When a folder of that id already exists, `_2`, `_3` and so on are appended. A replay run's id is `script_replay_<name>_<YYYYMMDD_HHMMSS>`.

The observation and the recording use the run id as their ids, so `observation_run_id` and `recording_id` equal `run_id`. Paths in answers are absolute:

- `output_dir`: `<Project>/Saved/MCPObservations/<run_id>`, holding the observation files and `results.json`.
- `results_path`: `<output_dir>/results.json`.
- `recording_dir`: `<Project>/Saved/MCPRecordings/<run_id>`.

### The answer

`script_run` answers with the run as it stands when armed: `run_id`, `script` (the asset path, or `inline`), `script_name`, `state` (`armed`), `current_step`, `output_dir`, `results_path`, `observation_run_id`, `recording_id` (absent with `record=false`), `elapsed_ms`, `steps`, `awaiting`, `events`, `last_seq`, `summary`, `pie_started`, `pie_running`, `map` (when the run has one) and `message`.

The run moves to `running` on the observer's first frame. All step and event times (`started_at_ms`, `ended_at_ms`, `at_ms`, `elapsed_ms`) are game milliseconds from that frame, the clock the observer and the replayer share.

With `wait=true` the answer is merged with the run's final status, without `events`, and carries a `wait` object:

```json
{
  "run_id": "script_DoorOpens_20261023_100000",
  "state": "finished",
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0, "awaiting": 0, "human_verdicts": 1, "agent_verdicts": 0 },
  "wait": { "state": "finished", "durationMs": 41250, "elicitation": "accepted" }
}
```

`wait` holds `state`, `durationMs`, `awaiting` (step indices, with `awaiting_verdict`), `elicitation` (how the last verdict form went: `accepted`, `declined`, `cancelled`, `auto_answered`, `failed` or `unavailable`) and `error` (with `timed_out` or `unreachable`). `wait.state` is one of:

| State | Meaning |
|---|---|
| `finished` | The run ended: every step settled, PIE ended, or the observation ended. |
| `stopped` | `script_stop` ended the run, or the editor no longer holds it. |
| `awaiting_verdict` | Manual steps wait for `pie(verdict)` and no form will ask for them. The answer carries `verdictHint`. |
| `timed_out` | `timeout_seconds` ran out. The run is still going. |
| `unreachable` | The editor failed five status calls in a row, or the six hour follow limit ran out. |

### Status

`script_status` (`run_id?`, `since_seq?`, `wait?`, `timeout_seconds?`) reports a run with the same fields as the `script_run` answer, plus `active`. Run states are `armed`, `running`, `awaiting_verdict`, `finished`, `stopped` and `idle`. Step statuses are `pending`, `running`, `passed`, `failed`, `awaiting_verdict` and `skipped`.

The editor holds one run in memory: the active one, or the last one to end. An older run id answers from that run's `results.json`, events included, with `live: false`. A run id with no `results.json`, and any call before the first run, answers `state: "idle"` with empty `steps`, `awaiting` and `events`, and `last_seq` equal to `since_seq`.

`awaiting` lists the steps waiting for a verdict as `{ index, instruction }`. `since_seq` returns only the events after that number, so a caller that polls passes the `last_seq` it last saw. Each event is `{ seq, type, run_id, step, at_ms, wall_time, data }`, where `step` is the step index or `-1`. The editor's event types are `run_started`, `step_started`, `step_passed`, `step_failed`, `awaiting_verdict`, `verdict` and `run_finished`.

`wait` and `timeout_seconds` work as they do on `script_run`, and the merged answer keeps `events` after `since_seq`. Calling `script_status` on a run that has not ended also makes the server follow it again, so its events reach the daemon stream after a server restart.

### How a run ends

A run finishes with `stop_reason` `all steps settled` once every step has passed, failed or been skipped. It also finishes when PIE ends (`PIE ended`) or the observation ends (`observation ended`). An auto step still running then fails with `run ended before the step settled (<reason>)`. Pending steps stay `pending` and manual steps without a verdict stay `awaiting_verdict`.

`script_stop` (`run_id?`, `reason?`) ends the active run the same way with state `stopped` and `stop_reason` set to `reason` (default `stopped`), writes `results.json`, and answers with the run's fields, `stopped: true` and `changed: true`. A `run_id` that is not the active run is refused. With no active run the answer has `success: false`, `alreadyStopped: true`, and the last run's `run_id`, `state` and `results_path`.

## Manual runs

```text
pie(action="script_run", script="/Game/PIEStudio/Scripts/DoorOpens", mode="manual")
```

A manual run starts no PIE, observer or recording. Every manual step awaits its verdict from the start and takes it in any order through `verdict`; auto steps stay `pending` and the summary counts them under `unchecked`. The run finishes once every manual step has a verdict, and writes `results.json` with `mode: "manual"`. A full run started later on the same script is a new run with no verdicts. Answers, `results.json` and every event carry `mode`. With no PIE, no step's `arrange` or `act` runs; each step that has them carries `act_result` with `status: "skipped"` and the reason.

## Manual steps and verdicts

When the run reaches manual steps, the server puts them to the person in one form, if the MCP client advertises the elicitation capability. Each step has a choice of `pass`, `fail` or `skip` and an optional note. The answers are posted as the steps' verdicts, with `verdict_source: "elicitation"`.

The form is asked once per step. When the client cannot show it, or the person declines or cancels it, or the client answers faster than a person could (`UE_MCP_ELICIT_MIN_HUMAN_MS`, default 1000), the steps are left for the `verdict` action and are not asked about again in that run. A step left blank on an accepted form is also left for `verdict`.

```text
pie(action="verdict", step=2, verdict="fail", note="Pops on the second frame")
```

| Param | Meaning |
|---|---|
| `step` | The step's 0-based index. It must be a manual step that has started. |
| `verdict` | `pass`, `fail` or `skip`. |
| `note` | Optional. Kept in `results.json`. |
| `run_id` | Optional. Defaults to the run the editor holds. Any other run is refused. |
| `source` | `human` (default) or `agent`. A human verdict is recorded with `verdict_source: "action"`, an agent verdict with `verdict_source: "agent"`. `elicitation` is the form's own channel, and `action` is accepted as another name for `human`. |
| `evidence` | `{ note, captures? }`. Required with `source: "agent"`, optional otherwise. `note` says what was observed and must not be empty. `captures` lists capture files that show it, absolute or relative to the run's `output_dir`; each must exist, and is recorded as an absolute path. |

A verdict on an auto step, a step that has not started, or a run other than the one the editor holds is refused with `success: false` and an `error`. So is an agent verdict without an evidence note, an agent verdict on a step with `agent_judgeable: false`, and evidence naming a capture that does not exist. A verdict can replace an earlier one, and repeating the same verdict, note, source and evidence answers `unchanged: true`. A verdict on a run that has already ended is accepted for its awaiting steps and rewrites its `results.json`.

The answer carries `run_id`, `step`, `status`, `verdict`, `verdict_source`, `note`, `evidence` (when the verdict has some), `state` and `awaiting` (the indices still waiting).

### Agent verdicts

An agent that judges a manual step itself, for example from a capture it took, says so with `source: "agent"` and cites what it saw:

```text
pie(action="verdict", step=2, verdict="pass", source="agent",
    evidence={"note": "The door swings open over 12 frames with no jump in the capture",
              "captures": ["door_open.png"]})
```

The step records `verdict_source: "agent"` and the evidence in `results.json`, on the `verdict` event, and in `script_status`, and the run's `summary` counts it under `agent_verdicts` rather than `human_verdicts`. A person can replace an agent verdict with their own, which clears the agent's evidence. A step whose judgement has to be a person's declares `agent_judgeable: false`. `awaiting` entries and manual steps carry `agent_judgeable`, so an agent can tell which steps it may judge before it tries.

## Results

`script_results` (`run_id?` or `results_path?`) returns `results.json` with `success` and `live` added. For the active run it is built from the run in memory (`live: true`). Otherwise it is read from `results_path`, or from `<Project>/Saved/MCPObservations/<run_id>/results.json`. The run writes the file when it ends.

```json
{
  "version": 1,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
  "description": "The front door opens when the player walks up",
  "profile": "/Game/PIEStudio/Profiles/Door",
  "started_at": "2026-10-23T15:00:00.412Z",
  "ended_at": "2026-10-23T15:00:31.050Z",
  "state": "finished",
  "stop_reason": "all steps settled",
  "block_on_manual": true,
  "default_within_ms": 5000,
  "observation_run_id": "script_DoorOpens_20261023_100000",
  "output_dir": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000",
  "results_path": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json",
  "recording_id": "script_DoorOpens_20261023_100000",
  "recording_dir": "C:/MyGame/Saved/MCPRecordings/script_DoorOpens_20261023_100000",
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0, "awaiting": 0, "human_verdicts": 1, "agent_verdicts": 0 },
  "steps": [
    {
      "index": 0,
      "instruction": "Walk up to the door",
      "judge": "auto",
      "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 },
      "status": "passed",
      "within_ms": 3000,
      "started_at_ms": 33.3,
      "ended_at_ms": 50,
      "started_at": "2026-10-23T15:00:00.412Z",
      "ended_at": "2026-10-23T15:00:00.429Z",
      "evidence": {
        "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2,
        "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7
      },
      "verdict_source": "auto"
    },
    {
      "index": 1,
      "instruction": "Open the door",
      "judge": "auto",
      "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 },
      "status": "passed",
      "within_ms": 5000,
      "started_at_ms": 50,
      "ended_at_ms": 2210,
      "started_at": "2026-10-23T15:00:00.429Z",
      "ended_at": "2026-10-23T15:00:02.590Z",
      "evidence": {
        "value": "Door.OpenAngle", "at_start": 0, "at_end": 81.4, "samples": 130,
        "observed": true, "verdict_frame": 133, "verdict_at_ms": 2160
      },
      "verdict_source": "auto"
    },
    {
      "index": 2,
      "instruction": "The door opens without a visible pop",
      "judge": "manual",
      "agent_judgeable": true,
      "status": "failed",
      "within_ms": 5000,
      "started_at_ms": 2210,
      "ended_at_ms": 30671,
      "started_at": "2026-10-23T15:00:02.590Z",
      "ended_at": "2026-10-23T15:00:31.050Z",
      "note": "Pops on the second frame",
      "verdict_source": "elicitation"
    }
  ],
  "success": true,
  "live": false
}
```

| Field | Meaning |
|---|---|
| `version` | `1`. |
| `script`, `script_name` | The asset path (or `inline`) and the run's name. |
| `description` | The script's description. Absent when empty. |
| `profile` | The observation profile, or an empty string. |
| `started_at`, `ended_at` | ISO 8601 UTC wall time of the observer's first frame and of the end. |
| `state` | `finished` or `stopped`, or the live state with `live: true`. |
| `stop_reason` | Why the run ended. |
| `block_on_manual`, `default_within_ms` | The run's settings. |
| `observation_run_id`, `output_dir`, `results_path` | As in [Run ids and files](#run-ids-and-files). |
| `recording_id`, `recording_dir` | Absent with `record=false`. |
| `summary` | `total`, `passed`, `failed`, `skipped`, `pending`, `awaiting`, `human_verdicts` and `agent_verdicts`. `pending` counts every step not settled, awaiting ones included. `human_verdicts` counts manual steps settled by a person (`verdict_source` `action` or `elicitation`), `agent_verdicts` those settled by an agent. A replay adds `regressions`. |
| `steps[].within_ms` | The step's effective window. |
| `steps[].started_at_ms`, `ended_at_ms` | Game ms from the observer's first frame. Absent for a step that never started or ended. |
| `steps[].started_at`, `ended_at` | ISO 8601 UTC wall time. |
| `steps[].evidence` | Auto steps with `expect`: `value`, `at_start` and `at_end` (first and last observed value in the step, `null` when never read; 1 or 0 for `visible` and `exists`), `samples`, `observed`, `verdict_frame` (the observer frame of the verdict), `verdict_at_ms` (ms from the step's start, or the end of its act, to its verdict) and `reason` (why it failed). Manual steps carry the verdict's `{ note, captures? }` when it had evidence, which an agent verdict always has. Other steps carry `evidence` only as `{ reason }`, when there is one. |
| `steps[].note` | The verdict's note. |
| `steps[].verdict_source` | `auto`, `action` (a person, through `verdict`), `elicitation` (a person, through the form), `agent`, or `source_run` on a replay. |
| `steps[].agent_judgeable` | Manual steps: whether an agent verdict is accepted. |
| `steps[].arrange`, `act` | The step's arrange and act entries, as written. Absent when it has none. |
| `steps[].act_result` | Steps with `arrange` or `act`: what they did and how long it took ([Arrange and act](#arrange-and-act)). |

### Listing runs

`script_runs` (`script?`, `limit?` default 50, `include_steps?` default true) lists script runs newest first: the active run, built from memory with `live: true`, then every `results.json` under `<Project>/Saved/MCPObservations`. `script` takes an asset or object path and keeps that script's runs. Each run carries `run_id`, `script`, `script_name`, `state`, `mode`, `started_at`, `ended_at`, `stop_reason`, `summary`, `results_path`, `live`, and its `steps` as `results.json` has them. The answer adds `count`, `total` (runs that matched before `limit`) and `observations_dir`.

## Replay regression

`script_replay` (`run_id?` or `results_path?`, `time_scale?` (default 1), `auto_stop_pie?` (default `true`), `pie_instance?`, `client_id?`, `wait?`, `timeout_seconds?`) replays the source run's recording with the same observation profile, in the level the source run played in when it had a `map`, and checks every auto step again in its recorded window. Without `run_id` or `results_path` it replays the last run. It needs PIE stopped, starts PIE itself, and with `auto_stop_pie` ends PIE once the replay finishes. A run made with `record=false` has no recording and is refused.

```text
pie(action="script_replay", run_id="script_DoorOpens_20261023_100000", wait=true)
```

The answer is the replay run's status, as for `script_run`, plus `pie_started`, `auto_stop_pie`, `time_scale`, `replay_of` and `message`.

- `arrange` and `act` do not run again: the recording carries the input the act drove. Each step that has them says so in `act_result` (`status: "skipped"`). What arrange set and what `place` and `console` did is not input, so a step that depends on them can fail in a replay.
- Each auto step that started in the source run gets a window from `window_start_ms` (its source `started_at_ms`, or the end of its source act, plus `replay_offset_ms`, the recording's settle time) to `window_end_ms` (`window_start_ms` plus `within_ms`). It starts on the first frame at or after `window_start_ms` and is judged as in a live run. Steps are judged in their own windows, not one after another.
- An auto step that never started in the source run is `skipped` with reason `not reached in the source run`.
- A manual step keeps the source run's verdict, note and evidence, with `verdict_source: "source_run"`. One without a verdict there is `skipped` with reason `no verdict in the source run`.
- Every step carries `source_status` (its status in the source run) and `regression`, which is `true` when it passed in the source run and failed in the replay.

The replay's `results.json` adds `replay_of` (the source run id), `time_scale`, `replay_offset_ms` and `summary.regressions`. Its `recording_id` and `recording_dir` are the source run's, and `block_on_manual` is `false`. A replayed step:

```json
{
  "index": 0,
  "instruction": "Walk up to the door",
  "judge": "auto",
  "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 },
  "status": "failed",
  "within_ms": 3000,
  "started_at_ms": 533.3,
  "ended_at_ms": 3533.3,
  "started_at": "2026-10-23T15:05:00.912Z",
  "ended_at": "2026-10-23T15:05:03.912Z",
  "evidence": {
    "value": "Door.Distance", "at_start": 412.5, "at_end": 412.5, "samples": 181,
    "observed": true, "verdict_frame": 214, "verdict_at_ms": 3000,
    "reason": "Door.Distance decreasing did not hold within 3000 ms"
  },
  "verdict_source": "auto",
  "source_status": "passed",
  "regression": true,
  "window_start_ms": 533.3,
  "window_end_ms": 3533.3
}
```

## Daemon events

The daemon publishes every event of a run the server follows on its event stream, `GET /v1/events`, as the server reads it from the editor. The server follows a run it started, and a run `script_status` was called on before it ended. A client that did not start the run sees it too.

| Event | When | Payload beyond the common fields |
|---|---|---|
| `script.run_started` | The run was armed. | `resultsPath?` |
| `script.step_started` | A step began. | `step` |
| `script.step_acted` | A step's arrange and act ended, done or failed. | `step`, with `actResult` |
| `script.step_passed` | An auto step met its expectation. | `step` |
| `script.step_failed` | An auto step did not, or the run ended while it ran. | `step` |
| `script.verdict_requested` | Manual steps wait for a verdict. | `awaiting`: `[{ index, instruction }]` |
| `script.verdict` | A manual step got its verdict. | `step` |
| `script.run_finished` | The run finished or was stopped. | `summary`, `state?`, `resultsPath?` |

Every payload carries `runId`, `script` (the asset path, or `inline`), `at` (the editor's ISO wall time of the event), `atMs` (the event's `at_ms`: game ms from the observer's first frame, 0 for `run_started`), `seq` (the editor's number for the event, increasing within a run) and, for a replay, `replayOf`.

`step` is `{ index, instruction, judge, agentJudgeable?, expect?, arrange?, act?, actResult?, status, evidence?, note?, verdictSource?, withinMs?, startedAtMs?, endedAtMs?, sourceStatus?, regression? }`. `actResult` is the step's `act_result`, unchanged. `evidence` is the editor's evidence object, unchanged. `verdictSource` is `auto`, `action`, `elicitation`, `agent` or, on a replay, `source_run`; `sourceStatus` and `regression` appear on a replay only. Fields the event does not carry are filled from the status the server read with it, so a `step_started` step can already hold `evidence` and `verdictSource`. `summary` on `run_finished` is `{ total, passed, failed, skipped, pending, awaiting?, humanVerdicts?, agentVerdicts? }`, plus `regressions` on a replay.

```text
id: 412
event: script.step_passed
data: {"at":"2026-10-23T15:00:00.429Z","runId":"script_DoorOpens_20261023_100000","script":"/Game/PIEStudio/Scripts/DoorOpens","atMs":50,"seq":3,"step":{"index":0,"instruction":"Walk up to the door","judge":"auto","status":"passed","expect":{"value":"Door.Distance","op":"decreasing","within_ms":3000},"evidence":{"value":"Door.Distance","at_start":412.5,"at_end":411.9,"samples":2,"observed":true,"verdict_frame":3,"verdict_at_ms":16.7},"verdictSource":"auto"}}

id: 416
event: script.verdict_requested
data: {"at":"2026-10-23T15:00:02.590Z","runId":"script_DoorOpens_20261023_100000","script":"/Game/PIEStudio/Scripts/DoorOpens","atMs":2210,"seq":7,"awaiting":[{"index":2,"instruction":"The door opens without a visible pop"}]}

id: 418
event: script.run_finished
data: {"at":"2026-10-23T15:00:31.050Z","runId":"script_DoorOpens_20261023_100000","script":"/Game/PIEStudio/Scripts/DoorOpens","atMs":30671,"seq":9,"summary":{"total":3,"passed":2,"failed":1,"skipped":0,"pending":0},"state":"finished","resultsPath":"C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json"}
```

To subscribe, read the daemon's port and token from `~/.ue-mcp/daemons/<key>.json` and open the stream with the token:

```bash
curl -N -H "Authorization: Bearer $TOKEN" http://127.0.0.1:$PORT/v1/events
```

A browser `EventSource`, which cannot set headers, passes `?token=` instead. To resume after a disconnect, send the last `id:` you saw as the `Last-Event-ID` header (or `?since=`); the daemon replays what came after it. When some of those events have already dropped out of its window, it sends a `gap` event first, and the client reads the run's state with `pie(script_status)`.

A daemon publishes each event of a run once. A new daemon process that picks up a run in progress (after `pie(script_status)` on it) reads it from its first event again, so a client that keeps state across a daemon restart ignores an event whose `runId` and `seq` it has already seen.
