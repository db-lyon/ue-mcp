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
import type { ActionSpec } from "../../types.js";
import { bp } from "../../category-tool.js";

const S_epic_add_component_bound_event = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"event_name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["component","event_name","graph"]} as const;
const S_epic_add_event = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"event_name":{"type":"string"},"position":{"type":"object"}},"required":["blueprint","event_name"]} as const;
const S_epic_add_event_dispatcher = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["blueprint","name"]} as const;
const S_epic_add_function_graph = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["blueprint","graph_name"]} as const;
const S_epic_add_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"param_type":{"type":"string"},"input_param":{"type":"boolean"},"container_type":{"type":"string"}},"required":["graph","param_name","param_type","input_param"]} as const;
const S_epic_add_node_pin = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_add_object_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"object_class":{"type":"object","properties":{"refPath":{}}},"input_param":{"type":"boolean"},"container_type":{"type":"string"}},"required":["graph","param_name","object_class","input_param"]} as const;
const S_epic_add_object_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"object_class":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"container_type":{"type":"string"}},"required":["blueprint","name","object_class"]} as const;
const S_epic_add_struct_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"struct_type":{"type":"object","properties":{"refPath":{}}},"input_param":{"type":"boolean"},"container_type":{"type":"string"}},"required":["graph","param_name","struct_type","input_param"]} as const;
const S_epic_add_struct_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"struct_type":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"container_type":{"type":"string"}},"required":["blueprint","name","struct_type"]} as const;
const S_epic_add_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"type_name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}},"container_type":{"type":"string"}},"required":["blueprint","name","type_name"]} as const;
const S_epic_arrange_nodes = {"properties":{"nodes":{"type":"array"}},"required":["nodes"]} as const;
const S_epic_break_pins = {"properties":{"output_pin":{"type":"object"},"input_pin":{"type":"object"}},"required":["output_pin","input_pin"]} as const;
const S_epic_compile_blueprint = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"warnings_as_errors":{"type":"boolean"}},"required":["blueprint"]} as const;
const S_epic_connect_pins = {"properties":{"output_pin":{"type":"object"},"input_pin":{"type":"object"}},"required":["output_pin","input_pin"]} as const;
const S_epic_create = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"asset_type":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","asset_type"]} as const;
const S_epic_create_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"type_id":{"type":"string"},"pos":{"type":"object"},"declaring_class":{"type":"object","properties":{"refPath":{}}}},"required":["graph","type_id","pos"]} as const;
const S_epic_delete_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_find_node_categories = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"category_filter":{"type":"string"},"context_pins":{"type":"array"}},"required":["graph","category_filter","context_pins"]} as const;
const S_epic_find_node_types = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"type_id_filter":{"type":"string"},"context_pins":{"type":"array"}},"required":["graph","type_id_filter","context_pins"]} as const;
const S_epic_find_nodes = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"title":{"type":"string"},"node_class":{"type":"object","properties":{"refPath":{}}},"entry_points_only":{"type":"boolean"}},"required":["graph","title"]} as const;
const S_epic_get_connected_subgraph = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_create_event_function = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_default_object = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_get_graph = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["blueprint","graph_name"]} as const;
const S_epic_get_graph_dsl_docs = {"properties":{}} as const;
const S_epic_get_node_infos = {"properties":{"nodes":{"type":"array"}},"required":["nodes"]} as const;
const S_epic_get_node_type_pins = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"type_id":{"type":"string"}},"required":["graph","type_id"]} as const;
const S_epic_get_parent = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_get_pin_value = {"properties":{"pin":{"type":"object"}},"required":["pin"]} as const;
const S_epic_get_variable_category = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"}},"required":["blueprint","variable_name"]} as const;
const S_epic_get_variable_replication = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"}},"required":["blueprint","variable_name"]} as const;
const S_epic_list_compatible_event_functions = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_list_component_events = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_list_event_dispatchers = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_events = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_functions = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_graphs = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_variables = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_read_graph_dsl = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_remove_function_graph = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["blueprint","graph_name"]} as const;
const S_epic_remove_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"input_param":{"type":"boolean"}},"required":["graph","param_name","input_param"]} as const;
const S_epic_remove_node_pin = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object"}},"required":["node","pin"]} as const;
const S_epic_remove_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","name"]} as const;
const S_epic_retarget_node_class = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"old_class":{"type":"object","properties":{"refPath":{}}},"new_class":{"type":"object","properties":{"refPath":{}}}},"required":["node","old_class","new_class"]} as const;
const S_epic_set_create_event_function = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"function_name":{"type":"string"}},"required":["node","function_name"]} as const;
const S_epic_set_node_position = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pos":{"type":"object"}},"required":["node","pos"]} as const;
const S_epic_set_parent = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"parent_class":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","parent_class"]} as const;
const S_epic_set_pin_value = {"properties":{"pin":{"type":"object"},"value":{"type":"string"}},"required":["pin","value"]} as const;
const S_epic_set_variable_category = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"category":{"type":"string"}},"required":["blueprint","variable_name","category"]} as const;
const S_epic_set_variable_instance_editable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"instance_editable":{"type":"boolean"}},"required":["blueprint","variable_name","instance_editable"]} as const;
const S_epic_set_variable_replication = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"replication":{"type":"string"}},"required":["blueprint","variable_name","replication"]} as const;
const S_epic_write_graph_dsl = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"code":{"type":"string"}},"required":["graph","code"]} as const;

