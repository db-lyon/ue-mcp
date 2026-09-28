// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd statetree handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_state_tree_binding": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
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
    ],
    "commit": "none"
  },
  "add_state_tree_color": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
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
    ],
    "commit": "none"
  },
  "add_state_tree_consideration": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "add_state_tree_enter_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "add_state_tree_evaluator": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
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
    ],
    "commit": "none"
  },
  "add_state_tree_global_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
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
    ],
    "commit": "none"
  },
  "add_state_tree_state": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "Parent state GUID (omit both parent selectors for a root state)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Parent state dot-path, as an alternative to stateId",
        "role": "nodeRef"
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
        "description": "Position among the new state's siblings (default: append)",
        "role": "slotIndex"
      },
      {
        "name": "linkedSubtree",
        "type": "string",
        "required": false,
        "description": "StateTree ASSET path assigned to LinkedAsset. To link a Subtree state inside this asset, use set_state_link with linkType=subtree"
      }
    ],
    "commit": "none"
  },
  "add_state_tree_state_parameter": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "add_state_tree_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "add_state_tree_transition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
        "description": "Target state GUID for GotoState transitions",
        "role": "nodeRef"
      },
      {
        "name": "targetStatePath",
        "type": "string",
        "required": false,
        "description": "Target state dot-path for GotoState transitions",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "add_state_tree_transition_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "transitionIndex",
        "type": "integer",
        "required": true,
        "description": "Transition index within the state",
        "role": "slotIndex"
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
    ],
    "commit": "none"
  },
  "clear_state_tree_state_nodes": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      }
    ],
    "commit": "none"
  },
  "compile_state_tree": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      }
    ],
    "commit": "both"
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
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "newParentStateId",
        "type": "string",
        "required": false,
        "description": "GUID of the state to move under",
        "role": "nodeRef"
      },
      {
        "name": "newParentStatePath",
        "type": "string",
        "required": false,
        "description": "Dot-path of the state to move under, as an alternative to newParentStateId",
        "role": "nodeRef"
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
        "description": "Position among the new state's siblings (default: append)",
        "role": "slotIndex"
      }
    ],
    "commit": "none"
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
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
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
    ],
    "commit": "none"
  },
  "remove_state_tree_consideration": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "considerationIndex",
        "type": "integer",
        "required": true,
        "description": "Index of the consideration within the state, as read_state reports it",
        "role": "slotIndex"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_enter_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "conditionIndex",
        "type": "integer",
        "required": true,
        "description": "Condition index within the list, as read_state reports it",
        "role": "slotIndex"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_evaluator": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the evaluator node",
        "role": "nodeRef"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_global_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the global task node",
        "role": "nodeRef"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_state": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_state_parameter": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "paramName",
        "type": "string",
        "required": true,
        "description": "Parameter name"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_task": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "taskIndex",
        "type": "integer",
        "required": true,
        "description": "Task index within the state",
        "role": "slotIndex"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_transition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "transitionIndex",
        "type": "integer",
        "required": true,
        "description": "Transition index within the state",
        "role": "slotIndex"
      }
    ],
    "commit": "none"
  },
  "remove_state_tree_transition_condition": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "transitionIndex",
        "type": "integer",
        "required": true,
        "description": "Transition index within the state",
        "role": "slotIndex"
      },
      {
        "name": "conditionIndex",
        "type": "integer",
        "required": true,
        "description": "Condition index within the list, as read_state reports it",
        "role": "slotIndex"
      }
    ],
    "commit": "none"
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
        "description": "GUID of the state to transition to (or pass targetStateTag)",
        "role": "nodeRef"
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
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the evaluator node",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "set_state_tree_evaluator_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the evaluator node",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "set_state_tree_global_task_instance_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the global task node",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "set_state_tree_global_task_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the global task node",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "set_state_tree_node_class": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "GUID of the Blueprint node wrapper, as every add_* action returns it",
        "role": "nodeRef"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": true,
        "description": "The Blueprint class this node runs, as a class path or a loaded class name"
      }
    ],
    "commit": "none"
  },
  "set_state_tree_root_parameters": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "parameters",
        "type": "array",
        "required": true,
        "description": "Root parameter definitions [{name, type}] where type is float, int32, bool, string, name or double",
        "items": "object"
      }
    ],
    "commit": "none"
  },
  "set_state_tree_schema": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "schema",
        "type": "string",
        "required": false,
        "description": "Schema class path, e.g. /Script/GameplayStateTreeModule.StateTreeComponentSchema (default: the first concrete schema this editor has)"
      }
    ],
    "commit": "both"
  },
  "set_state_tree_state_link": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
        "description": "linkType=subtree: GUID of the Subtree state to link to",
        "role": "nodeRef"
      },
      {
        "name": "targetStatePath",
        "type": "string",
        "required": false,
        "description": "linkType=subtree: dot-path of the Subtree state to link to",
        "role": "nodeRef"
      },
      {
        "name": "linkedAsset",
        "type": "string",
        "required": false,
        "description": "linkType=asset: the StateTree asset this state runs"
      }
    ],
    "commit": "none"
  },
  "set_state_tree_state_parameter": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "set_state_tree_state_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
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
    ],
    "commit": "none"
  },
  "set_state_tree_task_instance_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "taskIndex",
        "type": "integer",
        "required": true,
        "description": "Task index within the state",
        "role": "slotIndex"
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
    ],
    "commit": "none"
  },
  "set_state_tree_task_property": {
    "category": "statetree",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StateTree asset path, e.g. /Game/Path/To/ST_Asset",
        "role": "editTarget"
      },
      {
        "name": "stateId",
        "type": "string",
        "required": false,
        "description": "GUID of the target state (or pass statePath)",
        "role": "nodeRef"
      },
      {
        "name": "statePath",
        "type": "string",
        "required": false,
        "description": "Dot-path to the target state, as an alternative to stateId",
        "role": "nodeRef"
      },
      {
        "name": "taskIndex",
        "type": "integer",
        "required": true,
        "description": "Task index within the state",
        "role": "slotIndex"
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
    ],
    "commit": "none"
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

/** Every key the spec'd statetree handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
