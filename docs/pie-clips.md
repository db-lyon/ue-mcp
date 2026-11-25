---
description: "Playtests as timelines on game time: input tracks, events, check notes and regions, written as data or recorded from play, then replayed and judged per region."
---

# Playtest clips

A playtest clip is a test laid out on a timeline of game time instead of an ordered list of steps. Input sits on one track per Input Action, setup and actions sit on an event track, and what the test checks sits on a check track as notes with a start, a length and a mode. Named regions divide the timeline, and results report against them.

People and agents edit the same clip. A person records input into it by playing; an agent writes and edits it as data through the actions on this page, plays it and adjusts it from the results. [Test scripts](pie-scripts.md) keep working as they are: a clip is a separate asset, and nothing converts one into the other.

Every action on this page is in the `pie` category.

## The clip

Times are frames at the clip's `rate`, frames per second of game time (default 60), and may be fractional. `clip_edit` snaps the times it writes to the clip's `grid`: `frame` (default), `1/N` for Nths of a second, or `free`, which keeps them as given. A recording keeps the exact frames it read input on.

### Input tracks

A track belongs to one Input Action and drives it for client 0, the editor's own player. It holds the raw value that reaches the action: the keys mapped to it combined, after each mapping's own modifiers (such as Negate on S for a move), before the action's own modifiers and triggers. Playback feeds that value to the action, so the action's modifiers and triggers run once, and rebinding a key does not change the clip.

- A Boolean action is a row of notes, each a `start` and a `length`. The action is held down for the note.
- An axis action (`axis1d`, `axis2d`, `axis3d`) is a curve of keys, each a time `t`, a value `v` and an `interp` of `linear` (default), `constant` or `cubic`. Before its first key the curve feeds nothing; after its last it holds the last value. Wherever the curve is zero nothing is fed, the same as no key being down.
- An axis track can also carry notes, each with a `value` held over its span. A note wins over the curve where they overlap.
- Notes on one track may not overlap.

### Events

An event fires once at its frame. Its `do` is a step's act entry (`place`, `console`, `capture`, `key`, `press`, `hold`, `tape`, `inject`) or an arrange entry (`{kind: spawn|set|call, ...}`), with the meanings [Arrange and act](pie-scripts.md#arrange-and-act) gives them. `wait_ms` is refused: on a timeline, the next event goes at a later frame instead. Menu keys go through `key`, which reaches the player's UI through Slate.

### Check notes

A check note judges an expectation, `expect: {value, op, target?}`, with the values and ops a step's `expect` takes ([Ops](pie-scripts.md#ops)). Check notes may overlap, and each takes the baseline of a `decreasing` or `increasing` op at its own start.

| Mode | Passes when | Fails when |
|---|---|---|
| `by_end` | The condition holds on any frame from the note's start to its end. A zero-length note asserts on one frame. | The note ends first. |
| `throughout` | The condition holds on every frame of the note. | One frame does not hold, or the value cannot be read. |
| `hold` | The condition holds. Playback stops advancing at the note's start until then, and the game keeps running. | It has waited its `length` in frames of game time, or the clip's `hold_timeout` (default 600) when its length is 0. Playback then carries on. |

A manual check (`manual: true` with an `instruction`) is a `hold` that pauses PIE until it has a verdict through `clip_verdict`. `agent_judgeable: false` takes only a person's verdict.

While playback stands at a `hold`, every track keeps feeding the value it has at that frame: a key held across the hold stays held, so walking towards a door until the character reaches it is one note and one hold.

### Regions

Regions are named spans, each a `start` and an `end`. They may not overlap. Each has a stable `id`, which results, Studio and recordings refer to.

## JSON and text

A clip is JSON:

