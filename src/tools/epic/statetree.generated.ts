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

const S_epic_add_blueprint_consideration = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"consideration_class":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","state","consideration_class"]} as const;
const S_epic_add_blueprint_enter_condition = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"condition_class":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","state","condition_class"]} as const;
const S_epic_add_blueprint_evaluator = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"evaluator_class":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","evaluator_class"]} as const;
const S_epic_add_blueprint_global_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"task_class":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","task_class"]} as const;
const S_epic_add_blueprint_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"task_class":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","state","task_class"]} as const;
const S_epic_add_child_state = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"state_type":{"type":"string","enum":["State","Group","Linked","LinkedAsset","Subtree"]}},"required":["state_tree","parent","name"]} as const;
const S_epic_add_consideration = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"consideration_struct":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","state","consideration_struct"]} as const;
const S_epic_add_enter_condition = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"condition_struct":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","state","condition_struct"]} as const;
const S_epic_add_evaluator = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"evaluator_struct":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","evaluator_struct"]} as const;
const S_epic_add_global_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"task_struct":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","task_struct"]} as const;
const S_epic_add_sub_tree = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"state_type":{"type":"string","enum":["State","Group","Linked","LinkedAsset","Subtree"]}},"required":["state_tree","name"]} as const;
const S_epic_add_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"task_struct":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","state","task_struct"]} as const;
const S_epic_compile = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_all_states = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_children = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_children__1 = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_considerations = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_editor_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_editor_data__1 = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_enter_conditions = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_enter_conditions__1 = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_evaluators = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_evaluators__1 = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_extension = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"extension_class":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","extension_class"]} as const;
const S_epic_get_global_tasks = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_global_tasks__1 = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_node_description = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"node":{"type":"object","properties":{"_structType":{"type":"string"}}},"instance":{"type":"object","properties":{"_structType":{"type":"string"}}},"instanceObject":{"type":"object","properties":{"refPath":{}}},"executionRuntimeData":{"type":"object","properties":{"_structType":{"type":"string"}}},"executionRuntimeDataObject":{"type":"object","properties":{"refPath":{}}},"iD":{"type":"string"},"expressionIndent":{"type":"integer"},"expressionOperand":{"type":"string","enum":["Copy","And","Or","Multiply"]}}}},"required":["state_tree","node"]} as const;
const S_epic_get_node_description__1 = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"node":{"type":"object","properties":{"_structType":{"type":"string"}}},"instance":{"type":"object","properties":{"_structType":{"type":"string"}}},"instanceObject":{"type":"object","properties":{"refPath":{}}},"executionRuntimeData":{"type":"object","properties":{"_structType":{"type":"string"}}},"executionRuntimeDataObject":{"type":"object","properties":{"refPath":{}}},"iD":{"type":"string"},"expressionIndent":{"type":"integer"},"expressionOperand":{"type":"string","enum":["And","Or","Multiply"]}}}},"required":["state_tree","node"]} as const;
const S_epic_get_node_execution_runtime_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"}},"required":["state_tree","node_id"]} as const;
const S_epic_get_node_execution_runtime_object = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"}},"required":["state_tree","node_id"]} as const;
const S_epic_get_node_instance_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"}},"required":["state_tree","node_id"]} as const;
const S_epic_get_node_instance_object = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"}},"required":["state_tree","node_id"]} as const;
const S_epic_get_node_template_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"}},"required":["state_tree","node_id"]} as const;
const S_epic_get_root_states = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_root_states__1 = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_tasks = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_tasks__1 = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_transitions = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_transitions__1 = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_mark_as_modified = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_move_child_state = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}},"child":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["state_tree","parent","child","index"]} as const;
const S_epic_move_consideration = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"consideration_id":{"type":"string"},"index":{"type":"integer"}},"required":["state_tree","state","consideration_id","index"]} as const;
const S_epic_move_enter_condition = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"condition_id":{"type":"string"},"index":{"type":"integer"}},"required":["state_tree","state","condition_id","index"]} as const;
const S_epic_move_evaluator = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"evaluator_id":{"type":"string"},"index":{"type":"integer"}},"required":["state_tree","evaluator_id","index"]} as const;
const S_epic_move_global_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"task_id":{"type":"string"},"index":{"type":"integer"}},"required":["state_tree","task_id","index"]} as const;
const S_epic_move_sub_tree = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"sub_tree":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["state_tree","sub_tree","index"]} as const;
const S_epic_move_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"task_id":{"type":"string"},"index":{"type":"integer"}},"required":["state_tree","state","task_id","index"]} as const;
const S_epic_remove_child_state = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}},"child":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","parent","child"]} as const;
const S_epic_remove_consideration = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"consideration_id":{"type":"string"}},"required":["state_tree","state","consideration_id"]} as const;
const S_epic_remove_enter_condition = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"condition_id":{"type":"string"}},"required":["state_tree","state","condition_id"]} as const;
const S_epic_remove_evaluator = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"evaluator_id":{"type":"string"}},"required":["state_tree","evaluator_id"]} as const;
const S_epic_remove_global_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"task_id":{"type":"string"}},"required":["state_tree","task_id"]} as const;
const S_epic_remove_sub_tree = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"sub_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree","sub_tree"]} as const;
const S_epic_remove_task = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"state":{"type":"object","properties":{"refPath":{}}},"task_id":{"type":"string"}},"required":["state_tree","state","task_id"]} as const;
const S_epic_request_ui_refresh = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_set_node_execution_runtime_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"},"runtime_data":{"type":"object","properties":{"_structType":{"type":"string"}}}},"required":["state_tree","node_id","runtime_data"]} as const;
const S_epic_set_node_instance_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"},"instance_data":{"type":"object","properties":{"_structType":{"type":"string"}}}},"required":["state_tree","node_id","instance_data"]} as const;
const S_epic_set_node_template_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node_id":{"type":"string"},"node_data":{"type":"object","properties":{"_structType":{"type":"string"}}}},"required":["state_tree","node_id","node_data"]} as const;

