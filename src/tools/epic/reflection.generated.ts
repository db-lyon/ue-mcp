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

const S_epic_get_class = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_get_class__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_get_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"properties":{"type":"array","items":{"type":"string"}}},"required":["instance","properties"]} as const;
const S_epic_get_properties__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"properties":{"type":"array","items":{"type":"string"}}},"required":["instance","properties"]} as const;
const S_epic_get_property_schema = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"root":{"type":"string"},"maxDepth":{"type":"integer"}},"required":["instance"]} as const;
const S_epic_list_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_list_properties__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"root":{"type":"string"},"nameFilter":{"type":"string"},"maxDepth":{"type":"integer"}},"required":["instance"]} as const;
const S_epic_reset_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"properties":{"type":"array","items":{"type":"string"}}},"required":["instance","properties"]} as const;
const S_epic_reset_properties__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"properties":{"type":"array","items":{"type":"string"}}},"required":["instance","properties"]} as const;
const S_epic_search_subclasses = {"properties":{"base_class":{"type":"object","properties":{"refPath":{}}},"class_name":{"type":"string"}},"required":["base_class","class_name"]} as const;
const S_epic_search_subclasses__1 = {"properties":{"baseClass":{"type":"object","properties":{"refPath":{}}},"className":{"type":"string"}},"required":["baseClass"]} as const;
const S_epic_set_properties = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"values":{"type":"string"}},"required":["instance","values"]} as const;
const S_epic_set_properties__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"values":{"type":"string"}},"required":["instance","values"]} as const;

/** 7 wrapped engine tools routed to the `reflection` category. */
export const actions: Record<string, ActionSpec> = {
  epic_get_class: {
    epicSchema: S_epic_get_class,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.get_class", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.GetClass", engines: ["5.9"], schema: S_epic_get_class__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Returns the class of an Unreal object. Params: instance. On UE 5.9 it calls EditorToolset.ObjectToolset.GetClass (Params: instance).", "epic_call_tool"),
  },
  epic_get_properties: {
    epicSchema: S_epic_get_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.get_properties", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.GetProperties", engines: ["5.9"], schema: S_epic_get_properties__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Returns the values of one or more properties on an object. Params: instance, properties. On UE 5.9 it calls EditorToolset.ObjectToolset.GetProperties (Params: instance, properties).", "epic_call_tool"),
  },
  epic_get_property_schema: {
    epicSchema: S_epic_get_property_schema,
    epicTool: { toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.GetPropertySchema", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.ObjectToolset] Returns a JSON Schema of the properties on an object, with each property's type and description. Much larger than ListProperties, so find the property there and come here only when its type is needed. A schema describes types rather than an instance, so Root cannot carry a subscript. Available on UE 5.9; absent from UE 5.8. Params: instance, root?, maxDepth?", "epic_call_tool"),
  },
  epic_list_properties: {
    epicSchema: S_epic_list_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.list_properties", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.ListProperties", engines: ["5.9"], schema: S_epic_list_properties__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Returns a list of properties that are on the specified object. Params: instance. On UE 5.9 it calls EditorToolset.ObjectToolset.ListProperties (Params: instance, root?, nameFilter?, maxDepth?).", "epic_call_tool"),
  },
  epic_reset_properties: {
    epicSchema: S_epic_reset_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.reset_properties", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.ResetProperties", engines: ["5.9"], schema: S_epic_reset_properties__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.object.ObjectTools] Resets one or more properties on an object to their default values, removing any per-instance overrides. Params: instance, properties. On UE 5.9 it calls EditorToolset.ObjectToolset.ResetProperties (Params: instance, properties).", "epic_call_tool"),
  },
  epic_search_subclasses: {
    epicSchema: S_epic_search_subclasses,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.search_subclasses", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.SearchSubclasses", engines: ["5.9"], schema: S_epic_search_subclasses__1, rename: {"base_class":"baseClass","class_name":"className"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.object.ObjectTools] Finds all subclasses of a given class. Params: base_class, class_name. On UE 5.9 it calls EditorToolset.ObjectToolset.SearchSubclasses (Params: baseClass, className?).", "epic_call_tool"),
  },
  epic_set_properties: {
    epicSchema: S_epic_set_properties,
    epicTool: { toolset: "editor_toolset.toolsets.object.ObjectTools", name: "editor_toolset.toolsets.object.ObjectTools.set_properties", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ObjectToolset", name: "EditorToolset.ObjectToolset.SetProperties", engines: ["5.9"], schema: S_epic_set_properties__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.object.ObjectTools] Sets the values of properties on an object. Params: instance, values. On UE 5.9 it calls EditorToolset.ObjectToolset.SetProperties (Params: instance, values).", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  base_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  baseClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The class to search subclasses of."),
  class_name: z.string().optional(),
  className: z.string().optional().describe("Optional case-insensitive substring filter on the class path."),
  instance: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  maxDepth: z.number().optional().describe("Levels below Root to describe. Zero or less describes every level. Defaults to one here so an agent cannot dump a whole schema by accident."),
  nameFilter: z.string().optional().describe("Regular expression searched, ignoring case, in a property's name and its Keywords metadata. A match lists with its subtree, a non-matching ancestor survives on…"),
  properties: z.array(z.unknown()).optional().describe("The names or dotted paths of the properties to query, such as \"BodyInstance.MassInKgOverride\" or \"OverrideMaterials[0]\". Results are keyed by the path as writt…"),
  root: z.string().optional().describe("Dotted path to describe from. Empty describes the object itself. A container segment describes its element type."),
  values: z.string().optional().describe("A JSON formatted string of the properties to set and their values. Keys may be names or dotted paths, such as {\"BodyInstance.MassInKgOverride\": 2.0} or {\"Overr…"),
};
