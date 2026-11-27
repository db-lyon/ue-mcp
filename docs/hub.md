---
description: "The Playtest Hub: clips, tracks and arrangements to compose a scenario; tests and suites to grade it; runs on the open editor or a headless one, through the daemon API and the hub MCP category."
---

# Playtest Hub

The Playtest Hub composes playtests and grades them, and keeps the two apart. Composing never grades anything, and testing never drives anything.

| Composing | What it is |
|---|---|
| **Clip** | A reusable performance for a player: Input Action lanes. A clip has no checks. |
| **Track** | One player and a timeline of clip instances. |
| **Arrangement** | The scenario: a level, its tracks, one Checks track and named regions. It plays in one Play In Editor session. |

| Testing | What it is |
|---|---|
| **Test** | An arrangement, or one of its regions. It passes when all of its checks pass. |
| **Suite** | A named list of tests and suites. It owns health, pass-rate history, flaky marks and quarantine. |

The hub runs in the project's daemon, which owns the data, validation, runs and runners. An agent reaches it through the `hub` MCP category and a UI through the HTTP routes below; both call the same code.

## Files

Clips, arrangements and suites are JSON files in the project, so they are versioned and merged with it:

```
<project>/PlaytestHub/clips/<id>.json
<project>/PlaytestHub/arrangements/<id>.json
<project>/PlaytestHub/suites/<id>.json
```

The file name is the id: lowercase letters, digits and dashes. Every write uses one key order, two-space indentation and a final newline, and sorts notes, keys, instances, checks and regions by time, so the same content always gives the same bytes and two people's edits merge line by line. A file that does not read is listed under `problems` in the overview and left untouched.

Runs, the queue and schedules belong to the machine that ran them, under `~/.ue-mcp/data/<project>/hub`.

## Clips

A clip has a `rate` in frames per second (60 by default), a `length` in frames and Input Action lanes (`input`). Each lane holds an Input Action's raw value, after its mapping's modifiers and before its own: notes for a Boolean action, and curve keys or notes holding a value for an axis. `performer` is `player`, the one kind there is, and may be left out. A `checks` field refuses the clip, and so does any other kind of lane.

## Arrangements