```json
{
  "rate": 60,
  "length": 260,
  "regions": [{ "id": "r1", "name": "Walk forward", "start": 0, "end": 120 }],
  "tracks": [
    { "action": "/Game/Input/IA_Move.IA_Move", "type": "axis2d",
      "keys": [{ "id": "k1", "t": 0, "v": [0, 1], "interp": "constant" }, { "id": "k2", "t": 90, "v": [0, 0] }] },
    { "action": "/Game/Input/IA_Jump.IA_Jump", "type": "bool", "notes": [{ "id": "n1", "start": 40, "length": 6 }] }
  ],
  "events": [{ "id": "e1", "at": 0, "do": { "console": "stat fps" } }],
  "checks": [
    { "id": "c1", "start": 0, "length": 100, "mode": "by_end", "expect": { "value": "pos_x", "op": "increasing", "target": 200 } },
    { "id": "c2", "start": 100, "length": 0, "mode": "hold", "expect": { "value": "speed2d", "op": "lt", "target": 1 } }
  ]
}
```

| Field | Default | Meaning |
|---|---|---|
| `rate` | 60 | Frames per second of game time. Also the fixed timestep of a replay and the rate a take is resampled at. |
| `length` | 0 | Frames. 0 ends the clip at the last thing on it. |
| `grid` | `frame` | `frame`, `1/N` or `free`. |
| `map`, `profile`, `description` | | The level the clip plays in, the observation profile it arms, what it tests. |
| `fixed_timestep` | `true` | Play every frame as exactly 1/`rate` of game time. |
| `seed` | 0 | Applied with `FMath::RandInit` when playback starts; 0 leaves the random state alone. |
| `hold_timeout` | 600 | Frames a `hold` check waits when its own length is 0. |
| `start` | | `{location, rotation}`: where the player pawn is teleported before frame 0. A recording from frame 0 sets it. |
| `reference` | | A run id whose pawn track drift is measured against. A recording sets it to its own run. |
| `frame_times` | | Where a recording's frames landed, in frames, ascending. A fixed-timestep run takes each frame's delta from them over the range they cover, and 1/`rate` elsewhere. |
| `takes` | | Notes and keys a recording replaced, kept as history. |

Ids are filled in where missing (`n1`, `k1`, `c1`, `e1`, `r1`, after the highest of each already used). Input Actions may be given as package paths (`/Game/Input/IA_Jump`) and are stored as object paths.

The same clip in its compact text form, which `clip_read` returns as `text` and every action that takes a clip accepts as `text`:

```text
clip rate=60 length=260 grid=frame fixed_timestep=true seed=0 hold_timeout=600
region r1 0 120 "Walk forward"
track /Game/Input/IA_Move.IA_Move axis2d
  key k1 0 0,1 constant
  key k2 90 0,0
track /Game/Input/IA_Jump.IA_Jump bool
  note n1 40 6
event e1 0 {"console":"stat fps"}
check c1 0 100 by_end pos_x increasing 200
check c2 100 0 hold speed2d lt 1
```

| Line | Shape |
|---|---|
| `clip` | `clip key=value ...` with `rate`, `length`, `grid`, `fixed_timestep`, `seed`, `hold_timeout`. |
| `map`, `profile`, `description`, `reference` | The word and its value. |
| `start` | `start x,y,z [pitch,yaw,roll]` |
| `region` | `region <id> <start> <end> "name"` |
| `track` | `track <action> <bool\|axis1d\|axis2d\|axis3d>`, followed by its indented `note` and `key` lines. |
| `note` | `note <id> <start> <length> [value]`. An axis note needs its value. |
| `key` | `key <id> <t> <value> [linear\|constant\|cubic]` |
| `event` | `event <id> <at> {json}`, the entry as JSON to the end of the line. |
| `check` | `check <id> <start> <length> <by_end\|throughout\|hold> <value> <op> [target] [name="..."]`, or `check <id> <start> <length> manual "question" [agent_judgeable=false]`. |
| `recorded` | `recorded <check id> <frame> <game_ms> <value>`: how a hold resolved when the clip was recorded. |
| `timing` | `timing <frame>,<frame>,...`: the recorded frame times. |

A value is comma-separated with no spaces. A string with spaces is quoted, with `\"` and `\\` escapes. `-` as an id has one assigned. Lines starting with `#` are comments.

## Writing and checking a clip

