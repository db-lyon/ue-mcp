---
description: "The Playtest Hub: tracks and scenes kept per project, the clips that prove them, run evidence, and plans that run under an editor lease, through the daemon API and the hub MCP category."
---

# Playtest Hub

The Playtest Hub tracks what a game must do and whether its clips (PIE test scripts, see [PIE testing](pie-scripts.md)) show it does. A track names the clips that prove it, a scene is a track that requires other tracks, and each clip's runs are the evidence. Status is worked out on every read from those runs and never stored.

The hub's data and API keep their original names: a track is a goal, a scene is a goal of kind `milestone`, and a clip is a check (`playtests` in `run_plan`). The routes, fields and MCP actions below use those names.

The hub runs in the project's daemon, so every client sees the same tracks, evidence and queue: an MCP client through the `hub` category, a UI through the HTTP routes below.

## Data

Everything is kept on this computer, outside the game repository, in `~/.ue-mcp/data/<project>/hub/` (`<project>` is the daemon's project key, the folder `~/.ue-mcp/daemons/<key>.json` is named for):

| Folder or file | Holds |
|---|---|
| `goals/` | One file per track or scene, with its graph, owner, priority and due date |
| `checks/` | What a clip says about itself: its title and links |
| `signoffs/` | Sign-offs, with the revision and run ids they were given against |
| `rules/` | The project's rule for flaky and stale clips |
| `runs/` | The run history, an append-only log of segments, and `aliases.json` for scripts that moved |
| `queue.json` | The plan queue and the schedules |

Tracks refer to clips by asset path. A script missing on the current checkout shows as missing; the track never forks. `GET /v1/hub/store` exports every record and `POST /v1/hub/store` merges an export back by id, which is how a set of tracks moves between computers. A project that kept goals in `QA/Goals` has them imported once into an empty store, and the files are never changed.

## What a run tested

`pie(script_run)` and `pie(script_replay)` write `provenance.json` beside the run's `results.json` in `Saved/MCPObservations/<run_id>/`: the source control kind, revision and number of changed files, a hash of the script's definition, the engine version, and the PIE players. Health, flaky and stable-run counts compare only runs on the same provenance. A `provenance` object in `results.json` itself takes precedence over the file.

## The editor lease

The editor is the developer's workspace first. The hub starts a plan item only while nothing else holds it: no Play In Editor session it did not start, open dialog, editor operation, run from another client, or call from another MCP client. It also defers to the [consent ledger](configuration.md#several-agents-on-one-editor): a session holding the editor with `editor(hold)`, one with an editor transaction open, and an open request to stop, restart or rebuild the editor each keep the next item waiting, with that reason in `waiting`. While an item runs the hub holds a lease, tells the editor, and acts only on the run and the Play In Editor session that item started. `POST /v1/hub/release` stops what the hub holds and pauses the queue; after the editor reconnects, queued work waits for a person to resume it.

A person's Run (`POST /v1/hub/plans` and the clip run route) counts as that person resuming. It resumes a paused queue and, with nothing on the editor, starts ahead of whatever waited behind the pause. Behind running work it queues, or goes first with `next: true`. The answer carries a `placement`: `started`, `waiting` with the reason the editor is not free, or `queued` with its position. An agent's plan never resumes a paused queue.

A schedule runs a plan on chosen days at a local time, unattended unless it says otherwise, so a clip with a manual step is skipped. It never takes the editor from anyone. When it comes due, it starts only if the queue is running and empty, nothing above holds the editor, every other session has been quiet for the consent idle window, and nobody has used the computer lately. Until then it waits with the reason in `waiting`. After an hour it gives up and records the reason in `lastSkipped`.

A manual step holds its run, with PIE paused, until a verdict is given or the wait runs out, which leaves the run unjudged rather than failed.

## Routes

All under `/v1/hub`, with the daemon's bearer token. A bad body answers 400, an unknown id 404, and a request that cannot be met now 409, each with `{ error }`. Ids in paths are URL-encoded.

| Route | Purpose |
|---|---|
| `GET /v1/hub` | Overview: tracks with status, clips with health, attention list, queue, recent runs |
| `GET`, `PUT`, `DELETE /v1/hub/goals/<id>`, `POST /v1/hub/goals` | Read, change, delete, create a track or scene |
| `POST`, `PUT`, `DELETE` under `/v1/hub/goals/<id>/checks`, `/requires` | Add, reorder, remove a track's clips and required tracks |
| `POST`, `DELETE /v1/hub/goals/<id>/sign-off` | Sign a track off, or clear it |
| `POST /v1/hub/goals/<id>/write-playtests` | Ask a client that can write clips to do it (409 when none can) |
| `GET`, `PUT /v1/hub/checks/<check id>` | A clip's health and runs; its title and links |
| `POST /v1/hub/checks/<check id>/run` | Run one clip |
| `PUT /v1/hub/rules` | The flaky and stale rule |
| `GET`, `POST /v1/hub/store` | Export and import every record |
| `GET /v1/hub/runs`, `/runs/<id>`, `/runs/<id>/results` | History, one run with its steps and series, its `results.json` |
| `POST`, `DELETE /v1/hub/runs/<id>/links` | Link a run to a tracker item |
| `GET /v1/hub/queue`, `PUT /v1/hub/queue/paused` | The plan queue; pause or resume it |
| `POST /v1/hub/plans`, `POST /v1/hub/plans/<id>/cancel` | Run a plan, cancel one |
| `POST /v1/hub/plans/<id>/move` | Move a queued plan to `{ to }`, 0 for next |
| `POST /v1/hub/schedules`, `PUT`, `DELETE /v1/hub/schedules/<id>` | Create, change, delete a schedule |
| `GET /v1/hub/lease`, `POST /v1/hub/release` | The hub's hold on the editor; release it |
| `POST /v1/hub/fix` | Queue fix loops for failing clips (409 when no client can run one) |

`DAEMON_ROUTES` in `ue-mcp/daemon` names each as `hub.<name>`, under the `hub` feature in `/v1/health`.

Deleting a track or scene removes it and its sign-off, and every track or scene that required it stops requiring it. Deleting a scene does not delete the tracks it requires. Clips are not touched: they stay in the library, on any other track or scene that lists them, with their titles, links and past runs. While a plan made for the track is queued or running, the delete answers 409 naming the plan; cancel it or let it finish, then retry the delete. A daemon that behaves this way lists `hub.goalDelete` in `/v1/health`.

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
| `list_goals`, `read_goal`, `read_check` | Tracks and scenes with status and progress; one track with its clips; one clip with its runs |
| `create_goal`, `update_goal` | Create or change a track, or a scene with `kind: "milestone"` |
| `add_link`, `remove_link` | Link a track, a clip or a run to a tracker item by URL; the hub never contacts it |
| `require_goal`, `unrequire_goal` | Add a track to a scene, or take it out; a loop is refused |
| `run_plan` | Queue a plan for a track, or for named clips (`playtests`), under the lease |
| `read_queue` | The queue, why the next item waits, and the finished plans |

`run_plan` starts nothing while the editor is held, and returns the queued plan at once. The category needs the project's daemon; a server running in process has no hub.

## The arrangement

The hub arranges clips the way Ableton Live's Arrangement view does (#2113). A track is a timeline of clip instances with empty space between them, and a scene is a set of tracks played together. On the wire a track is a goal and a scene is a milestone, so the goal routes above keep working on them.

- **Instances.** An instance places a clip on a track at a start frame (60 frames a second by default): a [timeline clip](pie-clips.md), trimmed with `in` and `out` in its own frames, or a [step-list clip](pie-scripts.md), which settles by condition and so reserves a `length` instead. An instance refers to its clip, so editing the clip changes every instance of it; making an instance unique copies the clip asset and points the instance at the copy. Instances on a track may not overlap, and their ids are never reused on it.
- **Tracks** have an order, a color, a performer, and mute and solo. A run leaves muted tracks out, and while any track is soloed only soloed tracks play.
- **Scenes** have an order, a color and the tracks they play, in order.

### Performers

A track drives one performer: the editor's own player, a remote client, or any actor in the level. On the wire `performer` is `{ kind: "player", client }` (0 the editor's player, N remote client N) or `{ kind: "actor", actor }`, where `actor` is the actor's label, object name or path. A track that names no performer drives the editor's player, so every track made before performers is a player track and keeps its `player` field. The set also answers `player` on every track for clients that read it, 0 for an actor track.

