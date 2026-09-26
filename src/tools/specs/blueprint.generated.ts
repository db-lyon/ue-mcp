// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd blueprint handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_blueprint_interface": {
    "category": "blueprint",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path"
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
        ]
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
        ]
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
        "description": "Blueprint asset path"
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        "description": "Cap on Blueprints loaded (default 2000)"
      },
      {
        "name": "maxSamples",
        "type": "integer",
        "required": false,
        "description": "Samples listed per finding kind per Blueprint (default 20, max 500); counts stay complete"
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
        ]
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
        ]
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
        ]
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
        "items": "string"
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        "description": "Full destination asset path"
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
        ]
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
        ]
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
        ]
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
        "description": "Cap on Blueprints exported (default 200, max 5000)"
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
        ]
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
        ]
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
        ]
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
        ]
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
        "description": "Row offset into the (filtered) node list"
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        "description": "Row offset, the older non-resumable form of paging"
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
        "description": "Cap on Blueprints loaded (default 2000)"
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_blueprint_interface: "Params: blueprintPath, interfacePath",
  add_component: "Params: assetPath (or path), componentClass, componentName?, onConflict?, parentComponent?, childActorClass?",
  add_custom_event: "Params: assetPath (or path), eventName, graphName?, parameters?, netMode?, reliable?, callInEditor?, posX?, posY?",
  add_event_dispatcher: "Params: blueprintPath, name, parameters?",
  add_function_parameter: "Params: assetPath (or path), functionName, parameterName, parameterType?, isOutput?",
  add_local_variable: "Params: assetPath (or path), functionName, name, varType? (or type)",
  add_node: "Params: assetPath (or path), graphName?, nodeClass, nodeParams?",
  add_timeline_track: "Params: assetPath (or path), timelineName, trackName, trackType?, keyframes?",
  add_variable: "Params: assetPath (or path), name, varType? (or type), onConflict?",
  audit_blueprint_dead_code: "Params: directory?, assetPaths?, recursive?, includeLevelScripts?, scanReferencers?, maxBlueprints?, maxSamples?, limit?, dumpToFile?, outputPath?",
  auto_layout_graph: "Params: assetPath (or path), graphName?, columnGap?, rowGap?, previousPositionsLimit?, capturePreviousPositions?",
  cleanup_graph: "Params: assetPath (or path), graphName?",
  compile_blueprint: "Params: assetPath (or path)",
  compile_blueprints: "Params: assetPaths, save?",
  connect_pins: "Params: assetPath (or path), graphName?, graphSelector?, sourceNodeId (or sourceNode), sourcePin (or sourcePinName), targetNodeId (or targetNode), targetPin (or targetPinName), breakExistingSource?, breakExistingTarget?",
  connect_pins_batch: "Params: assetPath (or path), graphName?, connections",
  create_blueprint: "Params: assetPath (or path) OR name + packagePath, parentClass?, onConflict?",
  create_blueprint_interface: "Params: assetPath (or path), onConflict?",
  create_function: "Params: assetPath (or path), functionName, onConflict?",
  create_macro: "Params: assetPath (or path), macroName, inputs?, outputs?, onConflict?",
  delete_function: "Params: assetPath (or path), functionName",
  delete_graph: "Params: assetPath (or path), graphName?, graphSelector?, force?",
  delete_macro: "Params: assetPath (or path), macroName",
  delete_node: "Params: assetPath (or path), graphName?, graphSelector?, nodeId (or nodeName)",
  delete_variable: "Params: assetPath (or path), name",
  diff_blueprint: "Params: assetPath (or path), otherPath, fromRevision?, toRevision?",
  disconnect_pins: "Params: assetPath (or path), graphName?, graphSelector?, nodeId (or nodeName), pinName, linkedNodeId?, linkedPinName?",
  duplicate_blueprint: "Params: sourcePath, destinationPath",
  edit_graph_parameters: "Params: assetPath (or path), op, functionName?, eventName?, graphName?, isOutput?, parameterName?, parameterType?, newName?, defaultValue?, order?",
  edit_local_variable: "Params: assetPath (or path), functionName, name, op, newName?, varType? (or type), defaultValue?",
  edit_struct_metadata: "Params: assetPath (or path), tooltip?, fields?",
  export_blueprint_batch: "Params: assetPaths?, directory?, recursive?, parentClass?, includeLevelScripts?, outputDir?, maxAssets?, includeT3D?",
  export_nodes_t3d: "Params: assetPath (or path), graphName?, nodeIds?",
  flush_blueprint_component_templates: "Params: assetPath (or path)",
  flush_inheritable_component_handler: "Params: assetPath (or path)",
  get_blueprint_component_property: "Params: assetPath (or path), componentName, propertyName",
  get_blueprint_connections: "Params: assetPath (or path), graphName?, graphSelector?, nodeId?, kind?, includeNestedGraphs?, cursor?, limit?",
  get_blueprint_dependencies: "Params: assetPath (or path), reverse?",
  get_blueprint_execution_flow: "Params: assetPath (or path), graphName?, entryPoint?",
  get_blueprint_variable_default: "Params: assetPath (or path), name",
  get_blueprint_variable_metadata: "Params: assetPath (or path), name, functionName?",
  get_cdo_properties: "Params: className, propertyNames?",
  get_component_collision: "Params: assetPath (or path), componentName, channel?, includeAllChannels?",
  import_nodes_t3d: "Params: assetPath (or path), graphName?, t3d (or text), posX?, posY?",
  list_blueprint_functions: "Params: assetPath (or path), includeInherited?, cursor?, limit?",
  list_blueprint_graphs: "Params: assetPath (or path)",
  list_blueprint_interfaces: "Params: assetPath (or blueprintPath)",
  list_blueprint_variables: "Params: assetPath (or path), includeValues?, cursor?, limit?",
  list_event_dispatchers: "Params: assetPath (or blueprintPath)",
  list_graph_parameters: "Params: assetPath (or path), functionName?, eventName?, graphName?",
  list_local_variables: "Params: assetPath (or path), functionName",
  list_node_types: "Params: category?, cursor?, limit?",
  list_overridable_functions: "Params: assetPath (or path)",
  override_function: "Params: assetPath (or path), functionName, source?, preferFunction?, interfacePath?",
  read_blueprint: "Params: assetPath (or path), includeComponentProperties?",
  read_blueprint_graph: "Params: assetPath (or path), graphName?, offset?, limit?, includePins?, includeDefaults?, includeComments?, dumpToFile?, outputPath?, titleFilter?, classFilter?",
  read_blueprint_graph_summary: "Params: assetPath (or path), graphName?, titleFilter?, classFilter?",
  read_component_properties: "Params: assetPath (or path), componentName",
  read_node_property: "Params: assetPath (or path), graphName?, nodeId (or nodeName), propertyName (or pinName)",
  read_user_defined_enum: "Params: assetPath (or path)",
  read_user_defined_struct: "Params: assetPath (or path)",
  refresh_node: "Params: assetPath (or path), graphName?, graphSelector?, nodeId (or nodeName), breakOrphanedPins?",
  remove_blueprint_interface: "Params: assetPath (or blueprintPath), interfacePath, preserveFunctions?",
  remove_component: "Params: assetPath (or path), componentName",
  remove_event_dispatcher: "Params: assetPath (or blueprintPath), name",
  rename_blueprint_variable: "Params: assetPath (or path), oldName, newName",
  rename_function: "Params: assetPath (or path), oldName, newName",
  reorder_enum_values: "Params: assetPath (or path), order",
  reorder_struct_fields: "Params: assetPath (or path), order",
  reparent_blueprint: "Params: assetPath (or path), parentClass",
  reparent_component: "Params: assetPath (or path), componentName, newParent",
  resolve_blueprint_graph: "Params: assetPath (or path), graphName",
  resolve_collision_profile: "Params: profileName, channel?, includeAllChannels?",
  run_construction_script: "Params: assetPath (or path), location?",
  search_blueprint_call_sites: "Params: functionNames, className?, directory?, includeNestedGraphs?, includeLevelScripts?, includeNeighbours?, narrowByRegistry?, offset?, cursor?, limit?, maxBlueprints?, dumpToFile?, outputPath?",
  search_blueprint_nodes: "Params: assetPath (or blueprintPath), titles?, nodeClasses?, variableName?, variableAccess?, includeNestedGraphs?, authoredOnly?, cursor?, limit?",
  search_node_types: "Params: query, className? (or classFilter), includeGraphNodes?, cursor?, limit?",
  set_actor_tick_settings: "Params: assetPath (or path), bCanEverTick?, bStartWithTickEnabled?, TickInterval?",
  set_blueprint_component_property: "Params: assetPath (or path), componentName, propertyName, value",
  set_blueprint_variable_metadata: "Params: assetPath (or path), name, metadata, functionName?",
  set_capsule_size: "Params: assetPath (or path), componentName, halfHeight?, radius?",
  set_cdo_property: "Params: className, propertyName, value",
  set_class_default: "Params: assetPath (or path), propertyName, value",
  set_component_override_materials: "Params: assetPath (or path), componentName, materialPaths",
  set_enum_metadata: "Params: assetPath (or path), bitflags?, entries?",
  set_function_properties: "Params: assetPath (or path), functionName, pure?, isConst?, accessSpecifier?, category?, tooltip?, keywords?, compactNodeTitle?, callInEditor?, threadSafe?, deprecated?, deprecationMessage?",
  set_node_position: "Params: assetPath (or path), graphName?, nodeId (or nodeName), posX?, posY?",
  set_node_property: "Params: assetPath (or path), graphName?, nodeId (or nodeName), propertyName (or pinName), value (or defaultValue)",
  set_struct_field_default: "Params: assetPath (or path), defaultValue, fieldName?, fieldGuid?",
  set_variable_default: "Params: assetPath (or path), name, value",
  set_variable_properties: "Params: assetPath (or path), name, editFlag?, instanceEditable?, private?, category?, tooltip?, exposeOnSpawn?",
  validate_blueprint: "Params: assetPath (or path)",
};

