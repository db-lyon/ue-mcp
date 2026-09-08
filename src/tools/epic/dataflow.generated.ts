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

const S_epic_add_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array"},"comment":{"type":"string"},"color":{"type":"object"}},"required":["graph","nodes"]} as const;
const S_epic_add_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"typeName":{"type":"string"},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["graph","typeName","nodeName","jsonParams"]} as const;
const S_epic_add_variable = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"type":{"type":"string"}},"required":["graph","name","type"]} as const;
const S_epic_assign_dataflow_template = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"templateId":{"type":"string"}},"required":["asset","templateId"]} as const;
const S_epic_connect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPin":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPin":{"type":"string"}},"required":["fromNode","fromPin","toNode","toPin"]} as const;
const S_epic_create_dataflow_compatible_asset = {"properties":{"className":{"type":"string"},"name":{"type":"string"},"path":{"type":"string"}},"required":["className","name"]} as const;
const S_epic_create_dataflow_compatible_asset_from_template = {"properties":{"className":{"type":"string"},"name":{"type":"string"},"path":{"type":"string"},"templateId":{"type":"string"}},"required":["className","name","path","templateId"]} as const;
const S_epic_create_graph = {"properties":{"name":{"type":"string"},"path":{"type":"string"}},"required":["name","path"]} as const;
const S_epic_disconnect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPin":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPin":{"type":"string"}},"required":["fromNode","fromPin","toNode","toPin"]} as const;
const S_epic_get_graph_structure = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_node_info = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_node_type_schema = {"properties":{"typeName":{"type":"string"}},"required":["typeName"]} as const;
const S_epic_list_dataflow_compatible_asset_types = {"properties":{}} as const;
const S_epic_list_dataflow_templates_for_asset_class = {"properties":{"className":{"type":"string"},"bIncludeBlank":{"type":"boolean"}},"required":["className"]} as const;
const S_epic_list_node_types = {"properties":{"bCommonOnly":{"type":"boolean"}}} as const;
const S_epic_list_variables = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_remove_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"}},"required":["graph","commentId"]} as const;
const S_epic_remove_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["graph","node"]} as const;
const S_epic_remove_variable = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["graph","name"]} as const;
const S_epic_reposition_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["node","x","y"]} as const;
const S_epic_set_variable = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"string"}},"required":["graph","name","value"]} as const;
const S_epic_update_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"}},"required":["node","jsonParams"]} as const;

