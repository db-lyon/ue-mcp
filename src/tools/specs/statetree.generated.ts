// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd statetree handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_state_tree_binding": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "sourceStructId",
        "type": "string",
        "required": true,
        "description": "Source struct GUID, as list_bindable_sources reports it"
      },
      {
        "name": "sourcePath",
        "type": "string",
        "required": true,
        "description": "Source property path"
      },
      {
        "name": "targetStructId",
        "type": "string",
        "required": true,
        "description": "Target struct GUID"
      },
      {
        "name": "targetPath",
        "type": "string",
        "required": true,
        "description": "Target property path"
      }
    ]
  },
  "add_state_tree_color": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "displayName",
        "type": "string",
        "required": true,
        "description": "Display name for the new palette entry"
      },
      {
        "name": "color",
        "type": "string",
        "required": false,
        "description": "FLinearColor string, e.g. (R=1.0,G=0.0,B=0.0,A=1.0)"
      }
    ]
  },
  "add_state_tree_consideration": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "structType",
        "type": "string",
        "required": true,
        "description": "C++ struct name of the consideration"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Key-value map of instance data properties to set on the new node"
      },
      {
        "name": "operand",
        "type": "string",
        "required": false,
        "description": "Expression operand for conditions: And or Or"
      }
    ]
  },
  "add_state_tree_enter_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "structType",
        "type": "string",
        "required": true,
        "description": "C++ struct name of the condition"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Key-value map of instance data properties to set on the new node"
      },
      {
        "name": "operand",
        "type": "string",
        "required": false,
        "description": "Expression operand for conditions: And or Or"
      }
    ]
  },
  "add_state_tree_evaluator": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "structType",
        "type": "string",
        "required": true,
        "description": "C++ struct name deriving from FStateTreeEvaluatorBase"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Key-value map of instance data properties to set on the new node"
      }
    ]
  },
  "add_state_tree_global_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "structType",
        "type": "string",
        "required": true,
        "description": "C++ struct name deriving from FStateTreeTaskBase"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Key-value map of instance data properties to set on the new node"
      }
    ]
  },
  "add_state_tree_state": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "Parent state GUID (omit both parent selectors for a root state)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Parent state dot-path, as an alternative to stateId"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Name for the new state"
      },
      {
        "name": "stateType",
        "type": "string",
        "required": false,
        "description": "State type: State, Group, LinkedAsset, Subtree"
      },
      {
        "name": "selectionBehavior",
        "type": "string",
        "required": false,
        "description": "Selection behavior: TryEnterState, TrySelectChildrenInOrder, TrySelectChildrenAtRandom, etc."
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Position among the new state's siblings (default: append)"
      },
      {
        "name": "linkedSubtree",
        "type": "string",
        "required": false,
        "description": "StateTree ASSET path assigned to LinkedAsset. To link a Subtree state inside this asset, use set_state_link with linkType=subtree"
      }
    ]
  },
  "add_state_tree_state_parameter": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "paramName",
        "type": "string",
        "required": true,
        "description": "Parameter name"
      },
      {
        "name": "paramType",
        "type": "string",
        "required": true,
        "description": "Bool, Int32, Int64, Float, Double, Name, String or Text"
      }
    ]
  },
  "add_state_tree_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "structType",
        "type": "string",
        "required": true,
        "description": "C++ struct name, e.g. FMyStateTreeTask or an engine task like FStateTreeRunParallelStateTreesTask"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Key-value map of instance data properties to set on the new node"
      }
    ]
  },
  "add_state_tree_transition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "trigger",
        "type": "string",
        "required": true,
        "description": "OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick or OnEvent; combine with | e.g. OnStateSucceeded|OnStateFailed"
      },
      {
        "name": "transitionType",
        "type": "string",
        "required": true,
        "description": "GotoState, NextState, NextSelectableState, Succeeded or Failed"
      },
      {
        "name": "eventTag",
        "type": "string",
        "required": false,
        "description": "Gameplay tag for OnEvent transitions, e.g. MyGame.SomeEvent"
      },
      {
        "name": "targetStateId",
        "type": "string",
        "required": false,
        "description": "Target state GUID for GotoState transitions"
      },
      {
        "name": "targetStatePath",
        "type": "string",
        "required": false,
        "description": "Target state dot-path for GotoState transitions"
      },
      {
        "name": "priority",
        "type": "string",
        "required": false,
        "description": "Low, Normal, Medium, High or Critical"
      },
      {
        "name": "delayDuration",
        "type": "number",
        "required": false,
        "description": "Transition delay in seconds"
      },
      {
        "name": "bDelayTransition",
        "type": "boolean",
        "required": false,
        "description": "Enable transition delay"
      }
    ]
  },
  "add_state_tree_transition_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "transitionIndex",
        "type": "integer",
        "required": true,
        "description": "Transition index within the state"
      },
      {
        "name": "structType",
        "type": "string",
        "required": true,
        "description": "C++ struct name of the condition"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Key-value map of instance data properties to set on the new node"
      },
      {
        "name": "operand",
        "type": "string",
        "required": false,
        "description": "Expression operand for conditions: And or Or"
      }
    ]
  },
  "clear_state_tree_state_nodes": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      }
    ]
  },
  "compile_state_tree": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      }
    ]
  },
  "list_state_tree_bindable_sources": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
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
  "list_state_tree_bindings": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "structId",
        "type": "string",
        "required": false,
        "description": "Only bindings whose source or target is this struct GUID"
      }
    ]
  },
  "list_state_tree_colors": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      }
    ]
  },
  "list_state_tree_node_types": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeType",
        "type": "string",
        "required": false,
        "description": "task, condition, evaluator, consideration, or all (default)"
      },
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Only structs and classes whose name contains this substring"
      },
      {
        "name": "includeInstanceProperties",
        "type": "boolean",
        "required": false,
        "description": "Include each node type's instance-data property list (default true)"
      },
      {
        "name": "schemaAllowedOnly",
        "type": "boolean",
        "required": false,
        "description": "Only node types this tree's schema permits (default true)"
      }
    ]
  },
  "list_state_tree_state_parameters": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      }
    ]
  },
  "list_state_tree_states": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
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
  "move_state_tree_state": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "newParentStateId",
        "type": "string",
        "required": false,
        "description": "GUID of the state to move under"
      },
      {
        "name": "newParentStatePath",
        "type": "string",
        "required": false,
        "description": "Dot-path of the state to move under, as an alternative to newParentStateId"
      },
      {
        "name": "toRoot",
        "type": "boolean",
        "required": false,
        "description": "Move the state to the top level of the asset instead of under a parent"
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Position among the new state's siblings (default: append)"
      }
    ]
  },
  "read_state_tree": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      }
    ]
  },
  "read_state_tree_runtime": {
    "category": "statetree",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor carrying the running StateTree component (or pass actorPath)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; unambiguous where a label is not"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Which StateTree component, when the actor has more than one"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "pie (default), auto, or editor"
      },
      {
        "name": "pieInstance",
        "type": "integer",
        "required": false,
        "description": "Which PIE instance, when several are running"
      },
      {
        "name": "includeDebugStrings",
        "type": "boolean",
        "required": false,
        "description": "Include the engine's own debug dump of the execution state"
      }
    ]
  },
  "read_state_tree_state": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      }
    ]
  },
  "remove_state_tree_binding": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "targetStructId",
        "type": "string",
        "required": true,
        "description": "Target struct GUID"
      },
      {
        "name": "targetPath",
        "type": "string",
        "required": true,
        "description": "Target property path"
      }
    ]
  },
  "remove_state_tree_consideration": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "considerationIndex",
        "type": "integer",
        "required": true,
        "description": "Index of the consideration within the state, as read_state reports it"
      }
    ]
  },
  "remove_state_tree_enter_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "conditionIndex",
        "type": "integer",
        "required": true,
        "description": "Condition index within the list, as read_state reports it"
      }
    ]
  },
  "remove_state_tree_evaluator": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the evaluator node"
      }
    ]
  },
  "remove_state_tree_global_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the global task node"
      }
    ]
  },
  "remove_state_tree_state": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      }
    ]
  },
  "remove_state_tree_state_parameter": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "paramName",
        "type": "string",
        "required": true,
        "description": "Parameter name"
      }
    ]
  },
  "remove_state_tree_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "taskIndex",
        "type": "integer",
        "required": true,
        "description": "Task index within the state"
      }
    ]
  },
  "remove_state_tree_transition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "transitionIndex",
        "type": "integer",
        "required": true,
        "description": "Transition index within the state"
      }
    ]
  },
  "remove_state_tree_transition_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "transitionIndex",
        "type": "integer",
        "required": true,
        "description": "Transition index within the state"
      },
      {
        "name": "conditionIndex",
        "type": "integer",
        "required": true,
        "description": "Condition index within the list, as read_state reports it"
      }
    ]
  },
  "request_state_tree_transition": {
    "category": "statetree",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor carrying the running StateTree component (or pass actorPath)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; unambiguous where a label is not"
      },
      {
        "name": "targetStateId",
        "type": "string",
        "required": false,
        "description": "GUID of the state to transition to (or pass targetStateTag)"
      },
      {
        "name": "targetStateTag",
        "type": "string",
        "required": false,
        "description": "Tag of the state to transition to, UE 5.8 and later"
      },
      {
        "name": "priority",
        "type": "string",
        "required": false,
        "description": "Low, Normal, Medium, High or Critical"
      },
      {
        "name": "fallback",
        "type": "string",
        "required": false,
        "description": "None (default) or NextSelectableSibling"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Which StateTree component, when the actor has more than one"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "pie (default), auto, or editor"
      },
      {
        "name": "pieInstance",
        "type": "integer",
        "required": false,
        "description": "Which PIE instance, when several are running"
      }
    ]
  },
  "send_state_tree_event": {
    "category": "statetree",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor carrying the running StateTree component (or pass actorPath)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; unambiguous where a label is not"
      },
      {
        "name": "eventTag",
        "type": "string",
        "required": true,
        "description": "Registered gameplay tag of the event"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Which StateTree component, when the actor has more than one"
      },
      {
        "name": "origin",
        "type": "string",
        "required": false,
        "description": "Who the event says it came from (default ue-mcp)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "pie (default), auto, or editor"
      },
      {
        "name": "pieInstance",
        "type": "integer",
        "required": false,
        "description": "Which PIE instance, when several are running"
      }
    ]
  },
  "set_state_tree_evaluator_instance_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the evaluator node"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "set_state_tree_evaluator_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the evaluator node"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "set_state_tree_global_task_instance_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the global task node"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "set_state_tree_global_task_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the global task node"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "set_state_tree_node_class": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the Blueprint node wrapper, as every add_* action returns it"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": true,
        "description": "The Blueprint class this node runs, as a class path or a loaded class name"
      }
    ]
  },
  "set_state_tree_root_parameters": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "parameters",
        "type": "array",
        "required": true,
        "description": "Root parameter definitions [{name, type}] where type is float, int32, bool, string, name or double",
        "items": "object"
      }
    ]
  },
  "set_state_tree_schema": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "schema",
        "type": "string",
        "required": false,
        "description": "Schema class path, e.g. /Script/GameplayStateTreeModule.StateTreeComponentSchema (default: the first concrete schema this editor has)"
      }
    ]
  },
  "set_state_tree_state_link": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "linkType",
        "type": "string",
        "required": true,
        "description": "subtree (a Subtree state in this asset), asset (another StateTree asset), or none (clear both)"
      },
      {
        "name": "targetStateId",
        "type": "string",
        "required": false,
        "description": "linkType=subtree: GUID of the Subtree state to link to"
      },
      {
        "name": "targetStatePath",
        "type": "string",
        "required": false,
        "description": "linkType=subtree: dot-path of the Subtree state to link to"
      },
      {
        "name": "linkedAsset",
        "type": "string",
        "required": false,
        "description": "linkType=asset: the StateTree asset this state runs"
      }
    ]
  },
  "set_state_tree_state_parameter": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "paramName",
        "type": "string",
        "required": true,
        "description": "Parameter name"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "set_state_tree_state_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set: name, type, selectionBehavior, bEnabled, weight, linkedAsset, description, tag, customTickRate or color"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string. tag: gameplay tag or empty to clear. customTickRate: seconds or empty to disable. color: palette display name, GUID, or empty to clear"
      }
    ]
  },
  "set_state_tree_task_instance_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "taskIndex",
        "type": "integer",
        "required": true,
        "description": "Task index within the state"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "set_state_tree_task_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId"
      },
      {
        "name": "taskIndex",
        "type": "integer",
        "required": true,
        "description": "Task index within the state"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property to set"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Property value as a string"
      }
    ]
  },
  "validate_state_tree": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_state_tree_binding: "Params: assetPath, sourceStructId, sourcePath, targetStructId, targetPath",
  add_state_tree_color: "Params: assetPath, displayName, color?",
  add_state_tree_consideration: "Params: assetPath, stateId?, statePath?, structType, instanceProperties?, operand?",
  add_state_tree_enter_condition: "Params: assetPath, stateId?, statePath?, structType, instanceProperties?, operand?",
  add_state_tree_evaluator: "Params: assetPath, structType, instanceProperties?",
  add_state_tree_global_task: "Params: assetPath, structType, instanceProperties?",
  add_state_tree_state: "Params: assetPath, stateId?, statePath?, name, stateType?, selectionBehavior?, insertIndex?, linkedSubtree?",
  add_state_tree_state_parameter: "Params: assetPath, stateId?, statePath?, paramName, paramType",
  add_state_tree_task: "Params: assetPath, stateId?, statePath?, structType, instanceProperties?",
  add_state_tree_transition: "Params: assetPath, stateId?, statePath?, trigger, transitionType, eventTag?, targetStateId?, targetStatePath?, priority?, delayDuration?, bDelayTransition?",
  add_state_tree_transition_condition: "Params: assetPath, stateId?, statePath?, transitionIndex, structType, instanceProperties?, operand?",
  clear_state_tree_state_nodes: "Params: assetPath, stateId?, statePath?",
  compile_state_tree: "Params: assetPath",
  list_state_tree_bindable_sources: "Params: assetPath, cursor?, limit?",
  list_state_tree_bindings: "Params: assetPath, structId?",
  list_state_tree_colors: "Params: assetPath",
  list_state_tree_node_types: "Params: assetPath, nodeType?, filter?, includeInstanceProperties?, schemaAllowedOnly?",
  list_state_tree_state_parameters: "Params: assetPath, stateId?, statePath?",
  list_state_tree_states: "Params: assetPath, cursor?, limit?",
  move_state_tree_state: "Params: assetPath, stateId?, statePath?, newParentStateId?, newParentStatePath?, toRoot?, insertIndex?",
  read_state_tree: "Params: assetPath",
  read_state_tree_runtime: "Params: actorLabel?, actorPath?, componentName?, world?, pieInstance?, includeDebugStrings?",
  read_state_tree_state: "Params: assetPath, stateId?, statePath?",
  remove_state_tree_binding: "Params: assetPath, targetStructId, targetPath",
  remove_state_tree_consideration: "Params: assetPath, stateId?, statePath?, considerationIndex",
  remove_state_tree_enter_condition: "Params: assetPath, stateId?, statePath?, conditionIndex",
  remove_state_tree_evaluator: "Params: assetPath, nodeId",
  remove_state_tree_global_task: "Params: assetPath, nodeId",
  remove_state_tree_state: "Params: assetPath, stateId?, statePath?",
  remove_state_tree_state_parameter: "Params: assetPath, stateId?, statePath?, paramName",
  remove_state_tree_task: "Params: assetPath, stateId?, statePath?, taskIndex",
  remove_state_tree_transition: "Params: assetPath, stateId?, statePath?, transitionIndex",
  remove_state_tree_transition_condition: "Params: assetPath, stateId?, statePath?, transitionIndex, conditionIndex",
  request_state_tree_transition: "Params: actorLabel?, actorPath?, targetStateId?, targetStateTag?, priority?, fallback?, componentName?, world?, pieInstance?",
  send_state_tree_event: "Params: actorLabel?, actorPath?, eventTag, componentName?, origin?, world?, pieInstance?",
  set_state_tree_evaluator_instance_property: "Params: assetPath, nodeId, propertyName, value",
  set_state_tree_evaluator_property: "Params: assetPath, nodeId, propertyName, value",
  set_state_tree_global_task_instance_property: "Params: assetPath, nodeId, propertyName, value",
  set_state_tree_global_task_property: "Params: assetPath, nodeId, propertyName, value",
  set_state_tree_node_class: "Params: assetPath, nodeId, nodeClass",
  set_state_tree_root_parameters: "Params: assetPath, parameters",
  set_state_tree_schema: "Params: assetPath, schema?",
  set_state_tree_state_link: "Params: assetPath, stateId?, statePath?, linkType, targetStateId?, targetStatePath?, linkedAsset?",
  set_state_tree_state_parameter: "Params: assetPath, stateId?, statePath?, paramName, value",
  set_state_tree_state_property: "Params: assetPath, stateId?, statePath?, propertyName, value",
  set_state_tree_task_instance_property: "Params: assetPath, stateId?, statePath?, taskIndex, propertyName, value",
  set_state_tree_task_property: "Params: assetPath, stateId?, statePath?, taskIndex, propertyName, value",
  validate_state_tree: "Params: assetPath",
};

