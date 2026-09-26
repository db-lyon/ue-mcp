// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd editor handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_sequence_section": {
    "category": "editor",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "Level Sequence asset path",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "trackType",
        "type": "string",
        "required": true,
        "description": "Transform | Float | SkeletalAnimation | CameraCut | Audio | Event | Fade"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "startSeconds",
        "type": "number",
        "required": false,
        "description": "Section start in seconds"
      },
      {
        "name": "endSeconds",
        "type": "number",
        "required": false,
        "description": "Section end in seconds (default one second after the start)"
      },
      {
        "name": "cameraActorLabel",
        "type": "string",
        "required": false,
        "description": "Camera actor to bind a CameraCut section to"
      },
      {
        "name": "cameraActorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the camera actor. Wins over cameraActorLabel"
      }
    ]
  },
  "add_sequence_track": {
    "category": "editor",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Level Sequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "trackType",
        "type": "string",
        "required": true,
        "description": "Transform | Float | SkeletalAnimation | CameraCut | Audio | Event | Fade"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      }
    ]
  },
  "add_trace_bookmark": {
    "category": "editor",
    "params": [
      {
        "name": "bookmarkName",
        "type": "string",
        "required": true,
        "description": "Label for the timeline marker"
      }
    ],
    "contractExempt": "writes a bookmark into the running trace"
  },
  "begin_editor_transaction": {
    "category": "editor",
    "params": [
      {
        "name": "description",
        "type": "string",
        "required": false,
        "description": "Undo-stack label for the transaction (default MCP Edit)",
        "aliases": [
          "label"
        ]
      }
    ],
    "contractExempt": "opens an undo transaction"
  },
  "begin_profile_region": {
    "category": "editor",
    "params": [
      {
        "name": "regionName",
        "type": "string",
        "required": true,
        "description": "Name the bracket is keyed by; end_profile_region closes it"
      },
      {
        "name": "regionCategory",
        "type": "string",
        "required": false,
        "description": "Category shown alongside the region in Unreal Insights"
      }
    ],
    "contractExempt": "opens a profiling region"
  },
  "build_all": {
    "category": "editor",
    "params": [],
    "contractExempt": "builds geometry, lighting, paths and HLODs"
  },
  "build_geometry": {
    "category": "editor",
    "params": [],
    "contractExempt": "rebuilds BSP geometry"
  },
  "build_hlod": {
    "category": "editor",
    "params": [],
    "contractExempt": "builds HLODs"
  },
  "build_lighting": {
    "category": "editor",
    "params": [
      {
        "name": "quality",
        "type": "string",
        "required": false,
        "description": "Preview (default) | Medium | High | Production"
      }
    ],
    "contractExempt": "builds lighting for the open level"
  },
  "cancel_editor_transaction": {
    "category": "editor",
    "params": [
      {
        "name": "index",
        "type": "number",
        "required": false,
        "description": "Which open transaction to cancel (default 0)"
      }
    ],
    "contractExempt": "cancels the open undo transaction and restores what it touched"
  },
  "capture_scene_png": {
    "category": "editor",
    "params": [
      {
        "name": "outputPath",
        "type": "string",
        "required": true,
        "description": "Absolute or project-relative PNG path to write, e.g. Saved/Screenshots/cap.png",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Camera location"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "Camera rotation"
      },
      {
        "name": "focusActorLabel",
        "type": "string",
        "required": false,
        "description": "Frame the camera on this actor's bounds"
      },
      {
        "name": "focusActorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the actor to frame. Wins over focusActorLabel"
      },
      {
        "name": "focusDirection",
        "type": "vec3",
        "required": false,
        "description": "Framing direction from the focus actor (default front and above)"
      },
      {
        "name": "focusMargin",
        "type": "number",
        "required": false,
        "description": "Positive bounds fill margin; higher pulls back (default 1.5)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor (default) | pie"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      },
      {
        "name": "width",
        "type": "number",
        "required": false,
        "description": "Capture width in pixels (default 1280)"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "Capture height in pixels (default 720)"
      },
      {
        "name": "fov",
        "type": "number",
        "required": false,
        "description": "Capture FOV in degrees, greater than 0 and less than 180 (default 90)"
      },
      {
        "name": "fullyLoadTextures",
        "type": "boolean",
        "required": false,
        "description": "Stream textures in and flush the render thread before the capture (default true)"
      }
    ],
    "contractExempt": "spawns a capture actor and writes an image file"
  },
  "capture_screenshot": {
    "category": "editor",
    "params": [
      {
        "name": "filename",
        "type": "string",
        "required": true,
        "description": "Image path to write; .png is appended without an image extension",
        "aliases": [
          "outputPath"
        ]
      },
      {
        "name": "target",
        "type": "string",
        "required": false,
        "description": "auto (default) | pie | editor | window"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      },
      {
        "name": "worldPath",
        "type": "string",
        "required": false,
        "description": "Exact PIE UWorld path or name to capture"
      }
    ],
    "contractExempt": "writes an image file"
  },
  "check_for_crashes": {
    "category": "editor",
    "params": []
  },
  "clear_dialog_policy": {
    "category": "editor",
    "params": [
      {
        "name": "pattern",
        "type": "string",
        "required": false,
        "description": "Exact pattern of the policy to clear; omit to clear every policy"
      }
    ]
  },
  "close_sequence": {
    "category": "editor",
    "params": [],
    "contractExempt": "closes the open Sequencer"
  },
  "configure_pie": {
    "category": "editor",
    "params": [
      {
        "name": "numClients",
        "type": "number",
        "required": false,
        "description": "Number of PIE clients"
      },
      {
        "name": "netMode",
        "type": "string",
        "required": false,
        "description": "standalone | listen | client"
      },
      {
        "name": "runUnderOneProcess",
        "type": "boolean",
        "required": false,
        "description": "Run every client in the editor process"
      },
      {
        "name": "launchSeparateServer",
        "type": "boolean",
        "required": false,
        "description": "Launch a separate dedicated server"
      },
      {
        "name": "newWindowWidth",
        "type": "number",
        "required": false,
        "description": "Play-in-New-Window width"
      },
      {
        "name": "newWindowHeight",
        "type": "number",
        "required": false,
        "description": "Play-in-New-Window height"
      }
    ],
    "contractExempt": "writes the editor's Play settings"
  },
  "cook_content": {
    "category": "editor",
    "params": [
      {
        "name": "platform",
        "type": "string",
        "required": false,
        "description": "Target platform (default Windows)"
      }
    ],
    "contractExempt": "starts a content cook"
  },
  "create_level_sequence": {
    "category": "editor",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Name of the new Level Sequence asset"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Cinematics)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ],
    "contractExempt": "creates a Level Sequence asset"
  },
  "create_new_level": {
    "category": "editor",
    "params": [
      {
        "name": "levelPath",
        "type": "string",
        "required": true,
        "description": "Long package path of the new level, e.g. /Game/Maps/MyLevel. Validated before the engine is asked"
      },
      {
        "name": "templateLevel",
        "type": "string",
        "required": false,
        "description": "Level to copy; omit, Empty or None for a blank level"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the level exists: skip (default) returns it untouched, error refuses"
      }
    ],
    "contractExempt": "creates and opens a level"
  },
  "describe_object": {
    "category": "editor",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": true,
        "description": "Object, asset, class or Blueprint path. A class or Blueprint resolves to its default object",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "includeProperties",
        "type": "boolean",
        "required": false,
        "description": "Include reflected property metadata (default true)"
      },
      {
        "name": "includeValues",
        "type": "boolean",
        "required": false,
        "description": "Include current property values (default false)"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Dotted or indexed property paths to report instead of every property",
        "items": "string"
      }
    ]
  },
  "end_editor_transaction": {
    "category": "editor",
    "params": [],
    "contractExempt": "commits the open undo transaction"
  },
  "end_profile_region": {
    "category": "editor",
    "params": [
      {
        "name": "regionName",
        "type": "string",
        "required": true,
        "description": "Name the region was opened under"
      }
    ]
  },
  "execute_command": {
    "category": "editor",
    "params": [
      {
        "name": "command",
        "type": "string",
        "required": true,
        "description": "Console command to run in the editor world"
      }
    ],
    "contractExempt": "runs a console command"
  },
  "find_object": {
    "category": "editor",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": false,
        "description": "Check one path: reports found and isValid rather than failing when it is gone. Wins over the search filters"
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Class to search for: a short name, a /Script path, a generated class name (WBP_Hud_C) or a Blueprint asset path"
      },
      {
        "name": "nameContains",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the object name"
      },
      {
        "name": "outerPath",
        "type": "string",
        "required": false,
        "description": "Only objects somewhere under this outer, such as one level or world"
      },
      {
        "name": "exactClass",
        "type": "boolean",
        "required": false,
        "description": "Match className exactly instead of including subclasses (default false)"
      },
      {
        "name": "includeDefaults",
        "type": "boolean",
        "required": false,
        "description": "Include class default objects and archetypes (default false)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: any (default) | editor | pie"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Objects on this page (default 50, max 1000)"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "objectPath"
          ],
          [
            "className"
          ],
          [
            "nameContains"
          ]
        ]
      }
    ]
  },
  "focus_viewport_on_actor": {
    "category": "editor",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      }
    ]
  },
  "get_build_status": {
    "category": "editor",
    "params": []
  },
  "get_crash_info": {
    "category": "editor",
    "params": [
      {
        "name": "crashFolder",
        "type": "string",
        "required": true,
        "description": "Crash folder name, as list_crashes reports it"
      }
    ]
  },
  "get_cvars": {
    "category": "editor",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Console variable to read"
      },
      {
        "name": "names",
        "type": "array",
        "required": false,
        "description": "Console variables to read",
        "items": "string"
      },
      {
        "name": "pattern",
        "type": "string",
        "required": false,
        "description": "Substring matched against every registered console variable. Pass at least one of name, names and pattern"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Max rows for a pattern search (default 100, max 1000)"
      }
    ]
  },
  "get_dialog_policy": {
    "category": "editor",
    "params": []
  },
  "get_editor_performance_stats": {
    "category": "editor",
    "params": []
  },
  "get_frame_timing": {
    "category": "editor",
    "params": [
      {
        "name": "cpuGpuMarginPercent",
        "type": "number",
        "required": false,
        "description": "How far ahead one side must be before the frame is called bound by it (default 10)"
      }
    ]
  },
  "get_insights_trace_status": {
    "category": "editor",
    "params": []
  },
  "get_message_log": {
    "category": "editor",
    "params": [
      {
        "name": "logName",
        "type": "string",
        "required": false,
        "description": "Listing to read (MapCheck, AssetCheck, PIE, LoadErrors...); omit to list the registered ones"
      },
      {
        "name": "maxLines",
        "type": "integer",
        "required": false,
        "description": "Messages to return (default 200)"
      },
      {
        "name": "severity",
        "type": "string",
        "required": false,
        "description": "Severity-name substring: Error | Warning | PerformanceWarning | Info"
      }
    ]
  },
  "get_object_properties": {
    "category": "editor",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": false,
        "description": "Object path of the live instance. Wins over target"
      },
      {
        "name": "target",
        "type": "string",
        "required": false,
        "description": "gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem"
      },
      {
        "name": "subsystemClass",
        "type": "string",
        "required": false,
        "description": "Subsystem class name or /Script path, with target=subsystem"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Player index for target=playercontroller or playerpawn (default 0)"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Only these properties. The Details-panel spelling is accepted",
        "items": "string"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Max properties returned (default 200)"
      },
      {
        "name": "maxValueLength",
        "type": "number",
        "required": false,
        "description": "Truncate each exported value past this many characters (default 2000)"
      }
    ]
  },
  "get_open_asset_editors": {
    "category": "editor",
    "params": []
  },
  "get_output_log": {
    "category": "editor",
    "params": [
      {
        "name": "maxLines",
        "type": "integer",
        "required": false,
        "description": "How far back into the ring buffer to read (default 100)"
      },
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring the message must contain"
      },
      {
        "name": "category",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring the log category must contain"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Lines on this page (default 200, max 4096)"
      }
    ]
  },
  "get_pie_config": {
    "category": "editor",
    "params": []
  },
  "get_pie_pawn": {
    "category": "editor",
    "params": [
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "0-based player index (default 0)"
      }
    ]
  },
  "get_property": {
    "category": "editor",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": true,
        "description": "Object, asset, class or Blueprint path. A class or Blueprint resolves to its default object",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name; dotted and indexed paths reach component and struct fields"
      }
    ]
  },
  "get_runtime_value": {
    "category": "editor",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name; dotted and indexed paths reach component and struct fields"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "get_runtime_values": {
    "category": "editor",
    "params": [
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Actor or component class name substring; omit to match every actor"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Root every path at the component with this instance name"
      },
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Dotted property or function paths to evaluate per match. A function segment may carry literal arguments, e.g. GetBalance(gold, 2)",
        "items": "string"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "get_sequence_info": {
    "category": "editor",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Level Sequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeSectionDetails",
        "type": "boolean",
        "required": false,
        "description": "Include attach sockets and first-key transform values per track"
      }
    ]
  },
  "get_standalone_status": {
    "category": "editor",
    "params": []
  },
  "get_transaction_history": {
    "category": "editor",
    "params": [
      {
        "name": "maxEntries",
        "type": "number",
        "required": false,
        "description": "Cap on entries returned (default 50)"
      }
    ]
  },
  "get_undo_state": {
    "category": "editor",
    "params": []
  },
  "get_viewport_info": {
    "category": "editor",
    "params": []
  },
  "get_viewport_state": {
    "category": "editor",
    "params": [
      {
        "name": "viewportIndex",
        "type": "number",
        "required": false,
        "description": "Level viewport to act on (default the active one)"
      }
    ]
  },
  "get_world_state": {
    "category": "editor",
    "params": []
  },
  "hit_test_viewport_pixel": {
    "category": "editor",
    "params": [
      {
        "name": "x",
        "type": "number",
        "required": true,
        "description": "Viewport pixel X"
      },
      {
        "name": "y",
        "type": "number",
        "required": true,
        "description": "Viewport pixel Y"
      },
      {
        "name": "width",
        "type": "number",
        "required": false,
        "description": "Viewport width to read x against, when picking from a screenshot of another resolution"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "Viewport height to read y against, when picking from a screenshot of another resolution"
      },
      {
        "name": "maxDistance",
        "type": "number",
        "required": false,
        "description": "Max ray length in cm (default 200000)"
      },
      {
        "name": "ignoreActors",
        "type": "array",
        "required": false,
        "description": "Actor labels to skip",
        "items": "string"
      }
    ]
  },
  "hot_reload": {
    "category": "editor",
    "params": [],
    "contractExempt": "recompiles and reloads C++ modules"
  },
  "invoke_function": {
    "category": "editor",
    "params": [
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "BlueprintCallable or Exec UFUNCTION on the actor, or on component"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "component",
        "type": "string",
        "required": false,
        "description": "Component subobject name to call the function on instead of the actor"
      },
      {
        "name": "args",
        "type": "any",
        "required": false,
        "description": "Function arguments: an object mapping parameter name to value (a struct value takes a JSON object such as {X,Y,Z} or export text), an entry list [{name, value}], or a JSON string of either",
        "forms": [
          "argMap",
          "stringList",
          "argEntryList",
          "string"
        ]
      },
      {
        "name": "actorArgs",
        "type": "object",
        "required": false,
        "description": "UObject* parameter name -> actor label, resolved against live actors in the selected world"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "editor (default) | pie | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      },
      {
        "name": "deferToNextTick",
        "type": "boolean",
        "required": false,
        "description": "Queue the call for the next engine tick, outside the editor script-execution guard, so a replicated UFUNCTION routes normally; return and out values are not reported"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ],
    "contractExempt": "calls a UFUNCTION"
  },
  "invoke_object_function": {
    "category": "editor",
    "params": [
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "UFUNCTION to call; an unknown name lists the available ones"
      },
      {
        "name": "objectPath",
        "type": "string",
        "required": false,
        "description": "Object path of the live instance. Wins over target"
      },
      {
        "name": "target",
        "type": "string",
        "required": false,
        "description": "gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem"
      },
      {
        "name": "subsystemClass",
        "type": "string",
        "required": false,
        "description": "Subsystem class name or /Script path, with target=subsystem"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Player index for target=playercontroller or playerpawn (default 0)"
      },
      {
        "name": "args",
        "type": "any",
        "required": false,
        "description": "Function arguments: an object mapping parameter name to value (a struct value takes a JSON object such as {X,Y,Z} or export text), an entry list [{name, value}], or a JSON string of either",
        "forms": [
          "argMap",
          "stringList",
          "argEntryList",
          "string"
        ]
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      },
      {
        "name": "deferToNextTick",
        "type": "boolean",
        "required": false,
        "description": "Queue the call for the next engine tick, outside the editor script-execution guard, so a replicated UFUNCTION routes normally; return and out values are not reported"
      }
    ],
    "contractExempt": "calls a UFUNCTION"
  },
  "invoke_object_functions": {
    "category": "editor",
    "params": [
      {
        "name": "calls",
        "type": "array",
        "required": true,
        "description": "1 to 64 calls, run in order in one game-thread dispatch; the first failure stops the sequence",
        "items": "object",
        "fields": [
          {
            "name": "functionName",
            "type": "string",
            "required": true,
            "description": "UFUNCTION to call"
          },
          {
            "name": "objectPath",
            "type": "string",
            "required": false,
            "description": "Object path of the live instance. Wins over target"
          },
          {
            "name": "target",
            "type": "string",
            "required": false,
            "description": "gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem"
          },
          {
            "name": "subsystemClass",
            "type": "string",
            "required": false,
            "description": "Subsystem class name or /Script path, with target=subsystem"
          },
          {
            "name": "playerIndex",
            "type": "integer",
            "required": false,
            "description": "Player index for target=playercontroller or playerpawn (default 0)"
          },
          {
            "name": "args",
            "type": "any",
            "required": false,
            "description": "This call's arguments, in any form invoke_object_function takes",
            "forms": [
              "argMap",
              "stringList",
              "argEntryList",
              "string"
            ]
          }
        ]
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ],
    "contractExempt": "calls UFUNCTIONs"
  },
  "invoke_static_function": {
    "category": "editor",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "UBlueprintFunctionLibrary class: a short name or a /Script/Module.Class path"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Static UFUNCTION on the library"
      },
      {
        "name": "args",
        "type": "any",
        "required": false,
        "description": "Function arguments: an object mapping parameter name to value (a struct value takes a JSON object such as {X,Y,Z} or export text), an entry list [{name, value}], or a JSON string of either",
        "forms": [
          "argMap",
          "stringList",
          "argEntryList",
          "string"
        ]
      },
      {
        "name": "actorArgs",
        "type": "object",
        "required": false,
        "description": "UObject* parameter name -> actor label, resolved against live actors in the selected world"
      },
      {
        "name": "worldContextParam",
        "type": "string",
        "required": false,
        "description": "UObject* parameter to fill with the selected world; detected from WorldContext metadata and for WorldContextObject when omitted"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "editor (default) | pie | game | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ],
    "contractExempt": "calls a static UFUNCTION"
  },
  "launch_standalone_game": {
    "category": "editor",
    "params": [
      {
        "name": "mapName",
        "type": "string",
        "required": false,
        "description": "Map to open in the standalone process"
      },
      {
        "name": "channels",
        "type": "array",
        "required": false,
        "description": "Trace channels or a preset, as an array or a comma-separated string. Passing it (or traceFile) adds -trace and -tracefile",
        "items": "string",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "traceFile",
        "type": "string",
        "required": false,
        "description": ".utrace path for the standalone process to write"
      },
      {
        "name": "windowed",
        "type": "boolean",
        "required": false,
        "description": "Run windowed rather than fullscreen (default true)"
      },
      {
        "name": "resX",
        "type": "number",
        "required": false,
        "description": "Window width (default 1280)"
      },
      {
        "name": "resY",
        "type": "number",
        "required": false,
        "description": "Window height (default 720)"
      },
      {
        "name": "extraArgs",
        "type": "string",
        "required": false,
        "description": "Extra command-line arguments, appended verbatim"
      }
    ],
    "contractExempt": "launches a game process"
  },
  "list_crashes": {
    "category": "editor",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Crash folders on this page (default 50, max 500)"
      }
    ]
  },
  "list_dialogs": {
    "category": "editor",
    "params": []
  },
  "list_dirty_packages": {
    "category": "editor",
    "params": []
  },
  "list_function_libraries": {
    "category": "editor",
    "params": [
      {
        "name": "pattern",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the library class name"
      },
      {
        "name": "includeFunctions",
        "type": "boolean",
        "required": false,
        "description": "Include each library's static BlueprintCallable functions (default true)"
      }
    ]
  },
  "list_pie_instances": {
    "category": "editor",
    "params": []
  },
  "list_trace_channels": {
    "category": "editor",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring over channel name and description"
      },
      {
        "name": "enabledOnly",
        "type": "boolean",
        "required": false,
        "description": "Only channels that are currently on (default false)"
      }
    ]
  },
  "open_asset": {
    "category": "editor",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset to open in its editor",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "open_settings": {
    "category": "editor",
    "params": [
      {
        "name": "container",
        "type": "string",
        "required": false,
        "description": "Project (default) | Editor"
      },
      {
        "name": "category",
        "type": "string",
        "required": false,
        "description": "Settings category, e.g. Engine"
      },
      {
        "name": "section",
        "type": "string",
        "required": false,
        "description": "Settings section, e.g. Physics, or a combined Engine.Physics"
      }
    ],
    "contractExempt": "opens a settings viewer"
  },
  "open_tab": {
    "category": "editor",
    "params": [
      {
        "name": "tabId",
        "type": "string",
        "required": true,
        "description": "Registered editor tab id, e.g. ProjectSettings, OutputLog or ContentBrowserTab1"
      }
    ],
    "contractExempt": "opens an editor tab"
  },
  "pause_insights_trace": {
    "category": "editor",
    "params": [
      {
        "name": "paused",
        "type": "boolean",
        "required": false,
        "description": "true pauses the running trace, false resumes it (default true)"
      }
    ],
    "contractExempt": "pauses or resumes the running Insights trace"
  },
  "pie_control": {
    "category": "editor",
    "params": [
      {
        "name": "pieAction",
        "type": "string",
        "required": false,
        "description": "start | stop | status (default status)"
      },
      {
        "name": "waitForAssetRegistry",
        "type": "boolean",
        "required": false,
        "description": "start only: block until the AssetRegistry initial scan completes, since PIE silently no-ops during it on a cold editor (default true)"
      },
      {
        "name": "assetRegistryTimeoutSeconds",
        "type": "number",
        "required": false,
        "description": "start only: how long to wait for that scan (default 180)"
      }
    ],
    "contractExempt": "starts and stops Play In Editor"
  },
  "pie_set_player_view": {
    "category": "editor",
    "params": [
      {
        "name": "pitch",
        "type": "number",
        "required": false,
        "description": "Control-rotation pitch"
      },
      {
        "name": "yaw",
        "type": "number",
        "required": false,
        "description": "Control-rotation yaw"
      },
      {
        "name": "roll",
        "type": "number",
        "required": false,
        "description": "Control-rotation roll"
      }
    ],
    "contractExempt": "rotates the running PIE player's view"
  },
  "play_sequence": {
    "category": "editor",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": false,
        "description": "Level Sequence to open and drive; omit to drive the one already open",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "sequenceAction",
        "type": "string",
        "required": false,
        "description": "play (default) | pause | stop"
      }
    ],
    "contractExempt": "opens a sequence in Sequencer and drives its transport"
  },
  "purge_python_modules": {
    "category": "editor",
    "params": [
      {
        "name": "prefix",
        "type": "string",
        "required": true,
        "description": "Purge sys.modules entries starting with this prefix; must be non-empty"
      }
    ],
    "contractExempt": "purges loaded Python modules"
  },
  "read_bone_transforms": {
    "category": "editor",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "SkeletalMeshComponent to read; omit for the first one"
      },
      {
        "name": "bones",
        "type": "array",
        "required": false,
        "description": "Bone or socket names; omit for every bone up to limit",
        "items": "string"
      },
      {
        "name": "relativeTo",
        "type": "string",
        "required": false,
        "description": "Bone or socket whose live frame every sample is expressed in; supersedes space"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "world (default) | component"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Max bones when bones is omitted (default 200)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "redo": {
    "category": "editor",
    "params": [],
    "contractExempt": "redoes the last undone editor transaction"
  },
  "redraw_viewport": {
    "category": "editor",
    "params": [
      {
        "name": "allViewports",
        "type": "boolean",
        "required": false,
        "description": "Redraw every level viewport rather than one (default false)"
      },
      {
        "name": "invalidateHitProxies",
        "type": "boolean",
        "required": false,
        "description": "Also invalidate hit proxies, needed before a hit test (default true)"
      },
      {
        "name": "viewportIndex",
        "type": "number",
        "required": false,
        "description": "Level viewport to act on (default the active one)"
      }
    ]
  },
  "reload_handlers": {
    "category": "editor",
    "params": [],
    "contractExempt": "reloads the Python bridge handlers from disk"
  },
  "render_sequence_frames": {
    "category": "editor",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "Level Sequence to render",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "startFrame",
        "type": "number",
        "required": false,
        "description": "First display frame to render. Not with startSeconds"
      },
      {
        "name": "endFrame",
        "type": "number",
        "required": false,
        "description": "Last display frame to render, inclusive. Not with endSeconds"
      },
      {
        "name": "startSeconds",
        "type": "number",
        "required": false,
        "description": "Range start in seconds (default the playback range)"
      },
      {
        "name": "endSeconds",
        "type": "number",
        "required": false,
        "description": "Range end in seconds, exclusive"
      },
      {
        "name": "frameStep",
        "type": "number",
        "required": false,
        "description": "Render every Nth frame (default 1)"
      },
      {
        "name": "maxFrames",
        "type": "number",
        "required": false,
        "description": "Refuse a range with more frames than this (default 300, max 5000)"
      },
      {
        "name": "width",
        "type": "number",
        "required": false,
        "description": "Frame width in pixels (default 1280)"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "Frame height in pixels (default 720)"
      },
      {
        "name": "outputDir",
        "type": "string",
        "required": false,
        "description": "Absolute or project-relative directory for the frames (default Saved/SequenceFrames/<sequence>)"
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "Image format; only png"
      },
      {
        "name": "cameraActorLabel",
        "type": "string",
        "required": false,
        "description": "Camera actor to render through instead of the camera cuts"
      },
      {
        "name": "cameraActorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that camera actor. Wins over cameraActorLabel"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Fixed camera location, when no camera actor is given"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "Fixed camera rotation"
      },
      {
        "name": "fov",
        "type": "number",
        "required": false,
        "description": "Fixed camera FOV in degrees (default 90)"
      },
      {
        "name": "fullyLoadTextures",
        "type": "boolean",
        "required": false,
        "description": "Stream textures in before each capture (default true)"
      }
    ],
    "contractExempt": "writes image files"
  },
  "respond_to_dialog": {
    "category": "editor",
    "params": [
      {
        "name": "buttonLabel",
        "type": "string",
        "required": false,
        "description": "Label of the button to press, matched exactly first, then as a substring"
      },
      {
        "name": "buttonIndex",
        "type": "number",
        "required": false,
        "description": "Index of the button to press, in the order list_dialogs reports"
      },
      {
        "name": "dialogAction",
        "type": "string",
        "required": false,
        "description": "escape | close: dismiss the dialog without pressing a button, for a modal offering none that fits"
      },
      {
        "name": "items",
        "type": "array",
        "required": false,
        "description": "The dialog's own tickable rows to set before the button is pressed; indices come from list_dialogs, and rows left out keep their state",
        "items": "object",
        "fields": [
          {
            "name": "index",
            "type": "number",
            "required": true,
            "description": "Row index from list_dialogs"
          },
          {
            "name": "checked",
            "type": "boolean",
            "required": true,
            "description": "Whether the row ends up ticked"
          }
        ]
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "buttonLabel"
          ],
          [
            "buttonIndex"
          ],
          [
            "dialogAction"
          ]
        ]
      }
    ],
    "contractExempt": "presses a button on the active modal dialog"
  },
  "restore_runtime_visibility": {
    "category": "editor",
    "params": [
      {
        "name": "rollbackToken",
        "type": "string",
        "required": true,
        "description": "Token from a non-dry-run set_runtime_visibility, valid for that PIE session only"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "run_automation_tests": {
    "category": "editor",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Substring of the test names to run"
      },
      {
        "name": "maxTests",
        "type": "number",
        "required": false,
        "description": "Cap on tests to run (default 50)"
      },
      {
        "name": "latentTimeoutSeconds",
        "type": "number",
        "required": false,
        "description": "How long one test's latent command queue may take before it is reported abandoned (default 5, max 120)"
      }
    ],
    "contractExempt": "runs automation tests"
  },
  "run_python_file": {
    "category": "editor",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Absolute path to the .py file",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "entryPoint",
        "type": "string",
        "required": false,
        "description": "Function in the file to call after loading it under a run name other than __main__, so its main guard does not fire; its return value comes back as result"
      },
      {
        "name": "args",
        "type": "any",
        "required": false,
        "description": "Positional arguments, as sys.argv[1:] or, with entryPoint, the call's arguments: a list of strings, a JSON array string, or one string. A parameter map is refused",
        "forms": [
          "argMap",
          "stringList",
          "argEntryList",
          "string"
        ]
      },
      {
        "name": "kwargs",
        "type": "object",
        "required": false,
        "description": "Keyword arguments for the entryPoint call"
      },
      {
        "name": "resultVariable",
        "type": "string",
        "required": false,
        "description": "Top-level Python variable to return as result, separate from the log (default result with entryPoint)"
      },
      {
        "name": "captureLog",
        "type": "boolean",
        "required": false,
        "description": "false drops everything the script logged except its errors (default true)"
      },
      {
        "name": "maxLogChars",
        "type": "number",
        "required": false,
        "description": "Keep only the last N characters of logged output"
      }
    ],
    "contractExempt": "runs a Python file"
  },
  "run_stat_command": {
    "category": "editor",
    "params": [
      {
        "name": "command",
        "type": "string",
        "required": false,
        "description": "Full console command; wins over name"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Bare stat name such as unit, fps, game or gpu, prefixed with 'stat ' (default fps)"
      }
    ],
    "contractExempt": "runs a stat console command"
  },
  "save_dirty": {
    "category": "editor",
    "params": [
      {
        "name": "includeMaps",
        "type": "boolean",
        "required": false,
        "description": "Include map packages (default true)"
      },
      {
        "name": "includeContent",
        "type": "boolean",
        "required": false,
        "description": "Include content packages (default true)"
      },
      {
        "name": "commitDeletes",
        "type": "boolean",
        "required": false,
        "description": "Use the editor's dirty-package save, which deletes the packages of deleted World Partition actors and reports written and deleted files (default false)"
      }
    ],
    "contractExempt": "saves every dirty package"
  },
  "scrub_sequence": {
    "category": "editor",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": false,
        "description": "Level Sequence to open and scrub; omit to scrub the one already open",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "seconds",
        "type": "number",
        "required": false,
        "description": "Playhead position in seconds. Pass exactly one of seconds and frame"
      },
      {
        "name": "frame",
        "type": "number",
        "required": false,
        "description": "Playhead position as a frame number, read in timeUnit"
      },
      {
        "name": "timeUnit",
        "type": "string",
        "required": false,
        "description": "How to read frame: display (default, the frame numbers Sequencer shows) | tick (the units get_sequence_info's playbackRange reports)"
      }
    ]
  },
  "search_log": {
    "category": "editor",
    "params": [
      {
        "name": "query",
        "type": "string",
        "required": true,
        "description": "Case-insensitive substring to search the captured log for"
      },
      {
        "name": "maxResults",
        "type": "integer",
        "required": false,
        "description": "Cap on matching lines collected out of the 4096-line ring buffer (default 4096)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Matches on this page (default 100, max 4096)"
      }
    ]
  },
  "set_cvars": {
    "category": "editor",
    "params": [
      {
        "name": "cvars",
        "type": "array",
        "required": true,
        "description": "Console variables to set, as [{name, value}] or a {name: value} object",
        "items": "object",
        "orTypes": [
          "object"
        ],
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Console variable name"
          },
          {
            "name": "value",
            "type": "any",
            "required": true,
            "description": "Value to set; numbers and booleans are written as text"
          }
        ]
      }
    ],
    "contractExempt": "writes console variables"
  },
  "set_dialog_policy": {
    "category": "editor",
    "params": [
      {
        "name": "pattern",
        "type": "string",
        "required": true,
        "description": "Substring matched case-insensitively against the dialog title and message"
      },
      {
        "name": "response",
        "type": "string",
        "required": false,
        "description": "yes | no | ok | cancel | retry | continue | yesall | noall. On a Slate modal it presses whichever button carries that meaning; an unknown keyword is refused"
      },
      {
        "name": "buttonLabel",
        "type": "string",
        "required": false,
        "description": "Button a matched dialog gets pressed for it, matched exactly first, then as a substring. Reaches buttons no response names, such as Don't Save"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "response"
          ],
          [
            "buttonLabel"
          ]
        ]
      }
    ],
    "contractExempt": "arms a policy that answers dialogs unattended"
  },
  "set_game_view": {
    "category": "editor",
    "params": [
      {
        "name": "enabled",
        "type": "boolean",
        "required": false,
        "description": "Hide editor-only overlays (default true); false shows them again"
      },
      {
        "name": "viewportIndex",
        "type": "number",
        "required": false,
        "description": "Level viewport to act on (default the active one)"
      }
    ],
    "contractExempt": "toggles game view on a live level viewport"
  },
  "set_movement_mode": {
    "category": "editor",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "none | walking | navwalking | falling | swimming | flying | custom"
      },
      {
        "name": "customMode",
        "type": "integer",
        "required": false,
        "description": "0-255, only with mode=custom"
      },
      {
        "name": "velocity",
        "type": "vec3",
        "required": false,
        "description": "Velocity written to the CharacterMovementComponent"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "set_object_property": {
    "category": "editor",
    "params": [
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name; dotted and indexed paths reach component and struct fields"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New value as structured JSON, the form get_property returns under value"
      },
      {
        "name": "objectPath",
        "type": "string",
        "required": false,
        "description": "Object path of the live instance. Wins over target"
      },
      {
        "name": "target",
        "type": "string",
        "required": false,
        "description": "gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem"
      },
      {
        "name": "subsystemClass",
        "type": "string",
        "required": false,
        "description": "Subsystem class name or /Script path, with target=subsystem"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Player index for target=playercontroller or playerpawn (default 0)"
      },
      {
        "name": "postEditChange",
        "type": "boolean",
        "required": false,
        "description": "Fire PostEditChangeProperty after the write (default false)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "set_pie_time_scale": {
    "category": "editor",
    "params": [
      {
        "name": "factor",
        "type": "number",
        "required": true,
        "description": "Time-scale factor, greater than 0 (e.g. 500)"
      }
    ]
  },
  "set_property": {
    "category": "editor",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": true,
        "description": "Object, asset, class or Blueprint path. A class or Blueprint resolves to its default object",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name; dotted and indexed paths reach component and struct fields"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New value as structured JSON, the form get_property returns under value"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the package to disk after the write (default true; false leaves it dirty)"
      }
    ]
  },
  "set_realtime": {
    "category": "editor",
    "params": [
      {
        "name": "enabled",
        "type": "boolean",
        "required": false,
        "description": "Realtime update on every level viewport (default true)"
      }
    ],
    "contractExempt": "toggles realtime on every level viewport"
  },
  "set_runtime_visibility": {
    "category": "editor",
    "params": [
      {
        "name": "hidden",
        "type": "boolean",
        "required": true,
        "description": "true hides the target, false shows it"
      },
      {
        "name": "actorLabels",
        "type": "array",
        "required": false,
        "description": "Explicit actor labels; a label matching several actors is refused",
        "items": "string"
      },
      {
        "name": "actorPaths",
        "type": "array",
        "required": false,
        "description": "Explicit actor object paths, the unambiguous selector",
        "items": "string"
      },
      {
        "name": "actorClass",
        "type": "string",
        "required": false,
        "description": "Every actor of this class in the PIE world, bounded by maxTargets"
      },
      {
        "name": "componentNames",
        "type": "array",
        "required": false,
        "description": "Only SceneComponents with these names; implies affectComponents",
        "items": "string"
      },
      {
        "name": "componentClasses",
        "type": "array",
        "required": false,
        "description": "Only SceneComponents of these classes; implies affectComponents",
        "items": "string"
      },
      {
        "name": "affectActor",
        "type": "boolean",
        "required": false,
        "description": "Hide or show the actor itself (default true only without a component filter)"
      },
      {
        "name": "affectComponents",
        "type": "boolean",
        "required": false,
        "description": "Hide or show matched components (default true with a component filter)"
      },
      {
        "name": "propagateToChildren",
        "type": "boolean",
        "required": false,
        "description": "Also take each matched component's descendants (default true)"
      },
      {
        "name": "matchSubclasses",
        "type": "boolean",
        "required": false,
        "description": "Match subclasses of actorClass and componentClasses (default true)"
      },
      {
        "name": "maxTargets",
        "type": "integer",
        "required": false,
        "description": "Upper bound on resolved actor and component targets; a larger set is refused rather than truncated"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report the targets without touching them (default TRUE; pass false to apply)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabels"
          ],
          [
            "actorPaths"
          ],
          [
            "actorClass"
          ]
        ]
      }
    ],
    "contractExempt": "hides and shows live PIE actors"
  },
  "set_scalability": {
    "category": "editor",
    "params": [
      {
        "name": "level",
        "type": "string",
        "required": false,
        "description": "Low | Medium | High | Epic | Cinematic (default Epic)"
      }
    ]
  },
  "set_sequence_keyframes": {
    "category": "editor",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "Level Sequence asset path",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "trackType",
        "type": "string",
        "required": true,
        "description": "Transform | Float | SkeletalAnimation | CameraCut | Audio | Event | Fade"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": false,
        "description": "Target section index (default 0)"
      },
      {
        "name": "channel",
        "type": "string",
        "required": true,
        "description": "Location.X/Y/Z or Rotation.X/Y/Z (also x/y/z, yaw/pitch/roll) on a Transform track; the float channel on Fade or Float"
      },
      {
        "name": "keyframes",
        "type": "array",
        "required": true,
        "description": "Keys to add, as [{seconds, value}]",
        "items": "object"
      },
      {
        "name": "interpolation",
        "type": "string",
        "required": false,
        "description": "cubic (default) | linear"
      }
    ]
  },
  "set_sequence_playback_range": {
    "category": "editor",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "Level Sequence asset path",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "startSeconds",
        "type": "number",
        "required": true,
        "description": "Range start in seconds"
      },
      {
        "name": "endSeconds",
        "type": "number",
        "required": true,
        "description": "Range end in seconds"
      }
    ]
  },
  "set_trace_channels": {
    "category": "editor",
    "params": [
      {
        "name": "enable",
        "type": "array",
        "required": false,
        "description": "Channels to turn on, as an array or a comma-separated string",
        "items": "string",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "disable",
        "type": "array",
        "required": false,
        "description": "Channels to turn off, as an array or a comma-separated string",
        "items": "string",
        "orTypes": [
          "string"
        ]
      }
    ],
    "contractExempt": "switches trace channels on and off"
  },
  "set_view_mode": {
    "category": "editor",
    "params": [
      {
        "name": "viewMode",
        "type": "string",
        "required": true,
        "description": "Lit | Unlit | Wireframe | LightingOnly | DetailLighting | ShaderComplexity | ... get_viewport_state lists what this build supports"
      },
      {
        "name": "viewportIndex",
        "type": "number",
        "required": false,
        "description": "Level viewport to act on (default the active one)"
      }
    ]
  },
  "set_viewport_camera": {
    "category": "editor",
    "params": [
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Camera location"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "Camera rotation"
      },
      {
        "name": "projection",
        "type": "string",
        "required": false,
        "description": "perspective | top | bottom | left | right | front | back | orthoFreelook. Switched before the pose is applied"
      },
      {
        "name": "viewportType",
        "type": "string",
        "required": false,
        "description": "Perspective | Top | Bottom | Left | Right | Front | Back | OrthoFreelook"
      },
      {
        "name": "orthoZoom",
        "type": "number",
        "required": false,
        "description": "Orthographic zoom, within the engine's own limits"
      }
    ]
  },
  "set_viewport_exposure": {
    "category": "editor",
    "params": [
      {
        "name": "ev100",
        "type": "number",
        "required": false,
        "description": "Fixed EV100 to pin the viewport to; implies fixed exposure"
      },
      {
        "name": "fixed",
        "type": "boolean",
        "required": false,
        "description": "Use a fixed exposure rather than eye adaptation"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "fixed | auto"
      },
      {
        "name": "viewportIndex",
        "type": "number",
        "required": false,
        "description": "Level viewport to act on (default the active one)"
      }
    ]
  },
  "set_viewport_view": {
    "category": "editor",
    "params": [
      {
        "name": "fov",
        "type": "number",
        "required": false,
        "description": "Field of view in degrees, greater than 0 and less than 180"
      },
      {
        "name": "nearClip",
        "type": "number",
        "required": false,
        "description": "Near clip plane; negative clears the override"
      },
      {
        "name": "farClip",
        "type": "number",
        "required": false,
        "description": "Far clip plane override"
      },
      {
        "name": "viewportType",
        "type": "string",
        "required": false,
        "description": "Perspective | Top | Bottom | Left | Right | Front | Back | OrthoFreelook"
      },
      {
        "name": "cameraSpeed",
        "type": "number",
        "required": false,
        "description": "Viewport camera speed, greater than 0"
      },
      {
        "name": "viewportIndex",
        "type": "number",
        "required": false,
        "description": "Level viewport to act on (default the active one)"
      }
    ]
  },
  "stage_game_input": {
    "category": "editor",
    "params": [
      {
        "name": "inputMode",
        "type": "string",
        "required": false,
        "description": "gameOnly (default) | gameAndUI | uiOnly"
      },
      {
        "name": "showMouseCursor",
        "type": "boolean",
        "required": false,
        "description": "Show the mouse cursor (default false for gameOnly, true otherwise)"
      }
    ],
    "contractExempt": "sets the running PIE player's input mode"
  },
  "start_insights_trace": {
    "category": "editor",
    "params": [
      {
        "name": "channels",
        "type": "array",
        "required": false,
        "description": "Trace channels or a preset, as an array or a comma-separated string (default 'default'). list_trace_channels lists what this build registers",
        "items": "string",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "traceTarget",
        "type": "string",
        "required": false,
        "description": "file (default, writes a .utrace) | network (streams to a trace server) | none (memory only)"
      },
      {
        "name": "file",
        "type": "string",
        "required": false,
        "description": ".utrace path to write, absolute or relative (default a timestamped file under <Project>/Saved/Profiling)"
      },
      {
        "name": "host",
        "type": "string",
        "required": false,
        "description": "Trace server for traceTarget=network (default 127.0.0.1)"
      },
      {
        "name": "truncate",
        "type": "boolean",
        "required": false,
        "description": "Overwrite the target file if it exists (default true)"
      },
      {
        "name": "excludeTail",
        "type": "boolean",
        "required": false,
        "description": "Drop events buffered before the trace started (default false)"
      }
    ],
    "contractExempt": "starts an Insights trace"
  },
  "stop_insights_trace": {
    "category": "editor",
    "params": [],
    "contractExempt": "stops the running Insights trace"
  },
  "stop_standalone_game": {
    "category": "editor",
    "params": [],
    "contractExempt": "terminates the standalone game process"
  },
  "teleport_runtime_actor": {
    "category": "editor",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label. Editor labels are not unique, so a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector. Wins over actorLabel"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Destination; omit to keep the current location"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "Destination rotation; omit to keep the current one"
      },
      {
        "name": "stopMovement",
        "type": "boolean",
        "required": false,
        "description": "Stop the movement component so the move is not undone (default true)"
      },
      {
        "name": "sweep",
        "type": "boolean",
        "required": false,
        "description": "Collide on the way (default false)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: editor | pie | auto. Each action names its own default"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"
      }
    ]
  },
  "trigger_hitch": {
    "category": "editor",
    "params": [
      {
        "name": "hitchMilliseconds",
        "type": "number",
        "required": false,
        "description": "How long to stall the game thread (default 250, max 5000)"
      },
      {
        "name": "bookmark",
        "type": "boolean",
        "required": false,
        "description": "Also drop a trace bookmark at the stall (default true)"
      }
    ],
    "contractExempt": "stalls the game thread"
  },
  "undo": {
    "category": "editor",
    "params": [],
    "contractExempt": "undoes the last editor transaction"
  },
  "undo_redo_steps": {
    "category": "editor",
    "params": [
      {
        "name": "steps",
        "type": "integer",
        "required": false,
        "description": "How many steps to apply (default 1)"
      },
      {
        "name": "direction",
        "type": "string",
        "required": false,
        "description": "undo (default) | redo"
      }
    ]
  },
  "validate_assets": {
    "category": "editor",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Package path validated recursively (default /Game/). Not with assetPath or assetPaths"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "One exact package or object path to validate"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "Exact package or object paths to validate",
        "items": "string"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_sequence_section: "Params: sequencePath (or assetPath, or path), trackType, actorLabel?, actorPath?, startSeconds?, endSeconds?, cameraActorLabel?, cameraActorPath?",
  add_sequence_track: "Params: assetPath (or path), trackType, actorLabel?, actorPath?",
  add_trace_bookmark: "Params: bookmarkName",
  begin_editor_transaction: "Params: description? (or label)",
  begin_profile_region: "Params: regionName, regionCategory?",
  build_all: "Params: none",
  build_geometry: "Params: none",
  build_hlod: "Params: none",
  build_lighting: "Params: quality?",
  cancel_editor_transaction: "Params: index?",
  capture_scene_png: "Params: outputPath (or filename), location?, rotation?, focusActorLabel?, focusActorPath?, focusDirection?, focusMargin?, world?, pieInstance?, width?, height?, fov?, fullyLoadTextures?",
  capture_screenshot: "Params: filename (or outputPath), target?, pieInstance?, worldPath?",
  check_for_crashes: "Params: none",
  clear_dialog_policy: "Params: pattern?",
  close_sequence: "Params: none",
  configure_pie: "Params: numClients?, netMode?, runUnderOneProcess?, launchSeparateServer?, newWindowWidth?, newWindowHeight?",
  cook_content: "Params: platform?",
  create_level_sequence: "Params: name, packagePath?, onConflict?",
  create_new_level: "Params: levelPath, templateLevel?, onConflict?",
  describe_object: "Params: objectPath (or path, or assetPath), includeProperties?, includeValues?, propertyNames?",
  end_editor_transaction: "Params: none",
  end_profile_region: "Params: regionName",
  execute_command: "Params: command",
  find_object: "Params: at least one of objectPath/className/nameContains, outerPath?, exactClass?, includeDefaults?, world?, pieInstance?, cursor?, limit?",
  focus_viewport_on_actor: "Params: actorLabel?, actorPath?",
  get_build_status: "Params: none",
  get_crash_info: "Params: crashFolder",
  get_cvars: "Params: name?, names?, pattern?, limit?",
  get_dialog_policy: "Params: none",
  get_editor_performance_stats: "Params: none",
  get_frame_timing: "Params: cpuGpuMarginPercent?",
  get_insights_trace_status: "Params: none",
  get_message_log: "Params: logName?, maxLines?, severity?",
  get_object_properties: "Params: objectPath?, target?, subsystemClass?, playerIndex?, propertyNames?, world?, pieInstance?, limit?, maxValueLength?",
  get_open_asset_editors: "Params: none",
  get_output_log: "Params: maxLines?, filter?, category?, cursor?, limit?",
  get_pie_config: "Params: none",
  get_pie_pawn: "Params: playerIndex?",
  get_property: "Params: objectPath (or path, or assetPath), propertyName",
  get_runtime_value: "Params: actorLabel?, actorPath?, propertyName, world?, pieInstance?",
  get_runtime_values: "Params: classFilter?, componentName?, paths, world?, pieInstance?",
  get_sequence_info: "Params: assetPath (or path), includeSectionDetails?",
  get_standalone_status: "Params: none",
  get_transaction_history: "Params: maxEntries?",
  get_undo_state: "Params: none",
  get_viewport_info: "Params: none",
  get_viewport_state: "Params: viewportIndex?",
  get_world_state: "Params: none",
  hit_test_viewport_pixel: "Params: x, y, width?, height?, maxDistance?, ignoreActors?",
  hot_reload: "Params: none",
  invoke_function: "Params: functionName, at least one of actorLabel/actorPath, component?, args?, actorArgs?, world?, pieInstance?, deferToNextTick?",
  invoke_object_function: "Params: functionName, objectPath?, target?, subsystemClass?, playerIndex?, args?, world?, pieInstance?, deferToNextTick?",
  invoke_object_functions: "Params: calls, world?, pieInstance?",
  invoke_static_function: "Params: className, functionName, args?, actorArgs?, worldContextParam?, world?, pieInstance?",
  launch_standalone_game: "Params: mapName?, channels?, traceFile?, windowed?, resX?, resY?, extraArgs?",
  list_crashes: "Params: cursor?, limit?",
  list_dialogs: "Params: none",
  list_dirty_packages: "Params: none",
  list_function_libraries: "Params: pattern?, includeFunctions?",
  list_pie_instances: "Params: none",
  list_trace_channels: "Params: filter?, enabledOnly?",
  open_asset: "Params: assetPath (or path)",
  open_settings: "Params: container?, category?, section?",
  open_tab: "Params: tabId",
  pause_insights_trace: "Params: paused?",
  pie_control: "Params: pieAction?, waitForAssetRegistry?, assetRegistryTimeoutSeconds?",
  pie_set_player_view: "Params: pitch?, yaw?, roll?",
  play_sequence: "Params: sequencePath? (or assetPath, or path), sequenceAction?",
  purge_python_modules: "Params: prefix",
  read_bone_transforms: "Params: actorLabel?, actorPath?, componentName?, bones?, relativeTo?, space?, limit?, world?, pieInstance?",
  redo: "Params: none",
  redraw_viewport: "Params: allViewports?, invalidateHitProxies?, viewportIndex?",
  reload_handlers: "Params: none",
  render_sequence_frames: "Params: sequencePath (or assetPath, or path), startFrame?, endFrame?, startSeconds?, endSeconds?, frameStep?, maxFrames?, width?, height?, outputDir?, format?, cameraActorLabel?, cameraActorPath?, location?, rotation?, fov?, fullyLoadTextures?",
  respond_to_dialog: "Params: at least one of buttonLabel/buttonIndex/dialogAction, items?",
  restore_runtime_visibility: "Params: rollbackToken, world?, pieInstance?",
  run_automation_tests: "Params: filter?, maxTests?, latentTimeoutSeconds?",
  run_python_file: "Params: filePath (or path), entryPoint?, args?, kwargs?, resultVariable?, captureLog?, maxLogChars?",
  run_stat_command: "Params: command?, name?",
  save_dirty: "Params: includeMaps?, includeContent?, commitDeletes?",
  scrub_sequence: "Params: sequencePath? (or assetPath, or path), seconds?, frame?, timeUnit?",
  search_log: "Params: query, maxResults?, cursor?, limit?",
  set_cvars: "Params: cvars",
  set_dialog_policy: "Params: pattern, at least one of response/buttonLabel",
  set_game_view: "Params: enabled?, viewportIndex?",
  set_movement_mode: "Params: actorLabel?, actorPath?, mode?, customMode?, velocity?, world?, pieInstance?",
  set_object_property: "Params: propertyName, value, objectPath?, target?, subsystemClass?, playerIndex?, postEditChange?, world?, pieInstance?",
  set_pie_time_scale: "Params: factor",
  set_property: "Params: objectPath (or path, or assetPath), propertyName, value, save?",
  set_realtime: "Params: enabled?",
  set_runtime_visibility: "Params: hidden, actorLabels OR actorPaths OR actorClass, componentNames?, componentClasses?, affectActor?, affectComponents?, propagateToChildren?, matchSubclasses?, maxTargets?, dryRun?, world?, pieInstance?",
  set_scalability: "Params: level?",
  set_sequence_keyframes: "Params: sequencePath (or assetPath, or path), trackType, actorLabel?, actorPath?, sectionIndex?, channel, keyframes, interpolation?",
  set_sequence_playback_range: "Params: sequencePath (or assetPath, or path), startSeconds, endSeconds",
  set_trace_channels: "Params: enable?, disable?",
  set_view_mode: "Params: viewMode, viewportIndex?",
  set_viewport_camera: "Params: location?, rotation?, projection?, viewportType?, orthoZoom?",
  set_viewport_exposure: "Params: ev100?, fixed?, mode?, viewportIndex?",
  set_viewport_view: "Params: fov?, nearClip?, farClip?, viewportType?, cameraSpeed?, viewportIndex?",
  stage_game_input: "Params: inputMode?, showMouseCursor?",
  start_insights_trace: "Params: channels?, traceTarget?, file?, host?, truncate?, excludeTail?",
  stop_insights_trace: "Params: none",
  stop_standalone_game: "Params: none",
  teleport_runtime_actor: "Params: actorLabel?, actorPath?, location?, rotation?, stopMovement?, sweep?, world?, pieInstance?",
  trigger_hitch: "Params: hitchMilliseconds?, bookmark?",
  undo: "Params: none",
  undo_redo_steps: "Params: steps?, direction?",
  validate_assets: "Params: directory?, assetPath?, assetPaths?",
};