/** 22 wrapped engine tools routed to the `dataflow` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_comment_box: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Adds a comment box around the given nodes. Params: graph, nodes, comment?, color?",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.AddCommentBox", S_epic_add_comment_box, p),
  ),
  epic_add_node: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Adds a node of the given type to the Dataflow graph. Params: graph, typeName, nodeName, jsonParams, x?, y?",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.AddNode", S_epic_add_node, p),
  ),
  epic_add_variable: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Adds a new variable to the Dataflow graph. Supported type strings: Primitives : \"Bool\", \"Int32\", \"Int64\", \"Float\", \"Double\", \"Name\", \"String\" Structs : UScriptStruct name with or without the \"F\" prefix e.g. \"Vector\", \"FVector\", \"Transform\", \"FTransform\", \"Rotator\", \"LinearColor\" Objects : \"Object:<ClassName>\" where ClassName is with or without the \"U\"/\"A\" prefix e.g. \"Object:StaticMesh\", \"Object:USkeletalMesh\" Params: graph, name, type",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.AddVariable", S_epic_add_variable, p),
  ),
  epic_assign_dataflow_template: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Assigns a Dataflow template to an existing Dataflow-compatible asset by duplicating the template graph and embedding it. Replaces any existing embedded graph. Params: asset, templateId",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.AssignDataflowTemplate", S_epic_assign_dataflow_template, p),
  ),
  epic_connect_node_pins: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Connects an output pin of one node to an input pin of another. Params: fromNode, fromPin, toNode, toPin",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.ConnectNodePins", S_epic_connect_node_pins, p),
  ),
  epic_create_dataflow_compatible_asset: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Creates a new Dataflow-compatible asset (e.g. ChaosClothAsset, GeometryCollection, FleshAsset, GroomAsset) with an empty embedded Dataflow graph. Params: className, name, path?",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.CreateDataflowCompatibleAsset", S_epic_create_dataflow_compatible_asset, p),
  ),
  epic_create_dataflow_compatible_asset_from_template: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Creates a new Dataflow-compatible asset and initialises its embedded Dataflow graph from a registered template in one step. Params: className, name, path, templateId",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.CreateDataflowCompatibleAssetFromTemplate", S_epic_create_dataflow_compatible_asset_from_template, p),
  ),
  epic_create_graph: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Creates a new saved Dataflow graph asset. Params: name, path",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.CreateGraph", S_epic_create_graph, p),
  ),
  epic_disconnect_node_pins: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Removes the connection between two node pins. Params: fromNode, fromPin, toNode, toPin",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.DisconnectNodePins", S_epic_disconnect_node_pins, p),
  ),
  epic_get_graph_structure: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns the complete structure of a Dataflow graph including all nodes and connections. Params: graph",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.GetGraphStructure", S_epic_get_graph_structure, p),
  ),
  epic_get_node_info: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns information about a node as a JSON object (name, type, position, pins). Params: node",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.GetNodeInfo", S_epic_get_node_info, p),
  ),
  epic_get_node_type_schema: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns the schema for a Dataflow node type including its input/output pins and editable UPROPERTY parameters. Params: typeName",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.GetNodeTypeSchema", S_epic_get_node_type_schema, p),
  ),
  epic_list_dataflow_compatible_asset_types: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns a JSON list of every UClass that can host an embedded Dataflow graph (i.e. implements IDataflowInstanceInterface). Each entry has \"className\", \"displayName\", and \"modulePath\" fields. Use the \"className\" value as input to CreateDataflowCompatibleAsset or ListDataflowTemplatesForAssetClass. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.ListDataflowCompatibleAssetTypes", S_epic_list_dataflow_compatible_asset_types, p),
  ),
  epic_list_dataflow_templates_for_asset_class: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns a JSON list of Dataflow templates registered for the given asset class. Templates registered for parent classes are included (class hierarchy walk). Params: className, bIncludeBlank?",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.ListDataflowTemplatesForAssetClass", S_epic_list_dataflow_templates_for_asset_class, p),
  ),
  epic_list_node_types: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns a JSON list of all registered Dataflow node types. Params: bCommonOnly?",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.ListNodeTypes", S_epic_list_node_types, p),
  ),
  epic_list_variables: bp(
    "read",
    "[Epic DataflowAgent.DataflowAgentToolset] Returns all variables defined on the Dataflow graph as a JSON array. Each entry contains \"name\", \"type\", and \"value\" fields. Params: graph",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.ListVariables", S_epic_list_variables, p),
  ),
  epic_remove_comment_box: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Removes a comment box node from the graph. Params: graph, commentId",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.RemoveCommentBox", S_epic_remove_comment_box, p),
  ),
  epic_remove_node: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Removes a node and all its connections from the Dataflow graph. Params: graph, node",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.RemoveNode", S_epic_remove_node, p),
  ),
  epic_remove_variable: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Removes a variable from the Dataflow graph. Params: graph, name",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.RemoveVariable", S_epic_remove_variable, p),
  ),
  epic_reposition_node: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Moves a node to a new position in the graph editor. Params: node, x, y",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.RepositionNode", S_epic_reposition_node, p),
  ),
  epic_set_variable: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Sets the value of an existing variable using its serialized string representation. The format depends on the variable's type (e.g., \"3.14\" for float, \"true\" for bool, \"42\" for int, \"MyName\" for FName). Params: graph, name, value",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.SetVariable", S_epic_set_variable, p),
  ),
  epic_update_node: bp(
    "mutate",
    "[Epic DataflowAgent.DataflowAgentToolset] Updates an existing node's editable properties via JSON. Params: node, jsonParams",
    "epic_call_tool",
    (p) => epicToolCall("DataflowAgent.DataflowAgentToolset", "DataflowAgent.DataflowAgentToolset.UpdateNode", S_epic_update_node, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset: z.union([z.string(), z.record(z.unknown())]).optional().describe("The target asset (must implement IDataflowInstanceInterface)"),
  bCommonOnly: z.boolean().optional().describe("When true, only return non-deprecated non-experimental nodes. Default is true."),
  bIncludeBlank: z.boolean().optional().describe("When true, include a \"Blank\" option (empty graph) at the start"),
  className: z.string().optional().describe("Asset class name with or without the \"U\"/\"A\" prefix (e.g. \"ChaosClothAsset\" or \"UChaosClothAsset\")"),
  color: z.record(z.unknown()).optional().describe("Background color of the comment box (defaults to White)"),
  comment: z.string().optional().describe("Text to display on the comment box"),
  commentId: z.string().optional().describe("The node GUID string returned by AddCommentBox"),
  fromNode: z.union([z.string(), z.record(z.unknown())]).optional().describe("The source EdNode"),
  fromPin: z.string().optional().describe("Name of the output pin on the source node"),
  graph: z.union([z.string(), z.record(z.unknown())]).optional().describe("The Dataflow asset to add the comment to"),
  jsonParams: z.string().optional().describe("Optional JSON object of property overrides (e.g., {\"Value\": 3.14})"),
  name: z.string().optional().describe("Unique name for the new variable"),
  node: z.union([z.string(), z.record(z.unknown())]).optional().describe("The EdNode to query"),
  nodeName: z.string().optional().describe("Unique name for the node within this graph"),
  nodes: z.array(z.unknown()).optional().describe("List of EdNodes to surround with the comment box"),
  path: z.string().optional().describe("Content folder path where the asset should be created. Defaults to \"/Game/Dataflow\""),
  templateId: z.string().optional().describe("Template identifier returned by ListDataflowTemplatesForAssetClass. Pass an empty string to assign a fresh empty graph."),
  toNode: z.union([z.string(), z.record(z.unknown())]).optional().describe("The destination EdNode"),
  toPin: z.string().optional().describe("Name of the input pin on the destination node"),
  type: z.string().optional().describe("Type string as described above"),
  typeName: z.string().optional().describe("The node type name (e.g., \"FAddFloatsDataflowNode\")"),
  value: z.string().optional().describe("Serialized string value to assign"),
  x: z.number().optional().describe("X position in the graph editor (default 0, typical node width ~200)"),
  y: z.number().optional().describe("Y position in the graph editor (default 0, typical node height ~100)"),
};
