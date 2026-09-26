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
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../category-tool.js";

const S_epic_get_children = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_editor_data = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_enter_conditions = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_evaluators = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_global_tasks = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_node_description = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object"}},"required":["state_tree","node"]} as const;
const S_epic_get_root_states = {"properties":{"state_tree":{"type":"object","properties":{"refPath":{}}}},"required":["state_tree"]} as const;
const S_epic_get_tasks = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;
const S_epic_get_transitions = {"properties":{"state":{"type":"object","properties":{"refPath":{}}}},"required":["state"]} as const;

/** 9 wrapped engine tools routed to the `statetree` category. */
export const actions: Record<string, ActionSpec> = {
  epic_get_children: {
    epicSchema: S_epic_get_children,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_children" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns child states of a state. Params: state", "epic_call_tool"),
  },
  epic_get_editor_data: {
    epicSchema: S_epic_get_editor_data,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_editor_data" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns the editor data for a StateTree asset. Params: state_tree", "epic_call_tool"),
  },
  epic_get_enter_conditions: {
    epicSchema: S_epic_get_enter_conditions,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_enter_conditions" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns enter conditions on a state. Params: state", "epic_call_tool"),
  },
  epic_get_evaluators: {
    epicSchema: S_epic_get_evaluators,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_evaluators" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns global evaluators. Params: state_tree", "epic_call_tool"),
  },
  epic_get_global_tasks: {
    epicSchema: S_epic_get_global_tasks,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_global_tasks" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns global tasks that run across all states. Params: state_tree", "epic_call_tool"),
  },
  epic_get_node_description: {
    epicSchema: S_epic_get_node_description,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_node_description" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns a human-readable description for a node. Params: state_tree, node", "epic_call_tool"),
  },
  epic_get_root_states: {
    epicSchema: S_epic_get_root_states,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_root_states" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns top-level states of a StateTree. Params: state_tree", "epic_call_tool"),
  },
  epic_get_tasks: {
    epicSchema: S_epic_get_tasks,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_tasks" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns tasks on a state. Params: state", "epic_call_tool"),
  },
  epic_get_transitions: {
    epicSchema: S_epic_get_transitions,
    epicTool: { toolset: "state_tree_toolset.toolsets.state_tree.StateTreeTools", name: "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_transitions" },
    ...bp("read", "[Epic state_tree_toolset.toolsets.state_tree.StateTreeTools] Returns transitions on a state. Params: state", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  node: z.record(z.unknown()).optional(),
  state: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  state_tree: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
};