/** 53 wrapped engine tools routed to the `blueprint` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_component_bound_event: {
    epicSchema: S_epic_add_component_bound_event,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_component_bound_event" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Creates a component bound event node in the event graph. Params: component, event_name, graph", "epic_call_tool"),
  },
  epic_add_event: {
    epicSchema: S_epic_add_event,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_event" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an event node to the Blueprint's event graph. If event_name matches an inherited overridable event, the new node is an override of that event. Otherwise a new custom event with the given name is created. Idempotent - if an event node with that name already exists, that node is returned. Params: blueprint, event_name, position?", "epic_call_tool"),
  },
  epic_add_event_dispatcher: {
    epicSchema: S_epic_add_event_dispatcher,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_event_dispatcher" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an event dispatcher to a Blueprint. Params: blueprint, name", "epic_call_tool"),
  },
  epic_add_function_graph: {
    epicSchema: S_epic_add_function_graph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_function_graph" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a function graph to the Blueprint. If graph_name matches an inherited overridable function, the new graph is a function-graph override of that function. Idempotent - if a graph with that name already exists, that graph is returned. Params: blueprint, graph_name", "epic_call_tool"),
  },
  epic_add_function_param: {
    epicSchema: S_epic_add_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_function_param" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an input or output to a function or event dispatcher Params: graph, param_name, param_type, input_param, container_type?", "epic_call_tool"),
  },
  epic_add_node_pin: {
    epicSchema: S_epic_add_node_pin,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_node_pin" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a pin to a node that supports dynamic pin addition. Works for Switch nodes (adds one case pin), Sequence nodes (adds one Then output), commutative binary operators like Add/Multiply (adds one input), Make Array nodes, etc. Params: node", "epic_call_tool"),
  },
  epic_add_object_function_param: {
    epicSchema: S_epic_add_object_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_object_function_param" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an object reference input or output to a function or event dispatcher. Params: graph, param_name, object_class, input_param, container_type?", "epic_call_tool"),
  },
  epic_add_object_variable: {
    epicSchema: S_epic_add_object_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_object_variable" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a member or local variable that holds an object reference to a Blueprint. Params: blueprint, name, object_class, graph?, container_type?", "epic_call_tool"),
  },
  epic_add_struct_function_param: {
    epicSchema: S_epic_add_struct_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_struct_function_param" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a struct input or output to a function or event dispatcher. Params: graph, param_name, struct_type, input_param, container_type?", "epic_call_tool"),
  },
  epic_add_struct_variable: {
    epicSchema: S_epic_add_struct_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_struct_variable" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a member or local variable of a struct type to a Blueprint. Use this to add variables of any UStruct type, including custom structs and engine structs not in the basic list supported by add_variable (e.g. HitResult, GameplayTag). Params: blueprint, name, struct_type, graph?, container_type?", "epic_call_tool"),
  },
  epic_add_variable: {
    epicSchema: S_epic_add_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_variable" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a member or local variable to a Blueprint. Supported type names: Primitives: 'bool', 'int', 'float', 'byte', 'string', 'name', 'text' Structs: 'Vector', 'Rotator', 'Transform', 'Vector2D', 'LinearColor' Params: blueprint, name, type_name, graph?, container_type?", "epic_call_tool"),
  },
  epic_arrange_nodes: {
    epicSchema: S_epic_arrange_nodes,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.arrange_nodes" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Arranges a list of nodes in a readable left-to-right layout. Organizes nodes into columns based on data/execution flow, with producer nodes to the left of the nodes they feed into. Call this after building a graph to avoid nodes overlapping. Connections to nodes outside the list are used as anchors so the arranged nodes integrate cleanly with the rest of the graph. Params: nodes", "epic_call_tool"),
  },
  epic_break_pins: {
    epicSchema: S_epic_break_pins,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.break_pins" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Breaks the connection between two pins. Params: output_pin, input_pin", "epic_call_tool"),
  },
  epic_compile_blueprint: {
    epicSchema: S_epic_compile_blueprint,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.compile_blueprint" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Compiles the given Blueprint. Blueprints should be compiled after all graph modifications are complete. Params: blueprint, warnings_as_errors?", "epic_call_tool"),
  },
  epic_connect_pins: {
    epicSchema: S_epic_connect_pins,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.connect_pins" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Makes a connection between source (output) and dest (input) pins. Params: output_pin, input_pin", "epic_call_tool"),
  },
  epic_create: {
    epicSchema: S_epic_create,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.create" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Creates a new Blueprint asset in the project. Params: folder_path, asset_name, asset_type", "epic_call_tool"),
  },
  epic_create_node: {
    epicSchema: S_epic_create_node,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.create_node" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a new node to the graph. Params: graph, type_id, pos, declaring_class?", "epic_call_tool"),
  },
  epic_delete_node: {
    epicSchema: S_epic_delete_node,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.delete_node" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Deletes the node from its graph. Params: node", "epic_call_tool"),
  },
  epic_find_node_categories: {
    epicSchema: S_epic_find_node_categories,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.find_node_categories" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Retrieves a list of available node categories in a Blueprint graph, optionally filtered by compatible input and/or output pin types Params: graph, category_filter, context_pins", "epic_call_tool"),
  },
  epic_find_node_types: {
    epicSchema: S_epic_find_node_types,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.find_node_types" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Finds node types that can be created in a particular graph meeting the search criteria. Params: graph, type_id_filter, context_pins", "epic_call_tool"),
  },
  epic_find_nodes: {
    epicSchema: S_epic_find_nodes,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.find_nodes" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Finds nodes in a graph by title, class, and/or execution role. All filters are optional and ANDed together. Useful for locating specific event chains in large graphs like EventGraph before reading them with get_connected_subgraph. Params: graph, title, node_class?, entry_points_only?", "epic_call_tool"),
  },
  epic_get_connected_subgraph: {
    epicSchema: S_epic_get_connected_subgraph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_connected_subgraph" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns detailed information for all nodes connected to the given node. Use this alongside find_nodes to read a single event chain from a large graph (like Event Graph) without reading the entire graph. Params: node", "epic_call_tool"),
  },
  epic_get_create_event_function: {
    epicSchema: S_epic_get_create_event_function,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_create_event_function" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the function currently bound to a Create Event node. Params: node", "epic_call_tool"),
  },
  epic_get_default_object: {
    epicSchema: S_epic_get_default_object,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_default_object" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the Class Default Object (CDO) for a Blueprint's. ObjectTools list/set/get property will get the CDO automatically, so this should primarily be used before calling tools that want to operate on the object inside the blueprint (like ActorTools). Params: blueprint", "epic_call_tool"),
  },
  epic_get_graph: {
    epicSchema: S_epic_get_graph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_graph" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Retrieves a specific graph from a Blueprint asset by name. Params: blueprint, graph_name", "epic_call_tool"),
  },
  epic_get_graph_dsl_docs: {
    epicSchema: S_epic_get_graph_dsl_docs,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_graph_dsl_docs" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the full syntax reference for write_graph_dsl. Call this before using write_graph_dsl for the first time. Params: none", "epic_call_tool"),
  },
  epic_get_node_infos: {
    epicSchema: S_epic_get_node_infos,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_node_infos" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Retrieves detailed information for a list of Blueprint graph nodes. Params: nodes", "epic_call_tool"),
  },
  epic_get_node_type_pins: {
    epicSchema: S_epic_get_node_type_pins,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_node_type_pins" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the pin names and types for a node type. Params: graph, type_id", "epic_call_tool"),
  },
  epic_get_parent: {
    epicSchema: S_epic_get_parent,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_parent" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the parent class of a Blueprint. Params: blueprint", "epic_call_tool"),
  },
  epic_get_pin_value: {
    epicSchema: S_epic_get_pin_value,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_pin_value" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Gets the value of a Blueprint graph pin. Params: pin", "epic_call_tool"),
  },
  epic_get_variable_category: {
    epicSchema: S_epic_get_variable_category,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_variable_category" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Gets the user-defined category of a Blueprint member variable. Categories group variables in the My Blueprint panel. Variables with no explicit category default to the Blueprint's name in the UI. Params: blueprint, variable_name", "epic_call_tool"),
  },
  epic_get_variable_replication: {
    epicSchema: S_epic_get_variable_replication,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_variable_replication" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Gets the replication mode of a Blueprint member variable. Params: blueprint, variable_name", "epic_call_tool"),
  },
  epic_list_compatible_event_functions: {
    epicSchema: S_epic_list_compatible_event_functions,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_compatible_event_functions" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists functions that can be bound to a Create Event node. Params: node", "epic_call_tool"),
  },
  epic_list_component_events: {
    epicSchema: S_epic_list_component_events,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_component_events" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists the bindable delegate events available on a component. Params: component", "epic_call_tool"),
  },
  epic_list_event_dispatchers: {
    epicSchema: S_epic_list_event_dispatchers,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_event_dispatchers" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all event dispatchers defined on a Blueprint. Params: blueprint", "epic_call_tool"),
  },
  epic_list_events: {
    epicSchema: S_epic_list_events,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_events" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all events visible on the Blueprint - locally defined custom events plus inheritable events from the parent class chain and implemented interfaces. Params: blueprint", "epic_call_tool"),
  },
  epic_list_functions: {
    epicSchema: S_epic_list_functions,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_functions" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all functions visible on the Blueprint - locally defined plus inheritable ones from the parent class chain and implemented interfaces. Params: blueprint", "epic_call_tool"),
  },
  epic_list_graphs: {
    epicSchema: S_epic_list_graphs,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_graphs" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all graphs in the Blueprint. Params: blueprint", "epic_call_tool"),
  },
  epic_list_variables: {
    epicSchema: S_epic_list_variables,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_variables" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists member or local variables defined on a Blueprint. Params: blueprint, graph?", "epic_call_tool"),
  },
  epic_read_graph_dsl: {
    epicSchema: S_epic_read_graph_dsl,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.read_graph_dsl" },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Reads a Blueprint graph and returns a DSL script. The returned code uses the same syntax that write_graph_dsl accepts and can be edited and passed back to write_graph_dsl to modify the graph. Call get_graph_dsl_docs() for the full syntax reference. Params: graph", "epic_call_tool"),
  },
  epic_remove_function_graph: {
    epicSchema: S_epic_remove_function_graph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_function_graph" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes a function graph or event dispatcher from the Blueprint. Params: blueprint, graph_name", "epic_call_tool"),
  },
  epic_remove_function_param: {
    epicSchema: S_epic_remove_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_function_param" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes an input or output from a function or event dispatcher. Params: graph, param_name, input_param", "epic_call_tool"),
  },
  epic_remove_node_pin: {
    epicSchema: S_epic_remove_node_pin,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_node_pin" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes a specific pin from a node that supports dynamic pin removal. Works for Switch nodes (removes one case pin), Sequence nodes (removes one Then output), commutative binary operators like Add/Multiply (removes one input), Make Array nodes, etc. Params: node, pin", "epic_call_tool"),
  },
  epic_remove_variable: {
    epicSchema: S_epic_remove_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_variable" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes a member or local variable from a Blueprint. Params: blueprint, name, graph?", "epic_call_tool"),
  },
  epic_retarget_node_class: {
    epicSchema: S_epic_retarget_node_class,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.retarget_node_class" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Replaces a node's baked-in class reference from old_class to new_class in place. If the node's current class reference matches old_class it is replaced with new_class and the node is reconstructed so its pins reflect the new type. If the node already references new_class the call is a no-op. When a Blueprint is duplicated, nodes in the copied graph retain class references pointing to the original Blueprint. Calling this on each node with the original and duplicate classes retargets them without manual delete-recreate-rewire cycles. Handles cast, function call, event, and multicast delegate nodes. The Blueprint must be compiled after all retargeting is complete. Params: node, old_class, new_class", "epic_call_tool"),
  },
  epic_set_create_event_function: {
    epicSchema: S_epic_set_create_event_function,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_create_event_function" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Binds a function to a Create Event node. Use list_compatible_event_functions to find valid function names. Params: node, function_name", "epic_call_tool"),
  },
  epic_set_node_position: {
    epicSchema: S_epic_set_node_position,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_node_position" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets a new position for the node. Params: node, pos", "epic_call_tool"),
  },
  epic_set_parent: {
    epicSchema: S_epic_set_parent,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_parent" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Reparents a Blueprint to a new parent class. The Blueprint must be recompiled after reparenting. Params: blueprint, parent_class", "epic_call_tool"),
  },
  epic_set_pin_value: {
    epicSchema: S_epic_set_pin_value,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_pin_value" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets the value of a Blueprint graph pin. Params: pin, value", "epic_call_tool"),
  },
  epic_set_variable_category: {
    epicSchema: S_epic_set_variable_category,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_category" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets the user-defined category on a Blueprint member variable. Categories group variables in the My Blueprint panel. Pass an empty string to reset to the default (the Blueprint's name). Params: blueprint, variable_name, category", "epic_call_tool"),
  },
  epic_set_variable_instance_editable: {
    epicSchema: S_epic_set_variable_instance_editable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_instance_editable" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets whether a member variable is editable per-instance on actors placed in the level. Params: blueprint, variable_name, instance_editable", "epic_call_tool"),
  },
  epic_set_variable_replication: {
    epicSchema: S_epic_set_variable_replication,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_replication" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets the replication mode on a Blueprint member variable. RepNotify will automatically create an OnRep_ function on the Blueprint if one does not already exist. Params: blueprint, variable_name, replication", "epic_call_tool"),
  },
  epic_write_graph_dsl: {
    epicSchema: S_epic_write_graph_dsl,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.write_graph_dsl" },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Populates a Blueprint graph with nodes from a DSL script and compiles the Blueprint. Call get_graph_dsl_docs() for the full syntax reference and examples. Params: graph, code", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset_name: z.string().optional(),
  asset_type: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  blueprint: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  category: z.string().optional(),
  category_filter: z.string().optional(),
  code: z.string().optional(),
  component: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  container_type: z.string().optional(),
  context_pins: z.array(z.unknown()).optional(),
  declaring_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  entry_points_only: z.boolean().optional(),
  event_name: z.string().optional(),
  folder_path: z.string().optional(),
  function_name: z.string().optional(),
  graph: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  graph_name: z.string().optional(),
  input_param: z.boolean().optional(),
  input_pin: z.record(z.unknown()).optional(),
  instance_editable: z.boolean().optional(),
  name: z.string().optional(),
  new_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  node: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  node_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  nodes: z.array(z.unknown()).optional(),
  object_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  old_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  output_pin: z.record(z.unknown()).optional(),
  param_name: z.string().optional(),
  param_type: z.string().optional(),
  parent_class: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  pin: z.record(z.unknown()).optional(),
  pos: z.record(z.unknown()).optional(),
  position: z.record(z.unknown()).optional(),
  replication: z.string().optional(),
  struct_type: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  title: z.string().optional(),
  type_id: z.string().optional(),
  type_id_filter: z.string().optional(),
  type_name: z.string().optional(),
  value: z.string().optional(),
  variable_name: z.string().optional(),
  warnings_as_errors: z.boolean().optional(),
};
