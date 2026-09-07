// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog.json
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
import { z } from "zod";
import { bp, type ActionSpec } from "../../types.js";
import { epicToolCall } from "../../epic-input.js";

const S_epic_add_body = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_add_constraint = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"bone1Name":{"type":"string"},"bone2Name":{"type":"string"}},"required":["physicsAsset","bone1Name","bone2Name"]} as const;
const S_epic_add_tag = {"properties":{"tagName":{"type":"string"},"comment":{"type":"string"},"tagSource":{"type":"string"}},"required":["tagName","tagSource"]} as const;
const S_epic_create_from_mesh = {"properties":{"meshPath":{"type":"string"},"bAssignToMesh":{"type":"boolean"}},"required":["meshPath","bAssignToMesh"]} as const;
const S_epic_find_referencers_by_tag = {"properties":{"tagName":{"type":"string"}},"required":["tagName"]} as const;
const S_epic_get_blackboard = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_get_body_mass_scale = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_get_body_names = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}}},"required":["physicsAsset"]} as const;
const S_epic_get_body_physics_mode = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_get_body_shapes = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_get_children = {"properties":{"composite":{"type":"object","properties":{"refPath":{}}}},"required":["composite"]} as const;
const S_epic_get_condition_description = {"properties":{"condition":{"type":"object"}},"required":["condition"]} as const;
const S_epic_get_constraints = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}}},"required":["physicsAsset"]} as const;
const S_epic_get_node_depth = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}},"node_index":{"type":"integer"}},"required":["behavior_tree","node_index"]} as const;
const S_epic_get_node_depths = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_get_query_description = {"properties":{"queryDefinition":{"type":"object"}},"required":["queryDefinition"]} as const;
const S_epic_get_root_decorators = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_get_subtree = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_tag_info = {"properties":{"tagName":{"type":"string"}},"required":["tagName"]} as const;
const S_epic_list_nodes = {"properties":{"behavior_tree":{"type":"object","properties":{"refPath":{}}}},"required":["behavior_tree"]} as const;
const S_epic_list_tags = {"properties":{"parentTag":{"type":"string"}},"required":["parentTag"]} as const;
const S_epic_remove_body = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["physicsAsset","boneName"]} as const;
const S_epic_remove_constraint = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"bone1Name":{"type":"string"},"bone2Name":{"type":"string"}},"required":["physicsAsset","bone1Name","bone2Name"]} as const;
const S_epic_remove_shape = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"}},"required":["physicsAsset","boneName","shapeName"]} as const;
const S_epic_remove_tag = {"properties":{"tagName":{"type":"string"}},"required":["tagName"]} as const;
const S_epic_rename_tag = {"properties":{"oldTagName":{"type":"string"},"newTagName":{"type":"string"}},"required":["oldTagName","newTagName"]} as const;
const S_epic_set_body_mass_scale = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"massScale":{"type":"number"}},"required":["physicsAsset","boneName","massScale"]} as const;
const S_epic_set_body_physics_mode = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"mode":{"type":"string"}},"required":["physicsAsset","boneName","mode"]} as const;
const S_epic_set_box = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"},"center":{"type":"object"},"rotation":{"type":"object"},"extentX":{"type":"number"},"extentY":{"type":"number"},"extentZ":{"type":"number"}},"required":["physicsAsset","boneName","shapeName","center","rotation","extentX","extentY","extentZ"]} as const;
const S_epic_set_capsule = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"},"center":{"type":"object"},"rotation":{"type":"object"},"radius":{"type":"number"},"length":{"type":"number"}},"required":["physicsAsset","boneName","shapeName","center","rotation","radius","length"]} as const;
const S_epic_set_constraint_limits = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"info":{"type":"object"}},"required":["physicsAsset","info"]} as const;
const S_epic_set_sphere = {"properties":{"physicsAsset":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"},"shapeName":{"type":"string"},"center":{"type":"object"},"radius":{"type":"number"}},"required":["physicsAsset","boneName","shapeName","center","radius"]} as const;

/** 32 wrapped engine tools routed to the `gameplay` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_body: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds a new empty body for the given bone. Params: physicsAsset, boneName",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.AddBody", S_epic_add_body, p),
  ),
  epic_add_constraint: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds a new constraint between two bodies. Both bodies must already exist. Params: physicsAsset, bone1Name, bone2Name",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.AddConstraint", S_epic_add_constraint, p),
  ),
  epic_add_tag: bp(
    "mutate",
    "[Epic GameplayTagsToolset.GameplayTagsToolset] Adds a new gameplay tag to the project. This should ONLY be called after getting explicit direction or permission from the user. Params: tagName, comment?, tagSource",
    "epic_call_tool",
    (p) => epicToolCall("GameplayTagsToolset.GameplayTagsToolset", "GameplayTagsToolset.GameplayTagsToolset.AddTag", S_epic_add_tag, p),
  ),
  epic_create_from_mesh: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Creates a physics asset from a skeletal mesh, auto-generating collision bodies for each bone. The asset is placed in the same folder as the mesh with the suffix \"_PhysicsAsset\". Params: meshPath, bAssignToMesh",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.CreateFromMesh", S_epic_create_from_mesh, p),
  ),
  epic_find_referencers_by_tag: bp(
    "read",
    "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns assets that reference a gameplay tag. Params: tagName",
    "epic_call_tool",
    (p) => epicToolCall("GameplayTagsToolset.GameplayTagsToolset", "GameplayTagsToolset.GameplayTagsToolset.FindReferencersByTag", S_epic_find_referencers_by_tag, p),
  ),
  epic_get_blackboard: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns the blackboard asset for this behavior tree. Params: behavior_tree",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_blackboard", S_epic_get_blackboard, p),
  ),
  epic_get_body_mass_scale: bp(
    "read",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns the mass-scale multiplier for the given body. Params: physicsAsset, boneName",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.GetBodyMassScale", S_epic_get_body_mass_scale, p),
  ),
  epic_get_body_names: bp(
    "read",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns the bone name for each rigid body in a physics asset. Params: physicsAsset",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.GetBodyNames", S_epic_get_body_names, p),
  ),
  epic_get_body_physics_mode: bp(
    "read",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns the physics simulation mode for the given body. Params: physicsAsset, boneName",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.GetBodyPhysicsMode", S_epic_get_body_physics_mode, p),
  ),
  epic_get_body_shapes: bp(
    "read",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns all collision shapes assigned to a body. Params: physicsAsset, boneName",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.GetBodyShapes", S_epic_get_body_shapes, p),
  ),
  epic_get_children: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns direct child nodes of a composite node. Params: composite",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_children", S_epic_get_children, p),
  ),
  epic_get_condition_description: bp(
    "read",
    "[Epic WorldConditionsToolset.WorldConditionTools] Returns a human-readable description of a single world condition. The condition must be passed as an FInstancedStruct containing an FWorldConditionBase-derived struct. Params: condition",
    "epic_call_tool",
    (p) => epicToolCall("WorldConditionsToolset.WorldConditionTools", "WorldConditionsToolset.WorldConditionTools.GetConditionDescription", S_epic_get_condition_description, p),
  ),
  epic_get_constraints: bp(
    "read",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Returns all constraints in the physics asset with their current angular limits. Params: physicsAsset",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.GetConstraints", S_epic_get_constraints, p),
  ),
  epic_get_node_depth: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns the tree depth of a node by its list_nodes index. Params: behavior_tree, node_index",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_node_depth", S_epic_get_node_depth, p),
  ),
  epic_get_node_depths: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns tree depths for all nodes, matching list_nodes order. Params: behavior_tree",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_node_depths", S_epic_get_node_depths, p),
  ),
  epic_get_query_description: bp(
    "read",
    "[Epic WorldConditionsToolset.WorldConditionTools] Returns a human-readable description of a world condition query. Params: queryDefinition",
    "epic_call_tool",
    (p) => epicToolCall("WorldConditionsToolset.WorldConditionTools", "WorldConditionsToolset.WorldConditionTools.GetQueryDescription", S_epic_get_query_description, p),
  ),
  epic_get_root_decorators: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns root-level decorators on this tree. Params: behavior_tree",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_root_decorators", S_epic_get_root_decorators, p),
  ),
  epic_get_subtree: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns the sub-BT asset referenced by a RunBehavior task. Params: node",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_subtree", S_epic_get_subtree, p),
  ),
  epic_get_tag_info: bp(
    "read",
    "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns detailed information about a specific gameplay tag. Params: tagName",
    "epic_call_tool",
    (p) => epicToolCall("GameplayTagsToolset.GameplayTagsToolset", "GameplayTagsToolset.GameplayTagsToolset.GetTagInfo", S_epic_get_tag_info, p),
  ),
  epic_list_nodes: bp(
    "read",
    "[Epic aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools] Returns a flat list of all node UObjects in tree order. Order: root decorators, then DFS (composite, services, per-child decorators, child node). Params: behavior_tree",
    "epic_call_tool",
    (p) => epicToolCall("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools", "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.list_nodes", S_epic_list_nodes, p),
  ),
  epic_list_tags: bp(
    "read",
    "[Epic GameplayTagsToolset.GameplayTagsToolset] Returns gameplay tags registered in the project. Params: parentTag",
    "epic_call_tool",
    (p) => epicToolCall("GameplayTagsToolset.GameplayTagsToolset", "GameplayTagsToolset.GameplayTagsToolset.ListTags", S_epic_list_tags, p),
  ),
  epic_remove_body: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Removes the body for the given bone along with any constraints that reference it. Raises a script error if PhysicsAsset is null or no body exists for BoneName. Params: physicsAsset, boneName",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.RemoveBody", S_epic_remove_body, p),
  ),
  epic_remove_constraint: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Removes the constraint between two bodies. Params: physicsAsset, bone1Name, bone2Name",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.RemoveConstraint", S_epic_remove_constraint, p),
  ),
  epic_remove_shape: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Removes a collision primitive from a body by name. Params: physicsAsset, boneName, shapeName",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.RemoveShape", S_epic_remove_shape, p),
  ),
  epic_remove_tag: bp(
    "mutate",
    "[Epic GameplayTagsToolset.GameplayTagsToolset] Removes a gameplay tag from the project. This should ONLY be called after getting explicit direction or permission from the user. Params: tagName",
    "epic_call_tool",
    (p) => epicToolCall("GameplayTagsToolset.GameplayTagsToolset", "GameplayTagsToolset.GameplayTagsToolset.RemoveTag", S_epic_remove_tag, p),
  ),
  epic_rename_tag: bp(
    "mutate",
    "[Epic GameplayTagsToolset.GameplayTagsToolset] Renames a gameplay tag, updating all references in the project. This should ONLY be called after getting explicit direction or permission from the user. Params: oldTagName, newTagName",
    "epic_call_tool",
    (p) => epicToolCall("GameplayTagsToolset.GameplayTagsToolset", "GameplayTagsToolset.GameplayTagsToolset.RenameTag", S_epic_rename_tag, p),
  ),
  epic_set_body_mass_scale: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Sets the mass-scale multiplier for the given body. Params: physicsAsset, boneName, massScale",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.SetBodyMassScale", S_epic_set_body_mass_scale, p),
  ),
  epic_set_body_physics_mode: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Sets the physics simulation mode for the given body. Params: physicsAsset, boneName, mode",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.SetBodyPhysicsMode", S_epic_set_body_physics_mode, p),
  ),
  epic_set_box: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds or replaces a box collision primitive on a body. If any shape with the given name already exists on the body it is removed first. Params: physicsAsset, boneName, shapeName, center, rotation, extentX, extentY, extentZ",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.SetBox", S_epic_set_box, p),
  ),
  epic_set_capsule: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds or replaces a capsule collision primitive on a body. If any shape with the given name already exists on the body it is removed first. The capsule's long axis is its local Z after applying Rotation. Params: physicsAsset, boneName, shapeName, center, rotation, radius, length",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.SetCapsule", S_epic_set_capsule, p),
  ),
  epic_set_constraint_limits: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Updates the angular limits for an existing constraint. Params: physicsAsset, info",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.SetConstraintLimits", S_epic_set_constraint_limits, p),
  ),
  epic_set_sphere: bp(
    "mutate",
    "[Epic PhysicsToolsets.PhysicsAssetToolset] Adds or replaces a sphere collision primitive on a body. If any shape with the given name already exists on the body it is removed first. Params: physicsAsset, boneName, shapeName, center, radius",
    "epic_call_tool",
    (p) => epicToolCall("PhysicsToolsets.PhysicsAssetToolset", "PhysicsToolsets.PhysicsAssetToolset.SetSphere", S_epic_set_sphere, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  bAssignToMesh: z.boolean().optional().describe("If true, assigns the new physics asset to the mesh."),
  behavior_tree: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  bone1Name: z.string().optional().describe("Name of the child bone."),
  bone2Name: z.string().optional().describe("Name of the parent bone."),
  boneName: z.string().optional().describe("The name of the bone to add a body for."),
  center: z.record(z.unknown()).optional().describe("Center of the box in bone-local space (cm)."),
  comment: z.string().optional().describe("An optional developer comment describing the tag's purpose."),
  composite: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  condition: z.record(z.unknown()).optional().describe("The instanced struct holding the world condition."),
  extentX: z.number().optional().describe("Full extent along local X (cm). Must be greater than zero."),
  extentY: z.number().optional().describe("Full extent along local Y (cm). Must be greater than zero."),
  extentZ: z.number().optional().describe("Full extent along local Z (cm). Must be greater than zero."),
  info: z.record(z.unknown()).optional().describe("Constraint descriptor. Bone1Name and Bone2Name identify the constraint."),
  length: z.number().optional().describe("Length of the cylindrical section (cm). Must be non-negative. Total capsule height = Length + 2 * Radius."),
  massScale: z.number().optional().describe("Multiplier applied to the computed mass. Must be greater than zero."),
  meshPath: z.string().optional().describe("Content-browser path to the skeletal mesh, e.g. '/Game/Characters/SKM_Hero'."),
  mode: z.string().optional().describe("The desired simulation mode."),
  newTagName: z.string().optional().describe("The new fully-qualified name for the tag."),
  node: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  node_index: z.number().optional(),
  oldTagName: z.string().optional().describe("The fully-qualified name of the tag to rename."),
  parentTag: z.string().optional().describe("If non-empty, only tags that are descendants of this tag are returned. For example, passing \"Character.State\" returns \"Character.State.Dead\", \"Character.State.…"),
  physicsAsset: z.union([z.string(), z.record(z.unknown())]).optional().describe("The physics asset to modify."),
  queryDefinition: z.record(z.unknown()).optional().describe("The query definition to describe."),
  radius: z.number().optional().describe("Radius of the capsule end-caps (cm). Must be greater than zero."),
  rotation: z.record(z.unknown()).optional().describe("Orientation of the box in bone-local space."),
  shapeName: z.string().optional().describe("The name of the shape to remove."),
  tagName: z.string().optional().describe("The fully-qualified name of the tag to add, e.g. \"Character.State.Dead\"."),
  tagSource: z.string().optional().describe("The INI source to add the tag to. Uses the default source if empty."),
};