/** Every key the spec'd blueprint handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  accessSpecifier: z.string().optional().describe("public, protected or private"),
  assetPath: z.string().optional().describe("Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint (add_component, add_custom_event, add_function_parameter, add_local_variable, add_node, add_timeline_track, add_variable, auto_layout_graph, cleanup_graph, compile_blueprint, connect_pins, connect_pins_batch, create_function, create_macro, delete_function, delete_graph, delete_macro, delete_node, delete_variable, disconnect_pins, edit_graph_parameters, edit_local_variable, export_nodes_t3d, flush_blueprint_component_templates, flush_inheritable_component_handler, get_blueprint_component_property, get_blueprint_connections, get_blueprint_dependencies, get_blueprint_execution_flow, get_blueprint_variable_default, get_blueprint_variable_metadata, import_nodes_t3d, list_blueprint_functions, list_blueprint_graphs, list_blueprint_interfaces, list_blueprint_variables, list_event_dispatchers, list_graph_parameters, list_local_variables, list_overridable_functions, override_function, read_blueprint, read_blueprint_graph, read_blueprint_graph_summary, read_component_properties, read_node_property, refresh_node, remove_blueprint_interface, remove_component, remove_event_dispatcher, rename_blueprint_variable, rename_function, reparent_blueprint, reparent_component, resolve_blueprint_graph, run_construction_script, search_blueprint_nodes, set_actor_tick_settings, set_blueprint_component_property, set_blueprint_variable_metadata, set_capsule_size, set_class_default, set_component_override_materials, set_function_properties, set_node_position, set_node_property, set_variable_default, set_variable_properties, validate_blueprint). Full destination, e.g. /Game/Blueprints/BP_Example. A .uasset suffix, an object suffix and backslashes are normalized away (create_blueprint). Full destination of the new Blueprint Interface (create_blueprint_interface). Base Blueprint (A) (diff_blueprint). UserDefinedStruct asset path (edit_struct_metadata, read_user_defined_struct, reorder_struct_fields, set_struct_field_default). Blueprint path, or a native class (/Script/MyGame.MyCharacter or MyCharacter) (get_component_collision). UserDefinedEnum asset path (read_user_defined_enum, reorder_enum_values, set_enum_metadata)"),
  assetPaths: z.array(z.string()).optional().describe("Blueprint asset paths; a World path resolves to its level script (audit_blueprint_dead_code, export_blueprint_batch). Blueprint asset paths to compile (compile_blueprints)"),
  authoredOnly: z.boolean().optional().describe("Leave out transient and generated compiler graphs (default true)"),
  bCanEverTick: z.boolean().optional().describe("PrimaryActorTick.bCanEverTick"),
  bitflags: z.boolean().optional().describe("Mark the enum as a bitflags type"),
  blueprintPath: z.string().optional().describe("Blueprint asset path (add_blueprint_interface, add_event_dispatcher). Alias for assetPath (list_blueprint_interfaces, list_event_dispatchers, remove_blueprint_interface, remove_event_dispatcher, search_blueprint_nodes)"),
  breakExistingSource: z.boolean().optional().describe("Break every existing link on the source pin first"),
  breakExistingTarget: z.boolean().optional().describe("Break every existing link on the target pin first"),
  breakOrphanedPins: z.boolean().optional().describe("Also break the links holding orphaned pins alive, which removes those pins"),
  bStartWithTickEnabled: z.boolean().optional().describe("PrimaryActorTick.bStartWithTickEnabled"),
  callInEditor: z.boolean().optional().describe("Expose a details-panel button that runs it on a selected instance"),
  capturePreviousPositions: z.boolean().optional().describe("Capture previousPositions regardless of previousPositionsLimit (default false)"),
  category: z.string().optional().describe("Node category: utilities (default), math, string, gameplay, actor and so on (list_node_types). Category (set_function_properties, set_variable_properties)"),
  channel: z.string().optional().describe("Narrow the answer to one collision channel: its configured name, the C++ enumerator, or the container index"),
  childActorClass: z.string().optional().describe("ChildActorClass for an added ChildActorComponent: a Blueprint path with or without _C, or a C++ class"),
  classFilter: z.string().optional().describe("Case-insensitive substring match on node class name (read_blueprint_graph, read_blueprint_graph_summary). Alias for className (search_node_types)"),
  className: z.string().optional().describe("C++ class name or path (get_cdo_properties, set_cdo_property). Declaring class to narrow the functions to (search_blueprint_call_sites). Narrow to one owning class, by short name or object path (search_node_types)"),
  columnGap: z.number().int().optional().describe("Horizontal spacing between columns (default 360)"),
  compactNodeTitle: z.string().optional().describe("Render the node in compact form under this title"),
  componentClass: z.string().optional().describe("Component class: a short name such as ChildActorComponent, or a full path"),
  componentName: z.string().optional().describe("Name of the new component (default: componentClass) (add_component). SCS or inherited component name (get_blueprint_component_property, read_component_properties, remove_component, reparent_component, set_blueprint_component_property, set_capsule_size, set_component_override_materials). Component whose effective collision to read (get_component_collision)"),
  connections: z.array(z.record(z.unknown())).optional().describe("[{sourceNode, sourcePin, targetNode, targetPin}]"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  defaultValue: z.unknown().optional().describe("Default value as Unreal export text; an empty string clears it (edit_graph_parameters, edit_local_variable, set_struct_field_default). Alias for value (set_node_property)"),
  deprecated: z.boolean().optional().describe("Mark the function deprecated so its call sites warn"),
  deprecationMessage: z.string().optional().describe("Text shown at a deprecated function's call sites"),
  destinationPath: z.string().optional().describe("Full destination asset path"),
  directory: z.string().optional().describe("Content path to search under (default /Game)"),
  dumpToFile: z.boolean().optional().describe("Write the whole result to a JSON file instead of returning it inline"),
  editFlag: z.string().optional().describe("EditAnywhere, EditDefaultsOnly, EditInstanceOnly or none: the value list_variables reports"),
  entries: z.array(z.record(z.unknown())).optional().describe("[{name or index, tooltip}] per-enumerator tooltips"),
  entryPoint: z.string().optional().describe("Event or function name to start the exec-flow trace from"),
  eventName: z.string().optional().describe("Custom event name (add_custom_event). Custom event to edit (edit_graph_parameters). Custom event to read (list_graph_parameters)"),
  exposeOnSpawn: z.boolean().optional().describe("ExposeOnSpawn flag"),
  fieldGuid: z.string().optional().describe("Resolve the member by its GUID, which is stable across renames"),
  fieldName: z.string().optional().describe("Resolve the member by its display or internal name"),
  fields: z.array(z.record(z.unknown())).optional().describe("[{fieldName or fieldGuid, tooltip?, editableOnInstance?, saveGame?, multiLineText?, widget3D?, metadata?}]"),
  force: z.boolean().optional().describe("Remove a graph still owned by a live node, or an event graph; both are refused without it"),
  fromRevision: z.string().optional().describe("Reserved for source-control revision diffing, which is not implemented; passing it is refused"),
  functionName: z.string().optional().describe("Function name (add_function_parameter, add_local_variable, create_function, delete_function, edit_local_variable, list_local_variables, override_function, set_function_properties). Function, macro or dispatcher signature to edit; name exactly one of functionName and eventName (edit_graph_parameters). The function owning a local variable; omit for a member variable (get_blueprint_variable_metadata, set_blueprint_variable_metadata). Function, macro or dispatcher signature to read; name exactly one of functionName and eventName (list_graph_parameters)"),
  functionNames: z.array(z.string()).optional().describe("Function names to find call sites for, max 50 per request"),
  graphName: z.string().optional().describe("Graph name or the selector list_graphs reports (default EventGraph) (add_custom_event, add_node, auto_layout_graph, connect_pins, connect_pins_batch, delete_graph, delete_node, disconnect_pins, export_nodes_t3d, get_blueprint_connections, get_blueprint_execution_flow, import_nodes_t3d, read_blueprint_graph, read_blueprint_graph_summary, read_node_property, refresh_node, set_node_position, set_node_property). Graph to clean (default: every graph) (cleanup_graph). Narrows an eventName search to one graph (edit_graph_parameters, list_graph_parameters). Graph name to resolve (resolve_blueprint_graph)"),
  graphSelector: z.string().optional().describe("The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"),
  halfHeight: z.number().optional().describe("Capsule half height (unscaled)"),
  includeAllChannels: z.boolean().optional().describe("Include the unused GameTraceChannel slots (default false)"),
  includeComments: z.boolean().optional().describe("Include node comments (default true)"),
  includeComponentProperties: z.boolean().optional().describe("Dump UPROPERTY name, type and value per component template (default false)"),
  includeDefaults: z.boolean().optional().describe("Include pin default values (default true)"),
  includeGraphNodes: z.boolean().optional().describe("Include UEdGraphNode classes alongside function-library entries (default true)"),
  includeInherited: z.boolean().optional().describe("Append overridable parent and interface functions this Blueprint has not implemented (default false)"),
  includeLevelScripts: z.boolean().optional().describe("Also sweep the level script Blueprint of every map under the directory, at a full map load each (default false)"),
  includeNeighbours: z.boolean().optional().describe("Include each hit's immediate execution and data neighbours"),
  includeNestedGraphs: z.boolean().optional().describe("Also walk collapsed and nested graphs (default true)"),
  includePins: z.boolean().optional().describe("Include pins (default true)"),
  includeT3D: z.boolean().optional().describe("Also write one T3D file per graph (default false)"),
  includeValues: z.boolean().optional().describe("Also resolve each variable's default off the generated-class CDO, and report whether the package holds unsaved changes (default false)"),
  inputs: z.array(z.record(z.unknown())).optional().describe("Input parameters [{name, type}]"),
  instanceEditable: z.boolean().optional().describe("Two-state shorthand for editFlag; mutually exclusive with it"),
  interfacePath: z.string().optional().describe("Interface class path (add_blueprint_interface). Implement this interface first when it is not present (override_function). Interface class path, as list_interfaces reports it (remove_blueprint_interface)"),
  isConst: z.boolean().optional().describe("const: the function only reads state"),
  isOutput: z.boolean().optional().describe("An output parameter rather than an input (default false) (add_function_parameter). Edit the return side (default false) (edit_graph_parameters)"),
  keyframes: z.array(z.record(z.unknown())).optional().describe("[{time, value}]: value is a number for float and event, {x,y,z} for vector, {r,g,b,a} for color"),
  keywords: z.string().optional().describe("Extra palette search keywords"),
  kind: z.string().optional().describe("exec, data or all (default all)"),
  limit: z.number().int().optional().describe("Rows to return (default 200, max 5000) (audit_blueprint_dead_code). Rows to return on this page (default 200, max 1000) (get_blueprint_connections, search_blueprint_call_sites, search_blueprint_nodes). Rows to return on this page (default 200, max 2000) (list_blueprint_functions, list_blueprint_variables). Rows to return on this page (default 100, max 1000) (list_node_types). Nodes to return; omit for a safe default page, or dump the whole graph with dumpToFile (read_blueprint_graph). Rows to return on this page (default 50, max 500) (search_node_types)"),
  linkedNodeId: z.string().optional().describe("Break only links to this node (GUID or name). Omit to break every link on the pin"),
  linkedPinName: z.string().optional().describe("With linkedNodeId, break only the link to this pin"),
  location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Spawn location for the temporary actor"),
  macroName: z.string().optional().describe("Macro graph name"),
  materialPaths: z.array(z.string()).optional().describe("Material asset paths; an empty array clears"),
  maxAssets: z.number().int().optional().describe("Cap on Blueprints exported (default 200, max 5000)"),
  maxBlueprints: z.number().int().optional().describe("Cap on Blueprints loaded (default 2000)"),
  maxSamples: z.number().int().optional().describe("Samples listed per finding kind per Blueprint (default 20, max 500); counts stay complete"),
  metadata: z.record(z.unknown()).optional().describe("{key: 'value'} pairs; a null value removes that key. Unreal stores every metadata value as text"),
  name: z.string().optional().describe("Dispatcher name (add_event_dispatcher, remove_event_dispatcher). Variable name (add_local_variable, add_variable, delete_variable, edit_local_variable, get_blueprint_variable_default, get_blueprint_variable_metadata, set_blueprint_variable_metadata, set_variable_default, set_variable_properties). Asset name; with packagePath, the same destination as assetPath (create_blueprint)"),
  narrowByRegistry: z.boolean().optional().describe("Use Asset Registry dependencies to rule out Blueprints before loading them (default true)"),
  netMode: z.string().optional().describe("none (default), multicast, server or client"),
  newName: z.string().optional().describe("New name, for rename (edit_graph_parameters, edit_local_variable). New name (rename_blueprint_variable, rename_function)"),
  newParent: z.string().optional().describe("New parent component name"),
  nodeClass: z.string().optional().describe("Node class or short alias such as CallFunction, CallParent or CustomEvent"),
  nodeClasses: z.array(z.string()).optional().describe("Exact node class names, such as K2Node_VariableGet"),
  nodeId: z.string().optional().describe("Node GUID, as get_connections and find_nodes report it, or the node name (delete_node, disconnect_pins, read_node_property, refresh_node, set_node_position, set_node_property). Report only the edges into and out of this node (GUID or name) (get_blueprint_connections)"),
  nodeIds: z.array(z.string()).optional().describe("Node GUIDs or names to export (omit for the whole graph)"),
  nodeName: z.string().optional().describe("Alias for nodeId"),
  nodeParams: z.record(z.unknown()).optional().describe("Node-specific settings, such as functionName and className for a CallFunction node"),
  offset: z.number().int().optional().describe("Row offset into the (filtered) node list (read_blueprint_graph). Row offset, the older non-resumable form of paging (search_blueprint_call_sites)"),
  oldName: z.string().optional().describe("Current name"),
  onConflict: z.string().optional().describe("skip (default) reports the existing entry; error refuses"),
  op: z.string().optional().describe("add, remove, rename, set_type, set_default or reorder (edit_graph_parameters). rename, remove, set_type or set_default (edit_local_variable)"),
  order: z.array(z.unknown()).optional().describe("The COMPLETE desired order, never a partial list"),
  otherPath: z.string().optional().describe("Blueprint to compare against (B)"),
  outputDir: z.string().optional().describe("Absolute directory, or one relative to Saved/ (default Saved/UE_MCP/BlueprintExport)"),
  outputPath: z.string().optional().describe("Absolute or Saved-relative JSON path for the dump"),
  outputs: z.array(z.record(z.unknown())).optional().describe("Output parameters [{name, type}]"),
  packagePath: z.string().optional().describe("Destination folder, used with name"),
  parameterName: z.string().optional().describe("Parameter name (add_function_parameter). Parameter to act on (edit_graph_parameters)"),
  parameters: z.array(z.record(z.unknown())).optional().describe("Typed signature parameters [{name, type}]; type takes the add_variable vocabulary, containers included"),
  parameterType: z.string().optional().describe("Parameter type in the add_variable vocabulary, containers included (default float) (add_function_parameter). Type in the add_variable vocabulary, for add and set_type (edit_graph_parameters)"),
  parentClass: z.string().optional().describe("Parent class: short name or full path (default Actor) (create_blueprint). Only Blueprints deriving from this class (export_blueprint_batch). New parent class: short name or full path (reparent_blueprint)"),
  parentComponent: z.string().optional().describe("SCS parent component for the hierarchy"),
  path: z.string().optional().describe("Alias for assetPath"),
  pinName: z.string().optional().describe("The pin whose links are broken (disconnect_pins). Alias for propertyName (read_node_property, set_node_property)"),
  posX: z.number().optional().describe("Node X position in the graph (add_custom_event, set_node_position). Re-center pasted nodes around this X (with posY) (import_nodes_t3d)"),
  posY: z.number().optional().describe("Node Y position in the graph (add_custom_event, set_node_position). Re-center pasted nodes around this Y (with posX) (import_nodes_t3d)"),
  preferFunction: z.boolean().optional().describe("Force the function-graph form even when the function could be placed as an override event"),
  preserveFunctions: z.boolean().optional().describe("Keep the implementations as ordinary Blueprint functions (default false)"),
  previousPositionsLimit: z.number().int().optional().describe("Capture each node's pre-layout coordinates only when the graph has at most this many nodes (default 200)"),
  private: z.boolean().optional().describe("The Blueprint editor's Private checkbox, independent of editFlag"),
  profileName: z.string().optional().describe("Collision profile to resolve, e.g. Pawn, BlockAll, or one the project defined"),
  propertyName: z.string().optional().describe("Property name (get_blueprint_component_property, set_blueprint_component_property, set_cdo_property, set_class_default). Pin or reflected node property name (read_node_property). Pin or struct property name (set_node_property)"),
  propertyNames: z.array(z.string()).optional().describe("Property names to read (omit for all)"),
  pure: z.boolean().optional().describe("BlueprintPure: no side effects, and the node loses its exec pins"),
  query: z.string().optional().describe("Words to match against the C++ name, the palette label and Keywords metadata"),
  radius: z.number().optional().describe("Capsule radius (unscaled)"),
  recursive: z.boolean().optional().describe("Include subfolders of directory (default true)"),
  reliable: z.boolean().optional().describe("Send the replicated event reliably (default true; ignored when netMode is none)"),
  reverse: z.boolean().optional().describe("true reports the referencers of this asset instead"),
  rowGap: z.number().int().optional().describe("Vertical spacing between rows (default 200)"),
  save: z.boolean().optional().describe("Persist on success (default true)"),
  scanReferencers: z.boolean().optional().describe("Also scan every Blueprint package that depends on an audited one for references (default true)"),
  source: z.string().optional().describe("Advisory hint for where the function comes from: auto (default), interface or parent. Echoed back"),
  sourceNode: z.string().optional().describe("Alias for sourceNodeId"),
  sourceNodeId: z.string().optional().describe("Source node GUID, as get_connections and find_nodes report it, or its title"),
  sourcePath: z.string().optional().describe("Blueprint asset to duplicate"),
  sourcePin: z.string().optional().describe("Source pin name"),
  sourcePinName: z.string().optional().describe("Alias for sourcePin"),
  t3d: z.string().optional().describe("T3D blob from export_nodes_t3d, or copied from the Blueprint graph editor"),
  targetNode: z.string().optional().describe("Alias for targetNodeId"),
  targetNodeId: z.string().optional().describe("Target node GUID, as get_connections and find_nodes report it, or its title"),
  targetPin: z.string().optional().describe("Target pin name"),
  targetPinName: z.string().optional().describe("Alias for targetPin"),
  text: z.string().optional().describe("Alias for t3d"),
  threadSafe: z.boolean().optional().describe("Safe to call off the game thread"),
  TickInterval: z.number().optional().describe("PrimaryActorTick.TickInterval in seconds"),
  timelineName: z.string().optional().describe("Timeline name"),
  titleFilter: z.string().optional().describe("Case-insensitive substring match on node title"),
  titles: z.array(z.string()).optional().describe("Case-insensitive substrings matched against each node title"),
  tooltip: z.string().optional().describe("Tooltip"),
  toRevision: z.string().optional().describe("Reserved for source-control revision diffing, which is not implemented; passing it is refused"),
  trackName: z.string().optional().describe("Track name within the timeline"),
  trackType: z.string().optional().describe("float (default), vector, color or event"),
  type: z.string().optional().describe("Alias for varType"),
  value: z.unknown().optional().describe("Value to write: a JSON value, or Unreal export text. null clears an object, class or interface reference (set_blueprint_component_property, set_cdo_property, set_class_default). New pin default or property value, as a string (set_node_property). New default value, as a string (set_variable_default)"),
  variableAccess: z.string().optional().describe("Narrow variableName to get, set or any (default any)"),
  variableName: z.string().optional().describe("The member a node reads or writes"),
  varType: z.string().optional().describe("Variable type: bool, int, float, string, name, text, byte, vector, rotator, transform, object:/Script/Module.Class, struct:/Game/Path, enum:/Game/Path.Enum, or a container over any of those (Type[], set<Type>, map<Key,Value>)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
