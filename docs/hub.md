---
description: "The Playtest Hub: goals and milestones tracked per project, the playtests that prove them, run evidence, and plans that run under an editor lease, through the daemon API and the hub MCP category."
---

# Playtest Hub

The Playtest Hub tracks what a game must do and whether its playtests (PIE test scripts, see [PIE testing](pie-scripts.md)) show it does. A goal names the playtests that prove it, a milestone is a goal that requires other goals, and each playtest's runs are the evidence. Status is worked out on every read from those runs and never stored.

The hub runs in the project's daemon, so every client sees the same goals, evidence and queue: an MCP client through the `hub` category, a UI through the HTTP routes below.

## Data

Everything is kept on this computer, outside the game repository, in `~/.ue-mcp/data/<project>/hub/` (`<project>` is the daemon's project key, the folder `~/.ue-mcp/daemons/<key>.json` is named for):

| Folder or file | Holds |
|---|---|
| `goals/` | One file per goal or milestone, with its graph, owner, priority and due date |
| `checks/` | What a playtest says about itself: its title and links |
| `signoffs/` | Sign-offs, with the revision and run ids they were given against |
| `rules/` | The project's rule for flaky and stale playtests |
| `runs/` | The run history, an append-only log of segments, and `aliases.json` for scripts that moved |
| `queue.json` | The plan queue and the schedules |

Goals refer to playtests by asset path. A script missing on the current checkout shows as missing; the goal never forks. `GET /v1/hub/store` exports every record and `POST /v1/hub/store` merges an export back by id, which is how a goal set moves between computers. A project that kept goals in `QA/Goals` has them imported once into an empty store, and the files are never changed.

## What a run tested

`pie(script_run)` and `pie(script_replay)` write `provenance.json` beside the run's `results.json` in `Saved/MCPObservations/<run_id>/`: the source control kind, revision and number of changed files, a hash of the script's definition, the engine version, and the PIE players. Health, flaky and stable-run counts compare only runs on the same provenance. A `provenance` object in `results.json` itself takes precedence over the file.

## The editor lease

The editor is the developer's workspace first. The hub starts a plan item only while nothing else holds it: no Play In Editor session it did not start, open dialog, editor operation, run from another client, or call from another MCP client. It also defers to the [consent ledger](configuration.md#several-agents-on-one-editor): a session holding the editor with `editor(hold)`, one with an editor transaction open, and an open request to stop, restart or rebuild the editor each keep the next item waiting, with that reason in `waiting`. While an item runs the hub holds a lease, tells the editor, and acts only on the run and the Play In Editor session that item started. `POST /v1/hub/release` stops what the hub holds and pauses the queue; after the editor reconnects, queued work waits for a person to resume it.

A person's Run (`POST /v1/hub/plans` and the playtest run route) counts as that person resuming. It resumes a paused queue and, with nothing on the editor, starts ahead of whatever waited behind the pause. Behind running work it queues, or goes first with `next: true`. The answer carries a `placement`: `started`, `waiting` with the reason the editor is not free, or `queued` with its position. An agent's plan never resumes a paused queue.

A schedule runs a plan on chosen days at a local time, unattended unless it says otherwise, so a playtest with a manual step is skipped. It never takes the editor from anyone. When it comes due, it starts only if the queue is running and empty, nothing above holds the editor, every other session has been quiet for the consent idle window, and nobody has used the computer lately. Until then it waits with the reason in `waiting`. After an hour it gives up and records the reason in `lastSkipped`.

A manual step holds its run, with PIE paused, until a verdict is given or the wait runs out, which leaves the run unjudged rather than failed.

## Routes

All under `/v1/hub`, with the daemon's bearer token. A bad body answers 400, an unknown id 404, and a request that cannot be met now 409, each with `{ error }`. Ids in paths are URL-encoded.

| Route | Purpose |
|---|---|
| `GET /v1/hub` | Overview: goals with status, playtests with health, attention list, queue, recent runs |
| `GET`, `PUT`, `DELETE /v1/hub/goals/<id>`, `POST /v1/hub/goals` | Read, change, delete, create a goal |
| `POST`, `PUT`, `DELETE` under `/v1/hub/goals/<id>/checks`, `/requires` | Add, reorder, remove a goal's playtests and required goals |
| `POST`, `DELETE /v1/hub/goals/<id>/sign-off` | Sign a goal off, or clear it |
| `POST /v1/hub/goals/<id>/write-playtests` | Ask a client that can write playtests to do it (409 when none can) |
| `GET`, `PUT /v1/hub/checks/<check id>` | A playtest's health and runs; its title and links |
| `POST /v1/hub/checks/<check id>/run` | Run one playtest |
| `PUT /v1/hub/rules` | The flaky and stale rule |
| `GET`, `POST /v1/hub/store` | Export and import every record |
| `GET /v1/hub/runs`, `/runs/<id>`, `/runs/<id>/results` | History, one run with its steps and series, its `results.json` |
| `POST`, `DELETE /v1/hub/runs/<id>/links` | Link a run to a tracker item |
| `GET /v1/hub/queue`, `PUT /v1/hub/queue/paused` | The plan queue; pause or resume it |
| `POST /v1/hub/plans`, `POST /v1/hub/plans/<id>/cancel` | Run a plan, cancel one |
| `POST /v1/hub/plans/<id>/move` | Move a queued plan to `{ to }`, 0 for next |
| `POST /v1/hub/schedules`, `PUT`, `DELETE /v1/hub/schedules/<id>` | Create, change, delete a schedule |
| `GET /v1/hub/lease`, `POST /v1/hub/release` | The hub's hold on the editor; release it |
| `POST /v1/hub/fix` | Queue fix loops for failing playtests (409 when no client can run one) |

`DAEMON_ROUTES` in `ue-mcp/daemon` names each as `hub.<name>`, under the `hub` feature in `/v1/health`.

Deleting a goal or milestone removes it and its sign-off, and every goal or milestone that required it stops requiring it. Deleting a milestone does not delete the goals it requires. Playtests are not touched: they stay in the library, on any other goal or milestone that lists them, with their titles, links and past runs. While a plan made for the goal is queued or running, the delete answers 409 naming the plan; cancel it or let it finish, then retry the delete. A daemon that behaves this way lists `hub.goalDelete` in `/v1/health`.

The hub's types and rules (`checkHealth`, `goalStatus`, `rollUpGoals`, `buildAttention` and the rest) are exported from `ue-mcp/daemon` too.

## Events

On `GET /v1/events`:

| Event | Data |
|---|---|
| `hub.changed` | `{ what, goalId?, runId?, planRunId? }`: what changed (`goals`, `checks`, `runs`, `queue`, `attention`), gathered over 200 ms. A client rereads what it shows |
| `hub.lease` | `{ lease }`: the hub's hold on the editor, or `null` |
| `hub.held` | `{ held }`: the runs holding at a manual step for a verdict |

## MCP tools

The `hub` category:

| Action | Does |
|---|---|
| `list_goals`, `read_goal`, `read_check` | Goals with status and progress; one goal with its playtests; one playtest with its runs |
| `create_goal`, `update_goal` | Create or change a goal or milestone |
| `add_link`, `remove_link` | Link a goal, a playtest or a run to a tracker item by URL; the hub never contacts it |
| `require_goal`, `unrequire_goal` | Put a goal under a milestone, or take it out; a loop is refused |
| `run_plan` | Queue a plan for a goal, or for named playtests, under the lease |
| `read_queue` | The queue, why the next item waits, and the finished plans |

`run_plan` starts nothing while the editor is held, and returns the queued plan at once. The category needs the project's daemon; a server running in process has no hub.

## The arrangement

The hub arranges clips the way Ableton Live's Arrangement view does (#2113). A track is a timeline of clip instances with empty space between them, and a scene is a set of tracks played together. On the wire a track is a goal and a scene is a milestone, so the goal routes above keep working on them.

- **Instances.** An instance places a clip on a track at a start frame (60 frames a second by default): a [timeline clip](pie-clips.md), trimmed with `in` and `out` in its own frames, or a [step-list playtest](pie-scripts.md), which settles by condition and so reserves a `length` instead. An instance refers to its clip, so editing the clip changes every instance of it; making an instance unique copies the clip asset and points the instance at the copy. Instances on a track may not overlap, and their ids are never reused on it.
- **Tracks** have an order, a color, a player (0, the editor's own player, by default; N for remote client N), and mute and solo. A run leaves muted tracks out, and while any track is soloed only soloed tracks play.
- **Scenes** have an order, a color and the tracks they play, in order.

### Runs

A run plays in one PIE session. The daemon reads each clip and composes the timelines into one inline clip for the [clip player](pie-clips.md#playing-a-clip), each instance's lanes, events, check notes and regions offset to its start, cut to its trim and prefixed with its key (`i2.c1` on one track, `2-i2.c1` when several tracks play). Each lane drives its track's player. A step-list instance splits the run: the clips before it play, the script plays with `pie(script_run)` in the same session, and the timeline resumes at its end. A hold check holds the whole run.

| Run | Plays | Record |
|---|---|---|
| Track | One track's timeline | `kind: "track"`, target the track |
| Scene | The scene's tracks together | `kind: "scene"`, target the scene |
| Arrangement | Every track together | `kind: "arrangement"`, target `arrangement` |

Each run is one record whose `arrangement` gives every instance's result at its place (`instances[]`: track, id, start and end frames, status, checks passed, and `awaiting`: each manual check waiting for a verdict as `{ check, instruction }`, where `check` is what the verdict route takes), each track's status (`tracks[]`), and the segments it played as (`segments[]`), so a client can draw the playhead at a segment's `offset` plus the `position` of its clip run's `clip.position` events. A run passes when every instance passes. A track's status comes from its track and arrangement runs, and a scene's from its scene runs; before the first one, each rolls up from its playtests and tracks as before.

Each goal that existed before became a track once, its playtests laid end to end, each reserving its newest run's duration. Each milestone became a scene that plays the goals it required. Nothing else changed, so an older client reads the same records.

### Routes

| Route | Purpose |
|---|---|
| `GET /v1/hub/set` | Every track (fields, timeline, `layout` with each instance's resolved length and, for a timeline clip, its own `clipRate` and `clipLength`, status) and scene (tracks, status), the text form, and findings |
| `POST /v1/hub/set/edit` | `{ ops }`, as op objects or text lines, all or none. A name or clip that matches nothing answers 400 with `finding.candidates` |
| `GET`, `PUT /v1/hub/goals/<id>/arrangement`, `POST .../arrangement/edit` | One track's timeline, replaced or edited with instance ops |
| `GET /v1/hub/clips?q=&kind=` | Timeline clips and step-list playtests, for a browser |
| `POST /v1/hub/run` | `{ scope: { kind: "track" \| "scene", goalId } \| { kind: "arrangement" }, next? }`: queue one run |
| `POST /v1/hub/runs/<id>/verdict` | `{ check, verdict, note? }`: a manual check's verdict in a running track, scene or arrangement |

A daemon that has these lists `hub.arrangement` in `/v1/health`.

### Text form and ops

```text
set rate=60
track walk-k3f9 Walk color=green player=0
  clip i1 /Game/PIEStudio/Clips/CL_Walk 0 out=240
  script i2 /Game/Tests/PT_OpenDoor 240 length=600
track guard-x81a Guard color=red player=1 mute
scene boss-0p2d "Boss fight" tracks=walk-k3f9,guard-x81a
```

Ops, one per line; a track or scene is named by its id or its exact name, and an instance as `<track>/<id>`:

| Ops | |
|---|---|
| Tracks | `track add "Name" [color=C] [player=N] [at=N]`, `track delete`, `rename`, `color`, `move <t> to <N>`, `mute <t> on\|off`, `solo <t> on\|off`, `player <t> <N>` |
| Scenes | `scene add "Name"`, `scene insert "Name" at <N>`, `scene delete`, `rename`, `color`, `duplicate`, `move <s> to <N>`, `scene track <s> add\|remove <t>` |
| Instances | `add <t> <clip\|script> <path> [at <frame>]`, `duplicate <t>/<i> [at <frame>]`, `copy <t>/<i> to <t2> [at <frame>]`, `move <t>/<i> to <frame>`, `by <frames>` or `to <t2> [at <frame>]`, `trim <t>/<i> in=N out=N` or `length=N`, `rename <t>/<i> "Name"`, `color <t>/<i> <C>`, `delete <t>/<i>`, `unique <t>/<i>`, `retarget <t>/<i> [clip\|script] <path>` |

`retarget` points an instance at another clip and keeps its place, name, color and trim; naming the other kind (a step-list playtest in place of a timeline clip, or the reverse) drops the trim. A path that names no clip fails with the closest ones as candidates.

Colors are `red`, `orange`, `yellow`, `lime`, `green`, `teal`, `cyan`, `blue`, `indigo`, `violet`, `pink` and `gray`. The set model also carries Session view slots (`slot ...` ops and `slot` runs) for the grid that comes later; no view shows them yet.

`track delete` and `scene delete` remove the record as `DELETE /v1/hub/goals/<id>` does, its sign-off included, and are refused the same way while a run made for it is queued or running. A track's slots go with it; its clips stay.

The `hub` MCP category has the same through `read_set`, `edit_set` (`ops`), `run` (`scope`: `track <t>`, `scene <s>` or `arrangement`) and `read_run`.

## Clients

A client with ideas ue-mcp has none of, such as chat sessions and run cards, brings them through `api.hub.configure(options)` in the extension API (feature `hub`): its own way of starting and following runs (`broker`), a fix loop (`fixer`), a flow that writes playtests (`writePlaytests`), the account a sign-off is from (`signer`), whether an agent may start a plan (`playtestRuns`), an on and off switch (`enabled`) and how long the desktop has been idle (`inputAgeMs`). Every member has a default, and the daemon rebuilds the hub around what a client configures, with its files and queue unchanged.