| Action | Params |
|---|---|
| `clip_create` | `name`, `clip` or `text`, `package_path?` (default `/Game/PIEStudio/Clips`), `onConflict?` (`skip` (default), `replace` or `error`) |
| `clip_read` | `path`. Returns `clip`, `text` and `counts` (`tracks`, `notes`, `keys`, `events`, `checks`, `regions`, `takes`, `length`). |
| `clip_update` | `path`, `clip` or `text`. Replaces the clip; its `takes` are kept when the new clip carries none. |
| `clip_validate` | One of `path`, `clip` or `text`. Every finding in `errors`, `valid` when there are none and `saveable` when none of them stops a save. |
| `clip_list` | `directory?` (default `/Game`). Returns `clips` (`path`, `object_path`, `name`, `map`) and `count`. |
| `clip_delete` | `path`, `confirm=true`. |

A clip is checked whenever it is saved, against the level open in the editor and the clip's profile:

- **Structure.** Overlapping regions or notes, two keys on one frame, keys on a Boolean track, times before frame 0, duplicate ids, an event entry that does not parse, a `throughout` check on `triggered`.
- **What it names.** Each track's Input Action has to exist and have the track's value type. Each check's value has to resolve and suit its op, and the actors, classes and Input Actions events name have to exist, by the rules [Checks when a script is saved](pie-scripts.md#checks-when-a-script-is-saved) gives. A clip whose `map` is not the open level leaves its actors for the run to resolve.

Each finding is `{id, field, message, candidates?}`, where `id` names the note, key, check, event or region at fault, or the track's action. A structural problem refuses the save (`errorCode: "INVALID_CLIP"`, findings in `errors`); everything else saves and comes back as `warnings`, since a value may only resolve once PIE runs. `resolve_in_pie: true` on a check lets such a value through.

## Editing a clip

`clip_edit` (`path`, `ops`) applies operations in order, all or none. Times snap to the clip's grid.

| Op | Fields | Effect |
|---|---|---|
| `add` | `kind: note`, `track`, `start`, `length`, `value?` | A note on the track for that Input Action. |
| `add` | `kind: key`, `track`, `time`, `value`, `interp?` | A curve key. |
| `add` | `kind: track`, `action`, `type` | A track. |
| `add` | `kind: check`, and a check's fields | A check note. |
| `add` | `kind: event`, `at`, `do` | An event. |
| `add` | `kind: region`, `name`, `start`, `end` | A region. |
| `move` | `id`, `to` or `by` | Moves a note, key, check, event or region. |
| `trim` | `id`, `start?`, `length?` or `end?` | Changes a note's, check's or region's span. |
| `set` | `id`, fields | Sets fields of what `id` names. On a check, `expect` fields are merged into its expectation. |
| `delete` | `id` | Removes it. A track is named by its action. |
| `quantize` | `grid?`, `track?` or `ids?` | Snaps times to the grid, or to `grid` when given. A note keeps at least one grid step. |
| `set_clip` | clip fields | `rate`, `length`, `grid`, `description`, `map`, `profile`, `fixed_timestep`, `seed`, `hold_timeout`, `reference`. |

The answer has `changed` (the ids added or changed, in order), the clip as `clip` and `text`, and `warnings`. An edit that names nothing on the clip, or leaves it unsaveable, changes nothing and answers `success: false` with the finding and candidates: an id that does not exist offers the closest ids, and a note on a track the clip does not have offers the closest tracks.

```text
pie(action="clip_edit", path="/Game/PIEStudio/Clips/CL_Door",
    ops=[{"op": "move", "id": "k2", "to": 90},
         {"op": "add", "kind": "note", "track": "/Game/Input/IA_Jump", "start": 140, "length": 6}])
```

## Playing a clip

```text
pie(action="clip_run", path="/Game/PIEStudio/Clips/CL_Door", start_pie=true, wait=true)
```