A clip's lanes each have a capability, and a performer takes some of them:

| Performer | Input | Move | Action | Value |
|---|---|---|---|---|
| Player | Yes | No | Yes, on its pawn | Yes, on its pawn |
| Actor | No | Yes | Yes | Yes |

Each lane of a clip drives its track's performer when the track names anyone but the editor's player, and otherwise the lane's own performer, then the clip's, then the editor's player. A run that would play a lane its performer does not take is refused, with a finding naming the lane and the capabilities the performer does take. In a scene, two tracks that move the same actor, or drive one of its values, at the same time are refused as two tracks driving one player's Input Action are; two tracks may call the same actor's functions. The [clip page](pie-clips.md#actor-lanes) describes the lanes.

`GET /v1/hub/performers?arrangement=` lists what a track can drive, each with its `label` and the `capabilities` it takes: the editor's player and remote clients 1 to 3, then the actors of the level the editor has open, pawns first, without the actors a level keeps for itself, such as lights, volumes and navigation data. Without an editor it lists only the players and says why in `reason`. A daemon that has this lists `hub.performers` in `/v1/health`.

### Arrangements

A project holds any number of named arrangements, the way a folder holds several Ableton Live Sets. Each has its own tracks and scenes and, optionally, a level: a map that its runs open before Play In Editor starts. A run that finds the map open already starts without reloading it, and a run whose map has unsaved changes does not start; the run says why. Clips stay one library, and every arrangement's instances refer to the same clip assets.

