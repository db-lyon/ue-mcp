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
        "description": "Teleport the live PIE pawn to the recorded pose (default false)"
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
  "pie_script_create": {
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
        "description": "Package folder (default /Game/PIEStudio/Scripts)"
      },
      {
        "name": "steps",
        "type": "array",
        "required": true,
        "description": "Steps, run in order: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}}",
        "items": "object"
      },
      {
        "name": "description",
        "type": "string",
        "required": false,
        "description": "What the script tests"
      },
      {
        "name": "profile",
        "type": "string",
        "required": false,
        "description": "Observation profile asset path"
      },
      {
        "name": "default_within_ms",
        "type": "integer",
        "required": false,
        "description": "Default auto-step window in game ms (default 5000)",
        "minimum": 1
      },
      {
        "name": "manual_only",
        "type": "boolean",
        "required": false,
        "description": "Every step is judged by a verdict: allows a script with no auto step that has an expect (default false)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default), update or replace (overwrite it), error",
        "enum": [
          "skip",
          "update",
          "replace",
          "error"
        ]
      }
    ],
    "contractExempt": "Creates and saves a script asset"
  },
  "pie_script_delete": {
    "category": "pie",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Test script asset path"
      },
      {
        "name": "confirm",
        "type": "boolean",
        "required": false,
        "description": "Must be true to delete"
      }
    ]
  },
  "pie_script_list": {
    "category": "pie",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content folder to list under (default /Game)"
      }
    ]
  },
  "pie_script_read": {
    "category": "pie",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Test script asset path"
      }
    ]
  },
  "pie_script_replay": {
    "category": "pie",
    "params": [
      {
        "name": "run_id",
        "type": "string",
        "required": false,
        "description": "Finished run to replay (default the last run)"
      },
      {
        "name": "results_path",
        "type": "string",
        "required": false,
        "description": "results.json of the run to replay, instead of a run id"
      },
      {
        "name": "time_scale",
        "type": "number",
        "required": false,
        "description": "Global time dilation during the replay (default 1)",
        "minimum": 0
      },
      {
        "name": "auto_stop_pie",
        "type": "boolean",
        "required": false,
        "description": "End PIE when the replay finishes (default true)"
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext PIEInstance to observe (default the active PIE world)",
        "minimum": 0
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Block until the run finishes or awaits a verdict"
      },
      {
        "name": "timeout_seconds",
        "type": "number",
        "required": false,
        "description": "Most seconds to wait when wait is set",
        "minimum": 0
      }
    ],
    "contractExempt": "Arms the replayer and observer and starts PIE"
  },
  "pie_script_results": {
    "category": "pie",
    "params": [
      {
        "name": "run_id",
        "type": "string",
        "required": false,
        "description": "Script run id (default the current or last run)"
      },
      {
        "name": "results_path",
        "type": "string",
        "required": false,
        "description": "results.json path, instead of a run id"
      }
    ]
  },
  "pie_script_run": {
    "category": "pie",
    "params": [
      {
        "name": "script",
        "type": "string",
        "required": false,
        "description": "Test script asset path"
      },
      {
        "name": "steps",
        "type": "array",
        "required": false,
        "description": "Inline steps instead of a script asset: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}}",
        "items": "object"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Name for an inline run (default inline)"
      },
      {
        "name": "profile",
        "type": "string",
        "required": false,
        "description": "Observation profile asset path; overrides the script's"
      },
      {
        "name": "record",
        "type": "boolean",
        "required": false,
        "description": "Record input so pie_script_replay can rerun it (default true)"
      },
      {
        "name": "start_pie",
        "type": "boolean",
        "required": false,
        "description": "Start PIE when it is not running (default false)"
      },
      {
        "name": "block_on_manual",
        "type": "boolean",
        "required": false,
        "description": "Hold the run at each manual block until every step in it has a verdict (default true)"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "full (default) plays the script in PIE; manual starts no PIE, observer or recording, leaves auto steps unchecked and puts every manual step up for its verdict at once",
        "enum": [
          "full",
          "manual"
        ]
      },
      {
        "name": "default_within_ms",
        "type": "integer",
        "required": false,
        "description": "Default auto-step window in game ms; overrides the script's (default 5000)",
        "minimum": 1
      },
      {
        "name": "pie_instance",
        "type": "integer",
        "required": false,
        "description": "FWorldContext PIEInstance to observe (default the active PIE world)",
        "minimum": 0
      },
      {
        "name": "client_id",
        "type": "integer",
        "required": false,
        "description": "Local-player index inside the selected PIE world (default 0)",
        "minimum": 0
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Block until the run finishes or awaits a verdict"
      },
      {
        "name": "timeout_seconds",
        "type": "number",
        "required": false,
        "description": "Most seconds to wait when wait is set",
        "minimum": 0
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "script"
          ],
          [
            "steps"
          ]
        ]
      }
    ],
    "contractExempt": "Arms the observer and recorder and can start PIE"
  },
  "pie_script_runs": {
    "category": "pie",
    "params": [
      {
        "name": "script",
        "type": "string",
        "required": false,
        "description": "Test script asset path: only that script's runs"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most runs, newest first (default 50)",
        "minimum": 1
      },
      {
        "name": "include_steps",
        "type": "boolean",
        "required": false,
        "description": "Each run's per-step results (default true)"
      }
    ]
  },
  "pie_script_status": {
    "category": "pie",
    "params": [
      {
        "name": "run_id",
        "type": "string",
        "required": false,
        "description": "Script run id (default the current or last run)"
      },
      {
        "name": "since_seq",
        "type": "integer",
        "required": false,
        "description": "Only events with a larger seq (default 0, every event)",
        "minimum": 0
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Block until the run finishes or awaits a verdict"
      },
      {
        "name": "timeout_seconds",
        "type": "number",
        "required": false,
        "description": "Most seconds to wait when wait is set",
        "minimum": 0
      }
    ]
  },
  "pie_script_stop": {
    "category": "pie",
    "params": [
      {
        "name": "run_id",
        "type": "string",
        "required": false,
        "description": "Run to stop; must be the active run when given"
      },
      {
        "name": "reason",
        "type": "string",
        "required": false,
        "description": "Recorded as the run's stop reason"
      }
    ]
  },
  "pie_script_update": {
    "category": "pie",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Test script asset path"
      },
      {
        "name": "steps",
        "type": "array",
        "required": false,
        "description": "Replacement steps: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}}",
        "items": "object"
      },
      {
        "name": "description",
        "type": "string",
        "required": false,
        "description": "What the script tests"
      },
      {
        "name": "profile",
        "type": "string",
        "required": false,
        "description": "Observation profile asset path; empty clears it"
      },
      {
        "name": "default_within_ms",
        "type": "integer",
        "required": false,
        "description": "Default auto-step window in game ms",
        "minimum": 1
      },
      {
        "name": "manual_only",
        "type": "boolean",
        "required": false,
        "description": "Every step is judged by a verdict: allows a script with no auto step that has an expect (default false)"
      }
    ]
  },
  "pie_script_validate": {
    "category": "pie",
    "params": [
      {
        "name": "steps",
        "type": "array",
        "required": true,
        "description": "Steps to check, as script_create takes them: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}}",
        "items": "object"
      },
      {
        "name": "profile",
        "type": "string",
        "required": false,
        "description": "Observation profile asset path the script would use"
      },
      {
        "name": "manual_only",
        "type": "boolean",
        "required": false,
        "description": "Every step is judged by a verdict: allows a script with no auto step that has an expect (default false)"
      }
    ]
  },
  "pie_script_values": {
    "category": "pie",
    "params": [
      {
        "name": "target",
        "type": "string",
        "required": false,
        "description": "Actor class or Blueprint to report on: its actors in the level, and whether it is the player pawn"
      },
      {
        "name": "profile",
        "type": "string",
        "required": false,
        "description": "Observation profile asset path whose tracked values to list"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most entries per list (default 400)",
        "minimum": 1
      }
    ]
  },
  "pie_script_verdict": {
    "category": "pie",
    "params": [
      {
        "name": "step",
        "type": "integer",
        "required": true,
        "description": "Manual step index, 0-based",
        "minimum": 0
      },
      {
        "name": "verdict",
        "type": "string",
        "required": true,
        "description": "Verdict",
        "enum": [
          "pass",
          "fail",
          "skip"
        ]
      },
      {
        "name": "note",
        "type": "string",
        "required": false,
        "description": "Tester's note"
      },
      {
        "name": "run_id",
        "type": "string",
        "required": false,
        "description": "Run the step belongs to (default the current or last run)"
      },
      {
        "name": "source",
        "type": "string",
        "required": false,
        "description": "Who judged the step: human (default) or agent. An agent verdict needs evidence and is refused on a step with agent_judgeable false. elicitation is the verdict form's human channel, action an alias of human",
        "enum": [
          "human",
          "agent",
          "elicitation",
          "action"
        ]
      },
      {
        "name": "evidence",
        "type": "object",
        "required": false,
        "description": "What the verdict rests on; required when source is agent",
        "fields": [
          {
            "name": "note",
            "type": "string",
            "required": true,
            "description": "What was observed"
          },
          {
            "name": "captures",
            "type": "array",
            "required": false,
            "description": "Capture files that show it: absolute, or relative to the run's output_dir; each must exist",
            "items": "string"
          }
        ]
      }
    ]
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
      }
    ]
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
        "description": "Scenario object: {name, map, pie:{players, net_mode, one_process, windowless, client_fps}, join_timeout_s, settle_ms, steps:[{name, do[], expect[], forbid[], window_ms}]}; see docs/pie.md"
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
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  pie_anim_properties: "Params: actorLabel, propertyNames?",
  pie_anim_state: "Params: actorLabel",
  pie_assert_eval: "Params: run_id?, output_dir?, recording_dir?, recording_id?, csv_path?, assertions?, test_path?",
  pie_clients: "Params: none",
  pie_console: "Params: command, client",
  pie_inject_input: "Params: action_path, value_x?, value_y?, value_z?, client_id?, pie_instance?, client?",
  pie_inject_input_start: "Params: action_path, value_x?, value_y?, value_z?, injection_id?, client_id?, pie_instance?, client?",
  pie_inject_input_stop: "Params: injection_id",
  pie_inject_input_tape: "Params: action_path, values, hz?, injection_id?, client_id?, pie_instance?, client?",
  pie_inject_input_update: "Params: injection_id, value_x?, value_y?, value_z?",
  pie_mark: "Params: label",
  pie_observe_arm: "Params: profile, output_dir?, sample_hz?, client_id?, pie_instance?, pin_fps?",
  pie_observe_disarm: "Params: none",
  pie_observe_list: "Params: output_dir?, limit?",
  pie_observe_read: "Params: run_id, output_dir?, file?, limit?, offset?, channels?, threshold?, include_values?, max_points?",
  pie_observe_status: "Params: none",
  pie_observe_stop: "Params: none",
  pie_perf_summary: "Params: recording_id?, recording_dir?",
  pie_profile_create: "Params: name, package_path?, onConflict?, tracked_values?, tracked_actors?, tracked_widgets?, capture_pawn_state?, capture_montage?, position_threshold_cm?, rotation_threshold_deg?, velocity_threshold_cms?, tracked_value_default_threshold?",
  pie_profile_delete: "Params: path, confirm?",
  pie_profile_list: "Params: directory?",
  pie_profile_read: "Params: path",
  pie_profile_update: "Params: path, tracked_values?, tracked_actors?, tracked_widgets?, capture_pawn_state?, capture_montage?, position_threshold_cm?, rotation_threshold_deg?, velocity_threshold_cms?, tracked_value_default_threshold?",
  pie_record_arm: "Params: actions?, track_values?, track_actors?, axis_threshold?, sample_hz?, pin_fps?, capture_pawn_state?, capture_montage?, take_record?, client_id?, pie_instance?, rng_seed?, run_gap_frames?, recording_dir?, id?",
  pie_record_delete: "Params: id, confirm?, recording_dir?",
  pie_record_diff: "Params: a_id, b_id, recording_dir?, position_cm?, rotation_deg?, velocity_cms?, tracked_default?, tracked_thresholds?",
  pie_record_disarm: "Params: none",
  pie_record_list: "Params: recording_dir?, limit?, offset?",
  pie_record_read: "Params: id, file?, recording_dir?, limit?, offset?",
  pie_record_status: "Params: none",
  pie_record_stop: "Params: none",
  pie_remote_result: "Params: request_id (or injection_id)",
  pie_replay_analyze: "Params: recording_id?, recording_dir?",
  pie_replay_arm: "Params: recording_id?, sequence_path?, recording_dir?, steps?, settle_ms?, sample_hz?, rng_seed?, pin_fps?, apply_rng_seed?, record_drift?, auto_stop_pie?, eject?, time_scale?, mode?, capture_frame_every?, encode_gif?, fixed_timestep?, client_id?, pie_instance?, drift_thresholds?",
  pie_replay_disarm: "Params: none",
  pie_replay_run: "Params: recording_id?, sequence_path?, recording_dir?, steps?, settle_ms?, sample_hz?, rng_seed?, pin_fps?, apply_rng_seed?, record_drift?, auto_stop_pie?, eject?, time_scale?, mode?, capture_frame_every?, encode_gif?, fixed_timestep?, client_id?, pie_instance?, drift_thresholds?",
  pie_replay_state: "Params: recording_id?, recording_dir?, at_time?, at_frame?, apply?",
  pie_replay_status: "Params: none",
  pie_replay_stop: "Params: none",
  pie_scenario_scaffold: "Params: recording_id?, recording_dir?, name?, max_errors?",
  pie_scenario_validate: "Params: scenario_path OR scenario",
  pie_script_create: "Params: name, package_path?, steps, description?, profile?, default_within_ms?, manual_only?, onConflict?",
  pie_script_delete: "Params: path, confirm?",
  pie_script_list: "Params: directory?",
  pie_script_read: "Params: path",
  pie_script_replay: "Params: run_id?, results_path?, time_scale?, auto_stop_pie?, pie_instance?, client_id?, wait?, timeout_seconds?",
  pie_script_results: "Params: run_id?, results_path?",
  pie_script_run: "Params: script OR steps, name?, profile?, record?, start_pie?, block_on_manual?, mode?, default_within_ms?, pie_instance?, client_id?, wait?, timeout_seconds?",
  pie_script_runs: "Params: script?, limit?, include_steps?",
  pie_script_status: "Params: run_id?, since_seq?, wait?, timeout_seconds?",
  pie_script_stop: "Params: run_id?, reason?",
  pie_script_update: "Params: path, steps?, description?, profile?, default_within_ms?, manual_only?",
  pie_script_validate: "Params: steps, profile?, manual_only?",
  pie_script_values: "Params: target?, profile?, limit?",
  pie_script_verdict: "Params: step, verdict, note?, run_id?, source?, evidence?",
  pie_session_errors: "Params: session?, session_dir?, min_verbosity?",
  pie_session_log: "Params: session?, session_dir?, min_verbosity?, category?, contains?, limit?, offset?",
  pie_test_list: "Params: recording_id?, recording_dir?",
  pie_test_run: "Params: test_path?, recording_id?, recording_dir?, name?, auto_run?",
  pie_test_scaffold: "Params: recording_id?, recording_dir?, name?, max_position_drift_cm?, max_velocity_drift_cms?, max_rotation_drift_deg?, max_errors?",
  pie_uat_abort: "Params: reason?",
  pie_uat_run: "Params: scenario OR path",
  pie_uat_status: "Params: none",
};