An arrangement has a `level` (a map's package path), a `rate`, and:

- **tracks**, each with a `performer` (`{kind: "player", client}`: client 0 is the editor's own player, 1 to 15 a remote client), `mute`, `solo` and `instances` (`{ id, clip, at }`, `at` in the arrangement's frames). An instance's clip must exist, and instances on one track may not overlap.
- **checks**, the Checks track. Each has an `at`, a `length` and a `mode`:

| Mode | Passes when |
|---|---|
| `by_end` | `expect` holds on any frame before the check ends. |
| `throughout` | `expect` holds on every frame of the check. |
| `hold` | Playback waits at the check's start until `expect` holds, failing after its length (or `timeout` frames when the length is 0). |
| `manual` | A person or an agent gives a pass or fail verdict, following `instruction`. PIE pauses until then. `personOnly` refuses an agent's verdict. |

- **regions**, named spans (`{ id, name, start, end }`) that may not overlap.

A run plays the audible tracks: none muted, and only the soloed ones when any track is soloed.

### Performers

`GET /v1/hub/performers` lists what a track can drive: the editor's player and the first remote clients.

### Auditions

`POST /v1/hub/play` with `{ test, from?, loop? }` plays an arrangement or one region on the open editor so you can watch it while composing. `from` starts at that frame, the playhead, and skips checks that start before it. `loop` goes back to the loop start at the end until stopped: the playhead, else the region's start, else frame 0. Checks are judged so they show, manual ones are left out, and nothing goes into the run history. It is refused while the editor is in use. `GET /v1/hub/play` reads it, with the loop's `pass`, and `POST /v1/hub/play/stop` stops it.

### Recording

`POST /v1/hub/record/start` records the editor's player's Input Actions into a new clip on the open editor. With `clip`, `from` and `to`, it punches in: the clip plays up to `from` with the person's input locked out, then only that span is replaced. `POST /v1/hub/record/stop` saves the take.

`GET /v1/hub/record` (the agent's `read_recording`) answers the recording under way, or null. The daemon keeps it from start to stop, so a client that reloads mid-take can show the take and stop it. A client may pass `arrangement`, `track`, `at` and `seconds` to `record/start` to say where the take lands and how long it runs; the daemon keeps them and hands them back here, and does not act on them.

## Tests and suites

A test's id is its arrangement's id, or `<arrangement>:<region>` for a region. A region test plays that region and judges the checks that start inside it. A test with no checks cannot be queued.

A suite's `items` are `{ test }` and `{ suite }` entries; suites may nest but never in a loop. A test's health is `running`, `failing`, `flaky`, `passing`, `never_run` or `missing`. It is flaky when its latest five results on one revision mix passes and failures, and the checks that flip are named. A suite's health rolls up its tests, nested suites' included, except quarantined ones, which still run but do not count. A test marked flaky in `flaky` counts as flaky whatever its runs say. The pass-rate history has one point per queueing that ran any of the suite's tests.

## Runs

Queueing a test or a suite adds one item per test. Each plays on a runner:

- **A hidden editor** (the default when it fits). ue-mcp launches an editor with `-nullrhi -unattended` and no window on its own copy of the project, under `~/.ue-mcp/data/<project>/hub/headless`, mirrored from the project before each launch without `Saved`, `Intermediate` or caches. It starts only when it fits in memory beside the headroom kept for the rest of the machine (at least 4 GB, or 12%), and before anything is sent it asks the editor which project it has open and refuses to go on unless it is the copy. It stays up between runs and closes after ten idle minutes or when the daemon stops.
- **The open editor**, under the hub's lease: when the hidden editor does not fit, when the test has manual checks (a person judges them in the open editor), or when `runner: "editor"` is asked for. Each queued item's `runnerReason` says why it went where it did. An item starts only while nothing else holds the editor: Play In Editor the hub did not start, a dialog, an editor operation, another session's hold or transaction, or another client's calls. A scheduled item also waits for this computer and every other session to go quiet. `POST /v1/hub/release` stops the hub's run and pauses the queue.

A client may bring its own headless runner through `api.hub.configure({ headless })`. `UE_MCP_HUB_HEADLESS=0` turns ue-mcp's own off.

A run records its test, the runner, the source control revision it ran on, each check's status, frame and message, and any verdict with who gave it. Headless and editor runs go into the same history.

### From the command line

```
ue-mcp hub run <test|suite> [--headless | --editor] [--report <file>] [--timeout <seconds>]
```

queues the test or every test of the suite through the project's daemon, waits for the runs to end and writes a JSON report (`hub-report.json` by default) with every run, what was skipped and why, and `ok`. It exits 0 only when every run passed, 1 otherwise, and 2 on a bad command.

## Routes

Each answers JSON. A body that does not read answers 400 with `findings`, each a `path`, a `message` and, for an unknown id, `candidates`.

| Route | What it does |
|---|---|
| `GET /v1/hub` | Everything: clips, arrangements, suites, tests, queue, lease, recent runs, schedules, problems |
| `GET/POST /v1/hub/clips`, `GET/PUT/DELETE /v1/hub/clips/:id` | Clips |
| `POST /v1/hub/record/start`, `GET /v1/hub/record`, `POST /v1/hub/record/stop` | Record a performance into a new clip, or punch into one (`clip`, `from`, `to`); read the recording under way |
| `GET/POST /v1/hub/arrangements`, `GET/PUT/DELETE /v1/hub/arrangements/:id` | Arrangements |
| `GET /v1/hub/performers` | What a track can drive |
| `POST /v1/hub/play`, `GET /v1/hub/play`, `POST /v1/hub/play/stop` | Audition an arrangement or region from the playhead, looping when asked |
| `GET /v1/hub/tests`, `GET /v1/hub/tests/:id` | Tests with health, checks and runs |
| `GET/POST /v1/hub/suites`, `GET/PUT/DELETE /v1/hub/suites/:id` | Suites |
| `POST /v1/hub/suites/:id/quarantine`, `POST /v1/hub/suites/:id/flaky` | `{ test, on? }` |
| `GET /v1/hub/runs`, `GET /v1/hub/runs/:runId` | Runs, newest first |
| `POST /v1/hub/runs/:runId/verdict`, `POST /v1/hub/runs/:runId/stop` | A manual check's verdict; stop a run |
| `GET/POST /v1/hub/queue`, `DELETE /v1/hub/queue/:id`, `PUT /v1/hub/queue/paused` | The queue |
| `GET /v1/hub/lease`, `POST /v1/hub/release` | The editor lease |
| `GET/POST /v1/hub/schedules`, `PUT/DELETE /v1/hub/schedules/:id` | Run a suite or a test on chosen weekdays at a local time |

Every change publishes `hub.changed` (`{ what, id?, runId? }`) on `GET /v1/events`, and the lease publishes `hub.lease`.

## MCP tools

The `hub` category has one action per route above: `write_clip`, `write_arrangement` and `write_suite` take the file's JSON as `body`; `run` queues a test or a suite; `verdict` and `stop_run` act on a run; `play`, `read_play` and `stop_play` audition one. Every action reads and writes the same files and runs as the routes.
