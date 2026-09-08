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
const S_epic_add_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nativeNodeType":{"type":"string"},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"},"nodeComment":{"type":"string"},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["graph","nativeNodeType","nodeName","jsonParams","nodeTitle","nodeComment"]} as const;
const S_epic_add_subgraph_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"subGraphForNode":{"type":"object","properties":{"refPath":{}}},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"},"nodeComment":{"type":"string"},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["graph","subGraphForNode","nodeName","jsonParams","nodeTitle","nodeComment"]} as const;
const S_epic_connect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["fromNode","fromPinLabel","toNode","toPinLabel"]} as const;
const S_epic_create_graph = {"properties":{"name":{"type":"string"},"path":{"type":"string"}},"required":["name"]} as const;
const S_epic_disconnect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["fromNode","fromPinLabel","toNode","toPinLabel"]} as const;
const S_epic_draw_spline = {"properties":{"actorLabel":{"type":"string"},"actorTag":{"type":"string"},"bRedraw":{"type":"boolean"},"bClosedSpline":{"type":"boolean"}},"required":["actorLabel","actorTag","bRedraw","bClosedSpline"]} as const;
const S_epic_execute_graph_instance = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}}},"required":["pCGVolume"]} as const;
const S_epic_get_graph_description = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_instance_params = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}}},"required":["pCGVolume"]} as const;
const S_epic_get_graph_schema = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_structure = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_native_node_schema = {"properties":{"nodeName":{"type":"string"}},"required":["nodeName"]} as const;
const S_epic_get_node_data_view = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}},"pinLabel":{"type":"string"},"attributeName":{"type":"string"},"startIndex":{"type":"integer"},"endIndex":{"type":"integer"}},"required":["pCGVolume","node","attributeName"]} as const;
const S_epic_get_node_info = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_list_available_subgraphs = {"properties":{}} as const;
const S_epic_list_graph_instances = {"properties":{}} as const;
const S_epic_list_native_nodes = {"properties":{"bCommonOnly":{"type":"boolean"}}} as const;
const S_epic_remove_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"}},"required":["graph","commentId"]} as const;
const S_epic_remove_graph_params = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramNames":{"type":"array"}},"required":["graph","paramNames"]} as const;
const S_epic_remove_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["graph","node"]} as const;
const S_epic_reposition_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["node","xPositionIdx","yPositionIdx"]} as const;
const S_epic_reset_graph_instance_params = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"paramNames":{"type":"array"}},"required":["pCGVolume","paramNames"]} as const;
const S_epic_run_pcginstant_graph = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"params":{"type":"object"}},"required":["graph","params"]} as const;
const S_epic_set_graph_description = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"description":{"type":"string"}},"required":["graph","description"]} as const;
const S_epic_set_graph_instance_params = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"}},"required":["pCGVolume","jsonParams"]} as const;
const S_epic_set_graph_params = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"params":{"type":"array"}},"required":["graph","params"]} as const;
const S_epic_set_node_comment = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"nodeComment":{"type":"string"}},"required":["node","nodeComment"]} as const;
const S_epic_spawn_graph_instance = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"transform":{"type":"object"},"jsonParams":{"type":"string"}},"required":["graph","name","transform","jsonParams"]} as const;
const S_epic_update_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"},"nodes":{"type":"array"},"comment":{"type":"string"},"color":{"type":"object"}},"required":["graph","commentId","nodes"]} as const;
const S_epic_update_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"}},"required":["node","jsonParams","nodeTitle"]} as const;

