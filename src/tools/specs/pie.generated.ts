// @session independent: Generated parameter declarations from the recorded C++ specs; data only, with no editor state.
// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd pie handler. */
export const handlerSpecs: HandlerSpecs = {
  "pie_anim_properties": {
    "category": "pie",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": true,
        "description": "Actor label or name in PIE"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Property names to read (default: all)",
        "items": "string"
      }
    ]
  },
  "pie_anim_state": {
    "category": "pie",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": true,
        "description": "Actor label or name in PIE"
      }
    ]
  },
  "pie_arrangement_play": {
    "category": "pie",
    "params": [
      {
        "name": "runId",
        "type": "string",
        "required": true,
        "description": "The hub's id for the run"
      },
      {
        "name": "arrangement",
        "type": "object",
        "required": true,
        "description": "The arrangement as the hub stores it: {id, level, rate, tracks: [{id, performer, mute, solo, instances: [{id, clip, at}]}], checks: [{id, at, length, mode: by_end|throughout|hold|manual, expect?: {value, op, target?, subject?}, instruction?, timeout?}], regions: [{id, start, end}]}"
      },
      {
        "name": "clips",
        "type": "object",
        "required": true,
        "description": "Every clip the tracks place, by id: {rate, length, performer, input, lanes}"
      },
      {
        "name": "region",
        "type": "string",
        "required": false,
        "description": "Play from frame 0 to this region's end and judge only the checks that start inside it"
      }
    ],
    "contractExempt": "Loads the arrangement's level, starts PIE and plays it"
  },
  "pie_arrangement_status": {
    "category": "pie",
    "params": [
      {
        "name": "runId",
        "type": "string",
        "required": true,
        "description": "The hub's id for the run"
      }
    ]
  },
  "pie_arrangement_stop": {
    "category": "pie",
    "params": [
      {
        "name": "runId",
        "type": "string",
        "required": true,
        "description": "The hub's id for the run"
      },
      {
        "name": "reason",
        "type": "string",
        "required": false,
        "description": "Why it stopped, for the unsettled checks"
      }
    ],
    "contractExempt": "Ends the run and the PIE session it started"
  },
  "pie_arrangement_verdict": {
    "category": "pie",
    "params": [
      {
        "name": "runId",
        "type": "string",
        "required": true,
        "description": "The hub's id for the run"
      },
      {
        "name": "check",
        "type": "string",
        "required": true,
        "description": "Id of the manual check awaiting its verdict"
      },
      {
        "name": "verdict",
        "type": "string",
        "required": true,
        "description": "The verdict",
        "enum": [
          "pass",
          "fail"
        ]
      },
      {
        "name": "note",
        "type": "string",
        "required": false,
        "description": "What the verdict rests on"
      }
    ],
    "contractExempt": "Settles a check in a running play and resumes PIE"
  },
  "pie_assert_eval": {
    "category": "pie",
    "params": [
      {
        "name": "run_id",
        "type": "string",
        "required": false,
        "description": "Observation run id (resolves observation.csv under output_dir)"
      },
      {
        "name": "output_dir",
        "type": "string",
        "required": false,
        "description": "Observation root (default Saved/MCPObservations)"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recording or replay folder (resolves recording.csv)"
      },
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "csv_path",
        "type": "string",
        "required": false,
        "description": "Explicit per-frame series CSV"
      },
      {
        "name": "assertions",
        "type": "array",
        "required": false,
        "description": "Predicates {name, channel|event, op, value|min+max|tol, hold, from_s, to_s, within_s, after_event, severity}; omitted reads test_path's predicates",
        "items": "object"
      },
      {
        "name": "test_path",
        "type": "string",
        "required": false,
        "description": "Scenario json whose predicates block is evaluated when assertions is omitted"
      }
    ],
    "contractExempt": "Writes verdict.json beside the series it evaluates"
  },
  "pie_clients": {
    "category": "pie",
    "params": []
  },
  "pie_clip_record_start": {
    "category": "pie",
    "params": [
      {
        "name": "performer",
        "type": "object",
        "required": true,
        "description": "Who to record: {kind: player, client: 0} or {kind: actor, actor}"
      },
      {
        "name": "kind",
        "type": "string",
        "required": true,
        "description": "The performer's kind",
        "enum": [
          "player",
          "actor"
        ]
      },
      {
        "name": "rate",
        "type": "number",
        "required": false,
        "description": "Frames per second of a new clip (default 60)",
        "minimum": 1
      },
      {
        "name": "punch",
        "type": "object",
        "required": false,
        "description": "Punch in: play clip from frame 0 with the person's input locked out, record from `from` to `to` (the clip's frames), and replace that range",
        "fields": [
          {
            "name": "clip",
            "type": "object",
            "required": true,
            "description": "The clip as the hub stores it"
          },
          {
            "name": "from",
            "type": "number",
            "required": true,
            "description": "The frame the take starts on"
          },
          {
            "name": "to",
            "type": "number",
            "required": true,
            "description": "The frame the take ends on"
          }
        ]
      }
    ],
    "contractExempt": "Starts PIE and records a performance"
  },
  "pie_clip_record_stop": {
    "category": "pie",
    "params": [],
    "contractExempt": "Ends the recording and the PIE session it started"
  },
  "pie_console": {
    "category": "pie",
    "params": [
      {
        "name": "command",
        "type": "string",
        "required": true,
        "description": "Console command"
      },
      {
        "name": "client",
        "type": "integer",
        "required": true,
        "description": "Remote player 1..N (join order)",
        "minimum": 1
      }
    ],
    "contractExempt": "Runs a console command in a remote player's process"
  },
  "pie_drive_pawn": {
    "category": "pie",
    "params": [
      {
        "name": "direction",
        "type": "vec3",
        "required": false,
        "description": "World direction to push movement input in every frame"
      },
      {
        "name": "targetLocation",
        "type": "vec3",
        "required": false,
        "description": "World point to steer toward; the drive ends as arrived within acceptanceRadius"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "input (default): movement input on the pawn; navigate: a navmesh move from the player controller, needs targetLocation",
        "enum": [
          "input",
          "navigate"
        ]
      },
      {
        "name": "durationSeconds",
        "type": "number",
        "required": false,
        "description": "Real seconds to drive before ending as completed or timeout (default 5, at most 600)",
        "minimum": 0,
        "maximum": 600
      },
      {
        "name": "scale",
        "type": "number",
        "required": false,
        "description": "Movement input scale (default 1)",
        "minimum": -1,
        "maximum": 1
      },
      {
        "name": "sampleEvery",
        "type": "number",
        "required": false,
        "description": "Seconds between track samples; 0 samples every frame (default 0.1)",
        "minimum": 0
      },
      {
        "name": "acceptanceRadius",
        "type": "number",
        "required": false,
        "description": "Distance in cm from targetLocation that counts as arrived (default 50)",
        "minimum": 0
      },
      {
        "name": "planar",
        "type": "boolean",
        "required": false,
        "description": "Steer and measure arrival on the XY plane (default true); false for flying or swimming pawns"
      },
      {
        "name": "stopOnMapChange",
        "type": "boolean",
        "required": false,
        "description": "End the drive as map_changed when the player arrives in another level (default false: keep driving there)"
      },
      {
        "name": "playerIndex",
        "type": "integer",
        "required": false,
        "description": "Local player index in the PIE world (default 0)"
      },
      {
        "name": "pieInstance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "driveId",
        "type": "string",
        "required": false,
        "description": "Caller-chosen drive id (minted when absent)"
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Return only when the drive ends, with its whole track (default true)"
      },
      {
        "name": "timeoutSeconds",
        "type": "number",
        "required": false,
        "description": "Longest wait=true waits (default durationSeconds + 30)",
        "minimum": 0
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "direction"
          ],
          [
            "targetLocation"
          ]
        ]
      }
    ],
    "contractExempt": "Drives the PIE player's pawn over later frames; pie(drive_stop) ends it"
  },
  "pie_drive_status": {
    "category": "pie",
    "params": [
      {
        "name": "driveId",
        "type": "string",
        "required": false,
        "description": "Drive id from drive_pawn (default: the most recent drive)"
      },
      {
        "name": "includeSamples",
        "type": "boolean",
        "required": false,
        "description": "Include the sampled track (default true)"
      }
    ]
  },
  "pie_drive_stop": {
    "category": "pie",
    "params": [
      {
        "name": "driveId",
        "type": "string",
        "required": false,
        "description": "Drive id from drive_pawn (default: the most recent drive)"
      }
    ],
    "contractExempt": "Ends a running pawn drive"
  },
  "pie_inject_input": {
    "category": "pie",
    "params": [
      {
        "name": "action_path",
        "type": "string",
        "required": true,
        "description": "InputAction asset path"
      },
      {
        "name": "value_x",
        "type": "number",
        "required": false,
        "description": "Axis or bool value X (default 1)"
      },
      {
        "name": "value_y",
        "type": "number",
        "required": false,
        "description": "Axis value Y (default 0)"
      },
      {
        "name": "value_z",
        "type": "number",
        "required": false,
        "description": "Axis value Z (default 0)"
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "client",
        "type": "integer",
        "required": false,
        "description": "1..N relays to that remote player (join order) over the game connection, including separate-process clients; 0 or absent uses the local player",
        "minimum": 0
      }
    ],
    "contractExempt": "Drives input into the running PIE session"
  },
  "pie_inject_input_start": {
    "category": "pie",
    "params": [
      {
        "name": "action_path",
        "type": "string",
        "required": true,
        "description": "InputAction asset path"
      },
      {
        "name": "value_x",
        "type": "number",
        "required": false,
        "description": "Axis or bool value X (default 1)"
      },
      {
        "name": "value_y",
        "type": "number",
        "required": false,
        "description": "Axis value Y (default 0)"
      },
      {
        "name": "value_z",
        "type": "number",
        "required": false,
        "description": "Axis value Z (default 0)"
      },
      {
        "name": "injection_id",
        "type": "string",
        "required": false,
        "description": "Caller-chosen hold id (minted when absent)"
      },
      {
        "name": "duration_seconds",
        "type": "number",
        "required": false,
        "description": "Game seconds after which the hold releases itself (default 0: held until inject_input_stop or PIE ends). Local player only",
        "minimum": 0
      },
      {
        "name": "control_rotation",
        "type": "rotator",
        "required": false,
        "description": "Player controller control rotation, re-applied every frame while the hold runs. Local player only"
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "client",
        "type": "integer",
        "required": false,
        "description": "1..N relays to that remote player (join order) over the game connection, including separate-process clients; 0 or absent uses the local player",
        "minimum": 0
      }
    ],
    "contractExempt": "Starts holding an input action in the running PIE session"
  },
  "pie_inject_input_stop": {
    "category": "pie",
    "params": [
      {
        "name": "injection_id",
        "type": "string",
        "required": true,
        "description": "Hold or tape id to stop"
      }
    ],
    "contractExempt": "Stops a running input hold or tape"
  },
  "pie_inject_input_tape": {
    "category": "pie",
    "params": [
      {
        "name": "action_path",
        "type": "string",
        "required": true,
        "description": "InputAction asset path"
      },
      {
        "name": "values",
        "type": "array",
        "required": true,
        "description": "Per-frame values: numbers or [x,y,z] arrays"
      },
      {
        "name": "hz",
        "type": "number",
        "required": false,
        "description": "Playback rate (default 60)"
      },
      {
        "name": "injection_id",
        "type": "string",
        "required": false,
        "description": "Caller-chosen tape id (minted when absent)"
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "client",
        "type": "integer",
        "required": false,
        "description": "1..N relays to that remote player (join order) over the game connection, including separate-process clients; 0 or absent uses the local player",
        "minimum": 0
      }
    ],
    "contractExempt": "Plays a value tape into the running PIE session"
  },
  "pie_inject_input_update": {
    "category": "pie",
    "params": [
      {
        "name": "injection_id",
        "type": "string",
        "required": true,
        "description": "Hold id from inject_input_start"
      },
      {
        "name": "value_x",
        "type": "number",
        "required": false,
        "description": "Axis or bool value X (default 1)"
      },
      {
        "name": "value_y",
        "type": "number",
        "required": false,
        "description": "Axis value Y (default 0)"
      },
      {
        "name": "value_z",
        "type": "number",
        "required": false,
        "description": "Axis value Z (default 0)"
      },
      {
        "name": "control_rotation",
        "type": "rotator",
        "required": false,
        "description": "New control rotation the hold re-applies every frame; sent alone it keeps the hold's value"
      }
    ],
    "contractExempt": "Changes the value of a running input hold"
  },
  "pie_mark": {
    "category": "pie",
    "params": [
      {
        "name": "label",
        "type": "string",
        "required": true,
        "description": "Marker label"
      }
    ],
    "contractExempt": "Writes a marker into the recording in flight"
  },
  "pie_observe_arm": {
    "category": "pie",
    "params": [
      {
        "name": "profile",
        "type": "string",
        "required": true,
        "description": "Observation profile asset path"
      },
      {
        "name": "output_dir",
        "type": "string",
        "required": false,
        "description": "Observation root (default Saved/MCPObservations)"
      },
      {
        "name": "sample_hz",
        "type": "integer",
        "required": false,
        "description": "Sample rate (default 60)",
        "minimum": 1
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "pin_fps",
        "type": "integer",
        "required": false,
        "description": "Pin PIE fps while observing"
      }
    ],
    "contractExempt": "Arms the observer, which starts on the next PIE session"
  },
  "pie_observe_disarm": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and cancels armed observations"
  },
  "pie_observe_list": {
    "category": "pie",
    "params": [
      {
        "name": "output_dir",
        "type": "string",
        "required": false,
        "description": "Observation root (default Saved/MCPObservations)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most rows (default 200)",
        "minimum": 1
      }
    ]
  },
  "pie_observe_read": {
    "category": "pie",
    "params": [
      {
        "name": "run_id",
        "type": "string",
        "required": true,
        "description": "Observation run id"
      },
      {
        "name": "output_dir",
        "type": "string",
        "required": false,
        "description": "Observation root (default Saved/MCPObservations)"
      },
      {
        "name": "file",
        "type": "string",
        "required": false,
        "description": "Artifact to read; series returns per-channel time series (default manifest)",
        "enum": [
          "manifest",
          "csv",
          "tracked",
          "series",
          "widgets"
        ]
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "csv/tracked: most rows (default 1000)",
        "minimum": 1
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "csv/tracked: first row",
        "minimum": 0
      },
      {
        "name": "channels",
        "type": "string",
        "required": false,
        "description": "series: comma-separated columns (default: every t:* plus speed2d, gpu_ms, game_ms)"
      },
      {
        "name": "threshold",
        "type": "number",
        "required": false,
        "description": "series: report the first frame where |value| exceeds this"
      },
      {
        "name": "include_values",
        "type": "boolean",
        "required": false,
        "description": "series: include the downsampled values (default false)"
      },
      {
        "name": "max_points",
        "type": "integer",
        "required": false,
        "description": "series: most values per channel (default 500)",
        "minimum": 1
      }
    ]
  },
  "pie_observe_status": {
    "category": "pie",
    "params": []
  },
  "pie_observe_stop": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and finalizes the observation in flight"
  },
  "pie_perf_summary": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root (default: the newest recording)"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      }
    ]
  },
  "pie_profile_create": {
    "category": "pie",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "package_path",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Observation)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "What to do when the asset exists (default skip)",
        "enum": [
          "skip",
          "replace"
        ]
      },
      {
        "name": "tracked_values",
        "type": "array",
        "required": false,
        "description": "Value paths, or {path, drift_threshold} objects"
      },
      {
        "name": "tracked_actors",
        "type": "array",
        "required": false,
        "description": "Actor ids to track",
        "items": "string"
      },
      {
        "name": "tracked_widgets",
        "type": "array",
        "required": false,
        "description": "Widget class names or paths, or widget names, sampled as visible true or false",
        "items": "string"
      },
      {
        "name": "capture_pawn_state",
        "type": "boolean",
        "required": false,
        "description": "Sample the pawn transform and movement"
      },
      {
        "name": "capture_montage",
        "type": "boolean",
        "required": false,
        "description": "Sample the active montage"
      },
      {
        "name": "position_threshold_cm",
        "type": "number",
        "required": false,
        "description": "Position drift threshold"
      },
      {
        "name": "rotation_threshold_deg",
        "type": "number",
        "required": false,
        "description": "Rotation drift threshold"
      },
      {
        "name": "velocity_threshold_cms",
        "type": "number",
        "required": false,
        "description": "Velocity drift threshold"
      },
      {
        "name": "tracked_value_default_threshold",
        "type": "number",
        "required": false,
        "description": "Default tracked-value threshold"
      }
    ],
    "contractExempt": "Creates an observation profile asset"
  },
  "pie_profile_delete": {
    "category": "pie",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Profile asset path"
      },
      {
        "name": "confirm",
        "type": "boolean",
        "required": false,
        "description": "Must be true to delete"
      }
    ],
    "contractExempt": "Deletes the profile asset"
  },
  "pie_profile_list": {
    "category": "pie",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path prefix (default /Game)"
      }
    ]
  },
  "pie_profile_read": {
    "category": "pie",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Profile asset path"
      }
    ]
  },
  "pie_profile_update": {
    "category": "pie",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Profile asset path"
      },
      {
        "name": "tracked_values",
        "type": "array",
        "required": false,
        "description": "Replaces the tracked values"
      },
      {
        "name": "tracked_actors",
        "type": "array",
        "required": false,
        "description": "Replaces the tracked actors",
        "items": "string"
      },
      {
        "name": "tracked_widgets",
        "type": "array",
        "required": false,
        "description": "Replaces the tracked widgets",
        "items": "string"
      },
      {
        "name": "capture_pawn_state",
        "type": "boolean",
        "required": false,
        "description": "Sample the pawn transform and movement"
      },
      {
        "name": "capture_montage",
        "type": "boolean",
        "required": false,
        "description": "Sample the active montage"
      },
      {
        "name": "position_threshold_cm",
        "type": "number",
        "required": false,
        "description": "Position drift threshold"
      },
      {
        "name": "rotation_threshold_deg",
        "type": "number",
        "required": false,
        "description": "Rotation drift threshold"
      },
      {
        "name": "velocity_threshold_cms",
        "type": "number",
        "required": false,
        "description": "Velocity drift threshold"
      },
      {
        "name": "tracked_value_default_threshold",
        "type": "number",
        "required": false,
        "description": "Default tracked-value threshold"
      }
    ],
    "contractExempt": "Writes and saves the profile asset"
  },
  "pie_record_arm": {
    "category": "pie",
    "params": [
      {
        "name": "actions",
        "type": "array",
        "required": false,
        "description": "InputAction paths to record (default: every action bound on the pawn)",
        "items": "string"
      },
      {
        "name": "track_values",
        "type": "array",
        "required": false,
        "description": "Dotted value paths sampled each frame; sub:<Class>.<path> reads a subsystem, bone:/bonecs:<Bone>.<channel> a bone",
        "items": "string"
      },
      {
        "name": "track_actors",
        "type": "array",
        "required": false,
        "description": "Actor ids sampled for position, rotation and velocity",
        "items": "string"
      },
      {
        "name": "axis_threshold",
        "type": "number",
        "required": false,
        "description": "Axis change deadzone (default 0.15)"
      },
      {
        "name": "sample_hz",
        "type": "integer",
        "required": false,
        "description": "Sample rate (default 60)",
        "minimum": 1
      },
      {
        "name": "pin_fps",
        "type": "integer",
        "required": false,
        "description": "Pin PIE fps for reproducibility (default sample_hz)"
      },
      {
        "name": "capture_pawn_state",
        "type": "boolean",
        "required": false,
        "description": "Sample the pawn transform and movement (default true)"
      },
      {
        "name": "capture_montage",
        "type": "boolean",
        "required": false,
        "description": "Sample the active montage (default true)"
      },
      {
        "name": "take_record",
        "type": "boolean",
        "required": false,
        "description": "Also drive Take Recorder (default false)"
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "rng_seed",
        "type": "number",
        "required": false,
        "description": "Seed the stream RNG for reproducible runs"
      },
      {
        "name": "run_gap_frames",
        "type": "integer",
        "required": false,
        "description": "Idle frames that split input runs (default 6)"
      },
      {
        "name": "capture_ui_pointer",
        "type": "boolean",
        "required": false,
        "description": "Also record the mouse through Slate (cursor, UMG hover, click, drag, wheel) while the cursor is shown, as ui_input steps (default false)"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root (default Saved/MCPRecordings)"
      },
      {
        "name": "id",
        "type": "string",
        "required": false,
        "description": "Recording id (default: a timestamp)"
      }
    ],
    "contractExempt": "Arms the recorder, which starts on the next PIE session"
  },
  "pie_record_delete": {
    "category": "pie",
    "params": [
      {
        "name": "id",
        "type": "string",
        "required": true,
        "description": "Recording id"
      },
      {
        "name": "confirm",
        "type": "boolean",
        "required": false,
        "description": "Must be true to delete"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root (default Saved/MCPRecordings)"
      }
    ],
    "contractExempt": "Deletes a recording folder from disk"
  },
  "pie_record_diff": {
    "category": "pie",
    "params": [
      {
        "name": "a_id",
        "type": "string",
        "required": true,
        "description": "First recording id"
      },
      {
        "name": "b_id",
        "type": "string",
        "required": true,
        "description": "Second recording id"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root (default Saved/MCPRecordings)"
      },
      {
        "name": "position_cm",
        "type": "number",
        "required": false,
        "description": "Position drift threshold (default 5)"
      },
      {
        "name": "rotation_deg",
        "type": "number",
        "required": false,
        "description": "Rotation drift threshold (default 2)"
      },
      {
        "name": "velocity_cms",
        "type": "number",
        "required": false,
        "description": "Velocity drift threshold (default 25)"
      },
      {
        "name": "tracked_default",
        "type": "number",
        "required": false,
        "description": "Default tracked-value threshold"
      },
      {
        "name": "tracked_thresholds",
        "type": "object",
        "required": false,
        "description": "Per-path tracked-value thresholds {path: threshold}"
      },
      {
        "name": "align",
        "type": "string",
        "required": false,
        "description": "auto (default) pairs a's rows with b's shifted by the row offset that best matches the two trajectories, so input landing a few frames later is not read as drift; index pairs rows by index. The result's alignment reports the offset",
        "enum": [
          "auto",
          "index"
        ]
      },
      {
        "name": "max_offset_rows",
        "type": "integer",
        "required": false,
        "description": "align=auto: largest shift tried, in rows either way (default 30)",
        "minimum": 0
      }
    ]
  },
  "pie_record_disarm": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and cancels an armed recording"
  },
  "pie_record_list": {
    "category": "pie",
    "params": [
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root (default Saved/MCPRecordings)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most rows (default 200)",
        "minimum": 1
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "Rows to skip",
        "minimum": 0
      }
    ]
  },
  "pie_record_read": {
    "category": "pie",
    "params": [
      {
        "name": "id",
        "type": "string",
        "required": true,
        "description": "Recording id"
      },
      {
        "name": "file",
        "type": "string",
        "required": false,
        "description": "Artifact to read (default manifest)",
        "enum": [
          "manifest",
          "sequence",
          "csv",
          "drift",
          "tracked"
        ]
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root (default Saved/MCPRecordings)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "csv: most rows (default 1000)",
        "minimum": 1
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "csv: first row",
        "minimum": 0
      }
    ]
  },
  "pie_record_status": {
    "category": "pie",
    "params": []
  },
  "pie_record_stop": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and finalizes the recording in flight"
  },
  "pie_remote_result": {
    "category": "pie",
    "params": [
      {
        "name": "request_id",
        "type": "string",
        "required": true,
        "description": "injection_id or request_id a relayed call returned",
        "aliases": [
          "injection_id"
        ]
      }
    ]
  },
  "pie_replay_analyze": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most frames_over_threshold entries returned (default 50)",
        "minimum": 0
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "First frames_over_threshold entry returned",
        "minimum": 0
      }
    ]
  },
  "pie_replay_arm": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "sequence_path",
        "type": "string",
        "required": false,
        "description": "Explicit sequence.json path"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "steps",
        "type": "array",
        "required": false,
        "description": "Inline step array, instead of a recording",
        "items": "object"
      },
      {
        "name": "settle_ms",
        "type": "integer",
        "required": false,
        "description": "Settle delay before playback (default 500)",
        "minimum": 0
      },
      {
        "name": "sample_hz",
        "type": "integer",
        "required": false,
        "description": "Inline steps: sample rate (default 60)",
        "minimum": 1
      },
      {
        "name": "rng_seed",
        "type": "number",
        "required": false,
        "description": "Inline steps: RNG seed"
      },
      {
        "name": "pin_fps",
        "type": "integer",
        "required": false,
        "description": "Pin PIE fps during replay"
      },
      {
        "name": "apply_rng_seed",
        "type": "boolean",
        "required": false,
        "description": "Re-apply the recorded seed (default true)"
      },
      {
        "name": "record_drift",
        "type": "boolean",
        "required": false,
        "description": "Record drift against the source (default true)"
      },
      {
        "name": "auto_stop_pie",
        "type": "boolean",
        "required": false,
        "description": "End PIE when the replay completes (replay_arm default false, replay_run default true)"
      },
      {
        "name": "eject",
        "type": "boolean",
        "required": false,
        "description": "Eject to a spectator camera during replay"
      },
      {
        "name": "lock_input",
        "type": "boolean",
        "required": false,
        "description": "Block the user's keyboard, mouse and gamepad from the PIE game while the replay runs, so only the recording drives the player; Esc, Shift+F1 and F8 still work, and monitor mode ignores it (default false)"
      },
      {
        "name": "time_scale",
        "type": "number",
        "required": false,
        "description": "Playback speed multiplier (default 1)"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "replay drives input; monitor only samples (default replay)",
        "enum": [
          "replay",
          "monitor"
        ]
      },
      {
        "name": "capture_frame_every",
        "type": "integer",
        "required": false,
        "description": "Capture a viewport JPEG every N steps and build a contact sheet",
        "minimum": 0
      },
      {
        "name": "encode_gif",
        "type": "boolean",
        "required": false,
        "description": "Also encode an animated GIF (default false)"
      },
      {
        "name": "fixed_timestep",
        "type": "boolean",
        "required": false,
        "description": "Fixed 1/pin_fps delta during replay (default false)"
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "drift_thresholds",
        "type": "object",
        "required": false,
        "description": "{position_cm, rotation_deg, velocity_cms, tracked_default, tracked:{path: threshold}}"
      },
      {
        "name": "start_when",
        "type": "object",
        "required": false,
        "description": "Hold the steps until the player can act, then resume at the matching point of the recording: {pawn_at_rest (default true: speed <= max_speed_cms, default 5, and on the ground), hold_ms (default 500), log_contains, timeout_ms (default 60000)}. Replaces settle_ms"
      }
    ],
    "contractExempt": "Arms the replayer, which drives the next PIE session"
  },
  "pie_replay_disarm": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and cancels an armed replay"
  },
  "pie_replay_run": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "sequence_path",
        "type": "string",
        "required": false,
        "description": "Explicit sequence.json path"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "steps",
        "type": "array",
        "required": false,
        "description": "Inline step array, instead of a recording",
        "items": "object"
      },
      {
        "name": "settle_ms",
        "type": "integer",
        "required": false,
        "description": "Settle delay before playback (default 500)",
        "minimum": 0
      },
      {
        "name": "sample_hz",
        "type": "integer",
        "required": false,
        "description": "Inline steps: sample rate (default 60)",
        "minimum": 1
      },
      {
        "name": "rng_seed",
        "type": "number",
        "required": false,
        "description": "Inline steps: RNG seed"
      },
      {
        "name": "pin_fps",
        "type": "integer",
        "required": false,
        "description": "Pin PIE fps during replay"
      },
      {
        "name": "apply_rng_seed",
        "type": "boolean",
        "required": false,
        "description": "Re-apply the recorded seed (default true)"
      },
      {
        "name": "record_drift",
        "type": "boolean",
        "required": false,
        "description": "Record drift against the source (default true)"
      },
      {
        "name": "auto_stop_pie",
        "type": "boolean",
        "required": false,
        "description": "End PIE when the replay completes (replay_arm default false, replay_run default true)"
      },
      {
        "name": "eject",
        "type": "boolean",
        "required": false,
        "description": "Eject to a spectator camera during replay"
      },
      {
        "name": "lock_input",
        "type": "boolean",
        "required": false,
        "description": "Block the user's keyboard, mouse and gamepad from the PIE game while the replay runs, so only the recording drives the player; Esc, Shift+F1 and F8 still work, and monitor mode ignores it (default false)"
      },
      {
        "name": "time_scale",
        "type": "number",
        "required": false,
        "description": "Playback speed multiplier (default 1)"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "replay drives input; monitor only samples (default replay)",
        "enum": [
          "replay",
          "monitor"
        ]
      },
      {
        "name": "capture_frame_every",
        "type": "integer",
        "required": false,
        "description": "Capture a viewport JPEG every N steps and build a contact sheet",
        "minimum": 0
      },
      {
        "name": "encode_gif",
        "type": "boolean",
        "required": false,
        "description": "Also encode an animated GIF (default false)"
      },
      {
        "name": "fixed_timestep",
        "type": "boolean",
        "required": false,
        "description": "Fixed 1/pin_fps delta during replay (default false)"
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back",
        "minimum": 0
      },
      {
        "name": "drift_thresholds",
        "type": "object",
        "required": false,
        "description": "{position_cm, rotation_deg, velocity_cms, tracked_default, tracked:{path: threshold}}"
      },
      {
        "name": "start_when",
        "type": "object",
        "required": false,
        "description": "Hold the steps until the player can act, then resume at the matching point of the recording: {pawn_at_rest (default true: speed <= max_speed_cms, default 5, and on the ground), hold_ms (default 500), log_contains, timeout_ms (default 60000)}. Replaces settle_ms"
      }
    ],
    "contractExempt": "Arms the replayer and starts PIE"
  },
  "pie_replay_state": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "at_time",
        "type": "number",
        "required": false,
        "description": "Seconds to sample, interpolated (default: the last frame)"
      },
      {
        "name": "at_frame",
        "type": "integer",
        "required": false,
        "description": "Frame index to sample, instead of at_time",
        "minimum": 0
      },
      {
        "name": "apply",
        "type": "boolean",
        "required": false,
        "description": "Teleport the live PIE pawn to the recorded pose (default false). On a net client the server's pawn for that player moves too, so the server does not correct it back"
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "With apply: FWorldContext PIEInstance whose player to move (default the active PIE world)",
        "minimum": 0
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "With apply: local-player index inside that PIE world (default 0)",
        "minimum": 0
      }
    ],
    "contractExempt": "With apply it moves the live PIE pawn"
  },
  "pie_replay_status": {
    "category": "pie",
    "params": []
  },
  "pie_replay_stop": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and stops the replay in flight"
  },
  "pie_scenario_scaffold": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Scenario name (default scenario)"
      },
      {
        "name": "max_errors",
        "type": "integer",
        "required": false,
        "description": "no-errors predicate threshold (default 0)",
        "minimum": 0
      }
    ],
    "contractExempt": "Writes scenario.json beside the recording"
  },
  "pie_scenario_validate": {
    "category": "pie",
    "params": [
      {
        "name": "scenario_path",
        "type": "string",
        "required": false,
        "description": "Path to a scenario.json"
      },
      {
        "name": "scenario",
        "type": "object",
        "required": false,
        "description": "Inline scenario object"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "scenario_path"
          ],
          [
            "scenario"
          ]
        ]
      }
    ]
  },
  "pie_send_key": {
    "category": "pie",
    "params": [
      {
        "name": "key",
        "type": "string",
        "required": false,
        "description": "FKey name: Right, Left, Up, Down, Tab, Enter, SpaceBar, Escape, Gamepad_DPad_Down, Gamepad_FaceButton_Bottom..."
      },
      {
        "name": "text",
        "type": "string",
        "required": false,
        "description": "Characters to type into the focused widget, one character event each, instead of a key"
      },
      {
        "name": "event",
        "type": "string",
        "required": false,
        "description": "press (default: down then up) | down | up",
        "enum": [
          "press",
          "down",
          "up"
        ]
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext::PIEInstance of the client whose UI gets the key. Omitted picks the editor's own player: the listen-server host, else the lowest client PIE instance",
        "minimum": 0
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "key"
          ],
          [
            "text"
          ]
        ]
      }
    ],
    "contractExempt": "Sends key events into the running PIE session"
  },
  "pie_session_errors": {
    "category": "pie",
    "params": [
      {
        "name": "session",
        "type": "string",
        "required": false,
        "description": "Session id under Saved/MCPSessions (default: the live or most recent session)"
      },
      {
        "name": "session_dir",
        "type": "string",
        "required": false,
        "description": "Absolute session folder, overriding session"
      },
      {
        "name": "min_verbosity",
        "type": "string",
        "required": false,
        "description": "Lowest severity kept (default warning)",
        "enum": [
          "error",
          "warning"
        ]
      }
    ]
  },
  "pie_session_log": {
    "category": "pie",
    "params": [
      {
        "name": "session",
        "type": "string",
        "required": false,
        "description": "Session id under Saved/MCPSessions (default: the live or most recent session)"
      },
      {
        "name": "session_dir",
        "type": "string",
        "required": false,
        "description": "Absolute session folder, overriding session"
      },
      {
        "name": "min_verbosity",
        "type": "string",
        "required": false,
        "description": "Lowest severity kept (default log)",
        "enum": [
          "error",
          "warning",
          "display",
          "log",
          "verbose"
        ]
      },
      {
        "name": "category",
        "type": "string",
        "required": false,
        "description": "Exact log category, such as LogTemp"
      },
      {
        "name": "contains",
        "type": "string",
        "required": false,
        "description": "Substring the message must contain"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most rows (default 200)",
        "minimum": 1
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "First row",
        "minimum": 0
      },
      {
        "name": "max_message_chars",
        "type": "integer",
        "required": false,
        "description": "Longest message returned; longer ones are cut and marked truncated (default 500, 0 for no limit)",
        "minimum": 0
      }
    ]
  },
  "pie_stream_start": {
    "category": "pie",
    "params": [
      {
        "name": "signallingUrl",
        "type": "string",
        "required": true,
        "description": "ws:// or wss:// URL of the signalling server the editor's streamer connects to"
      },
      {
        "name": "streamerId",
        "type": "string",
        "required": false,
        "description": "Id the streamer identifies itself with at the signalling server (default ue-mcp)"
      },
      {
        "name": "source",
        "type": "string",
        "required": false,
        "description": "pie (default): the PIE viewport, armed until PIE starts; level_viewport: the active level viewport; editor: the whole editor",
        "enum": [
          "pie",
          "level_viewport",
          "editor"
        ]
      },
      {
        "name": "resolution",
        "type": "string",
        "required": false,
        "description": "WIDTHxHEIGHT such as 1280x720. Resizes the offscreen editor's window, or fixes the level viewport's size"
      },
      {
        "name": "fps",
        "type": "integer",
        "required": false,
        "description": "Encoded frame rate (default 30)",
        "minimum": 1
      },
      {
        "name": "maxBitrateKbps",
        "type": "integer",
        "required": false,
        "description": "Ceiling on the WebRTC bitrate in kbps (default 4000)",
        "minimum": 100
      },
      {
        "name": "codec",
        "type": "string",
        "required": false,
        "description": "Video codec (default H264)",
        "enum": [
          "H264",
          "VP8",
          "VP9",
          "AV1"
        ]
      }
    ]
  },
  "pie_stream_status": {
    "category": "pie",
    "params": []
  },
  "pie_stream_stop": {
    "category": "pie",
    "params": [],
    "contractExempt": "Takes no parameters and stops and disarms the stream"
  },
  "pie_test_list": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      }
    ]
  },
  "pie_test_run": {
    "category": "pie",
    "params": [
      {
        "name": "test_path",
        "type": "string",
        "required": false,
        "description": "Explicit test.json path"
      },
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Test name (default repro)"
      },
      {
        "name": "auto_run",
        "type": "boolean",
        "required": false,
        "description": "Drive the replay when no drift.json exists yet (default true)"
      }
    ],
    "contractExempt": "Replays the recording in PIE"
  },
  "pie_test_scaffold": {
    "category": "pie",
    "params": [
      {
        "name": "recording_id",
        "type": "string",
        "required": false,
        "description": "Recording id under the recordings root"
      },
      {
        "name": "recording_dir",
        "type": "string",
        "required": false,
        "description": "Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Test name (default repro)"
      },
      {
        "name": "max_position_drift_cm",
        "type": "number",
        "required": false,
        "description": "Most position drift allowed (default 10)"
      },
      {
        "name": "max_velocity_drift_cms",
        "type": "number",
        "required": false,
        "description": "Most velocity drift allowed (default 50)"
      },
      {
        "name": "max_rotation_drift_deg",
        "type": "number",
        "required": false,
        "description": "Most rotation drift allowed (default 5)"
      },
      {
        "name": "max_errors",
        "type": "integer",
        "required": false,
        "description": "Most errors logged during the run (default 0)",
        "minimum": 0
      },
      {
        "name": "start_when",
        "type": "object",
        "required": false,
        "description": "Replay gate test_run uses, as replay_run's start_when"
      }
    ],
    "contractExempt": "Writes test.json beside the recording"
  },
  "pie_uat_abort": {
    "category": "pie",
    "params": [
      {
        "name": "reason",
        "type": "string",
        "required": false,
        "description": "Why the run is ended, recorded in the report"
      }
    ],
    "contractExempt": "Ends the acceptance run and stops PIE"
  },
  "pie_uat_run": {
    "category": "pie",
    "params": [
      {
        "name": "scenario",
        "type": "object",
        "required": false,
        "description": "Scenario object: {name, map, pie:{players, net_mode, one_process, windowless, client_fps}, join_timeout_s, settle_ms, steps:[{name, do[], expect[{log host|client:N, pattern (regex), min, max, since step|run}], forbid[], window_ms}]}. A rule reads only lines logged after its step began unless since is run; see docs/pie.md"
      },
      {
        "name": "path",
        "type": "string",
        "required": false,
        "description": "Scenario JSON file, relative to the project directory"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "scenario"
          ],
          [
            "path"
          ]
        ]
      }
    ],
    "contractExempt": "Opens the scenario map and starts a multiplayer PIE session"
  },
  "pie_uat_status": {
    "category": "pie",
    "params": []
  },
  "pie_view_trace": {
    "category": "pie",
    "params": [
      {
        "name": "playerIndex",
        "type": "integer",
        "required": false,
        "description": "Local player index in the selected PIE world (default 0)"
      },
      {
        "name": "pieInstance",
        "type": "integer",
        "required": false,
        "description": "PIE instance id; omitted selects the active PIE world",
        "minimum": 0
      },
      {
        "name": "distance",
        "type": "number",
        "required": false,
        "description": "Positive trace distance in cm (default 1000)"
      },
      {
        "name": "channel",
        "type": "string",
        "required": false,
        "description": "Collision channel, including project-renamed channels (default Visibility)"
      },
      {
        "name": "sweepRadius",
        "type": "number",
        "required": false,
        "description": "Also sweep a sphere of this radius in cm when positive"
      },
      {
        "name": "traceComplex",
        "type": "boolean",
        "required": false,
        "description": "Trace per-triangle collision (default false)"
      },
      {
        "name": "ignoreActors",
        "type": "array",
        "required": false,
        "description": "Actor labels or paths to skip",
        "items": "string"
      },
      {
        "name": "ignorePawn",
        "type": "boolean",
        "required": false,
        "description": "Ignore the viewing player's pawn (default true)"
      }
    ]
  }
};

