# QA checklist client contract

The contract a client builds a QA checklist UI against: a list of test script steps that fills in live as a run progresses, with pass, fail and skip controls on the manual steps. It names what ue-mcp guarantees and nothing about how any one client draws it.

A client reaches the run three ways, and may use any mix:

1. MCP tool calls on the `pie` category (any MCP client).
2. The MCP elicitation form the server raises for manual steps (an MCP client that advertises elicitation).
3. The daemon's event stream, `GET /v1/events`, and, for a daemon extension, `api.callAction`.

All field names on the wire are snake_case in tool params, tool answers and `results.json`. Event payloads on `/v1/events` are camelCase, apart from the `expect` and `evidence` objects, which pass through as the editor wrote them.

## 1. Actions

Every action is `pie(<action>, ...)`. Through the micro gateway it is `tools(action="call", category="pie", method="<action>", args={...})`; through a daemon extension it is `api.callAction("pie", "<action>", {...})`.

The editor holds one run: the active one, or the last one to end. `script_run` and `script_replay` are refused while a run is active. `script_status` and `verdict` act only on the run the editor holds; read an older run with `script_results`.

### Ids, paths and times

- A run id is `script_<name>_<YYYYMMDD_HHMMSS>` (editor local time), and a replay run's is `script_replay_<name>_<YYYYMMDD_HHMMSS>`. Characters other than letters, digits, `-` and `_` in the name become `_`. When the folder already exists, `_2`, `_3` and so on are appended.
- `observation_run_id` and `recording_id` equal `run_id`. A replay run's `recording_id` is the source run's.
- Paths are absolute: `output_dir` is `<Project>/Saved/MCPObservations/<run_id>`, `results_path` is `<output_dir>/results.json`, `recording_dir` is `<Project>/Saved/MCPRecordings/<run_id>`.
- `*_ms` times on steps and events (`started_at_ms`, `ended_at_ms`, `at_ms`, `elapsed_ms`, `window_start_ms`, `window_end_ms`) are game milliseconds from the observer's first frame. `verdict_at_ms` is game milliseconds from the step's start. Wall times (`started_at`, `ended_at`, `wall_time`, `at`) are ISO 8601 UTC.

### pie(script_run)

Params:

| Param | Type | Default | |
|---|---|---|---|
| `script` | string | | Script asset path. Exactly one of `script` or `steps` is required. |
| `steps` | Step[] | | Inline steps (section 5). |
| `name` | string | the asset name, or `inline` | Run name. Also overrides the asset name. |
| `profile` | string | the script's | Observation profile asset path. |
| `record` | boolean | `true` | Arm the recorder. `false` makes the run not replayable. |
| `start_pie` | boolean | `false` | Start PIE when it is not running. |
| `block_on_manual` | boolean | `true` | Hold at a manual step, or a block of consecutive manual steps, until each has a verdict. |
| `mode` | `full` \| `manual` | `full` | `manual` starts no PIE, observer or recording (section 1.8). |
| `default_within_ms` | integer | the script's, or `5000` | Window for an auto step without `within_ms`. |
| `pie_instance` | integer | the active PIE world | PIE world to observe. |
| `client_id` | integer | `0` | Local player in that world. |
| `wait` | boolean | `false` | Hold the answer (section 1.7). |
| `timeout_seconds` | number | `600` | How long `wait` holds it. |

Answer (abridged: it also carries `created`, `existed`, `rollback` and `message`):

```json
{
  "success": true,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
  "state": "armed",
  "current_step": 0,
  "output_dir": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000",
  "results_path": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json",
  "observation_run_id": "script_DoorOpens_20261023_100000",
  "recording_id": "script_DoorOpens_20261023_100000",
  "elapsed_ms": 0,
  "steps": [
    { "index": 0, "instruction": "Walk up to the door", "judge": "auto", "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 }, "status": "pending", "within_ms": 3000 },
    { "index": 1, "instruction": "Open the door", "judge": "auto", "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 }, "status": "pending", "within_ms": 5000 },
    { "index": 2, "instruction": "The door opens without a visible pop", "judge": "manual", "agent_judgeable": true, "status": "pending", "within_ms": 5000 }
  ],
  "awaiting": [],
  "events": [
    { "seq": 1, "type": "run_started", "run_id": "script_DoorOpens_20261023_100000", "step": -1, "at_ms": 0, "wall_time": "2026-10-23T15:00:00.000Z",
      "data": { "script": "/Game/PIEStudio/Scripts/DoorOpens", "script_name": "DoorOpens", "steps": 3, "observation_run_id": "script_DoorOpens_20261023_100000", "recording_id": "script_DoorOpens_20261023_100000" } }
  ],
  "last_seq": 1,
  "summary": { "total": 3, "passed": 0, "failed": 0, "skipped": 0, "pending": 3, "awaiting": 0, "human_verdicts": 0, "agent_verdicts": 0 },
  "pie_started": true,
  "pie_running": false
}
```

`recording_id` is absent with `record=false`. The run moves from `armed` to `running` on the observer's first frame. A successful answer always starts the server following the run, so its events reach `/v1/events` whether or not the caller waits.

### pie(script_status)

Params: `run_id?` (default: the run the editor holds), `since_seq?` (integer), `wait?`, `timeout_seconds?`.

Answer: the same fields as the `script_run` answer, plus `active`, with `events` after `since_seq`:

```json
{
  "success": true,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
  "state": "awaiting_verdict",
  "current_step": 2,
  "output_dir": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000",
  "results_path": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json",
  "observation_run_id": "script_DoorOpens_20261023_100000",
  "recording_id": "script_DoorOpens_20261023_100000",
  "elapsed_ms": 2210,
  "steps": [
    { "index": 0, "instruction": "Walk up to the door", "judge": "auto", "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 }, "status": "passed", "within_ms": 3000,
      "started_at_ms": 33.3, "ended_at_ms": 50, "started_at": "2026-10-23T15:00:00.412Z", "ended_at": "2026-10-23T15:00:00.429Z",
      "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2, "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7 }, "verdict_source": "auto" },
    { "index": 1, "instruction": "Open the door", "judge": "auto", "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 }, "status": "passed", "within_ms": 5000,
      "started_at_ms": 50, "ended_at_ms": 2210, "started_at": "2026-10-23T15:00:00.429Z", "ended_at": "2026-10-23T15:00:02.590Z",
      "evidence": { "value": "Door.OpenAngle", "at_start": 0, "at_end": 81.4, "samples": 130, "observed": true, "verdict_frame": 133, "verdict_at_ms": 2160 }, "verdict_source": "auto" },
    { "index": 2, "instruction": "The door opens without a visible pop", "judge": "manual", "agent_judgeable": true, "status": "awaiting_verdict", "within_ms": 5000,
      "started_at_ms": 2210, "started_at": "2026-10-23T15:00:02.590Z" }
  ],
  "awaiting": [{ "index": 2, "instruction": "The door opens without a visible pop", "agent_judgeable": true }],
  "events": [
    { "seq": 6, "type": "step_started", "run_id": "script_DoorOpens_20261023_100000", "step": 2, "at_ms": 2210, "wall_time": "2026-10-23T15:00:02.590Z",
      "data": { "status": "awaiting_verdict", "instruction": "The door opens without a visible pop", "judge": "manual", "agent_judgeable": true } },
    { "seq": 7, "type": "awaiting_verdict", "run_id": "script_DoorOpens_20261023_100000", "step": 2, "at_ms": 2210, "wall_time": "2026-10-23T15:00:02.590Z",
      "data": { "awaiting": [2], "blocking": true, "steps": [{ "index": 2, "instruction": "The door opens without a visible pop", "agent_judgeable": true }] } }
  ],
  "last_seq": 7,
  "summary": { "total": 3, "passed": 2, "failed": 0, "skipped": 0, "pending": 1, "awaiting": 1, "human_verdicts": 0, "agent_verdicts": 0 },
  "active": true
}
```

- `state`: `armed` | `running` | `awaiting_verdict` | `finished` | `stopped` | `idle`. A run the editor no longer holds in memory answers from its `results.json` (events included) with `live: false`. `idle` means no such run exists on disk either (or there has been no run yet): the answer then has only `state`, `run_id` (when given), empty `steps`, `awaiting` and `events`, and `last_seq` equal to `since_seq`.
- step `status`: `pending` | `running` | `passed` | `failed` | `awaiting_verdict` | `skipped`. A manual step ends as `passed`, `failed` or `skipped` from its verdict.
- step fields: `index`, `instruction`, `judge`, `agent_judgeable` (manual steps), `expect?`, `status`, `within_ms`, then once set `started_at_ms`, `ended_at_ms`, `started_at`, `ended_at`, `evidence`, `note`, `verdict_source`. Replay steps add `source_status`, `regression`, and for auto steps `window_start_ms` and `window_end_ms` (section 4).
- `awaiting`: the steps waiting for a verdict, as `{ index, instruction, agent_judgeable }`.
- `summary`: `total`, `passed`, `failed`, `skipped`, `pending`, `awaiting`, `human_verdicts`, `agent_verdicts`, and `regressions` on a replay. `pending` counts every step not settled, awaiting ones included. `human_verdicts` counts manual steps a person settled (`verdict_source` `action` or `elicitation`), `agent_verdicts` those an agent settled (`agent`).
- `events`: the events with `seq > since_seq` (all of them without `since_seq`), oldest first. Each is `{ seq, type, run_id, step, at_ms, wall_time, data }`; `step` is the step index, or `-1` for a run event and for an `awaiting_verdict` that covers several steps.
- `last_seq`: the highest seq the run has. Pass it as the next `since_seq`.

Editor event types and their `data`:

| Type | `data` |
|---|---|
| `run_started` | `script`, `script_name`, `steps` (the step count), `observation_run_id`, `recording_id?`, and `replay_of` on a replay |
| `step_started` | `status`, `instruction`, `judge`, `agent_judgeable` on a manual step, and for a step with `expect`: `expect`, `within_ms`; on a replay `window_start_ms`, `window_end_ms` |
| `step_passed`, `step_failed` | `status`, `evidence` (or `reason` for a step without `expect`), `regression: true` on a replay regression |
| `awaiting_verdict` | `awaiting` (indices), `blocking` (false with `block_on_manual=false`), `steps` (`[{ index, instruction, agent_judgeable }]`) |
| `verdict` | `status`, `verdict`, `note?`, `verdict_source`, `evidence?` (`{ note, captures? }`, always present on an agent verdict) |
| `run_finished` | `state`, `reason`, `summary`, `results_path`, `awaiting?` (indices still waiting) |

A polling client calls `script_status(since_seq=last_seq)` and applies `events` in order. Calling it on a run that has not ended also restarts the server's follower, so a client can call it once after a server restart to get the daemon stream going again.

### pie(verdict)