| Param | Default | Meaning |
|---|---|---|
| `path`, `clip` or `text` | | The clip asset, or an inline clip. Exactly one. |
| `name` | the asset name, or `inline` | The run's name. |
| `region` | | Judge only this region's checks and stop at its end. Playback still starts at frame 0, since the state at a region's start comes from playing what comes before it. |
| `start_pie` | `false` | Start PIE when it is not running. The run ends PIE it started when it ends. |
| `keep_pie` | `false` | Leave that session running. |
| `fixed_timestep` | the clip's | Fixed frame deltas: to the next recorded frame where the clip has `frame_times`, else 1/`rate`. |
| `lock_input` | `true` | Keep the person's keyboard, mouse and gamepad out of PIE while the clip plays. |
| `pie_instance`, `client_id` | the editor's own player | The player the clip drives and observes. |
| `wait`, `timeout_seconds` | `false`, 600 | Answer once the run finishes, stops or waits for a verdict. |

How a run plays:

- **Frame 0** is the first frame the observer samples after PIE's first, long frame, in a run and a recording alike. The pawn is teleported to the clip's `start`, the seed applied, and the fixed timestep switched on. Where the clip has recorded `frame_times`, each frame's delta takes the run to the next recorded frame, so every frame of a replayed take lands where the recorded one did.
- **Every frame, before the actors tick,** the position moves on by that frame's game time, the events it passed fire, and each track's value at the position is fed to its action with `InjectInputForAction`, so the value is processed on its own frame.
- **At the end of every frame** the check notes are judged against what the observer read, and the pawn's position and velocity are written to the run's track.
- **The run finishes** once the position reaches the clip's length (or the region's end) with no check still waiting. It also ends when PIE ends, or with `clip_stop` (`reason?`), and any check still open then fails.

`clip_status` (`run_id?`, `wait?`, `timeout_seconds?`) answers with the run; a finished run the editor no longer holds answers from its `clip_results.json`, under `<Project>/Saved/MCPObservations/<run_id>/`. `clip_verdict` (`check`, `verdict` of `pass`, `fail` or `skip`, `note?`, `source?` of `human` or `agent`, `evidence?` of `{note, captures?}`) answers the manual check a run is paused on. An agent verdict needs an evidence note.

### Results

