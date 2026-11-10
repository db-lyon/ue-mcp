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

const S_epic_add_comment = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"comment":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"size":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["dataflow","graphPath","comment","position","size","color"]} as const;
const S_epic_add_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"comment":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["graph","nodes"]} as const;
const S_epic_add_graph = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphName":{"type":"string"},"bIsForEach":{"type":"boolean"}},"required":["dataflow","graphName","bIsForEach"]} as const;
const S_epic_add_graph_binding = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"direction":{"type":"string"},"bindingName":{"type":"string"},"dataType":{"type":"string"}},"required":["dataflow","graphPath","direction","bindingName","dataType"]} as const;
const S_epic_add_link = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"outputPinPath":{"type":"string"},"inputPinPath":{"type":"string"}},"required":["dataflow","outputPinPath","inputPinPath"]} as const;
const S_epic_add_link_with_new_pin = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"sourcePinPath":{"type":"string"},"targetNodePath":{"type":"string"}},"required":["dataflow","sourcePinPath","targetNodePath"]} as const;
const S_epic_add_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"typeName":{"type":"string"},"nodeName":{"type":"string"},"jsonParams":{"type":"string"},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["graph","typeName","nodeName"]} as const;
const S_epic_add_node__dataflow_scripting_subsystem = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"nodeType":{"type":"string"},"nodeName":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["dataflow","graphPath","nodeType","nodeName","position"]} as const;
const S_epic_add_node_pins = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"count":{"type":"integer"}},"required":["dataflow","nodePath","count"]} as const;
const S_epic_add_variable = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"type":{"type":"string"}},"required":["graph","name","type"]} as const;
const S_epic_add_variable__dataflow_scripting_subsystem = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"dataType":{"type":"string"},"defaultValue":{"type":"string"}},"required":["dataflow","variableName","dataType","defaultValue"]} as const;
const S_epic_assign_dataflow_template = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"templateId":{"type":"string"}},"required":["asset","templateId"]} as const;
const S_epic_clear_preview_variable = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["asset","variableName"]} as const;
const S_epic_clear_variable_override = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["asset","variableName"]} as const;
const S_epic_connect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPin":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPin":{"type":"string"}},"required":["fromNode","fromPin","toNode","toPin"]} as const;
const S_epic_create_asset_with_dataflow = {"properties":{"assetClass":{"type":"object","properties":{"refPath":{}}},"assetPath":{"type":"string"},"templatePath":{"type":"string"}},"required":["assetClass","assetPath","templatePath"]} as const;
const S_epic_create_dataflow = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_create_dataflow_compatible_asset = {"properties":{"className":{"type":"string"},"name":{"type":"string"},"path":{"type":"string"}},"required":["className","name"]} as const;
const S_epic_create_dataflow_compatible_asset_from_template = {"properties":{"className":{"type":"string"},"name":{"type":"string"},"path":{"type":"string"},"templateId":{"type":"string"}},"required":["className","name","path","templateId"]} as const;
const S_epic_create_graph = {"properties":{"name":{"type":"string"},"path":{"type":"string"}},"required":["name","path"]} as const;
const S_epic_disconnect_node_pins = {"properties":{"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPin":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPin":{"type":"string"}},"required":["fromNode","fromPin","toNode","toPin"]} as const;
const S_epic_execute_node_function = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"functionName":{"type":"string"}},"required":["node","functionName"]} as const;
const S_epic_execute_node_function__dataflow_scripting_subsystem = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"functionName":{"type":"string"}},"required":["dataflow","nodePath","functionName"]} as const;
const S_epic_get_dataflow_from_asset = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}}},"required":["asset"]} as const;
const S_epic_get_dataflow_info = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}}},"required":["dataflow"]} as const;
const S_epic_get_dataflow_instance_info = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}}},"required":["asset"]} as const;
const S_epic_get_graph_info = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"}},"required":["dataflow","graphPath"]} as const;
const S_epic_get_graph_structure = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_node_info = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_node_info__dataflow_scripting_subsystem = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"}},"required":["dataflow","nodePath"]} as const;
const S_epic_get_node_type_info = {"properties":{"nodeType":{"type":"string"}},"required":["nodeType"]} as const;
const S_epic_get_node_type_schema = {"properties":{"typeName":{"type":"string"}},"required":["typeName"]} as const;
const S_epic_list_dataflow_asset_classes = {"properties":{}} as const;
const S_epic_list_dataflow_compatible_asset_types = {"properties":{}} as const;
const S_epic_list_dataflow_templates = {"properties":{"assetClass":{"type":"object","properties":{"refPath":{}}}},"required":["assetClass"]} as const;
const S_epic_list_dataflow_templates_for_asset_class = {"properties":{"className":{"type":"string"},"bIncludeBlank":{"type":"boolean"}},"required":["className"]} as const;
const S_epic_list_node_types = {"properties":{"bCommonOnly":{"type":"boolean"}}} as const;
const S_epic_list_node_types__dataflow_scripting_subsystem = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"filter":{"type":"string"}},"required":["dataflow","filter"]} as const;
const S_epic_list_variables = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_remove_all_links = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"}},"required":["dataflow","pinPath"]} as const;
const S_epic_remove_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentId":{"type":"string"}},"required":["graph","commentId"]} as const;
const S_epic_remove_graph = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"}},"required":["dataflow","graphPath"]} as const;
const S_epic_remove_graph_binding = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"bindingName":{"type":"string"}},"required":["dataflow","graphPath","bindingName"]} as const;
const S_epic_remove_link = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"outputPinPath":{"type":"string"},"inputPinPath":{"type":"string"}},"required":["dataflow","outputPinPath","inputPinPath"]} as const;
const S_epic_remove_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["graph","node"]} as const;
const S_epic_remove_node_pins = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"count":{"type":"integer"}},"required":["dataflow","nodePath","count"]} as const;
const S_epic_remove_nodes = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePaths":{"type":"array","items":{"type":"string"}}},"required":["dataflow","nodePaths"]} as const;
const S_epic_remove_variable = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["graph","name"]} as const;
const S_epic_remove_variable__dataflow_scripting_subsystem = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["dataflow","variableName"]} as const;
const S_epic_rename_graph = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"newName":{"type":"string"}},"required":["dataflow","graphPath","newName"]} as const;
const S_epic_rename_graph_binding = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"bindingName":{"type":"string"},"newName":{"type":"string"}},"required":["dataflow","graphPath","bindingName","newName"]} as const;
const S_epic_rename_node = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"newName":{"type":"string"}},"required":["dataflow","nodePath","newName"]} as const;
const S_epic_rename_variable = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"newName":{"type":"string"}},"required":["dataflow","variableName","newName"]} as const;
const S_epic_reposition_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["node","x","y"]} as const;
const S_epic_set_comment = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"comment":{"type":"string"}},"required":["dataflow","nodePath","comment"]} as const;
const S_epic_set_comment_style = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"fontSize":{"type":"integer"},"bShowBubble":{"type":"boolean"},"bColorBubble":{"type":"boolean"},"bMovesNodes":{"type":"boolean"}},"required":["dataflow","nodePath","color","fontSize","bShowBubble","bColorBubble","bMovesNodes"]} as const;
const S_epic_set_dataflow_from_template = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"templatePath":{"type":"string"}},"required":["asset","templatePath"]} as const;
const S_epic_set_dataflow_terminal = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"nodeName":{"type":"string"}},"required":["asset","nodeName"]} as const;
const S_epic_set_graph_binding_data_type = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"bindingName":{"type":"string"},"dataType":{"type":"string"}},"required":["dataflow","graphPath","bindingName","dataType"]} as const;
const S_epic_set_graph_for_each = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"bIsForEach":{"type":"boolean"}},"required":["dataflow","graphPath","bIsForEach"]} as const;
const S_epic_set_node_position = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["dataflow","nodePath","position"]} as const;
const S_epic_set_node_properties = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"properties":{"type":"object"}},"required":["dataflow","nodePath","properties"]} as const;
const S_epic_set_pin_type = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"},"dataType":{"type":"string"}},"required":["dataflow","pinPath","dataType"]} as const;
const S_epic_set_preview_blueprint = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"blueprintClass":{"type":"object","properties":{"refPath":{}}}},"required":["dataflow","blueprintClass"]} as const;
const S_epic_set_preview_variable = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"value":{"type":"string"}},"required":["asset","variableName","value"]} as const;
const S_epic_set_variable = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"string"}},"required":["graph","name","value"]} as const;
const S_epic_set_variable_data_type = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"dataType":{"type":"string"}},"required":["dataflow","variableName","dataType"]} as const;
const S_epic_set_variable_default_value = {"properties":{"dataflow":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"defaultValue":{"type":"string"}},"required":["dataflow","variableName","defaultValue"]} as const;
const S_epic_set_variable_override = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"value":{"type":"string"}},"required":["asset","variableName","value"]} as const;
const S_epic_update_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"jsonParams":{"type":"string"}},"required":["node","jsonParams"]} as const;