Params: `step` (integer, 0-based, required), `verdict` (`pass` | `fail` | `skip`, required), `note?` (string), `run_id?` (default: the run the editor holds), `source?` (`human` | `agent`, default `human`; `elicitation` is the form's channel and `action` another name for `human`), `evidence?` (`{ note: string, captures?: string[] }`, required with `source: "agent"`).

- `source: "human"` is recorded as `verdict_source: "action"`, `source: "agent"` as `verdict_source: "agent"`.
- `evidence.note` must not be empty for an agent verdict. `evidence.captures` are file paths, absolute or relative to the run's `output_dir`; each must exist and is recorded absolute.
- An agent verdict on a step with `agent_judgeable: false` is refused. A human verdict is accepted on any manual step, and a human verdict replacing an agent's clears the agent's evidence.

Answer: `run_id`, `step`, `status`, `verdict`, `verdict_source`, `note?`, `evidence?`, `state` and `awaiting` (the indices still waiting). A repeat of the same verdict, note, source and evidence answers `unchanged: true`. The verdict is refused with `success: false` and an `error` for an auto step, a step that has not started, a step index out of range, a `run_id` the editor does not hold, an agent verdict without an evidence note, an agent verdict on a step with `agent_judgeable: false`, or a capture that does not exist:

```json
{ "success": false, "error": "Run script_DoorOpens_20261023_100000 is not the current run (script_DoorOpens_20261023_100200)" }
```

A verdict can replace an earlier one, and is accepted on a run that has ended, which rewrites its `results.json`. It appears on the stream as `script.verdict` once the server's next poll reads it, while the server follows the run.

### pie(script_stop)

Params: `run_id?` (must be the active run when given), `reason?`. Ends the active run with `state: "stopped"` and `stop_reason` set to `reason` (default `stopped`), writes `results.json`, and answers with the run's fields (including `summary` and `results_path`), `stopped: true` and `changed: true`. An auto step still running fails; pending steps stay `pending` and manual steps without a verdict stay `awaiting_verdict`. With no active run it answers `success: false`, `alreadyStopped: true`, `changed: false`.

### pie(script_results)

Params: `run_id?` or `results_path?`. Answers with `results.json` (section 4) plus `success` and `live`. For the active run it is built from memory (`live: true`); otherwise it is read from `results_path`, or `<Project>/Saved/MCPObservations/<run_id>/results.json`.

### pie(script_replay)

Params: `run_id?` (default: the last run) or `results_path?`, `time_scale?` (default 1), `auto_stop_pie?` (default `true`), `pie_instance?`, `client_id?`, `wait?`, `timeout_seconds?`.

Refused while PIE is running, and for a source run made with `record=false`. Starts PIE, replays the source run's recording with the same observation profile, and evaluates every auto step again in its recorded window. Answers like `script_run`, with a new `run_id`, plus `replay_of`, `pie_started`, `auto_stop_pie` and `time_scale`. That run's events carry `replayOf`, and its results carry `replay_of` and the per-step comparison of section 4.

### 1.8 Manual runs

`script_run` with `mode: "manual"` judges a script by its manual steps alone, with no PIE.

- Nothing is armed: no PIE is started, and no observer or recording runs. `start_pie`, `record`, `block_on_manual`, `pie_instance` and `client_id` have no effect. The answer has no `recording_id` and no `observation_run_id`, and `pie_started` is `false`.
- Every manual step is `awaiting_verdict` from the start, announced by one `awaiting_verdict` event with `blocking: false`, and takes its verdict in any order. Auto steps stay `pending` and count in `summary.unchecked`, not in `pending`.
- `pie(verdict)` works as on a full run. The run finishes with `stop_reason` `all manual steps settled` once every manual step has a verdict (`no manual steps` at once for a script with none), and `script_stop` ends it early.
- It writes `results.json` as a full run does, with `mode: "manual"`. Times are wall milliseconds from the start of the run.
- `script_replay` refuses it, as a run with no recording.
- A full run started later on the same script is a separate run with its own id, and its manual steps start without a verdict.

Every run's answer, `results.json` and every event's `data` carry `mode`: `full` or `manual`. Every `summary` carries `unchecked`, 0 on a full run.

### 1.7 wait

With `wait: true`, `script_run`, `script_replay` and `script_status` hold the answer and return it merged with the run's final status and a `wait` object:

```json
{
  "wait": {
    "state": "awaiting_verdict",
    "durationMs": 5400,
    "awaiting": [2],
    "elicitation": "unavailable"
  },
  "verdictHint": "call pie(verdict, step, verdict, note?); an agent passes source=\"agent\" with evidence {note, captures?}"
}
```

| Field | Type | |
|---|---|---|
| `state` | `finished` \| `stopped` \| `awaiting_verdict` \| `timed_out` \| `unreachable` | Why the wait ended. |
| `durationMs` | integer | How long it waited. |
| `awaiting` | integer[] | With `awaiting_verdict`: the steps waiting for `pie(verdict)`. |
| `elicitation` | `accepted` \| `declined` \| `cancelled` \| `auto_answered` \| `failed` \| `unavailable` | How the last verdict form went. Absent when no form was due. |
| `error` | string | With `timed_out` or `unreachable`, and with `stopped` when the editor no longer holds the run. |

- `awaiting_verdict` is returned only when no form can or will collect the verdict: the client has no elicitation, or the form for those steps was declined, cancelled, answered without being shown, or failed. While a form is up, the wait goes on.
- `timed_out` does not stop the run.
- `stopped` also covers a run id the editor has no record of (its status is `idle`).
- `unreachable` means the editor failed to answer five status polls in a row, or the six hour follow limit ran out.
- `verdictHint` is present exactly when `wait.state` is `awaiting_verdict`.
- On `script_run` and `script_replay` the merged answer has no `events`; read them from `script_status` or the stream. On `script_status` the merged `events` honour the caller's `since_seq`.

## 2. The verdict form (MCP elicitation)

Raised by the server when a run's status shows manual steps awaiting a verdict and the connected MCP client advertised the `elicitation` capability. One form covers every step awaiting at that moment that has not been asked about before in this run.

```json
{
  "message": "Test script /Game/PIEStudio/Scripts/DoorOpens (run script_DoorOpens_20261023_100000) needs your verdict on one manual step:\n\nStep 3: The door opens without a visible pop\n\nPick pass, fail or skip for each. A step left blank waits for pie(verdict).",
  "requestedSchema": {
    "type": "object",
    "properties": {
      "step_2": {
        "type": "string",
        "title": "Step 3",
        "description": "The door opens without a visible pop",
        "enum": ["pass", "fail", "skip"]
      },
      "step_2_note": {
        "type": "string",
        "title": "Step 3 note",
        "description": "Optional. What you saw."
      }
    }
  }
}
```

- The message names the script as the asset path, or `inline`, and says `<n> manual steps` for more than one.
- Property names: `step_<i>` and `step_<i>_note`, where `<i>` is the 0-based step index. Titles number from 1 (`Step <i+1>`).
- `step_<i>` is a string enum of exactly `pass`, `fail`, `skip`. `step_<i>_note` is a free string.
- No property is required. The schema has no `required` key.

Answers:

| Answer | Effect |
|---|---|
| `accept` | For each `step_<i>` holding `pass`, `fail` or `skip`, the server calls `pie_script_verdict { run_id, step: i, verdict, source: "elicitation", note? }`. The note is trimmed and sent only when not empty. A step left blank stays awaiting. |
| `decline`, `cancel` | Nothing is posted. The steps stay awaiting. |
| any answer other than `accept` sooner than `UE_MCP_ELICIT_MIN_HUMAN_MS` (default 1000 ms) | Taken as a client that did not show the form (`auto_answered`). Nothing is posted. |
| the request throws | `failed`. Nothing is posted. |

Every step put to a form is never put to a form again in the same run, whatever the answer. A step the form did not settle is settled only by `pie(verdict)`. Without elicitation, no form is raised, and `script.verdict_requested` on the stream plus `wait.state: "awaiting_verdict"` are the signals to collect the verdict another way.

## 3. Events on /v1/events

### Transport

- `GET http://127.0.0.1:<port>/v1/events`, with `Authorization: Bearer <token>`. Port and token are in the daemon's discovery file, `~/.ue-mcp/daemons/<key>.json`. A browser `EventSource` passes `?token=<token>`.
- Server-sent events. Each frame is `id: <n>`, `event: <type>`, `data: <json>`. `id` is the daemon's stream id, increasing by one across every event type.
- Resume with `Last-Event-ID: <n>` (or `?since=<n>`): the daemon replays what it still holds after `n`, then streams live. It holds the last 1000 events. When some events after `n` have dropped out, it first sends `event: gap` with `data: {}`; the client then rebuilds the run with `pie(script_status)` without `since_seq`, or `pie(script_results)` for an ended run.
- `data` always has `at` (ISO 8601). For script events it is the editor's wall time of the event.

### Event types

`script.run_started`, `script.step_started`, `script.step_acted`, `script.step_passed`, `script.step_failed`, `script.verdict_requested`, `script.verdict`, `script.run_finished`. `script.verdict_requested` is the editor's `awaiting_verdict`; the others keep the editor's names. A client ignores any other `script.*` type it does not know.

### Common fields

| Field | Type | |
|---|---|---|
| `runId` | string | The run. |
| `script` | string | Script asset path, or `"inline"`. |
| `at` | string | ISO 8601 wall time, from the editor. |
| `atMs` | number | The event's `at_ms`: game milliseconds from the observer's first frame. `0` on `run_started`, which comes before it. |
| `seq` | integer | The editor's number for the event, increasing within the run. |
| `replayOf` | string? | For a replay run, the source run id. |
| `mode` | `"full"` \| `"manual"` | The run's mode (section 1.8). |

### Step object

Carried as `step` by `step_started`, `step_acted`, `step_passed`, `step_failed` and `verdict`:

| Field | Type | |
|---|---|---|
| `index` | integer | 0-based. |
| `instruction` | string | |
| `judge` | `"auto"` \| `"manual"` | |
| `expect` | object? | `{ value: string, op: "decreasing"\|"increasing"\|"eq"\|"gt"\|"lt"\|"visible"\|"exists", target?: number\|boolean, within_ms?: integer }` |
| `arrange`, `act` | array? | The step's arrange and act entries (section 5.3). |
| `actResult` | object? | What arrange and act did, the step's `act_result` (section 4). |
| `status` | string | The step's status after this event: `running` (or `awaiting_verdict` for a manual step without `arrange` or `act`) on `step_started`, `running` on `step_acted` (`failed` when the act failed), `passed` on `step_passed`, `failed` on `step_failed`, and `passed`, `failed` or `skipped` on `verdict`. |
| `agentJudgeable` | boolean? | Manual steps: whether an agent verdict is accepted. |
| `evidence` | object? | The editor's evidence object for an auto step with `expect`: `{ value, at_start, at_end, samples, observed, verdict_frame?, verdict_at_ms?, reason? }`. On a manual step, the verdict's `{ note, captures? }`. |
| `note` | string? | |
| `verdictSource` | `"auto"` \| `"action"` \| `"elicitation"` \| `"agent"` \| `"source_run"`, optional | Who decided the step: the editor, a person through `pie(verdict)`, a person through the form, an agent, or the source run of a replay. |

Fields the event itself does not carry are filled from the status the server read with it, so a `step_started` step can already hold `evidence` and `verdictSource`. The step object also carries `withinMs`, `startedAtMs` and `endedAtMs` once set, and on a replay `sourceStatus` and `regression`. The `run_finished` summary carries `awaiting`, and `regressions` on a replay.

### Per type

| Type | Extra fields |
|---|---|
| `script.run_started` | `resultsPath?: string` |
| `script.step_started` | `step` |
| `script.step_acted` | `step`, carrying `actResult`. Once per step with `arrange` or `act`, when they end, done or failed, on a full run. |
| `script.step_passed` | `step` |
| `script.step_failed` | `step` |
| `script.verdict_requested` | `awaiting: Array<{ index: integer, instruction: string }>` |
| `script.verdict` | `step` |
| `script.run_finished` | `summary: { total, passed, failed, skipped, pending, awaiting?, humanVerdicts?, agentVerdicts?, unchecked?, regressions? }` (integers), `state?: "finished" \| "stopped"`, `resultsPath?: string` |

### Examples

```json
{ "at": "2026-10-23T15:00:00.000Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 0, "seq": 1, "resultsPath": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json" }
```

```json
{ "at": "2026-10-23T15:00:00.429Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 50, "seq": 3,
  "step": { "index": 0, "instruction": "Walk up to the door", "judge": "auto", "status": "passed",
            "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 },
            "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2, "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7 },
            "verdictSource": "auto" } }
```

```json
{ "at": "2026-10-23T15:00:02.590Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 2210, "seq": 7,
  "awaiting": [{ "index": 2, "instruction": "The door opens without a visible pop" }] }
```

```json
{ "at": "2026-10-23T15:00:31.050Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 30671, "seq": 8,
  "step": { "index": 2, "instruction": "The door opens without a visible pop", "judge": "manual", "status": "failed", "note": "Pops on the second frame", "verdictSource": "elicitation" } }
```

```json
{ "at": "2026-10-23T15:00:31.050Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 30671, "seq": 9,
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0 }, "state": "finished",
  "resultsPath": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json" }
```

### Ordering and duplicates

- Within one run, events arrive in increasing `seq`, and each `seq` at most once per daemon process.
- A run's events do not arrive as they happen: the server polls the editor every 250 ms to 1 s, so several can arrive together. `atMs` and `at` carry the real timing.
- Runs interleave freely; filter on `runId`.
- `seq` may skip numbers when the editor records an event type this version does not publish.
- A new daemon process that picks up a run in progress reads it from its first event, so a client that keeps state across a daemon restart drops any event whose (`runId`, `seq`) it already holds.
- `verdict_requested` may come once per step or once for several steps; treat `awaiting` as the set of steps to show controls for, and clear a step's controls on its `verdict` event.
- The stream says nothing for a run nobody started following in this daemon. A client that wants a run on the stream calls `pie(script_status, run_id)` once, before the run ends.

## 4. results.json

Written to `<Project>/Saved/MCPObservations/<run_id>/results.json`, beside the observation files, when the run ends, and again when a verdict lands on an ended run.

```json
{
  "version": 1,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
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
      "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2, "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7 },
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
      "evidence": { "value": "Door.OpenAngle", "at_start": 0, "at_end": 81.4, "samples": 130, "observed": true, "verdict_frame": 133, "verdict_at_ms": 2160 },
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
  ]
}
```

| Field | Type | |
|---|---|---|
| `version` | integer | `1`. |
| `run_id`, `script`, `script_name` | string | `script` is `"inline"` for inline steps. |
| `description` | string? | The script's description. Absent when empty. |
| `profile` | string | Empty when the run has none. |
| `started_at`, `ended_at` | ISO 8601 string | The observer's first frame, and the end. |
| `state` | `finished` \| `stopped` | |
| `mode` | `full` \| `manual` | Section 1.8. |
| `stop_reason` | string | `all steps settled`, `PIE ended`, `observation ended`, or the `script_stop` reason (default `stopped`). |
| `block_on_manual` | boolean | `false` on a replay. |
| `default_within_ms` | integer | |
| `observation_run_id`, `output_dir`, `results_path` | string | Absolute paths. |
| `recording_id`, `recording_dir` | string? | Absent with `record=false`. The source run's on a replay. |
| `replay_of`, `time_scale`, `replay_offset_ms` | string?, number?, number? | Replay runs only. `replay_offset_ms` is the recording's settle time. |
| `summary` | `{ total, passed, failed, skipped, pending, awaiting, human_verdicts, agent_verdicts, unchecked, regressions? }` | Integers. `pending` includes awaiting steps. `unchecked` counts a manual run's auto steps. `human_verdicts` and `agent_verdicts` count settled manual steps by who judged them. `regressions` on a replay only. |
| `steps[].status` | step status | `pending` or `awaiting_verdict` only on a run that ended before they settled. |
| `steps[].within_ms` | integer | The effective window. |
| `steps[].started_at_ms`, `ended_at_ms` | number? | Game ms from the observer's first frame. Absent until set. |
| `steps[].started_at`, `ended_at` | ISO 8601 string? | Absent until set. |
| `steps[].evidence` | object? | Auto steps with `expect`: `{ value, at_start, at_end, samples, observed, verdict_frame?, verdict_at_ms?, reason? }`, `at_start` and `at_end` `null` when never read. Manual steps: the verdict's `{ note, captures? }` when it had evidence (always on an agent verdict), with `reason` on a replay skip. Other steps: `{ reason }` when there is one (`no expectation`, or a replay skip reason). |
| `steps[].note` | string? | |
| `steps[].verdict_source` | `auto` \| `action` \| `elicitation` \| `agent` \| `source_run` | Absent until the step settles. `action` and `elicitation` are a person's verdicts, `agent` an agent's. |
| `steps[].agent_judgeable` | boolean? | Manual steps only. |
| `steps[].arrange`, `steps[].act` | array? | As the step was written (section 5.3). Absent when empty. |
| `steps[].act_result` | object? | Steps with `arrange` or `act`: `{ status: "running"\|"done"\|"failed"\|"skipped", reason?, arranged, actions: [{ index, action, ok, error?, at_ms }], started_at_ms, ended_at_ms, duration_ms, wall_ms, captures? }`. `action` is the entry's verb (`place`, `press`, `wait_ms`, ...). Times are game ms from the observer's first frame, `wall_ms` wall time. A `skipped` one (a manual run, a replay) carries only `status` and `reason`. |

Replay runs add to each step:

| Field | Type | |
|---|---|---|
| `source_status` | step status | The step's status in the source run. |
| `regression` | boolean | `true` when it passed in the source run and failed in the replay. |
| `window_start_ms`, `window_end_ms` | number? | Auto steps the source run reached: the source `started_at_ms` (or, for a step that acted, the source `act_result.ended_at_ms`) plus `replay_offset_ms`, and that plus `within_ms`. |

A replay does not run `arrange` or `act` again: the recording replays the input. Each step that has them carries `act_result` with `status: "skipped"`.

On a replay, manual steps carry the source verdict, note and evidence with `verdict_source: "source_run"`, or are `skipped` with reason `no verdict in the source run`. An auto step the source run never reached is `skipped` with reason `not reached in the source run`.

## 5. Step format (script_create, script_update, inline steps)

```json
{ "instruction": "string", "judge": "auto | manual", "agent_judgeable": true, "resolve_in_pie": false, "arrange": [], "act": [], "expect": { "value": "string", "op": "decreasing | increasing | eq | gt | lt | visible | exists", "target": 0, "within_ms": 3000 } }
```

- `instruction` is required. `judge` defaults to `auto`.
- `agent_judgeable` applies to manual steps and defaults to `true`; `false` makes `pie(verdict)` refuse `source: "agent"` for the step. It is refused on an auto step. `script_read` and results write it on every manual step.
- `resolve_in_pie` defaults to `false` and applies to a step with `expect`, `arrange` or `act`; it is refused on a step with none of them. `true` says `expect.value`, and the actors `arrange` and `act` name, only resolve once PIE is running (an actor spawned during play, a widget created at run time), so `script_create` and `script_update` do not resolve them (5.1).
- `arrange` and `act` are optional arrays (5.3). `within_ms` counts from the end of `act`. `script_read` and results write it only when `true`.
- A manual step with `expect` is refused. An auto step without `expect` passes when it starts.
- `target` is required for `eq`, `gt` and `lt`, refused for `decreasing` and `increasing`, and defaults to `true` for `visible` and `exists`. It is a number or a boolean; `script_read` and results write it back as a number for `eq`, `gt` and `lt`, and as a boolean for `visible` and `exists`.
- `within_ms` 0 or absent uses the run's `default_within_ms`.

Storage actions: `script_create { name, steps, package_path?, description?, profile?, default_within_ms?, manual_only?, onConflict? (skip | update | replace | error) }`, `script_read { path }`, `script_update { path, steps?, description?, profile?, default_within_ms?, manual_only? }`, `script_delete { path, confirm: true }`, `script_list { directory? }`. `script_read` answers `path`, `name`, `description`, `profile`, `default_within_ms`, `manual_only`, `steps` and `step_count`.

### 5.1 Draft-time checks

`script_create` and `script_update` refuse a script the observer could not check, before it is saved. `script_update` checks the script as it would stand after the update whenever `steps`, `profile` or `manual_only` is given; a change to `description` or `default_within_ms` alone is not checked. Inline steps on `script_run` are not checked this way.

- **`expect.value`** is resolved the way the observer reads it at run time, against the level open in the editor and the script's profile:
  - For `decreasing`, `increasing`, `eq`, `gt` and `lt`: a profile tracked value, by its exact path or its last segment; otherwise a dotted reflection path on the player pawn (the pawn in the level that player 0 possesses, or the game mode's `DefaultPawnClass`), whose first segment may name a component; `sub:<Subsystem>.<path>` on a game instance, local player or world subsystem; or `bone:<bone or socket>.<channel>` and `bonecs:<bone or socket>.<channel>` on the pawn's skeletal mesh, channel one of `x`, `y`, `z`, `pitch`, `yaw`, `roll`, `scalex`, `scaley`, `scalez`. The path must end at a number, a bool or an enum.
  - For `exists`: an actor in the level, by label, name or class, or a class the game mode spawns (pawn, controller, game state, player state, HUD).
  - For `visible`: a widget class, a Widget Blueprint, or the name of a widget inside a Widget Blueprint under `/Game`.
  - A step with `resolve_in_pie: true` is not resolved.
- **`op` and `target`** are checked against the value's type. A bool takes only `eq`, with target `true` or `false` (or 1 or 0). An enum takes only `eq`, with a target that is one of its values. A number takes a number target, never `true` or `false`. An integer is refused a fractional `eq` target, an `eq` target outside its range, and a `gt` or `lt` target it can never pass. `visible` and `exists` take target `true` or `false` (or 1 or 0).
- **Auto steps.** A script needs at least one auto step with an `expect`, unless it sets `manual_only: true`. The flag is stored on the script.
- **`arrange` and `act`**: the actor each `set`, `call` and `place.at` names (as `exists` resolves one), the actor class each `spawn` names, and the Input Action each `press`, `inject`, `hold` and `tape` names (an object path that loads as one). Fields are `arrange[<j>].target`, `arrange[<j>].class`, `act[<j>].place.at` and `act[<j>].<verb>.action`. `resolve_in_pie: true` skips the actors, never the classes or Input Actions.
- The profile must exist.

A refusal lists every problem it found, so a client can hand the list back to the agent that drafted the script:

```json
{
  "success": false,
  "errorCode": "INVALID_SCRIPT_STEPS",
  "error": "The script cannot be checked as written (2 problems): steps[0].expect.op: 'bCanBeDamaged' is a bool, and gt compares numbers; use eq with target true or false | steps[1].expect.value: 'BaseEyeHieght' cannot be read: DefaultPawn has no property or component named BaseEyeHieght (the observer reads it from the player pawn, DefaultPawn, the DefaultPawnClass of GameModeBase). If it only exists once PIE is running, set resolve_in_pie: true on the step.",
  "errors": [
    { "step": 0, "field": "expect.op", "message": "steps[0].expect.op: 'bCanBeDamaged' is a bool, and gt compares numbers; use eq with target true or false", "candidates": ["eq"] },
    { "step": 1, "field": "expect.value", "message": "steps[1].expect.value: 'BaseEyeHieght' cannot be read: DefaultPawn has no property or component named BaseEyeHieght (the observer reads it from the player pawn, DefaultPawn, the DefaultPawnClass of GameModeBase). If it only exists once PIE is running, set resolve_in_pie: true on the step.", "candidates": ["BaseEyeHeight", "BaseTurnRate", "BaseLookUpRate", "bHidden", "bHiddenEd"] }
  ]
}
```

| Field | Type | |
|---|---|---|
| `step` | integer \| null | 0-based step index, or `null` for a problem with the script as a whole (`steps`, `profile`). |
| `field` | string | The step field at fault: `instruction`, `judge`, `agent_judgeable`, `resolve_in_pie`, `expect`, `expect.value`, `expect.op`, `expect.target`, `expect.within_ms`, `arrange`, `act` (an entry that does not parse), `arrange[<j>].target`, `arrange[<j>].class`, `act[<j>].place.at`, `act[<j>].<verb>.action`; or `steps` or `profile` for the script. Empty when the step is not an object. |
| `message` | string | What is wrong, starting `steps[<i>].<field>:` for a step problem. |
| `candidates` | string[]? | Closest valid replacements, best first: property paths, actor labels, widget names, ops, enum values as `"<value> (<name>)"`, profile paths, or `manual_only` for a script with no auto step. Absent when there are none. |

`pie(script_validate) { steps, profile?, manual_only? }` runs these checks without saving and answers `{ success: true, valid, errors, summary?, steps }`, `errors` in the shape above. `pie(script_values) { target?, profile?, limit? }` lists what the checks accept for the open level: `pawn { class, source, components }`, `values [{ path, kind, type, values? }]` (`kind` is `number`, `integer`, `boolean` or `enum`; an enum's `values` are `{ name, value }`), `actors`, `spawned_classes`, `widgets`, `profile_values`, `truncated`, and with `target` a `target { requested, found, class?, is_player_pawn?, actors? }`.

### 5.2 The playtest flow

`flow(run, flowName: "playtest", params: { target, context?, profile?, name?, package_path? })`, or `POST /v1/flows/playtest/run` with `{ params }`, drafts a script with the flow model and saves it. It gathers the target Blueprint, the input mapping contexts and `pie(script_values)`, asks an agent step for `{ steps, manual_only?, description? }` in the step format above, checks the answer with `pie(script_validate)` and sends the refusal's `errors` back to the model up to 2 times, then saves with `pie(script_create)` (`onConflict: "replace"`, name default `TS_Playtest_<target>`). When the third draft is refused the run fails with `The drafted script was still refused after 3 attempts: <summary>`, its step data carrying `errors`, and nothing is saved.

The model is a daemon extension's provider (section 6), else `ANTHROPIC_API_KEY`. With neither, the agent step fails with `No model is configured for flow agent steps.`

`error` joins every `message` with ` | `. Parse problems (a missing `instruction`, an unknown `op`, a missing `target`) come in the same list, one per step, and the steps that parsed are still checked.

### 5.3 arrange and act

A step sets up and drives what it then checks. Both run in the PIE world the run observes (the server's, in a multiplayer session), through the code that already runs them elsewhere: `arrange` through a scenario's arrange (`scenario_validate`), `act` through the step actions `pie(uat_run)` runs in a step's `do`. There is no second implementation of either.

```json
{
  "instruction": "Open the door",
  "arrange": [{ "kind": "set", "target": "Door", "path": "OpenAngle", "value": 0 }],
  "act": [{ "place": { "at": "Door", "offset": [-150, 0, 0] } }, { "press": { "action": "/Game/Input/IA_Interact.IA_Interact" } }],
  "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 }
}
```

| `arrange` entry | |
|---|---|
| `{ kind: "spawn", class, at?: [x,y,z], rotation?: [pitch,yaw,roll], scale?: [x,y,z] }` | Spawns a native class or a Blueprint (path with or without `_C`). |
| `{ kind: "set", target, path, value }` | Sets a property on an actor by dotted path. |
| `{ kind: "call", target, func, args?: [] }` | Calls a function on an actor, arguments by position. |

`target` is an actor's label, name or class.

| `act` entry | |
|---|---|
| `{ client?, place: { at, offset?: [x,y,z], face?: true } }` or `{ client?, place: { location: [x,y,z], yaw? } }` | Teleports the player's pawn next to an actor (offset in its frame, facing it) or to a spot, else the nearest free spot within 360 units. |
| `{ client?, press: { action, frames?: 4 } }` | Holds an Input Action for `frames` frames, then releases it. |
| `{ client?, inject: { action, value } }` | One frame of an Input Action. |
| `{ client?, hold: { action, value, ms } }` | Holds an Input Action for `ms` wall milliseconds. |
| `{ client?, tape: { action, values: [] } }` | Plays a per-frame value array at 60 Hz. |
| `{ client?, console: "cmd" }` | Runs a console command. |
| `{ client?, capture: "label" \| true }` | Saves the player's view to `<output_dir>/captures/step<NN>_client<C>_<label>.jpg`. |
| `{ wait_ms }` | Waits before the next entry. |

`client` is 0 (the editor's own player, the default) or a remote player 1..N. `action` is an Input Action object path; `value` is a number, a boolean or `[x, y, z]`.

- **Order and timing.** On the frame a step starts: `arrange` runs, then `act` starts. The step holds the run until `act` ends: every entry ran, every `press`, `hold` and `tape` on the editor's player played out, every remote player confirmed what it was sent (up to 10 s), and every capture was written. Then the `expect` window opens and `within_ms` counts from there. `evidence.verdict_at_ms` counts from there too.
- **Manual steps** may carry `arrange` and `act`. The step reports `running` while they run, refuses `pie(verdict)` until they end, then turns `awaiting_verdict`. A manual step with `arrange` or `act` begins its own block of manual steps.
- **Failure.** An arrange entry that cannot apply, or an act entry that cannot run, fails the step with `evidence.reason` naming it (`act[1] could not run: InputAction /Game/Input/IA_Interact.IA_Interact not found`), and the run moves on. A run that ends mid act fails the act and the step.
- **Manual runs** (1.8) and **replays** do not run `arrange` or `act`; the step's `act_result` is `skipped` with the reason.
- **Reporting.** `step_acted` (section 3) and `steps[].act_result` (section 4) carry what each entry did and how long the act took.

## 6. A daemon extension

An extension runs actions in process and reads the same event log:

```js
export function activate(api) {
  if (!(api.features ?? []).includes("events.script")) return; // an older daemon

  api.events.subscribe((e) => {
    if (!e.type.startsWith("script.")) return;
    // e.id, e.type, e.at, e.data (the payload in section 3)
  });

  api.route("POST", "/verdict", async (req) => {
    const { run_id, step, verdict, note } = req.body;
    const r = await api.callAction("pie", "verdict", { run_id, step, verdict, ...(note ? { note } : {}) });
    return { status: r.isError ? 409 : 200, body: r }; // r = { isError, content }: the MCP result blocks
  });
}
```

- `api.callAction("pie", "verdict", { run_id, step, verdict, note? })` posts a verdict exactly as an MCP client's `pie(verdict)` does, recorded with `verdict_source: "action"`. An extension whose own agent judged the step passes `source: "agent"` and `evidence: { note, captures? }`, recorded with `verdict_source: "agent"`.
- `api.callAction("pie", "script_run", {...}, { elicit })` puts the verdict form (section 2) to the extension's `elicit` instead of an MCP client, when the extension passes one (feature `callAction.elicit`). The follower keeps that `elicit` after the call returns, so the form can arrive later. Without it no form is raised, and the extension collects verdicts itself from `script.verdict_requested`.
- The extension never calls the bridge's `pie_script_*` methods directly; the action layer keeps the follower and the stream in step.
- `api.models.register(provider)` (extension API 2, feature `models.provider`) supplies the model flow agent steps run on, the playtest flow's among them, ahead of `ANTHROPIC_API_KEY`. The newest registration still live wins, and the returned function removes it. `provider.complete(request)` takes `{ system?, prompt?, messages?, model?, maxTokens?, temperature?, stop?, schema?, tools?, toolChoice?, signal? }` and answers `{ text, parsed?, toolCalls?, finishReason?, usage?, model? }`; the types are `ModelProvider`, `ModelRequest` and `ModelResponse` from `ue-mcp/daemon`.

## 7. Versioning

- Additive. `DAEMON_API_VERSION` is unchanged. `EXTENSION_API_VERSION` is 2 since `api.models`; the daemon serves extension API 1 to 2 (`EXTENSION_API_MIN_VERSION` 1), so an extension declaring `api: { min: 1, max: 1 }` keeps loading.
- Feature flag `events.script` in the extension API's `api.features` marks a daemon that publishes these events. An MCP or HTTP client without the extension API detects it by the first `script.*` event, or by the ue-mcp version.
- New event types, new payload fields and new `wait.state` or `elicitation` values may be added without a version change. A client ignores what it does not know.
- Renaming or removing a field, an event type or a form property is a breaking change and bumps `EXTENSION_API_VERSION`.
- `results.json` changes additively within `version: 1`.