/** Every key the spec'd editor handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorArgs: z.record(z.unknown()).optional().describe("UObject* parameter name -> actor label, resolved against live actors in the selected world"),
  actorClass: z.string().optional().describe("Every actor of this class in the PIE world, bounded by maxTargets"),
  actorLabel: z.string().optional().describe("Actor label. Editor labels are not unique, so a label naming several actors is refused"),
  actorLabels: z.array(z.string()).optional().describe("Explicit actor labels; a label matching several actors is refused"),
  actorPath: z.string().optional().describe("Full actor object path, the unambiguous selector. Wins over actorLabel"),
  actorPaths: z.array(z.string()).optional().describe("Explicit actor object paths, the unambiguous selector"),
  affectActor: z.boolean().optional().describe("Hide or show the actor itself (default true only without a component filter)"),
  affectComponents: z.boolean().optional().describe("Hide or show matched components (default true with a component filter)"),
  allViewports: z.boolean().optional().describe("Redraw every level viewport rather than one (default false)"),
  args: z.union([z.record(z.string(), z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.string(), z.number(), z.boolean(), z.null()]))]))])), z.array(z.string()), z.array(z.object({ name: z.string(), value: z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.string(), z.number(), z.boolean(), z.null()]))]))]).optional() })), z.string()], { errorMap: () => ({ message: "args must be an object mapping parameter name to value (e.g. {\"bEnabled\": true}), an array of positional strings, an entry list ([{\"name\": \"bEnabled\", \"value\": true}]), or a string" }) }).optional().describe("Function arguments: an object mapping parameter name to value (a struct value takes a JSON object such as {X,Y,Z} or export text), an entry list [{name, value}], or a JSON string of either (invoke_function, invoke_object_function, invoke_static_function). Positional arguments, as sys.argv[1:] or, with entryPoint, the call's arguments: a list of strings, a JSON array string, or one string. A parameter map is refused (run_python_file)"),
  assetPath: z.string().optional().describe("Alias for sequencePath (add_sequence_section, play_sequence, render_sequence_frames, scrub_sequence, set_sequence_keyframes, set_sequence_playback_range). Level Sequence asset path (add_sequence_track, get_sequence_info). Alias for objectPath (describe_object, get_property, set_property). Asset to open in its editor (open_asset). One exact package or object path to validate (validate_assets)"),
  assetPaths: z.array(z.string()).optional().describe("Exact package or object paths to validate"),
  assetRegistryTimeoutSeconds: z.number().optional().describe("start only: how long to wait for that scan (default 180)"),
  bones: z.array(z.string()).optional().describe("Bone or socket names; omit for every bone up to limit"),
  bookmark: z.boolean().optional().describe("Also drop a trace bookmark at the stall (default true)"),
  bookmarkName: z.string().optional().describe("Label for the timeline marker"),
  buttonIndex: z.number().optional().describe("Index of the button to press, in the order list_dialogs reports"),
  buttonLabel: z.string().optional().describe("Label of the button to press, matched exactly first, then as a substring (respond_to_dialog). Button a matched dialog gets pressed for it, matched exactly first, then as a substring. Reaches buttons no response names, such as Don't Save (set_dialog_policy)"),
  calls: z.array(z.object({ functionName: z.string().describe("UFUNCTION to call"), objectPath: z.string().optional().describe("Object path of the live instance. Wins over target"), target: z.string().optional().describe("gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem"), subsystemClass: z.string().optional().describe("Subsystem class name or /Script path, with target=subsystem"), playerIndex: z.number().int().optional().describe("Player index for target=playercontroller or playerpawn (default 0)"), args: z.union([z.record(z.string(), z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.string(), z.number(), z.boolean(), z.null()]))]))])), z.array(z.string()), z.array(z.object({ name: z.string(), value: z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.union([z.string(), z.number(), z.boolean(), z.null()]), z.object({}).passthrough(), z.array(z.union([z.string(), z.number(), z.boolean(), z.null()]))]))]).optional() })), z.string()], { errorMap: () => ({ message: "args must be an object mapping parameter name to value (e.g. {\"bEnabled\": true}), an array of positional strings, an entry list ([{\"name\": \"bEnabled\", \"value\": true}]), or a string" }) }).optional().describe("This call's arguments, in any form invoke_object_function takes") })).optional().describe("1 to 64 calls, run in order in one game-thread dispatch; the first failure stops the sequence"),
  cameraActorLabel: z.string().optional().describe("Camera actor to bind a CameraCut section to (add_sequence_section). Camera actor to render through instead of the camera cuts (render_sequence_frames)"),
  cameraActorPath: z.string().optional().describe("Full object path of the camera actor. Wins over cameraActorLabel (add_sequence_section). Full object path of that camera actor. Wins over cameraActorLabel (render_sequence_frames)"),
  cameraSpeed: z.number().optional().describe("Viewport camera speed, greater than 0"),
  captureLog: z.boolean().optional().describe("false drops everything the script logged except its errors (default true)"),
  category: z.string().optional().describe("Case-insensitive substring the log category must contain (get_output_log). Settings category, e.g. Engine (open_settings)"),
  channel: z.string().optional().describe("Location.X/Y/Z or Rotation.X/Y/Z (also x/y/z, yaw/pitch/roll) on a Transform track; the float channel on Fade or Float"),
  channels: z.union([z.array(z.string()), z.string()]).optional().describe("Trace channels or a preset, as an array or a comma-separated string. Passing it (or traceFile) adds -trace and -tracefile (launch_standalone_game). Trace channels or a preset, as an array or a comma-separated string (default 'default'). list_trace_channels lists what this build registers (start_insights_trace)"),
  classFilter: z.string().optional().describe("Actor or component class name substring; omit to match every actor"),
  className: z.string().optional().describe("Class to search for: a short name, a /Script path, a generated class name (WBP_Hud_C) or a Blueprint asset path (find_object). UBlueprintFunctionLibrary class: a short name or a /Script/Module.Class path (invoke_static_function)"),
  command: z.string().optional().describe("Console command to run in the editor world (execute_command). Full console command; wins over name (run_stat_command)"),
  commitDeletes: z.boolean().optional().describe("Use the editor's dirty-package save, which deletes the packages of deleted World Partition actors and reports written and deleted files (default false)"),
  component: z.string().optional().describe("Component subobject name to call the function on instead of the actor"),
  componentClasses: z.array(z.string()).optional().describe("Only SceneComponents of these classes; implies affectComponents"),
  componentName: z.string().optional().describe("Root every path at the component with this instance name (get_runtime_values). SkeletalMeshComponent to read; omit for the first one (read_bone_transforms)"),
  componentNames: z.array(z.string()).optional().describe("Only SceneComponents with these names; implies affectComponents"),
  container: z.string().optional().describe("Project (default) | Editor"),
  cpuGpuMarginPercent: z.number().optional().describe("How far ahead one side must be before the frame is called bound by it (default 10)"),
  crashFolder: z.string().optional().describe("Crash folder name, as list_crashes reports it"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  customMode: z.number().int().optional().describe("0-255, only with mode=custom"),
  cvars: z.union([z.array(z.object({ name: z.string().describe("Console variable name"), value: z.unknown().describe("Value to set; numbers and booleans are written as text") })), z.record(z.unknown())]).optional().describe("Console variables to set, as [{name, value}] or a {name: value} object"),
  deferToNextTick: z.boolean().optional().describe("Queue the call for the next engine tick, outside the editor script-execution guard, so a replicated UFUNCTION routes normally; return and out values are not reported"),
  description: z.string().optional().describe("Undo-stack label for the transaction (default MCP Edit)"),
  dialogAction: z.string().optional().describe("escape | close: dismiss the dialog without pressing a button, for a modal offering none that fits"),
  direction: z.string().optional().describe("undo (default) | redo"),
  directory: z.string().optional().describe("Package path validated recursively (default /Game/). Not with assetPath or assetPaths"),
  disable: z.union([z.array(z.string()), z.string()]).optional().describe("Channels to turn off, as an array or a comma-separated string"),
  dryRun: z.boolean().optional().describe("Report the targets without touching them (default TRUE; pass false to apply)"),
  enable: z.union([z.array(z.string()), z.string()]).optional().describe("Channels to turn on, as an array or a comma-separated string"),
  enabled: z.boolean().optional().describe("Hide editor-only overlays (default true); false shows them again (set_game_view). Realtime update on every level viewport (default true) (set_realtime)"),
  enabledOnly: z.boolean().optional().describe("Only channels that are currently on (default false)"),
  endFrame: z.number().optional().describe("Last display frame to render, inclusive. Not with endSeconds"),
  endSeconds: z.number().optional().describe("Section end in seconds (default one second after the start) (add_sequence_section). Range end in seconds, exclusive (render_sequence_frames). Range end in seconds (set_sequence_playback_range)"),
  entryPoint: z.string().optional().describe("Function in the file to call after loading it under a run name other than __main__, so its main guard does not fire; its return value comes back as result"),
  ev100: z.number().optional().describe("Fixed EV100 to pin the viewport to; implies fixed exposure"),
  exactClass: z.boolean().optional().describe("Match className exactly instead of including subclasses (default false)"),
  excludeTail: z.boolean().optional().describe("Drop events buffered before the trace started (default false)"),
  extraArgs: z.string().optional().describe("Extra command-line arguments, appended verbatim"),
  factor: z.number().optional().describe("Time-scale factor, greater than 0 (e.g. 500)"),
  farClip: z.number().optional().describe("Far clip plane override"),
  file: z.string().optional().describe(".utrace path to write, absolute or relative (default a timestamped file under <Project>/Saved/Profiling)"),
  filename: z.string().optional().describe("Alias for outputPath (capture_scene_png). Image path to write; .png is appended without an image extension (capture_screenshot)"),
  filePath: z.string().optional().describe("Absolute path to the .py file"),
  filter: z.string().optional().describe("Case-insensitive substring the message must contain (get_output_log). Case-insensitive substring over channel name and description (list_trace_channels). Substring of the test names to run (run_automation_tests)"),
  fixed: z.boolean().optional().describe("Use a fixed exposure rather than eye adaptation"),
  focusActorLabel: z.string().optional().describe("Frame the camera on this actor's bounds"),
  focusActorPath: z.string().optional().describe("Full object path of the actor to frame. Wins over focusActorLabel"),
  focusDirection: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Framing direction from the focus actor (default front and above)"),
  focusMargin: z.number().optional().describe("Positive bounds fill margin; higher pulls back (default 1.5)"),
  format: z.string().optional().describe("Image format; only png"),
  fov: z.number().optional().describe("Capture FOV in degrees, greater than 0 and less than 180 (default 90) (capture_scene_png). Fixed camera FOV in degrees (default 90) (render_sequence_frames). Field of view in degrees, greater than 0 and less than 180 (set_viewport_view)"),
  frame: z.number().optional().describe("Playhead position as a frame number, read in timeUnit"),
  frameStep: z.number().optional().describe("Render every Nth frame (default 1)"),
  fullyLoadTextures: z.boolean().optional().describe("Stream textures in and flush the render thread before the capture (default true) (capture_scene_png). Stream textures in before each capture (default true) (render_sequence_frames)"),
  functionName: z.string().optional().describe("BlueprintCallable or Exec UFUNCTION on the actor, or on component (invoke_function). UFUNCTION to call; an unknown name lists the available ones (invoke_object_function). Static UFUNCTION on the library (invoke_static_function)"),
  height: z.number().optional().describe("Capture height in pixels (default 720) (capture_scene_png). Viewport height to read y against, when picking from a screenshot of another resolution (hit_test_viewport_pixel). Frame height in pixels (default 720) (render_sequence_frames)"),
  hidden: z.boolean().optional().describe("true hides the target, false shows it"),
  hitchMilliseconds: z.number().optional().describe("How long to stall the game thread (default 250, max 5000)"),
  host: z.string().optional().describe("Trace server for traceTarget=network (default 127.0.0.1)"),
  ignoreActors: z.array(z.string()).optional().describe("Actor labels to skip"),
  includeContent: z.boolean().optional().describe("Include content packages (default true)"),
  includeDefaults: z.boolean().optional().describe("Include class default objects and archetypes (default false)"),
  includeFunctions: z.boolean().optional().describe("Include each library's static BlueprintCallable functions (default true)"),
  includeMaps: z.boolean().optional().describe("Include map packages (default true)"),
  includeProperties: z.boolean().optional().describe("Include reflected property metadata (default true)"),
  includeSectionDetails: z.boolean().optional().describe("Include attach sockets and first-key transform values per track"),
  includeValues: z.boolean().optional().describe("Include current property values (default false)"),
  index: z.number().optional().describe("Which open transaction to cancel (default 0)"),
  inputMode: z.string().optional().describe("gameOnly (default) | gameAndUI | uiOnly"),
  interpolation: z.string().optional().describe("cubic (default) | linear"),
  invalidateHitProxies: z.boolean().optional().describe("Also invalidate hit proxies, needed before a hit test (default true)"),
  items: z.array(z.object({ index: z.number().describe("Row index from list_dialogs"), checked: z.boolean().describe("Whether the row ends up ticked") })).optional().describe("The dialog's own tickable rows to set before the button is pressed; indices come from list_dialogs, and rows left out keep their state"),
  keyframes: z.array(z.record(z.unknown())).optional().describe("Keys to add, as [{seconds, value}]"),
  kwargs: z.record(z.unknown()).optional().describe("Keyword arguments for the entryPoint call"),
  label: z.string().optional().describe("Alias for description"),
  latentTimeoutSeconds: z.number().optional().describe("How long one test's latent command queue may take before it is reported abandoned (default 5, max 120)"),
  launchSeparateServer: z.boolean().optional().describe("Launch a separate dedicated server"),
  level: z.string().optional().describe("Low | Medium | High | Epic | Cinematic (default Epic)"),
  levelPath: z.string().optional().describe("Long package path of the new level, e.g. /Game/Maps/MyLevel. Validated before the engine is asked"),
  limit: z.number().optional().describe("Objects on this page (default 50, max 1000) (find_object). Max rows for a pattern search (default 100, max 1000) (get_cvars). Max properties returned (default 200) (get_object_properties). Lines on this page (default 200, max 4096) (get_output_log). Crash folders on this page (default 50, max 500) (list_crashes). Max bones when bones is omitted (default 200) (read_bone_transforms). Matches on this page (default 100, max 4096) (search_log)"),
  location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Camera location (capture_scene_png, set_viewport_camera). Fixed camera location, when no camera actor is given (render_sequence_frames). Destination; omit to keep the current location (teleport_runtime_actor)"),
  logName: z.string().optional().describe("Listing to read (MapCheck, AssetCheck, PIE, LoadErrors...); omit to list the registered ones"),
  mapName: z.string().optional().describe("Map to open in the standalone process"),
  matchSubclasses: z.boolean().optional().describe("Match subclasses of actorClass and componentClasses (default true)"),
  maxDistance: z.number().optional().describe("Max ray length in cm (default 200000)"),
  maxEntries: z.number().optional().describe("Cap on entries returned (default 50)"),
  maxFrames: z.number().optional().describe("Refuse a range with more frames than this (default 300, max 5000)"),
  maxLines: z.number().int().optional().describe("Messages to return (default 200) (get_message_log). How far back into the ring buffer to read (default 100) (get_output_log)"),
  maxLogChars: z.number().optional().describe("Keep only the last N characters of logged output"),
  maxResults: z.number().int().optional().describe("Cap on matching lines collected out of the 4096-line ring buffer (default 4096)"),
  maxTargets: z.number().int().optional().describe("Upper bound on resolved actor and component targets; a larger set is refused rather than truncated"),
  maxTests: z.number().optional().describe("Cap on tests to run (default 50)"),
  maxValueLength: z.number().optional().describe("Truncate each exported value past this many characters (default 2000)"),
  mode: z.string().optional().describe("none | walking | navwalking | falling | swimming | flying | custom (set_movement_mode). fixed | auto (set_viewport_exposure)"),
  name: z.string().optional().describe("Name of the new Level Sequence asset (create_level_sequence). Console variable to read (get_cvars). Bare stat name such as unit, fps, game or gpu, prefixed with 'stat ' (default fps) (run_stat_command)"),
  nameContains: z.string().optional().describe("Case-insensitive substring of the object name"),
  names: z.array(z.string()).optional().describe("Console variables to read"),
  nearClip: z.number().optional().describe("Near clip plane; negative clears the override"),
  netMode: z.string().optional().describe("standalone | listen | client"),
  newWindowHeight: z.number().optional().describe("Play-in-New-Window height"),
  newWindowWidth: z.number().optional().describe("Play-in-New-Window width"),
  numClients: z.number().optional().describe("Number of PIE clients"),
  objectPath: z.string().optional().describe("Object, asset, class or Blueprint path. A class or Blueprint resolves to its default object (describe_object, get_property, set_property). Check one path: reports found and isValid rather than failing when it is gone. Wins over the search filters (find_object). Object path of the live instance. Wins over target (get_object_properties, invoke_object_function, set_object_property)"),
  onConflict: z.string().optional().describe("skip (default) returns an existing asset untouched, error refuses (create_level_sequence). When the level exists: skip (default) returns it untouched, error refuses (create_new_level)"),
  orthoZoom: z.number().optional().describe("Orthographic zoom, within the engine's own limits"),
  outerPath: z.string().optional().describe("Only objects somewhere under this outer, such as one level or world"),
  outputDir: z.string().optional().describe("Absolute or project-relative directory for the frames (default Saved/SequenceFrames/<sequence>)"),
  outputPath: z.string().optional().describe("Absolute or project-relative PNG path to write, e.g. Saved/Screenshots/cap.png (capture_scene_png). Alias for filename (capture_screenshot)"),
  packagePath: z.string().optional().describe("Destination folder (default /Game/Cinematics)"),
  path: z.string().optional().describe("Alias for sequencePath (add_sequence_section, play_sequence, render_sequence_frames, scrub_sequence, set_sequence_keyframes, set_sequence_playback_range). Alias for assetPath (add_sequence_track, get_sequence_info, open_asset). Alias for objectPath (describe_object, get_property, set_property). Alias for filePath (run_python_file)"),
  paths: z.array(z.string()).optional().describe("Dotted property or function paths to evaluate per match. A function segment may carry literal arguments, e.g. GetBalance(gold, 2)"),
  pattern: z.string().optional().describe("Exact pattern of the policy to clear; omit to clear every policy (clear_dialog_policy). Substring matched against every registered console variable. Pass at least one of name, names and pattern (get_cvars). Case-insensitive substring of the library class name (list_function_libraries). Substring matched case-insensitively against the dialog title and message (set_dialog_policy)"),
  paused: z.boolean().optional().describe("true pauses the running trace, false resumes it (default true)"),
  pieAction: z.string().optional().describe("start | stop | status (default status)"),
  pieInstance: z.number().optional().describe("PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances"),
  pitch: z.number().optional().describe("Control-rotation pitch"),
  platform: z.string().optional().describe("Target platform (default Windows)"),
  playerIndex: z.number().optional().describe("Player index for target=playercontroller or playerpawn (default 0) (get_object_properties, invoke_object_function, set_object_property). 0-based player index (default 0) (get_pie_pawn)"),
  postEditChange: z.boolean().optional().describe("Fire PostEditChangeProperty after the write (default false)"),
  prefix: z.string().optional().describe("Purge sys.modules entries starting with this prefix; must be non-empty"),
  projection: z.string().optional().describe("perspective | top | bottom | left | right | front | back | orthoFreelook. Switched before the pose is applied"),
  propagateToChildren: z.boolean().optional().describe("Also take each matched component's descendants (default true)"),
  propertyName: z.string().optional().describe("Property name; dotted and indexed paths reach component and struct fields"),
  propertyNames: z.array(z.string()).optional().describe("Dotted or indexed property paths to report instead of every property (describe_object). Only these properties. The Details-panel spelling is accepted (get_object_properties)"),
  quality: z.string().optional().describe("Preview (default) | Medium | High | Production"),
  query: z.string().optional().describe("Case-insensitive substring to search the captured log for"),
  regionCategory: z.string().optional().describe("Category shown alongside the region in Unreal Insights"),
  regionName: z.string().optional().describe("Name the bracket is keyed by; end_profile_region closes it (begin_profile_region). Name the region was opened under (end_profile_region)"),
  relativeTo: z.string().optional().describe("Bone or socket whose live frame every sample is expressed in; supersedes space"),
  response: z.string().optional().describe("yes | no | ok | cancel | retry | continue | yesall | noall. On a Slate modal it presses whichever button carries that meaning; an unknown keyword is refused"),
  resultVariable: z.string().optional().describe("Top-level Python variable to return as result, separate from the log (default result with entryPoint)"),
  resX: z.number().optional().describe("Window width (default 1280)"),
  resY: z.number().optional().describe("Window height (default 720)"),
  roll: z.number().optional().describe("Control-rotation roll"),
  rollbackToken: z.string().optional().describe("Token from a non-dry-run set_runtime_visibility, valid for that PIE session only"),
  rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Camera rotation (capture_scene_png, set_viewport_camera). Fixed camera rotation (render_sequence_frames). Destination rotation; omit to keep the current one (teleport_runtime_actor)"),
  runUnderOneProcess: z.boolean().optional().describe("Run every client in the editor process"),
  save: z.boolean().optional().describe("Save the package to disk after the write (default true; false leaves it dirty)"),
  seconds: z.number().optional().describe("Playhead position in seconds. Pass exactly one of seconds and frame"),
  section: z.string().optional().describe("Settings section, e.g. Physics, or a combined Engine.Physics"),
  sectionIndex: z.number().int().optional().describe("Target section index (default 0)"),
  sequenceAction: z.string().optional().describe("play (default) | pause | stop"),
  sequencePath: z.string().optional().describe("Level Sequence asset path (add_sequence_section, set_sequence_keyframes, set_sequence_playback_range). Level Sequence to open and drive; omit to drive the one already open (play_sequence). Level Sequence to render (render_sequence_frames). Level Sequence to open and scrub; omit to scrub the one already open (scrub_sequence)"),
  severity: z.string().optional().describe("Severity-name substring: Error | Warning | PerformanceWarning | Info"),
  showMouseCursor: z.boolean().optional().describe("Show the mouse cursor (default false for gameOnly, true otherwise)"),
  space: z.string().optional().describe("world (default) | component"),
  startFrame: z.number().optional().describe("First display frame to render. Not with startSeconds"),
  startSeconds: z.number().optional().describe("Section start in seconds (add_sequence_section). Range start in seconds (default the playback range) (render_sequence_frames). Range start in seconds (set_sequence_playback_range)"),
  steps: z.number().int().optional().describe("How many steps to apply (default 1)"),
  stopMovement: z.boolean().optional().describe("Stop the movement component so the move is not undone (default true)"),
  subsystemClass: z.string().optional().describe("Subsystem class name or /Script path, with target=subsystem"),
  sweep: z.boolean().optional().describe("Collide on the way (default false)"),
  tabId: z.string().optional().describe("Registered editor tab id, e.g. ProjectSettings, OutputLog or ContentBrowserTab1"),
  target: z.string().optional().describe("auto (default) | pie | editor | window (capture_screenshot). gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem (get_object_properties, invoke_object_function, set_object_property)"),
  templateLevel: z.string().optional().describe("Level to copy; omit, Empty or None for a blank level"),
  timeUnit: z.string().optional().describe("How to read frame: display (default, the frame numbers Sequencer shows) | tick (the units get_sequence_info's playbackRange reports)"),
  traceFile: z.string().optional().describe(".utrace path for the standalone process to write"),
  traceTarget: z.string().optional().describe("file (default, writes a .utrace) | network (streams to a trace server) | none (memory only)"),
  trackType: z.string().optional().describe("Transform | Float | SkeletalAnimation | CameraCut | Audio | Event | Fade"),
  truncate: z.boolean().optional().describe("Overwrite the target file if it exists (default true)"),
  value: z.unknown().optional().describe("New value as structured JSON, the form get_property returns under value"),
  velocity: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Velocity written to the CharacterMovementComponent"),
  viewMode: z.string().optional().describe("Lit | Unlit | Wireframe | LightingOnly | DetailLighting | ShaderComplexity | ... get_viewport_state lists what this build supports"),
  viewportIndex: z.number().optional().describe("Level viewport to act on (default the active one)"),
  viewportType: z.string().optional().describe("Perspective | Top | Bottom | Left | Right | Front | Back | OrthoFreelook"),
  waitForAssetRegistry: z.boolean().optional().describe("start only: block until the AssetRegistry initial scan completes, since PIE silently no-ops during it on a cold editor (default true)"),
  width: z.number().optional().describe("Capture width in pixels (default 1280) (capture_scene_png). Viewport width to read x against, when picking from a screenshot of another resolution (hit_test_viewport_pixel). Frame width in pixels (default 1280) (render_sequence_frames)"),
  windowed: z.boolean().optional().describe("Run windowed rather than fullscreen (default true)"),
  world: z.string().optional().describe("World scope: editor (default) | pie (capture_scene_png). World scope: any (default) | editor | pie (find_object). World scope: editor | pie | auto. Each action names its own default (get_object_properties, get_runtime_value, get_runtime_values, invoke_object_function, invoke_object_functions, read_bone_transforms, restore_runtime_visibility, set_movement_mode, set_object_property, set_runtime_visibility, teleport_runtime_actor). editor (default) | pie | auto (invoke_function). editor (default) | pie | game | auto (invoke_static_function)"),
  worldContextParam: z.string().optional().describe("UObject* parameter to fill with the selected world; detected from WorldContext metadata and for WorldContextObject when omitted"),
  worldPath: z.string().optional().describe("Exact PIE UWorld path or name to capture"),
  x: z.number().optional().describe("Viewport pixel X"),
  y: z.number().optional().describe("Viewport pixel Y"),
  yaw: z.number().optional().describe("Control-rotation yaw"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