The tracks and scenes a project had before it held several arrangements are its first one, Main, whose id stays `main` when it is renamed. Duplicating an arrangement copies its tracks, scenes and instances under new ids, right after the original; the copies refer to the same clips. Deleting one deletes its tracks and scenes as `DELETE /v1/hub/goals/<id>` does, never a clip, and is refused for a project's last arrangement and while a run made in it is queued or running.

Runs, the queue, schedules, history and status belong to one arrangement. A request that names none gets the first arrangement, so a client that predates them keeps working on Main.

| Route | Purpose |
|---|---|
| `GET /v1/hub/arrangements` | Every arrangement in order, with its track and scene counts, level, status and newest run, and `default`, the one a request that names none gets |
| `POST /v1/hub/arrangements` | `{ title, level? }`: a new, empty arrangement at the end |
| `PUT /v1/hub/arrangements/<id>` | `{ title?, level? }`: rename it or set its level; `level: null` clears it |
| `POST /v1/hub/arrangements/<id>/duplicate` | `{ title? }`: a copy named "<title> copy" unless one is given |
| `DELETE /v1/hub/arrangements/<id>` | Delete it with its tracks and scenes |

`GET /v1/hub/set`, `GET /v1/hub/queue` and `GET /v1/hub/runs` take `?arrangement=` (an id or exact name), `POST /v1/hub/set/edit` takes `arrangement` in its body, and the arrangement run scope takes `arrangementId`. The set answers with the `arrangement` it shows, and plans and schedules carry their `arrangementId`. A daemon that has these lists `hub.arrangements` in `/v1/health`.

### Runs

