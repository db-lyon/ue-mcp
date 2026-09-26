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
import { bp } from "../../surface/category-tool.js";

const S_epic_get_class = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_get_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"properties":{"type":"array"}},"required":["instance","properties"]} as const;
const S_epic_list_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_reset_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"properties":{"type":"array"}},"required":["instance","properties"]} as const;
const S_epic_search_subclasses = {"properties":{"base_class":{"type":"object","properties":{"refPath":{}}},"class_name":{"type":"string"}},"required":["base_class","class_name"]} as const;
const S_epic_set_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"values":{"type":"string"}},"required":["instance","values"]} as const;

/** 6 wrapped engine tools routed to the `reflection` category. */
export const actions: Record<string, ActionSpec> = {
  epic_get_class: {
    epicSchema: S_epic_get_class,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.get_class" },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Returns the class of an Unreal object. Params: instance", "epic_call_tool"),
  },
  epic_get_properties: {
    epicSchema: S_epic_get_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.get_properties" },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Returns the values of one or more properties on an object. Params: instance, properties", "epic_call_tool"),
  },
  epic_list_properties: {
    epicSchema: S_epic_list_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.list_properties" },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Returns a list of properties that are on the specified object. Params: instance", "epic_call_tool"),
  },
  epic_reset_properties: {
    epicSchema: S_epic_reset_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.reset_properties" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.object.ObjectTools] Resets one or more properties on an object to their default values, removing any per-instance overrides. Params: instance, properties", "epic_call_tool"),
  },
  epic_search_subclasses: {
    epicSchema: S_epic_search_subclasses,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.search_subclasses" },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Finds all subclasses of a given class. Params: base_class, class_name", "epic_call_tool"),
  },
  epic_set_properties: {
    epicSchema: S_epic_set_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.set_properties" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.object.ObjectTools] Sets the values of properties on an object. Params: instance, values", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  base_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  class_name: z.string().optional(),
  instance: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  properties: z.array(z.unknown()).optional(),
  values: z.string().optional(),
};