/** 31 wrapped engine tools routed to the `pcg` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_comment_box: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Adds a comment box around the given nodes. Params: graph, nodes, comment?, color?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.AddCommentBox", S_epic_add_comment_box, p),
  ),
  epic_add_node: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Adds a native node to the graph. Params: graph, nativeNodeType, nodeName, jsonParams, nodeTitle, nodeComment, xPositionIdx?, yPositionIdx?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.AddNode", S_epic_add_node, p),
  ),
  epic_add_subgraph_node: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Adds a subgraph node to the graph. Params: graph, subGraphForNode, nodeName, jsonParams, nodeTitle, nodeComment, xPositionIdx?, yPositionIdx?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.AddSubgraphNode", S_epic_add_subgraph_node, p),
  ),
  epic_connect_node_pins: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Add an edge between two nodes connected to the specified pins. Params: fromNode, fromPinLabel, toNode, toPinLabel",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.ConnectNodePins", S_epic_connect_node_pins, p),
  ),
  epic_create_graph: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Creates a new saved PCG graph asset. Params: name, path?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.CreateGraph", S_epic_create_graph, p),
  ),
  epic_disconnect_node_pins: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Removes the edge between two nodes connected to the specified pins. Params: fromNode, fromPinLabel, toNode, toPinLabel",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.DisconnectNodePins", S_epic_disconnect_node_pins, p),
  ),
  epic_draw_spline: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Triggers the user to draw a spline in the viewport to be used later in the world building. Waits for the user to be done. Params: actorLabel, actorTag, bRedraw, bClosedSpline",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.DrawSpline", S_epic_draw_spline, p),
  ),
  epic_execute_graph_instance: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Executes the graph instance and returns any issues encountered during execution. Params: pCGVolume",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.ExecuteGraphInstance", S_epic_execute_graph_instance, p),
  ),
  epic_get_graph_description: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns the description of a PCG graph. Params: graph",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetGraphDescription", S_epic_get_graph_description, p),
  ),
  epic_get_graph_instance_params: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Gets the graph instance params of a specific actor, actor MUST have a graph instance Params: pCGVolume",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetGraphInstanceParams", S_epic_get_graph_instance_params, p),
  ),
  epic_get_graph_schema: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns the schema for a PCG Graph's graph parameters Params: graph",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetGraphSchema", S_epic_get_graph_schema, p),
  ),
  epic_get_graph_structure: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns the complete structure of a PCG graph including all nodes, connections, exposed parameters, and comment boxes. Params: graph",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetGraphStructure", S_epic_get_graph_structure, p),
  ),
  epic_get_native_node_schema: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns the schema for a PCG node type including input/output pins, parameters, and their types. Params: nodeName",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetNativeNodeSchema", S_epic_get_native_node_schema, p),
  ),
  epic_get_node_data_view: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns a JSON Data View of a specific node's output data from the last graph execution. On first call, enables inspection so future ExecuteGraphInstance calls store per-node data. If no inspection data exists, returns an error prompting re-execution. IMPORTANT: Inspection state is shared at the graph asset level. If multiple actors use the same graph, you MUST call this tool (and ExecuteGraphInstance) on only one actor at a time. Wait for each call to fully complete before calling on the next actor. Concurrent calls on actors sharing the same graph will cause a freeze. Params: pCGVolume, node, pinLabel?, attributeName, startIndex?, endIndex?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetNodeDataView", S_epic_get_node_data_view, p),
  ),
  epic_get_node_info: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns node details including name, position, and all parameter values. Params: node",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.GetNodeInfo", S_epic_get_node_info, p),
  ),
  epic_list_available_subgraphs: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Lists the PCG graphs that can be used with the Subgraph native node. Only these graphs should be used with the Subgraph native node. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.ListAvailableSubgraphs", S_epic_list_available_subgraphs, p),
  ),
  epic_list_graph_instances: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Gets all actors with a PCG graph instance in the scene. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.ListGraphInstances", S_epic_list_graph_instances, p),
  ),
  epic_list_native_nodes: bp(
    "read",
    "[Epic PCGToolset.PCGToolset] Returns a list of available native PCG node type names. Params: bCommonOnly?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.ListNativeNodes", S_epic_list_native_nodes, p),
  ),
  epic_remove_comment_box: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Removes a comment box from the graph. Does not affect the nodes it contains. Params: graph, commentId",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.RemoveCommentBox", S_epic_remove_comment_box, p),
  ),
  epic_remove_graph_params: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Removes graph parameters to a specific PCG graph, such that they are not overridable anymore. Params: graph, paramNames",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.RemoveGraphParams", S_epic_remove_graph_params, p),
  ),
  epic_remove_node: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Removes the node from the graph, will also remove edges connected to the node. Params: graph, node",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.RemoveNode", S_epic_remove_node, p),
  ),
  epic_reposition_node: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Change the position of node. Params: node, xPositionIdx, yPositionIdx",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.RepositionNode", S_epic_reposition_node, p),
  ),
  epic_reset_graph_instance_params: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Resets the given graph instance params back to the graph's default values. Actor MUST have a graph instance. Params: pCGVolume, paramNames",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.ResetGraphInstanceParams", S_epic_reset_graph_instance_params, p),
  ),
  epic_run_pcginstant_graph: bp(
    "mutate",
    "[Epic PCGToolset.PCGSpatialToolset] Runs an instant PCG graph with the specified parameters in fire-and-forget mode (Should be called directly: Not callable in a python context execution context) Params: graph, params",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGSpatialToolset", "PCGToolset.PCGSpatialToolset.RunPCGInstantGraph", S_epic_run_pcginstant_graph, p),
  ),
  epic_set_graph_description: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Set the description of a PCGGraph Params: graph, description",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.SetGraphDescription", S_epic_set_graph_description, p),
  ),
  epic_set_graph_instance_params: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Sets the graph instance params of a specific actor, actor MUST have a graph instance Params: pCGVolume, jsonParams",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.SetGraphInstanceParams", S_epic_set_graph_instance_params, p),
  ),
  epic_set_graph_params: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Adds one or more graph user parameters to a specific PCG graph, such that they will be overridable in per graph instance. Params: graph, params",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.SetGraphParams", S_epic_set_graph_params, p),
  ),
  epic_set_node_comment: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Change the comment on the specified node. Params: node, nodeComment",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.SetNodeComment", S_epic_set_node_comment, p),
  ),
  epic_spawn_graph_instance: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Spawns a PCG Volume with associated Graph Instance into the scene, optionally with Graph Param overrides. Params: graph, name, transform, jsonParams",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.SpawnGraphInstance", S_epic_spawn_graph_instance, p),
  ),
  epic_update_comment_box: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Updates an existing comment box with new nodes and value. Params: graph, commentId, nodes, comment?, color?",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.UpdateCommentBox", S_epic_update_comment_box, p),
  ),
  epic_update_node: bp(
    "mutate",
    "[Epic PCGToolset.PCGToolset] Updates a node by changing its params and/or title. Params: node, jsonParams, nodeTitle",
    "epic_call_tool",
    (p) => epicToolCall("PCGToolset.PCGToolset", "PCGToolset.PCGToolset.UpdateNode", S_epic_update_node, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("The label of the actor created or if bRedraw, the label of the actor to redraw the spline"),
  actorTag: z.string().optional().describe("Tag assigned to the actor"),
  attributeName: z.string().optional().describe("Filter to a single attribute/property (e.g. \"$Position\", \"$Density\", \"MyCustomAttr\"). Empty = all attributes."),
  bClosedSpline: z.boolean().optional().describe("true: closed spline (region) false: path."),
  bCommonOnly: z.boolean().optional().describe("Whether to only return the commonly used Native nodes. Only use bCommonOnly = false if the user specifically asks for it."),
  bRedraw: z.boolean().optional().describe("false: creates a new actor with the spline. true: find an actor with the label, and replaces its spline."),
  color: z.record(z.unknown()).optional().describe("The color of the comment box. Defaults to White"),
  comment: z.string().optional().describe("The comment to put on the comment box."),
  commentId: z.string().optional().describe("The unique id of the comment to remove."),
  description: z.string().optional().describe("New description of graph"),
  endIndex: z.number().optional().describe("Element range end, exclusive. -1 means all elements (Python slice convention). Default -1."),
  fromNode: z.union([z.string(), z.record(z.unknown())]).optional().describe("The source node of the edge to add."),
  fromPinLabel: z.string().optional().describe("The label of the source pin of the source node."),
  graph: z.union([z.string(), z.record(z.unknown())]).optional().describe("The PCG graph to execute"),
  jsonParams: z.string().optional().describe("The Json string representing a dictionary of the params to set on the node. Optional. Default is empty. Only non-default params need be included."),
  name: z.string().optional().describe("Name for the new PCG graph asset (e.g., \"PCG_ForestScatter\")"),
  nativeNodeType: z.string().optional().describe("The native type of the added node."),
  node: z.union([z.string(), z.record(z.unknown())]).optional().describe("The node whose output to inspect."),
  nodeComment: z.string().optional().describe("The comment attached to the node, if needed. Default is empty."),
  nodeName: z.string().optional().describe("The name of the added node. (Must be unique identifier in the graph)"),
  nodes: z.array(z.unknown()).optional().describe("The list of nodes to include in the comment box."),
  nodeTitle: z.string().optional().describe("The Display Title of the node. Optional. Default is empty."),
  paramNames: z.array(z.unknown()).optional().describe("An array of existing param names that will be removed."),
  params: z.unknown().optional().describe("Key-value parameters to pass to the graph"),
  path: z.string().optional().describe("Content folder path where the asset should be created. Defaults to \"/Game/PCG\""),
  pCGVolume: z.union([z.string(), z.record(z.unknown())]).optional().describe("The PCG Volume whose graph instance to execute."),
  pinLabel: z.string().optional().describe("Output pin label to read. Defaults to \"Out\"."),
  startIndex: z.number().optional().describe("Element range start, inclusive, 0-based. Default 0."),
  subGraphForNode: z.union([z.string(), z.record(z.unknown())]).optional().describe("The subgraph to use in the added node."),
  toNode: z.union([z.string(), z.record(z.unknown())]).optional().describe("The destination node of the edge to add."),
  toPinLabel: z.string().optional().describe("The label of the destination pin of the destination node."),
  transform: z.record(z.unknown()).optional().describe("The transform to use for the new PCGVolume actor. Place at the origin unless there is a reason not to and use default scale3D of {\"x\": 25,\"y\": 25,\"z\": 10}"),
  xPositionIdx: z.number().optional().describe("The X coordinate of the position of the node in the editor. Optional. Default is 0. Typical node size X is 200"),
  yPositionIdx: z.number().optional().describe("The Y coordinate of the position of the node in the editor. Optional. Default is 0. Typical node size Y is 100"),
};
