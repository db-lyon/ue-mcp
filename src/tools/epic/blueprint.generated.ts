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

const S_epic_add_comment_box = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"commentText":{"type":"string"},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"rect":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"bIsValid":{"type":"boolean"}}},"padding":{"type":"integer"}},"required":["graph","commentText"]} as const;
const S_epic_add_component_bound_event = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"event_name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["component","event_name","graph"]} as const;
const S_epic_add_component_bound_event__1 = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"eventName":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["component","eventName","graph"]} as const;
const S_epic_add_event = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"event_name":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"}}}},"required":["blueprint","event_name"]} as const;
const S_epic_add_event__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"eventName":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"}}}},"required":["blueprint","eventName"]} as const;
const S_epic_add_event_dispatcher = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["blueprint","name"]} as const;
const S_epic_add_event_dispatcher__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["blueprint","name"]} as const;
const S_epic_add_function_graph = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["blueprint","graph_name"]} as const;
const S_epic_add_function_graph__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graphName":{"type":"string"},"flags":{"type":"object","properties":{"access":{"type":"string","enum":["Public","Protected","Private"]},"bIsPure":{"type":"boolean"},"bIsConst":{"type":"boolean"},"bIsExec":{"type":"boolean"},"bIsThreadSafe":{"type":"boolean"},"bIsCallInEditor":{"type":"boolean"},"bIsUnsafeDuringActorConstruction":{"type":"boolean"},"bIsDeprecated":{"type":"boolean"},"deprecationMessage":{"type":"string"},"category":{"type":"string"},"tooltip":{"type":"string"},"keywords":{"type":"string"},"compactNodeTitle":{"type":"string"}}}},"required":["blueprint","graphName"]} as const;
const S_epic_add_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"param_type":{"type":"string"},"input_param":{"type":"boolean"},"container_type":{"type":"string","enum":["ARRAY","SET","MAP"]}},"required":["graph","param_name","param_type","input_param"]} as const;
const S_epic_add_function_param__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramName":{"type":"string"},"paramType":{"type":"string"},"bInputParam":{"type":"boolean"},"options":{"type":"object","properties":{"containerType":{"type":"string","enum":["Array","Set","Map"]},"bPassByReference":{"type":"boolean"},"bIsConst":{"type":"boolean"}}}},"required":["graph","paramName","paramType","bInputParam"]} as const;
const S_epic_add_interface = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"interfaceClass":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","interfaceClass"]} as const;
const S_epic_add_matching_delegate_event = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"eventName":{"type":"string"}},"required":["node"]} as const;
const S_epic_add_matching_delegate_function = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"functionName":{"type":"string"}},"required":["node"]} as const;
const S_epic_add_node_pin = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_add_node_pin__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_add_object_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"object_class":{"type":"object","properties":{"refPath":{}}},"input_param":{"type":"boolean"},"container_type":{"type":"string","enum":["ARRAY","SET","MAP"]}},"required":["graph","param_name","object_class","input_param"]} as const;
const S_epic_add_object_function_param__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramName":{"type":"string"},"objectClass":{"type":"object","properties":{"refPath":{}}},"bInputParam":{"type":"boolean"},"options":{"type":"object","properties":{"containerType":{"type":"string","enum":["Array","Set","Map"]},"bPassByReference":{"type":"boolean"},"bIsConst":{"type":"boolean"}}}},"required":["graph","paramName","objectClass","bInputParam"]} as const;
const S_epic_add_object_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"object_class":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"container_type":{"type":"string","enum":["ARRAY","SET","MAP"]}},"required":["blueprint","name","object_class"]} as const;
const S_epic_add_object_variable__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"objectClass":{"type":"object","properties":{"refPath":{}}},"options":{"type":"object","properties":{"graph":{"type":"object","properties":{"refPath":{}}},"containerType":{"type":"string","enum":["Array","Set","Map"]},"tooltip":{"type":"string"},"category":{"type":"string"},"bReadOnly":{"type":"boolean"}}}},"required":["blueprint","name","objectClass"]} as const;
const S_epic_add_struct_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"struct_type":{"type":"object","properties":{"refPath":{}}},"input_param":{"type":"boolean"},"container_type":{"type":"string","enum":["ARRAY","SET","MAP"]}},"required":["graph","param_name","struct_type","input_param"]} as const;
const S_epic_add_struct_function_param__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramName":{"type":"string"},"structType":{"type":"object","properties":{"refPath":{}}},"bInputParam":{"type":"boolean"},"options":{"type":"object","properties":{"containerType":{"type":"string","enum":["Array","Set","Map"]},"bPassByReference":{"type":"boolean"},"bIsConst":{"type":"boolean"}}}},"required":["graph","paramName","structType","bInputParam"]} as const;
const S_epic_add_struct_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"struct_type":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"container_type":{"type":"string","enum":["ARRAY","SET","MAP"]}},"required":["blueprint","name","struct_type"]} as const;
const S_epic_add_struct_variable__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"structType":{"type":"object","properties":{"refPath":{}}},"options":{"type":"object","properties":{"graph":{"type":"object","properties":{"refPath":{}}},"containerType":{"type":"string","enum":["Array","Set","Map"]},"tooltip":{"type":"string"},"category":{"type":"string"},"bReadOnly":{"type":"boolean"}}}},"required":["blueprint","name","structType"]} as const;
const S_epic_add_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"type_name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}},"container_type":{"type":"string","enum":["ARRAY","SET","MAP"]}},"required":["blueprint","name","type_name"]} as const;
const S_epic_add_variable__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"typeName":{"type":"string"},"options":{"type":"object","properties":{"graph":{"type":"object","properties":{"refPath":{}}},"containerType":{"type":"string","enum":["Array","Set","Map"]},"tooltip":{"type":"string"},"category":{"type":"string"},"bReadOnly":{"type":"boolean"}}}},"required":["blueprint","name","typeName"]} as const;
const S_epic_arrange_graph_layered = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"pinnedNodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"restrictToIslandOf":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_arrange_nodes = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["nodes"]} as const;
const S_epic_arrange_nodes__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["graph","nodes"]} as const;
const S_epic_break_pins = {"properties":{"output_pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}},"input_pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}}},"required":["output_pin","input_pin"]} as const;
const S_epic_compile_blueprint = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"warnings_as_errors":{"type":"boolean"}},"required":["blueprint"]} as const;
const S_epic_compile_blueprint__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"bWarningsAsErrors":{"type":"boolean"}},"required":["blueprint"]} as const;
const S_epic_connect_pins = {"properties":{"output_pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}},"input_pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}}},"required":["output_pin","input_pin"]} as const;
const S_epic_connect_pins__1 = {"properties":{"outputPin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}},"inputPin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}}},"required":["outputPin","inputPin"]} as const;
const S_epic_create = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"asset_type":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","asset_type"]} as const;
const S_epic_create__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"assetType":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","assetType"]} as const;
const S_epic_create_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"type_id":{"type":"string"},"pos":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"}}},"declaring_class":{"type":"object","properties":{"refPath":{}}}},"required":["graph","type_id","pos"]} as const;
const S_epic_create_node__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"typeId":{"type":"string"},"pos":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"}}},"declaringClass":{"type":"object","properties":{"refPath":{}}}},"required":["graph","typeId","pos"]} as const;
const S_epic_delete_node = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_delete_node__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_diff = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_diff__blueprint_dsl_tools = {"properties":{"old_asset_path":{"type":"string"},"new_asset_path":{"type":"string"}},"required":["old_asset_path","new_asset_path"]} as const;
const S_epic_diff_blueprint_html = {"properties":{"asset_path":{"type":"string"},"output_path":{"type":"string"},"revision":{"type":"string"},"fragment":{"type":"boolean"}},"required":["asset_path","output_path"]} as const;
const S_epic_diff_blueprint_revisions_html = {"properties":{"asset_path":{"type":"string"},"old_revision":{"type":"string"},"new_revision":{"type":"string"},"output_path":{"type":"string"},"fragment":{"type":"boolean"}},"required":["asset_path","old_revision","new_revision","output_path"]} as const;
const S_epic_diff_changelist_blueprints_html = {"properties":{"changelist":{"type":"integer"},"output_dir":{"type":"string"},"fragment":{"type":"boolean"}},"required":["changelist","output_dir"]} as const;
const S_epic_diff_graphs = {"properties":{"oldGraph":{"type":"object","properties":{"refPath":{}}},"newGraph":{"type":"object","properties":{"refPath":{}}}},"required":["oldGraph","newGraph"]} as const;
const S_epic_disconnect_pins = {"properties":{"outputPin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}},"inputPin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}}},"required":["outputPin","inputPin"]} as const;
const S_epic_docs = {"properties":{}} as const;
const S_epic_edit = {"properties":{"assetPath":{"type":"string"},"edits":{"type":"array","items":{"type":"object","properties":{"oldString":{"type":"string"},"newString":{"type":"string"},"bReplaceAll":{"type":"boolean"}}}},"revision":{"type":"string"},"scope":{"type":"string"}},"required":["assetPath","edits","revision"]} as const;
const S_epic_find_node_categories = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"category_filter":{"type":"string"},"context_pins":{"type":"array","items":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}}}},"required":["graph","category_filter","context_pins"]} as const;
const S_epic_find_node_categories__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"categoryFilter":{"type":"string"},"contextPins":{"type":"array","items":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}}}},"required":["graph"]} as const;
const S_epic_find_node_references = {"properties":{"assetPath":{"type":"string"},"query":{"type":"string"}},"required":["assetPath","query"]} as const;
const S_epic_find_node_types = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"type_id_filter":{"type":"string"},"context_pins":{"type":"array","items":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}}}},"required":["graph","type_id_filter","context_pins"]} as const;
const S_epic_find_node_types__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"typeIdFilter":{"type":"string"},"contextPins":{"type":"array","items":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}}}},"required":["graph","typeIdFilter"]} as const;
const S_epic_find_nodes = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"title":{"type":"string"},"node_class":{"type":"object","properties":{"refPath":{}}},"entry_points_only":{"type":"boolean"}},"required":["graph","title"]} as const;
const S_epic_find_nodes__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"title":{"type":"string"},"nodeClass":{"type":"object","properties":{"refPath":{}}},"bEntryPointsOnly":{"type":"boolean"}},"required":["graph"]} as const;
const S_epic_get_comment_box_text = {"properties":{"commentBox":{"type":"object","properties":{"refPath":{}}}},"required":["commentBox"]} as const;
const S_epic_get_connected_subgraph = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_connected_subgraph__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_create_event_function = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_create_event_function__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_default_object = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_get_default_object__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_get_delegate_signature = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_function_flags = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["blueprint","graph_name"]} as const;
const S_epic_get_graph__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graphName":{"type":"string"}},"required":["blueprint","graphName"]} as const;
const S_epic_get_graph_dsl_docs = {"properties":{}} as const;
const S_epic_get_graph_dsl_docs__blueprint_dsl_tools = {"properties":{}} as const;
const S_epic_get_node_infos = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["nodes"]} as const;
const S_epic_get_node_infos__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["nodes"]} as const;
const S_epic_get_node_note = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_node_type_pins = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"type_id":{"type":"string"}},"required":["graph","type_id"]} as const;
const S_epic_get_node_type_pins__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"typeId":{"type":"string"}},"required":["graph","typeId"]} as const;
const S_epic_get_nodes_in_comment_box = {"properties":{"commentBox":{"type":"object","properties":{"refPath":{}}}},"required":["commentBox"]} as const;
const S_epic_get_parent = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_get_parent__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_get_pin_value = {"properties":{"pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}}},"required":["pin"]} as const;
const S_epic_get_pin_value__1 = {"properties":{"pin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}}},"required":["pin"]} as const;
const S_epic_get_variable_category = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"}},"required":["blueprint","variable_name"]} as const;
const S_epic_get_variable_category__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","variableName"]} as const;
const S_epic_get_variable_read_only = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["blueprint","variableName"]} as const;
const S_epic_get_variable_replication = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"}},"required":["blueprint","variable_name"]} as const;
const S_epic_get_variable_replication__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["blueprint","variableName"]} as const;
const S_epic_get_variable_tooltip = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","variableName"]} as const;
const S_epic_is_node_note_pinned = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_list_comment_boxes = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_list_compatible_event_functions = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_list_compatible_event_functions__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_list_component_events = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_list_component_events__1 = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_list_event_dispatchers = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_event_dispatchers__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_events = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_events__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_functions = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_functions__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_graphs = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_graphs__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_interfaces = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"bIncludeInherited":{"type":"boolean"}},"required":["blueprint"]} as const;
const S_epic_list_variables = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint"]} as const;
const S_epic_list_variables__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"scope":{"type":"string","enum":["Member","Local","Component"]},"graph":{"type":"object","properties":{"refPath":{}}},"bIncludeInherited":{"type":"boolean"}},"required":["blueprint"]} as const;
const S_epic_read = {"properties":{"assetPath":{"type":"string"},"scope":{"type":"string"},"bIncludePositions":{"type":"boolean"}},"required":["assetPath"]} as const;
const S_epic_read_graph_dsl = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_read_graph_dsl__blueprint_dsl_tools = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"entry_name":{"type":"string"},"allow_unresolved_references":{"type":"boolean"}},"required":["graph"]} as const;
const S_epic_remove_comment_box = {"properties":{"commentBox":{"type":"object","properties":{"refPath":{}}}},"required":["commentBox"]} as const;
const S_epic_remove_function_graph = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["blueprint","graph_name"]} as const;
const S_epic_remove_function_graph__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"graphName":{"type":"string"}},"required":["blueprint","graphName"]} as const;
const S_epic_remove_function_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"param_name":{"type":"string"},"input_param":{"type":"boolean"}},"required":["graph","param_name","input_param"]} as const;
const S_epic_remove_function_param__1 = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramName":{"type":"string"},"bInputParam":{"type":"boolean"}},"required":["graph","paramName","bInputParam"]} as const;
const S_epic_remove_interface = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"interfaceClass":{"type":"object","properties":{"refPath":{}}},"bPreserveFunctions":{"type":"boolean"}},"required":["blueprint","interfaceClass"]} as const;
const S_epic_remove_node_pin = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}}},"required":["node","pin"]} as const;
const S_epic_remove_node_pin__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}}},"required":["node","pin"]} as const;
const S_epic_remove_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","name"]} as const;
const S_epic_remove_variable__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","name"]} as const;
const S_epic_retarget_node_class = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"old_class":{"type":"object","properties":{"refPath":{}}},"new_class":{"type":"object","properties":{"refPath":{}}}},"required":["node","old_class","new_class"]} as const;
const S_epic_retarget_node_class__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"oldClass":{"type":"object","properties":{"refPath":{}}},"newClass":{"type":"object","properties":{"refPath":{}}}},"required":["node","oldClass","newClass"]} as const;
const S_epic_schema = {"properties":{"assetPath":{"type":"string"},"nodeType":{"type":"string"}},"required":["assetPath","nodeType"]} as const;
const S_epic_set_comment_box_text = {"properties":{"commentBox":{"type":"object","properties":{"refPath":{}}},"text":{"type":"string"}},"required":["commentBox","text"]} as const;
const S_epic_set_create_event_function = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"function_name":{"type":"string"}},"required":["node","function_name"]} as const;
const S_epic_set_create_event_function__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"functionName":{"type":"string"}},"required":["node","functionName"]} as const;
const S_epic_set_function_flags = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"flags":{"type":"object","properties":{"access":{"type":"string","enum":["Public","Protected","Private"]},"bIsPure":{"type":"boolean"},"bIsConst":{"type":"boolean"},"bIsExec":{"type":"boolean"},"bIsThreadSafe":{"type":"boolean"},"bIsCallInEditor":{"type":"boolean"},"bIsUnsafeDuringActorConstruction":{"type":"boolean"},"bIsDeprecated":{"type":"boolean"},"deprecationMessage":{"type":"string"},"category":{"type":"string"},"tooltip":{"type":"string"},"keywords":{"type":"string"},"compactNodeTitle":{"type":"string"}}}},"required":["graph","flags"]} as const;
const S_epic_set_node_note = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"text":{"type":"string"}},"required":["node","text"]} as const;
const S_epic_set_node_note_pinned = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"bPinned":{"type":"boolean"}},"required":["node","bPinned"]} as const;
const S_epic_set_node_position = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pos":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"}}}},"required":["node","pos"]} as const;
const S_epic_set_node_position__1 = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pos":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"}}}},"required":["node","pos"]} as const;
const S_epic_set_parent = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"parent_class":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","parent_class"]} as const;
const S_epic_set_parent__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","parentClass"]} as const;
const S_epic_set_pin_value = {"properties":{"pin":{"type":"object","properties":{"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]},"index_id":{"type":"integer"},"node":{"type":"object","properties":{"refPath":{}}}}},"value":{"type":"string"}},"required":["pin","value"]} as const;
const S_epic_set_pin_value__1 = {"properties":{"pin":{"type":"object","properties":{"node":{"type":"object","properties":{"refPath":{}}},"indexId":{"type":"integer"},"direction":{"type":"string","enum":["EGPD_Input","EGPD_Output"]}}},"value":{"type":"string"}},"required":["pin","value"]} as const;
const S_epic_set_variable_category = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"category":{"type":"string"}},"required":["blueprint","variable_name","category"]} as const;
const S_epic_set_variable_category__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"category":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","variableName","category"]} as const;
const S_epic_set_variable_instance_editable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"instance_editable":{"type":"boolean"}},"required":["blueprint","variable_name","instance_editable"]} as const;
const S_epic_set_variable_instance_editable__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"bInstanceEditable":{"type":"boolean"}},"required":["blueprint","variableName","bInstanceEditable"]} as const;
const S_epic_set_variable_read_only = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"bReadOnly":{"type":"boolean"}},"required":["blueprint","variableName","bReadOnly"]} as const;
const S_epic_set_variable_replication = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"replication":{"type":"string","enum":["None","Replicated","RepNotify"]}},"required":["blueprint","variable_name","replication"]} as const;
const S_epic_set_variable_replication__1 = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"replication":{"type":"string","enum":["None","Replicated","RepNotify"]}},"required":["blueprint","variableName","replication"]} as const;
const S_epic_set_variable_tooltip = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"tooltip":{"type":"string"},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["blueprint","variableName","tooltip"]} as const;
const S_epic_write = {"properties":{"assetPath":{"type":"string"},"dsl":{"type":"string"},"scope":{"type":"string"}},"required":["assetPath","dsl"]} as const;
const S_epic_write_graph_dsl = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"code":{"type":"string"}},"required":["graph","code"]} as const;
const S_epic_write_graph_dsl__blueprint_dsl_tools = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"code":{"type":"string"},"entry_name":{"type":"string"},"force_rewrite":{"type":"boolean"}},"required":["graph","code"]} as const;