| Field | Meaning |
|---|---|
| `run_id`, `clip`, `clip_name`, `mode` | The run, the clip, and `play` or `record`. |
| `state`, `stop_reason` | `armed`, `running`, `finished` or `stopped`, and why it ended. |
| `passed` | Finished, with at least one check passed and none failed or open. |
| `position`, `length`, `game_ms`, `held_ms`, `frames` | Where playback is in frames, the clip's length, game ms since frame 0, ms spent standing at holds, frames played. |
| `holding`, `awaiting` | The check playback stands at, and the manual checks waiting for a verdict. |
| `summary` | `total`, `passed`, `failed`, `skipped`, `pending`, `awaiting`. |
| `checks[]` | `id`, `mode`, `status` (`pending`, `running`, `passed`, `failed`, `awaiting_verdict`, `skipped`), `start`, `length`, `started_frame`, `ended_frame`, `started_ms`, `ended_ms`, `evidence` (`at_start`, `last`, `samples`, `reason`, and a verdict's `note` and `captures`), and on a hold `hold` (`held_ms`, `resolved_frame`, `resolved_ms`, `value`, and when the clip was recorded `recorded_ms` and `drift_ms`). |
| `regions[]` | `id`, `name`, `start`, `end`, `status` (`passed`, `failed`, `pending` or `empty`), the `checks` that start in it, and with a reference `drift` (`max_position_cm`, `max_velocity_cms`, `frames_compared`, `first_over_frame`). |
| `drift` | Against the clip's `reference` over the whole run: `compared`, `frames_compared`, `max_position_cm`, `max_velocity_cms`, `first_over_frame` (5 cm or 25 cm/s), or `error`. Frames spent standing at a hold are not compared. |
| `events[]` | Each event fired: `id`, `frame`, `game_ms`, `ok`, `error`. |
| `take` | On a recording: see below. |

### Daemon events

While a run started through `clip_run` or `clip_record` plays, the server polls the editor every 250 ms and the daemon publishes what changed on its event stream (`GET /v1/events`), so a client can move a timeline cursor and color check notes without polling. A `clip_status` call for a run that is still playing starts the same stream, after a server restart for example. A daemon that does this lists `events.clip` in `/v1/health`.

| Event | When | Payload beyond the common fields |
|---|---|---|
| `clip.run_started` | The server first saw the run. | `mode`, `clipName?`, `region?`, `rate?`, `resultsPath?`, and the position fields |
| `clip.position` | Playback moved, reached or left a hold, or changed state. | `state`, `position`, `length`, `gameMs`, `heldMs`, `frames`, `holding?` |
| `clip.check` | A check's status changed. | `check`, its entry in `checks[]` |
| `clip.region` | A region's status or drift changed. | `region`, its entry in `regions[]` |
| `clip.event_fired` | The run fired an event. | `event`, its entry in `events[]` |
| `clip.verdict_requested` | Manual checks wait for `clip_verdict`. | `awaiting`, their ids |
| `clip.run_finished` | The run finished or stopped, or the editor stopped answering (`state: "unreachable"`). | `state`, `stopReason?`, `passed?`, `summary?`, `drift?`, `resultsPath?` |

Every payload carries `runId`, `clip` (the asset path, or `inline`), `at` (when the server saw the change) and `seq` (increasing within a run).

## Recording into a clip

```text
pie(action="clip_record", path="/Game/PIEStudio/Clips/CL_Door", region="r2")
pie(action="clip_record_stop")
```

`clip_record` (`path`, `region?` or `from_check?`, `armed?`, `pie_instance?`, `client_id?`) starts PIE and plays the clip with the person's input locked out up to the punch-in, then stops feeding the armed tracks and records what the person does to them. The tracks that are not armed keep playing.

- **Punch-in** is at a region's start, which also punches out at its end and ends the run there, or at a `hold` check's frame (`from_check`), which records until `clip_record_stop`. With neither, the take starts at frame 0 and runs until `clip_record_stop`. Input replayed before the punch-in does not reproduce physics, AI or random state from the middle of a region, so a punch-in anywhere else is not offered.
- **Armed tracks** are the Input Actions in `armed`, or every action the player has a mapping for. To record a new clip, create an empty one with `clip_create` and record into it.
- **Comping.** The take replaces every armed track over its range: the notes and keys there go into a new entry in `takes`, and the recording goes in their place. Takes are never mixed, since summing two takes would make input no player made.
- **Timing.** The lead-in before the punch-in plays as `clip_run` would; from the punch-in, recording runs on variable delta time with `t.MaxFPS` pinned to the clip's rate, so the game does not slow down for the person when frames drop. Each frame's raw value is read as the engine computes it, on the frame it was played: a Boolean track becomes notes from its edges, and an axis track becomes a key on every frame, reduced with `FRichCurve` to within 0.0001. The frames go into `frame_times`, so a replay lands on them.
- **Reference.** The take's run becomes the clip's `reference`, so later runs report drift against it. A take from frame 0 also sets the clip's `start` pose.
- **Hold checks** record how they resolved, the frame, game ms and value, as `recorded`. A replay waits at that frame rather than the check's start, and reports `drift_ms` against it.

`clip_record_stop` ends the take at the current frame and saves it. The answer, and `take` on the run, carries `take_id`, `punch_in`, `punch_out`, `punched_in`, `ended_at_frame`, `armed`, `samples`, `saved` and, when the clip did not save, `error`.

## How closely a take replays

Measured with the probe arena in the test project over 30 s of a character walking, jumping and steering with the stick, ten replays at a fixed timestep (`scripts/timeline-probe.mjs`):

| Scene | Character, max drift | Things it touched |
|---|---|---|
| The character alone | 0.018 cm, the same in every replay | |
| With a physics crate in its path and an AI chaser following it | 4.8 to 26.9 cm, from the frame it reached the crate | the crate ended 34 to 846 cm from where it did in the take; the chaser stayed within the character's own drift |

Movement driven by input reproduces. Physics does not reproduce run to run, even at the same frame deltas, and whatever touches it inherits the difference. Check anything physics moves with a `hold` check, so playback waits for the state instead of assuming when it arrives.

## Not covered

- Remote players can be driven by an event's act entry, but not recorded.
- `ui_input` cursor events are not on a clip: they run on real time, which conflicts with game time and with a paused hold.