/** Every key the spec'd pie handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  a_id: z.string().optional().describe("First recording id"),
  acceptanceRadius: z.number().min(0).optional().describe("Distance in cm from targetLocation that counts as arrived (default 50)"),
  action_path: z.string().optional().describe("InputAction asset path"),
  actions: z.array(z.string()).optional().describe("InputAction paths to record (default: every action bound on the pawn)"),
  actorLabel: z.string().optional().describe("Actor label or name in PIE"),
  align: z.enum(["auto","index"]).optional().describe("auto (default) pairs a's rows with b's shifted by the row offset that best matches the two trajectories, so input landing a few frames later is not read as drift; index pairs rows by index. The result's alignment reports the offset"),
  apply: z.boolean().optional().describe("Teleport the live PIE pawn to the recorded pose (default false). On a net client the server's pawn for that player moves too, so the server does not correct it back"),
  apply_rng_seed: z.boolean().optional().describe("Re-apply the recorded seed (default true)"),
  arrangement: z.record(z.string(), z.unknown()).optional().describe("The arrangement as the hub stores it: {id, level, rate, tracks: [{id, performer, mute, solo, instances: [{id, clip, at}]}], checks: [{id, at, length, mode: by_end|throughout|hold|manual, expect?: {value, op, target?, subject?}, instruction?, timeout?}], regions: [{id, start, end}]}"),
  assertions: z.array(z.record(z.string(), z.unknown())).optional().describe("Predicates {name, channel|event, op, value|min+max|tol, hold, from_s, to_s, within_s, after_event, severity}; omitted reads test_path's predicates"),
  at_frame: z.number().int().min(0).optional().describe("Frame index to sample, instead of at_time"),
  at_time: z.number().optional().describe("Seconds to sample, interpolated (default: the last frame)"),
  auto_run: z.boolean().optional().describe("Drive the replay when no drift.json exists yet (default true)"),
  auto_stop_pie: z.boolean().optional().describe("End PIE when the replay completes (replay_arm default false, replay_run default true)"),
  axis_threshold: z.number().optional().describe("Axis change deadzone (default 0.15)"),
  b_id: z.string().optional().describe("Second recording id"),
  capture_frame_every: z.number().int().min(0).optional().describe("Capture a viewport JPEG every N steps and build a contact sheet"),
  capture_montage: z.boolean().optional().describe("Sample the active montage (pie_profile_create, pie_profile_update). Sample the active montage (default true) (pie_record_arm)"),
  capture_pawn_state: z.boolean().optional().describe("Sample the pawn transform and movement (pie_profile_create, pie_profile_update). Sample the pawn transform and movement (default true) (pie_record_arm)"),
  capture_ui_pointer: z.boolean().optional().describe("Also record the mouse through Slate (cursor, UMG hover, click, drag, wheel) while the cursor is shown, as ui_input steps (default false)"),
  category: z.string().optional().describe("Exact log category, such as LogTemp"),
  channel: z.string().optional().describe("Collision channel, including project-renamed channels (default Visibility)"),
  channels: z.string().optional().describe("series: comma-separated columns (default: every t:* plus speed2d, gpu_ms, game_ms)"),
  check: z.string().optional().describe("Id of the manual check awaiting its verdict"),
  client: z.number().int().optional().describe("Remote player 1..N (join order) (pie_console). 1..N relays to that remote player (join order) over the game connection, including separate-process clients; 0 or absent uses the local player (pie_inject_input, pie_inject_input_start, pie_inject_input_tape)"),
  client_id: z.number().int().min(0).optional().describe("Local-player index inside the selected PIE world (default 0) (pie_inject_input, pie_inject_input_start, pie_inject_input_tape, pie_observe_arm, pie_record_arm, pie_replay_arm, pie_replay_run, pie_send_key). With apply: local-player index inside that PIE world (default 0) (pie_replay_state)"),
  clips: z.record(z.string(), z.unknown()).optional().describe("Every clip the tracks place, by id: {rate, length, performer, input, lanes}"),
  codec: z.enum(["H264","VP8","VP9","AV1"]).optional().describe("Video codec (default H264)"),
  command: z.string().optional().describe("Console command"),
  confirm: z.boolean().optional().describe("Must be true to delete"),
  contains: z.string().optional().describe("Substring the message must contain"),
  control_rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Player controller control rotation, re-applied every frame while the hold runs. Local player only (pie_inject_input_start). New control rotation the hold re-applies every frame; sent alone it keeps the hold's value (pie_inject_input_update)"),
  csv_path: z.string().optional().describe("Explicit per-frame series CSV"),
  direction: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("World direction to push movement input in every frame"),
  directory: z.string().optional().describe("Content path prefix (default /Game)"),
  distance: z.number().optional().describe("Positive trace distance in cm (default 1000)"),
  drift_thresholds: z.record(z.string(), z.unknown()).optional().describe("{position_cm, rotation_deg, velocity_cms, tracked_default, tracked:{path: threshold}}"),
  driveId: z.string().optional().describe("Caller-chosen drive id (minted when absent) (pie_drive_pawn). Drive id from drive_pawn (default: the most recent drive) (pie_drive_status, pie_drive_stop)"),
  duration_seconds: z.number().min(0).optional().describe("Game seconds after which the hold releases itself (default 0: held until inject_input_stop or PIE ends). Local player only"),
  durationSeconds: z.number().min(0).max(600).optional().describe("Real seconds to drive before ending as completed or timeout (default 5, at most 600)"),
  eject: z.boolean().optional().describe("Eject to a spectator camera during replay"),
  encode_gif: z.boolean().optional().describe("Also encode an animated GIF (default false)"),
  event: z.enum(["press","down","up"]).optional().describe("press (default: down then up) | down | up"),
  file: z.string().optional().describe("Artifact to read; series returns per-channel time series (default manifest) (pie_observe_read). Artifact to read (default manifest) (pie_record_read)"),
  fixed_timestep: z.boolean().optional().describe("Fixed 1/pin_fps delta during replay (default false)"),
  fps: z.number().int().min(1).optional().describe("Encoded frame rate (default 30)"),
  hz: z.number().optional().describe("Playback rate (default 60)"),
  id: z.string().optional().describe("Recording id (default: a timestamp) (pie_record_arm). Recording id (pie_record_delete, pie_record_read)"),
  ignoreActors: z.array(z.string()).optional().describe("Actor labels or paths to skip"),
  ignorePawn: z.boolean().optional().describe("Ignore the viewing player's pawn (default true)"),
  include_values: z.boolean().optional().describe("series: include the downsampled values (default false)"),
  includeSamples: z.boolean().optional().describe("Include the sampled track (default true)"),
  injection_id: z.string().optional().describe("Caller-chosen hold id (minted when absent) (pie_inject_input_start). Hold or tape id to stop (pie_inject_input_stop). Caller-chosen tape id (minted when absent) (pie_inject_input_tape). Hold id from inject_input_start (pie_inject_input_update). Alias for request_id (pie_remote_result)"),
  key: z.string().optional().describe("FKey name: Right, Left, Up, Down, Tab, Enter, SpaceBar, Escape, Gamepad_DPad_Down, Gamepad_FaceButton_Bottom..."),
  kind: z.enum(["player","actor"]).optional().describe("The performer's kind"),
  label: z.string().optional().describe("Marker label"),
  limit: z.number().int().optional().describe("Most rows (default 200) (pie_observe_list, pie_record_list, pie_session_log). csv/tracked: most rows (default 1000) (pie_observe_read). csv: most rows (default 1000) (pie_record_read). Most frames_over_threshold entries returned (default 50) (pie_replay_analyze)"),
  lock_input: z.boolean().optional().describe("Block the user's keyboard, mouse and gamepad from the PIE game while the replay runs, so only the recording drives the player; Esc, Shift+F1 and F8 still work, and monitor mode ignores it (default false)"),
  max_errors: z.number().int().min(0).optional().describe("no-errors predicate threshold (default 0) (pie_scenario_scaffold). Most errors logged during the run (default 0) (pie_test_scaffold)"),
  max_message_chars: z.number().int().min(0).optional().describe("Longest message returned; longer ones are cut and marked truncated (default 500, 0 for no limit)"),
  max_offset_rows: z.number().int().min(0).optional().describe("align=auto: largest shift tried, in rows either way (default 30)"),
  max_points: z.number().int().min(1).optional().describe("series: most values per channel (default 500)"),
  max_position_drift_cm: z.number().optional().describe("Most position drift allowed (default 10)"),
  max_rotation_drift_deg: z.number().optional().describe("Most rotation drift allowed (default 5)"),
  max_velocity_drift_cms: z.number().optional().describe("Most velocity drift allowed (default 50)"),
  maxBitrateKbps: z.number().int().min(100).optional().describe("Ceiling on the WebRTC bitrate in kbps (default 4000)"),
  min_verbosity: z.string().optional().describe("Lowest severity kept (default warning) (pie_session_errors). Lowest severity kept (default log) (pie_session_log)"),
  mode: z.string().optional().describe("input (default): movement input on the pawn; navigate: a navmesh move from the player controller, needs targetLocation (pie_drive_pawn). replay drives input; monitor only samples (default replay) (pie_replay_arm, pie_replay_run)"),
  name: z.string().optional().describe("Asset name (pie_profile_create). Scenario name (default scenario) (pie_scenario_scaffold). Test name (default repro) (pie_test_run, pie_test_scaffold)"),
  note: z.string().optional().describe("What the verdict rests on"),
  offset: z.number().int().min(0).optional().describe("csv/tracked: first row (pie_observe_read). Rows to skip (pie_record_list). csv: first row (pie_record_read). First frames_over_threshold entry returned (pie_replay_analyze). First row (pie_session_log)"),
  onConflict: z.enum(["skip","replace"]).optional().describe("What to do when the asset exists (default skip)"),
  output_dir: z.string().optional().describe("Observation root (default Saved/MCPObservations)"),
  package_path: z.string().optional().describe("Content folder (default /Game/Observation)"),
  path: z.string().optional().describe("Profile asset path (pie_profile_delete, pie_profile_read, pie_profile_update). Scenario JSON file, relative to the project directory (pie_uat_run)"),
  performer: z.record(z.string(), z.unknown()).optional().describe("Who to record: {kind: player, client: 0} or {kind: actor, actor}"),
  pie_instance: z.number().int().min(0).optional().describe("FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back (pie_inject_input, pie_inject_input_start, pie_inject_input_tape, pie_observe_arm, pie_record_arm, pie_replay_arm, pie_replay_run). With apply: FWorldContext PIEInstance whose player to move (default the active PIE world) (pie_replay_state). FWorldContext::PIEInstance of the client whose UI gets the key. Omitted picks the editor's own player: the listen-server host, else the lowest client PIE instance (pie_send_key)"),
  pieInstance: z.number().int().min(0).optional().describe("FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back (pie_drive_pawn). PIE instance id; omitted selects the active PIE world (pie_view_trace)"),
  pin_fps: z.number().int().optional().describe("Pin PIE fps while observing (pie_observe_arm). Pin PIE fps for reproducibility (default sample_hz) (pie_record_arm). Pin PIE fps during replay (pie_replay_arm, pie_replay_run)"),
  planar: z.boolean().optional().describe("Steer and measure arrival on the XY plane (default true); false for flying or swimming pawns"),
  playerIndex: z.number().int().optional().describe("Local player index in the PIE world (default 0) (pie_drive_pawn). Local player index in the selected PIE world (default 0) (pie_view_trace)"),
  position_cm: z.number().optional().describe("Position drift threshold (default 5)"),
  position_threshold_cm: z.number().optional().describe("Position drift threshold"),
  profile: z.string().optional().describe("Observation profile asset path"),
  propertyNames: z.array(z.string()).optional().describe("Property names to read (default: all)"),
  punch: z.object({ clip: z.record(z.string(), z.unknown()).describe("The clip as the hub stores it"), from: z.number().describe("The frame the take starts on"), to: z.number().describe("The frame the take ends on") }).optional().describe("Punch in: play clip from frame 0 with the person's input locked out, record from `from` to `to` (the clip's frames), and replace that range"),
  rate: z.number().min(1).optional().describe("Frames per second of a new clip (default 60)"),
  reason: z.string().optional().describe("Why it stopped, for the unsettled checks (pie_arrangement_stop). Why the run is ended, recorded in the report (pie_uat_abort)"),
  record_drift: z.boolean().optional().describe("Record drift against the source (default true)"),
  recording_dir: z.string().optional().describe("Recording or replay folder (resolves recording.csv) (pie_assert_eval). Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings) (pie_perf_summary, pie_replay_analyze, pie_replay_arm, pie_replay_run, pie_replay_state, pie_scenario_scaffold, pie_test_list, pie_test_run, pie_test_scaffold). Recordings root (default Saved/MCPRecordings) (pie_record_arm, pie_record_delete, pie_record_diff, pie_record_list, pie_record_read)"),
  recording_id: z.string().optional().describe("Recording id under the recordings root (pie_assert_eval, pie_replay_analyze, pie_replay_arm, pie_replay_run, pie_replay_state, pie_scenario_scaffold, pie_test_list, pie_test_run, pie_test_scaffold). Recording id under the recordings root (default: the newest recording) (pie_perf_summary)"),
  region: z.string().optional().describe("Play from frame 0 to this region's end and judge only the checks that start inside it"),
  request_id: z.string().optional().describe("injection_id or request_id a relayed call returned"),
  resolution: z.string().optional().describe("WIDTHxHEIGHT such as 1280x720. Resizes the offscreen editor's window, or fixes the level viewport's size"),
  rng_seed: z.number().optional().describe("Seed the stream RNG for reproducible runs (pie_record_arm). Inline steps: RNG seed (pie_replay_arm, pie_replay_run)"),
  rotation_deg: z.number().optional().describe("Rotation drift threshold (default 2)"),
  rotation_threshold_deg: z.number().optional().describe("Rotation drift threshold"),
  run_gap_frames: z.number().int().optional().describe("Idle frames that split input runs (default 6)"),
  run_id: z.string().optional().describe("Observation run id (resolves observation.csv under output_dir) (pie_assert_eval). Observation run id (pie_observe_read)"),
  runId: z.string().optional().describe("The hub's id for the run"),
  sample_hz: z.number().int().min(1).optional().describe("Sample rate (default 60) (pie_observe_arm, pie_record_arm). Inline steps: sample rate (default 60) (pie_replay_arm, pie_replay_run)"),
  sampleEvery: z.number().min(0).optional().describe("Seconds between track samples; 0 samples every frame (default 0.1)"),
  scale: z.number().min(-1).max(1).optional().describe("Movement input scale (default 1)"),
  scenario: z.record(z.string(), z.unknown()).optional().describe("Inline scenario object (pie_scenario_validate). Scenario object: {name, map, pie:{players, net_mode, one_process, windowless, client_fps}, join_timeout_s, settle_ms, steps:[{name, do[], expect[{log host|client:N, pattern (regex), min, max, since step|run}], forbid[], window_ms}]}. A rule reads only lines logged after its step began unless since is run; see docs/pie.md (pie_uat_run)"),
  scenario_path: z.string().optional().describe("Path to a scenario.json"),
  sequence_path: z.string().optional().describe("Explicit sequence.json path"),
  session: z.string().optional().describe("Session id under Saved/MCPSessions (default: the live or most recent session)"),
  session_dir: z.string().optional().describe("Absolute session folder, overriding session"),
  settle_ms: z.number().int().min(0).optional().describe("Settle delay before playback (default 500)"),
  signallingUrl: z.string().optional().describe("ws:// or wss:// URL of the signalling server the editor's streamer connects to"),
  source: z.enum(["pie","level_viewport","editor"]).optional().describe("pie (default): the PIE viewport, armed until PIE starts; level_viewport: the active level viewport; editor: the whole editor"),
  start_when: z.record(z.string(), z.unknown()).optional().describe("Hold the steps until the player can act, then resume at the matching point of the recording: {pawn_at_rest (default true: speed <= max_speed_cms, default 5, and on the ground), hold_ms (default 500), log_contains, timeout_ms (default 60000)}. Replaces settle_ms (pie_replay_arm, pie_replay_run). Replay gate test_run uses, as replay_run's start_when (pie_test_scaffold)"),
  steps: z.array(z.record(z.string(), z.unknown())).optional().describe("Inline step array, instead of a recording"),
  stopOnMapChange: z.boolean().optional().describe("End the drive as map_changed when the player arrives in another level (default false: keep driving there)"),
  streamerId: z.string().optional().describe("Id the streamer identifies itself with at the signalling server (default ue-mcp)"),
  sweepRadius: z.number().optional().describe("Also sweep a sphere of this radius in cm when positive"),
  take_record: z.boolean().optional().describe("Also drive Take Recorder (default false)"),
  targetLocation: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("World point to steer toward; the drive ends as arrived within acceptanceRadius"),
  test_path: z.string().optional().describe("Scenario json whose predicates block is evaluated when assertions is omitted (pie_assert_eval). Explicit test.json path (pie_test_run)"),
  text: z.string().optional().describe("Characters to type into the focused widget, one character event each, instead of a key"),
  threshold: z.number().optional().describe("series: report the first frame where |value| exceeds this"),
  time_scale: z.number().optional().describe("Playback speed multiplier (default 1)"),
  timeoutSeconds: z.number().min(0).optional().describe("Longest wait=true waits (default durationSeconds + 30)"),
  traceComplex: z.boolean().optional().describe("Trace per-triangle collision (default false)"),
  track_actors: z.array(z.string()).optional().describe("Actor ids sampled for position, rotation and velocity"),
  track_values: z.array(z.string()).optional().describe("Dotted value paths sampled each frame; sub:<Class>.<path> reads a subsystem, bone:/bonecs:<Bone>.<channel> a bone"),
  tracked_actors: z.array(z.string()).optional().describe("Actor ids to track (pie_profile_create). Replaces the tracked actors (pie_profile_update)"),
  tracked_default: z.number().optional().describe("Default tracked-value threshold"),
  tracked_thresholds: z.record(z.string(), z.unknown()).optional().describe("Per-path tracked-value thresholds {path: threshold}"),
  tracked_value_default_threshold: z.number().optional().describe("Default tracked-value threshold"),
  tracked_values: z.array(z.unknown()).optional().describe("Value paths, or {path, drift_threshold} objects (pie_profile_create). Replaces the tracked values (pie_profile_update)"),
  tracked_widgets: z.array(z.string()).optional().describe("Widget class names or paths, or widget names, sampled as visible true or false (pie_profile_create). Replaces the tracked widgets (pie_profile_update)"),
  value_x: z.number().optional().describe("Axis or bool value X (default 1)"),
  value_y: z.number().optional().describe("Axis value Y (default 0)"),
  value_z: z.number().optional().describe("Axis value Z (default 0)"),
  values: z.array(z.unknown()).optional().describe("Per-frame values: numbers or [x,y,z] arrays"),
  velocity_cms: z.number().optional().describe("Velocity drift threshold (default 25)"),
  velocity_threshold_cms: z.number().optional().describe("Velocity drift threshold"),
  verdict: z.enum(["pass","fail"]).optional().describe("The verdict"),
  wait: z.boolean().optional().describe("Return only when the drive ends, with its whole track (default true)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