/** 92 wrapped engine tools routed to the `blueprint` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_comment_box: {
    epicSchema: S_epic_add_comment_box,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddCommentBox", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Adds a comment box (the resizable labeled rectangle) to a Blueprint graph. Pass Nodes to wrap them in a box sized to their bounds, or pass Rect to place a standalone box. Available on UE 5.9; absent from UE 5.8. Params: graph, commentText, nodes?, rect?, padding?", "epic_call_tool"),
  },
  epic_add_component_bound_event: {
    epicSchema: S_epic_add_component_bound_event,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_component_bound_event", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddComponentBoundEvent", engines: ["5.9"], schema: S_epic_add_component_bound_event__1, rename: {"event_name":"eventName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Creates a component bound event node in the event graph. Params: component, event_name, graph. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddComponentBoundEvent (Params: component, eventName, graph).", "epic_call_tool"),
  },
  epic_add_event: {
    epicSchema: S_epic_add_event,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_event", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddEvent", engines: ["5.9"], schema: S_epic_add_event__1, rename: {"event_name":"eventName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an event node to the Blueprint's event graph. If event_name matches an inherited overridable event, the new node is an override of that event. Otherwise a new custom event with the given name is created. Idempotent - if an event node with that name already exists, that node is returned. Params: blueprint, event_name, position?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddEvent (Params: blueprint, eventName, position?).", "epic_call_tool"),
  },
  epic_add_event_dispatcher: {
    epicSchema: S_epic_add_event_dispatcher,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_event_dispatcher", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddEventDispatcher", engines: ["5.9"], schema: S_epic_add_event_dispatcher__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an event dispatcher to a Blueprint. Params: blueprint, name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddEventDispatcher (Params: blueprint, name).", "epic_call_tool"),
  },
  epic_add_function_graph: {
    epicSchema: S_epic_add_function_graph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_function_graph", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddFunctionGraph", engines: ["5.9"], schema: S_epic_add_function_graph__1, rename: {"graph_name":"graphName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a function graph to the Blueprint. If graph_name matches an inherited overridable function, the new graph is a function-graph override of that function. Idempotent - if a graph with that name already exists, that graph is returned. Params: blueprint, graph_name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddFunctionGraph (Params: blueprint, graphName, flags?).", "epic_call_tool"),
  },
  epic_add_function_param: {
    epicSchema: S_epic_add_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_function_param", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddFunctionParam", engines: ["5.9"], schema: S_epic_add_function_param__1, rename: {"param_name":"paramName","param_type":"paramType","input_param":"bInputParam"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an input or output to a function or event dispatcher Params: graph, param_name, param_type, input_param, container_type?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddFunctionParam (Params: graph, paramName, paramType, bInputParam, options?).", "epic_call_tool"),
  },
  epic_add_interface: {
    epicSchema: S_epic_add_interface,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddInterface", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Adds an interface to a Blueprint's implemented interfaces. The interface's function-shape members become function graphs on the Blueprint, and its event-shape members become overridable events. The Blueprint must be recompiled for the change to take effect. Idempotent - adding an interface the Blueprint already implements is a no-op. Available on UE 5.9; absent from UE 5.8. Params: blueprint, interfaceClass", "epic_call_tool"),
  },
  epic_add_matching_delegate_event: {
    epicSchema: S_epic_add_matching_delegate_event,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddMatchingDelegateEvent", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Adds a custom event matching a Create Event node's delegate signature and binds it. The generated event's params match the delegate exactly. Signatures with output params or a return value cannot be implemented by an event - use AddMatchingDelegateFunction for those. The node's OutputDelegate pin must be connected first; the signature is read from it. Idempotent - if an event of the requested name already exists and matches the delegate it is bound and returned. Available on UE 5.9; absent from UE 5.8. Params: node, eventName?", "epic_call_tool"),
  },
  epic_add_matching_delegate_function: {
    epicSchema: S_epic_add_matching_delegate_function,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddMatchingDelegateFunction", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Adds a function graph matching a Create Event node's delegate signature and binds it. The generated function's params match the delegate exactly, including the passing mode of each param. The node's OutputDelegate pin must be connected first; the signature is read from it. Idempotent - if a graph of the requested name already exists and matches the delegate it is bound and returned. Available on UE 5.9; absent from UE 5.8. Params: node, functionName?", "epic_call_tool"),
  },
  epic_add_node_pin: {
    epicSchema: S_epic_add_node_pin,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_node_pin", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddNodePin", engines: ["5.9"], schema: S_epic_add_node_pin__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a pin to a node that supports dynamic pin addition. Works for Switch nodes (adds one case pin), Sequence nodes (adds one Then output), commutative binary operators like Add/Multiply (adds one input), Make Array nodes, etc. Params: node. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddNodePin (Params: node).", "epic_call_tool"),
  },
  epic_add_object_function_param: {
    epicSchema: S_epic_add_object_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_object_function_param", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddObjectFunctionParam", engines: ["5.9"], schema: S_epic_add_object_function_param__1, rename: {"param_name":"paramName","object_class":"objectClass","input_param":"bInputParam"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds an object reference input or output to a function or event dispatcher. Params: graph, param_name, object_class, input_param, container_type?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddObjectFunctionParam (Params: graph, paramName, objectClass, bInputParam, options?).", "epic_call_tool"),
  },
  epic_add_object_variable: {
    epicSchema: S_epic_add_object_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_object_variable", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddObjectVariable", engines: ["5.9"], schema: S_epic_add_object_variable__1, rename: {"object_class":"objectClass"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a member or local variable that holds an object reference to a Blueprint. Params: blueprint, name, object_class, graph?, container_type?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddObjectVariable (Params: blueprint, name, objectClass, options?).", "epic_call_tool"),
  },
  epic_add_struct_function_param: {
    epicSchema: S_epic_add_struct_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_struct_function_param", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddStructFunctionParam", engines: ["5.9"], schema: S_epic_add_struct_function_param__1, rename: {"param_name":"paramName","struct_type":"structType","input_param":"bInputParam"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a struct input or output to a function or event dispatcher. Params: graph, param_name, struct_type, input_param, container_type?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddStructFunctionParam (Params: graph, paramName, structType, bInputParam, options?).", "epic_call_tool"),
  },
  epic_add_struct_variable: {
    epicSchema: S_epic_add_struct_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_struct_variable", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddStructVariable", engines: ["5.9"], schema: S_epic_add_struct_variable__1, rename: {"struct_type":"structType"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a member or local variable of a struct type to a Blueprint. Use this to add variables of any UStruct type, including custom structs and engine structs not in the basic list supported by add_variable (e.g. HitResult, GameplayTag). Params: blueprint, name, struct_type, graph?, container_type?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddStructVariable (Params: blueprint, name, structType, options?).", "epic_call_tool"),
  },
  epic_add_variable: {
    epicSchema: S_epic_add_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.add_variable", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.AddVariable", engines: ["5.9"], schema: S_epic_add_variable__1, rename: {"type_name":"typeName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a member or local variable to a Blueprint. Supported type names: Primitives: 'bool', 'int', 'float', 'byte', 'string', 'name', 'text' Structs: 'Vector', 'Rotator', 'Transform', 'Vector2D', 'LinearColor' Params: blueprint, name, type_name, graph?, container_type?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.AddVariable (Params: blueprint, name, typeName, options?).", "epic_call_tool"),
  },
  epic_arrange_graph_layered: {
    epicSchema: S_epic_arrange_graph_layered,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ArrangeGraphLayered", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Arranges a whole graph in a readable left-to-right layout, per connected component: each component becomes a horizontal band, execution chains hold a single straight row, and comment boxes are honored as nested groups and rebuilt around their members. Does not require an open graph editor. Available on UE 5.9; absent from UE 5.8. Params: graph, pinnedNodes?, restrictToIslandOf?", "epic_call_tool"),
  },
  epic_arrange_nodes: {
    epicSchema: S_epic_arrange_nodes,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.arrange_nodes", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ArrangeNodes", engines: ["5.9"], schema: S_epic_arrange_nodes__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Arranges a list of nodes in a readable left-to-right layout. Organizes nodes into columns based on data/execution flow, with producer nodes to the left of the nodes they feed into. Call this after building a graph to avoid nodes overlapping. Connections to nodes outside the list are used as anchors so the arranged nodes integrate cleanly with the rest of the graph. Params: nodes. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ArrangeNodes (Params: graph, nodes).", "epic_call_tool"),
  },
  epic_break_pins: {
    epicSchema: S_epic_break_pins,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.break_pins", engines: ["5.8"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Breaks the connection between two pins. Available on UE 5.8; absent from UE 5.9. Params: output_pin, input_pin", "epic_call_tool"),
  },
  epic_compile_blueprint: {
    epicSchema: S_epic_compile_blueprint,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.compile_blueprint", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.CompileBlueprint", engines: ["5.9"], schema: S_epic_compile_blueprint__1, rename: {"warnings_as_errors":"bWarningsAsErrors"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Compiles the given Blueprint. Blueprints should be compiled after all graph modifications are complete. Params: blueprint, warnings_as_errors?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.CompileBlueprint (Params: blueprint, bWarningsAsErrors?).", "epic_call_tool"),
  },
  epic_connect_pins: {
    epicSchema: S_epic_connect_pins,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.connect_pins", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ConnectPins", engines: ["5.9"], schema: S_epic_connect_pins__1, rename: {"output_pin":"outputPin","input_pin":"inputPin"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Makes a connection between source (output) and dest (input) pins. Params: output_pin, input_pin. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ConnectPins (Params: outputPin, inputPin).", "epic_call_tool"),
  },
  epic_create: {
    epicSchema: S_epic_create,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.create", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.Create", engines: ["5.9"], schema: S_epic_create__1, rename: {"folder_path":"folderPath","asset_name":"assetName","asset_type":"assetType"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Creates a new Blueprint asset in the project. Params: folder_path, asset_name, asset_type. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.Create (Params: folderPath, assetName, assetType).", "epic_call_tool"),
  },
  epic_create_node: {
    epicSchema: S_epic_create_node,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.create_node", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.CreateNode", engines: ["5.9"], schema: S_epic_create_node__1, rename: {"type_id":"typeId","declaring_class":"declaringClass"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Adds a new node to the graph. Params: graph, type_id, pos, declaring_class?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.CreateNode (Params: graph, typeId, pos, declaringClass?).", "epic_call_tool"),
  },
  epic_delete_node: {
    epicSchema: S_epic_delete_node,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.delete_node", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.DeleteNode", engines: ["5.9"], schema: S_epic_delete_node__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Deletes the node from its graph. Params: node. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.DeleteNode (Params: node).", "epic_call_tool"),
  },
  epic_diff: {
    epicSchema: S_epic_diff,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintDslToolset] Returns a line diff between two Blueprints' graphs, as DSL, and their class defaults. Empty when they match. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_diff__blueprint_dsl_tools: {
    epicSchema: S_epic_diff__blueprint_dsl_tools,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintDslTools", name: "editor_toolset.toolsets.blueprint.BlueprintDslTools.diff", engines: ["5.9"] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintDslTools] Returns a unified text diff between two Blueprint assets. Available on UE 5.9; absent from UE 5.8. Params: old_asset_path, new_asset_path", "epic_call_tool"),
  },
  epic_diff_blueprint_html: {
    epicSchema: S_epic_diff_blueprint_html,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools", name: "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools.diff_blueprint_html", engines: ["5.9"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools] Diffs the workspace version of a Blueprint against a depot revision and writes HTML. Available on UE 5.9; absent from UE 5.8. Params: asset_path, output_path, revision?, fragment?", "epic_call_tool"),
  },
  epic_diff_blueprint_revisions_html: {
    epicSchema: S_epic_diff_blueprint_revisions_html,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools", name: "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools.diff_blueprint_revisions_html", engines: ["5.9"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools] Diffs two depot revisions of a Blueprint and writes HTML. Available on UE 5.9; absent from UE 5.8. Params: asset_path, old_revision, new_revision, output_path, fragment?", "epic_call_tool"),
  },
  epic_diff_changelist_blueprints_html: {
    epicSchema: S_epic_diff_changelist_blueprints_html,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools", name: "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools.diff_changelist_blueprints_html", engines: ["5.9"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools] Writes one HTML diff per Blueprint asset that a changelist modifies. Works for submitted and shelved changelists on any branch. The before and after files come from Perforce, so the workspace version is not used. Added, moved, deleted, and non-Blueprint files are skipped, since they have no earlier version at their path. Available on UE 5.9; absent from UE 5.8. Params: changelist, output_dir, fragment?", "epic_call_tool"),
  },
  epic_diff_graphs: {
    epicSchema: S_epic_diff_graphs,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.DiffGraphs", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Compares two versions of a graph with the same matching the editor's Diff window uses. Available on UE 5.9; absent from UE 5.8. Params: oldGraph, newGraph", "epic_call_tool"),
  },
  epic_disconnect_pins: {
    epicSchema: S_epic_disconnect_pins,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.DisconnectPins", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Breaks two connected pins. Available on UE 5.9; absent from UE 5.8. Params: outputPin, inputPin", "epic_call_tool"),
  },
  epic_docs: {
    epicSchema: S_epic_docs,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.Docs", engines: ["5.9"] },
    ...bp("unknown", "[Epic BlueprintToolsets.BlueprintDslToolset] Returns the common grammar followed by the Blueprint dialect reference. Read it before the first Write or Edit. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_edit: {
    epicSchema: S_epic_edit,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.Edit", engines: ["5.9"] },
    ...bp("unknown", "[Epic BlueprintToolsets.BlueprintDslToolset] Applies ordered exact replacements to the latest session text. Revision must come from the latest Read, Write, or Edit for the same asset and scope; otherwise call Read again. The full batch is staged before mutation. The edit fails without a matching session, with a stale Revision, when an OldString is absent or non-unique unless bReplaceAll is true, or when the resulting DSL does not parse. Any failure leaves the asset unchanged. When bDslOmitted is true, apply the replacements to your existing copy. Otherwise Dsl is the authoritative canonical text for later edits. Available on UE 5.9; absent from UE 5.8. Params: assetPath, edits, revision, scope?", "epic_call_tool"),
  },
  epic_find_node_categories: {
    epicSchema: S_epic_find_node_categories,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.find_node_categories", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.FindNodeCategories", engines: ["5.9"], schema: S_epic_find_node_categories__1, rename: {"category_filter":"categoryFilter","context_pins":"contextPins"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Retrieves a list of available node categories in a Blueprint graph, optionally filtered by compatible input and/or output pin types Params: graph, category_filter, context_pins. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.FindNodeCategories (Params: graph, categoryFilter?, contextPins?).", "epic_call_tool"),
  },
  epic_find_node_references: {
    epicSchema: S_epic_find_node_references,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.FindNodeReferences", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintDslToolset] Returns Graph DSL node references whose palette name, category or reference contains the query (case and spaces ignored), with each one's palette category and name. The reference, not the palette name, is what goes in a Write or Edit call head. Available on UE 5.9; absent from UE 5.8. Params: assetPath, query", "epic_call_tool"),
  },
  epic_find_node_types: {
    epicSchema: S_epic_find_node_types,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.find_node_types", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.FindNodeTypes", engines: ["5.9"], schema: S_epic_find_node_types__1, rename: {"type_id_filter":"typeIdFilter","context_pins":"contextPins"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Finds node types that can be created in a particular graph meeting the search criteria. Params: graph, type_id_filter, context_pins. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.FindNodeTypes (Params: graph, typeIdFilter, contextPins?).", "epic_call_tool"),
  },
  epic_find_nodes: {
    epicSchema: S_epic_find_nodes,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.find_nodes", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.FindNodes", engines: ["5.9"], schema: S_epic_find_nodes__1, rename: {"node_class":"nodeClass","entry_points_only":"bEntryPointsOnly"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Finds nodes in a graph by title, class, and/or execution role. All filters are optional and ANDed together. Useful for locating specific event chains in large graphs like EventGraph before reading them with get_connected_subgraph. Params: graph, title, node_class?, entry_points_only?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.FindNodes (Params: graph, title?, nodeClass?, bEntryPointsOnly?).", "epic_call_tool"),
  },
  epic_get_comment_box_text: {
    epicSchema: S_epic_get_comment_box_text,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetCommentBoxText", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Returns the text of a comment box. Available on UE 5.9; absent from UE 5.8. Params: commentBox", "epic_call_tool"),
  },
  epic_get_connected_subgraph: {
    epicSchema: S_epic_get_connected_subgraph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_connected_subgraph", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetConnectedSubgraph", engines: ["5.9"], schema: S_epic_get_connected_subgraph__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns detailed information for all nodes connected to the given node. Use this alongside find_nodes to read a single event chain from a large graph (like Event Graph) without reading the entire graph. Params: node. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetConnectedSubgraph (Params: node).", "epic_call_tool"),
  },
  epic_get_create_event_function: {
    epicSchema: S_epic_get_create_event_function,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_create_event_function", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetCreateEventFunction", engines: ["5.9"], schema: S_epic_get_create_event_function__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the function currently bound to a Create Event node. Params: node. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetCreateEventFunction (Params: node).", "epic_call_tool"),
  },
  epic_get_default_object: {
    epicSchema: S_epic_get_default_object,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_default_object", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetDefaultObject", engines: ["5.9"], schema: S_epic_get_default_object__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the Class Default Object (CDO) for a Blueprint's. ObjectTools list/set/get property will get the CDO automatically, so this should primarily be used before calling tools that want to operate on the object inside the blueprint (like ActorTools). Params: blueprint. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetDefaultObject (Params: blueprint).", "epic_call_tool"),
  },
  epic_get_delegate_signature: {
    epicSchema: S_epic_get_delegate_signature,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetDelegateSignature", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Returns the signature a Create Event node's bound function must implement. Each param is listed on its own line with its type, direction and passing mode, in the terms AddFunctionParam takes. Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_get_function_flags: {
    epicSchema: S_epic_get_function_flags,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetFunctionFlags", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Returns the detail-panel settings of a Blueprint function. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_get_graph: {
    epicSchema: S_epic_get_graph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_graph", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetGraph", engines: ["5.9"], schema: S_epic_get_graph__1, rename: {"graph_name":"graphName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Retrieves a specific graph from a Blueprint asset by name. Params: blueprint, graph_name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetGraph (Params: blueprint, graphName).", "epic_call_tool"),
  },
  epic_get_graph_dsl_docs: {
    epicSchema: S_epic_get_graph_dsl_docs,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_graph_dsl_docs", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the full syntax reference for write_graph_dsl. Call this before using write_graph_dsl for the first time. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_graph_dsl_docs__blueprint_dsl_tools: {
    epicSchema: S_epic_get_graph_dsl_docs__blueprint_dsl_tools,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintDslTools", name: "editor_toolset.toolsets.blueprint.BlueprintDslTools.get_graph_dsl_docs", engines: ["5.9"] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintDslTools] Returns the full syntax reference for write_graph_dsl. Call this before using write_graph_dsl for the first time. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_node_infos: {
    epicSchema: S_epic_get_node_infos,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_node_infos", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetNodeInfos", engines: ["5.9"], schema: S_epic_get_node_infos__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Retrieves detailed information for a list of Blueprint graph nodes. Params: nodes. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetNodeInfos (Params: nodes).", "epic_call_tool"),
  },
  epic_get_node_note: {
    epicSchema: S_epic_get_node_note,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetNodeNote", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Returns the note text on a Blueprint node (the small comment bubble shown over an individual node). Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_get_node_type_pins: {
    epicSchema: S_epic_get_node_type_pins,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_node_type_pins", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetNodeTypePins", engines: ["5.9"], schema: S_epic_get_node_type_pins__1, rename: {"type_id":"typeId"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the pin names and types for a node type. Params: graph, type_id. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetNodeTypePins (Params: graph, typeId).", "epic_call_tool"),
  },
  epic_get_nodes_in_comment_box: {
    epicSchema: S_epic_get_nodes_in_comment_box,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetNodesInCommentBox", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Returns the nodes grouped inside a comment box. Available on UE 5.9; absent from UE 5.8. Params: commentBox", "epic_call_tool"),
  },
  epic_get_parent: {
    epicSchema: S_epic_get_parent,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_parent", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetParent", engines: ["5.9"], schema: S_epic_get_parent__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Returns the parent class of a Blueprint. Params: blueprint. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetParent (Params: blueprint).", "epic_call_tool"),
  },
  epic_get_pin_value: {
    epicSchema: S_epic_get_pin_value,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_pin_value", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetPinValue", engines: ["5.9"], schema: S_epic_get_pin_value__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Gets the value of a Blueprint graph pin. Params: pin. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetPinValue (Params: pin).", "epic_call_tool"),
  },
  epic_get_variable_category: {
    epicSchema: S_epic_get_variable_category,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_variable_category", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetVariableCategory", engines: ["5.9"], schema: S_epic_get_variable_category__1, rename: {"variable_name":"variableName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Gets the user-defined category of a Blueprint member variable. Categories group variables in the My Blueprint panel. Variables with no explicit category default to the Blueprint's name in the UI. Params: blueprint, variable_name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetVariableCategory (Params: blueprint, variableName, graph?).", "epic_call_tool"),
  },
  epic_get_variable_read_only: {
    epicSchema: S_epic_get_variable_read_only,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetVariableReadOnly", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Gets whether a member variable is Blueprint Read Only (graphs can read but not write it). Available on UE 5.9; absent from UE 5.8. Params: blueprint, variableName", "epic_call_tool"),
  },
  epic_get_variable_replication: {
    epicSchema: S_epic_get_variable_replication,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.get_variable_replication", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetVariableReplication", engines: ["5.9"], schema: S_epic_get_variable_replication__1, rename: {"variable_name":"variableName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Gets the replication mode of a Blueprint member variable. Params: blueprint, variable_name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.GetVariableReplication (Params: blueprint, variableName).", "epic_call_tool"),
  },
  epic_get_variable_tooltip: {
    epicSchema: S_epic_get_variable_tooltip,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.GetVariableTooltip", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Gets the tooltip shown for a Blueprint variable in the editor. Available on UE 5.9; absent from UE 5.8. Params: blueprint, variableName, graph?", "epic_call_tool"),
  },
  epic_is_node_note_pinned: {
    epicSchema: S_epic_is_node_note_pinned,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.IsNodeNotePinned", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Returns whether a node's note bubble is pinned (always visible). Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_list_comment_boxes: {
    epicSchema: S_epic_list_comment_boxes,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListCommentBoxes", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Lists all comment boxes in a Blueprint graph. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_list_compatible_event_functions: {
    epicSchema: S_epic_list_compatible_event_functions,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_compatible_event_functions", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListCompatibleEventFunctions", engines: ["5.9"], schema: S_epic_list_compatible_event_functions__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists functions that can be bound to a Create Event node. Params: node. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListCompatibleEventFunctions (Params: node).", "epic_call_tool"),
  },
  epic_list_component_events: {
    epicSchema: S_epic_list_component_events,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_component_events", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListComponentEvents", engines: ["5.9"], schema: S_epic_list_component_events__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists the bindable delegate events available on a component. Params: component. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListComponentEvents (Params: component).", "epic_call_tool"),
  },
  epic_list_event_dispatchers: {
    epicSchema: S_epic_list_event_dispatchers,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_event_dispatchers", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListEventDispatchers", engines: ["5.9"], schema: S_epic_list_event_dispatchers__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all event dispatchers defined on a Blueprint. Params: blueprint. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListEventDispatchers (Params: blueprint).", "epic_call_tool"),
  },
  epic_list_events: {
    epicSchema: S_epic_list_events,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_events", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListEvents", engines: ["5.9"], schema: S_epic_list_events__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all events visible on the Blueprint - locally defined custom events plus inheritable events from the parent class chain and implemented interfaces. Params: blueprint. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListEvents (Params: blueprint).", "epic_call_tool"),
  },
  epic_list_functions: {
    epicSchema: S_epic_list_functions,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_functions", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListFunctions", engines: ["5.9"], schema: S_epic_list_functions__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all functions visible on the Blueprint - locally defined plus inheritable ones from the parent class chain and implemented interfaces. Params: blueprint. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListFunctions (Params: blueprint).", "epic_call_tool"),
  },
  epic_list_graphs: {
    epicSchema: S_epic_list_graphs,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_graphs", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListGraphs", engines: ["5.9"], schema: S_epic_list_graphs__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists all graphs in the Blueprint. Params: blueprint. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListGraphs (Params: blueprint).", "epic_call_tool"),
  },
  epic_list_interfaces: {
    epicSchema: S_epic_list_interfaces,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListInterfaces", engines: ["5.9"] },
    ...bp("read", "[Epic BlueprintToolsets.BlueprintToolset] Lists the interfaces implemented by a Blueprint. Available on UE 5.9; absent from UE 5.8. Params: blueprint, bIncludeInherited?", "epic_call_tool"),
  },
  epic_list_variables: {
    epicSchema: S_epic_list_variables,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.list_variables", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.ListVariables", engines: ["5.9"], schema: S_epic_list_variables__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Lists member or local variables defined on a Blueprint. Params: blueprint, graph?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.ListVariables (Params: blueprint, scope?, graph?, bIncludeInherited?).", "epic_call_tool"),
  },
  epic_read: {
    epicSchema: S_epic_read,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.Read", engines: ["5.9"] },
    ...bp("unknown", "[Epic BlueprintToolsets.BlueprintDslToolset] Reads a Blueprint scope as canonical DSL and returns the revision required by Edit. Scope is empty for the whole asset, GraphName for one graph, or GraphName/EntryName for one entry. Backtick-quote a segment containing spaces, syntax characters, or `/`; `` `A/B` `` is one segment. Set bIncludePositions only when node positions are needed. Available on UE 5.9; absent from UE 5.8. Params: assetPath, scope?, bIncludePositions?", "epic_call_tool"),
  },
  epic_read_graph_dsl: {
    epicSchema: S_epic_read_graph_dsl,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.read_graph_dsl", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Reads a Blueprint graph and returns a DSL script. The returned code uses the same syntax that write_graph_dsl accepts and can be edited and passed back to write_graph_dsl to modify the graph. Call get_graph_dsl_docs() for the full syntax reference. Available on UE 5.8; absent from UE 5.9. Params: graph", "epic_call_tool"),
  },
  epic_read_graph_dsl__blueprint_dsl_tools: {
    epicSchema: S_epic_read_graph_dsl__blueprint_dsl_tools,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintDslTools", name: "editor_toolset.toolsets.blueprint.BlueprintDslTools.read_graph_dsl", engines: ["5.9"] },
    ...bp("read", "[Epic editor_toolset.toolsets.blueprint.BlueprintDslTools] Reads a Blueprint graph and returns a DSL script. The returned code uses the same syntax that write_graph_dsl accepts and can be edited and passed back to write_graph_dsl to modify the graph. Call get_graph_dsl_docs() for the full syntax reference. Available on UE 5.9; absent from UE 5.8. Params: graph, entry_name?, allow_unresolved_references?", "epic_call_tool"),
  },
  epic_remove_comment_box: {
    epicSchema: S_epic_remove_comment_box,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RemoveCommentBox", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Removes a comment box from its graph. Available on UE 5.9; absent from UE 5.8. Params: commentBox", "epic_call_tool"),
  },
  epic_remove_function_graph: {
    epicSchema: S_epic_remove_function_graph,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_function_graph", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RemoveFunctionGraph", engines: ["5.9"], schema: S_epic_remove_function_graph__1, rename: {"graph_name":"graphName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes a function graph or event dispatcher from the Blueprint. Params: blueprint, graph_name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.RemoveFunctionGraph (Params: blueprint, graphName).", "epic_call_tool"),
  },
  epic_remove_function_param: {
    epicSchema: S_epic_remove_function_param,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_function_param", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RemoveFunctionParam", engines: ["5.9"], schema: S_epic_remove_function_param__1, rename: {"param_name":"paramName","input_param":"bInputParam"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes an input or output from a function or event dispatcher. Params: graph, param_name, input_param. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.RemoveFunctionParam (Params: graph, paramName, bInputParam).", "epic_call_tool"),
  },
  epic_remove_interface: {
    epicSchema: S_epic_remove_interface,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RemoveInterface", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Removes an implemented interface from a Blueprint. The Blueprint must be recompiled afterward. Available on UE 5.9; absent from UE 5.8. Params: blueprint, interfaceClass, bPreserveFunctions?", "epic_call_tool"),
  },
  epic_remove_node_pin: {
    epicSchema: S_epic_remove_node_pin,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_node_pin", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RemoveNodePin", engines: ["5.9"], schema: S_epic_remove_node_pin__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes a specific pin from a node that supports dynamic pin removal. Works for Switch nodes (removes one case pin), Sequence nodes (removes one Then output), commutative binary operators like Add/Multiply (removes one input), Make Array nodes, etc. Params: node, pin. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.RemoveNodePin (Params: node, pin).", "epic_call_tool"),
  },
  epic_remove_variable: {
    epicSchema: S_epic_remove_variable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.remove_variable", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RemoveVariable", engines: ["5.9"], schema: S_epic_remove_variable__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Removes a member or local variable from a Blueprint. Params: blueprint, name, graph?. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.RemoveVariable (Params: blueprint, name, graph?).", "epic_call_tool"),
  },
  epic_retarget_node_class: {
    epicSchema: S_epic_retarget_node_class,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.retarget_node_class", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.RetargetNodeClass", engines: ["5.9"], schema: S_epic_retarget_node_class__1, rename: {"old_class":"oldClass","new_class":"newClass"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Replaces a node's baked-in class reference from old_class to new_class in place. If the node's current class reference matches old_class it is replaced with new_class and the node is reconstructed so its pins reflect the new type. If the node already references new_class the call is a no-op. When a Blueprint is duplicated, nodes in the copied graph retain class references pointing to the original Blueprint. Calling this on each node with the original and duplicate classes retargets them without manual delete-recreate-rewire cycles. Handles cast, function call, event, and multicast delegate nodes. The Blueprint must be compiled after all retargeting is complete. Params: node, old_class, new_class. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.RetargetNodeClass (Params: node, oldClass, newClass).", "epic_call_tool"),
  },
  epic_schema: {
    epicSchema: S_epic_schema,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.Schema", engines: ["5.9"] },
    ...bp("unknown", "[Epic BlueprintToolsets.BlueprintDslToolset] Returns one node type's schema as a DSL form. Query unfamiliar node types before writing: (node `Type` (in (`Pin` Type) ...) ; data input pins (out (`Pin` Type) ...) ; data output pins (params (`Prop` Type [:over]) ...) ; writable properties, baked as :`Prop` value (determines `Prop` ...)) ; properties whose value changes the pin set (Enum A B ...) lists writable values. :over marks a property with an override input, supplied as :`Prop` (wire Source). Write an (Instanced /Script/Pkg.Base) property with a concrete class-headed value: (class:/Script/Pkg.Concrete :`Field` value ...). Empty sections are omitted. Available on UE 5.9; absent from UE 5.8. Params: assetPath, nodeType", "epic_call_tool"),
  },
  epic_set_comment_box_text: {
    epicSchema: S_epic_set_comment_box_text,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetCommentBoxText", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Sets the text on a comment box. Available on UE 5.9; absent from UE 5.8. Params: commentBox, text", "epic_call_tool"),
  },
  epic_set_create_event_function: {
    epicSchema: S_epic_set_create_event_function,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_create_event_function", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetCreateEventFunction", engines: ["5.9"], schema: S_epic_set_create_event_function__1, rename: {"function_name":"functionName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Binds a function to a Create Event node. Use list_compatible_event_functions to find valid function names. Params: node, function_name. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetCreateEventFunction (Params: node, functionName).", "epic_call_tool"),
  },
  epic_set_function_flags: {
    epicSchema: S_epic_set_function_flags,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetFunctionFlags", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Sets detail-panel settings on a Blueprint function. Only the fields set on Flags are applied; leave a field unset to keep the function's current value for that flag. The Blueprint must be recompiled for the changes to take effect. Available on UE 5.9; absent from UE 5.8. Params: graph, flags", "epic_call_tool"),
  },
  epic_set_node_note: {
    epicSchema: S_epic_set_node_note,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetNodeNote", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Sets the note text on a Blueprint node (its comment bubble). The bubble is made immediately visible in the graph editor when a non-empty note is set. Available on UE 5.9; absent from UE 5.8. Params: node, text", "epic_call_tool"),
  },
  epic_set_node_note_pinned: {
    epicSchema: S_epic_set_node_note_pinned,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetNodeNotePinned", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Sets whether a node's note bubble is pinned (always visible), vs. only shown on hover. Available on UE 5.9; absent from UE 5.8. Params: node, bPinned", "epic_call_tool"),
  },
  epic_set_node_position: {
    epicSchema: S_epic_set_node_position,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_node_position", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetNodePosition", engines: ["5.9"], schema: S_epic_set_node_position__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets a new position for the node. Params: node, pos. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetNodePosition (Params: node, pos).", "epic_call_tool"),
  },
  epic_set_parent: {
    epicSchema: S_epic_set_parent,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_parent", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetParent", engines: ["5.9"], schema: S_epic_set_parent__1, rename: {"parent_class":"parentClass"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Reparents a Blueprint to a new parent class. The Blueprint must be recompiled after reparenting. Params: blueprint, parent_class. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetParent (Params: blueprint, parentClass).", "epic_call_tool"),
  },
  epic_set_pin_value: {
    epicSchema: S_epic_set_pin_value,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_pin_value", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetPinValue", engines: ["5.9"], schema: S_epic_set_pin_value__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets the value of a Blueprint graph pin. Params: pin, value. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetPinValue (Params: pin, value).", "epic_call_tool"),
  },
  epic_set_variable_category: {
    epicSchema: S_epic_set_variable_category,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_category", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetVariableCategory", engines: ["5.9"], schema: S_epic_set_variable_category__1, rename: {"variable_name":"variableName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets the user-defined category on a Blueprint member variable. Categories group variables in the My Blueprint panel. Pass an empty string to reset to the default (the Blueprint's name). Params: blueprint, variable_name, category. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetVariableCategory (Params: blueprint, variableName, category, graph?).", "epic_call_tool"),
  },
  epic_set_variable_instance_editable: {
    epicSchema: S_epic_set_variable_instance_editable,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_instance_editable", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetVariableInstanceEditable", engines: ["5.9"], schema: S_epic_set_variable_instance_editable__1, rename: {"variable_name":"variableName","instance_editable":"bInstanceEditable"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets whether a member variable is editable per-instance on actors placed in the level. Params: blueprint, variable_name, instance_editable. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetVariableInstanceEditable (Params: blueprint, variableName, bInstanceEditable).", "epic_call_tool"),
  },
  epic_set_variable_read_only: {
    epicSchema: S_epic_set_variable_read_only,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetVariableReadOnly", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Sets whether a member variable is Blueprint Read Only. A read-only variable can still be read by graphs, but a setter can no longer be created for it. Local variables are not supported. The Blueprint must be recompiled for the change to take effect. Available on UE 5.9; absent from UE 5.8. Params: blueprint, variableName, bReadOnly", "epic_call_tool"),
  },
  epic_set_variable_replication: {
    epicSchema: S_epic_set_variable_replication,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_replication", engines: ["5.8"], successors: [{ toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetVariableReplication", engines: ["5.9"], schema: S_epic_set_variable_replication__1, rename: {"variable_name":"variableName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Sets the replication mode on a Blueprint member variable. RepNotify will automatically create an OnRep_ function on the Blueprint if one does not already exist. Params: blueprint, variable_name, replication. On UE 5.9 it calls BlueprintToolsets.BlueprintToolset.SetVariableReplication (Params: blueprint, variableName, replication).", "epic_call_tool"),
  },
  epic_set_variable_tooltip: {
    epicSchema: S_epic_set_variable_tooltip,
    epicTool: { toolset: "BlueprintToolsets.BlueprintToolset", name: "BlueprintToolsets.BlueprintToolset.SetVariableTooltip", engines: ["5.9"] },
    ...bp("mutate", "[Epic BlueprintToolsets.BlueprintToolset] Sets the tooltip shown for a Blueprint variable in the editor. Available on UE 5.9; absent from UE 5.8. Params: blueprint, variableName, tooltip, graph?", "epic_call_tool"),
  },
  epic_write: {
    epicSchema: S_epic_write,
    epicTool: { toolset: "BlueprintToolsets.BlueprintDslToolset", name: "BlueprintToolsets.BlueprintDslToolset.Write", engines: ["5.9"] },
    ...bp("unknown", "[Epic BlueprintToolsets.BlueprintDslToolset] Transactionally applies a DSL document and returns its canonical form and a new revision. A failure leaves the asset unchanged. Use Write for a complete document or a large structural change. Use Edit for a targeted change that can be expressed as exact replacements against text you just read. Top-level entries use upsert semantics: omitted entries remain and produce a warning. Delete an entry with a tombstone such as (event `Foo` :!delete). Deleting a missing entry only warns. Dsl is omitted when bDslOmitted is true because the submitted document is already canonical. Available on UE 5.9; absent from UE 5.8. Params: assetPath, dsl, scope?", "epic_call_tool"),
  },
  epic_write_graph_dsl: {
    epicSchema: S_epic_write_graph_dsl,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintTools", name: "editor_toolset.toolsets.blueprint.BlueprintTools.write_graph_dsl", engines: ["5.8"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintTools] Populates a Blueprint graph with nodes from a DSL script and compiles the Blueprint. Call get_graph_dsl_docs() for the full syntax reference and examples. Available on UE 5.8; absent from UE 5.9. Params: graph, code", "epic_call_tool"),
  },
  epic_write_graph_dsl__blueprint_dsl_tools: {
    epicSchema: S_epic_write_graph_dsl__blueprint_dsl_tools,
    epicTool: { toolset: "editor_toolset.toolsets.blueprint.BlueprintDslTools", name: "editor_toolset.toolsets.blueprint.BlueprintDslTools.write_graph_dsl", engines: ["5.9"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.blueprint.BlueprintDslTools] Makes a Blueprint graph match a DSL script and compiles the Blueprint. Statements whose DSL is unchanged from the graph's current state keep their existing nodes - positions, wires, and node ids are preserved - and only changed statements are rebuilt. Call get_graph_dsl_docs() for the full syntax reference and examples. Available on UE 5.9; absent from UE 5.8. Params: graph, code, entry_name?, force_rewrite?", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  allow_unresolved_references: z.boolean().optional().describe("When True, a node whose type cannot be matched to a unique creatable node type (its name is ambiguous across classes, or the member is not among the graph's av…"),
  asset_name: z.string().optional(),
  asset_path: z.string().optional().describe("Content path of the Blueprint (e.g. \"/Game/UI/WBP_Menu\")."),
  asset_type: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  assetName: z.string().optional().describe("The name of the asset in the folder."),
  assetPath: z.string().optional().describe("Content-browser path to the Blueprint."),
  assetType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The specific kind of Blueprint to make."),
  bEntryPointsOnly: z.boolean().optional().describe("If true, only returns entry point nodes - nodes with an exec output but no exec input (event nodes, function entry nodes, macro input tunnels, etc.)."),
  bIncludeInherited: z.boolean().optional().describe("If true, also includes interfaces inherited through the parent class chain, including natively-implemented ones."),
  bIncludePositions: z.boolean().optional().describe("Include :!pos metadata on each named bind's node."),
  bInputParam: z.boolean().optional().describe("True to add an input param, false to add an output."),
  bInstanceEditable: z.boolean().optional().describe("True to make the variable editable on instances, false to hide it."),
  blueprint: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  bPinned: z.boolean().optional().describe("True to pin the bubble, false to make it hover-only."),
  bPreserveFunctions: z.boolean().optional().describe("If true, the interface's implemented function graphs are kept as normal Blueprint functions instead of being deleted."),
  bReadOnly: z.boolean().optional().describe("True to make the variable Blueprint Read Only, false to make it writable."),
  bWarningsAsErrors: z.boolean().optional().describe("If true, treat compile warnings as errors too."),
  category: z.string().optional().describe("The new category text."),
  category_filter: z.string().optional(),
  categoryFilter: z.string().optional().describe("Substring to match against the node categories (case-insensitive). Unset matches all categories."),
  changelist: z.number().optional().describe("Perforce changelist number."),
  code: z.string().optional().describe("The S-expression DSL script to convert into Blueprint nodes."),
  commentBox: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The comment box to read."),
  commentText: z.string().optional().describe("The text displayed on the comment box."),
  component: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  container_type: z.string().optional(),
  context_pins: z.array(z.unknown()).optional(),
  contextPins: z.array(z.unknown()).optional().describe("If set, only returns categories with nodes whose pin types match those specified here."),
  declaring_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  declaringClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The class that should own the created node. Only set this to disambiguate nodes that share the same type_id."),
  dsl: z.string().optional().describe("Complete DSL document to apply to the selected scope."),
  edits: z.array(z.unknown()).optional().describe("Ordered list of text replacements to apply."),
  entry_name: z.string().optional().describe("If set, only that one entry (event, custom event, or function) is returned, rather than the whole graph. Empty (the default) returns every entry in the graph."),
  entry_points_only: z.boolean().optional(),
  event_name: z.string().optional(),
  eventName: z.string().optional().describe("The name of the delegate event to bind, e.g. \"OnComponentBeginOverlap\"."),
  flags: z.record(z.string(), z.unknown()).optional().describe("If set, the detail-panel flags applied to the function. Unset fields are left unchanged."),
  folder_path: z.string().optional(),
  folderPath: z.string().optional().describe("The path to the folder that will contain the asset."),
  force_rewrite: z.boolean().optional().describe("When True, every statement is rebuilt from the script even if unchanged, discarding existing node positions, reroute knots, and engine-inserted conversion node…"),
  fragment: z.boolean().optional().describe("True writes an embeddable block instead of a full page."),
  function_name: z.string().optional(),
  functionName: z.string().optional().describe("The name for the new function graph. Empty to generate a name."),
  graph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  graph_name: z.string().optional(),
  graphName: z.string().optional().describe("The name of the new function graph."),
  input_param: z.boolean().optional(),
  input_pin: z.record(z.string(), z.unknown()).optional(),
  inputPin: z.record(z.string(), z.unknown()).optional().describe("The FBlueprintPinID of the input pin to connect to."),
  instance_editable: z.boolean().optional(),
  interfaceClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The interface class to implement."),
  name: z.string().optional().describe("The name of the new event dispatcher."),
  new_asset_path: z.string().optional().describe("Content path to the new Blueprint."),
  new_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  new_revision: z.string().optional().describe("The later revision."),
  newAssetPath: z.string().optional().describe("Content-browser path to the Blueprint on the new side of the diff."),
  newClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The replacement class."),
  newGraph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The graph to look for changes in."),
  node: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  node_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  nodeClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("If set, only returns nodes that are instances of this class or its subclasses."),
  nodes: z.array(z.unknown()).optional().describe("Nodes to wrap. When unset or empty, a standalone box is created."),
  nodeType: z.string().optional().describe("Node type as it appears as a DSL call head; backticks are optional."),
  object_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  objectClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The class of object this param will reference."),
  old_asset_path: z.string().optional().describe("Content path to the old Blueprint."),
  old_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  old_revision: z.string().optional().describe("The earlier revision, as listed by SourceControlHelpers.ListAssetRevisions."),
  oldAssetPath: z.string().optional().describe("Content-browser path to the Blueprint on the old side of the diff."),
  oldClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The class currently baked into the node."),
  oldGraph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The baseline graph."),
  options: z.record(z.string(), z.unknown()).optional().describe("Container type and passing-mode settings for the parameter."),
  output_dir: z.string().optional().describe("Absolute path of the folder to write the HTML files into."),
  output_path: z.string().optional().describe("Absolute path of the HTML file to write."),
  output_pin: z.record(z.string(), z.unknown()).optional(),
  outputPin: z.record(z.string(), z.unknown()).optional().describe("The FBlueprintPinID of the output pin to connect from."),
  padding: z.number().optional().describe("Space between the wrapped nodes and the box border, when nodes are given."),
  param_name: z.string().optional(),
  param_type: z.string().optional(),
  paramName: z.string().optional().describe("The name of the new parameter."),
  paramType: z.string().optional().describe("The parameter type as a string (see above)."),
  parent_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  parentClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The new parent class."),
  pin: z.record(z.string(), z.unknown()).optional().describe("The FBlueprintPinID of the pin to read."),
  pinnedNodes: z.array(z.unknown()).optional().describe("Nodes that keep their current position. They steer placement and are avoided, but do not define columns."),
  pos: z.record(z.string(), z.unknown()).optional().describe("The x, y position of the created node."),
  position: z.record(z.string(), z.unknown()).optional().describe("Position of the new node in the event graph."),
  query: z.string().optional().describe("Text to look for, e.g. \"print string\"."),
  rect: z.record(z.string(), z.unknown()).optional().describe("Position and size of a standalone box (min is the top-left corner). Required when no nodes are given."),
  replication: z.string().optional().describe("Replication mode (NONE, REPLICATED, or REP_NOTIFY)."),
  restrictToIslandOf: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("When set, only the connected component containing this node is arranged; everything else is left alone."),
  revision: z.string().optional().describe("Revision token from the most recent Read/Write/Edit on this asset."),
  scope: z.string().optional().describe("Sub-document selector from the operation that returned Revision."),
  struct_type: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  structType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The struct type for this parameter."),
  text: z.string().optional().describe("The new comment box text."),
  title: z.string().optional().describe("Case-insensitive substring to match against the node's displayed title. Empty matches any title."),
  tooltip: z.string().optional().describe("The new tooltip text. Pass empty to clear."),
  type_id: z.string().optional(),
  type_id_filter: z.string().optional(),
  type_name: z.string().optional(),
  typeId: z.string().optional().describe("A palette type id (Category|Name, from FindNodeTypes), not a Graph DSL reference, e.g. \"Development|PrintString\", \"Utilities|Operators|Add\", a macro like \"Util…"),
  typeIdFilter: z.string().optional().describe("Substring to match against the node type_id (case-insensitive)."),
  typeName: z.string().optional().describe("The variable type as a string (see above)."),
  value: z.string().optional().describe("The value to assign as a string; format depends on the pin's data type (e.g. \"42\", \"3.14\", \"true\", \"Hello World\", \"/Game/Path/To/Asset.Asset\")."),
  variable_name: z.string().optional(),
  variableName: z.string().optional().describe("The name of the variable to query."),
  warnings_as_errors: z.boolean().optional(),
};
