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

const S_epic_add_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"comment":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["graph","nodes"]} as const;
const S_epic_add_comment_box__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"comment":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["graph","nodes","comment"]} as const;
const S_epic_add_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nativeNodeType":{"type":"string"},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"},"nodeComment":{"type":"string"},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["graph","nativeNodeType","nodeName"]} as const;
const S_epic_add_node__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nativeNodeType":{"type":"string"},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"},"nodeComment":{"type":"string"},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["graph","nativeNodeType","nodeName"]} as const;
const S_epic_add_subgraph_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"subGraphForNode":{"type":"object","properties":{"refPath":{}}},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"},"nodeComment":{"type":"string"},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["graph","subGraphForNode","nodeName"]} as const;
const S_epic_add_subgraph_node__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"subGraphForNode":{"type":"object","properties":{"refPath":{}}},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"},"nodeComment":{"type":"string"},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["graph","subGraphForNode","nodeName"]} as const;
const S_epic_collapse_nodes_to_subgraph = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"name":{"type":"string"},"assetPath":{"type":"string"}},"required":["graph","nodes","name"]} as const;
const S_epic_connect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["fromNode","fromPinLabel","toNode","toPinLabel"]} as const;
const S_epic_connect_node_pins__pcgeditor_subsystem = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["fromNode","fromPinLabel","toNode","toPinLabel"]} as const;
const S_epic_create_graph = {"properties":{"name":{"type":"string"},"path":{"type":"string"}},"required":["name"]} as const;
const S_epic_create_graph__pcgeditor_subsystem = {"properties":{"name":{"type":"string"},"path":{"type":"string"}},"required":["name"]} as const;
const S_epic_delete_embedded_subgraph = {"properties":{"embeddedGraph":{"type":"object","properties":{"refPath":{}}},"bAllowReferenced":{"type":"boolean"}},"required":["embeddedGraph"]} as const;
const S_epic_disconnect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["fromNode","fromPinLabel","toNode","toPinLabel"]} as const;
const S_epic_disconnect_node_pins__pcgeditor_subsystem = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["fromNode","fromPinLabel","toNode","toPinLabel"]} as const;
const S_epic_draw_spline = {"properties":{"actorLabel":{"type":"string"},"actorTag":{"type":"string"},"bRedraw":{"type":"boolean"},"bClosedSpline":{"type":"boolean"}},"required":["actorLabel","actorTag","bRedraw","bClosedSpline"]} as const;
const S_epic_draw_spline__pcgeditor_subsystem = {"properties":{"actorLabel":{"type":"string"},"actorTag":{"type":"string"},"bRedraw":{"type":"boolean"},"bClosedSpline":{"type":"boolean"}},"required":["actorLabel","actorTag","bRedraw","bClosedSpline"]} as const;
const S_epic_execute_graph_instance = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"executionSource":{"type":"object","properties":{"refPath":{}}}}} as const;
const S_epic_execute_graph_instance__pcgeditor_subsystem = {"properties":{"executionSource":{"type":"object","properties":{"refPath":{}}}},"required":["executionSource"]} as const;
const S_epic_execute_transient_graph_async = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"params":{"type":"object"}},"required":["graph","params"]} as const;
const S_epic_get_execution_sources = {"properties":{"executionSource":{"type":"object","properties":{"refPath":{}}}}} as const;
const S_epic_get_execution_sources__pcgtoolset = {"properties":{"executionSource":{"type":"object","properties":{"refPath":{}}}}} as const;
const S_epic_get_graph_description = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_description__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_instance_params = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}}},"required":["pCGVolume"]} as const;
const S_epic_get_graph_instance_params__pcgeditor_subsystem = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}}},"required":["pCGVolume"]} as const;
const S_epic_get_graph_schema = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_schema__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_structure = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_structure__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_native_node_schema = {"properties":{"nodeName":{"type":"string"}},"required":["nodeName"]} as const;
const S_epic_get_native_node_schema__pcgeditor_subsystem = {"properties":{"nodeName":{"type":"string"}},"required":["nodeName"]} as const;
const S_epic_get_node_data_view = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}},"pinLabel":{"type":"string"},"attributeName":{"type":"string"},"startIndex":{"type":"integer"},"endIndex":{"type":"integer"},"executionSource":{"type":"object","properties":{"refPath":{}}},"executionStack":{"type":"string"}},"required":["node"]} as const;
const S_epic_get_node_data_view__pcgeditor_subsystem = {"properties":{"executionSource":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}},"pinLabel":{"type":"string"},"attributeName":{"type":"string"},"startIndex":{"type":"integer"},"endIndex":{"type":"integer"},"executionStack":{"type":"string"}},"required":["executionSource","node"]} as const;
const S_epic_get_node_info = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_node_info__pcgeditor_subsystem = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_is_graph_automatic_regeneration_paused = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_list_available_subgraphs = {"properties":{}} as const;
const S_epic_list_available_subgraphs__pcgeditor_subsystem = {"properties":{"packagePaths":{"type":"array","items":{"type":"string"}}},"required":["packagePaths"]} as const;
const S_epic_list_embedded_subgraphs = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_list_graph_instances = {"properties":{}} as const;
const S_epic_list_graph_instances__pcgeditor_subsystem = {"properties":{}} as const;
const S_epic_list_native_nodes = {"properties":{"bCommonOnly":{"type":"boolean"}}} as const;
const S_epic_list_native_nodes__pcgeditor_subsystem = {"properties":{}} as const;
const S_epic_remove_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"}},"required":["graph","commentId"]} as const;
const S_epic_remove_comment_box__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"}},"required":["graph","commentId"]} as const;
const S_epic_remove_graph_params = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramNames":{"type":"array","items":{"type":"string"}}},"required":["graph","paramNames"]} as const;
const S_epic_remove_graph_params__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramNames":{"type":"array","items":{"type":"string"}}},"required":["graph","paramNames"]} as const;
const S_epic_remove_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["graph","node"]} as const;
const S_epic_remove_node__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["graph","node"]} as const;
const S_epic_rename_embedded_subgraph = {"properties":{"embeddedGraph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["embeddedGraph","name"]} as const;
const S_epic_reposition_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["node","xPositionIdx","yPositionIdx"]} as const;
const S_epic_reposition_node__pcgeditor_subsystem = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"xPositionIdx":{"type":"integer"},"yPositionIdx":{"type":"integer"}},"required":["node","xPositionIdx","yPositionIdx"]} as const;
const S_epic_reset_graph_instance_params = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"paramNames":{"type":"array","items":{"type":"string"}}},"required":["pCGVolume","paramNames"]} as const;
const S_epic_reset_graph_instance_params__pcgeditor_subsystem = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"paramNames":{"type":"array","items":{"type":"string"}}},"required":["pCGVolume","paramNames"]} as const;
const S_epic_run_pcginstant_graph = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"params":{"type":"object"}},"required":["graph","params"]} as const;
const S_epic_save_data_as_asset = {"properties":{"data":{"type":"object","properties":{"taggedData":{"type":"array","items":{"type":"object","properties":{"data":{"type":"object","properties":{"data":{"type":"object","properties":{"refPath":{}}}}},"tags":{"type":"array","items":{"type":"string"}},"pin":{"type":"string"},"bPinlessData":{"type":"boolean"},"bIsUsedMultipleTimes":{"type":"boolean"}}}},"bCancelExecutionOnEmpty":{"type":"boolean"}}},"parameters":{"type":"object","properties":{"bOpenSaveDialog":{"type":"boolean"},"assetName":{"type":"string"},"assetPath":{"type":"string"},"bSaveOnExportEnded":{"type":"boolean"}}}},"required":["data","parameters"]} as const;
const S_epic_set_graph_automatic_regeneration_paused = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"bPaused":{"type":"boolean"}},"required":["graph","bPaused"]} as const;
const S_epic_set_graph_description = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"description":{"type":"string"}},"required":["graph","description"]} as const;
const S_epic_set_graph_description__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"description":{"type":"string"}},"required":["graph","description"]} as const;
const S_epic_set_graph_instance_params = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"}},"required":["pCGVolume","jsonParams"]} as const;
const S_epic_set_graph_instance_params__pcgeditor_subsystem = {"properties":{"pCGVolume":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"}},"required":["pCGVolume","jsonParams"]} as const;
const S_epic_set_graph_params = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"params":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"type":{"type":"string","enum":["Float","Double","Integer32","Integer64","Vector2","Vector","Vector4","Quaternion","Transform","String","Boolean","Rotator","Name","SoftObjectPath","SoftClassPath"]},"description":{"type":"string"},"containerType":{"type":"string","enum":["None","Array","Set","Map"]},"defaultValueJson":{"type":"string"}}}}},"required":["graph","params"]} as const;
const S_epic_set_graph_params__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"params":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"type":{"type":"string","enum":["Float","Double","Integer32","Integer64","Vector2","Vector","Vector4","Quaternion","Transform","String","Boolean","Rotator","Name","SoftObjectPath","SoftClassPath"]},"description":{"type":"string"},"containerType":{"type":"string","enum":["None","Array","Set","Map"]},"defaultValueJson":{"type":"string"}}}}},"required":["graph","params"]} as const;
const S_epic_set_node_comment = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"nodeComment":{"type":"string"}},"required":["node","nodeComment"]} as const;
const S_epic_set_node_comment__pcgeditor_subsystem = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"nodeComment":{"type":"string"}},"required":["node","nodeComment"]} as const;
const S_epic_spawn_graph_instance = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"jsonParams":{"type":"string"}},"required":["graph","name","transform"]} as const;
const S_epic_spawn_graph_instance__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"jsonParams":{"type":"string"}},"required":["graph","name","transform"]} as const;
const S_epic_try_add_edge = {"properties":{"from":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"to":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["from","fromPinLabel","to","toPinLabel"]} as const;
const S_epic_update_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"comment":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["graph","commentId","nodes"]} as const;
const S_epic_update_comment_box__pcgeditor_subsystem = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"comment":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["graph","commentId","nodes"]} as const;
const S_epic_update_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"}},"required":["node"]} as const;
const S_epic_update_node__pcgeditor_subsystem = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"},"nodeTitle":{"type":"string"}},"required":["node"]} as const;
const S_epic_validate_edge = {"properties":{"from":{"type":"object","properties":{"refPath":{}}},"fromPinLabel":{"type":"string"},"to":{"type":"object","properties":{"refPath":{}}},"toPinLabel":{"type":"string"}},"required":["from","fromPinLabel","to","toPinLabel"]} as const;
const S_epic_wait_for_graph_instance = {"properties":{"executionSource":{"type":"object","properties":{"refPath":{}}}},"required":["executionSource"]} as const;
const S_epic_wait_for_graph_instance__pcgtoolset = {"properties":{"executionSource":{"type":"object","properties":{"refPath":{}}}},"required":["executionSource"]} as const;