/** Every key the spec'd statetree handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("Label of the actor carrying the running StateTree component (or pass actorPath)"),
  actorPath: z.string().optional().describe("Full object path of that actor; unambiguous where a label is not"),
  assetPath: z.string().optional().describe("StateTree asset path, e.g. /Game/Path/To/ST_Asset"),
  bDelayTransition: z.boolean().optional().describe("Enable transition delay"),
  color: z.string().optional().describe("FLinearColor string, e.g. (R=1.0,G=0.0,B=0.0,A=1.0)"),
  componentName: z.string().optional().describe("Which StateTree component, when the actor has more than one"),
  conditionIndex: z.number().int().optional().describe("Condition index within the list, as read_state reports it"),
  considerationIndex: z.number().int().optional().describe("Index of the consideration within the state, as read_state reports it"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  delayDuration: z.number().optional().describe("Transition delay in seconds"),
  displayName: z.string().optional().describe("Display name for the new palette entry"),
  eventTag: z.string().optional().describe("Gameplay tag for OnEvent transitions, e.g. MyGame.SomeEvent (add_state_tree_transition). Registered gameplay tag of the event (send_state_tree_event)"),
  fallback: z.string().optional().describe("None (default) or NextSelectableSibling"),
  filter: z.string().optional().describe("Only structs and classes whose name contains this substring"),
  includeDebugStrings: z.boolean().optional().describe("Include the engine's own debug dump of the execution state"),
  includeInstanceProperties: z.boolean().optional().describe("Include each node type's instance-data property list (default true)"),
  insertIndex: z.number().int().optional().describe("Position among the new state's siblings (default: append)"),
  instanceProperties: z.record(z.unknown()).optional().describe("Key-value map of instance data properties to set on the new node"),
  limit: z.number().int().optional().describe("Rows to return on this page (default 200, max 2000)"),
  linkedAsset: z.string().optional().describe("linkType=asset: the StateTree asset this state runs"),
  linkedSubtree: z.string().optional().describe("StateTree ASSET path assigned to LinkedAsset. To link a Subtree state inside this asset, use set_state_link with linkType=subtree"),
  linkType: z.string().optional().describe("subtree (a Subtree state in this asset), asset (another StateTree asset), or none (clear both)"),
  name: z.string().optional().describe("Name for the new state"),
  newParentStateId: z.string().optional().describe("GUID of the state to move under"),
  newParentStatePath: z.string().optional().describe("Dot-path of the state to move under, as an alternative to newParentStateId"),
  nodeClass: z.string().optional().describe("The Blueprint class this node runs, as a class path or a loaded class name"),
  nodeId: z.string().optional().describe("GUID of the evaluator node (remove_state_tree_evaluator, set_state_tree_evaluator_instance_property, set_state_tree_evaluator_property). GUID of the global task node (remove_state_tree_global_task, set_state_tree_global_task_instance_property, set_state_tree_global_task_property). GUID of the Blueprint node wrapper, as every add_* action returns it (set_state_tree_node_class)"),
  nodeType: z.string().optional().describe("task, condition, evaluator, consideration, or all (default)"),
  operand: z.string().optional().describe("Expression operand for conditions: And or Or"),
  origin: z.string().optional().describe("Who the event says it came from (default ue-mcp)"),
  parameters: z.array(z.record(z.unknown())).optional().describe("Root parameter definitions [{name, type}] where type is float, int32, bool, string, name or double"),
  paramName: z.string().optional().describe("Parameter name"),
  paramType: z.string().optional().describe("Bool, Int32, Int64, Float, Double, Name, String or Text"),
  pieInstance: z.number().int().optional().describe("Which PIE instance, when several are running"),
  priority: z.string().optional().describe("Low, Normal, Medium, High or Critical"),
  propertyName: z.string().optional().describe("Property to set (set_state_tree_evaluator_instance_property, set_state_tree_evaluator_property, set_state_tree_global_task_instance_property, set_state_tree_global_task_property, set_state_tree_task_instance_property, set_state_tree_task_property). Property to set: name, type, selectionBehavior, bEnabled, weight, linkedAsset, description, tag, customTickRate or color (set_state_tree_state_property)"),
  schema: z.string().optional().describe("Schema class path, e.g. /Script/GameplayStateTreeModule.StateTreeComponentSchema (default: the first concrete schema this editor has)"),
  schemaAllowedOnly: z.boolean().optional().describe("Only node types this tree's schema permits (default true)"),
  selectionBehavior: z.string().optional().describe("Selection behavior: TryEnterState, TrySelectChildrenInOrder, TrySelectChildrenAtRandom, etc."),
  sourcePath: z.string().optional().describe("Source property path"),
  sourceStructId: z.string().optional().describe("Source struct GUID, as list_bindable_sources reports it"),
  stateId: z.string().optional().describe("GUID of the target state (or pass statePath) (add_state_tree_consideration, add_state_tree_enter_condition, add_state_tree_state_parameter, add_state_tree_task, add_state_tree_transition, add_state_tree_transition_condition, clear_state_tree_state_nodes, list_state_tree_state_parameters, move_state_tree_state, read_state_tree_state, remove_state_tree_consideration, remove_state_tree_enter_condition, remove_state_tree_state, remove_state_tree_state_parameter, remove_state_tree_task, remove_state_tree_transition, remove_state_tree_transition_condition, set_state_tree_state_link, set_state_tree_state_parameter, set_state_tree_state_property, set_state_tree_task_instance_property, set_state_tree_task_property). Parent state GUID (omit both parent selectors for a root state) (add_state_tree_state)"),
  statePath: z.string().optional().describe("Dot-path to the target state, as an alternative to stateId (add_state_tree_consideration, add_state_tree_enter_condition, add_state_tree_state_parameter, add_state_tree_task, add_state_tree_transition, add_state_tree_transition_condition, clear_state_tree_state_nodes, list_state_tree_state_parameters, move_state_tree_state, read_state_tree_state, remove_state_tree_consideration, remove_state_tree_enter_condition, remove_state_tree_state, remove_state_tree_state_parameter, remove_state_tree_task, remove_state_tree_transition, remove_state_tree_transition_condition, set_state_tree_state_link, set_state_tree_state_parameter, set_state_tree_state_property, set_state_tree_task_instance_property, set_state_tree_task_property). Parent state dot-path, as an alternative to stateId (add_state_tree_state)"),
  stateType: z.string().optional().describe("State type: State, Group, LinkedAsset, Subtree"),
  structId: z.string().optional().describe("Only bindings whose source or target is this struct GUID"),
  structType: z.string().optional().describe("C++ struct name of the consideration (add_state_tree_consideration). C++ struct name of the condition (add_state_tree_enter_condition, add_state_tree_transition_condition). C++ struct name deriving from FStateTreeEvaluatorBase (add_state_tree_evaluator). C++ struct name deriving from FStateTreeTaskBase (add_state_tree_global_task). C++ struct name, e.g. FMyStateTreeTask or an engine task like FStateTreeRunParallelStateTreesTask (add_state_tree_task)"),
  targetPath: z.string().optional().describe("Target property path"),
  targetStateId: z.string().optional().describe("Target state GUID for GotoState transitions (add_state_tree_transition). GUID of the state to transition to (or pass targetStateTag) (request_state_tree_transition). linkType=subtree: GUID of the Subtree state to link to (set_state_tree_state_link)"),
  targetStatePath: z.string().optional().describe("Target state dot-path for GotoState transitions (add_state_tree_transition). linkType=subtree: dot-path of the Subtree state to link to (set_state_tree_state_link)"),
  targetStateTag: z.string().optional().describe("Tag of the state to transition to, UE 5.8 and later"),
  targetStructId: z.string().optional().describe("Target struct GUID"),
  taskIndex: z.number().int().optional().describe("Task index within the state"),
  toRoot: z.boolean().optional().describe("Move the state to the top level of the asset instead of under a parent"),
  transitionIndex: z.number().int().optional().describe("Transition index within the state"),
  transitionType: z.string().optional().describe("GotoState, NextState, NextSelectableState, Succeeded or Failed"),
  trigger: z.string().optional().describe("OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick or OnEvent; combine with | e.g. OnStateSucceeded|OnStateFailed"),
  value: z.string().optional().describe("Property value as a string (set_state_tree_evaluator_instance_property, set_state_tree_evaluator_property, set_state_tree_global_task_instance_property, set_state_tree_global_task_property, set_state_tree_state_parameter, set_state_tree_task_instance_property, set_state_tree_task_property). Property value as a string. tag: gameplay tag or empty to clear. customTickRate: seconds or empty to disable. color: palette display name, GUID, or empty to clear (set_state_tree_state_property)"),
  world: z.string().optional().describe("pie (default), auto, or editor"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
