// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_add_blackboard_key = {"properties":{"blackboard":{"type":"object","properties":{"refPath":{}}},"keyName":{"type":"string"},"keyType":{"type":"object","properties":{"refPath":{}}},"bInstanceSynced":{"type":"boolean"}},"required":["blackboard","keyName","keyType"]} as const;
const S_epic_add_body = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_add_composite = {"properties":{"parent":{"type":"object","properties":{"refPath":{}}},"compositeClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["parent","compositeClass"]} as const;
const S_epic_add_constraint = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"bone1Name":{"type":"string"},"bone2Name":{"type":"string"}},"required":["physicsAsset","bone1Name","bone2Name"]} as const;
const S_epic_add_decorator = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"decoratorClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["owner","decoratorClass"]} as const;
const S_epic_add_option = {"properties":{"query":{"type":"object","properties":{"refPath":{}}},"generatorClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["query","generatorClass"]} as const;
const S_epic_add_root_composite = {"properties":{"behaviorTree":{"type":"object","properties":{"refPath":{}}},"compositeClass":{"type":"object","properties":{"refPath":{}}}},"required":["behaviorTree","compositeClass"]} as const;
const S_epic_add_root_decorator = {"properties":{"behaviorTree":{"type":"object","properties":{"refPath":{}}},"decoratorClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["behaviorTree","decoratorClass"]} as const;
const S_epic_add_service = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"serviceClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["owner","serviceClass"]} as const;
const S_epic_add_tag = {"properties":{"tagName":{"type":"string"},"comment":{"type":"string"},"tagSource":{"type":"string"},"devComment":{"type":"string"}},"required":["tagName","tagSource"]} as const;
const S_epic_add_tag_source = {"properties":{"sourceName":{"type":"string"},"tagRoot":{"type":"string"}},"required":["sourceName","tagRoot"]} as const;
const S_epic_add_task = {"properties":{"parent":{"type":"object","properties":{"refPath":{}}},"taskClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["parent","taskClass"]} as const;
const S_epic_add_test = {"properties":{"option":{"type":"object","properties":{"refPath":{}}},"testClass":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["option","testClass"]} as const;
const S_epic_apply_input = {"properties":{"entries":{"type":"array","items":{"type":"object","properties":{"action":{"type":"object","properties":{"refPath":{}}},"key":{"type":"object","properties":{"keyName":{"type":"string"}}},"value":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"durationSeconds":{"type":"number"}},"required":["entries","durationSeconds"]} as const;
const S_epic_create_behavior_tree = {"properties":{"folderPath":{"type":"object","properties":{"path":{"type":"string"}}},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_blackboard_data = {"properties":{"folderPath":{"type":"object","properties":{"path":{"type":"string"}}},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_environment_query = {"properties":{"folderPath":{"type":"object","properties":{"path":{"type":"string"}}},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_from_mesh = {"properties":{"meshPath":{"type":"string"},"bAssignToMesh":{"type":"boolean"}},"required":["meshPath","bAssignToMesh"]} as const;
const S_epic_describe_nav_mesh = {"properties":{"bIncludeNonDefaultProperties":{"type":"boolean"},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["bIncludeNonDefaultProperties","navData","agentName","world"]} as const;
const S_epic_describe_poly_at_location = {"properties":{"point":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["point","navData","agentName","world"]} as const;
const S_epic_find_distance_to_wall = {"properties":{"point":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"maxDistance":{"type":"number"},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["point","maxDistance","navData","agentName","world"]} as const;
const S_epic_find_path = {"properties":{"start":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"end":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"areaCostOverrides":{"type":"array","items":{"type":"object","properties":{"areaClass":{"type":"object","properties":{"refPath":{}}},"cost":{"type":"number"}}}},"excludedAreaClasses":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"bAllowPartialPath":{"type":"boolean"},"costLimit":{"type":"number"},"maxPathPoints":{"type":"integer"},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["start","end","areaCostOverrides","excludedAreaClasses","bAllowPartialPath","costLimit","maxPathPoints","navData","agentName","world"]} as const;
const S_epic_find_referencers_by_tag = {"properties":{"tagName":{"type":"string"}},"required":["tagName"]} as const;
const S_epic_get_actor_nav_info = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"bIncludeNonDefaultProperties":{"type":"boolean"}},"required":["actor","bIncludeNonDefaultProperties"]} as const;
const S_epic_get_allowed_blackboard_keys = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"selectorPropertyName":{"type":"string"}},"required":["node","selectorPropertyName"]} as const;
const S_epic_get_blackboard = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_get_blackboard__behavior_tree_toolset = {"properties":{"behaviorTree":{"type":"object","properties":{"refPath":{}}}},"required":["behaviorTree"]} as const;
const S_epic_get_blackboard_key_names = {"properties":{"blackboard":{"type":"object","properties":{"refPath":{}}}},"required":["blackboard"]} as const;
const S_epic_get_body_mass_scale = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_get_body_names = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}}},"required":["physicsAsset"]} as const;
const S_epic_get_body_physics_mode = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_get_body_shapes = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_get_child_graph_nodes = {"properties":{"parent":{"type":"object","properties":{"refPath":{}}}},"required":["parent"]} as const;
const S_epic_get_children = {"properties":{"composite":{"type":"object","properties":{"refPath":{}}}},"required":["composite"]} as const;
const S_epic_get_condition_description = {"properties":{"condition":{"type":"object","properties":{"_structType":{"type":"string"}}}},"required":["condition"]} as const;
const S_epic_get_constraints = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}}},"required":["physicsAsset"]} as const;
const S_epic_get_decorator_graph_nodes = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}}},"required":["owner"]} as const;
const S_epic_get_generator = {"properties":{"option":{"type":"object","properties":{"refPath":{}}}},"required":["option"]} as const;
const S_epic_get_nav_mesh_build_status = {"properties":{"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["navData","agentName","world"]} as const;
const S_epic_get_navigation_system_info = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"bIncludeNonDefaultProperties":{"type":"boolean"}},"required":["world","bIncludeNonDefaultProperties"]} as const;
const S_epic_get_node_depth = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}},"node_index":{"type":"integer"}},"required":["behavior_tree","node_index"]} as const;
const S_epic_get_node_depths = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_get_node_instance = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_option_graph_nodes = {"properties":{"query":{"type":"object","properties":{"refPath":{}}}},"required":["query"]} as const;
const S_epic_get_option_instance = {"properties":{"option":{"type":"object","properties":{"refPath":{}}}},"required":["option"]} as const;
const S_epic_get_query_description = {"properties":{"queryDefinition":{"type":"object","properties":{"sharedDefinition":{"type":"object"},"schemaClass":{"type":"object","properties":{"refPath":{}}},"editableConditions":{"type":"array","items":{"type":"object","properties":{"expressionDepth":{"type":"integer"},"operator":{"type":"string","enum":["And","Or"]},"bInvert":{"type":"boolean"},"condition":{"type":"object","properties":{"_structType":{"type":"string"}}}}}}}}},"required":["queryDefinition"]} as const;
const S_epic_get_reachable_area = {"properties":{"origin":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"maxCost":{"type":"number"},"maxPolyCenters":{"type":"integer"},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["origin","maxCost","maxPolyCenters","navData","agentName","world"]} as const;
const S_epic_get_root_decorators = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_get_root_graph_node = {"properties":{"behaviorTree":{"type":"object","properties":{"refPath":{}}}},"required":["behaviorTree"]} as const;
const S_epic_get_root_graph_node__environment_query_toolset = {"properties":{"query":{"type":"object","properties":{"refPath":{}}}},"required":["query"]} as const;
const S_epic_get_service_graph_nodes = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}}},"required":["owner"]} as const;
const S_epic_get_subtree = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_subtree__behavior_tree_toolset = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_tag_info = {"properties":{"tagName":{"type":"string"}},"required":["tagName"]} as const;
const S_epic_get_test_graph_nodes = {"properties":{"option":{"type":"object","properties":{"refPath":{}}}},"required":["option"]} as const;
const S_epic_get_test_instance = {"properties":{"test":{"type":"object","properties":{"refPath":{}}}},"required":["test"]} as const;
const S_epic_get_tile_info = {"properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["location","navData","agentName","world"]} as const;
const S_epic_is_input_applied = {"properties":{"entries":{"type":"array","items":{"type":"object","properties":{"action":{"type":"object","properties":{"refPath":{}}},"key":{"type":"object","properties":{"keyName":{"type":"string"}}},"value":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}}},"required":["entries"]} as const;
const S_epic_list_nav_agent_actors = {"properties":{"classFilter":{"type":"object","properties":{"refPath":{}}},"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"radius":{"type":"number"},"maxResults":{"type":"integer"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["classFilter","center","radius","maxResults","world"]} as const;
const S_epic_list_nav_areas = {"properties":{"world":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_list_nav_bounds_volumes = {"properties":{"world":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_list_nav_data = {"properties":{"world":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_list_nav_invokers = {"properties":{"world":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_list_nav_links = {"properties":{"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"radius":{"type":"number"},"source":{"type":"string","enum":["All","ProxyActors","CustomLinkComponents","InMeshLinks"]},"maxResults":{"type":"integer"},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["center","radius","source","maxResults","navData","agentName","world"]} as const;
const S_epic_list_nav_modifiers = {"properties":{"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"radius":{"type":"number"},"maxResults":{"type":"integer"},"bIncludeNonDefaultProperties":{"type":"boolean"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["center","radius","maxResults","bIncludeNonDefaultProperties","world"]} as const;
const S_epic_list_nodes = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_list_supported_agents = {"properties":{"world":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_list_tag_source_roots = {"properties":{}} as const;
const S_epic_list_tag_sources = {"properties":{}} as const;
const S_epic_list_tags = {"properties":{"parentTag":{"type":"string"}},"required":["parentTag"]} as const;
const S_epic_list_tags_in_source = {"properties":{"tagSource":{"type":"string"}},"required":["tagSource"]} as const;
const S_epic_list_worlds = {"properties":{}} as const;
const S_epic_move_graph_node = {"properties":{"parent":{"type":"object","properties":{"refPath":{}}},"child":{"type":"object","properties":{"refPath":{}}},"newIndex":{"type":"integer"}},"required":["parent","child","newIndex"]} as const;
const S_epic_move_option = {"properties":{"query":{"type":"object","properties":{"refPath":{}}},"option":{"type":"object","properties":{"refPath":{}}},"newIndex":{"type":"integer"}},"required":["query","option","newIndex"]} as const;
const S_epic_move_subnode = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"subnode":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["owner","subnode","index"]} as const;
const S_epic_move_tags = {"properties":{"tagsToMove":{"type":"array","items":{"type":"string"}},"targetTagSource":{"type":"string"}},"required":["tagsToMove","targetTagSource"]} as const;
const S_epic_move_test = {"properties":{"option":{"type":"object","properties":{"refPath":{}}},"test":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["option","test","index"]} as const;
const S_epic_project_point_to_nav_mesh = {"properties":{"point":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"horizontalExtent":{"type":"number"},"verticalExtent":{"type":"number"},"bAllAgents":{"type":"boolean"},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["point","horizontalExtent","verticalExtent","bAllAgents","navData","agentName","world"]} as const;
const S_epic_query_nav_octree = {"properties":{"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"radius":{"type":"number"},"maxResults":{"type":"integer"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["center","radius","maxResults","world"]} as const;
const S_epic_raycast_nav_mesh = {"properties":{"start":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"end":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"navData":{"type":"object","properties":{"refPath":{}}},"agentName":{"type":"string"},"world":{"type":"object","properties":{"refPath":{}}}},"required":["start","end","navData","agentName","world"]} as const;
const S_epic_remove_blackboard_key = {"properties":{"blackboard":{"type":"object","properties":{"refPath":{}}},"keyName":{"type":"string"}},"required":["blackboard","keyName"]} as const;
const S_epic_remove_body = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_remove_constraint = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"bone1Name":{"type":"string"},"bone2Name":{"type":"string"}},"required":["physicsAsset","bone1Name","bone2Name"]} as const;
const S_epic_remove_graph_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_remove_option = {"properties":{"option":{"type":"object","properties":{"refPath":{}}}},"required":["option"]} as const;
const S_epic_remove_shape = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"}},"required":["physicsAsset","boneName","shapeName"]} as const;
const S_epic_remove_subnode = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"subnode":{"type":"object","properties":{"refPath":{}}}},"required":["owner","subnode"]} as const;
const S_epic_remove_tag = {"properties":{"tagName":{"type":"string"}},"required":["tagName"]} as const;
const S_epic_remove_test = {"properties":{"option":{"type":"object","properties":{"refPath":{}}},"test":{"type":"object","properties":{"refPath":{}}}},"required":["option","test"]} as const;
const S_epic_rename_tag = {"properties":{"oldTagName":{"type":"string"},"newTagName":{"type":"string"}},"required":["oldTagName","newTagName"]} as const;
const S_epic_set_blackboard = {"properties":{"behaviorTree":{"type":"object","properties":{"refPath":{}}},"blackboard":{"type":"object","properties":{"refPath":{}}}},"required":["behaviorTree","blackboard"]} as const;
const S_epic_set_body_mass_scale = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"massScale":{"type":"number"}},"required":["physicsAsset","boneName","massScale"]} as const;
const S_epic_set_body_physics_mode = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"mode":{"type":"string","enum":["Default","Kinematic","Simulated"]}},"required":["physicsAsset","boneName","mode"]} as const;
const S_epic_set_box = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"},"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"extentX":{"type":"number"},"extentY":{"type":"number"},"extentZ":{"type":"number"}},"required":["physicsAsset","boneName","shapeName","center","rotation","extentX","extentY","extentZ"]} as const;
const S_epic_set_capsule = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"},"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"radius":{"type":"number"},"length":{"type":"number"}},"required":["physicsAsset","boneName","shapeName","center","rotation","radius","length"]} as const;
const S_epic_set_constraint_limits = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"info":{"type":"object","properties":{"bone1Name":{"type":"string"},"bone2Name":{"type":"string"},"swing1Motion":{"type":"string","enum":["Free","Limited","Locked"]},"swing1LimitDegrees":{"type":"number"},"swing2Motion":{"type":"string","enum":["Free","Limited","Locked"]},"swing2LimitDegrees":{"type":"number"},"twistMotion":{"type":"string","enum":["Free","Limited","Locked"]},"twistLimitDegrees":{"type":"number"}}}},"required":["physicsAsset","info"]} as const;
const S_epic_set_node_blackboard_key = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"selectorPropertyName":{"type":"string"},"keyName":{"type":"string"}},"required":["node","selectorPropertyName","keyName"]} as const;
const S_epic_set_sphere = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"},"center":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"radius":{"type":"number"}},"required":["physicsAsset","boneName","shapeName","center","radius"]} as const;
const S_epic_set_test_enabled = {"properties":{"test":{"type":"object","properties":{"refPath":{}}},"bEnabled":{"type":"boolean"}},"required":["test","bEnabled"]} as const;

/** 99 wrapped engine tools routed to the `gameplay` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_blackboard_key: {
    epicSchema: S_epic_add_blackboard_key,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddBlackboardKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds a key to the blackboard. Available on UE 5.9; absent from UE 5.8. Params: blackboard, keyName, keyType, bInstanceSynced?", "epic_call_tool"),
  },
  epic_add_body: {
    epicSchema: S_epic_add_body,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.AddBody", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds a new empty body for the given bone. Params: physicsAsset, boneName", "epic_call_tool"),
  },
  epic_add_composite: {
    epicSchema: S_epic_add_composite,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddComposite", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds a composite as a child of a composite (or root) parent. Available on UE 5.9; absent from UE 5.8. Params: parent, compositeClass, index?", "epic_call_tool"),
  },
  epic_add_constraint: {
    epicSchema: S_epic_add_constraint,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.AddConstraint", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds a new constraint between two bodies. Both bodies must already exist. Params: physicsAsset, bone1Name, bone2Name", "epic_call_tool"),
  },
  epic_add_decorator: {
    epicSchema: S_epic_add_decorator,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddDecorator", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds a decorator subnode to an owner. Available on UE 5.9; absent from UE 5.8. Params: owner, decoratorClass, index?", "epic_call_tool"),
  },
  epic_add_option: {
    epicSchema: S_epic_add_option,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.AddOption", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Adds an option to the query. An option wraps one generator that produces the items to score. Available on UE 5.9; absent from UE 5.8. Params: query, generatorClass, index?", "epic_call_tool"),
  },
  epic_add_root_composite: {
    epicSchema: S_epic_add_root_composite,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddRootComposite", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds the tree's single root composite. Available on UE 5.9; absent from UE 5.8. Params: behaviorTree, compositeClass", "epic_call_tool"),
  },
  epic_add_root_decorator: {
    epicSchema: S_epic_add_root_decorator,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddRootDecorator", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds a root-level decorator, used when the tree is referenced as a subtree. Available on UE 5.9; absent from UE 5.8. Params: behaviorTree, decoratorClass, index?", "epic_call_tool"),
  },
  epic_add_service: {
    epicSchema: S_epic_add_service,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddService", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds a service subnode to a composite owner. Available on UE 5.9; absent from UE 5.8. Params: owner, serviceClass, index?", "epic_call_tool"),
  },
  epic_add_tag: {
    epicSchema: S_epic_add_tag,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.AddTag", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic GameplayTagsToolset.GameplayTagsToolset] Adds a new gameplay tag to the project. This should ONLY be called after getting explicit direction or permission from the user. Params: tagName, comment?, tagSource, devComment?", "epic_call_tool"),
  },
  epic_add_tag_source: {
    epicSchema: S_epic_add_tag_source,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.AddTagSource", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameplayTagsToolset.GameplayTagsToolset] Creates a new tag list source that tags can then be added to. Its file is written when the first tag is added. No-ops if a tag list source with that name already exists in the same directory. This should ONLY be called after getting explicit direction or permission from the user. Available on UE 5.9; absent from UE 5.8. Params: sourceName, tagRoot", "epic_call_tool"),
  },
  epic_add_task: {
    epicSchema: S_epic_add_task,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.AddTask", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Adds a task as a child of a composite parent. Available on UE 5.9; absent from UE 5.8. Params: parent, taskClass, index?", "epic_call_tool"),
  },
  epic_add_test: {
    epicSchema: S_epic_add_test,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.AddTest", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Adds a test to an option. Tests filter and score the option's generated items. Available on UE 5.9; absent from UE 5.8. Params: option, testClass, index?", "epic_call_tool"),
  },
  epic_apply_input: {
    epicSchema: S_epic_apply_input,
    epicTool: { toolset: "InputToolset.InputToolset", name: "InputToolset.InputToolset.ApplyInput", engines: ["5.9"] },
    ...bp("mutate", "[Epic InputToolset.InputToolset] Presses one or more actions/keys together, holds them for DurationSeconds, then releases them, completing once the release is done. Available on UE 5.9; absent from UE 5.8. Params: entries, durationSeconds", "epic_call_tool"),
  },
  epic_create_behavior_tree: {
    epicSchema: S_epic_create_behavior_tree,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.CreateBehaviorTree", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Creates a new BehaviorTree asset, ready to edit. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_create_blackboard_data: {
    epicSchema: S_epic_create_blackboard_data,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.CreateBlackboardData", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Creates a new BlackboardData asset. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_create_environment_query: {
    epicSchema: S_epic_create_environment_query,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.CreateEnvironmentQuery", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Creates a new Environment Query asset. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_create_from_mesh: {
    epicSchema: S_epic_create_from_mesh,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.CreateFromMesh", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Creates a physics asset from a skeletal mesh, auto-generating collision bodies for each bone. The asset is placed in the same folder as the mesh with the suffix \"_PhysicsAsset\". Params: meshPath, bAssignToMesh", "epic_call_tool"),
  },
  epic_describe_nav_mesh: {
    epicSchema: S_epic_describe_nav_mesh,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.DescribeNavMesh", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Dumps the full generation and query configuration of one navmesh. Available on UE 5.9; absent from UE 5.8. Params: bIncludeNonDefaultProperties, navData, agentName, world", "epic_call_tool"),
  },
  epic_describe_poly_at_location: {
    epicSchema: S_epic_describe_poly_at_location,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.DescribePolyAtLocation", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Describes the navmesh polygon nearest a location. Available on UE 5.9; absent from UE 5.8. Params: point, navData, agentName, world", "epic_call_tool"),
  },
  epic_find_distance_to_wall: {
    epicSchema: S_epic_find_distance_to_wall,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.FindDistanceToWall", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Measures the distance from a point to the nearest navmesh wall (walkable-space boundary). Available on UE 5.9; absent from UE 5.8. Params: point, maxDistance, navData, agentName, world", "epic_call_tool"),
  },
  epic_find_path: {
    epicSchema: S_epic_find_path,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.FindPath", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Finds a path between two points. Area cost overrides and exclusions apply to this query only; no state is touched. Available on UE 5.9; absent from UE 5.8. Params: start, end, areaCostOverrides, excludedAreaClasses, bAllowPartialPath, costLimit, maxPathPoints, navData, agentName, world", "epic_call_tool"),
  },
  epic_find_referencers_by_tag: {
    epicSchema: S_epic_find_referencers_by_tag,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.FindReferencersByTag", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns assets that reference a gameplay tag. Params: tagName", "epic_call_tool"),
  },
  epic_get_actor_nav_info: {
    epicSchema: S_epic_get_actor_nav_info,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.GetActorNavInfo", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Deep navigation report for one actor: everything navigation-relevant it carries and, when it follows paths, where it is going. Available on UE 5.9; absent from UE 5.8. Params: actor, bIncludeNonDefaultProperties", "epic_call_tool"),
  },
  epic_get_allowed_blackboard_keys: {
    epicSchema: S_epic_get_allowed_blackboard_keys,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetAllowedBlackboardKeys", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the blackboard keys a node's key selector accepts, matching the editor's key dropdown: the tree blackboard's own keys (and its parents') filtered by the selector's allowed types (an empty filter accepts any key). Available on UE 5.9; absent from UE 5.8. Params: node, selectorPropertyName", "epic_call_tool"),
  },
  epic_get_blackboard: {
    epicSchema: S_epic_get_blackboard,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_blackboard", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns the blackboard asset for this behavior tree. Available on UE 5.8; absent from UE 5.9. Params: behavior_tree", "epic_call_tool"),
  },
  epic_get_blackboard__behavior_tree_toolset: {
    epicSchema: S_epic_get_blackboard__behavior_tree_toolset,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetBlackboard", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the tree's blackboard asset. Available on UE 5.9; absent from UE 5.8. Params: behaviorTree", "epic_call_tool"),
  },
  epic_get_blackboard_key_names: {
    epicSchema: S_epic_get_blackboard_key_names,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetBlackboardKeyNames", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the blackboard's own key names, not including parent-chain keys. Available on UE 5.9; absent from UE 5.8. Params: blackboard", "epic_call_tool"),
  },
  epic_get_body_mass_scale: {
    epicSchema: S_epic_get_body_mass_scale,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.GetBodyMassScale", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns the mass-scale multiplier for the given body. Params: physicsAsset, boneName", "epic_call_tool"),
  },
  epic_get_body_names: {
    epicSchema: S_epic_get_body_names,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.GetBodyNames", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns the bone name for each rigid body in a physics asset. Params: physicsAsset", "epic_call_tool"),
  },
  epic_get_body_physics_mode: {
    epicSchema: S_epic_get_body_physics_mode,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.GetBodyPhysicsMode", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns the physics simulation mode for the given body. Params: physicsAsset, boneName", "epic_call_tool"),
  },
  epic_get_body_shapes: {
    epicSchema: S_epic_get_body_shapes,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.GetBodyShapes", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns all collision shapes assigned to a body. Params: physicsAsset, boneName", "epic_call_tool"),
  },
  epic_get_child_graph_nodes: {
    epicSchema: S_epic_get_child_graph_nodes,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetChildGraphNodes", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the child graph nodes of a composite (or root) parent, in sibling order. Available on UE 5.9; absent from UE 5.8. Params: parent", "epic_call_tool"),
  },
  epic_get_children: {
    epicSchema: S_epic_get_children,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_children", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns direct child nodes of a composite node. Available on UE 5.8; absent from UE 5.9. Params: composite", "epic_call_tool"),
  },
  epic_get_condition_description: {
    epicSchema: S_epic_get_condition_description,
    epicTool: { toolset: "WorldConditionsToolset.WorldConditionTools", name: "WorldConditionsToolset.WorldConditionTools.GetConditionDescription", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic WorldConditionsToolset.WorldConditionTools] Returns a human-readable description of a single world condition. The condition must be passed as an FInstancedStruct containing an FWorldConditionBase-derived struct. Params: condition", "epic_call_tool"),
  },
  epic_get_constraints: {
    epicSchema: S_epic_get_constraints,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.GetConstraints", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns all constraints in the physics asset with their current angular limits. Params: physicsAsset", "epic_call_tool"),
  },
  epic_get_decorator_graph_nodes: {
    epicSchema: S_epic_get_decorator_graph_nodes,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetDecoratorGraphNodes", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the decorator subnode graph nodes attached to a graph node. Available on UE 5.9; absent from UE 5.8. Params: owner", "epic_call_tool"),
  },
  epic_get_generator: {
    epicSchema: S_epic_get_generator,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.GetGenerator", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.EnvironmentQueryToolset] Returns the generator instance wrapped by an option. This is the object that holds the generator's settings. Available on UE 5.9; absent from UE 5.8. Params: option", "epic_call_tool"),
  },
  epic_get_nav_mesh_build_status: {
    epicSchema: S_epic_get_nav_mesh_build_status,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.GetNavMeshBuildStatus", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Reports whether navmesh building is up to date and what rebuild work is pending or suspended. Addressing a navmesh narrows the report to it and adds tile detail (Recast only); null NavData with empty AgentName sweeps every navdata without tile detail. Available on UE 5.9; absent from UE 5.8. Params: navData, agentName, world", "epic_call_tool"),
  },
  epic_get_navigation_system_info: {
    epicSchema: S_epic_get_navigation_system_info,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.GetNavigationSystemInfo", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Reports the state of a world's navigation system. Available on UE 5.9; absent from UE 5.8. Params: world, bIncludeNonDefaultProperties", "epic_call_tool"),
  },
  epic_get_node_depth: {
    epicSchema: S_epic_get_node_depth,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_node_depth", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns the tree depth of a node by its list_nodes index. Available on UE 5.8; absent from UE 5.9. Params: behavior_tree, node_index", "epic_call_tool"),
  },
  epic_get_node_depths: {
    epicSchema: S_epic_get_node_depths,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_node_depths", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns tree depths for all nodes, matching list_nodes order. Available on UE 5.8; absent from UE 5.9. Params: behavior_tree", "epic_call_tool"),
  },
  epic_get_node_instance: {
    epicSchema: S_epic_get_node_instance,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetNodeInstance", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the behavior node instance that a graph node wraps. This is the object that holds the node's settings. Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_get_option_graph_nodes: {
    epicSchema: S_epic_get_option_graph_nodes,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.GetOptionGraphNodes", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.EnvironmentQueryToolset] Returns the query's option graph nodes, in order. Available on UE 5.9; absent from UE 5.8. Params: query", "epic_call_tool"),
  },
  epic_get_option_instance: {
    epicSchema: S_epic_get_option_instance,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.GetOptionInstance", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.EnvironmentQueryToolset] Returns the option instance a graph node wraps. Available on UE 5.9; absent from UE 5.8. Params: option", "epic_call_tool"),
  },
  epic_get_query_description: {
    epicSchema: S_epic_get_query_description,
    epicTool: { toolset: "WorldConditionsToolset.WorldConditionTools", name: "WorldConditionsToolset.WorldConditionTools.GetQueryDescription", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic WorldConditionsToolset.WorldConditionTools] Returns a human-readable description of a world condition query. Params: queryDefinition", "epic_call_tool"),
  },
  epic_get_reachable_area: {
    epicSchema: S_epic_get_reachable_area,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.GetReachableArea", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Computes the navmesh region reachable from an origin within a path-cost budget, connectivity respected. Available on UE 5.9; absent from UE 5.8. Params: origin, maxCost, maxPolyCenters, navData, agentName, world", "epic_call_tool"),
  },
  epic_get_root_decorators: {
    epicSchema: S_epic_get_root_decorators,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_root_decorators", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns root-level decorators on this tree. Available on UE 5.8; absent from UE 5.9. Params: behavior_tree", "epic_call_tool"),
  },
  epic_get_root_graph_node: {
    epicSchema: S_epic_get_root_graph_node,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetRootGraphNode", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the tree's root graph node. Available on UE 5.9; absent from UE 5.8. Params: behaviorTree", "epic_call_tool"),
  },
  epic_get_root_graph_node__environment_query_toolset: {
    epicSchema: S_epic_get_root_graph_node__environment_query_toolset,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.GetRootGraphNode", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.EnvironmentQueryToolset] Returns the query's root graph node. Options connect to it. Available on UE 5.9; absent from UE 5.8. Params: query", "epic_call_tool"),
  },
  epic_get_service_graph_nodes: {
    epicSchema: S_epic_get_service_graph_nodes,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetServiceGraphNodes", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the service subnode graph nodes attached to a graph node. Available on UE 5.9; absent from UE 5.8. Params: owner", "epic_call_tool"),
  },
  epic_get_subtree: {
    epicSchema: S_epic_get_subtree,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_subtree", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns the sub-BT asset referenced by a RunBehavior task. Available on UE 5.8; absent from UE 5.9. Params: node", "epic_call_tool"),
  },
  epic_get_subtree__behavior_tree_toolset: {
    epicSchema: S_epic_get_subtree__behavior_tree_toolset,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.GetSubtree", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.BehaviorTreeToolset] Returns the BehaviorTree run by a RunBehavior task node instance. Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_get_tag_info: {
    epicSchema: S_epic_get_tag_info,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.GetTagInfo", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns detailed information about a specific gameplay tag. Params: tagName", "epic_call_tool"),
  },
  epic_get_test_graph_nodes: {
    epicSchema: S_epic_get_test_graph_nodes,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.GetTestGraphNodes", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.EnvironmentQueryToolset] Returns the test subnode graph nodes attached to an option, in order. Available on UE 5.9; absent from UE 5.8. Params: option", "epic_call_tool"),
  },
  epic_get_test_instance: {
    epicSchema: S_epic_get_test_instance,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.GetTestInstance", engines: ["5.9"] },
    ...bp("read", "[Epic AIModuleToolset.EnvironmentQueryToolset] Returns the test instance a graph node wraps. This is the object that holds the test's settings. Available on UE 5.9; absent from UE 5.8. Params: test", "epic_call_tool"),
  },
  epic_get_tile_info: {
    epicSchema: S_epic_get_tile_info,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.GetTileInfo", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Describes the navmesh tile containing a location. Available on UE 5.9; absent from UE 5.8. Params: location, navData, agentName, world", "epic_call_tool"),
  },
  epic_is_input_applied: {
    epicSchema: S_epic_is_input_applied,
    epicTool: { toolset: "InputToolset.InputToolset", name: "InputToolset.InputToolset.IsInputApplied", engines: ["5.9"] },
    ...bp("read", "[Epic InputToolset.InputToolset] Reports whether each entry is currently active. Only Key reads player input. Available on UE 5.9; absent from UE 5.8. Params: entries", "epic_call_tool"),
  },
  epic_list_nav_agent_actors: {
    epicSchema: S_epic_list_nav_agent_actors,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavAgentActors", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists pawns as navigation agents, each with the navmesh it resolves to. Available on UE 5.9; absent from UE 5.8. Params: classFilter, center, radius, maxResults, world", "epic_call_tool"),
  },
  epic_list_nav_areas: {
    epicSchema: S_epic_list_nav_areas,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavAreas", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists the concrete nav area classes in the process (engine and game); abstract, deprecated, and superseded classes are skipped. Meta areas (per-agent area pickers) are flagged. Available on UE 5.9; absent from UE 5.8. Params: world", "epic_call_tool"),
  },
  epic_list_nav_bounds_volumes: {
    epicSchema: S_epic_list_nav_bounds_volumes,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavBoundsVolumes", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists every registered navigation bounds region and its supported agents. Navmesh only generates inside these bounds; a missing volume is the most common \"no navmesh here\". Available on UE 5.9; absent from UE 5.8. Params: world", "epic_call_tool"),
  },
  epic_list_nav_data: {
    epicSchema: S_epic_list_nav_data,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavData", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists the navigation data actors registered with the world's navigation system. Non-Recast navdata are included and flagged. Available on UE 5.9; absent from UE 5.8. Params: world", "epic_call_tool"),
  },
  epic_list_nav_invokers: {
    epicSchema: S_epic_list_nav_invokers,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavInvokers", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Reports the navigation invokers driving tile generation. On invoker-driven maps navmesh only exists near a registered invoker. Available on UE 5.9; absent from UE 5.8. Params: world", "epic_call_tool"),
  },
  epic_list_nav_links: {
    epicSchema: S_epic_list_nav_links,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavLinks", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists nav links from three sources: nav link proxy actors, custom link components, and links baked into the navmesh itself, including whether an in-mesh link is broken. COST NOTE: the in-mesh source pays a full debug-geometry gather per visited tile, bounded by a tile budget; pass a Radius or Source=InMeshLinks to spend that budget where it matters. Available on UE 5.9; absent from UE 5.8. Params: center, radius, source, maxResults, navData, agentName, world", "epic_call_tool"),
  },
  epic_list_nav_modifiers: {
    epicSchema: S_epic_list_nav_modifiers,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListNavModifiers", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists the nav modifier volumes and components (area carvers) shaping the navmesh. Available on UE 5.9; absent from UE 5.8. Params: center, radius, maxResults, bIncludeNonDefaultProperties, world", "epic_call_tool"),
  },
  epic_list_nodes: {
    epicSchema: S_epic_list_nodes,
    epicTool: { toolset: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", name: "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.list_nodes", engines: ["5.8"] },
    ...bp("read", "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns a flat list of all node UObjects in tree order. Order: root decorators, then DFS (composite, services, per-child decorators, child node). Available on UE 5.8; absent from UE 5.9. Params: behavior_tree", "epic_call_tool"),
  },
  epic_list_supported_agents: {
    epicSchema: S_epic_list_supported_agents,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListSupportedAgents", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists the agent configs a world supports and the live navigation data each one resolves to. Available on UE 5.9; absent from UE 5.8. Params: world", "epic_call_tool"),
  },
  epic_list_tag_source_roots: {
    epicSchema: S_epic_list_tag_source_roots,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.ListTagSourceRoots", engines: ["5.9"] },
    ...bp("read", "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns the directories a new tag source can be created in: the project's Config/Tags directory and each registered plugin's. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_tag_sources: {
    epicSchema: S_epic_list_tag_sources,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.ListTagSources", engines: ["5.9"] },
    ...bp("read", "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns every gameplay tag source, sorted by name. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_tags: {
    epicSchema: S_epic_list_tags,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.ListTags", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns gameplay tags registered in the project. Params: parentTag", "epic_call_tool"),
  },
  epic_list_tags_in_source: {
    epicSchema: S_epic_list_tags_in_source,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.ListTagsInSource", engines: ["5.9"] },
    ...bp("read", "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns all gameplay tags defined in a specific tag source. Available on UE 5.9; absent from UE 5.8. Params: tagSource", "epic_call_tool"),
  },
  epic_list_worlds: {
    epicSchema: S_epic_list_worlds,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ListWorlds", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Lists the worlds a navmesh tool call can target (editor, PIE server, PIE clients, game). Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_move_graph_node: {
    epicSchema: S_epic_move_graph_node,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.MoveGraphNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Reorders a child among its siblings. Available on UE 5.9; absent from UE 5.8. Params: parent, child, newIndex", "epic_call_tool"),
  },
  epic_move_option: {
    epicSchema: S_epic_move_option,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.MoveOption", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Reorders an option among its siblings. Option order is the tie-break order the query runs them in. Available on UE 5.9; absent from UE 5.8. Params: query, option, newIndex", "epic_call_tool"),
  },
  epic_move_subnode: {
    epicSchema: S_epic_move_subnode,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.MoveSubnode", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Reorders an existing decorator or service among its siblings without recreating it, so its configured settings are kept. Decorator order affects evaluation and abort priority. Available on UE 5.9; absent from UE 5.8. Params: owner, subnode, index", "epic_call_tool"),
  },
  epic_move_tags: {
    epicSchema: S_epic_move_tags,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.MoveTags", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameplayTagsToolset.GameplayTagsToolset] Moves a set of gameplay tags from their current INI source to a different one. This should ONLY be called after getting explicit direction or permission from the user. Available on UE 5.9; absent from UE 5.8. Params: tagsToMove, targetTagSource", "epic_call_tool"),
  },
  epic_move_test: {
    epicSchema: S_epic_move_test,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.MoveTest", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Reorders a test among its siblings without recreating it, so its settings are kept. Test order is the order they run in (and, with auto-sort off, affects performance). Available on UE 5.9; absent from UE 5.8. Params: option, test, index", "epic_call_tool"),
  },
  epic_project_point_to_nav_mesh: {
    epicSchema: S_epic_project_point_to_nav_mesh,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.ProjectPointToNavMesh", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Projects a point onto navigation data: does walkable space exist here, and where exactly. Available on UE 5.9; absent from UE 5.8. Params: point, horizontalExtent, verticalExtent, bAllAgents, navData, agentName, world", "epic_call_tool"),
  },
  epic_query_nav_octree: {
    epicSchema: S_epic_query_nav_octree,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.QueryNavOctree", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Queries the navigation octree for the elements contributing geometry or modifiers around a location: the authoritative answer to \"what carved or built the navmesh here\". Available on UE 5.9; absent from UE 5.8. Params: center, radius, maxResults, world", "epic_call_tool"),
  },
  epic_raycast_nav_mesh: {
    epicSchema: S_epic_raycast_nav_mesh,
    epicTool: { toolset: "NavMeshToolset.NavMeshToolset", name: "NavMeshToolset.NavMeshToolset.RaycastNavMesh", engines: ["5.9"] },
    ...bp("read", "[Epic NavMeshToolset.NavMeshToolset] Casts a ray across the navmesh surface and reports where walkable space ends. Available on UE 5.9; absent from UE 5.8. Params: start, end, navData, agentName, world", "epic_call_tool"),
  },
  epic_remove_blackboard_key: {
    epicSchema: S_epic_remove_blackboard_key,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.RemoveBlackboardKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Removes a key from the blackboard. Available on UE 5.9; absent from UE 5.8. Params: blackboard, keyName", "epic_call_tool"),
  },
  epic_remove_body: {
    epicSchema: S_epic_remove_body,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.RemoveBody", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Removes the body for the given bone along with any constraints that reference it. Raises a script error if PhysicsAsset is null or no body exists for BoneName. Params: physicsAsset, boneName", "epic_call_tool"),
  },
  epic_remove_constraint: {
    epicSchema: S_epic_remove_constraint,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.RemoveConstraint", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Removes the constraint between two bodies. Params: physicsAsset, bone1Name, bone2Name", "epic_call_tool"),
  },
  epic_remove_graph_node: {
    epicSchema: S_epic_remove_graph_node,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.RemoveGraphNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Removes a composite/task graph node, with its children and subnodes. The root cannot be removed. Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_remove_option: {
    epicSchema: S_epic_remove_option,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.RemoveOption", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Removes an option and its tests. Available on UE 5.9; absent from UE 5.8. Params: option", "epic_call_tool"),
  },
  epic_remove_shape: {
    epicSchema: S_epic_remove_shape,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.RemoveShape", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Removes a collision primitive from a body by name. Params: physicsAsset, boneName, shapeName", "epic_call_tool"),
  },
  epic_remove_subnode: {
    epicSchema: S_epic_remove_subnode,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.RemoveSubnode", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Removes a decorator or service subnode from its owner. Available on UE 5.9; absent from UE 5.8. Params: owner, subnode", "epic_call_tool"),
  },
  epic_remove_tag: {
    epicSchema: S_epic_remove_tag,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.RemoveTag", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic GameplayTagsToolset.GameplayTagsToolset] Removes a gameplay tag from the project. This should ONLY be called after getting explicit direction or permission from the user. Params: tagName", "epic_call_tool"),
  },
  epic_remove_test: {
    epicSchema: S_epic_remove_test,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.RemoveTest", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Removes a test from its option. Available on UE 5.9; absent from UE 5.8. Params: option, test", "epic_call_tool"),
  },
  epic_rename_tag: {
    epicSchema: S_epic_rename_tag,
    epicTool: { toolset: "GameplayTagsToolset.GameplayTagsToolset", name: "GameplayTagsToolset.GameplayTagsToolset.RenameTag", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic GameplayTagsToolset.GameplayTagsToolset] Renames a gameplay tag, updating all references in the project. This should ONLY be called after getting explicit direction or permission from the user. Params: oldTagName, newTagName", "epic_call_tool"),
  },
  epic_set_blackboard: {
    epicSchema: S_epic_set_blackboard,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.SetBlackboard", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Sets the tree's blackboard asset. Available on UE 5.9; absent from UE 5.8. Params: behaviorTree, blackboard", "epic_call_tool"),
  },
  epic_set_body_mass_scale: {
    epicSchema: S_epic_set_body_mass_scale,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.SetBodyMassScale", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Sets the mass-scale multiplier for the given body. Params: physicsAsset, boneName, massScale", "epic_call_tool"),
  },
  epic_set_body_physics_mode: {
    epicSchema: S_epic_set_body_physics_mode,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.SetBodyPhysicsMode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Sets the physics simulation mode for the given body. Params: physicsAsset, boneName, mode", "epic_call_tool"),
  },
  epic_set_box: {
    epicSchema: S_epic_set_box,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.SetBox", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds or replaces a box collision primitive on a body. If any shape with the given name already exists on the body it is removed first. Params: physicsAsset, boneName, shapeName, center, rotation, extentX, extentY, extentZ", "epic_call_tool"),
  },
  epic_set_capsule: {
    epicSchema: S_epic_set_capsule,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.SetCapsule", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds or replaces a capsule collision primitive on a body. If any shape with the given name already exists on the body it is removed first. The capsule's long axis is its local Z after applying Rotation. Params: physicsAsset, boneName, shapeName, center, rotation, radius, length", "epic_call_tool"),
  },
  epic_set_constraint_limits: {
    epicSchema: S_epic_set_constraint_limits,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.SetConstraintLimits", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Updates the angular limits for an existing constraint. Params: physicsAsset, info", "epic_call_tool"),
  },
  epic_set_node_blackboard_key: {
    epicSchema: S_epic_set_node_blackboard_key,
    epicTool: { toolset: "AIModuleToolset.BehaviorTreeToolset", name: "AIModuleToolset.BehaviorTreeToolset.SetNodeBlackboardKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.BehaviorTreeToolset] Sets which blackboard key a node's key selector points at, by name, resolving the binding against the tree's blackboard. Prefer this over writing the selector's fields directly, since the resolved key id is a transient cache derived from the name. Available on UE 5.9; absent from UE 5.8. Params: node, selectorPropertyName, keyName", "epic_call_tool"),
  },
  epic_set_sphere: {
    epicSchema: S_epic_set_sphere,
    epicTool: { toolset: "PhysicsToolsets.PhysicsAssetToolset", name: "PhysicsToolsets.PhysicsAssetToolset.SetSphere", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds or replaces a sphere collision primitive on a body. If any shape with the given name already exists on the body it is removed first. Params: physicsAsset, boneName, shapeName, center, radius", "epic_call_tool"),
  },
  epic_set_test_enabled: {
    epicSchema: S_epic_set_test_enabled,
    epicTool: { toolset: "AIModuleToolset.EnvironmentQueryToolset", name: "AIModuleToolset.EnvironmentQueryToolset.SetTestEnabled", engines: ["5.9"] },
    ...bp("mutate", "[Epic AIModuleToolset.EnvironmentQueryToolset] Enables or disables a test. A disabled test stays on the option but is excluded from the built query. Available on UE 5.9; absent from UE 5.8. Params: test, bEnabled", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actor: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The actor to inspect."),
  agentName: z.string().optional().describe("Agent whose navmesh to describe; empty targets the main navmesh."),
  areaCostOverrides: z.array(z.unknown()).optional().describe("Per-area travel cost overrides for this query; pass [] for none."),
  assetName: z.string().optional().describe("Name for the new asset."),
  bAllAgents: z.boolean().optional().describe("Project against every supported agent instead of one; overrides NavData and AgentName."),
  bAllowPartialPath: z.boolean().optional().describe("Accept a best-effort path when the end is unreachable."),
  bAssignToMesh: z.boolean().optional().describe("If true, assigns the new physics asset to the mesh."),
  behavior_tree: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  behaviorTree: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The tree to modify."),
  bEnabled: z.boolean().optional().describe("Whether the test is enabled."),
  bIncludeNonDefaultProperties: z.boolean().optional().describe("Also dump the navmesh's non-default reflected properties."),
  bInstanceSynced: z.boolean().optional().describe("Whether the key is synchronized across all instances of the blackboard."),
  blackboard: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The blackboard to modify."),
  bone1Name: z.string().optional().describe("Name of the child bone."),
  bone2Name: z.string().optional().describe("Name of the parent bone."),
  boneName: z.string().optional().describe("The name of the bone to add a body for."),
  center: z.record(z.string(), z.unknown()).optional().describe("Center of the box in bone-local space (cm)."),
  child: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The child graph node to move."),
  classFilter: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Only include pawns of this class; null includes all pawns."),
  comment: z.string().optional().describe("An optional developer comment describing the tag's purpose."),
  composite: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  compositeClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable composite class."),
  condition: z.record(z.string(), z.unknown()).optional().describe("The instanced struct holding the world condition."),
  costLimit: z.number().optional().describe("Abort the search above this cost; 0 means no limit."),
  decoratorClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable decorator class."),
  devComment: z.string().optional().describe("An optional developer comment describing the tag's purpose."),
  durationSeconds: z.number().optional().describe("How long to hold before releasing, in seconds. Zero holds for a single real tick, as a discrete tap. Must not be negative."),
  end: z.record(z.string(), z.unknown()).optional().describe("Path end, world space."),
  entries: z.array(z.unknown()).optional().describe("The actions/keys and values to hold. Each entry must set exactly one of Action or Key."),
  excludedAreaClasses: z.array(z.unknown()).optional().describe("Areas excluded from this query; pass [] for none."),
  extentX: z.number().optional().describe("Full extent along local X (cm). Must be greater than zero."),
  extentY: z.number().optional().describe("Full extent along local Y (cm). Must be greater than zero."),
  extentZ: z.number().optional().describe("Full extent along local Z (cm). Must be greater than zero."),
  folderPath: z.record(z.string(), z.unknown()).optional().describe("Content folder to create in (Path e.g. \"/Game/AI\")."),
  generatorClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable generator class."),
  horizontalExtent: z.number().optional().describe("XY search extent in cm, applied to both axes; 0 or negative uses the navdata default."),
  index: z.number().optional().describe("Sibling position; -1 appends."),
  info: z.record(z.string(), z.unknown()).optional().describe("Constraint descriptor. Bone1Name and Bone2Name identify the constraint."),
  keyName: z.string().optional().describe("Name for the new key."),
  keyType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable key-type class."),
  length: z.number().optional().describe("Length of the cylindrical section (cm). Must be non-negative. Total capsule height = Length + 2 * Radius."),
  location: z.record(z.string(), z.unknown()).optional().describe("World-space location."),
  massScale: z.number().optional().describe("Multiplier applied to the computed mass. Must be greater than zero."),
  maxCost: z.number().optional().describe("Path cost budget (equals distance in cm at cost 1)."),
  maxDistance: z.number().optional().describe("Search cap in cm."),
  maxPathPoints: z.number().optional().describe("Cap on returned path points, clamped to 2..4096; bPointsTruncated reports when it bit."),
  maxPolyCenters: z.number().optional().describe("Cap on returned poly centers only (clamped to 4096); the counts and bounds stay exact."),
  maxResults: z.number().optional().describe("Result cap, clamped to 256; bTruncated reports when it bit."),
  meshPath: z.string().optional().describe("Content-browser path to the skeletal mesh, e.g. '/Game/Characters/SKM_Hero'."),
  mode: z.string().optional().describe("The desired simulation mode."),
  navData: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Navmesh to describe; wins over AgentName when both are set."),
  newIndex: z.number().optional().describe("Target sibling position."),
  newTagName: z.string().optional().describe("The new fully-qualified name for the tag."),
  node: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  node_index: z.number().optional(),
  oldTagName: z.string().optional().describe("The fully-qualified name of the tag to rename."),
  option: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The option graph node to add the test to."),
  origin: z.record(z.string(), z.unknown()).optional().describe("World-space origin."),
  owner: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Graph node to attach the decorator to."),
  parent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Graph node to add the composite under."),
  parentTag: z.string().optional().describe("If non-empty, only tags that are descendants of this tag are returned. For example, passing \"Character.State\" returns \"Character.State.Dead\", \"Character.State.…"),
  physicsAsset: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The physics asset to modify."),
  point: z.record(z.string(), z.unknown()).optional().describe("World-space location."),
  query: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The query to modify."),
  queryDefinition: z.record(z.string(), z.unknown()).optional().describe("The query definition to describe."),
  radius: z.number().optional().describe("Radius of the capsule end-caps (cm). Must be greater than zero."),
  rotation: z.record(z.string(), z.unknown()).optional().describe("Orientation of the box in bone-local space."),
  selectorPropertyName: z.string().optional().describe("The name of the node's key-selector property (usually \"BlackboardKey\")."),
  serviceClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable service class."),
  shapeName: z.string().optional().describe("The name of the shape to remove."),
  source: z.string().optional().describe("Which sources to gather; a single source spends the whole cap on itself."),
  sourceName: z.string().optional().describe("The source's name, e.g. \"MyPluginGameplayTags.ini\". \".ini\" is appended if missing."),
  start: z.record(z.string(), z.unknown()).optional().describe("Path start, world space."),
  subnode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The decorator or service graph node to move."),
  tagName: z.string().optional().describe("The fully-qualified name of the tag to add, e.g. \"Character.State.Dead\"."),
  tagRoot: z.string().optional().describe("The directory to create the source in, from ListTagSourceRoots. When empty, the source is created in the project's Config/Tags directory."),
  tagSource: z.string().optional().describe("An existing INI source to add the tag to (see ListTagSources); use AddTagSource to create a new one first. When empty, behavior is controlled by the Gameplay T…"),
  tagsToMove: z.array(z.unknown()).optional().describe("The fully-qualified names of the tags to move."),
  targetTagSource: z.string().optional().describe("The INI source to move the tags into, e.g. \"MyCoolPluginTags.ini\"."),
  taskClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable task class."),
  test: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The test graph node to read."),
  testClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A concrete, placeable test class."),
  verticalExtent: z.number().optional().describe("Z search extent in cm; 0 or negative uses the navdata default."),
  world: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Target world; null resolves to the best play world."),
};
