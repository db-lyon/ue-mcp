import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const gameplayTool: ToolDef = categoryTool(
  "gameplay",
  "Gameplay systems: physics, collision, navigation, input, behavior trees, AI (EQS, perception, State Trees, Smart Objects), game framework.",
  {
    // Physics & Collision
    set_collision_profile:  bp("set_collision_profile"),
    set_simulate_physics:   bp("set_simulate_physics"),
    set_collision_enabled:  bp("set_collision_enabled"),
    set_physics_properties: bp("set_physics_properties"),
    // Navigation
    rebuild_navigation:     bp("rebuild_navigation"),
    get_navmesh_info:       bp("get_navmesh_info"),
    project_to_nav:         bp("project_point_to_navigation"),
    spawn_nav_modifier:     bp("spawn_nav_modifier_volume"),
    // Input
    create_input_action:    bp("create_input_action"),
    create_input_mapping:   bp("create_input_mapping_context"),
    list_input_assets:      bp("list_input_assets"),
    // Behavior Trees
    list_behavior_trees:    bp("list_behavior_trees"),
    get_behavior_tree_info: bp("get_behavior_tree_info"),
    create_blackboard:      bp("create_blackboard"),
    create_behavior_tree:   bp("create_behavior_tree"),
    // EQS
    create_eqs_query:       bp("create_eqs_query"),
    list_eqs_queries:       bp("list_eqs_queries"),
    // AI Perception
    add_perception:         bp("add_perception_component"),
    configure_sense:        bp("configure_ai_perception_sense"),
    // State Trees
    create_state_tree:      bp("create_state_tree"),
    list_state_trees:       bp("list_state_trees"),
    add_state_tree_component: bp("add_state_tree_component"),
    // Smart Objects
    create_smart_object_def: bp("create_smart_object_definition"),
    add_smart_object_component: bp("add_smart_object_component"),
    // Game Framework
    create_game_mode:       bp("create_game_mode"),
    create_game_state:      bp("create_game_state"),
    create_player_controller: bp("create_player_controller"),
    create_player_state:    bp("create_player_state"),
    create_hud:             bp("create_hud"),
    set_world_game_mode:    bp("set_world_game_mode"),
    get_framework_info:     bp("get_game_framework_info"),
  },
  `- set_collision_profile: Set collision preset. Params: actorLabel, profileName
- set_simulate_physics: Toggle physics. Params: actorLabel, simulate
- set_collision_enabled: Set collision mode. Params: actorLabel, collisionEnabled
- set_physics_properties: Set mass/damping/gravity. Params: actorLabel, mass?, linearDamping?, angularDamping?, enableGravity?
- rebuild_navigation: Rebuild navmesh
- get_navmesh_info: Query nav system
- project_to_nav: Project point to navmesh. Params: location, extent?
- spawn_nav_modifier: Place nav modifier. Params: location, extent?, areaClass?
- create_input_action: Create InputAction. Params: name, packagePath?, valueType?
- create_input_mapping: Create InputMappingContext. Params: name, packagePath?
- list_input_assets: List input assets. Params: directory?, recursive?
- list_behavior_trees: List BTs. Params: directory?, recursive?
- get_behavior_tree_info: Inspect BT. Params: assetPath
- create_blackboard: Create Blackboard. Params: name, packagePath?
- create_behavior_tree: Create BT. Params: name, packagePath?, blackboardPath?
- create_eqs_query: Create EQS query. Params: name, packagePath?
- list_eqs_queries: List EQS queries. Params: directory?
- add_perception: Add AIPerceptionComponent. Params: blueprintPath, senses?
- configure_sense: Configure perception sense. Params: blueprintPath, senseType (Sight|Hearing|Damage|Touch|Team), settings?
- create_state_tree: Create StateTree. Params: name, packagePath?
- list_state_trees: List StateTrees. Params: directory?
- add_state_tree_component: Add StateTreeComponent. Params: blueprintPath
- create_smart_object_def: Create SmartObjectDefinition. Params: name, packagePath?
- add_smart_object_component: Add SmartObjectComponent. Params: blueprintPath
- create_game_mode: Create GameMode BP. Params: name, packagePath?, parentClass?, defaults? (defaultPawnClass, hudClass, playerControllerClass, gameStateClass)
- create_game_state: Create GameState BP. Params: name, packagePath?, parentClass?
- create_player_controller: Create PlayerController BP. Params: name, packagePath?, parentClass?
- create_player_state: Create PlayerState BP. Params: name, packagePath?
- create_hud: Create HUD BP. Params: name, packagePath?
- set_world_game_mode: Set level GameMode override. Params: gameModePath
- get_framework_info: Get level framework classes`,
  {
    actorLabel: z.string().optional(),
    profileName: z.string().optional(),
    simulate: z.boolean().optional(),
    collisionEnabled: z.string().optional(),
    mass: z.number().optional(),
    linearDamping: z.number().optional(),
    angularDamping: z.number().optional(),
    enableGravity: z.boolean().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    areaClass: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    valueType: z.string().optional(),
    directory: z.string().optional(),
    recursive: z.boolean().optional(),
    assetPath: z.string().optional(),
    blackboardPath: z.string().optional(),
    blueprintPath: z.string().optional(),
    senses: z.array(z.string()).optional(),
    senseType: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
    parentClass: z.string().optional(),
    defaults: z.record(z.string()).optional(),
    gameModePath: z.string().optional(),
  },
);
