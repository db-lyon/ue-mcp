import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const gameplayTool: ToolDef = categoryTool(
  "gameplay",
  "Gameplay systems: physics, collision, navigation, input, behavior trees, blackboards.",
  {
    set_collision_profile:  bp("set_collision_profile"),
    set_simulate_physics:   bp("set_simulate_physics"),
    set_collision_enabled:  bp("set_collision_enabled"),
    set_physics_properties: bp("set_physics_properties"),
    rebuild_navigation:     bp("rebuild_navigation"),
    get_navmesh_info:       bp("get_navmesh_info"),
    project_to_nav:         bp("project_point_to_navigation"),
    spawn_nav_modifier:     bp("spawn_nav_modifier_volume"),
    create_input_action:    bp("create_input_action"),
    create_input_mapping:   bp("create_input_mapping_context"),
    list_input_assets:      bp("list_input_assets"),
    list_behavior_trees:    bp("list_behavior_trees"),
    get_behavior_tree_info: bp("get_behavior_tree_info"),
    create_blackboard:      bp("create_blackboard"),
    create_behavior_tree:   bp("create_behavior_tree"),
  },
  `- set_collision_profile: Set collision preset. Params: actorLabel, profileName
- set_simulate_physics: Toggle physics. Params: actorLabel, simulate
- set_collision_enabled: Set collision mode. Params: actorLabel, collisionEnabled
- set_physics_properties: Set mass/damping/gravity. Params: actorLabel, mass?, linearDamping?, angularDamping?, enableGravity?
- rebuild_navigation: Rebuild navmesh
- get_navmesh_info: Query nav system
- project_to_nav: Project point to navmesh. Params: location {x,y,z}, extent?
- spawn_nav_modifier: Place nav modifier. Params: location {x,y,z}, extent?, areaClass?
- create_input_action: Create InputAction. Params: name, packagePath?, valueType?
- create_input_mapping: Create InputMappingContext. Params: name, packagePath?
- list_input_assets: List input assets. Params: directory?, recursive?
- list_behavior_trees: List BTs. Params: directory?, recursive?
- get_behavior_tree_info: Inspect BT. Params: assetPath
- create_blackboard: Create Blackboard. Params: name, packagePath?
- create_behavior_tree: Create BT. Params: name, packagePath?, blackboardPath?`,
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
  },
);
