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

const S_epic_add_expression = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"expression_class":{"type":"object","properties":{"refPath":{}}},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["material_or_function","expression_class"]} as const;
const S_epic_clear_parameters = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_connect_expressions = {"properties":{"from_expression":{"type":"object","properties":{"refPath":{}}},"from_output_name":{"type":"string"},"to_expression":{"type":"object","properties":{"refPath":{}}},"to_input_name":{"type":"string"}},"required":["from_expression","from_output_name","to_expression","to_input_name"]} as const;
const S_epic_connect_to_output = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}},"output_name":{"type":"string"},"material_property":{"type":"string"}},"required":["expression","output_name","material_property"]} as const;
const S_epic_create = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","parent"]} as const;
const S_epic_create_function = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create_material = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create_parameter_collection = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_delete_expression = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"expression":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function","expression"]} as const;
const S_epic_delete_parameter_group = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"group_name":{"type":"string"}},"required":["material_or_function","group_name"]} as const;
const S_epic_delete_unused_expressions = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_disconnect_expressions = {"properties":{"to_expression":{"type":"object","properties":{"refPath":{}}},"to_input_name":{"type":"string"}},"required":["to_expression","to_input_name"]} as const;
const S_epic_disconnect_from_output = {"properties":{"material":{"type":"object","properties":{"refPath":{}}},"material_property":{"type":"string"}},"required":["material","material_property"]} as const;
const S_epic_get_expression_input_names = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}}},"required":["expression"]} as const;
const S_epic_get_expression_inputs = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"expression":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function","expression"]} as const;
const S_epic_get_expression_output_names = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}}},"required":["expression"]} as const;
const S_epic_get_expressions = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_get_property_input = {"properties":{"material":{"type":"object","properties":{"refPath":{}}},"material_property":{"type":"string"}},"required":["material","material_property"]} as const;
const S_epic_get_referencing_materials = {"properties":{"material_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_function"]} as const;
const S_epic_get_scalar_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_static_switch_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_texture_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_vector_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_layout_expressions = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_list_expression_classes = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"search":{"type":"string"}},"required":["material_or_function","search"]} as const;
const S_epic_list_parameter_groups = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_list_parameters = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_recompile = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_rename_parameter_group = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["material_or_function","old_name","new_name"]} as const;
const S_epic_set_parameter_override = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"override":{"type":"boolean"}},"required":["instance","name","override"]} as const;
const S_epic_set_parent = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["instance","parent"]} as const;
const S_epic_set_scalar_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"number"}},"required":["instance","name","value"]} as const;
const S_epic_set_static_switch_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"boolean"}},"required":["instance","name","value"]} as const;
const S_epic_set_texture_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"object","properties":{"refPath":{}}}},"required":["instance","name","value"]} as const;
const S_epic_set_vector_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"object"}},"required":["instance","name","value"]} as const;