/** 70 wrapped engine tools routed to the `dataflow` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_comment: {
    epicSchema: S_epic_add_comment,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddComment", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] AddComment Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, comment, position, size, color", "epic_call_tool"),
  },
  epic_add_comment_box: {
    epicSchema: S_epic_add_comment_box,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.AddCommentBox", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Adds a comment box around the given nodes. Params: graph, nodes, comment?, color?", "epic_call_tool"),
  },
  epic_add_graph: {
    epicSchema: S_epic_add_graph,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Adds a sub-graph. Add its \"FDataflowSubGraphInputNode\" and \"FDataflowSubGraphOutputNode\" nodes with AddNode, its signature with AddGraphBinding, and run it with a \"SubGraph/<Name>\" node. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphName, bIsForEach", "epic_call_tool"),
  },
  epic_add_graph_binding: {
    epicSchema: S_epic_add_graph_binding,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddGraphBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Adds a binding to the signature of a sub-graph: an output pin of its input node (Direction \"Input\") or an input pin of its output node (Direction \"Output\"). The nodes running the sub-graph get the matching pin. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, direction, bindingName, dataType", "epic_call_tool"),
  },
  epic_add_link: {
    epicSchema: S_epic_add_link,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddLink", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Links an output pin to an input pin, replacing the input pin's existing link if any, as the graph editor does: a pin of any type takes the type of the pin it is linked to, and a conversion node is added when the types need one. Available on UE 5.9; absent from UE 5.8. Params: dataflow, outputPinPath, inputPinPath", "epic_call_tool"),
  },
  epic_add_link_with_new_pin: {
    epicSchema: S_epic_add_link_with_new_pin,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddLinkWithNewPin", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Adds a pin to a node that supports it (e.g. the input or output node of a sub-graph) for the type of SourcePinPath, and links the two, as dropping a link on the node in the graph editor does. Available on UE 5.9; absent from UE 5.8. Params: dataflow, sourcePinPath, targetNodePath", "epic_call_tool"),
  },
  epic_add_node: {
    epicSchema: S_epic_add_node,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.AddNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Adds a node of the given type to the Dataflow graph. Params: graph, typeName, nodeName, jsonParams?, x?, y?", "epic_call_tool"),
  },
  epic_add_node__dataflow_scripting_subsystem: {
    epicSchema: S_epic_add_node__dataflow_scripting_subsystem,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Adds a node. Use AddComment for comments. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, nodeType, nodeName, position", "epic_call_tool"),
  },
  epic_add_node_pins: {
    epicSchema: S_epic_add_node_pins,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddNodePins", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Adds pins to a node whose FDataflowNodeInfo::bCanAddPins is true, as its \"Add Pin\" menu does. Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, count", "epic_call_tool"),
  },
  epic_add_variable: {
    epicSchema: S_epic_add_variable,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.AddVariable", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Adds a new variable to the Dataflow graph. Supported type strings: Primitives : \"Bool\", \"Int32\", \"Int64\", \"Float\", \"Double\", \"Name\", \"String\" Structs : UScriptStruct name with or without the \"F\" prefix e.g. \"Vector\", \"FVector\", \"Transform\", \"FTransform\", \"Rotator\", \"LinearColor\" Objects : \"Object:<ClassName>\" where ClassName is with or without the \"U\"/\"A\" prefix e.g. \"Object:StaticMesh\", \"Object:USkeletalMesh\" Dataflow types, as pins show them: e.g. \"float\", \"FVector\", \"TObjectPtr<UStaticMesh>\", \"TArray<int32>\" Params: graph, name, type", "epic_call_tool"),
  },
  epic_add_variable__dataflow_scripting_subsystem: {
    epicSchema: S_epic_add_variable__dataflow_scripting_subsystem,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.AddVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] AddVariable Available on UE 5.9; absent from UE 5.8. Params: dataflow, variableName, dataType, defaultValue", "epic_call_tool"),
  },
  epic_assign_dataflow_template: {
    epicSchema: S_epic_assign_dataflow_template,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.AssignDataflowTemplate", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Assigns a Dataflow template to an existing Dataflow-compatible asset by duplicating the template graph and embedding it. Replaces any existing embedded graph. Params: asset, templateId", "epic_call_tool"),
  },
  epic_clear_preview_variable: {
    epicSchema: S_epic_clear_preview_variable,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.ClearPreviewVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Makes the asset use the preview Blueprint's default value of a variable again. Available on UE 5.9; absent from UE 5.8. Params: asset, variableName", "epic_call_tool"),
  },
  epic_clear_variable_override: {
    epicSchema: S_epic_clear_variable_override,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.ClearVariableOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Makes the asset use the graph's value of a variable again. Available on UE 5.9; absent from UE 5.8. Params: asset, variableName", "epic_call_tool"),
  },
  epic_connect_node_pins: {
    epicSchema: S_epic_connect_node_pins,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.ConnectNodePins", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Connects an output pin of one node to an input pin of another. Params: fromNode, fromPin, toNode, toPin", "epic_call_tool"),
  },
  epic_create_asset_with_dataflow: {
    epicSchema: S_epic_create_asset_with_dataflow,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.CreateAssetWithDataflow", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Creates an asset generated by an embedded Dataflow graph (e.g. a cloth asset or a geometry collection). Get the graph with GetDataflowFromAsset. Available on UE 5.9; absent from UE 5.8. Params: assetClass, assetPath, templatePath", "epic_call_tool"),
  },
  epic_create_dataflow: {
    epicSchema: S_epic_create_dataflow,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.CreateDataflow", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Creates a Dataflow asset with an empty main graph. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_create_dataflow_compatible_asset: {
    epicSchema: S_epic_create_dataflow_compatible_asset,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.CreateDataflowCompatibleAsset", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Creates a new Dataflow-compatible asset (e.g. ChaosClothAsset, GeometryCollection, FleshAsset, GroomAsset) with an empty embedded Dataflow graph. Params: className, name, path?", "epic_call_tool"),
  },
  epic_create_dataflow_compatible_asset_from_template: {
    epicSchema: S_epic_create_dataflow_compatible_asset_from_template,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.CreateDataflowCompatibleAssetFromTemplate", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Creates a new Dataflow-compatible asset and initialises its embedded Dataflow graph from a registered template in one step. Params: className, name, path, templateId", "epic_call_tool"),
  },
  epic_create_graph: {
    epicSchema: S_epic_create_graph,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.CreateGraph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Creates a new saved Dataflow graph asset. Params: name, path", "epic_call_tool"),
  },
  epic_disconnect_node_pins: {
    epicSchema: S_epic_disconnect_node_pins,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.DisconnectNodePins", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Removes the connection between two node pins. Params: fromNode, fromPin, toNode, toPin", "epic_call_tool"),
  },
  epic_execute_node_function: {
    epicSchema: S_epic_execute_node_function,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.ExecuteNodeFunction", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Runs a node function, as if its details panel button was pressed. Undoable. On a standalone graph, the function cannot see the per-instance overrides of the assets using it. Available on UE 5.9; absent from UE 5.8. Params: node, functionName", "epic_call_tool"),
  },
  epic_execute_node_function__dataflow_scripting_subsystem: {
    epicSchema: S_epic_execute_node_function__dataflow_scripting_subsystem,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.ExecuteNodeFunction", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Runs a function of a node, as pressing its button in the details panel does (e.g. \"Import\"). On a standalone graph, the function cannot see the per-asset overrides of the assets using it. Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, functionName", "epic_call_tool"),
  },
  epic_get_dataflow_from_asset: {
    epicSchema: S_epic_get_dataflow_from_asset,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.GetDataflowFromAsset", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Returns the Dataflow graph an asset is generated from, embedded or referenced (e.g. the graph of a cloth asset). Available on UE 5.9; absent from UE 5.8. Params: asset", "epic_call_tool"),
  },
  epic_get_dataflow_info: {
    epicSchema: S_epic_get_dataflow_info,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.GetDataflowInfo", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Returns the graphs and variables of a Dataflow asset. Available on UE 5.9; absent from UE 5.8. Params: dataflow", "epic_call_tool"),
  },
  epic_get_dataflow_instance_info: {
    epicSchema: S_epic_get_dataflow_instance_info,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.GetDataflowInstanceInfo", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Returns what an asset generated by a Dataflow graph keeps besides the graph: its terminal node and the values it overrides. Available on UE 5.9; absent from UE 5.8. Params: asset", "epic_call_tool"),
  },
  epic_get_graph_info: {
    epicSchema: S_epic_get_graph_info,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.GetGraphInfo", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Graphs Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath", "epic_call_tool"),
  },
  epic_get_graph_structure: {
    epicSchema: S_epic_get_graph_structure,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.GetGraphStructure", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns the complete structure of a Dataflow graph including all nodes and connections. Params: graph", "epic_call_tool"),
  },
  epic_get_node_info: {
    epicSchema: S_epic_get_node_info,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.GetNodeInfo", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns information about a node as a JSON object (name, type, position, pins). Params: node", "epic_call_tool"),
  },
  epic_get_node_info__dataflow_scripting_subsystem: {
    epicSchema: S_epic_get_node_info__dataflow_scripting_subsystem,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.GetNodeInfo", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Nodes Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath", "epic_call_tool"),
  },
  epic_get_node_type_info: {
    epicSchema: S_epic_get_node_type_info,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.GetNodeTypeInfo", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Returns a registered node type with the pins and editable properties of a new node of that type. Available on UE 5.9; absent from UE 5.8. Params: nodeType", "epic_call_tool"),
  },
  epic_get_node_type_schema: {
    epicSchema: S_epic_get_node_type_schema,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.GetNodeTypeSchema", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns the schema for a Dataflow node type including its input/output pins and editable UPROPERTY parameters. Params: typeName", "epic_call_tool"),
  },
  epic_list_dataflow_asset_classes: {
    epicSchema: S_epic_list_dataflow_asset_classes,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.ListDataflowAssetClasses", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Lists the asset classes whose assets can have a Dataflow graph: those generated by one (e.g. a cloth asset), and those it can be attached to (e.g. a skeletal mesh). Whichever it is, the other functions treat their assets alike. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_dataflow_compatible_asset_types: {
    epicSchema: S_epic_list_dataflow_compatible_asset_types,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.ListDataflowCompatibleAssetTypes", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns a JSON list of every UClass that can host an embedded Dataflow graph (i.e. implements IDataflowInstanceInterface). Each entry has \"className\", \"displayName\", and \"modulePath\" fields. Use the \"className\" value as input to CreateDataflowCompatibleAsset or ListDataflowTemplatesForAssetClass. Params: none", "epic_call_tool"),
  },
  epic_list_dataflow_templates: {
    epicSchema: S_epic_list_dataflow_templates,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.ListDataflowTemplates", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Lists the templates registered for an asset class, including those registered for its parent classes. Available on UE 5.9; absent from UE 5.8. Params: assetClass", "epic_call_tool"),
  },
  epic_list_dataflow_templates_for_asset_class: {
    epicSchema: S_epic_list_dataflow_templates_for_asset_class,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.ListDataflowTemplatesForAssetClass", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns a JSON list of Dataflow templates registered for the given asset class. Templates registered for parent classes are included (class hierarchy walk). Params: className, bIncludeBlank?", "epic_call_tool"),
  },
  epic_list_node_types: {
    epicSchema: S_epic_list_node_types,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.ListNodeTypes", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns a JSON list of all registered Dataflow node types. Params: bCommonOnly?", "epic_call_tool"),
  },
  epic_list_node_types__dataflow_scripting_subsystem: {
    epicSchema: S_epic_list_node_types__dataflow_scripting_subsystem,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.ListNodeTypes", engines: ["5.9"] },
    ...bp("read", "[Epic DataflowEditor.DataflowScriptingSubsystem] Lists the node types that can be added, including the nodes running the sub-graphs and reading the variables of Dataflow. Available on UE 5.9; absent from UE 5.8. Params: dataflow, filter", "epic_call_tool"),
  },
  epic_list_variables: {
    epicSchema: S_epic_list_variables,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.ListVariables", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataflowAgent.DataflowAgentToolset] Returns all variables defined on the Dataflow graph as a JSON array. Each entry contains \"name\", \"type\", and \"value\" fields. Params: graph", "epic_call_tool"),
  },
  epic_remove_all_links: {
    epicSchema: S_epic_remove_all_links,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveAllLinks", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] RemoveAllLinks Available on UE 5.9; absent from UE 5.8. Params: dataflow, pinPath", "epic_call_tool"),
  },
  epic_remove_comment_box: {
    epicSchema: S_epic_remove_comment_box,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.RemoveCommentBox", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Removes a comment box node from the graph. Params: graph, commentId", "epic_call_tool"),
  },
  epic_remove_graph: {
    epicSchema: S_epic_remove_graph,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Removes a sub-graph and its nodes. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath", "epic_call_tool"),
  },
  epic_remove_graph_binding: {
    epicSchema: S_epic_remove_graph_binding,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveGraphBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Removes a binding from the signature of a sub-graph, with its pins and links. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, bindingName", "epic_call_tool"),
  },
  epic_remove_link: {
    epicSchema: S_epic_remove_link,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveLink", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Remove Link Available on UE 5.9; absent from UE 5.8. Params: dataflow, outputPinPath, inputPinPath", "epic_call_tool"),
  },
  epic_remove_node: {
    epicSchema: S_epic_remove_node,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.RemoveNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Removes a node and all its connections from the Dataflow graph. Params: graph, node", "epic_call_tool"),
  },
  epic_remove_node_pins: {
    epicSchema: S_epic_remove_node_pins,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveNodePins", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Removes the last added pins of a node whose FDataflowNodeInfo::bCanRemovePins is true, with their links. Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, count", "epic_call_tool"),
  },
  epic_remove_nodes: {
    epicSchema: S_epic_remove_nodes,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Removes nodes and comments, and their links, as one undo step. Nothing is removed if any path is invalid. Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePaths", "epic_call_tool"),
  },
  epic_remove_variable: {
    epicSchema: S_epic_remove_variable,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.RemoveVariable", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Removes a variable from the Dataflow graph. Params: graph, name", "epic_call_tool"),
  },
  epic_remove_variable__dataflow_scripting_subsystem: {
    epicSchema: S_epic_remove_variable__dataflow_scripting_subsystem,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RemoveVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Remove Variable Available on UE 5.9; absent from UE 5.8. Params: dataflow, variableName", "epic_call_tool"),
  },
  epic_rename_graph: {
    epicSchema: S_epic_rename_graph,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RenameGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Renames a sub-graph, and the nodes running it. The main graph has no name. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, newName", "epic_call_tool"),
  },
  epic_rename_graph_binding: {
    epicSchema: S_epic_rename_graph_binding,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RenameGraphBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Renames a binding of the signature of a sub-graph, as its details panel does: its pins keep their links, also on the nodes running the sub-graph. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, bindingName, newName", "epic_call_tool"),
  },
  epic_rename_node: {
    epicSchema: S_epic_rename_node,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RenameNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Renames a node, as editing its title in the graph editor does. What refers to the node by name keeps the old name (e.g. the terminal of an asset, see SetDataflowTerminal). Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, newName", "epic_call_tool"),
  },
  epic_rename_variable: {
    epicSchema: S_epic_rename_variable,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.RenameVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Renames a variable, and updates the nodes reading it. Available on UE 5.9; absent from UE 5.8. Params: dataflow, variableName, newName", "epic_call_tool"),
  },
  epic_reposition_node: {
    epicSchema: S_epic_reposition_node,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.RepositionNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Moves a node to a new position in the graph editor. Params: node, x, y", "epic_call_tool"),
  },
  epic_set_comment: {
    epicSchema: S_epic_set_comment,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetComment", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Set Comment Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, comment", "epic_call_tool"),
  },
  epic_set_comment_style: {
    epicSchema: S_epic_set_comment_style,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetCommentStyle", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Sets how a comment shows, as its details panel does. Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, color, fontSize, bShowBubble, bColorBubble, bMovesNodes", "epic_call_tool"),
  },
  epic_set_dataflow_from_template: {
    epicSchema: S_epic_set_dataflow_from_template,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetDataflowFromTemplate", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Gives an asset a new embedded Dataflow graph, copied from a template, replacing the graph it had. An asset of a class the graph is attached to (e.g. a skeletal mesh) gets the attachment holding it. Available on UE 5.9; absent from UE 5.8. Params: asset, templatePath", "epic_call_tool"),
  },
  epic_set_dataflow_terminal: {
    epicSchema: S_epic_set_dataflow_terminal,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetDataflowTerminal", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] SetDataflowTerminal Available on UE 5.9; absent from UE 5.8. Params: asset, nodeName", "epic_call_tool"),
  },
  epic_set_graph_binding_data_type: {
    epicSchema: S_epic_set_graph_binding_data_type,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetGraphBindingDataType", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Changes the type of a binding of the signature of a sub-graph, as its details panel does: the links of its pins that the new type cannot keep are removed. Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, bindingName, dataType", "epic_call_tool"),
  },
  epic_set_graph_for_each: {
    epicSchema: S_epic_set_graph_for_each,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetGraphForEach", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] SetGraphForEach Available on UE 5.9; absent from UE 5.8. Params: dataflow, graphPath, bIsForEach", "epic_call_tool"),
  },
  epic_set_node_position: {
    epicSchema: S_epic_set_node_position,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetNodePosition", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] SetNodePosition Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, position", "epic_call_tool"),
  },
  epic_set_node_properties: {
    epicSchema: S_epic_set_node_properties,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetNodeProperties", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Sets properties of a node, as listed in FDataflowNodeInfo::Properties, as one undo step. The node updates as when the details panel edits them, which can change its pins. Nothing is set if any property cannot be. An instanced object of another asset (e.g. read from another graph) is duplicated into Dataflow. Available on UE 5.9; absent from UE 5.8. Params: dataflow, nodePath, properties", "epic_call_tool"),
  },
  epic_set_pin_type: {
    epicSchema: S_epic_set_pin_type,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetPinType", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Sets the type of an unlinked output pin of any type, as its \"Convert to\" menu does (e.g. the Result of a math node, float by default). Available on UE 5.9; absent from UE 5.8. Params: dataflow, pinPath, dataType", "epic_call_tool"),
  },
  epic_set_preview_blueprint: {
    epicSchema: S_epic_set_preview_blueprint,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetPreviewBlueprint", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Sets the Blueprint actor class the Dataflow editor previews the graph with (e.g. a character wearing a cloth asset). The assets using the graph set the values of its variables with SetPreviewVariable. Available on UE 5.9; absent from UE 5.8. Params: dataflow, blueprintClass", "epic_call_tool"),
  },
  epic_set_preview_variable: {
    epicSchema: S_epic_set_preview_variable,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetPreviewVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Sets, for an asset, a variable of the Blueprint its graph is previewed with (e.g. \"SkeletalMesh\", the mesh a cloth asset is previewed on). Available on UE 5.9; absent from UE 5.8. Params: asset, variableName, value", "epic_call_tool"),
  },
  epic_set_variable: {
    epicSchema: S_epic_set_variable,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.SetVariable", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Sets the value of an existing variable using its serialized string representation. The format depends on the variable's type (e.g., \"3.14\" for float, \"true\" for bool, \"42\" for int, \"MyName\" for FName). Params: graph, name, value", "epic_call_tool"),
  },
  epic_set_variable_data_type: {
    epicSchema: S_epic_set_variable_data_type,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetVariableDataType", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Set Variable Data Type Available on UE 5.9; absent from UE 5.8. Params: dataflow, variableName, dataType", "epic_call_tool"),
  },
  epic_set_variable_default_value: {
    epicSchema: S_epic_set_variable_default_value,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetVariableDefaultValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Set Variable Default Value Available on UE 5.9; absent from UE 5.8. Params: dataflow, variableName, defaultValue", "epic_call_tool"),
  },
  epic_set_variable_override: {
    epicSchema: S_epic_set_variable_override,
    epicTool: { toolset: "DataflowEditor.DataflowScriptingSubsystem", name: "DataflowEditor.DataflowScriptingSubsystem.SetVariableOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic DataflowEditor.DataflowScriptingSubsystem] Gives a graph variable a value of its own for an asset generated by the graph. Available on UE 5.9; absent from UE 5.8. Params: asset, variableName, value", "epic_call_tool"),
  },
  epic_update_node: {
    epicSchema: S_epic_update_node,
    epicTool: { toolset: "DataflowAgent.DataflowAgentToolset", name: "DataflowAgent.DataflowAgentToolset.UpdateNode", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic DataflowAgent.DataflowAgentToolset] Updates an existing node's editable properties via JSON. Params: node, jsonParams", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The target asset (must implement IDataflowInstanceInterface)"),
  assetClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A class from ListDataflowAssetClasses whose bCanCreate is true."),
  assetPath: z.string().optional().describe("Path of the new asset. Virtual paths are accepted."),
  bColorBubble: z.boolean().optional().describe("If true, the bubble takes the comment color."),
  bCommonOnly: z.boolean().optional().describe("When true, only return non-deprecated non-experimental nodes. Default is true."),
  bIncludeBlank: z.boolean().optional().describe("When true, include a \"Blank\" option (empty graph) at the start"),
  bindingName: z.string().optional().describe("Made unique if needed."),
  bIsForEach: z.boolean().optional().describe("If true, the sub-graph runs once per element of its array input."),
  blueprintClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A Blueprint generated actor class. Null for none."),
  bMovesNodes: z.boolean().optional().describe("If true, moving the comment moves the nodes it contains."),
  bShowBubble: z.boolean().optional().describe("If true, the comment also shows in a bubble when the graph is zoomed out."),
  className: z.string().optional().describe("Asset class name with or without the \"U\"/\"A\" prefix (e.g. \"ChaosClothAsset\" or \"UChaosClothAsset\")"),
  color: z.record(z.string(), z.unknown()).optional().describe("Background color of the comment box (defaults to White)"),
  comment: z.string().optional().describe("Text to display on the comment box"),
  commentId: z.string().optional().describe("The node GUID string returned by AddCommentBox"),
  count: z.number().optional(),
  dataflow: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Null to only list the registered node types."),
  dataType: z.string().optional().describe("One of the types the pin supports, e.g. \"int32\"."),
  defaultValue: z.string().optional().describe("Empty for the type's default."),
  direction: z.string().optional().describe("\"Input\" or \"Output\"."),
  filter: z.string().optional().describe("Case-insensitive text matched against the type, display name and category. Empty lists everything."),
  fontSize: z.number().optional().describe("Font size of the comment text, 1 to 1000 (18 for a new comment)."),
  fromNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The source EdNode"),
  fromPin: z.string().optional().describe("Name of the output pin on the source node"),
  functionName: z.string().optional().describe("Function property name (e.g., \"Import\"). An unknown name raises with the valid names, a disabled function raises."),
  graph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Dataflow asset to add the comment to"),
  graphName: z.string().optional().describe("Made unique if needed."),
  graphPath: z.string().optional(),
  inputPinPath: z.string().optional(),
  jsonParams: z.string().optional().describe("Optional JSON object of property overrides (e.g., {\"Value\": 3.14}). A string value is read as Unreal export text, e.g. the values GetNodeInfo returns. A proper…"),
  name: z.string().optional().describe("Name for the new variable, made unique if needed"),
  newName: z.string().optional().describe("Unique across the graph and its sub-graphs."),
  node: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The EdNode to query"),
  nodeName: z.string().optional().describe("Unique name for the node within this graph"),
  nodePath: z.string().optional(),
  nodePaths: z.array(z.unknown()).optional(),
  nodes: z.array(z.unknown()).optional().describe("List of EdNodes to surround with the comment box"),
  nodeType: z.string().optional().describe("Node type from ListNodeTypes, e.g. \"FAddFloatsDataflowNode\", \"SubGraph/MySubGraph\" or \"Variable/MyVariable\"."),
  outputPinPath: z.string().optional(),
  path: z.string().optional().describe("Content folder path where the asset should be created. Defaults to \"/Game/Dataflow\""),
  pinPath: z.string().optional(),
  position: z.record(z.string(), z.unknown()).optional().describe("Top-left corner of the comment box, in graph units."),
  properties: z.record(z.string(), z.unknown()).optional().describe("Property name to value, in Unreal text export format, e.g. the Value of the same property on another node."),
  size: z.record(z.string(), z.unknown()).optional().describe("Size of the comment box, in graph units."),
  sourcePinPath: z.string().optional(),
  targetNodePath: z.string().optional(),
  templateId: z.string().optional().describe("Template identifier returned by ListDataflowTemplatesForAssetClass. Pass an empty string to assign a fresh empty graph."),
  templatePath: z.string().optional().describe("Path of a Dataflow asset to start the graph from, empty for an empty graph."),
  toNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The destination EdNode"),
  toPin: z.string().optional().describe("Name of the input pin on the destination node"),
  type: z.string().optional().describe("Type string as described above"),
  typeName: z.string().optional().describe("The node type name (e.g., \"FAddFloatsDataflowNode\")"),
  value: z.string().optional().describe("Serialized string value to assign"),
  variableName: z.string().optional().describe("Made unique if needed."),
  x: z.number().optional().describe("X position in the graph editor (default 0, typical node width ~200)"),
  y: z.number().optional().describe("Y position in the graph editor (default 0, typical node height ~100)"),
};