/** 49 wrapped engine tools routed to the `statetree` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_blueprint_consideration: {
    epicSchema: S_epic_add_blueprint_consideration,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_consideration", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a Blueprint-based utility consideration to a state. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, consideration_class", "epic_call_tool"),
  },
  epic_add_blueprint_enter_condition: {
    epicSchema: S_epic_add_blueprint_enter_condition,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_enter_condition", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a Blueprint-based enter condition to a state. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, condition_class", "epic_call_tool"),
  },
  epic_add_blueprint_evaluator: {
    epicSchema: S_epic_add_blueprint_evaluator,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_evaluator", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a Blueprint-based evaluator to the asset. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, evaluator_class", "epic_call_tool"),
  },
  epic_add_blueprint_global_task: {
    epicSchema: S_epic_add_blueprint_global_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_global_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a Blueprint-based global task to the asset. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, task_class", "epic_call_tool"),
  },
  epic_add_blueprint_task: {
    epicSchema: S_epic_add_blueprint_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a Blueprint-based task to a state. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, task_class", "epic_call_tool"),
  },
  epic_add_child_state: {
    epicSchema: S_epic_add_child_state,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_child_state", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a new child state under the given parent state. Available on UE 5.9; absent from UE 5.8. Params: state_tree, parent, name, state_type?", "epic_call_tool"),
  },
  epic_add_consideration: {
    epicSchema: S_epic_add_consideration,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_consideration", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a struct-based utility consideration to a state. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, consideration_struct", "epic_call_tool"),
  },
  epic_add_enter_condition: {
    epicSchema: S_epic_add_enter_condition,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_enter_condition", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a struct-based enter condition to a state. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, condition_struct", "epic_call_tool"),
  },
  epic_add_evaluator: {
    epicSchema: S_epic_add_evaluator,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_evaluator", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a struct-based evaluator to the asset. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, evaluator_struct", "epic_call_tool"),
  },
  epic_add_global_task: {
    epicSchema: S_epic_add_global_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_global_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a struct-based global task to the asset. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, task_struct", "epic_call_tool"),
  },
  epic_add_sub_tree: {
    epicSchema: S_epic_add_sub_tree,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_sub_tree", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a new top-level subtree to the asset. Available on UE 5.9; absent from UE 5.8. Params: state_tree, name, state_type?", "epic_call_tool"),
  },
  epic_add_task: {
    epicSchema: S_epic_add_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.add_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Adds a struct-based task to a state. Returns the new node's ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, task_struct", "epic_call_tool"),
  },
  epic_compile: {
    epicSchema: S_epic_compile,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.compile", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Compiles a StateTree. Returns whether it succeeded and the compiler's messages. Available on UE 5.9; absent from UE 5.8. Params: state_tree", "epic_call_tool"),
  },
  epic_get_all_states: {
    epicSchema: S_epic_get_all_states,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_all_states", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns every state in the tree (root + descendants), in hierarchy order. Available on UE 5.9; absent from UE 5.8. Params: state_tree", "epic_call_tool"),
  },
  epic_get_children: {
    epicSchema: S_epic_get_children,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_children", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_children", engines: ["5.9"], schema: S_epic_get_children__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns child states of a state. Params: state. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_children (Params: state).", "epic_call_tool"),
  },
  epic_get_considerations: {
    epicSchema: S_epic_get_considerations,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_considerations", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the utility considerations on a state. Available on UE 5.9; absent from UE 5.8. Params: state", "epic_call_tool"),
  },
  epic_get_editor_data: {
    epicSchema: S_epic_get_editor_data,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_editor_data", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_editor_data", engines: ["5.9"], schema: S_epic_get_editor_data__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns the editor data for a StateTree asset. Params: state_tree. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_editor_data (Params: state_tree).", "epic_call_tool"),
  },
  epic_get_enter_conditions: {
    epicSchema: S_epic_get_enter_conditions,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_enter_conditions", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_enter_conditions", engines: ["5.9"], schema: S_epic_get_enter_conditions__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns enter conditions on a state. Params: state. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_enter_conditions (Params: state).", "epic_call_tool"),
  },
  epic_get_evaluators: {
    epicSchema: S_epic_get_evaluators,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_evaluators", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_evaluators", engines: ["5.9"], schema: S_epic_get_evaluators__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns global evaluators. Params: state_tree. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_evaluators (Params: state_tree).", "epic_call_tool"),
  },
  epic_get_extension: {
    epicSchema: S_epic_get_extension,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_extension", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the first editor-data extension of the requested type, or None. Available on UE 5.9; absent from UE 5.8. Params: state_tree, extension_class", "epic_call_tool"),
  },
  epic_get_global_tasks: {
    epicSchema: S_epic_get_global_tasks,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_global_tasks", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_global_tasks", engines: ["5.9"], schema: S_epic_get_global_tasks__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns global tasks that run across all states. Params: state_tree. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_global_tasks (Params: state_tree).", "epic_call_tool"),
  },
  epic_get_node_description: {
    epicSchema: S_epic_get_node_description,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_node_description", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_node_description", engines: ["5.9"], schema: S_epic_get_node_description__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns a human-readable description for a node. Params: state_tree, node. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_node_description (Params: state_tree, node).", "epic_call_tool"),
  },
  epic_get_node_execution_runtime_data: {
    epicSchema: S_epic_get_node_execution_runtime_data,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_node_execution_runtime_data", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the node's Execution Runtime struct (per-execution scratch data). Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id", "epic_call_tool"),
  },
  epic_get_node_execution_runtime_object: {
    epicSchema: S_epic_get_node_execution_runtime_object,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_node_execution_runtime_object", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the node's Execution Runtime UObject. Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id", "epic_call_tool"),
  },
  epic_get_node_instance_data: {
    epicSchema: S_epic_get_node_instance_data,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_node_instance_data", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the node's Instance struct (per-state-tree-instance data, struct-typed). Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id", "epic_call_tool"),
  },
  epic_get_node_instance_object: {
    epicSchema: S_epic_get_node_instance_object,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_node_instance_object", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the node's Instance UObject (per-state-tree-instance data, UObject-typed). Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id", "epic_call_tool"),
  },
  epic_get_node_template_data: {
    epicSchema: S_epic_get_node_template_data,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_node_template_data", engines: ["5.9"] },
    ...bp("read", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Returns the node's Template struct (the type-defining, shared data). Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id", "epic_call_tool"),
  },
  epic_get_root_states: {
    epicSchema: S_epic_get_root_states,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_root_states", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_root_states", engines: ["5.9"], schema: S_epic_get_root_states__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns top-level states of a StateTree. Params: state_tree. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_root_states (Params: state_tree).", "epic_call_tool"),
  },
  epic_get_tasks: {
    epicSchema: S_epic_get_tasks,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_tasks", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_tasks", engines: ["5.9"], schema: S_epic_get_tasks__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns tasks on a state. Params: state. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_tasks (Params: state).", "epic_call_tool"),
  },
  epic_get_transitions: {
    epicSchema: S_epic_get_transitions,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_transitions", engines: ["5.8"], successors: [{ toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.get_transitions", engines: ["5.9"], schema: S_epic_get_transitions__1 }] },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns transitions on a state. Params: state. On UE 5.9 it calls state_tree_toolset.state_tree.StateTreeToolset.get_transitions (Params: state).", "epic_call_tool"),
  },
  epic_mark_as_modified: {
    epicSchema: S_epic_mark_as_modified,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.mark_as_modified", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Marks the asset as dirty so a recompile and UI refresh happen on the next tick. Call this when you've mutated the asset outside the wrappers below (the wrappers below already call this for you on every write). Available on UE 5.9; absent from UE 5.8. Params: state_tree", "epic_call_tool"),
  },
  epic_move_child_state: {
    epicSchema: S_epic_move_child_state,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_child_state", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves a child state to a specific index in its parent's children list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, parent, child, index", "epic_call_tool"),
  },
  epic_move_consideration: {
    epicSchema: S_epic_move_consideration,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_consideration", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves a consideration to a specific index in the state's consideration list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, consideration_id, index", "epic_call_tool"),
  },
  epic_move_enter_condition: {
    epicSchema: S_epic_move_enter_condition,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_enter_condition", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves an enter condition to a specific index in the state's enter-condition list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, condition_id, index", "epic_call_tool"),
  },
  epic_move_evaluator: {
    epicSchema: S_epic_move_evaluator,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_evaluator", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves an evaluator to a specific index in the asset's evaluator list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, evaluator_id, index", "epic_call_tool"),
  },
  epic_move_global_task: {
    epicSchema: S_epic_move_global_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_global_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves a global task to a specific index in the asset's global-task list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, task_id, index", "epic_call_tool"),
  },
  epic_move_sub_tree: {
    epicSchema: S_epic_move_sub_tree,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_sub_tree", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves an existing subtree to a specific index in the subtree list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, sub_tree, index", "epic_call_tool"),
  },
  epic_move_task: {
    epicSchema: S_epic_move_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.move_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Moves a task to a specific index in the state's task list. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, task_id, index", "epic_call_tool"),
  },
  epic_remove_child_state: {
    epicSchema: S_epic_remove_child_state,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_child_state", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes a child state from its parent. Available on UE 5.9; absent from UE 5.8. Params: state_tree, parent, child", "epic_call_tool"),
  },
  epic_remove_consideration: {
    epicSchema: S_epic_remove_consideration,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_consideration", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes a consideration from a state by ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, consideration_id", "epic_call_tool"),
  },
  epic_remove_enter_condition: {
    epicSchema: S_epic_remove_enter_condition,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_enter_condition", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes an enter condition from a state by ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, condition_id", "epic_call_tool"),
  },
  epic_remove_evaluator: {
    epicSchema: S_epic_remove_evaluator,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_evaluator", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes an evaluator by ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, evaluator_id", "epic_call_tool"),
  },
  epic_remove_global_task: {
    epicSchema: S_epic_remove_global_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_global_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes a global task by ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, task_id", "epic_call_tool"),
  },
  epic_remove_sub_tree: {
    epicSchema: S_epic_remove_sub_tree,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_sub_tree", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes a subtree from the asset. Available on UE 5.9; absent from UE 5.8. Params: state_tree, sub_tree", "epic_call_tool"),
  },
  epic_remove_task: {
    epicSchema: S_epic_remove_task,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.remove_task", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Removes a task from a state by ID. Available on UE 5.9; absent from UE 5.8. Params: state_tree, state, task_id", "epic_call_tool"),
  },
  epic_request_ui_refresh: {
    epicSchema: S_epic_request_ui_refresh,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.request_ui_refresh", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Requests a full refresh of the open StateTree editor (no-op if no editor is open). Available on UE 5.9; absent from UE 5.8. Params: state_tree", "epic_call_tool"),
  },
  epic_set_node_execution_runtime_data: {
    epicSchema: S_epic_set_node_execution_runtime_data,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.set_node_execution_runtime_data", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Sets the node's Execution Runtime struct. The struct types must match. Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id, runtime_data", "epic_call_tool"),
  },
  epic_set_node_instance_data: {
    epicSchema: S_epic_set_node_instance_data,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.set_node_instance_data", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Sets the node's Instance struct. The struct types must match. Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id, instance_data", "epic_call_tool"),
  },
  epic_set_node_template_data: {
    epicSchema: S_epic_set_node_template_data,
    epicTool: { toolset: "state_tree_toolset.state_tree.StateTreeToolset", name: "state_tree_toolset.state_tree.StateTreeToolset.set_node_template_data", engines: ["5.9"] },
    ...bp("mutate", "[Epic state_tree_toolset.state_tree.StateTreeToolset] Sets the node's Template struct. The struct types must match. Available on UE 5.9; absent from UE 5.8. Params: state_tree, node_id, node_data", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  child: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  condition_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  condition_id: z.string().optional(),
  condition_struct: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  consideration_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  consideration_id: z.string().optional(),
  consideration_struct: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  evaluator_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  evaluator_id: z.string().optional(),
  evaluator_struct: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  extension_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  index: z.number().optional(),
  instance_data: z.record(z.string(), z.unknown()).optional(),
  name: z.string().optional(),
  node: z.record(z.string(), z.unknown()).optional(),
  node_data: z.record(z.string(), z.unknown()).optional(),
  node_id: z.string().optional(),
  parent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  runtime_data: z.record(z.string(), z.unknown()).optional(),
  state: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  state_tree: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  state_type: z.string().optional(),
  sub_tree: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  task_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  task_id: z.string().optional(),
  task_struct: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
};
