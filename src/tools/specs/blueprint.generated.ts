// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd blueprint handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_blueprint_interface": {
    "category": "blueprint",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path",
        "role": "editTarget"
      },
      {
        "name": "interfacePath",
        "type": "string",
        "required": true,
        "description": "Interface class path"
      }
    ]
  },
  "add_component": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "componentClass",
        "type": "string",
        "required": true,
        "description": "Component class: a short name such as ChildActorComponent, or a full path"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Name of the new component (default: componentClass)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) reports the existing entry; error refuses"
      },
      {
        "name": "parentComponent",
        "type": "string",
        "required": false,
        "description": "SCS parent component for the hierarchy"
      },
      {
        "name": "childActorClass",
        "type": "string",
        "required": false,
        "description": "ChildActorClass for an added ChildActorComponent: a Blueprint path with or without _C, or a C++ class"
      }
    ]
  },
  "add_custom_event": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "eventName",
        "type": "string",
        "required": true,
        "description": "Custom event name"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "parameters",
        "type": "array",
        "required": false,
        "description": "Typed signature parameters [{name, type}]; type takes the add_variable vocabulary, containers included",
        "items": "object"
      },
      {
        "name": "netMode",
        "type": "string",
        "required": false,
        "description": "none (default), multicast, server or client"
      },
      {
        "name": "reliable",
        "type": "boolean",
        "required": false,
        "description": "Send the replicated event reliably (default true; ignored when netMode is none)"
      },
      {
        "name": "callInEditor",
        "type": "boolean",
        "required": false,
        "description": "Expose a details-panel button that runs it on a selected instance"
      },
      {
        "name": "posX",
        "type": "number",
        "required": false,
        "description": "Node X position in the graph"
      },
      {
        "name": "posY",
        "type": "number",
        "required": false,
        "description": "Node Y position in the graph"
      }
    ]
  },
  "add_event_dispatcher": {
    "category": "blueprint",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path",
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Dispatcher name"
      },
      {
        "name": "parameters",
        "type": "array",
        "required": false,
        "description": "Typed signature parameters [{name, type}]; type takes the add_variable vocabulary, containers included",
        "items": "object"
      }
    ]
  },
  "add_function_parameter": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": true,
        "description": "Parameter name"
      },
      {
        "name": "parameterType",
        "type": "string",
        "required": false,
        "description": "Parameter type in the add_variable vocabulary, containers included (default float)"
      },
      {
        "name": "isOutput",
        "type": "boolean",
        "required": false,
        "description": "An output parameter rather than an input (default false)"
      }
    ]
  },
  "add_local_variable": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "varType",
        "type": "string",
        "required": false,
        "description": "Variable type: bool, int, float, string, name, text, byte, vector, rotator, transform, object:/Script/Module.Class, struct:/Game/Path, enum:/Game/Path.Enum, or a container over any of those (Type[], set<Type>, map<Key,Value>)",
        "aliases": [
          "type"
        ]
      }
    ]
  },
  "add_node": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": true,
        "description": "Node class or short alias such as CallFunction, CallParent or CustomEvent"
      },
      {
        "name": "nodeParams",
        "type": "object",
        "required": false,
        "description": "Node-specific settings, such as functionName and className for a CallFunction node"
      }
    ]
  },
  "add_timeline_track": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "timelineName",
        "type": "string",
        "required": true,
        "description": "Timeline name"
      },
      {
        "name": "trackName",
        "type": "string",
        "required": true,
        "description": "Track name within the timeline"
      },
      {
        "name": "trackType",
        "type": "string",
        "required": false,
        "description": "float (default), vector, color or event"
      },
      {
        "name": "keyframes",
        "type": "array",
        "required": false,
        "description": "[{time, value}]: value is a number for float and event, {x,y,z} for vector, {r,g,b,a} for color",
        "items": "object"
      }
    ]
  },
  "add_variable": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "varType",
        "type": "string",
        "required": false,
        "description": "Variable type: bool, int, float, string, name, text, byte, vector, rotator, transform, object:/Script/Module.Class, struct:/Game/Path, enum:/Game/Path.Enum, or a container over any of those (Type[], set<Type>, map<Key,Value>)",
        "aliases": [
          "type"
        ]
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) reports the existing entry; error refuses"
      }
    ]
  },
  "audit_blueprint_dead_code": {
    "category": "blueprint",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to search under (default /Game)"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "Blueprint asset paths; a World path resolves to its level script",
        "items": "string"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders of directory (default true)"
      },
      {
        "name": "includeLevelScripts",
        "type": "boolean",
        "required": false,
        "description": "Also sweep the level script Blueprint of every map under the directory, at a full map load each (default false)"
      },
      {
        "name": "scanReferencers",
        "type": "boolean",
        "required": false,
        "description": "Also scan every Blueprint package that depends on an audited one for references (default true)"
      },
      {
        "name": "maxBlueprints",
        "type": "integer",
        "required": false,
        "description": "Cap on Blueprints loaded (default 2000)",
        "min": 1
      },
      {
        "name": "maxSamples",
        "type": "integer",
        "required": false,
        "description": "Samples listed per finding kind per Blueprint (default 20, max 500); counts stay complete",
        "min": 0
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return (default 200, max 5000)"
      },
      {
        "name": "dumpToFile",
        "type": "boolean",
        "required": false,
        "description": "Write the whole result to a JSON file instead of returning it inline"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Absolute or Saved-relative JSON path for the dump"
      }
    ]
  },
  "auto_layout_graph": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "columnGap",
        "type": "integer",
        "required": false,
        "description": "Horizontal spacing between columns (default 360)"
      },
      {
        "name": "rowGap",
        "type": "integer",
        "required": false,
        "description": "Vertical spacing between rows (default 200)"
      },
      {
        "name": "previousPositionsLimit",
        "type": "integer",
        "required": false,
        "description": "Capture each node's pre-layout coordinates only when the graph has at most this many nodes (default 200)"
      },
      {
        "name": "capturePreviousPositions",
        "type": "boolean",
        "required": false,
        "description": "Capture previousPositions regardless of previousPositionsLimit (default false)"
      }
    ]
  },
  "cleanup_graph": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to clean (default: every graph)"
      }
    ]
  },
  "compile_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      }
    ]
  },
  "compile_blueprints": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPaths",
        "type": "array",
        "required": true,
        "description": "Blueprint asset paths to compile",
        "items": "string",
        "role": "editTarget"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist on success (default true)"
      }
    ]
  },
  "connect_pins": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "graphSelector",
        "type": "string",
        "required": false,
        "description": "The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"
      },
      {
        "name": "sourceNodeId",
        "type": "string",
        "required": true,
        "description": "Source node GUID, as get_connections and find_nodes report it, or its title",
        "aliases": [
          "sourceNode"
        ]
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": true,
        "description": "Source pin name",
        "aliases": [
          "sourcePinName"
        ]
      },
      {
        "name": "targetNodeId",
        "type": "string",
        "required": true,
        "description": "Target node GUID, as get_connections and find_nodes report it, or its title",
        "aliases": [
          "targetNode"
        ]
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": true,
        "description": "Target pin name",
        "aliases": [
          "targetPinName"
        ]
      },
      {
        "name": "breakExistingSource",
        "type": "boolean",
        "required": false,
        "description": "Break every existing link on the source pin first"
      },
      {
        "name": "breakExistingTarget",
        "type": "boolean",
        "required": false,
        "description": "Break every existing link on the target pin first"
      }
    ]
  },
  "connect_pins_batch": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "connections",
        "type": "array",
        "required": true,
        "description": "[{sourceNode, sourcePin, targetNode, targetPin}]",
        "items": "object"
      }
    ]
  },
  "create_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Full destination, e.g. /Game/Blueprints/BP_Example. A .uasset suffix, an object suffix and backslashes are normalized away",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name; with packagePath, the same destination as assetPath"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder, used with name"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent class: short name or full path (default Actor)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) reports the existing entry; error refuses"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "name",
            "packagePath"
          ]
        ]
      }
    ]
  },
  "create_blueprint_interface": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Full destination of the new Blueprint Interface",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) reports the existing entry; error refuses"
      }
    ],
    "contractExempt": "Creates and saves an interface at the contract path; nothing it reads fails first"
  },
  "create_function": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) reports the existing entry; error refuses"
      }
    ]
  },
  "create_macro": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "macroName",
        "type": "string",
        "required": true,
        "description": "Macro graph name"
      },
      {
        "name": "inputs",
        "type": "array",
        "required": false,
        "description": "Input parameters [{name, type}]",
        "items": "object"
      },
      {
        "name": "outputs",
        "type": "array",
        "required": false,
        "description": "Output parameters [{name, type}]",
        "items": "object"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) reports the existing entry; error refuses"
      }
    ]
  },
  "delete_function": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      }
    ]
  },
  "delete_graph": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "graphSelector",
        "type": "string",
        "required": false,
        "description": "The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Remove a graph still owned by a live node, or an event graph; both are refused without it"
      }
    ]
  },
  "delete_macro": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "macroName",
        "type": "string",
        "required": true,
        "description": "Macro graph name"
      }
    ]
  },
  "delete_node": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "graphSelector",
        "type": "string",
        "required": false,
        "description": "The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node GUID, as get_connections and find_nodes report it, or the node name",
        "aliases": [
          "nodeName"
        ]
      }
    ]
  },
  "delete_variable": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      }
    ]
  },
  "diff_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Base Blueprint (A)",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "otherPath",
        "type": "string",
        "required": true,
        "description": "Blueprint to compare against (B)"
      },
      {
        "name": "fromRevision",
        "type": "string",
        "required": false,
        "description": "Reserved for source-control revision diffing, which is not implemented; passing it is refused"
      },
      {
        "name": "toRevision",
        "type": "string",
        "required": false,
        "description": "Reserved for source-control revision diffing, which is not implemented; passing it is refused"
      }
    ]
  },
  "disconnect_pins": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "graphSelector",
        "type": "string",
        "required": false,
        "description": "The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node GUID, as get_connections and find_nodes report it, or the node name",
        "aliases": [
          "nodeName"
        ]
      },
      {
        "name": "pinName",
        "type": "string",
        "required": true,
        "description": "The pin whose links are broken"
      },
      {
        "name": "linkedNodeId",
        "type": "string",
        "required": false,
        "description": "Break only links to this node (GUID or name). Omit to break every link on the pin"
      },
      {
        "name": "linkedPinName",
        "type": "string",
        "required": false,
        "description": "With linkedNodeId, break only the link to this pin"
      }
    ]
  },
  "duplicate_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset to duplicate"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": true,
        "description": "Full destination asset path",
        "role": "editTarget"
      }
    ]
  },
  "edit_graph_parameters": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "op",
        "type": "string",
        "required": true,
        "description": "add, remove, rename, set_type, set_default or reorder"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": false,
        "description": "Function, macro or dispatcher signature to edit; name exactly one of functionName and eventName"
      },
      {
        "name": "eventName",
        "type": "string",
        "required": false,
        "description": "Custom event to edit"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Narrows an eventName search to one graph"
      },
      {
        "name": "isOutput",
        "type": "boolean",
        "required": false,
        "description": "Edit the return side (default false)"
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": false,
        "description": "Parameter to act on"
      },
      {
        "name": "parameterType",
        "type": "string",
        "required": false,
        "description": "Type in the add_variable vocabulary, for add and set_type"
      },
      {
        "name": "newName",
        "type": "string",
        "required": false,
        "description": "New name, for rename"
      },
      {
        "name": "defaultValue",
        "type": "any",
        "required": false,
        "description": "Default value as Unreal export text; an empty string clears it"
      },
      {
        "name": "order",
        "type": "array",
        "required": false,
        "description": "The COMPLETE desired order, never a partial list"
      }
    ]
  },
  "edit_local_variable": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "op",
        "type": "string",
        "required": true,
        "description": "rename, remove, set_type or set_default"
      },
      {
        "name": "newName",
        "type": "string",
        "required": false,
        "description": "New name, for rename"
      },
      {
        "name": "varType",
        "type": "string",
        "required": false,
        "description": "Variable type: bool, int, float, string, name, text, byte, vector, rotator, transform, object:/Script/Module.Class, struct:/Game/Path, enum:/Game/Path.Enum, or a container over any of those (Type[], set<Type>, map<Key,Value>)",
        "aliases": [
          "type"
        ]
      },
      {
        "name": "defaultValue",
        "type": "any",
        "required": false,
        "description": "Default value as Unreal export text; an empty string clears it"
      }
    ]
  },
  "edit_struct_metadata": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "tooltip",
        "type": "string",
        "required": false,
        "description": "Tooltip"
      },
      {
        "name": "fields",
        "type": "array",
        "required": false,
        "description": "[{fieldName or fieldGuid, tooltip?, editableOnInstance?, saveGame?, multiLineText?, widget3D?, metadata?}]",
        "items": "object"
      }
    ]
  },
  "export_blueprint_batch": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "Blueprint asset paths; a World path resolves to its level script",
        "items": "string"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to search under (default /Game)"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders of directory (default true)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Only Blueprints deriving from this class"
      },
      {
        "name": "includeLevelScripts",
        "type": "boolean",
        "required": false,
        "description": "Also sweep the level script Blueprint of every map under the directory, at a full map load each (default false)"
      },
      {
        "name": "outputDir",
        "type": "string",
        "required": false,
        "description": "Absolute directory, or one relative to Saved/ (default Saved/UE_MCP/BlueprintExport)"
      },
      {
        "name": "maxAssets",
        "type": "integer",
        "required": false,
        "description": "Cap on Blueprints exported (default 200, max 5000)",
        "min": 1
      },
      {
        "name": "includeT3D",
        "type": "boolean",
        "required": false,
        "description": "Also write one T3D file per graph (default false)"
      }
    ]
  },
  "export_nodes_t3d": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "nodeIds",
        "type": "array",
        "required": false,
        "description": "Node GUIDs or names to export (omit for the whole graph)",
        "items": "string"
      }
    ]
  },
  "flush_blueprint_component_templates": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      }
    ]
  },
  "flush_inheritable_component_handler": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      }
    ]
  },
  "get_blueprint_component_property": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name"
      }
    ]
  },
  "get_blueprint_connections": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "graphSelector",
        "type": "string",
        "required": false,
        "description": "The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": false,
        "description": "Report only the edges into and out of this node (GUID or name)"
      },
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "exec, data or all (default all)"
      },
      {
        "name": "includeNestedGraphs",
        "type": "boolean",
        "required": false,
        "description": "Also walk collapsed and nested graphs (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 1000)"
      }
    ]
  },
  "get_blueprint_dependencies": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "reverse",
        "type": "boolean",
        "required": false,
        "description": "true reports the referencers of this asset instead"
      }
    ]
  },
  "get_blueprint_execution_flow": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "entryPoint",
        "type": "string",
        "required": false,
        "description": "Event or function name to start the exec-flow trace from"
      }
    ]
  },
  "get_blueprint_variable_default": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      }
    ]
  },
  "get_blueprint_variable_metadata": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": false,
        "description": "The function owning a local variable; omit for a member variable"
      }
    ]
  },
  "get_cdo_properties": {
    "category": "blueprint",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "C++ class name or path"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Property names to read (omit for all)",
        "items": "string"
      }
    ]
  },
  "get_component_collision": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint path, or a native class (/Script/MyGame.MyCharacter or MyCharacter)",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "Component whose effective collision to read"
      },
      {
        "name": "channel",
        "type": "string",
        "required": false,
        "description": "Narrow the answer to one collision channel: its configured name, the C++ enumerator, or the container index"
      },
      {
        "name": "includeAllChannels",
        "type": "boolean",
        "required": false,
        "description": "Include the unused GameTraceChannel slots (default false)"
      }
    ]
  },
  "import_nodes_t3d": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "t3d",
        "type": "string",
        "required": true,
        "description": "T3D blob from export_nodes_t3d, or copied from the Blueprint graph editor",
        "aliases": [
          "text"
        ]
      },
      {
        "name": "posX",
        "type": "number",
        "required": false,
        "description": "Re-center pasted nodes around this X (with posY)"
      },
      {
        "name": "posY",
        "type": "number",
        "required": false,
        "description": "Re-center pasted nodes around this Y (with posX)"
      }
    ]
  },
  "list_blueprint_functions": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeInherited",
        "type": "boolean",
        "required": false,
        "description": "Append overridable parent and interface functions this Blueprint has not implemented (default false)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "list_blueprint_graphs": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_blueprint_interfaces": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "blueprintPath"
        ]
      }
    ]
  },
  "list_blueprint_variables": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeValues",
        "type": "boolean",
        "required": false,
        "description": "Also resolve each variable's default off the generated-class CDO, and report whether the package holds unsaved changes (default false)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "list_event_dispatchers": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "blueprintPath"
        ]
      }
    ]
  },
  "list_graph_parameters": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "functionName",
        "type": "string",
        "required": false,
        "description": "Function, macro or dispatcher signature to read; name exactly one of functionName and eventName"
      },
      {
        "name": "eventName",
        "type": "string",
        "required": false,
        "description": "Custom event to read"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Narrows an eventName search to one graph"
      }
    ]
  },
  "list_local_variables": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      }
    ]
  },
  "list_node_types": {
    "category": "blueprint",
    "params": [
      {
        "name": "category",
        "type": "string",
        "required": false,
        "description": "Node category: utilities (default), math, string, gameplay, actor and so on"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 100, max 1000)"
      }
    ]
  },
  "list_overridable_functions": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "override_function": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      },
      {
        "name": "source",
        "type": "string",
        "required": false,
        "description": "Advisory hint for where the function comes from: auto (default), interface or parent. Echoed back"
      },
      {
        "name": "preferFunction",
        "type": "boolean",
        "required": false,
        "description": "Force the function-graph form even when the function could be placed as an override event"
      },
      {
        "name": "interfacePath",
        "type": "string",
        "required": false,
        "description": "Implement this interface first when it is not present"
      }
    ]
  },
  "read_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeComponentProperties",
        "type": "boolean",
        "required": false,
        "description": "Dump UPROPERTY name, type and value per component template (default false)"
      }
    ]
  },
  "read_blueprint_graph": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "Row offset into the (filtered) node list",
        "min": 0
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Nodes to return; omit for a safe default page, or dump the whole graph with dumpToFile"
      },
      {
        "name": "includePins",
        "type": "boolean",
        "required": false,
        "description": "Include pins (default true)"
      },
      {
        "name": "includeDefaults",
        "type": "boolean",
        "required": false,
        "description": "Include pin default values (default true)"
      },
      {
        "name": "includeComments",
        "type": "boolean",
        "required": false,
        "description": "Include node comments (default true)"
      },
      {
        "name": "dumpToFile",
        "type": "boolean",
        "required": false,
        "description": "Write the whole result to a JSON file instead of returning it inline"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Absolute or Saved-relative JSON path for the dump"
      },
      {
        "name": "titleFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring match on node title"
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring match on node class name"
      }
    ]
  },
  "read_blueprint_graph_summary": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "titleFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring match on node title"
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring match on node class name"
      }
    ]
  },
  "read_component_properties": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      }
    ]
  },
  "read_node_property": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node GUID, as get_connections and find_nodes report it, or the node name",
        "aliases": [
          "nodeName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Pin or reflected node property name",
        "aliases": [
          "pinName"
        ]
      }
    ]
  },
  "read_user_defined_enum": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedEnum asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_user_defined_struct": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "refresh_node": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "graphSelector",
        "type": "string",
        "required": false,
        "description": "The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node GUID, as get_connections and find_nodes report it, or the node name",
        "aliases": [
          "nodeName"
        ]
      },
      {
        "name": "breakOrphanedPins",
        "type": "boolean",
        "required": false,
        "description": "Also break the links holding orphaned pins alive, which removes those pins"
      }
    ]
  },
  "remove_blueprint_interface": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "blueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "interfacePath",
        "type": "string",
        "required": true,
        "description": "Interface class path, as list_interfaces reports it"
      },
      {
        "name": "preserveFunctions",
        "type": "boolean",
        "required": false,
        "description": "Keep the implementations as ordinary Blueprint functions (default false)"
      }
    ]
  },
  "remove_component": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      }
    ]
  },
  "remove_event_dispatcher": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "blueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Dispatcher name"
      }
    ]
  },
  "rename_blueprint_variable": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "oldName",
        "type": "string",
        "required": true,
        "description": "Current name"
      },
      {
        "name": "newName",
        "type": "string",
        "required": true,
        "description": "New name"
      }
    ]
  },
  "rename_function": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "oldName",
        "type": "string",
        "required": true,
        "description": "Current name"
      },
      {
        "name": "newName",
        "type": "string",
        "required": true,
        "description": "New name"
      }
    ]
  },
  "reorder_enum_values": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedEnum asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "order",
        "type": "array",
        "required": true,
        "description": "The COMPLETE desired order, never a partial list"
      }
    ]
  },
  "reorder_struct_fields": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "order",
        "type": "array",
        "required": true,
        "description": "The COMPLETE desired order, never a partial list"
      }
    ]
  },
  "reparent_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": true,
        "description": "New parent class: short name or full path"
      }
    ]
  },
  "reparent_component": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      },
      {
        "name": "newParent",
        "type": "string",
        "required": true,
        "description": "New parent component name"
      }
    ]
  },
  "resolve_blueprint_graph": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": true,
        "description": "Graph name to resolve"
      }
    ]
  },
  "resolve_collision_profile": {
    "category": "blueprint",
    "params": [
      {
        "name": "profileName",
        "type": "string",
        "required": true,
        "description": "Collision profile to resolve, e.g. Pawn, BlockAll, or one the project defined"
      },
      {
        "name": "channel",
        "type": "string",
        "required": false,
        "description": "Narrow the answer to one collision channel: its configured name, the C++ enumerator, or the container index"
      },
      {
        "name": "includeAllChannels",
        "type": "boolean",
        "required": false,
        "description": "Include the unused GameTraceChannel slots (default false)"
      }
    ]
  },
  "run_construction_script": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Spawn location for the temporary actor"
      }
    ]
  },
  "search_blueprint_call_sites": {
    "category": "blueprint",
    "params": [
      {
        "name": "functionNames",
        "type": "array",
        "required": true,
        "description": "Function names to find call sites for, max 50 per request",
        "items": "string"
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Declaring class to narrow the functions to"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to search under (default /Game)"
      },
      {
        "name": "includeNestedGraphs",
        "type": "boolean",
        "required": false,
        "description": "Also walk collapsed and nested graphs (default true)"
      },
      {
        "name": "includeLevelScripts",
        "type": "boolean",
        "required": false,
        "description": "Also sweep the level script Blueprint of every map under the directory, at a full map load each (default false)"
      },
      {
        "name": "includeNeighbours",
        "type": "boolean",
        "required": false,
        "description": "Include each hit's immediate execution and data neighbours"
      },
      {
        "name": "narrowByRegistry",
        "type": "boolean",
        "required": false,
        "description": "Use Asset Registry dependencies to rule out Blueprints before loading them (default true)"
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "Row offset, the older non-resumable form of paging",
        "min": 0
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 1000)"
      },
      {
        "name": "maxBlueprints",
        "type": "integer",
        "required": false,
        "description": "Cap on Blueprints loaded (default 2000)",
        "min": 1
      },
      {
        "name": "dumpToFile",
        "type": "boolean",
        "required": false,
        "description": "Write the whole result to a JSON file instead of returning it inline"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Absolute or Saved-relative JSON path for the dump"
      }
    ]
  },
  "search_blueprint_nodes": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "blueprintPath"
        ]
      },
      {
        "name": "titles",
        "type": "array",
        "required": false,
        "description": "Case-insensitive substrings matched against each node title",
        "items": "string"
      },
      {
        "name": "nodeClasses",
        "type": "array",
        "required": false,
        "description": "Exact node class names, such as K2Node_VariableGet",
        "items": "string"
      },
      {
        "name": "variableName",
        "type": "string",
        "required": false,
        "description": "The member a node reads or writes"
      },
      {
        "name": "variableAccess",
        "type": "string",
        "required": false,
        "description": "Narrow variableName to get, set or any (default any)"
      },
      {
        "name": "includeNestedGraphs",
        "type": "boolean",
        "required": false,
        "description": "Also walk collapsed and nested graphs (default true)"
      },
      {
        "name": "authoredOnly",
        "type": "boolean",
        "required": false,
        "description": "Leave out transient and generated compiler graphs (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 1000)"
      }
    ]
  },
  "search_node_types": {
    "category": "blueprint",
    "params": [
      {
        "name": "query",
        "type": "string",
        "required": true,
        "description": "Words to match against the C++ name, the palette label and Keywords metadata"
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Narrow to one owning class, by short name or object path",
        "aliases": [
          "classFilter"
        ]
      },
      {
        "name": "includeGraphNodes",
        "type": "boolean",
        "required": false,
        "description": "Include UEdGraphNode classes alongside function-library entries (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 50, max 500)"
      }
    ]
  },
  "set_actor_tick_settings": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "bCanEverTick",
        "type": "boolean",
        "required": false,
        "description": "PrimaryActorTick.bCanEverTick"
      },
      {
        "name": "bStartWithTickEnabled",
        "type": "boolean",
        "required": false,
        "description": "PrimaryActorTick.bStartWithTickEnabled"
      },
      {
        "name": "TickInterval",
        "type": "number",
        "required": false,
        "description": "PrimaryActorTick.TickInterval in seconds"
      }
    ]
  },
  "set_blueprint_component_property": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Value to write: a JSON value, or Unreal export text. null clears an object, class or interface reference"
      }
    ]
  },
  "set_blueprint_variable_metadata": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "metadata",
        "type": "object",
        "required": true,
        "description": "{key: 'value'} pairs; a null value removes that key. Unreal stores every metadata value as text"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": false,
        "description": "The function owning a local variable; omit for a member variable"
      }
    ]
  },
  "set_capsule_size": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      },
      {
        "name": "halfHeight",
        "type": "number",
        "required": false,
        "description": "Capsule half height (unscaled)"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Capsule radius (unscaled)"
      }
    ]
  },
  "set_cdo_property": {
    "category": "blueprint",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "C++ class name or path"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Value to write: a JSON value, or Unreal export text. null clears an object, class or interface reference"
      }
    ]
  },
  "set_class_default": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Value to write: a JSON value, or Unreal export text. null clears an object, class or interface reference"
      }
    ]
  },
  "set_component_override_materials": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": true,
        "description": "SCS or inherited component name"
      },
      {
        "name": "materialPaths",
        "type": "array",
        "required": true,
        "description": "Material asset paths; an empty array clears",
        "items": "string"
      }
    ]
  },
  "set_enum_metadata": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedEnum asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "bitflags",
        "type": "boolean",
        "required": false,
        "description": "Mark the enum as a bitflags type"
      },
      {
        "name": "entries",
        "type": "array",
        "required": false,
        "description": "[{name or index, tooltip}] per-enumerator tooltips",
        "items": "object"
      }
    ]
  },
  "set_function_properties": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Function name"
      },
      {
        "name": "pure",
        "type": "boolean",
        "required": false,
        "description": "BlueprintPure: no side effects, and the node loses its exec pins"
      },
      {
        "name": "isConst",
        "type": "boolean",
        "required": false,
        "description": "const: the function only reads state"
      },
      {
        "name": "accessSpecifier",
        "type": "string",
        "required": false,
        "description": "public, protected or private"
      },
      {
        "name": "category",
        "type": "string",
        "required": false,
        "description": "Category"
      },
      {
        "name": "tooltip",
        "type": "string",
        "required": false,
        "description": "Tooltip"
      },
      {
        "name": "keywords",
        "type": "string",
        "required": false,
        "description": "Extra palette search keywords"
      },
      {
        "name": "compactNodeTitle",
        "type": "string",
        "required": false,
        "description": "Render the node in compact form under this title"
      },
      {
        "name": "callInEditor",
        "type": "boolean",
        "required": false,
        "description": "Expose a details-panel button that runs it on a selected instance"
      },
      {
        "name": "threadSafe",
        "type": "boolean",
        "required": false,
        "description": "Safe to call off the game thread"
      },
      {
        "name": "deprecated",
        "type": "boolean",
        "required": false,
        "description": "Mark the function deprecated so its call sites warn"
      },
      {
        "name": "deprecationMessage",
        "type": "string",
        "required": false,
        "description": "Text shown at a deprecated function's call sites"
      }
    ]
  },
  "set_node_position": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node GUID, as get_connections and find_nodes report it, or the node name",
        "aliases": [
          "nodeName"
        ]
      },
      {
        "name": "posX",
        "type": "number",
        "required": false,
        "description": "Node X position in the graph"
      },
      {
        "name": "posY",
        "type": "number",
        "required": false,
        "description": "Node Y position in the graph"
      }
    ]
  },
  "set_node_property": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph name or the selector list_graphs reports (default EventGraph)"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node GUID, as get_connections and find_nodes report it, or the node name",
        "aliases": [
          "nodeName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Pin or struct property name",
        "aliases": [
          "pinName"
        ]
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New pin default or property value, as a string",
        "aliases": [
          "defaultValue"
        ]
      }
    ]
  },
  "set_struct_field_default": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "defaultValue",
        "type": "any",
        "required": true,
        "description": "Default value as Unreal export text; an empty string clears it"
      },
      {
        "name": "fieldName",
        "type": "string",
        "required": false,
        "description": "Resolve the member by its display or internal name"
      },
      {
        "name": "fieldGuid",
        "type": "string",
        "required": false,
        "description": "Resolve the member by its GUID, which is stable across renames"
      }
    ]
  },
  "set_variable_default": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New default value, as a string"
      }
    ]
  },
  "set_variable_properties": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Variable name"
      },
      {
        "name": "editFlag",
        "type": "string",
        "required": false,
        "description": "EditAnywhere, EditDefaultsOnly, EditInstanceOnly or none: the value list_variables reports"
      },
      {
        "name": "instanceEditable",
        "type": "boolean",
        "required": false,
        "description": "Two-state shorthand for editFlag; mutually exclusive with it"
      },
      {
        "name": "private",
        "type": "boolean",
        "required": false,
        "description": "The Blueprint editor's Private checkbox, independent of editFlag"
      },
      {
        "name": "category",
        "type": "string",
        "required": false,
        "description": "Category"
      },
      {
        "name": "tooltip",
        "type": "string",
        "required": false,
        "description": "Tooltip"
      },
      {
        "name": "exposeOnSpawn",
        "type": "boolean",
        "required": false,
        "description": "ExposeOnSpawn flag"
      }
    ]
  },
  "validate_blueprint": {
    "category": "blueprint",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint",
        "aliases": [
          "path"
        ]
      }
    ]
  }
};

/** Every key the spec'd blueprint handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