/** 35 wrapped engine tools routed to the `material` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_expression: {
    epicSchema: S_epic_add_expression,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.add_expression" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Adds a new expression node to a Material or MaterialFunction graph. Use list_expression_classes to discover available types. Params: material_or_function, expression_class, x?, y?", "epic_call_tool"),
  },
  epic_clear_parameters: {
    epicSchema: S_epic_clear_parameters,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.clear_parameters" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Clears all parameter overrides on a material instance, reverting to parent defaults. Params: instance", "epic_call_tool"),
  },
  epic_connect_expressions: {
    epicSchema: S_epic_connect_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.connect_expressions" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Connects an expression node's output pin to another expression node's input pin. Params: from_expression, from_output_name, to_expression, to_input_name", "epic_call_tool"),
  },
  epic_connect_to_output: {
    epicSchema: S_epic_connect_to_output,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.connect_to_output" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Connects an expression node's output to one of the material's output properties. Params: expression, output_name, material_property", "epic_call_tool"),
  },
  epic_create: {
    epicSchema: S_epic_create,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.create" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Creates a new MaterialInstanceConstant asset derived from a parent material. Material instances expose the parent's parameters without triggering a full shader recompile when parameter values change. Params: folder_path, asset_name, parent", "epic_call_tool"),
  },
  epic_create_function: {
    epicSchema: S_epic_create_function,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.create_function" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Creates a new empty MaterialFunction asset. Params: folder_path, asset_name", "epic_call_tool"),
  },
  epic_create_material: {
    epicSchema: S_epic_create_material,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.create_material" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Creates a new empty Material asset. Warning: Each new Material increases shader compile times. Prefer creating a MaterialInstance from an existing Material where possible. Params: folder_path, asset_name", "epic_call_tool"),
  },
  epic_create_parameter_collection: {
    epicSchema: S_epic_create_parameter_collection,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.create_parameter_collection" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Creates a new empty MaterialParameterCollection (MPC) asset. An MPC holds named Scalar and Vector parameters with default values that materials can reference at runtime without recompiling shaders. Params: folder_path, asset_name", "epic_call_tool"),
  },
  epic_delete_expression: {
    epicSchema: S_epic_delete_expression,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.delete_expression" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Removes an expression node from a Material or MaterialFunction graph. Params: material_or_function, expression", "epic_call_tool"),
  },
  epic_delete_parameter_group: {
    epicSchema: S_epic_delete_parameter_group,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.delete_parameter_group" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Removes a parameter group, ungrouping all parameters that belong to it. The parameter expressions themselves are not deleted - only their group assignment is cleared. Params: material_or_function, group_name", "epic_call_tool"),
  },
  epic_delete_unused_expressions: {
    epicSchema: S_epic_delete_unused_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.delete_unused_expressions" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Deletes all expression nodes not connected to any material output. Useful for cleaning up a material graph after reorganising or after the AI has added experimental nodes that were later abandoned. Params: material", "epic_call_tool"),
  },
  epic_disconnect_expressions: {
    epicSchema: S_epic_disconnect_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.disconnect_expressions" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Disconnects the input pin of an expression node, removing whatever is connected to it. Params: to_expression, to_input_name", "epic_call_tool"),
  },
  epic_disconnect_from_output: {
    epicSchema: S_epic_disconnect_from_output,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.disconnect_from_output" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Disconnects the expression currently connected to a material output property. Params: material, material_property", "epic_call_tool"),
  },
  epic_get_expression_input_names: {
    epicSchema: S_epic_get_expression_input_names,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expression_input_names" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the names of all input pins on a material expression node. Use these names as to_input_name when calling connect_expressions. Params: expression", "epic_call_tool"),
  },
  epic_get_expression_inputs: {
    epicSchema: S_epic_get_expression_inputs,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expression_inputs" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the current wiring of each input pin on a material expression. Use after building or modifying a graph to verify the wiring matches expectations. Params: material_or_function, expression", "epic_call_tool"),
  },
  epic_get_expression_output_names: {
    epicSchema: S_epic_get_expression_output_names,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expression_output_names" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the names of all output pins on a material expression node. Use these names as from_output_name when calling connect_expressions or connect_to_output. The empty string represents the default (first) output of nodes that expose only an unnamed output. Params: expression", "epic_call_tool"),
  },
  epic_get_expressions: {
    epicSchema: S_epic_get_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expressions" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns all expression nodes in a Material or MaterialFunction graph. Params: material_or_function", "epic_call_tool"),
  },
  epic_get_property_input: {
    epicSchema: S_epic_get_property_input,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_property_input" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the expression and output pin feeding a material output property. Use to inspect what drives MP_EmissiveColor, MP_Opacity, MP_BaseColor, etc. Params: material, material_property", "epic_call_tool"),
  },
  epic_get_referencing_materials: {
    epicSchema: S_epic_get_referencing_materials,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_referencing_materials" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns asset data for all Materials that reference this MaterialFunction. Params: material_function", "epic_call_tool"),
  },
  epic_get_scalar_parameter: {
    epicSchema: S_epic_get_scalar_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_scalar_parameter" },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the current value of a scalar parameter on a material instance. Params: instance, name", "epic_call_tool"),
  },
  epic_get_static_switch_parameter: {
    epicSchema: S_epic_get_static_switch_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_static_switch_parameter" },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the value of a static switch parameter on a material instance. Params: instance, name", "epic_call_tool"),
  },
  epic_get_texture_parameter: {
    epicSchema: S_epic_get_texture_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_texture_parameter" },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the texture assigned to a texture parameter on a material instance. Params: instance, name", "epic_call_tool"),
  },
  epic_get_vector_parameter: {
    epicSchema: S_epic_get_vector_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_vector_parameter" },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the current value of a vector parameter on a material instance. Params: instance, name", "epic_call_tool"),
  },
  epic_layout_expressions: {
    epicSchema: S_epic_layout_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.layout_expressions" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Automatically arranges all expression nodes in a Material or MaterialFunction graph. Params: material_or_function", "epic_call_tool"),
  },
  epic_list_expression_classes: {
    epicSchema: S_epic_list_expression_classes,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.list_expression_classes" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns MaterialExpression subclasses valid for the given context. Use the results with add_expression. Pass a search string to filter by name, e.g. 'Multiply' or 'Parameter'. Params: material_or_function, search", "epic_call_tool"),
  },
  epic_list_parameter_groups: {
    epicSchema: S_epic_list_parameter_groups,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.list_parameter_groups" },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the unique parameter group names defined in a Material or MaterialFunction. Parameters are organised into groups in the Material Instance editor. This returns the distinct set of group names found across all parameter expressions in the graph. The empty string represents parameters that have not been assigned to a named group. Params: material_or_function", "epic_call_tool"),
  },
  epic_list_parameters: {
    epicSchema: S_epic_list_parameters,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.list_parameters" },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Returns all parameters exposed by a material or instance, with their names and types. Params: material", "epic_call_tool"),
  },
  epic_recompile: {
    epicSchema: S_epic_recompile,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.recompile" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Recompiles a Material or MaterialFunction after edits. For Materials, raises if the shader fails to compile. For MaterialFunctions, also recompiles any Materials that reference the function. Call this once after a set of graph modifications is complete - after adding or deleting expressions, making connections, or changing expression properties such as parameter names or default values. Params: material_or_function", "epic_call_tool"),
  },
  epic_rename_parameter_group: {
    epicSchema: S_epic_rename_parameter_group,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.rename_parameter_group" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Renames a parameter group across all parameter expressions in a Material or MaterialFunction. All parameters currently in old_name will be moved to new_name. If new_name already exists, the parameters are merged into it. Params: material_or_function, old_name, new_name", "epic_call_tool"),
  },
  epic_set_parameter_override: {
    epicSchema: S_epic_set_parameter_override,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parameter_override" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Enables or disables a parameter override on a material instance. Enabling sets the override to the current effective value. Disabling reverts to the parent. For non-static parameter types, disabling also discards the prior override value; re-enabling later restores the parent value, not the prior override. Static switches and static component masks preserve their value across toggle. Params: instance, name, override", "epic_call_tool"),
  },
  epic_set_parent: {
    epicSchema: S_epic_set_parent,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parent" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Changes the parent of a material instance. Params: instance, parent", "epic_call_tool"),
  },
  epic_set_scalar_parameter: {
    epicSchema: S_epic_set_scalar_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_scalar_parameter" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Sets the value of a scalar parameter on a material instance. Params: instance, name, value", "epic_call_tool"),
  },
  epic_set_static_switch_parameter: {
    epicSchema: S_epic_set_static_switch_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_static_switch_parameter" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Sets the value of a static switch parameter on a material instance. Params: instance, name, value", "epic_call_tool"),
  },
  epic_set_texture_parameter: {
    epicSchema: S_epic_set_texture_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_texture_parameter" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Assigns a texture to a texture parameter on a material instance. Params: instance, name, value", "epic_call_tool"),
  },
  epic_set_vector_parameter: {
    epicSchema: S_epic_set_vector_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_vector_parameter" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Sets the value of a vector parameter on a material instance. Params: instance, name, value", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset_name: z.string().optional(),
  expression: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  expression_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  folder_path: z.string().optional(),
  from_expression: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  from_output_name: z.string().optional(),
  group_name: z.string().optional(),
  instance: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material_function: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material_or_function: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material_property: z.string().optional(),
  name: z.string().optional(),
  new_name: z.string().optional(),
  old_name: z.string().optional(),
  output_name: z.string().optional(),
  override: z.boolean().optional(),
  parent: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  search: z.string().optional(),
  to_expression: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  to_input_name: z.string().optional(),
  value: z.unknown().optional().describe("Represents a reference to a UObject or UClass."),
  x: z.number().optional(),
  y: z.number().optional(),
};