/** 75 wrapped engine tools routed to the `pcg` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_comment_box: {
    epicSchema: S_epic_add_comment_box,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.AddCommentBox", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Adds a comment box around the given nodes. Params: graph, nodes, comment?, color?", "epic_call_tool"),
  },
  epic_add_comment_box__pcgeditor_subsystem: {
    epicSchema: S_epic_add_comment_box__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.AddCommentBox", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Adds a comment box around the given nodes. Available on UE 5.9; absent from UE 5.8. Params: graph, nodes, comment, color?", "epic_call_tool"),
  },
  epic_add_node: {
    epicSchema: S_epic_add_node,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.AddNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Adds a native node to the graph. Params: graph, nativeNodeType, nodeName, jsonParams?, nodeTitle?, nodeComment?, xPositionIdx?, yPositionIdx?", "epic_call_tool"),
  },
  epic_add_node__pcgeditor_subsystem: {
    epicSchema: S_epic_add_node__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.AddNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Adds a native node to the graph. Available on UE 5.9; absent from UE 5.8. Params: graph, nativeNodeType, nodeName, jsonParams?, nodeTitle?, nodeComment?, xPositionIdx?, yPositionIdx?", "epic_call_tool"),
  },
  epic_add_subgraph_node: {
    epicSchema: S_epic_add_subgraph_node,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.AddSubgraphNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Adds a subgraph node to the graph. Params: graph, subGraphForNode, nodeName, jsonParams?, nodeTitle?, nodeComment?, xPositionIdx?, yPositionIdx?", "epic_call_tool"),
  },
  epic_add_subgraph_node__pcgeditor_subsystem: {
    epicSchema: S_epic_add_subgraph_node__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.AddSubgraphNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Adds a subgraph node to the graph. Available on UE 5.9; absent from UE 5.8. Params: graph, subGraphForNode, nodeName, jsonParams?, nodeTitle?, nodeComment?, xPositionIdx?, yPositionIdx?", "epic_call_tool"),
  },
  epic_collapse_nodes_to_subgraph: {
    epicSchema: S_epic_collapse_nodes_to_subgraph,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.CollapseNodesToSubgraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Collapses at least two nodes into a subgraph. Empty AssetPath creates an embedded graph; otherwise creates a new unsaved asset named Name in the folder specified by AssetPath. Nodes must belong directly to Graph; input/output nodes are rejected. Comment boxes containing selected node positions and no unselected node positions move to the subgraph too. Available on UE 5.9; absent from UE 5.8. Params: graph, nodes, name, assetPath?", "epic_call_tool"),
  },
  epic_connect_node_pins: {
    epicSchema: S_epic_connect_node_pins,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.ConnectNodePins", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Add an edge between two nodes connected to the specified pins. Params: fromNode, fromPinLabel, toNode, toPinLabel", "epic_call_tool"),
  },
  epic_connect_node_pins__pcgeditor_subsystem: {
    epicSchema: S_epic_connect_node_pins__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ConnectNodePins", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Add an edge between two nodes connected to the specified pins. Available on UE 5.9; absent from UE 5.8. Params: fromNode, fromPinLabel, toNode, toPinLabel", "epic_call_tool"),
  },
  epic_create_graph: {
    epicSchema: S_epic_create_graph,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.CreateGraph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Creates a new saved PCG graph asset. Params: name, path?", "epic_call_tool"),
  },
  epic_create_graph__pcgeditor_subsystem: {
    epicSchema: S_epic_create_graph__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.CreateGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Creates a new unsaved PCG graph asset with a unique name. Available on UE 5.9; absent from UE 5.8. Params: name, path?", "epic_call_tool"),
  },
  epic_delete_embedded_subgraph: {
    epicSchema: S_epic_delete_embedded_subgraph,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.DeleteEmbeddedSubgraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Deletes an embedded graph. Referenced graphs raise a script error unless bAllowReferenced is true, which clears references and leaves missing-subgraph nodes. Available on UE 5.9; absent from UE 5.8. Params: embeddedGraph, bAllowReferenced?", "epic_call_tool"),
  },
  epic_disconnect_node_pins: {
    epicSchema: S_epic_disconnect_node_pins,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.DisconnectNodePins", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Removes the edge between two nodes connected to the specified pins. Params: fromNode, fromPinLabel, toNode, toPinLabel", "epic_call_tool"),
  },
  epic_disconnect_node_pins__pcgeditor_subsystem: {
    epicSchema: S_epic_disconnect_node_pins__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.DisconnectNodePins", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Removes the edge between two nodes connected to the specified pins. Available on UE 5.9; absent from UE 5.8. Params: fromNode, fromPinLabel, toNode, toPinLabel", "epic_call_tool"),
  },
  epic_draw_spline: {
    epicSchema: S_epic_draw_spline,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.DrawSpline", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Triggers the user to draw a spline in the viewport to be used later in the world building. Waits for the user to be done. Params: actorLabel, actorTag, bRedraw, bClosedSpline", "epic_call_tool"),
  },
  epic_draw_spline__pcgeditor_subsystem: {
    epicSchema: S_epic_draw_spline__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.DrawSpline", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Triggers the user to draw a spline in the viewport to be used later in the world building. Waits for the user to be done. Available on UE 5.9; absent from UE 5.8. Params: actorLabel, actorTag, bRedraw, bClosedSpline", "epic_call_tool"),
  },
  epic_execute_graph_instance: {
    epicSchema: S_epic_execute_graph_instance,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.ExecuteGraphInstance", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Schedules generation. Allow editor ticks until the returned result is complete, then inspect Error and Value (captured node diagnostics). Returns an error if the source is already generating. Params: pCGVolume?, executionSource?", "epic_call_tool"),
  },
  epic_execute_graph_instance__pcgeditor_subsystem: {
    epicSchema: S_epic_execute_graph_instance__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ExecuteGraphInstance", engines: ["5.9"] },
    ...bp("unknown", "[Epic PCGEditor.PCGEditorSubsystem] Schedules generation and returns immediately. Returns an error if the source is already generating. Allow editor ticks for generation to progress. Completion includes generation post-processing. Error reports invalid input, scheduling failure, cancellation or abandonment. Value contains captured node messages, which can include errors even when Error is empty. Available on UE 5.9; absent from UE 5.8. Params: executionSource", "epic_call_tool"),
  },
  epic_execute_transient_graph_async: {
    epicSchema: S_epic_execute_transient_graph_async,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ExecuteTransientGraphAsync", engines: ["5.9"] },
    ...bp("unknown", "[Epic PCGEditor.PCGEditorSubsystem] Executes a transient instance of the PCG graph asynchronously with the specified parameters. Await the returned result in Python to receive execution messages. Available on UE 5.9; absent from UE 5.8. Params: graph, params", "epic_call_tool"),
  },
  epic_get_execution_sources: {
    epicSchema: S_epic_get_execution_sources,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetExecutionSources", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns registered execution sources as JSON entries with executionSource, gridSize and gridCoordinates. Pass a PCG component or execution source to restrict the list to its world and graph instance; omit it to list all registered sources. Does not generate sources or load cells. Pass a returned source to GetNodeDataView. Available on UE 5.9; absent from UE 5.8. Params: executionSource?", "epic_call_tool"),
  },
  epic_get_execution_sources__pcgtoolset: {
    epicSchema: S_epic_get_execution_sources__pcgtoolset,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetExecutionSources", engines: ["5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns registered execution sources as JSON entries with executionSource, gridSize and gridCoordinates. Pass a PCG component or execution source to restrict the list to its world and graph instance; omit it to list all registered sources. Does not generate sources or load cells. Pass a returned source to GetNodeDataView. Available on UE 5.9; absent from UE 5.8. Params: executionSource?", "epic_call_tool"),
  },
  epic_get_graph_description: {
    epicSchema: S_epic_get_graph_description,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetGraphDescription", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns the description of a PCG graph. Params: graph", "epic_call_tool"),
  },
  epic_get_graph_description__pcgeditor_subsystem: {
    epicSchema: S_epic_get_graph_description__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetGraphDescription", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns the description of a PCG graph. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_get_graph_instance_params: {
    epicSchema: S_epic_get_graph_instance_params,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetGraphInstanceParams", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Gets the graph instance params of a specific actor, actor MUST have a graph instance Params: pCGVolume", "epic_call_tool"),
  },
  epic_get_graph_instance_params__pcgeditor_subsystem: {
    epicSchema: S_epic_get_graph_instance_params__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetGraphInstanceParams", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Gets the graph instance params of a specific actor, actor MUST have a graph instance Available on UE 5.9; absent from UE 5.8. Params: pCGVolume", "epic_call_tool"),
  },
  epic_get_graph_schema: {
    epicSchema: S_epic_get_graph_schema,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetGraphSchema", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns the schema for a PCG Graph's graph parameters Params: graph", "epic_call_tool"),
  },
  epic_get_graph_schema__pcgeditor_subsystem: {
    epicSchema: S_epic_get_graph_schema__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetGraphSchema", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns the schema for a PCG Graph's graph parameters Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_get_graph_structure: {
    epicSchema: S_epic_get_graph_structure,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetGraphStructure", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns the complete structure of a PCG graph including all nodes, connections, exposed parameters, and comment boxes. Params: graph", "epic_call_tool"),
  },
  epic_get_graph_structure__pcgeditor_subsystem: {
    epicSchema: S_epic_get_graph_structure__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetGraphStructure", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns the graph's name, description, nodes, and connections. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_get_native_node_schema: {
    epicSchema: S_epic_get_native_node_schema,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetNativeNodeSchema", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns the schema for a PCG node type including input/output pins, parameters, and their types. Params: nodeName", "epic_call_tool"),
  },
  epic_get_native_node_schema__pcgeditor_subsystem: {
    epicSchema: S_epic_get_native_node_schema__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetNativeNodeSchema", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns the schema for a PCG node type including input/output pins, parameters, and their types. Available on UE 5.9; absent from UE 5.8. Params: nodeName", "epic_call_tool"),
  },
  epic_get_node_data_view: {
    epicSchema: S_epic_get_node_data_view,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetNodeDataView", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns a JSON Data View of a specific node's output data from the supplied execution source. Enables source and node inspection and requests generation when inspection is first enabled. Runtime sources refresh through the runtime scheduler. Allow editor ticks and query again while pending; polling does not repeatedly regenerate. GPU output is read back before serialization. Params: pCGVolume?, node, pinLabel?, attributeName?, startIndex?, endIndex?, executionSource?, executionStack?", "epic_call_tool"),
  },
  epic_get_node_data_view__pcgeditor_subsystem: {
    epicSchema: S_epic_get_node_data_view__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetNodeDataView", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Returns a JSON Data View of a specific node's output data from the supplied execution source. Enables source and node inspection and requests generation when inspection is first enabled. Runtime sources refresh through the runtime scheduler. Allow editor ticks and query again while pending; polling does not repeatedly regenerate. GPU output is read back before serialization. Available on UE 5.9; absent from UE 5.8. Params: executionSource, node, pinLabel?, attributeName?, startIndex?, endIndex?, executionStack?", "epic_call_tool"),
  },
  epic_get_node_info: {
    epicSchema: S_epic_get_node_info,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.GetNodeInfo", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns node details including name, position, and all parameter values. Params: node", "epic_call_tool"),
  },
  epic_get_node_info__pcgeditor_subsystem: {
    epicSchema: S_epic_get_node_info__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.GetNodeInfo", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns node details including name, position, and all parameter values. Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_is_graph_automatic_regeneration_paused: {
    epicSchema: S_epic_is_graph_automatic_regeneration_paused,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.IsGraphAutomaticRegenerationPaused", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns whether automatic regeneration is paused for the graph by the user or scripting. Excludes temporary notification suppression. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_list_available_subgraphs: {
    epicSchema: S_epic_list_available_subgraphs,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.ListAvailableSubgraphs", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Lists the PCG graphs that can be used with the Subgraph native node. Only these graphs should be used with the Subgraph native node. Params: none", "epic_call_tool"),
  },
  epic_list_available_subgraphs__pcgeditor_subsystem: {
    epicSchema: S_epic_list_available_subgraphs__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ListAvailableSubgraphs", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Lists the PCG graphs that can be used with the Subgraph native node. Only these graphs should be used with the Subgraph native node. Available on UE 5.9; absent from UE 5.8. Params: packagePaths", "epic_call_tool"),
  },
  epic_list_embedded_subgraphs: {
    epicSchema: S_epic_list_embedded_subgraphs,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ListEmbeddedSubgraphs", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Lists the embedded graphs owned by Graph, including unused ones. An embedded Graph lists its owner's graphs. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_list_graph_instances: {
    epicSchema: S_epic_list_graph_instances,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.ListGraphInstances", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Gets all actors with a PCG graph instance in the scene. Params: none", "epic_call_tool"),
  },
  epic_list_graph_instances__pcgeditor_subsystem: {
    epicSchema: S_epic_list_graph_instances__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ListGraphInstances", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Gets all actors with a PCG graph instance in the scene. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_native_nodes: {
    epicSchema: S_epic_list_native_nodes,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.ListNativeNodes", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns a list of available native PCG node type names. Params: bCommonOnly?", "epic_call_tool"),
  },
  epic_list_native_nodes__pcgeditor_subsystem: {
    epicSchema: S_epic_list_native_nodes__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ListNativeNodes", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns a list of available native PCG node type names. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_remove_comment_box: {
    epicSchema: S_epic_remove_comment_box,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.RemoveCommentBox", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Removes a comment box from the graph. Does not affect the nodes it contains. Params: graph, commentId", "epic_call_tool"),
  },
  epic_remove_comment_box__pcgeditor_subsystem: {
    epicSchema: S_epic_remove_comment_box__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.RemoveCommentBox", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Removes a comment box from the graph. Does not affect the nodes it contains. Available on UE 5.9; absent from UE 5.8. Params: graph, commentId", "epic_call_tool"),
  },
  epic_remove_graph_params: {
    epicSchema: S_epic_remove_graph_params,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.RemoveGraphParams", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Removes graph parameters to a specific PCG graph, such that they are not overridable anymore. Params: graph, paramNames", "epic_call_tool"),
  },
  epic_remove_graph_params__pcgeditor_subsystem: {
    epicSchema: S_epic_remove_graph_params__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.RemoveGraphParams", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Removes graph parameters to a specific PCG graph, such that they are not overridable anymore. Available on UE 5.9; absent from UE 5.8. Params: graph, paramNames", "epic_call_tool"),
  },
  epic_remove_node: {
    epicSchema: S_epic_remove_node,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.RemoveNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Removes the node from the graph, will also remove edges connected to the node. Params: graph, node", "epic_call_tool"),
  },
  epic_remove_node__pcgeditor_subsystem: {
    epicSchema: S_epic_remove_node__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.RemoveNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Removes the node from the graph, will also remove edges connected to the node. Available on UE 5.9; absent from UE 5.8. Params: graph, node", "epic_call_tool"),
  },
  epic_rename_embedded_subgraph: {
    epicSchema: S_epic_rename_embedded_subgraph,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.RenameEmbeddedSubgraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Changes an embedded graph's display name. Name must be nonempty and unique among its siblings, ignoring case. Available on UE 5.9; absent from UE 5.8. Params: embeddedGraph, name", "epic_call_tool"),
  },
  epic_reposition_node: {
    epicSchema: S_epic_reposition_node,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.RepositionNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Change the position of node. Params: node, xPositionIdx, yPositionIdx", "epic_call_tool"),
  },
  epic_reposition_node__pcgeditor_subsystem: {
    epicSchema: S_epic_reposition_node__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.RepositionNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Change the position of node. Available on UE 5.9; absent from UE 5.8. Params: node, xPositionIdx, yPositionIdx", "epic_call_tool"),
  },
  epic_reset_graph_instance_params: {
    epicSchema: S_epic_reset_graph_instance_params,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.ResetGraphInstanceParams", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Resets the given graph instance params back to the graph's default values. Actor MUST have a graph instance. Params: pCGVolume, paramNames", "epic_call_tool"),
  },
  epic_reset_graph_instance_params__pcgeditor_subsystem: {
    epicSchema: S_epic_reset_graph_instance_params__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ResetGraphInstanceParams", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Resets the given graph instance params back to the graph's default values. Actor MUST have a graph instance. Available on UE 5.9; absent from UE 5.8. Params: pCGVolume, paramNames", "epic_call_tool"),
  },
  epic_run_pcginstant_graph: {
    epicSchema: S_epic_run_pcginstant_graph,
    epicTool: { toolset: "PCGToolset.PCGSpatialToolset", name: "PCGToolset.PCGSpatialToolset.RunPCGInstantGraph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGSpatialToolset] Runs an instant PCG graph with the specified parameters. Returns an asynchronous result containing execution messages. Params: graph, params", "epic_call_tool"),
  },
  epic_save_data_as_asset: {
    epicSchema: S_epic_save_data_as_asset,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SaveDataAsAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Exports a data collection to a PCG Data Asset using the attribute inspector's save operation. Game thread only. Headless, commandlet and unattended runs suppress the save dialog; provide Parameters.AssetPath and Parameters.AssetName. Otherwise, Parameters.bOpenSaveDialog controls whether the destination is chosen interactively. Parameters.bSaveOnExportEnded controls saving to disk, including unchanged assets. Returns the asset, or null for empty data, cancellation, export failure or a failed requested save. Save diagnostics are reported separately. Available on UE 5.9; absent from UE 5.8. Params: data, parameters", "epic_call_tool"),
  },
  epic_set_graph_automatic_regeneration_paused: {
    epicSchema: S_epic_set_graph_automatic_regeneration_paused,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SetGraphAutomaticRegenerationPaused", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Controls automatic graph regeneration without requiring an open graph editor. Shares the toolbar's transient pause state. Ordinary change notifications are deferred until resume; forced notifications and explicit execution can bypass the pause. Restore the previous state after edits, including on failure. Closing the graph editor also resumes regeneration. Available on UE 5.9; absent from UE 5.8. Params: graph, bPaused", "epic_call_tool"),
  },
  epic_set_graph_description: {
    epicSchema: S_epic_set_graph_description,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.SetGraphDescription", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Set the description of a PCGGraph Params: graph, description", "epic_call_tool"),
  },
  epic_set_graph_description__pcgeditor_subsystem: {
    epicSchema: S_epic_set_graph_description__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SetGraphDescription", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Set the description of a PCGGraph Available on UE 5.9; absent from UE 5.8. Params: graph, description", "epic_call_tool"),
  },
  epic_set_graph_instance_params: {
    epicSchema: S_epic_set_graph_instance_params,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.SetGraphInstanceParams", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Sets the graph instance params of a specific actor, actor MUST have a graph instance Params: pCGVolume, jsonParams", "epic_call_tool"),
  },
  epic_set_graph_instance_params__pcgeditor_subsystem: {
    epicSchema: S_epic_set_graph_instance_params__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SetGraphInstanceParams", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Sets the graph instance params of a specific actor, actor MUST have a graph instance Available on UE 5.9; absent from UE 5.8. Params: pCGVolume, jsonParams", "epic_call_tool"),
  },
  epic_set_graph_params: {
    epicSchema: S_epic_set_graph_params,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.SetGraphParams", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Adds one or more graph user parameters to a specific PCG graph, such that they will be overridable in per graph instance. Params: graph, params", "epic_call_tool"),
  },
  epic_set_graph_params__pcgeditor_subsystem: {
    epicSchema: S_epic_set_graph_params__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SetGraphParams", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Adds one or more graph user parameters to a specific PCG graph, such that they will be overridable in per graph instance. Available on UE 5.9; absent from UE 5.8. Params: graph, params", "epic_call_tool"),
  },
  epic_set_node_comment: {
    epicSchema: S_epic_set_node_comment,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.SetNodeComment", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Change the comment on the specified node. Params: node, nodeComment", "epic_call_tool"),
  },
  epic_set_node_comment__pcgeditor_subsystem: {
    epicSchema: S_epic_set_node_comment__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SetNodeComment", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Change the comment on the specified node. Available on UE 5.9; absent from UE 5.8. Params: node, nodeComment", "epic_call_tool"),
  },
  epic_spawn_graph_instance: {
    epicSchema: S_epic_spawn_graph_instance,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.SpawnGraphInstance", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Spawns a PCG Volume with associated Graph Instance into the scene, optionally with Graph Param overrides. Params: graph, name, transform, jsonParams?", "epic_call_tool"),
  },
  epic_spawn_graph_instance__pcgeditor_subsystem: {
    epicSchema: S_epic_spawn_graph_instance__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.SpawnGraphInstance", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Spawns a PCG Volume with associated Graph Instance into the scene, optionally with Graph Param overrides. Available on UE 5.9; absent from UE 5.8. Params: graph, name, transform, jsonParams?", "epic_call_tool"),
  },
  epic_try_add_edge: {
    epicSchema: S_epic_try_add_edge,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.TryAddEdge", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Adds a direct edge and returns true, including for an existing connection. Rejection raises a script error and preserves existing edges. Available on UE 5.9; absent from UE 5.8. Params: from, fromPinLabel, to, toPinLabel", "epic_call_tool"),
  },
  epic_update_comment_box: {
    epicSchema: S_epic_update_comment_box,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.UpdateCommentBox", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Updates an existing comment box with new nodes and value. Params: graph, commentId, nodes, comment?, color?", "epic_call_tool"),
  },
  epic_update_comment_box__pcgeditor_subsystem: {
    epicSchema: S_epic_update_comment_box__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.UpdateCommentBox", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Updates an existing comment box with new nodes and value. Available on UE 5.9; absent from UE 5.8. Params: graph, commentId, nodes, comment?, color?", "epic_call_tool"),
  },
  epic_update_node: {
    epicSchema: S_epic_update_node,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.UpdateNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PCGToolset.PCGToolset] Updates a node by changing its params and/or title. Params: node, jsonParams?, nodeTitle?", "epic_call_tool"),
  },
  epic_update_node__pcgeditor_subsystem: {
    epicSchema: S_epic_update_node__pcgeditor_subsystem,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.UpdateNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic PCGEditor.PCGEditorSubsystem] Updates a node by changing its params and/or title. Available on UE 5.9; absent from UE 5.8. Params: node, jsonParams?, nodeTitle?", "epic_call_tool"),
  },
  epic_validate_edge: {
    epicSchema: S_epic_validate_edge,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.ValidateEdge", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns whether the pins can connect directly without modifying the graph. Invalid nodes or pin labels raise a script error. Available on UE 5.9; absent from UE 5.8. Params: from, fromPinLabel, to, toPinLabel", "epic_call_tool"),
  },
  epic_wait_for_graph_instance: {
    epicSchema: S_epic_wait_for_graph_instance,
    epicTool: { toolset: "PCGEditor.PCGEditorSubsystem", name: "PCGEditor.PCGEditorSubsystem.WaitForGraphInstance", engines: ["5.9"] },
    ...bp("read", "[Epic PCGEditor.PCGEditorSubsystem] Returns an async result for the source's current generation, without starting or restarting it. Returns an error if no generation is active. Await the result in Python, or allow editor ticks until it completes, then inspect Error and Value as with ExecuteGraphInstance. Available on UE 5.9; absent from UE 5.8. Params: executionSource", "epic_call_tool"),
  },
  epic_wait_for_graph_instance__pcgtoolset: {
    epicSchema: S_epic_wait_for_graph_instance__pcgtoolset,
    epicTool: { toolset: "PCGToolset.PCGToolset", name: "PCGToolset.PCGToolset.WaitForGraphInstance", engines: ["5.9"] },
    ...bp("read", "[Epic PCGToolset.PCGToolset] Returns an async result for the source's current generation, without starting or restarting it. Returns an error if no generation is active. Inspect Error and Value as with ExecuteGraphInstance once the result completes. Available on UE 5.9; absent from UE 5.8. Params: executionSource", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("The label of the actor created or if bRedraw, the label of the actor to redraw the spline"),
  actorTag: z.string().optional().describe("Tag assigned to the actor"),
  assetPath: z.string().optional(),
  attributeName: z.string().optional().describe("Filter to a single attribute/property (e.g. \"$Position\", \"$Density\", \"MyCustomAttr\"). Empty = all attributes."),
  bAllowReferenced: z.boolean().optional(),
  bClosedSpline: z.boolean().optional().describe("true: closed spline (region) false: path."),
  bCommonOnly: z.boolean().optional().describe("Whether to only return the commonly used Native nodes. Only use bCommonOnly = false if the user specifically asks for it."),
  bPaused: z.boolean().optional(),
  bRedraw: z.boolean().optional().describe("false: creates a new actor with the spline. true: find an actor with the label, and replaces its spline."),
  color: z.record(z.string(), z.unknown()).optional().describe("The color of the comment box. Defaults to White"),
  comment: z.string().optional().describe("The comment to put on the comment box."),
  commentId: z.string().optional().describe("The unique id of the comment to remove."),
  data: z.record(z.string(), z.unknown()).optional(),
  description: z.string().optional().describe("New description of graph"),
  embeddedGraph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  endIndex: z.number().optional().describe("Element range end, exclusive. -1 means all elements (Python slice convention). Default -1."),
  executionSource: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Object implementing PCGGraphExecutionSource, such as a PCG component."),
  executionStack: z.string().optional().describe("Optional stack path from a selection_required response. Selects a subgraph invocation or loop iteration."),
  from: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  fromNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The source node of the edge to add."),
  fromPinLabel: z.string().optional().describe("The label of the source pin of the source node."),
  graph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The PCG graph to execute"),
  jsonParams: z.string().optional().describe("The Json string representing a dictionary of the params to set on the node. Optional. Default is empty. Only non-default params need be included."),
  name: z.string().optional().describe("Name for the new PCG graph asset (e.g., \"PCG_ForestScatter\")"),
  nativeNodeType: z.string().optional().describe("The native type of the added node."),
  node: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The node whose output to inspect."),
  nodeComment: z.string().optional().describe("The comment attached to the node, if needed. Default is empty."),
  nodeName: z.string().optional().describe("The name of the added node. (Must be unique identifier in the graph)"),
  nodes: z.array(z.unknown()).optional().describe("The list of nodes to include in the comment box."),
  nodeTitle: z.string().optional().describe("The Display Title of the node. Optional. Default is empty."),
  packagePaths: z.array(z.unknown()).optional(),
  parameters: z.record(z.string(), z.unknown()).optional(),
  paramNames: z.array(z.unknown()).optional().describe("An array of existing param names that will be removed."),
  params: z.unknown().optional().describe("Key-value parameters to pass to the graph"),
  path: z.string().optional().describe("Content folder path where the asset should be created. Defaults to \"/Game/PCG\""),
  pCGVolume: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The PCG Volume whose graph instance to execute."),
  pinLabel: z.string().optional().describe("Output pin label to read. Defaults to \"Out\"."),
  startIndex: z.number().optional().describe("Element range start, inclusive, 0-based. Default 0."),
  subGraphForNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The subgraph to use in the added node."),
  to: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  toNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The destination node of the edge to add."),
  toPinLabel: z.string().optional().describe("The label of the destination pin of the destination node."),
  transform: z.record(z.string(), z.unknown()).optional().describe("The transform to use for the new PCGVolume actor. Place at the origin unless there is a reason not to and use default scale3D of {\"x\": 25,\"y\": 25,\"z\": 10}"),
  xPositionIdx: z.number().optional().describe("The X coordinate of the position of the node in the editor. Optional. Default is 0. Typical node size X is 200"),
  yPositionIdx: z.number().optional().describe("The Y coordinate of the position of the node in the editor. Optional. Default is 0. Typical node size Y is 100"),
};
