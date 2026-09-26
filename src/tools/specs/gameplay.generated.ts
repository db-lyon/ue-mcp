// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../handler-spec.js";

/** The recorded contract of every spec'd gameplay handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_blackboard_key": {
    "category": "gameplay",
    "params": [
      {
        "name": "blackboardPath",
        "type": "string",
        "required": true,
        "description": "BlackboardData asset path"
      },
      {
        "name": "keyName",
        "type": "string",
        "required": true,
        "description": "Key name to add"
      },
      {
        "name": "keyType",
        "type": "string",
        "required": false,
        "description": "Bool (default) | Int | Float | String | Name | Vector | Rotator | Object | Class | Enum"
      },
      {
        "name": "baseClass",
        "type": "string",
        "required": false,
        "description": "Base class for an Object/Class key (e.g. /Script/Engine.Actor); for an Enum key, the enum when enumType is absent"
      },
      {
        "name": "enumType",
        "type": "string",
        "required": false,
        "description": "Enum name or path for keyType=Enum"
      }
    ]
  },
  "add_bt_node": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": true,
        "description": "Concrete BT node class to place, e.g. BTComposite_Selector or BTTask_MoveTo; list_bt_node_classes enumerates them"
      },
      {
        "name": "nodeCategory",
        "type": "string",
        "required": false,
        "description": "composite | task | decorator | service. Inferred from nodeClass when omitted; supplying it fails loudly on a mismatch"
      },
      {
        "name": "parent",
        "type": "string",
        "required": false,
        "description": "Node to attach under: a guid from list_bt_graph_nodes, a runtime address from read_behavior_tree_graph, a unique node name, or 'root' (default)"
      },
      {
        "name": "index",
        "type": "number",
        "required": false,
        "description": "Position among the parent's children, which is the execution order. For a decorator or service, its position in the parent's subnode list; on a SimpleParallel, the output pin (0 = main task, 1 = background)"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "Display name for the new node"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "UPROPERTY writes on the new node, as a map of property path to value"
      },
      {
        "name": "blackboardKeys",
        "type": "object",
        "required": false,
        "description": "Map of FBlackboardKeySelector property to blackboard key name, e.g. {BlackboardKey: 'TargetActor'}; resolved against the tree's blackboard"
      }
    ]
  },
  "add_eqs_generator": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      },
      {
        "name": "generatorClass",
        "type": "string",
        "required": true,
        "description": "Generator class, short name (OnCircle) or full path"
      }
    ]
  },
  "add_eqs_test": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      },
      {
        "name": "testClass",
        "type": "string",
        "required": true,
        "description": "Test class, short name (Distance) or full path"
      },
      {
        "name": "optionIndex",
        "type": "number",
        "required": false,
        "description": "Option to add the test to (default 0)"
      },
      {
        "name": "purpose",
        "type": "string",
        "required": false,
        "description": "filter | score | both"
      }
    ]
  },
  "add_imc_mapping": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path",
        "aliases": [
          "mappingContext",
          "assetPath"
        ]
      },
      {
        "name": "inputActionPath",
        "type": "string",
        "required": true,
        "description": "InputAction asset path to map",
        "aliases": [
          "inputAction"
        ]
      },
      {
        "name": "key",
        "type": "string",
        "required": true,
        "description": "FKey name to bind"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist the asset (default true); false defers the write"
      }
    ]
  },
  "add_perception_component": {
    "category": "gameplay",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path"
      },
      {
        "name": "senses",
        "type": "array",
        "required": false,
        "description": "Sense names (Sight, Hearing, Damage, Touch, Team, Prediction), AISenseConfig_* names or class paths",
        "items": "string"
      }
    ]
  },
  "add_smart_object_component": {
    "category": "gameplay",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path"
      }
    ]
  },
  "add_smart_object_default_behavior": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SmartObjectDefinition asset path"
      },
      {
        "name": "behaviorClass",
        "type": "string",
        "required": true,
        "description": "Behavior definition asset path or class path"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Property writes applied to a freshly-spawned behavior instance"
      }
    ]
  },
  "add_smart_object_slot": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SmartObjectDefinition asset path"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Slot name"
      },
      {
        "name": "offset",
        "type": "vec3",
        "required": false,
        "description": "Slot offset {x,y,z}"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "Slot rotation {pitch,yaw,roll}"
      },
      {
        "name": "tags",
        "type": "array",
        "required": false,
        "description": "Slot runtime tags (gameplay tag strings or struct shapes)"
      },
      {
        "name": "behaviorClass",
        "type": "string",
        "required": false,
        "description": "Behavior definition class or asset path to give the slot"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Property writes applied to a freshly-spawned behavior instance"
      }
    ]
  },
  "add_smart_object_slot_behavior": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SmartObjectDefinition asset path"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": true,
        "description": "Slot index"
      },
      {
        "name": "behaviorClass",
        "type": "string",
        "required": true,
        "description": "Behavior definition asset path or class path"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Property writes applied to a freshly-spawned behavior instance"
      }
    ]
  },
  "add_state_tree_component": {
    "category": "gameplay",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path"
      }
    ]
  },
  "apply_mapping_context": {
    "category": "gameplay",
    "params": [
      {
        "name": "mappingContext",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path to apply to the live player"
      },
      {
        "name": "priority",
        "type": "number",
        "required": false,
        "description": "Enhanced Input context priority; higher wins for the same key (default 0)"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the first running one"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Local player to act on (default 0)"
      }
    ]
  },
  "check_perception": {
    "category": "gameplay",
    "params": [
      {
        "name": "perceiverLabel",
        "type": "string",
        "required": false,
        "description": "The actor doing the perceiving"
      },
      {
        "name": "perceiverPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the perceiver"
      },
      {
        "name": "targetLabel",
        "type": "string",
        "required": false,
        "description": "The actor being perceived"
      },
      {
        "name": "targetPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the target"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      }
    ]
  },
  "configure_ai_perception_sense": {
    "category": "gameplay",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path"
      },
      {
        "name": "senseType",
        "type": "string",
        "required": false,
        "description": "Sight (default) | Hearing | Damage | Touch | Team | Prediction | Blueprint"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "Per-sense property writes, e.g. {SightRadius: 1500}"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Target AIPerceptionComponent name (default: first found)"
      }
    ]
  },
  "create_behavior_tree": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/AI)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      },
      {
        "name": "blackboardPath",
        "type": "string",
        "required": false,
        "description": "BlackboardData assigned before the first save; an unknown path creates nothing"
      }
    ]
  },
  "create_blackboard": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/AI)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      }
    ],
    "contractExempt": "Creates and saves an asset under the contract values; nothing it reads fails first"
  },
  "create_eqs_query": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/AI/EQS)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      }
    ],
    "contractExempt": "Creates and saves an asset under the contract values; nothing it reads fails first"
  },
  "create_game_mode": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Blueprints/GameFramework)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent deriving from GameModeBase: short name, /Script path or Blueprint asset path"
      }
    ]
  },
  "create_game_state": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Blueprints/GameFramework)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent deriving from GameStateBase: short name, /Script path or Blueprint asset path"
      }
    ]
  },
  "create_hud": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Blueprints/GameFramework)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent deriving from HUD: short name, /Script path or Blueprint asset path"
      }
    ]
  },
  "create_input_action": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Input)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      },
      {
        "name": "valueType",
        "type": "string",
        "required": false,
        "description": "Boolean (default) | Axis1D | Axis2D | Axis3D; an unrecognised type leaves the default"
      }
    ],
    "contractExempt": "Creates and saves an asset under the contract values; nothing it reads fails first"
  },
  "create_input_mapping_context": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Input)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      }
    ],
    "contractExempt": "Creates and saves an asset under the contract values; nothing it reads fails first"
  },
  "create_player_controller": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Blueprints/GameFramework)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent deriving from PlayerController: short name, /Script path or Blueprint asset path"
      }
    ]
  },
  "create_player_state": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/Blueprints/GameFramework)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent deriving from PlayerState: short name, /Script path or Blueprint asset path"
      }
    ]
  },
  "create_smart_object_definition": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/AI/SmartObjects)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      },
      {
        "name": "defaultBehaviorClass",
        "type": "string",
        "required": false,
        "description": "Behavior definition asset path or class path added to DefaultBehaviorDefinitions"
      },
      {
        "name": "instanceProperties",
        "type": "object",
        "required": false,
        "description": "Property writes applied to a freshly-spawned behavior instance"
      }
    ],
    "contractExempt": "Creates and saves an asset under the contract values; nothing it reads fails first"
  },
  "create_state_tree": {
    "category": "gameplay",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/AI)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      },
      {
        "name": "schema",
        "type": "string",
        "required": false,
        "description": "StateTree schema class path; omit to take the gameplay default (StateTreeComponentSchema, then StateTreeAIComponentSchema, then any concrete schema)"
      }
    ],
    "contractExempt": "Creates and saves an asset under the contract values; nothing it reads fails first"
  },
  "find_nav_path": {
    "category": "gameplay",
    "params": [
      {
        "name": "start",
        "type": "vec3",
        "required": true,
        "description": "Query start (world point)"
      },
      {
        "name": "end",
        "type": "vec3",
        "required": true,
        "description": "Query end (world point)"
      },
      {
        "name": "pathfindingContext",
        "type": "string",
        "required": false,
        "description": "Actor label whose navigation agent and filter the query uses"
      },
      {
        "name": "pathfindingContextPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the pathfinding context actor; alternative to pathfindingContext"
      }
    ]
  },
  "get_action_value": {
    "category": "gameplay",
    "params": [
      {
        "name": "inputActionPath",
        "type": "string",
        "required": false,
        "description": "InputAction to read; omit for every action the player has bound"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the first running one"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Local player to read (default 0)"
      }
    ]
  },
  "get_behavior_tree_info": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "get_bt_runtime": {
    "category": "gameplay",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "includeAuxNodes",
        "type": "boolean",
        "required": false,
        "description": "Include the decorators and services currently active (default true)"
      },
      {
        "name": "includeDebugStrings",
        "type": "boolean",
        "required": false,
        "description": "Include the engine's DescribeActiveTasks / DescribeActiveTrees dumps (default true)"
      }
    ]
  },
  "get_game_framework_info": {
    "category": "gameplay",
    "params": []
  },
  "get_input_mapping_contexts": {
    "category": "gameplay",
    "params": [
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for every running PIE world"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Player index; omit for every player"
      },
      {
        "name": "mappingContext",
        "type": "string",
        "required": false,
        "description": "Name or path of one context to answer yes/no about"
      },
      {
        "name": "includeActions",
        "type": "boolean",
        "required": false,
        "description": "Include each context's action/key mappings"
      }
    ]
  },
  "get_live_blackboard": {
    "category": "gameplay",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "key",
        "type": "string",
        "required": false,
        "description": "Report this one blackboard key; a miss lists every real key name"
      },
      {
        "name": "verbosity",
        "type": "string",
        "required": false,
        "description": "onlyValue | keyWithValue | detailed (default) | full"
      }
    ]
  },
  "get_navmesh_details": {
    "category": "gameplay",
    "params": []
  },
  "get_navmesh_info": {
    "category": "gameplay",
    "params": []
  },
  "get_perceived_actors": {
    "category": "gameplay",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "senseType",
        "type": "string",
        "required": false,
        "description": "Only report this sense: Sight | Hearing | Damage | Touch | Team | Prediction"
      }
    ]
  },
  "get_state_tree_runtime": {
    "category": "gameplay",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor label, name or path. Pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "pie (default) | editor | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary)"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "StateTree component to read (default: first found)"
      }
    ]
  },
  "list_ai_agents": {
    "category": "gameplay",
    "params": [
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "runningOnly",
        "type": "boolean",
        "required": false,
        "description": "Only agents whose brain is currently running"
      },
      {
        "name": "behaviorTreeOnly",
        "type": "boolean",
        "required": false,
        "description": "Only agents running a BehaviorTree"
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring over the agent's class name"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Maximum agents to return"
      }
    ]
  },
  "list_behavior_trees": {
    "category": "gameplay",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "nextCursor from the previous page, passed back unmodified. Omit for the first page"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "list_bt_graph_nodes": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_bt_node_classes": {
    "category": "gameplay",
    "params": [
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "composite | task | decorator | service. Omit for every kind; anything else is refused"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "nextCursor from the previous page, passed back unmodified. Omit for the first page"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "list_bt_tasks": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "BehaviorTree asset path; omit to sweep directory"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content folder to sweep when assetPath is omitted (default: every BehaviorTree)"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders of directory (default true)"
      },
      {
        "name": "taskClass",
        "type": "string",
        "required": false,
        "description": "Keep only tasks of this class or its subclasses, e.g. BTTask_MoveTo"
      },
      {
        "name": "filterClassOnly",
        "type": "boolean",
        "required": false,
        "description": "Keep only tasks that declare a FilterClass"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "How many BehaviorTree assets a directory sweep loads (default 200)"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": false,
        "description": "BT node selector: class name, /Script path or partial name; a resolved class matches its whole subtree"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "BT node selector: the node's object name or its NodeName display text"
      },
      {
        "name": "nodePath",
        "type": "string",
        "required": false,
        "description": "BT node address from read_behavior_tree_graph, e.g. 'Root.Children[0].Decorators[1]'"
      },
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "BT node kind: composite | task | decorator | service"
      }
    ]
  },
  "list_eqs_queries": {
    "category": "gameplay",
    "params": []
  },
  "list_eqs_types": {
    "category": "gameplay",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring over the class names"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "nextCursor from the previous page, passed back unmodified. Omit for the first page"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "list_imc_mappings": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path"
      }
    ]
  },
  "list_input_assets": {
    "category": "gameplay",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "nextCursor from the previous page, passed back unmodified. Omit for the first page"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "list_mass_types": {
    "category": "gameplay",
    "params": [
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "all | traits | processors (default all)"
      },
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring over the class names"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "How many classes per kind (default 200)"
      }
    ]
  },
  "list_nav_invokers": {
    "category": "gameplay",
    "params": []
  },
  "list_smart_object_slots": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SmartObjectDefinition asset path"
      }
    ]
  },
  "list_state_trees": {
    "category": "gameplay",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "nextCursor from the previous page, passed back unmodified. Omit for the first page"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "move_bt_node": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "node",
        "type": "string",
        "required": true,
        "description": "Node to move: a guid, a runtime address, or a unique node name",
        "aliases": [
          "nodePath"
        ]
      },
      {
        "name": "parent",
        "type": "string",
        "required": false,
        "description": "Node to attach under: a guid from list_bt_graph_nodes, a runtime address from read_behavior_tree_graph, a unique node name, or 'root' (default)"
      },
      {
        "name": "index",
        "type": "number",
        "required": false,
        "description": "Position among the parent's children, which is the execution order. For a decorator or service, its position in the parent's subnode list; on a SimpleParallel, the output pin (0 = main task, 1 = background)"
      }
    ]
  },
  "project_point_to_navigation": {
    "category": "gameplay",
    "params": [
      {
        "name": "location",
        "type": "vec3",
        "required": true,
        "description": "World point to project onto the navmesh"
      }
    ]
  },
  "query_zone_graph": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryMode",
        "type": "string",
        "required": false,
        "description": "summary | lanes | lane | nearest (default summary)"
      },
      {
        "name": "laneIndex",
        "type": "number",
        "required": false,
        "description": "Lane queryMode=lane reports, as queryMode=lanes numbers them"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Query point for queryMode=nearest"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "How far from location a nearest-lane hit is accepted, in world units (default 100000)"
      },
      {
        "name": "tags",
        "type": "array",
        "required": false,
        "description": "Zone Graph tag names to keep, as the project settings name them"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "How many lanes to report (default 50)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "editor | pie | auto (default editor)"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance when world is pie or auto"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Only this ZoneGraphData actor, by label or name"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Only this ZoneGraphData actor, by full object path"
      }
    ]
  },
  "read_behavior_tree_graph": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeProperties",
        "type": "boolean",
        "required": false,
        "description": "Include each node's own UPROPERTY values"
      },
      {
        "name": "includeInherited",
        "type": "boolean",
        "required": false,
        "description": "Keep the properties UBTNode itself declares (TreeAsset, ParentNode, NodeName), omitted by default"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Only report these UPROPERTY names",
        "items": "string"
      }
    ]
  },
  "read_blackboard": {
    "category": "gameplay",
    "params": [
      {
        "name": "blackboardPath",
        "type": "string",
        "required": true,
        "description": "BlackboardData asset path",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "read_bt_node_properties": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": false,
        "description": "BT node selector: class name, /Script path or partial name; a resolved class matches its whole subtree"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "BT node selector: the node's object name or its NodeName display text"
      },
      {
        "name": "nodePath",
        "type": "string",
        "required": false,
        "description": "BT node address from read_behavior_tree_graph, e.g. 'Root.Children[0].Decorators[1]'"
      },
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "BT node kind: composite | task | decorator | service"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Only report these UPROPERTY names",
        "items": "string"
      },
      {
        "name": "includeInherited",
        "type": "boolean",
        "required": false,
        "description": "Keep the properties UBTNode itself declares (TreeAsset, ParentNode, NodeName), omitted by default"
      }
    ]
  },
  "read_eqs_query": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      }
    ]
  },
  "read_imc": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path",
        "aliases": [
          "mappingContext",
          "assetPath"
        ]
      }
    ]
  },
  "read_input_action": {
    "category": "gameplay",
    "params": [
      {
        "name": "inputActionPath",
        "type": "string",
        "required": true,
        "description": "InputAction asset path"
      }
    ]
  },
  "read_perception": {
    "category": "gameplay",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": false,
        "description": "Blueprint whose AIPerceptionComponent template to read; or pass actorLabel/actorPath"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Target AIPerceptionComponent name (default: first found)"
      }
    ]
  },
  "rebuild_navigation": {
    "category": "gameplay",
    "params": [],
    "contractExempt": "Rebuilds the navmesh; it takes no parameters that could fail first"
  },
  "remove_blackboard_key": {
    "category": "gameplay",
    "params": [
      {
        "name": "blackboardPath",
        "type": "string",
        "required": true,
        "description": "BlackboardData asset path"
      },
      {
        "name": "keyName",
        "type": "string",
        "required": true,
        "description": "Key name to remove"
      }
    ]
  },
  "remove_bt_node": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "node",
        "type": "string",
        "required": true,
        "description": "Node to remove: a guid, a runtime address, or a unique node name",
        "aliases": [
          "nodePath"
        ]
      }
    ]
  },
  "remove_eqs_option": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      },
      {
        "name": "optionIndex",
        "type": "number",
        "required": true,
        "description": "Option to remove, as read_eqs_query reports it"
      }
    ]
  },
  "remove_eqs_test": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      },
      {
        "name": "testIndex",
        "type": "number",
        "required": true,
        "description": "Test to remove, as read_eqs_query reports it"
      },
      {
        "name": "optionIndex",
        "type": "number",
        "required": false,
        "description": "Option the test belongs to (default 0)"
      }
    ]
  },
  "remove_imc_mapping": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path",
        "aliases": [
          "mappingContext",
          "assetPath"
        ]
      },
      {
        "name": "mappingIndex",
        "type": "number",
        "required": false,
        "description": "Index of the mapping to remove"
      },
      {
        "name": "inputActionPath",
        "type": "string",
        "required": false,
        "description": "Select the mapping by InputAction (with key)",
        "aliases": [
          "inputAction"
        ]
      },
      {
        "name": "key",
        "type": "string",
        "required": false,
        "description": "Select the mapping by key (with inputActionPath)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist the asset (default true); false defers the write"
      }
    ]
  },
  "remove_mapping_context": {
    "category": "gameplay",
    "params": [
      {
        "name": "mappingContext",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path to remove from the live player"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the first running one"
      },
      {
        "name": "playerIndex",
        "type": "number",
        "required": false,
        "description": "Local player to act on (default 0)"
      }
    ]
  },
  "remove_mass_trait": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MassEntityConfigAsset path"
      },
      {
        "name": "traitClass",
        "type": "string",
        "required": false,
        "description": "Concrete UMassEntityTraitBase subclass to remove, short name or /Script path. Idempotent selector"
      },
      {
        "name": "index",
        "type": "number",
        "required": false,
        "description": "Positional index into Config.Traits, as read_mass_entity_config reports it"
      }
    ]
  },
  "remove_sense": {
    "category": "gameplay",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint carrying the AIPerceptionComponent template"
      },
      {
        "name": "index",
        "type": "number",
        "required": false,
        "description": "Index of the sense config to remove, as read_perception reports it"
      },
      {
        "name": "senseType",
        "type": "string",
        "required": false,
        "description": "Sense to remove when index is omitted: Sight | Hearing | Damage | Touch | Team | Prediction | Blueprint"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Target AIPerceptionComponent name (default: first found)"
      }
    ]
  },
  "remove_smart_object_slot": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SmartObjectDefinition asset path"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": true,
        "description": "Slot index to remove"
      }
    ]
  },
  "reorder_eqs_tests": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      },
      {
        "name": "order",
        "type": "array",
        "required": true,
        "description": "Current test indices in the order wanted; must be a full permutation",
        "items": "number"
      },
      {
        "name": "optionIndex",
        "type": "number",
        "required": false,
        "description": "Option whose tests to reorder (default 0)"
      }
    ]
  },
  "reorder_mass_traits": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MassEntityConfigAsset path"
      },
      {
        "name": "order",
        "type": "array",
        "required": true,
        "description": "Current trait indices in the order wanted; must be a full permutation",
        "items": "number"
      }
    ]
  },
  "report_noise_event": {
    "category": "gameplay",
    "params": [
      {
        "name": "senseType",
        "type": "string",
        "required": false,
        "description": "hearing (default) | damage"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Event location; defaults to the instigator's (hearing) or the damaged actor's (damage)"
      },
      {
        "name": "loudness",
        "type": "number",
        "required": false,
        "description": "Noise loudness (default 1)"
      },
      {
        "name": "maxRange",
        "type": "number",
        "required": false,
        "description": "Maximum range the noise carries (0 = unlimited)"
      },
      {
        "name": "instigatorLabel",
        "type": "string",
        "required": false,
        "description": "The actor that caused the event"
      },
      {
        "name": "instigatorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the instigator"
      },
      {
        "name": "tag",
        "type": "string",
        "required": false,
        "description": "Tag recorded on the stimulus"
      },
      {
        "name": "targetLabel",
        "type": "string",
        "required": false,
        "description": "Damaged actor (damage only)"
      },
      {
        "name": "targetPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the damaged actor (damage only)"
      },
      {
        "name": "amount",
        "type": "number",
        "required": false,
        "description": "Damage amount (damage only, required there)"
      },
      {
        "name": "hitLocation",
        "type": "object",
        "required": false,
        "description": "Impact point {x,y,z} (damage only)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      }
    ]
  },
  "run_behavior_tree": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset to run",
        "aliases": [
          "behaviorTreePath"
        ]
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "executionMode",
        "type": "string",
        "required": false,
        "description": "looped (default) | singleRun"
      },
      {
        "name": "restartIfRunning",
        "type": "boolean",
        "required": false,
        "description": "Restart the tree even if this one is already running"
      }
    ]
  },
  "run_eqs_query": {
    "category": "gameplay",
    "params": [
      {
        "name": "queryPath",
        "type": "string",
        "required": true,
        "description": "EnvQuery asset path"
      },
      {
        "name": "querierLabel",
        "type": "string",
        "required": false,
        "description": "Actor the query runs from, which querier-relative contexts resolve against"
      },
      {
        "name": "querierPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the querier; unambiguous where a label is not"
      },
      {
        "name": "runMode",
        "type": "string",
        "required": false,
        "description": "all | best | random (default all)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "auto | pie | editor (default auto)"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance when world is pie or auto"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "How many scored items to return (default 50)"
      }
    ]
  },
  "set_action_triggers": {
    "category": "gameplay",
    "params": [
      {
        "name": "inputActionPath",
        "type": "string",
        "required": true,
        "description": "InputAction asset path"
      },
      {
        "name": "triggers",
        "type": "array",
        "required": false,
        "description": "Trigger specs that replace the action's own Triggers array",
        "items": "object"
      },
      {
        "name": "modifiers",
        "type": "array",
        "required": false,
        "description": "Modifier specs that replace the action's own Modifiers array",
        "items": "object"
      },
      {
        "name": "clear",
        "type": "boolean",
        "required": false,
        "description": "Empty both arrays; an omitted array then means empty rather than left alone"
      }
    ]
  },
  "set_behavior_tree_blackboard": {
    "category": "gameplay",
    "params": [
      {
        "name": "behaviorTreePath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path"
      },
      {
        "name": "blackboardPath",
        "type": "string",
        "required": true,
        "description": "BlackboardData asset to bind"
      }
    ]
  },
  "set_blackboard_parent": {
    "category": "gameplay",
    "params": [
      {
        "name": "blackboardPath",
        "type": "string",
        "required": true,
        "description": "Child BlackboardData asset path"
      },
      {
        "name": "parentPath",
        "type": "string",
        "required": false,
        "description": "Parent BlackboardData asset path; None or omitted clears it"
      },
      {
        "name": "autoPruneDuplicateKeys",
        "type": "boolean",
        "required": false,
        "description": "Remove own keys the parent chain already defines (default true)"
      }
    ]
  },
  "set_bt_node_property": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodePath",
        "type": "string",
        "required": false,
        "description": "BT node address from read_behavior_tree_graph, e.g. 'Root.Children[0].Decorators[1]'"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "BT node selector: the node's object name or its NodeName display text"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": false,
        "description": "BT node selector: class name, /Script path or partial name; a resolved class matches its whole subtree"
      },
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "BT node kind: composite | task | decorator | service"
      },
      {
        "name": "property",
        "type": "string",
        "required": false,
        "description": "Dotted, indexed property path on the node, e.g. 'FilterClass' or 'Lines[1].Text'"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "Value for property: scalar, object, array, class path, or null to clear a reference"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Several writes at once, as a map of property path to value. Applied as one batch: a rejected write puts the others back"
      }
    ]
  },
  "set_bt_task_property": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BehaviorTree asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodePath",
        "type": "string",
        "required": false,
        "description": "BT node address from read_behavior_tree_graph, e.g. 'Root.Children[0].Decorators[1]'"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "BT node selector: the node's object name or its NodeName display text"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": false,
        "description": "BT node selector: class name, /Script path or partial name; a resolved class matches its whole subtree"
      },
      {
        "name": "kind",
        "type": "string",
        "required": false,
        "description": "BT node kind: composite | task | decorator | service"
      },
      {
        "name": "property",
        "type": "string",
        "required": false,
        "description": "Dotted, indexed property path on the node, e.g. 'FilterClass' or 'Lines[1].Text'"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "Value for property: scalar, object, array, class path, or null to clear a reference"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Several writes at once, as a map of property path to value. Applied as one batch: a rejected write puts the others back"
      }
    ]
  },
  "set_imc_mapping_action": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path"
      },
      {
        "name": "newInputActionPath",
        "type": "string",
        "required": true,
        "description": "InputAction the mapping should use"
      },
      {
        "name": "mappingIndex",
        "type": "number",
        "required": false,
        "description": "Index of the mapping to retarget"
      },
      {
        "name": "key",
        "type": "string",
        "required": false,
        "description": "Select the mapping by its key"
      },
      {
        "name": "inputActionPath",
        "type": "string",
        "required": false,
        "description": "Select the mapping by its current InputAction"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist the asset (default true); false defers the write"
      }
    ]
  },
  "set_imc_mapping_key": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path"
      },
      {
        "name": "newKey",
        "type": "string",
        "required": true,
        "description": "New FKey name"
      },
      {
        "name": "mappingIndex",
        "type": "number",
        "required": false,
        "description": "Index of the mapping to rebind"
      },
      {
        "name": "key",
        "type": "string",
        "required": false,
        "description": "Select the mapping by its current key"
      },
      {
        "name": "inputActionPath",
        "type": "string",
        "required": false,
        "description": "Select the mapping by its InputAction"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist the asset (default true); false defers the write"
      }
    ]
  },
  "set_live_blackboard": {
    "category": "gameplay",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "key",
        "type": "string",
        "required": true,
        "description": "Blackboard key to write"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "Value to write, typed to the key; required unless clear is true"
      },
      {
        "name": "clear",
        "type": "boolean",
        "required": false,
        "description": "Clear the key instead of writing a value"
      }
    ]
  },
  "set_mapping_modifiers": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": true,
        "description": "InputMappingContext asset path"
      },
      {
        "name": "mappingIndex",
        "type": "number",
        "required": false,
        "description": "Index of the mapping in the IMC (default 0)"
      },
      {
        "name": "modifiers",
        "type": "array",
        "required": false,
        "description": "Modifier objects: {type, ...props} or {class, properties}. Replaces the mapping's list",
        "items": "object"
      },
      {
        "name": "triggers",
        "type": "array",
        "required": false,
        "description": "Trigger objects: {type, ...props} or {class, properties}. Replaces the mapping's list",
        "items": "object"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist the asset (default true); false defers the write"
      }
    ]
  },
  "set_player_mappable_settings": {
    "category": "gameplay",
    "params": [
      {
        "name": "inputActionPath",
        "type": "string",
        "required": true,
        "description": "InputAction asset path"
      },
      {
        "name": "mappingName",
        "type": "string",
        "required": true,
        "description": "Stable FName saved with the player mapping; must be non-empty"
      },
      {
        "name": "displayName",
        "type": "string",
        "required": false,
        "description": "Display name written onto UPlayerMappableKeySettings"
      },
      {
        "name": "displayCategory",
        "type": "string",
        "required": false,
        "description": "Display category (Movement, Combat, ...) written onto UPlayerMappableKeySettings"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Persist the asset (default true); false defers the write"
      }
    ]
  },
  "set_smart_object_slot": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SmartObjectDefinition asset path"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": true,
        "description": "Slot index"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Slot name"
      },
      {
        "name": "offset",
        "type": "vec3",
        "required": false,
        "description": "Slot offset {x,y,z}"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "Slot rotation {pitch,yaw,roll}"
      },
      {
        "name": "tags",
        "type": "array",
        "required": false,
        "description": "Slot runtime tags (gameplay tag strings or struct shapes)"
      }
    ]
  },
  "set_world_game_mode": {
    "category": "gameplay",
    "params": [
      {
        "name": "gameModeClass",
        "type": "string",
        "required": true,
        "description": "GameMode class or Blueprint path",
        "aliases": [
          "gameModePath"
        ]
      }
    ]
  },
  "spawn_nav_modifier_volume": {
    "category": "gameplay",
    "params": [
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location of the volume (default origin)"
      },
      {
        "name": "extent",
        "type": "vec3",
        "required": false,
        "description": "Half-size in world units, positive on every axis (default 100)"
      },
      {
        "name": "areaClass",
        "type": "string",
        "required": false,
        "description": "UNavArea subclass: NavArea_Null (default, cuts a hole), NavArea_Obstacle, or a /Script path"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Actor label, also what onConflict dedupes on"
      },
      {
        "name": "scale",
        "type": "vec3",
        "required": false,
        "description": "Actor scale, applied after the brush is built"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the actor already carrying label) or error when one does"
      }
    ],
    "contractExempt": "Spawns a NavModifierVolume under the contract values; nothing it reads fails first"
  },
  "stop_behavior_tree": {
    "category": "gameplay",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label or name (the pawn or its AIController)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; wins over actorLabel"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "stop (default) | forced | restart | pause | resume"
      },
      {
        "name": "reason",
        "type": "string",
        "required": false,
        "description": "Recorded with the stop, for the log"
      },
      {
        "name": "completeRestart",
        "type": "boolean",
        "required": false,
        "description": "With mode=restart, restart from the root rather than resuming"
      }
    ]
  },
  "validate_input": {
    "category": "gameplay",
    "params": [
      {
        "name": "imcPath",
        "type": "string",
        "required": false,
        "description": "Audit this one InputMappingContext instead of sweeping directory"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content root the InputMappingContext sweep covers (default /Game)"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders of directory (default true)"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "How many InputMappingContexts a sweep loads (default 200)"
      }
    ]
  },
  "validate_mass_entity_config": {
    "category": "gameplay",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MassEntityConfigAsset path"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_blackboard_key: "Params: blackboardPath, keyName, keyType?, baseClass?, enumType?",
  add_bt_node: "Params: assetPath (or path), nodeClass, nodeCategory?, parent?, index?, nodeName?, properties?, blackboardKeys?",
  add_eqs_generator: "Params: queryPath, generatorClass",
  add_eqs_test: "Params: queryPath, testClass, optionIndex?, purpose?",
  add_imc_mapping: "Params: imcPath (or mappingContext, or assetPath), inputActionPath (or inputAction), key, save?",
  add_perception_component: "Params: blueprintPath, senses?",
  add_smart_object_component: "Params: blueprintPath",
  add_smart_object_default_behavior: "Params: assetPath, behaviorClass, instanceProperties?",
  add_smart_object_slot: "Params: assetPath, name?, offset?, rotation?, tags?, behaviorClass?, instanceProperties?",
  add_smart_object_slot_behavior: "Params: assetPath, slotIndex, behaviorClass, instanceProperties?",
  add_state_tree_component: "Params: blueprintPath",
  apply_mapping_context: "Params: mappingContext, priority?, pieInstance?, playerIndex?",
  check_perception: "Params: perceiverLabel?, perceiverPath?, targetLabel?, targetPath?, world?, pieInstance?",
  configure_ai_perception_sense: "Params: blueprintPath, senseType?, settings?, componentName?",
  create_behavior_tree: "Params: name, packagePath?, onConflict?, blackboardPath?",
  create_blackboard: "Params: name, packagePath?, onConflict?",
  create_eqs_query: "Params: name, packagePath?, onConflict?",
  create_game_mode: "Params: name, packagePath?, parentClass?",
  create_game_state: "Params: name, packagePath?, parentClass?",
  create_hud: "Params: name, packagePath?, parentClass?",
  create_input_action: "Params: name, packagePath?, onConflict?, valueType?",
  create_input_mapping_context: "Params: name, packagePath?, onConflict?",
  create_player_controller: "Params: name, packagePath?, parentClass?",
  create_player_state: "Params: name, packagePath?, parentClass?",
  create_smart_object_definition: "Params: name, packagePath?, onConflict?, defaultBehaviorClass?, instanceProperties?",
  create_state_tree: "Params: name, packagePath?, onConflict?, schema?",
  find_nav_path: "Params: start, end, pathfindingContext?, pathfindingContextPath?",
  get_action_value: "Params: inputActionPath?, pieInstance?, playerIndex?",
  get_behavior_tree_info: "Params: assetPath (or path)",
  get_bt_runtime: "Params: actorLabel?, actorPath?, world?, pieInstance?, includeAuxNodes?, includeDebugStrings?",
  get_game_framework_info: "Params: none",
  get_input_mapping_contexts: "Params: pieInstance?, playerIndex?, mappingContext?, includeActions?",
  get_live_blackboard: "Params: actorLabel?, actorPath?, world?, pieInstance?, key?, verbosity?",
  get_navmesh_details: "Params: none",
  get_navmesh_info: "Params: none",
  get_perceived_actors: "Params: actorLabel?, actorPath?, world?, pieInstance?, senseType?",
  get_state_tree_runtime: "Params: actorLabel?, actorPath?, world?, pieInstance?, componentName?",
  list_ai_agents: "Params: world?, pieInstance?, runningOnly?, behaviorTreeOnly?, classFilter?, limit?",
  list_behavior_trees: "Params: cursor?, limit?",
  list_bt_graph_nodes: "Params: assetPath (or path)",
  list_bt_node_classes: "Params: kind?, cursor?, limit?",
  list_bt_tasks: "Params: assetPath?, directory?, recursive?, taskClass?, filterClassOnly?, limit?, nodeClass?, nodeName?, nodePath?, kind?",
  list_eqs_queries: "Params: none",
  list_eqs_types: "Params: filter?, cursor?, limit?",
  list_imc_mappings: "Params: imcPath",
  list_input_assets: "Params: cursor?, limit?",
  list_mass_types: "Params: kind?, filter?, limit?",
  list_nav_invokers: "Params: none",
  list_smart_object_slots: "Params: assetPath",
  list_state_trees: "Params: cursor?, limit?",
  move_bt_node: "Params: assetPath (or path), node (or nodePath), parent?, index?",
  project_point_to_navigation: "Params: location",
  query_zone_graph: "Params: queryMode?, laneIndex?, location?, radius?, tags?, limit?, world?, pieInstance?, actorLabel?, actorPath?",
  read_behavior_tree_graph: "Params: assetPath (or path), includeProperties?, includeInherited?, propertyNames?",
  read_blackboard: "Params: blackboardPath (or assetPath)",
  read_bt_node_properties: "Params: assetPath (or path), nodeClass?, nodeName?, nodePath?, kind?, propertyNames?, includeInherited?",
  read_eqs_query: "Params: queryPath",
  read_imc: "Params: imcPath (or mappingContext, or assetPath)",
  read_input_action: "Params: inputActionPath",
  read_perception: "Params: blueprintPath?, actorLabel?, actorPath?, world?, pieInstance?, componentName?",
  rebuild_navigation: "Params: none",
  remove_blackboard_key: "Params: blackboardPath, keyName",
  remove_bt_node: "Params: assetPath (or path), node (or nodePath)",
  remove_eqs_option: "Params: queryPath, optionIndex",
  remove_eqs_test: "Params: queryPath, testIndex, optionIndex?",
  remove_imc_mapping: "Params: imcPath (or mappingContext, or assetPath), mappingIndex?, inputActionPath? (or inputAction), key?, save?",
  remove_mapping_context: "Params: mappingContext, pieInstance?, playerIndex?",
  remove_mass_trait: "Params: assetPath, traitClass?, index?",
  remove_sense: "Params: blueprintPath, index?, senseType?, componentName?",
  remove_smart_object_slot: "Params: assetPath, slotIndex",
  reorder_eqs_tests: "Params: queryPath, order, optionIndex?",
  reorder_mass_traits: "Params: assetPath, order",
  report_noise_event: "Params: senseType?, location?, loudness?, maxRange?, instigatorLabel?, instigatorPath?, tag?, targetLabel?, targetPath?, amount?, hitLocation?, world?, pieInstance?",
  run_behavior_tree: "Params: assetPath (or behaviorTreePath), actorLabel?, actorPath?, world?, pieInstance?, executionMode?, restartIfRunning?",
  run_eqs_query: "Params: queryPath, querierLabel?, querierPath?, runMode?, world?, pieInstance?, limit?",
  set_action_triggers: "Params: inputActionPath, triggers?, modifiers?, clear?",
  set_behavior_tree_blackboard: "Params: behaviorTreePath, blackboardPath",
  set_blackboard_parent: "Params: blackboardPath, parentPath?, autoPruneDuplicateKeys?",
  set_bt_node_property: "Params: assetPath (or path), nodePath?, nodeName?, nodeClass?, kind?, property?, value?, properties?",
  set_bt_task_property: "Params: assetPath (or path), nodePath?, nodeName?, nodeClass?, kind?, property?, value?, properties?",
  set_imc_mapping_action: "Params: imcPath, newInputActionPath, mappingIndex?, key?, inputActionPath?, save?",
  set_imc_mapping_key: "Params: imcPath, newKey, mappingIndex?, key?, inputActionPath?, save?",
  set_live_blackboard: "Params: actorLabel?, actorPath?, world?, pieInstance?, key, value?, clear?",
  set_mapping_modifiers: "Params: imcPath, mappingIndex?, modifiers?, triggers?, save?",
  set_player_mappable_settings: "Params: inputActionPath, mappingName, displayName?, displayCategory?, save?",
  set_smart_object_slot: "Params: assetPath, slotIndex, name?, offset?, rotation?, tags?",
  set_world_game_mode: "Params: gameModeClass (or gameModePath)",
  spawn_nav_modifier_volume: "Params: location?, extent?, areaClass?, label?, scale?, onConflict?",
  stop_behavior_tree: "Params: actorLabel?, actorPath?, world?, pieInstance?, mode?, reason?, completeRestart?",
  validate_input: "Params: imcPath?, directory?, recursive?, limit?",
  validate_mass_entity_config: "Params: assetPath",
};

/** Every key the spec'd gameplay handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("Live actor label or name (the pawn or its AIController) (get_bt_runtime, get_live_blackboard, get_perceived_actors, read_perception, run_behavior_tree, set_live_blackboard, stop_behavior_tree). Actor label, name or path. Pass actorLabel or actorPath (get_state_tree_runtime). Only this ZoneGraphData actor, by label or name (query_zone_graph)"),
  actorPath: z.string().optional().describe("Full actor object path; wins over actorLabel (get_bt_runtime, get_live_blackboard, get_perceived_actors, read_perception, run_behavior_tree, set_live_blackboard, stop_behavior_tree). Full actor object path; the unambiguous selector (get_state_tree_runtime). Only this ZoneGraphData actor, by full object path (query_zone_graph)"),
  amount: z.number().optional().describe("Damage amount (damage only, required there)"),
  areaClass: z.string().optional().describe("UNavArea subclass: NavArea_Null (default, cuts a hole), NavArea_Obstacle, or a /Script path"),
  assetPath: z.string().optional().describe("BehaviorTree asset path (add_bt_node, get_behavior_tree_info, list_bt_graph_nodes, move_bt_node, read_behavior_tree_graph, read_bt_node_properties, remove_bt_node, set_bt_node_property, set_bt_task_property). Alias for imcPath (add_imc_mapping, read_imc, remove_imc_mapping). SmartObjectDefinition asset path (add_smart_object_default_behavior, add_smart_object_slot, add_smart_object_slot_behavior, list_smart_object_slots, remove_smart_object_slot, set_smart_object_slot). BehaviorTree asset path; omit to sweep directory (list_bt_tasks). Alias for blackboardPath (read_blackboard). MassEntityConfigAsset path (remove_mass_trait, reorder_mass_traits, validate_mass_entity_config). BehaviorTree asset to run (run_behavior_tree)"),
  autoPruneDuplicateKeys: z.boolean().optional().describe("Remove own keys the parent chain already defines (default true)"),
  baseClass: z.string().optional().describe("Base class for an Object/Class key (e.g. /Script/Engine.Actor); for an Enum key, the enum when enumType is absent"),
  behaviorClass: z.string().optional().describe("Behavior definition asset path or class path (add_smart_object_default_behavior, add_smart_object_slot_behavior). Behavior definition class or asset path to give the slot (add_smart_object_slot)"),
  behaviorTreeOnly: z.boolean().optional().describe("Only agents running a BehaviorTree"),
  behaviorTreePath: z.string().optional().describe("Alias for assetPath (run_behavior_tree). BehaviorTree asset path (set_behavior_tree_blackboard)"),
  blackboardKeys: z.record(z.unknown()).optional().describe("Map of FBlackboardKeySelector property to blackboard key name, e.g. {BlackboardKey: 'TargetActor'}; resolved against the tree's blackboard"),
  blackboardPath: z.string().optional().describe("BlackboardData asset path (add_blackboard_key, read_blackboard, remove_blackboard_key). BlackboardData assigned before the first save; an unknown path creates nothing (create_behavior_tree). BlackboardData asset to bind (set_behavior_tree_blackboard). Child BlackboardData asset path (set_blackboard_parent)"),
  blueprintPath: z.string().optional().describe("Blueprint asset path (add_perception_component, add_smart_object_component, add_state_tree_component, configure_ai_perception_sense). Blueprint whose AIPerceptionComponent template to read; or pass actorLabel/actorPath (read_perception). Blueprint carrying the AIPerceptionComponent template (remove_sense)"),
  classFilter: z.string().optional().describe("Case-insensitive substring over the agent's class name"),
  clear: z.boolean().optional().describe("Empty both arrays; an omitted array then means empty rather than left alone (set_action_triggers). Clear the key instead of writing a value (set_live_blackboard)"),
  completeRestart: z.boolean().optional().describe("With mode=restart, restart from the root rather than resuming"),
  componentName: z.string().optional().describe("Target AIPerceptionComponent name (default: first found) (configure_ai_perception_sense, read_perception, remove_sense). StateTree component to read (default: first found) (get_state_tree_runtime)"),
  cursor: z.string().optional().describe("nextCursor from the previous page, passed back unmodified. Omit for the first page"),
  defaultBehaviorClass: z.string().optional().describe("Behavior definition asset path or class path added to DefaultBehaviorDefinitions"),
  directory: z.string().optional().describe("Content folder to sweep when assetPath is omitted (default: every BehaviorTree) (list_bt_tasks). Content root the InputMappingContext sweep covers (default /Game) (validate_input)"),
  displayCategory: z.string().optional().describe("Display category (Movement, Combat, ...) written onto UPlayerMappableKeySettings"),
  displayName: z.string().optional().describe("Display name written onto UPlayerMappableKeySettings"),
  end: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Query end (world point)"),
  enumType: z.string().optional().describe("Enum name or path for keyType=Enum"),
  executionMode: z.string().optional().describe("looped (default) | singleRun"),
  extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Half-size in world units, positive on every axis (default 100)"),
  filter: z.string().optional().describe("Case-insensitive substring over the class names"),
  filterClassOnly: z.boolean().optional().describe("Keep only tasks that declare a FilterClass"),
  gameModeClass: z.string().optional().describe("GameMode class or Blueprint path"),
  gameModePath: z.string().optional().describe("Alias for gameModeClass"),
  generatorClass: z.string().optional().describe("Generator class, short name (OnCircle) or full path"),
  hitLocation: z.record(z.unknown()).optional().describe("Impact point {x,y,z} (damage only)"),
  imcPath: z.string().optional().describe("InputMappingContext asset path (add_imc_mapping, list_imc_mappings, read_imc, remove_imc_mapping, set_imc_mapping_action, set_imc_mapping_key, set_mapping_modifiers). Audit this one InputMappingContext instead of sweeping directory (validate_input)"),
  includeActions: z.boolean().optional().describe("Include each context's action/key mappings"),
  includeAuxNodes: z.boolean().optional().describe("Include the decorators and services currently active (default true)"),
  includeDebugStrings: z.boolean().optional().describe("Include the engine's DescribeActiveTasks / DescribeActiveTrees dumps (default true)"),
  includeInherited: z.boolean().optional().describe("Keep the properties UBTNode itself declares (TreeAsset, ParentNode, NodeName), omitted by default"),
  includeProperties: z.boolean().optional().describe("Include each node's own UPROPERTY values"),
  index: z.number().optional().describe("Position among the parent's children, which is the execution order. For a decorator or service, its position in the parent's subnode list; on a SimpleParallel, the output pin (0 = main task, 1 = background) (add_bt_node, move_bt_node). Positional index into Config.Traits, as read_mass_entity_config reports it (remove_mass_trait). Index of the sense config to remove, as read_perception reports it (remove_sense)"),
  inputAction: z.string().optional().describe("Alias for inputActionPath"),
  inputActionPath: z.string().optional().describe("InputAction asset path to map (add_imc_mapping). InputAction to read; omit for every action the player has bound (get_action_value). InputAction asset path (read_input_action, set_action_triggers, set_player_mappable_settings). Select the mapping by InputAction (with key) (remove_imc_mapping). Select the mapping by its current InputAction (set_imc_mapping_action). Select the mapping by its InputAction (set_imc_mapping_key)"),
  instanceProperties: z.record(z.unknown()).optional().describe("Property writes applied to a freshly-spawned behavior instance"),
  instigatorLabel: z.string().optional().describe("The actor that caused the event"),
  instigatorPath: z.string().optional().describe("Full object path of the instigator"),
  key: z.string().optional().describe("FKey name to bind (add_imc_mapping). Report this one blackboard key; a miss lists every real key name (get_live_blackboard). Select the mapping by key (with inputActionPath) (remove_imc_mapping). Select the mapping by its key (set_imc_mapping_action). Select the mapping by its current key (set_imc_mapping_key). Blackboard key to write (set_live_blackboard)"),
  keyName: z.string().optional().describe("Key name to add (add_blackboard_key). Key name to remove (remove_blackboard_key)"),
  keyType: z.string().optional().describe("Bool (default) | Int | Float | String | Name | Vector | Rotator | Object | Class | Enum"),
  kind: z.string().optional().describe("composite | task | decorator | service. Omit for every kind; anything else is refused (list_bt_node_classes). BT node kind: composite | task | decorator | service (list_bt_tasks, read_bt_node_properties, set_bt_node_property, set_bt_task_property). all | traits | processors (default all) (list_mass_types)"),
  label: z.string().optional().describe("Actor label, also what onConflict dedupes on"),
  laneIndex: z.number().optional().describe("Lane queryMode=lane reports, as queryMode=lanes numbers them"),
  limit: z.number().optional().describe("Maximum agents to return (list_ai_agents). Rows on this page (default 200, max 2000) (list_behavior_trees, list_bt_node_classes, list_eqs_types, list_input_assets, list_state_trees). How many BehaviorTree assets a directory sweep loads (default 200) (list_bt_tasks). How many classes per kind (default 200) (list_mass_types). How many lanes to report (default 50) (query_zone_graph). How many scored items to return (default 50) (run_eqs_query). How many InputMappingContexts a sweep loads (default 200) (validate_input)"),
  location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("World point to project onto the navmesh (project_point_to_navigation). Query point for queryMode=nearest (query_zone_graph). Event location; defaults to the instigator's (hearing) or the damaged actor's (damage) (report_noise_event). World location of the volume (default origin) (spawn_nav_modifier_volume)"),
  loudness: z.number().optional().describe("Noise loudness (default 1)"),
  mappingContext: z.string().optional().describe("Alias for imcPath (add_imc_mapping, read_imc, remove_imc_mapping). InputMappingContext asset path to apply to the live player (apply_mapping_context). Name or path of one context to answer yes/no about (get_input_mapping_contexts). InputMappingContext asset path to remove from the live player (remove_mapping_context)"),
  mappingIndex: z.number().optional().describe("Index of the mapping to remove (remove_imc_mapping). Index of the mapping to retarget (set_imc_mapping_action). Index of the mapping to rebind (set_imc_mapping_key). Index of the mapping in the IMC (default 0) (set_mapping_modifiers)"),
  mappingName: z.string().optional().describe("Stable FName saved with the player mapping; must be non-empty"),
  maxRange: z.number().optional().describe("Maximum range the noise carries (0 = unlimited)"),
  mode: z.string().optional().describe("stop (default) | forced | restart | pause | resume"),
  modifiers: z.array(z.record(z.unknown())).optional().describe("Modifier specs that replace the action's own Modifiers array (set_action_triggers). Modifier objects: {type, ...props} or {class, properties}. Replaces the mapping's list (set_mapping_modifiers)"),
  name: z.string().optional().describe("Slot name (add_smart_object_slot, set_smart_object_slot). Asset name (create_behavior_tree, create_blackboard, create_eqs_query, create_input_action, create_input_mapping_context, create_smart_object_definition, create_state_tree). Blueprint name (create_game_mode, create_game_state, create_hud, create_player_controller, create_player_state)"),
  newInputActionPath: z.string().optional().describe("InputAction the mapping should use"),
  newKey: z.string().optional().describe("New FKey name"),
  node: z.string().optional().describe("Node to move: a guid, a runtime address, or a unique node name (move_bt_node). Node to remove: a guid, a runtime address, or a unique node name (remove_bt_node)"),
  nodeCategory: z.string().optional().describe("composite | task | decorator | service. Inferred from nodeClass when omitted; supplying it fails loudly on a mismatch"),
  nodeClass: z.string().optional().describe("Concrete BT node class to place, e.g. BTComposite_Selector or BTTask_MoveTo; list_bt_node_classes enumerates them (add_bt_node). BT node selector: class name, /Script path or partial name; a resolved class matches its whole subtree (list_bt_tasks, read_bt_node_properties, set_bt_node_property, set_bt_task_property)"),
  nodeName: z.string().optional().describe("Display name for the new node (add_bt_node). BT node selector: the node's object name or its NodeName display text (list_bt_tasks, read_bt_node_properties, set_bt_node_property, set_bt_task_property)"),
  nodePath: z.string().optional().describe("BT node address from read_behavior_tree_graph, e.g. 'Root.Children[0].Decorators[1]' (list_bt_tasks, read_bt_node_properties, set_bt_node_property, set_bt_task_property). Alias for node (move_bt_node, remove_bt_node)"),
  offset: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Slot offset {x,y,z}"),
  onConflict: z.string().optional().describe("skip (default, returns the existing asset) or error when the asset already exists (create_behavior_tree, create_blackboard, create_eqs_query, create_input_action, create_input_mapping_context, create_smart_object_definition, create_state_tree). skip (default, returns the actor already carrying label) or error when one does (spawn_nav_modifier_volume)"),
  optionIndex: z.number().optional().describe("Option to add the test to (default 0) (add_eqs_test). Option to remove, as read_eqs_query reports it (remove_eqs_option). Option the test belongs to (default 0) (remove_eqs_test). Option whose tests to reorder (default 0) (reorder_eqs_tests)"),
  order: z.array(z.number()).optional().describe("Current test indices in the order wanted; must be a full permutation (reorder_eqs_tests). Current trait indices in the order wanted; must be a full permutation (reorder_mass_traits)"),
  packagePath: z.string().optional().describe("Content folder (default /Game/AI) (create_behavior_tree, create_blackboard, create_state_tree). Content folder (default /Game/AI/EQS) (create_eqs_query). Content folder (default /Game/Blueprints/GameFramework) (create_game_mode, create_game_state, create_hud, create_player_controller, create_player_state). Content folder (default /Game/Input) (create_input_action, create_input_mapping_context). Content folder (default /Game/AI/SmartObjects) (create_smart_object_definition)"),
  parent: z.string().optional().describe("Node to attach under: a guid from list_bt_graph_nodes, a runtime address from read_behavior_tree_graph, a unique node name, or 'root' (default)"),
  parentClass: z.string().optional().describe("Parent deriving from GameModeBase: short name, /Script path or Blueprint asset path (create_game_mode). Parent deriving from GameStateBase: short name, /Script path or Blueprint asset path (create_game_state). Parent deriving from HUD: short name, /Script path or Blueprint asset path (create_hud). Parent deriving from PlayerController: short name, /Script path or Blueprint asset path (create_player_controller). Parent deriving from PlayerState: short name, /Script path or Blueprint asset path (create_player_state)"),
  parentPath: z.string().optional().describe("Parent BlackboardData asset path; None or omitted clears it"),
  path: z.string().optional().describe("Alias for assetPath"),
  pathfindingContext: z.string().optional().describe("Actor label whose navigation agent and filter the query uses"),
  pathfindingContextPath: z.string().optional().describe("Full object path of the pathfinding context actor; alternative to pathfindingContext"),
  perceiverLabel: z.string().optional().describe("The actor doing the perceiving"),
  perceiverPath: z.string().optional().describe("Full object path of the perceiver"),
  pieInstance: z.number().optional().describe("PIE world instance (0 = server/primary); omit for the first running one (apply_mapping_context, get_action_value, remove_mapping_context). PIE world instance (0 = server/primary); omit for the primary world (check_perception, get_bt_runtime, get_live_blackboard, get_perceived_actors, list_ai_agents, read_perception, report_noise_event, run_behavior_tree, set_live_blackboard, stop_behavior_tree). PIE world instance (0 = server/primary); omit for every running PIE world (get_input_mapping_contexts). PIE world instance (0 = server/primary) (get_state_tree_runtime). PIE world instance when world is pie or auto (query_zone_graph, run_eqs_query)"),
  playerIndex: z.number().optional().describe("Local player to act on (default 0) (apply_mapping_context, remove_mapping_context). Local player to read (default 0) (get_action_value). Player index; omit for every player (get_input_mapping_contexts)"),
  priority: z.number().optional().describe("Enhanced Input context priority; higher wins for the same key (default 0)"),
  properties: z.record(z.unknown()).optional().describe("UPROPERTY writes on the new node, as a map of property path to value (add_bt_node). Several writes at once, as a map of property path to value. Applied as one batch: a rejected write puts the others back (set_bt_node_property, set_bt_task_property)"),
  property: z.string().optional().describe("Dotted, indexed property path on the node, e.g. 'FilterClass' or 'Lines[1].Text'"),
  propertyNames: z.array(z.string()).optional().describe("Only report these UPROPERTY names"),
  purpose: z.string().optional().describe("filter | score | both"),
  querierLabel: z.string().optional().describe("Actor the query runs from, which querier-relative contexts resolve against"),
  querierPath: z.string().optional().describe("Full object path of the querier; unambiguous where a label is not"),
  queryMode: z.string().optional().describe("summary | lanes | lane | nearest (default summary)"),
  queryPath: z.string().optional().describe("EnvQuery asset path"),
  radius: z.number().optional().describe("How far from location a nearest-lane hit is accepted, in world units (default 100000)"),
  reason: z.string().optional().describe("Recorded with the stop, for the log"),
  recursive: z.boolean().optional().describe("Include subfolders of directory (default true)"),
  restartIfRunning: z.boolean().optional().describe("Restart the tree even if this one is already running"),
  rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Slot rotation {pitch,yaw,roll}"),
  runMode: z.string().optional().describe("all | best | random (default all)"),
  runningOnly: z.boolean().optional().describe("Only agents whose brain is currently running"),
  save: z.boolean().optional().describe("Persist the asset (default true); false defers the write"),
  scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Actor scale, applied after the brush is built"),
  schema: z.string().optional().describe("StateTree schema class path; omit to take the gameplay default (StateTreeComponentSchema, then StateTreeAIComponentSchema, then any concrete schema)"),
  senses: z.array(z.string()).optional().describe("Sense names (Sight, Hearing, Damage, Touch, Team, Prediction), AISenseConfig_* names or class paths"),
  senseType: z.string().optional().describe("Sight (default) | Hearing | Damage | Touch | Team | Prediction | Blueprint (configure_ai_perception_sense). Only report this sense: Sight | Hearing | Damage | Touch | Team | Prediction (get_perceived_actors). Sense to remove when index is omitted: Sight | Hearing | Damage | Touch | Team | Prediction | Blueprint (remove_sense). hearing (default) | damage (report_noise_event)"),
  settings: z.record(z.unknown()).optional().describe("Per-sense property writes, e.g. {SightRadius: 1500}"),
  slotIndex: z.number().optional().describe("Slot index (add_smart_object_slot_behavior, set_smart_object_slot). Slot index to remove (remove_smart_object_slot)"),
  start: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Query start (world point)"),
  tag: z.string().optional().describe("Tag recorded on the stimulus"),
  tags: z.array(z.unknown()).optional().describe("Slot runtime tags (gameplay tag strings or struct shapes) (add_smart_object_slot, set_smart_object_slot). Zone Graph tag names to keep, as the project settings name them (query_zone_graph)"),
  targetLabel: z.string().optional().describe("The actor being perceived (check_perception). Damaged actor (damage only) (report_noise_event)"),
  targetPath: z.string().optional().describe("Full object path of the target (check_perception). Full object path of the damaged actor (damage only) (report_noise_event)"),
  taskClass: z.string().optional().describe("Keep only tasks of this class or its subclasses, e.g. BTTask_MoveTo"),
  testClass: z.string().optional().describe("Test class, short name (Distance) or full path"),
  testIndex: z.number().optional().describe("Test to remove, as read_eqs_query reports it"),
  traitClass: z.string().optional().describe("Concrete UMassEntityTraitBase subclass to remove, short name or /Script path. Idempotent selector"),
  triggers: z.array(z.record(z.unknown())).optional().describe("Trigger specs that replace the action's own Triggers array (set_action_triggers). Trigger objects: {type, ...props} or {class, properties}. Replaces the mapping's list (set_mapping_modifiers)"),
  value: z.unknown().optional().describe("Value for property: scalar, object, array, class path, or null to clear a reference (set_bt_node_property, set_bt_task_property). Value to write, typed to the key; required unless clear is true (set_live_blackboard)"),
  valueType: z.string().optional().describe("Boolean (default) | Axis1D | Axis2D | Axis3D; an unrecognised type leaves the default"),
  verbosity: z.string().optional().describe("onlyValue | keyWithValue | detailed (default) | full"),
  world: z.string().optional().describe("World scope: auto (default) | pie | editor (check_perception, get_bt_runtime, get_live_blackboard, get_perceived_actors, list_ai_agents, read_perception, report_noise_event, run_behavior_tree, set_live_blackboard, stop_behavior_tree). pie (default) | editor | auto (get_state_tree_runtime). editor | pie | auto (default editor) (query_zone_graph). auto | pie | editor (default auto) (run_eqs_query)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