/** Every key the spec'd pie handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  a_id: z.string().optional().describe("First recording id"),
  action_path: z.string().optional().describe("InputAction asset path"),
  actions: z.array(z.string()).optional().describe("InputAction paths to record (default: every action bound on the pawn)"),
  actorLabel: z.string().optional().describe("Actor label or name in PIE"),
  apply: z.boolean().optional().describe("Teleport the live PIE pawn to the recorded pose (default false)"),
  apply_rng_seed: z.boolean().optional().describe("Re-apply the recorded seed (default true)"),
  assertions: z.array(z.record(z.string(), z.unknown())).optional().describe("Predicates {name, channel|event, op, value|min+max|tol, hold, from_s, to_s, within_s, after_event, severity}; omitted reads test_path's predicates"),
  at_frame: z.number().int().min(0).optional().describe("Frame index to sample, instead of at_time"),
  at_time: z.number().optional().describe("Seconds to sample, interpolated (default: the last frame)"),
  auto_run: z.boolean().optional().describe("Drive the replay when no drift.json exists yet (default true)"),
  auto_stop_pie: z.boolean().optional().describe("End PIE when the replay completes (replay_arm default false, replay_run default true) (pie_replay_arm, pie_replay_run). End PIE when the replay finishes (default true) (pie_script_replay)"),
  axis_threshold: z.number().optional().describe("Axis change deadzone (default 0.15)"),
  b_id: z.string().optional().describe("Second recording id"),
  block_on_manual: z.boolean().optional().describe("Hold the run at each manual block until every step in it has a verdict (default true)"),
  capture_frame_every: z.number().int().min(0).optional().describe("Capture a viewport JPEG every N steps and build a contact sheet"),
  capture_montage: z.boolean().optional().describe("Sample the active montage (pie_profile_create, pie_profile_update). Sample the active montage (default true) (pie_record_arm)"),
  capture_pawn_state: z.boolean().optional().describe("Sample the pawn transform and movement (pie_profile_create, pie_profile_update). Sample the pawn transform and movement (default true) (pie_record_arm)"),
  category: z.string().optional().describe("Exact log category, such as LogTemp"),
  channels: z.string().optional().describe("series: comma-separated columns (default: every t:* plus speed2d, gpu_ms, game_ms)"),
  client: z.number().int().optional().describe("Remote player 1..N (join order) (pie_console). 1..N relays to that remote player (join order) over the game connection, including separate-process clients; 0 or absent uses the local player (pie_inject_input, pie_inject_input_start, pie_inject_input_tape)"),
  client_id: z.number().int().min(0).optional().describe("Local-player index inside the selected PIE world (default 0)"),
  command: z.string().optional().describe("Console command"),
  confirm: z.boolean().optional().describe("Must be true to delete"),
  contains: z.string().optional().describe("Substring the message must contain"),
  csv_path: z.string().optional().describe("Explicit per-frame series CSV"),
  default_within_ms: z.number().int().min(1).optional().describe("Default auto-step window in game ms (default 5000) (pie_script_create). Default auto-step window in game ms; overrides the script's (default 5000) (pie_script_run). Default auto-step window in game ms (pie_script_update)"),
  description: z.string().optional().describe("What the script tests"),
  directory: z.string().optional().describe("Content path prefix (default /Game) (pie_profile_list). Content folder to list under (default /Game) (pie_script_list)"),
  drift_thresholds: z.record(z.string(), z.unknown()).optional().describe("{position_cm, rotation_deg, velocity_cms, tracked_default, tracked:{path: threshold}}"),
  eject: z.boolean().optional().describe("Eject to a spectator camera during replay"),
  encode_gif: z.boolean().optional().describe("Also encode an animated GIF (default false)"),
  evidence: z.object({ note: z.string().describe("What was observed"), captures: z.array(z.string()).optional().describe("Capture files that show it: absolute, or relative to the run's output_dir; each must exist") }).optional().describe("What the verdict rests on; required when source is agent"),
  file: z.string().optional().describe("Artifact to read; series returns per-channel time series (default manifest) (pie_observe_read). Artifact to read (default manifest) (pie_record_read)"),
  fixed_timestep: z.boolean().optional().describe("Fixed 1/pin_fps delta during replay (default false)"),
  hz: z.number().optional().describe("Playback rate (default 60)"),
  id: z.string().optional().describe("Recording id (default: a timestamp) (pie_record_arm). Recording id (pie_record_delete, pie_record_read)"),
  include_steps: z.boolean().optional().describe("Each run's per-step results (default true)"),
  include_values: z.boolean().optional().describe("series: include the downsampled values (default false)"),
  injection_id: z.string().optional().describe("Caller-chosen hold id (minted when absent) (pie_inject_input_start). Hold or tape id to stop (pie_inject_input_stop). Caller-chosen tape id (minted when absent) (pie_inject_input_tape). Hold id from inject_input_start (pie_inject_input_update). Alias for request_id (pie_remote_result)"),
  label: z.string().optional().describe("Marker label"),
  limit: z.number().int().min(1).optional().describe("Most rows (default 200) (pie_observe_list, pie_record_list, pie_session_log). csv/tracked: most rows (default 1000) (pie_observe_read). csv: most rows (default 1000) (pie_record_read). Most runs, newest first (default 50) (pie_script_runs). Most entries per list (default 400) (pie_script_values)"),
  manual_only: z.boolean().optional().describe("Every step is judged by a verdict: allows a script with no auto step that has an expect (default false)"),
  max_errors: z.number().int().min(0).optional().describe("no-errors predicate threshold (default 0) (pie_scenario_scaffold). Most errors logged during the run (default 0) (pie_test_scaffold)"),
  max_points: z.number().int().min(1).optional().describe("series: most values per channel (default 500)"),
  max_position_drift_cm: z.number().optional().describe("Most position drift allowed (default 10)"),
  max_rotation_drift_deg: z.number().optional().describe("Most rotation drift allowed (default 5)"),
  max_velocity_drift_cms: z.number().optional().describe("Most velocity drift allowed (default 50)"),
  min_verbosity: z.string().optional().describe("Lowest severity kept (default warning) (pie_session_errors). Lowest severity kept (default log) (pie_session_log)"),
  mode: z.string().optional().describe("replay drives input; monitor only samples (default replay) (pie_replay_arm, pie_replay_run). full (default) plays the script in PIE; manual starts no PIE, observer or recording, leaves auto steps unchecked and puts every manual step up for its verdict at once (pie_script_run)"),
  name: z.string().optional().describe("Asset name (pie_profile_create, pie_script_create). Scenario name (default scenario) (pie_scenario_scaffold). Name for an inline run (default inline) (pie_script_run). Test name (default repro) (pie_test_run, pie_test_scaffold)"),
  note: z.string().optional().describe("Tester's note"),
  offset: z.number().int().min(0).optional().describe("csv/tracked: first row (pie_observe_read). Rows to skip (pie_record_list). csv: first row (pie_record_read). First row (pie_session_log)"),
  onConflict: z.string().optional().describe("What to do when the asset exists (default skip) (pie_profile_create). When the asset exists: skip (default), update or replace (overwrite it), error (pie_script_create)"),
  output_dir: z.string().optional().describe("Observation root (default Saved/MCPObservations)"),
  package_path: z.string().optional().describe("Content folder (default /Game/Observation) (pie_profile_create). Package folder (default /Game/PIEStudio/Scripts) (pie_script_create)"),
  path: z.string().optional().describe("Profile asset path (pie_profile_delete, pie_profile_read, pie_profile_update). Test script asset path (pie_script_delete, pie_script_read, pie_script_update). Scenario JSON file, relative to the project directory (pie_uat_run)"),
  pie_instance: z.number().int().min(0).optional().describe("FWorldContext::PIEInstance, a non-negative integer. Omitted picks the active PlayWorld, then the first PIE world with the player; an explicit value never falls back (pie_inject_input, pie_inject_input_start, pie_inject_input_tape, pie_observe_arm, pie_record_arm, pie_replay_arm, pie_replay_run). FWorldContext PIEInstance to observe (default the active PIE world) (pie_script_replay, pie_script_run)"),
  pin_fps: z.number().int().optional().describe("Pin PIE fps while observing (pie_observe_arm). Pin PIE fps for reproducibility (default sample_hz) (pie_record_arm). Pin PIE fps during replay (pie_replay_arm, pie_replay_run)"),
  position_cm: z.number().optional().describe("Position drift threshold (default 5)"),
  position_threshold_cm: z.number().optional().describe("Position drift threshold"),
  profile: z.string().optional().describe("Observation profile asset path (pie_observe_arm, pie_script_create). Observation profile asset path; overrides the script's (pie_script_run). Observation profile asset path; empty clears it (pie_script_update). Observation profile asset path the script would use (pie_script_validate). Observation profile asset path whose tracked values to list (pie_script_values)"),
  propertyNames: z.array(z.string()).optional().describe("Property names to read (default: all)"),
  reason: z.string().optional().describe("Recorded as the run's stop reason (pie_script_stop). Why the run is ended, recorded in the report (pie_uat_abort)"),
  record: z.boolean().optional().describe("Record input so pie_script_replay can rerun it (default true)"),
  record_drift: z.boolean().optional().describe("Record drift against the source (default true)"),
  recording_dir: z.string().optional().describe("Recording or replay folder (resolves recording.csv) (pie_assert_eval). Recordings root, or with no id the recording folder itself (default Saved/MCPRecordings) (pie_perf_summary, pie_replay_analyze, pie_replay_arm, pie_replay_run, pie_replay_state, pie_scenario_scaffold, pie_test_list, pie_test_run, pie_test_scaffold). Recordings root (default Saved/MCPRecordings) (pie_record_arm, pie_record_delete, pie_record_diff, pie_record_list, pie_record_read)"),
  recording_id: z.string().optional().describe("Recording id under the recordings root"),
  request_id: z.string().optional().describe("injection_id or request_id a relayed call returned"),
  results_path: z.string().optional().describe("results.json of the run to replay, instead of a run id (pie_script_replay). results.json path, instead of a run id (pie_script_results)"),
  rng_seed: z.number().optional().describe("Seed the stream RNG for reproducible runs (pie_record_arm). Inline steps: RNG seed (pie_replay_arm, pie_replay_run)"),
  rotation_deg: z.number().optional().describe("Rotation drift threshold (default 2)"),
  rotation_threshold_deg: z.number().optional().describe("Rotation drift threshold"),
  run_gap_frames: z.number().int().optional().describe("Idle frames that split input runs (default 6)"),
  run_id: z.string().optional().describe("Observation run id (resolves observation.csv under output_dir) (pie_assert_eval). Observation run id (pie_observe_read). Finished run to replay (default the last run) (pie_script_replay). Script run id (default the current or last run) (pie_script_results, pie_script_status). Run to stop; must be the active run when given (pie_script_stop). Run the step belongs to (default the current or last run) (pie_script_verdict)"),
  sample_hz: z.number().int().min(1).optional().describe("Sample rate (default 60) (pie_observe_arm, pie_record_arm). Inline steps: sample rate (default 60) (pie_replay_arm, pie_replay_run)"),
  scenario: z.record(z.string(), z.unknown()).optional().describe("Inline scenario object (pie_scenario_validate). Scenario object: {name, map, pie:{players, net_mode, one_process, windowless, client_fps}, join_timeout_s, settle_ms, steps:[{name, do[], expect[], forbid[], window_ms}]}; see docs/pie.md (pie_uat_run)"),
  scenario_path: z.string().optional().describe("Path to a scenario.json"),
  script: z.string().optional().describe("Test script asset path (pie_script_run). Test script asset path: only that script's runs (pie_script_runs)"),
  sequence_path: z.string().optional().describe("Explicit sequence.json path"),
  session: z.string().optional().describe("Session id under Saved/MCPSessions (default: the live or most recent session)"),
  session_dir: z.string().optional().describe("Absolute session folder, overriding session"),
  settle_ms: z.number().int().min(0).optional().describe("Settle delay before playback (default 500)"),
  since_seq: z.number().int().min(0).optional().describe("Only events with a larger seq (default 0, every event)"),
  source: z.enum(["human","agent","elicitation","action"]).optional().describe("Who judged the step: human (default) or agent. An agent verdict needs evidence and is refused on a step with agent_judgeable false. elicitation is the verdict form's human channel, action an alias of human"),
  start_pie: z.boolean().optional().describe("Start PIE when it is not running (default false)"),
  step: z.number().int().min(0).optional().describe("Manual step index, 0-based"),
  steps: z.array(z.record(z.string(), z.unknown())).optional().describe("Inline step array, instead of a recording (pie_replay_arm, pie_replay_run). Steps, run in order: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}} (pie_script_create). Inline steps instead of a script asset: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}} (pie_script_run). Replacement steps: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}} (pie_script_update). Steps to check, as script_create takes them: {instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}} (pie_script_validate)"),
  take_record: z.boolean().optional().describe("Also drive Take Recorder (default false)"),
  target: z.string().optional().describe("Actor class or Blueprint to report on: its actors in the level, and whether it is the player pawn"),
  test_path: z.string().optional().describe("Scenario json whose predicates block is evaluated when assertions is omitted (pie_assert_eval). Explicit test.json path (pie_test_run)"),
  threshold: z.number().optional().describe("series: report the first frame where |value| exceeds this"),
  time_scale: z.number().optional().describe("Playback speed multiplier (default 1) (pie_replay_arm, pie_replay_run). Global time dilation during the replay (default 1) (pie_script_replay)"),
  timeout_seconds: z.number().min(0).optional().describe("Most seconds to wait when wait is set"),
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
  verdict: z.enum(["pass","fail","skip"]).optional().describe("Verdict"),
  wait: z.boolean().optional().describe("Block until the run finishes or awaits a verdict"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