A run plays in one PIE session. The daemon reads each clip and composes the timelines into one inline clip for the [clip player](pie-clips.md#playing-a-clip), each instance's lanes, events, check notes and regions offset to its start, cut to its trim and prefixed with its key (`i2.c1` on one track, `2-i2.c1` when several tracks play). Each lane drives its track's performer, which the composed lane names. A step-list instance splits the run: the clips before it play, the script plays with `pie(script_run)` in the same session, and the timeline resumes at its end. A hold check holds the whole run.

| Run | Plays | Record |
|---|---|---|
| Track | One track's timeline | `kind: "track"`, target the track |
| Scene | The scene's tracks together | `kind: "scene"`, target the scene |
| Arrangement | Every track of the arrangement together | `kind: "arrangement"`, target `arrangement` for Main and the arrangement's id for the others |

Each run is one record whose `arrangement` gives every instance's result at its place (`instances[]`: track, id, start and end frames, status, checks passed, and `awaiting`: each manual check waiting for a verdict as `{ check, instruction }`, where `check` is what the verdict route takes), each track's status (`tracks[]`), and the segments it played as (`segments[]`), so a client can draw the playhead at a segment's `offset` plus the `position` of its clip run's `clip.position` events. A run passes when every instance passes. A track's status comes from its track and arrangement runs, and a scene's from its scene runs; before the first one, each rolls up from its clips and tracks as before.

Each track that existed before was laid out once as a timeline, its clips end to end, each reserving its newest run's duration. Each scene now plays the tracks it required. Nothing else changed, so an older client reads the same records.

### Routes

| Route | Purpose |
|---|---|
| `GET /v1/hub/set` | Every track (fields, timeline, `layout` with each instance's resolved length and, for a timeline clip, its own `clipRate` and `clipLength`, status) and scene (tracks, status), the text form, and findings |
| `POST /v1/hub/set/edit` | `{ ops }`, as op objects or text lines, all or none. A name or clip that matches nothing answers 400 with `finding.candidates` |
| `GET`, `PUT /v1/hub/goals/<id>/arrangement`, `POST .../arrangement/edit` | One track's timeline, replaced or edited with instance ops |
| `GET /v1/hub/clips?q=&kind=` | Timeline clips and step-list clips, for a browser |
| `POST /v1/hub/run` | `{ scope: { kind: "track" \| "scene", goalId, from? } \| { kind: "arrangement", from? }, next? }`: queue one run. The answer is `{ plan, placement, runId }`; when the run starts at once, the answer comes once it has started and `runId` names it, and otherwise `runId` is null. `from` plays the timelines from that frame (at 60 per second), as from a playhead: clips that end before it are left out, a timeline clip it falls inside plays from that point, and a step-list clip it falls inside plays whole. The run's results stay at their places on the timelines, with `from` beside them |
| `POST /v1/hub/runs/<id>/verdict` | `{ check, verdict, note? }`: a manual check's verdict in a running track, scene or arrangement |

A daemon that has these lists `hub.arrangement` in `/v1/health`.

### Text form and ops

```text
set rate=60
track walk-k3f9 Walk color=green player=0
  clip i1 /Game/PIEStudio/Clips/CL_Walk 0 out=240
  script i2 /Game/Tests/PT_OpenDoor 240 length=600
track guard-x81a Guard color=red player=1 mute
track sentry-2b7c Sentry performer=actor:BP_Sentry_2
  clip i1 /Game/PIEStudio/Clips/CL_Patrol 0
scene boss-0p2d "Boss fight" tracks=walk-k3f9,guard-x81a
```

Ops, one per line; a track or scene is named by its id or its exact name, and an instance as `<track>/<id>`:

| Ops | |
|---|---|
| Tracks | `track add "Name" [color=C] [player=N \| performer=player:N\|actor:<name>] [at=N]`, `track delete`, `rename`, `color`, `move <t> to <N>`, `mute <t> on\|off`, `solo <t> on\|off`, `player <t> <N>`, `performer <t> player:N\|actor:<name>` |
| Scenes | `scene add "Name"`, `scene insert "Name" at <N>`, `scene delete`, `rename`, `color`, `duplicate`, `move <s> to <N>`, `scene track <s> add\|remove <t>` |
| Instances | `add <t> <clip\|script> <path> [at <frame>]`, `duplicate <t>/<i> [at <frame>]`, `copy <t>/<i> to <t2> [at <frame>]`, `move <t>/<i> to <frame>`, `by <frames>` or `to <t2> [at <frame>]`, `trim <t>/<i> in=N out=N` or `length=N`, `rename <t>/<i> "Name"`, `color <t>/<i> <C>`, `delete <t>/<i>`, `unique <t>/<i>`, `retarget <t>/<i> [clip\|script] <path>` |

`retarget` points an instance at another clip and keeps its place, name, color and trim; naming the other kind (a step-list clip in place of a timeline clip, or the reverse) drops the trim. A path that names no clip fails with the closest ones as candidates.

Colors are `red`, `orange`, `yellow`, `lime`, `green`, `teal`, `cyan`, `blue`, `indigo`, `violet`, `pink` and `gray`. The set model also carries Session view slots (`slot ...` ops and `slot` runs) for the grid that comes later; no view shows them yet.

`track delete` and `scene delete` remove the record as `DELETE /v1/hub/goals/<id>` does, its sign-off included, and are refused the same way while a run made for it is queued or running. A track's slots go with it; its clips stay.

The `hub` MCP category has the same through `read_set`, `edit_set` (`ops`), `list_performers`, `run` (`scope`: `track <t>`, `scene <s>` or `arrangement`, each optionally followed by `from <seconds>s` or `from <frame>`) and `read_run`. Each takes `arrangement`, an id or exact name, and works on the first arrangement without it; `read_set` lists every arrangement.

## Clients

A client with ideas ue-mcp has none of, such as chat sessions and run cards, brings them through `api.hub.configure(options)` in the extension API (feature `hub`): its own way of starting and following runs (`broker`), a fix loop (`fixer`), a flow that writes clips (`writePlaytests`), the account a sign-off is from (`signer`), whether an agent may start a plan (`playtestRuns`), an on and off switch (`enabled`) and how long the desktop has been idle (`inputAgeMs`). Every member has a default, and the daemon rebuilds the hub around what a client configures, with its files and queue unchanged.
