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

const S_epic_add_enumerator = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}},"displayName":{"type":"string"}},"required":["enum","displayName"]} as const;
const S_epic_add_gradient = {"properties":{"atlas":{"type":"object","properties":{"refPath":{}}},"gradient":{"type":"object","properties":{"refPath":{}}}},"required":["atlas","gradient"]} as const;
const S_epic_add_input_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"expressionClass":{"type":"object","properties":{"refPath":{}}},"paramName":{"type":"string"},"posX":{"type":"integer"},"posY":{"type":"integer"}},"required":["graph","expressionClass","paramName"]} as const;
const S_epic_add_key = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"key":{"type":"object","properties":{"time":{"type":"number"},"value":{"type":"number"}}}},"required":["curve_table","row_name","key"]} as const;
const S_epic_add_key__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}},"rowName":{"type":"string"},"key":{"type":"object","properties":{"time":{"type":"number"},"value":{"type":"number"}}}},"required":["curveTable","rowName","key"]} as const;
const S_epic_add_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"expressionClass":{"type":"object","properties":{"refPath":{}}},"posX":{"type":"integer"},"posY":{"type":"integer"}},"required":["graph","expressionClass"]} as const;
const S_epic_add_object_variable = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"objectClass":{"type":"object","properties":{"refPath":{}}},"spec":{"type":"object","properties":{"name":{"type":"string"},"elementType":{"type":"string"},"containerType":{"type":"string","enum":["None","Array","Set","Map"]},"defaultValue":{"type":"string"},"tooltip":{"type":"string"},"bEditableOnInstance":{"type":"boolean"},"bSaveGame":{"type":"boolean"}}}},"required":["struct","objectClass","spec"]} as const;
const S_epic_add_output = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"outputName":{"type":"string"},"posX":{"type":"integer"},"posY":{"type":"integer"}},"required":["graph","outputName"]} as const;
const S_epic_add_row = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"default_value":{"type":"number"}},"required":["curve_table","row_name"]} as const;
const S_epic_add_row__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}},"rowName":{"type":"string"},"defaultValue":{"type":"number"}},"required":["curveTable","rowName"]} as const;
const S_epic_add_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"row_names":{"type":"array","items":{"type":"string"}}},"required":["data_table","row_names"]} as const;
const S_epic_add_rows__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}},"rowNames":{"type":"array","items":{"type":"string"}}},"required":["dataTable","rowNames"]} as const;
const S_epic_add_struct_variable = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"structType":{"type":"object","properties":{"refPath":{}}},"spec":{"type":"object","properties":{"name":{"type":"string"},"elementType":{"type":"string"},"containerType":{"type":"string","enum":["None","Array","Set","Map"]},"defaultValue":{"type":"string"},"tooltip":{"type":"string"},"bEditableOnInstance":{"type":"boolean"},"bSaveGame":{"type":"boolean"}}}},"required":["struct","structType","spec"]} as const;
const S_epic_add_user_asset_tags = {"properties":{"assetPath":{"type":"string"},"tags":{"type":"array","items":{"type":"string"}}},"required":["assetPath","tags"]} as const;
const S_epic_add_variable = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"typeName":{"type":"string"},"spec":{"type":"object","properties":{"name":{"type":"string"},"elementType":{"type":"string"},"containerType":{"type":"string","enum":["None","Array","Set","Map"]},"defaultValue":{"type":"string"},"tooltip":{"type":"string"},"bEditableOnInstance":{"type":"boolean"},"bSaveGame":{"type":"boolean"}}}},"required":["struct","typeName","spec"]} as const;
const S_epic_append_box = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"dimensions":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["component","transform","dimensions"]} as const;
const S_epic_append_cone = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"base_radius":{"type":"number"},"top_radius":{"type":"number"},"height":{"type":"number"}},"required":["component","transform","base_radius","top_radius","height"]} as const;
const S_epic_append_cylinder = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"radius":{"type":"number"},"height":{"type":"number"}},"required":["component","transform","radius","height"]} as const;
const S_epic_append_disc = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"radius":{"type":"number"}},"required":["component","transform","radius"]} as const;
const S_epic_append_sphere = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"radius":{"type":"number"}},"required":["component","transform","radius"]} as const;
const S_epic_append_torus = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"major_radius":{"type":"number"},"minor_radius":{"type":"number"}},"required":["component","transform","major_radius","minor_radius"]} as const;
const S_epic_assign_clothing_to_section = {"properties":{"skeletalMeshPath":{"type":"string"},"clothingAssetName":{"type":"string"},"lodIndex":{"type":"integer"},"sectionIndex":{"type":"integer"},"clothingLodIndex":{"type":"integer"}},"required":["skeletalMeshPath","clothingAssetName","lodIndex","sectionIndex","clothingLodIndex"]} as const;
const S_epic_can_edit_asset = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_connect_pins = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPin":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPin":{"type":"string"}},"required":["graph","fromNode","fromPin","toNode","toPin"]} as const;
const S_epic_convert_clothing_asset_common_to_chaos_cloth_asset = {"properties":{"skeletalMeshPath":{"type":"string"},"clothingAssetName":{"type":"string"},"outputPackagePath":{"type":"string"},"assetName":{"type":"string"}},"required":["skeletalMeshPath","clothingAssetName","outputPackagePath","assetName"]} as const;
const S_epic_create = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create__data_asset_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"asset_type":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","asset_type"]} as const;
const S_epic_create__data_asset_toolset = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"assetType":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","assetType"]} as const;
const S_epic_create__data_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"schema":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","schema"]} as const;
const S_epic_create__data_table_tools__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"schema":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","schema"]} as const;
const S_epic_create__string_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create__string_table_tools__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create__user_defined_enum_toolset = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create__user_defined_struct_toolset = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_atlas = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"textureSize":{"type":"integer"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_clothing_asset = {"properties":{"skeletalMeshPath":{"type":"string"},"chaosClothAssetPath":{"type":"string"}},"required":["skeletalMeshPath","chaosClothAssetPath"]} as const;
const S_epic_create_folder = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_create_folder__1 = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_create_gradient = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_texture_graph = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_delete = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_delete__1 = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_diff = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_diff__data_asset_toolset = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_diff__data_table_toolset = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_diff__string_table_toolset = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_disconnect_pins = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"fromNode":{"type":"object","properties":{"refPath":{}}},"fromPin":{"type":"string"},"toNode":{"type":"object","properties":{"refPath":{}}},"toPin":{"type":"string"}},"required":["graph","fromNode","fromPin","toNode","toPin"]} as const;
const S_epic_duplicate = {"properties":{"path":{"type":"string"},"new_path":{"type":"string"}},"required":["path","new_path"]} as const;
const S_epic_duplicate__1 = {"properties":{"path":{"type":"string"},"newPath":{"type":"string"}},"required":["path","newPath"]} as const;
const S_epic_exists = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_exists__1 = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_export_png = {"properties":{"texture":{"type":"object","properties":{"refPath":{}}},"filePath":{"type":"string"}},"required":["texture","filePath"]} as const;
const S_epic_export_texture_graph = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"outputFolder":{"type":"string"}},"required":["graph","outputFolder"]} as const;
const S_epic_find_assets = {"properties":{"folder_path":{"type":"string"},"name":{"type":"string"},"asset_type":{"type":"object","properties":{"refPath":{}}},"recursive":{"type":"boolean"},"tags":{"type":"object"}},"required":["folder_path","name"]} as const;
const S_epic_find_assets__1 = {"properties":{"filter":{"type":"object","properties":{"folderPath":{"type":"string"},"name":{"type":"string"},"assetType":{"type":"object","properties":{"refPath":{}}},"bRecursive":{"type":"boolean"},"tags":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"},"value":{"type":"string"}}}},"maxResults":{"type":"integer"}}}}} as const;
const S_epic_find_similar = {"properties":{"assetPath":{"type":"object","properties":{"refPath":{}}},"classFilter":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"pathRegexes":{"type":"array","items":{"type":"string"}},"k":{"type":"integer"}},"required":["assetPath","classFilter","pathRegexes"]} as const;
const S_epic_fix_up_redirectors = {"properties":{"path":{"type":"string"},"bDeleteRedirectors":{"type":"boolean"}},"required":["path"]} as const;
const S_epic_generate_convex_collisions = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"hull_count":{"type":"integer"},"max_hull_verts":{"type":"integer"},"hull_precision":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_generate_convex_collisions__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"hullCount":{"type":"integer"},"maxHullVerts":{"type":"integer"},"hullPrecision":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_generate_lods = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"triangle_percents":{"type":"array","items":{"type":"number"}}},"required":["mesh","triangle_percents"]} as const;
const S_epic_generate_lods__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"trianglePercents":{"type":"array","items":{"type":"number"}}},"required":["mesh","trianglePercents"]} as const;
const S_epic_get_alpha_stops = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}}},"required":["gradient"]} as const;
const S_epic_get_asset_class = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_asset_class__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_asset_content_roots = {"properties":{"contains":{"type":"string"},"bWritable":{"type":"boolean"}}} as const;
const S_epic_get_asset_tags = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_asset_tags__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_bounds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_bounds__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_channel_keys = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}},"channel":{"type":"string","enum":["Red","Green","Blue","Alpha"]}},"required":["gradient","channel"]} as const;
const S_epic_get_collision_complexity = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_color_stops = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}}},"required":["gradient"]} as const;
const S_epic_get_dependencies = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_dependencies__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_entry = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"}},"required":["string_table","key"]} as const;
const S_epic_get_entry__1 = {"properties":{"stringTable":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"}},"required":["stringTable","key"]} as const;
const S_epic_get_enumerators = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}}},"required":["enum"]} as const;
const S_epic_get_gradients = {"properties":{"atlas":{"type":"object","properties":{"refPath":{}}}},"required":["atlas"]} as const;
const S_epic_get_graph = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_graph_issues = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_get_items = {"properties":{"registryName":{"type":"string"},"itemNames":{"type":"array","items":{"type":"string"}}},"required":["registryName","itemNames"]} as const;
const S_epic_get_keys = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"}},"required":["curve_table","row_name"]} as const;
const S_epic_get_keys__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}},"rowName":{"type":"string"}},"required":["curveTable","rowName"]} as const;
const S_epic_get_live_cloth_simulation_stats = {"properties":{}} as const;
const S_epic_get_lod_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_lod_count__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_lod_thresholds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_lod_thresholds__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"}},"required":["mesh","slot_name"]} as const;
const S_epic_get_material__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"}},"required":["mesh","slotName"]} as const;
const S_epic_get_material_slots = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_material_slots__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_metadata_tags = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_metadata_tags__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_namespace = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}}},"required":["string_table"]} as const;
const S_epic_get_namespace__1 = {"properties":{"stringTable":{"type":"object","properties":{"refPath":{}}}},"required":["stringTable"]} as const;
const S_epic_get_plugin_content_paths = {"properties":{"include_engine":{"type":"boolean"}}} as const;
const S_epic_get_quality_level_min_lod = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_quality_level_min_lod_default = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_referencers = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_referencers__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_registry_info = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_get_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"row_names":{"type":"array","items":{"type":"string"}}},"required":["data_table","row_names"]} as const;
const S_epic_get_rows__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}},"rowNames":{"type":"array","items":{"type":"string"}}},"required":["dataTable","rowNames"]} as const;
const S_epic_get_schema = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_get_schema__data_table_tools = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}}},"required":["data_table"]} as const;
const S_epic_get_schema__data_table_tools__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}}},"required":["dataTable"]} as const;
const S_epic_get_section_clothing = {"properties":{"skeletalMeshPath":{"type":"string"},"lodIndex":{"type":"integer"},"sectionIndex":{"type":"integer"}},"required":["skeletalMeshPath","lodIndex","sectionIndex"]} as const;
const S_epic_get_size = {"properties":{"texture":{"type":"object","properties":{"refPath":{}}}},"required":["texture"]} as const;
const S_epic_get_size__1 = {"properties":{"texture":{"type":"object","properties":{"refPath":{}}}},"required":["texture"]} as const;
const S_epic_get_table_id = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}}},"required":["string_table"]} as const;
const S_epic_get_table_id__1 = {"properties":{"stringTable":{"type":"object","properties":{"refPath":{}}}},"required":["stringTable"]} as const;
const S_epic_get_tooltip = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}}},"required":["struct"]} as const;
const S_epic_get_triangle_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_triangle_count__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lodIndex":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_user_asset_tags = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_vertex_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_vertex_count__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lodIndex":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_virtual_paths = {"properties":{"paths":{"type":"array","items":{"type":"string"}}},"required":["paths"]} as const;
const S_epic_import_file = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"interp_mode":{"type":"string","enum":["RCIM_Linear","RCIM_Constant","RCIM_Cubic","RCIM_None"]}},"required":["folder_path","asset_name","source_file","interp_mode"]} as const;
const S_epic_import_file__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"interpMode":{"type":"string","enum":["RCIM_Linear","RCIM_Constant","RCIM_Cubic"]},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile","interpMode"]} as const;
const S_epic_import_file__data_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"schema":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","source_file","schema"]} as const;
const S_epic_import_file__data_table_tools__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"schema":{"type":"object","properties":{"refPath":{}}},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile","schema"]} as const;
const S_epic_import_file__static_mesh_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"import_materials":{"type":"boolean"},"import_textures":{"type":"boolean"},"combine_meshes":{"type":"boolean"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_import_file__static_mesh_tools__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"bImportMaterials":{"type":"boolean"},"bImportTextures":{"type":"boolean"},"bCombineMeshes":{"type":"boolean"},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile"]} as const;
const S_epic_import_file__string_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_import_file__string_table_tools__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile"]} as const;
const S_epic_import_file__texture_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_import_file__texture_tools__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile"]} as const;
const S_epic_is_bitflags = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}}},"required":["enum"]} as const;
const S_epic_is_checked_out = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_is_dirty = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_is_dirty__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_is_nanite_enabled = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_is_nanite_mesh = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_list_clothing_assets = {"properties":{"skeletalMeshPath":{"type":"string"}},"required":["skeletalMeshPath"]} as const;
const S_epic_list_data_sources = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_list_folders = {"properties":{"root_path":{"type":"string"},"recursive":{"type":"boolean"}},"required":["root_path"]} as const;
const S_epic_list_folders__1 = {"properties":{"rootPath":{"type":"string"},"bRecursive":{"type":"boolean"}},"required":["rootPath"]} as const;
const S_epic_list_input_params = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_list_items = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_list_keys = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}}},"required":["string_table"]} as const;
const S_epic_list_keys__1 = {"properties":{"stringTable":{"type":"object","properties":{"refPath":{}}}},"required":["stringTable"]} as const;
const S_epic_list_node_types = {"properties":{}} as const;
const S_epic_list_outputs = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}}},"required":["graph"]} as const;
const S_epic_list_registries = {"properties":{"structFilter":{"type":"object","properties":{"refPath":{}}}}} as const;
const S_epic_list_rows = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}}},"required":["curve_table"]} as const;
const S_epic_list_rows__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}}},"required":["curveTable"]} as const;
const S_epic_list_rows__data_table_tools = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}}},"required":["data_table"]} as const;
const S_epic_list_rows__data_table_tools__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}}},"required":["dataTable"]} as const;
const S_epic_list_runtime_sources = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_list_variables = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}}},"required":["struct"]} as const;
const S_epic_load_asset = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_load_asset__1 = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_move = {"properties":{"path":{"type":"string"},"new_path":{"type":"string"}},"required":["path","new_path"]} as const;
const S_epic_move__1 = {"properties":{"path":{"type":"string"},"newPath":{"type":"string"}},"required":["path","newPath"]} as const;
const S_epic_move_enumerator = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}},"fromIndex":{"type":"integer"},"toIndex":{"type":"integer"}},"required":["enum","fromIndex","toIndex"]} as const;
const S_epic_move_variable = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"relativeToName":{"type":"string"},"bAbove":{"type":"boolean"}},"required":["struct","variableName","relativeToName","bAbove"]} as const;
const S_epic_read_file = {"properties":{"file_path":{"type":"string"}},"required":["file_path"]} as const;
const S_epic_read_texture = {"properties":{"texture":{"type":"object","properties":{"refPath":{}}}},"required":["texture"]} as const;
const S_epic_reload_asset = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_remove_clothing_from_section = {"properties":{"skeletalMeshPath":{"type":"string"},"lodIndex":{"type":"integer"},"sectionIndex":{"type":"integer"}},"required":["skeletalMeshPath","lodIndex","sectionIndex"]} as const;
const S_epic_remove_collisions = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_remove_collisions__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_remove_entry = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"}},"required":["string_table","key"]} as const;
const S_epic_remove_entry__1 = {"properties":{"stringTable":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"}},"required":["stringTable","key"]} as const;
const S_epic_remove_enumerator = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["enum","index"]} as const;
const S_epic_remove_gradient = {"properties":{"atlas":{"type":"object","properties":{"refPath":{}}},"gradient":{"type":"object","properties":{"refPath":{}}}},"required":["atlas","gradient"]} as const;
const S_epic_remove_input_param = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"paramName":{"type":"string"}},"required":["graph","paramName"]} as const;
const S_epic_remove_lods = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_remove_lods__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_remove_node = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["graph","node"]} as const;
const S_epic_remove_output = {"properties":{"graph":{"type":"object","properties":{"refPath":{}}},"outputName":{"type":"string"}},"required":["graph","outputName"]} as const;
const S_epic_remove_row = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"}},"required":["curve_table","row_name"]} as const;
const S_epic_remove_row__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}},"rowName":{"type":"string"}},"required":["curveTable","rowName"]} as const;
const S_epic_remove_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"row_names":{"type":"array","items":{"type":"string"}}},"required":["data_table","row_names"]} as const;
const S_epic_remove_rows__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}},"rowNames":{"type":"array","items":{"type":"string"}}},"required":["dataTable","rowNames"]} as const;
const S_epic_remove_user_asset_tags = {"properties":{"assetPath":{"type":"string"},"tags":{"type":"array","items":{"type":"string"}}},"required":["assetPath","tags"]} as const;
const S_epic_remove_variable = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["struct","variableName"]} as const;
const S_epic_rename_row = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"new_row_name":{"type":"string"}},"required":["curve_table","row_name","new_row_name"]} as const;
const S_epic_rename_row__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}},"rowName":{"type":"string"},"newRowName":{"type":"string"}},"required":["curveTable","rowName","newRowName"]} as const;
const S_epic_rename_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"renames":{"type":"object"}},"required":["data_table","renames"]} as const;
const S_epic_rename_rows__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}},"renames":{"type":"object"}},"required":["dataTable","renames"]} as const;
const S_epic_rename_variable = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"oldName":{"type":"string"},"newName":{"type":"string"}},"required":["struct","oldName","newName"]} as const;
const S_epic_sample_gradient = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}},"time":{"type":"number"}},"required":["gradient","time"]} as const;
const S_epic_save_asset = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_save_assets = {"properties":{"asset_paths":{"type":"array","items":{"type":"string"}}},"required":["asset_paths"]} as const;
const S_epic_search = {"properties":{"query":{"type":"string"},"classFilter":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"pathRegexes":{"type":"array","items":{"type":"string"}},"k":{"type":"integer"}},"required":["query","classFilter","pathRegexes"]} as const;
const S_epic_search_row_structs = {"properties":{"struct_name":{"type":"string"}}} as const;
const S_epic_search_row_structs__1 = {"properties":{"structName":{"type":"string"}}} as const;
const S_epic_set_alpha_stops = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}},"stops":{"type":"array","items":{"type":"object","properties":{"time":{"type":"number"},"alpha":{"type":"number"},"interpMode":{"type":"string","enum":["RCIM_Linear","RCIM_Constant","RCIM_Cubic"]}}}}},"required":["gradient","stops"]} as const;
const S_epic_set_bitflags = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}},"bEnabled":{"type":"boolean"}},"required":["enum","bEnabled"]} as const;
const S_epic_set_channel_keys = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}},"channel":{"type":"string","enum":["Red","Green","Blue","Alpha"]},"keys":{"type":"array","items":{"type":"object","properties":{"interpMode":{"type":"string","enum":["RCIM_Linear","RCIM_Constant","RCIM_Cubic"]},"tangentMode":{"type":"string","enum":["RCTM_Auto","RCTM_User","RCTM_Break","RCTM_SmartAuto"]},"tangentWeightMode":{"type":"string","enum":["RCTWM_WeightedNone","RCTWM_WeightedArrive","RCTWM_WeightedLeave","RCTWM_WeightedBoth"]},"time":{"type":"number"},"value":{"type":"number"},"arriveTangent":{"type":"number"},"arriveTangentWeight":{"type":"number"},"leaveTangent":{"type":"number"},"leaveTangentWeight":{"type":"number"}}}}},"required":["gradient","channel","keys"]} as const;
const S_epic_set_collision_complexity = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"complexity":{"type":"string","enum":["CTF_UseDefault","CTF_UseSimpleAndComplex","CTF_UseSimpleAsComplex","CTF_UseComplexAsSimple"]}},"required":["mesh","complexity"]} as const;
const S_epic_set_color_stops = {"properties":{"gradient":{"type":"object","properties":{"refPath":{}}},"stops":{"type":"array","items":{"type":"object","properties":{"time":{"type":"number"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"interpMode":{"type":"string","enum":["RCIM_Linear","RCIM_Constant","RCIM_Cubic"]}}}}},"required":["gradient","stops"]} as const;
const S_epic_set_display_name = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"},"displayName":{"type":"string"}},"required":["enum","index","displayName"]} as const;
const S_epic_set_entry = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"},"value":{"type":"string"}},"required":["string_table","key","value"]} as const;
const S_epic_set_entry__1 = {"properties":{"stringTable":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"},"value":{"type":"string"}},"required":["stringTable","key","value"]} as const;
const S_epic_set_gradients = {"properties":{"atlas":{"type":"object","properties":{"refPath":{}}},"gradients":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["atlas","gradients"]} as const;
const S_epic_set_keys = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"keys":{"type":"array","items":{"type":"object","properties":{"time":{"type":"number"},"value":{"type":"number"}}}}},"required":["curve_table","row_name","keys"]} as const;
const S_epic_set_keys__1 = {"properties":{"curveTable":{"type":"object","properties":{"refPath":{}}},"rowName":{"type":"string"},"keys":{"type":"array","items":{"type":"object","properties":{"time":{"type":"number"},"value":{"type":"number"}}}}},"required":["curveTable","rowName","keys"]} as const;
const S_epic_set_lod_thresholds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"thresholds":{"type":"array","items":{"type":"number"}}},"required":["mesh","thresholds"]} as const;
const S_epic_set_lod_thresholds__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"thresholds":{"type":"array","items":{"type":"number"}}},"required":["mesh","thresholds"]} as const;
const S_epic_set_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"},"material":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","slot_name","material"]} as const;
const S_epic_set_material__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"},"material":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","slotName","material"]} as const;
const S_epic_set_nanite_enabled = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"enabled":{"type":"boolean"}},"required":["mesh","enabled"]} as const;
const S_epic_set_nanite_enabled__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"bEnabled":{"type":"boolean"}},"required":["mesh","bEnabled"]} as const;
const S_epic_set_node_alias = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"newAlias":{"type":"string"}},"required":["node","newAlias"]} as const;
const S_epic_set_output_settings = {"properties":{"outputNode":{"type":"object","properties":{"refPath":{}}},"baseName":{"type":"string"},"folderPath":{"type":"string"},"presetType":{"type":"string","enum":["None","Diffuse","Emissive","FX","Normal","MaskComp","Specular","Tangent"]}},"required":["outputNode","baseName","folderPath","presetType"]} as const;
const S_epic_set_pin_value = {"properties":{"node":{"type":"object","properties":{"refPath":{}}},"pinName":{"type":"string"},"value":{"type":"string"}},"required":["node","pinName","value"]} as const;
const S_epic_set_quality_level_min_lod = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"overrides":{"type":"object"}},"required":["mesh","overrides"]} as const;
const S_epic_set_quality_level_min_lod_default = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"default":{"type":"integer"}},"required":["mesh","default"]} as const;
const S_epic_set_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"values":{"type":"string"}},"required":["data_table","values"]} as const;
const S_epic_set_rows__1 = {"properties":{"dataTable":{"type":"object","properties":{"refPath":{}}},"values":{"type":"string"}},"required":["dataTable","values"]} as const;
const S_epic_set_tooltip = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"tooltip":{"type":"string"}},"required":["struct","tooltip"]} as const;
const S_epic_set_variable_default_value = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"defaultValue":{"type":"string"}},"required":["struct","variableName","defaultValue"]} as const;
const S_epic_set_variable_editable_on_instance = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"bEditableOnInstance":{"type":"boolean"}},"required":["struct","variableName","bEditableOnInstance"]} as const;
const S_epic_set_variable_save_game = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"bSaveGame":{"type":"boolean"}},"required":["struct","variableName","bSaveGame"]} as const;
const S_epic_set_variable_tooltip = {"properties":{"struct":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"tooltip":{"type":"string"}},"required":["struct","variableName","tooltip"]} as const;
const S_epic_update_metadata_tags = {"properties":{"asset_path":{"type":"string"},"set_tags":{"type":"object"},"remove_tags":{"type":"array","items":{"type":"string"}}},"required":["asset_path"]} as const;
const S_epic_update_metadata_tags__1 = {"properties":{"assetPath":{"type":"string"},"setTags":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"},"value":{"type":"string"}}}},"removeTags":{"type":"array","items":{"type":"string"}}},"required":["assetPath"]} as const;
const S_epic_validate_assets = {"properties":{"assetPaths":{"type":"array","items":{"type":"string"}}},"required":["assetPaths"]} as const;
const S_epic_write_file = {"properties":{"file_path":{"type":"string"},"content":{"type":"string"}},"required":["file_path","content"]} as const;

/** 165 wrapped engine tools routed to the `asset` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_enumerator: {
    epicSchema: S_epic_add_enumerator,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.AddEnumerator", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedEnumToolset] Appends a new enumerator to an enum. Available on UE 5.9; absent from UE 5.8. Params: enum, displayName", "epic_call_tool"),
  },
  epic_add_gradient: {
    epicSchema: S_epic_add_gradient,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.AddGradient", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Appends a gradient to an atlas as a new final row. Available on UE 5.9; absent from UE 5.8. Params: atlas, gradient", "epic_call_tool"),
  },
  epic_add_input_param: {
    epicSchema: S_epic_add_input_param,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.AddInputParam", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Adds an input-parameter node (a graph-level parameter such as Scalar, Color, or Vector) and returns it. The parameter is exposed on the graph automatically. If ParamName is set it becomes the parameter's name, uniquified against the graph's existing parameters. Available on UE 5.9; absent from UE 5.8. Params: graph, expressionClass, paramName, posX?, posY?", "epic_call_tool"),
  },
  epic_add_key: {
    epicSchema: S_epic_add_key,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.add_key", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.AddKey", engines: ["5.9"], schema: S_epic_add_key__1, rename: {"curve_table":"curveTable","row_name":"rowName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Adds a key to a row. Params: curve_table, row_name, key. On UE 5.9 it calls EditorToolset.CurveTableToolset.AddKey (Params: curveTable, rowName, key).", "epic_call_tool"),
  },
  epic_add_node: {
    epicSchema: S_epic_add_node,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.AddNode", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Adds a node of the given expression type to the graph and returns the new node. Input-parameter and Output expression types are not accepted here; they are created with the dedicated input-parameter and output tools. Available on UE 5.9; absent from UE 5.8. Params: graph, expressionClass, posX?, posY?", "epic_call_tool"),
  },
  epic_add_object_variable: {
    epicSchema: S_epic_add_object_variable,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.AddObjectVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Adds a UObject-reference variable, applying all fields of Spec atomically. Available on UE 5.9; absent from UE 5.8. Params: struct, objectClass, spec", "epic_call_tool"),
  },
  epic_add_output: {
    epicSchema: S_epic_add_output,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.AddOutput", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Adds an Output node and returns it. The output is exposed on the graph automatically. If OutputName is set it becomes the output's name (and its export filename), uniquified against the graph's existing outputs. Configure the output further with the output-settings tool. Available on UE 5.9; absent from UE 5.8. Params: graph, outputName, posX?, posY?", "epic_call_tool"),
  },
  epic_add_row: {
    epicSchema: S_epic_add_row,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.add_row", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.AddRow", engines: ["5.9"], schema: S_epic_add_row__1, rename: {"curve_table":"curveTable","row_name":"rowName","default_value":"defaultValue"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Adds a new row to the curve table with an optional default value. Params: curve_table, row_name, default_value?. On UE 5.9 it calls EditorToolset.CurveTableToolset.AddRow (Params: curveTable, rowName, defaultValue?).", "epic_call_tool"),
  },
  epic_add_rows: {
    epicSchema: S_epic_add_rows,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.add_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.AddRows", engines: ["5.9"], schema: S_epic_add_rows__1, rename: {"data_table":"dataTable","row_names":"rowNames"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Adds new rows with default values to the data table. Params: data_table, row_names. On UE 5.9 it calls EditorToolset.DataTableToolset.AddRows (Params: dataTable, rowNames).", "epic_call_tool"),
  },
  epic_add_struct_variable: {
    epicSchema: S_epic_add_struct_variable,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.AddStructVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Adds a variable of an arbitrary script-struct type, applying all fields of Spec atomically. Available on UE 5.9; absent from UE 5.8. Params: struct, structType, spec", "epic_call_tool"),
  },
  epic_add_user_asset_tags: {
    epicSchema: S_epic_add_user_asset_tags,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.AddUserAssetTags", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.AssetToolset] Adds the given User Asset Tags to an asset. Pass tag names without the UAT. prefix; the prefix is added internally. Available on UE 5.9; absent from UE 5.8. Params: assetPath, tags", "epic_call_tool"),
  },
  epic_add_variable: {
    epicSchema: S_epic_add_variable,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.AddVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Adds a variable of a primitive or basic struct type, applying all fields of Spec atomically. Supported TypeName values (case-insensitive): bool, int, int64, byte, float, double, name, string, text, Vector, Rotator, Transform, Vector2D, LinearColor. Available on UE 5.9; absent from UE 5.8. Params: struct, typeName, spec", "epic_call_tool"),
  },
  epic_append_box: {
    epicSchema: S_epic_append_box,
    epicTool: { toolset: "geometry_toolset.geometry_ops.GeometryTools", name: "geometry_toolset.geometry_ops.GeometryTools.append_box", engines: ["5.9"] },
    ...bp("mutate", "[Epic geometry_toolset.geometry_ops.GeometryTools] Appends a box of the given dimensions to the component's mesh. Available on UE 5.9; absent from UE 5.8. Params: component, transform, dimensions", "epic_call_tool"),
  },
  epic_append_cone: {
    epicSchema: S_epic_append_cone,
    epicTool: { toolset: "geometry_toolset.geometry_ops.GeometryTools", name: "geometry_toolset.geometry_ops.GeometryTools.append_cone", engines: ["5.9"] },
    ...bp("mutate", "[Epic geometry_toolset.geometry_ops.GeometryTools] Appends a cone (or truncated cone) to the component's mesh. Available on UE 5.9; absent from UE 5.8. Params: component, transform, base_radius, top_radius, height", "epic_call_tool"),
  },
  epic_append_cylinder: {
    epicSchema: S_epic_append_cylinder,
    epicTool: { toolset: "geometry_toolset.geometry_ops.GeometryTools", name: "geometry_toolset.geometry_ops.GeometryTools.append_cylinder", engines: ["5.9"] },
    ...bp("mutate", "[Epic geometry_toolset.geometry_ops.GeometryTools] Appends a cylinder of the given radius and height to the component's mesh. Available on UE 5.9; absent from UE 5.8. Params: component, transform, radius, height", "epic_call_tool"),
  },
  epic_append_disc: {
    epicSchema: S_epic_append_disc,
    epicTool: { toolset: "geometry_toolset.geometry_ops.GeometryTools", name: "geometry_toolset.geometry_ops.GeometryTools.append_disc", engines: ["5.9"] },
    ...bp("mutate", "[Epic geometry_toolset.geometry_ops.GeometryTools] Appends a flat disc to the component's mesh. Available on UE 5.9; absent from UE 5.8. Params: component, transform, radius", "epic_call_tool"),
  },
  epic_append_sphere: {
    epicSchema: S_epic_append_sphere,
    epicTool: { toolset: "geometry_toolset.geometry_ops.GeometryTools", name: "geometry_toolset.geometry_ops.GeometryTools.append_sphere", engines: ["5.9"] },
    ...bp("mutate", "[Epic geometry_toolset.geometry_ops.GeometryTools] Appends a sphere of the given radius to the component's mesh. Available on UE 5.9; absent from UE 5.8. Params: component, transform, radius", "epic_call_tool"),
  },
  epic_append_torus: {
    epicSchema: S_epic_append_torus,
    epicTool: { toolset: "geometry_toolset.geometry_ops.GeometryTools", name: "geometry_toolset.geometry_ops.GeometryTools.append_torus", engines: ["5.9"] },
    ...bp("mutate", "[Epic geometry_toolset.geometry_ops.GeometryTools] Appends a torus to the component's mesh. Available on UE 5.9; absent from UE 5.8. Params: component, transform, major_radius, minor_radius", "epic_call_tool"),
  },
  epic_assign_clothing_to_section: {
    epicSchema: S_epic_assign_clothing_to_section,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.AssignClothingToSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Binds a clothing asset to a specific LOD and section on the skeletal mesh. Mirrors the SkeletalMesh Editor's \"Apply Clothing Data\" action. Any clothing already bound to the section is unbound first. Available on UE 5.9; absent from UE 5.8. Params: skeletalMeshPath, clothingAssetName, lodIndex, sectionIndex, clothingLodIndex", "epic_call_tool"),
  },
  epic_can_edit_asset: {
    epicSchema: S_epic_can_edit_asset,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.can_edit_asset", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Checks whether an asset can be edited. Available on UE 5.8; absent from UE 5.9. Params: asset_path", "epic_call_tool"),
  },
  epic_connect_pins: {
    epicSchema: S_epic_connect_pins,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.ConnectPins", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Connects an output pin on FromNode to an input pin on ToNode. Available on UE 5.9; absent from UE 5.8. Params: graph, fromNode, fromPin, toNode, toPin", "epic_call_tool"),
  },
  epic_convert_clothing_asset_common_to_chaos_cloth_asset: {
    epicSchema: S_epic_convert_clothing_asset_common_to_chaos_cloth_asset,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.ConvertClothingAssetCommonToChaosClothAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Converts a legacy UClothingAssetCommon (the cloth asset created via UChaosClothingSimulationFactory) into a new UChaosClothAsset whose embedded Dataflow graph mirrors the legacy UChaosClothConfig and UChaosClothSharedSimConfig values, with one WeightMap node per legacy weight map. Available on UE 5.9; absent from UE 5.8. Params: skeletalMeshPath, clothingAssetName, outputPackagePath, assetName", "epic_call_tool"),
  },
  epic_create: {
    epicSchema: S_epic_create,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.create", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.Create", engines: ["5.9"], schema: S_epic_create__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Creates a new CurveTable asset. Params: folder_path, asset_name. On UE 5.9 it calls EditorToolset.CurveTableToolset.Create (Params: folderPath, assetName).", "epic_call_tool"),
  },
  epic_create__data_asset_tools: {
    epicSchema: S_epic_create__data_asset_tools,
    epicTool: { toolset: "editor_toolset.toolsets.data_asset.DataAssetTools", name: "editor_toolset.toolsets.data_asset.DataAssetTools.create", engines: ["5.8"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_asset.DataAssetTools] Creates a new DataAsset asset in the project. Available on UE 5.8; absent from UE 5.9. Params: folder_path, asset_name, asset_type", "epic_call_tool"),
  },
  epic_create__data_asset_toolset: {
    epicSchema: S_epic_create__data_asset_toolset,
    epicTool: { toolset: "EditorToolset.DataAssetToolset", name: "EditorToolset.DataAssetToolset.Create", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.DataAssetToolset] Creates a new DataAsset asset in the project. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName, assetType", "epic_call_tool"),
  },
  epic_create__data_table_tools: {
    epicSchema: S_epic_create__data_table_tools,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.create", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.Create", engines: ["5.9"], schema: S_epic_create__data_table_tools__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Creates a new DataTable asset with the specified column schema. Params: folder_path, asset_name, schema. On UE 5.9 it calls EditorToolset.DataTableToolset.Create (Params: folderPath, assetName, schema).", "epic_call_tool"),
  },
  epic_create__string_table_tools: {
    epicSchema: S_epic_create__string_table_tools,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.create", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.Create", engines: ["5.9"], schema: S_epic_create__string_table_tools__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Creates a new StringTable asset. Params: folder_path, asset_name. On UE 5.9 it calls EditorToolset.StringTableToolset.Create (Params: folderPath, assetName).", "epic_call_tool"),
  },
  epic_create__user_defined_enum_toolset: {
    epicSchema: S_epic_create__user_defined_enum_toolset,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.Create", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedEnumToolset] Creates a new user-defined enum asset with no enumerators. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_create__user_defined_struct_toolset: {
    epicSchema: S_epic_create__user_defined_struct_toolset,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.Create", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Creates a new user-defined struct asset. The new struct starts with one default bool variable (engine convention). Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_create_atlas: {
    epicSchema: S_epic_create_atlas,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.CreateAtlas", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Creates a new curve atlas asset containing no gradients. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName, textureSize?", "epic_call_tool"),
  },
  epic_create_clothing_asset: {
    epicSchema: S_epic_create_clothing_asset,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.CreateClothingAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Creates a UChaosClothAssetSKMClothingAsset from a Chaos Cloth/Outfit Asset and adds it to the skeletal mesh's clothing asset array, making it available to assign to sections. Available on UE 5.9; absent from UE 5.8. Params: skeletalMeshPath, chaosClothAssetPath", "epic_call_tool"),
  },
  epic_create_folder: {
    epicSchema: S_epic_create_folder,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.create_folder", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.CreateFolder", engines: ["5.9"], schema: S_epic_create_folder__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Creates a folder at the specified path. Params: path. On UE 5.9 it calls EditorToolset.AssetToolset.CreateFolder (Params: path).", "epic_call_tool"),
  },
  epic_create_gradient: {
    epicSchema: S_epic_create_gradient,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.CreateGradient", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Creates a new linear color gradient asset, seeded with an opaque black-to-white ramp. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_create_texture_graph: {
    epicSchema: S_epic_create_texture_graph,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.CreateTextureGraph", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Creates a new empty TextureGraph asset. This should ONLY be called after getting explicit direction or permission from the user. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName", "epic_call_tool"),
  },
  epic_delete: {
    epicSchema: S_epic_delete,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.delete", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.Delete", engines: ["5.9"], schema: S_epic_delete__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Deletes an asset or folder. Params: path. On UE 5.9 it calls EditorToolset.AssetToolset.Delete (Params: path).", "epic_call_tool"),
  },
  epic_diff: {
    epicSchema: S_epic_diff,
    epicTool: { toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.CurveTableToolset] Returns a human-readable text diff between two CurveTable assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_diff__data_asset_toolset: {
    epicSchema: S_epic_diff__data_asset_toolset,
    epicTool: { toolset: "EditorToolset.DataAssetToolset", name: "EditorToolset.DataAssetToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.DataAssetToolset] Returns a human-readable text diff between two DataAsset assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_diff__data_table_toolset: {
    epicSchema: S_epic_diff__data_table_toolset,
    epicTool: { toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.DataTableToolset] Returns a human-readable text diff between two DataTable assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_diff__string_table_toolset: {
    epicSchema: S_epic_diff__string_table_toolset,
    epicTool: { toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.StringTableToolset] Returns a human-readable text diff between two StringTable assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_disconnect_pins: {
    epicSchema: S_epic_disconnect_pins,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.DisconnectPins", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Removes the edge between the specified pins. Available on UE 5.9; absent from UE 5.8. Params: graph, fromNode, fromPin, toNode, toPin", "epic_call_tool"),
  },
  epic_duplicate: {
    epicSchema: S_epic_duplicate,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.duplicate", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.Duplicate", engines: ["5.9"], schema: S_epic_duplicate__1, rename: {"new_path":"newPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Makes a copy of a folder or asset. Params: path, new_path. On UE 5.9 it calls EditorToolset.AssetToolset.Duplicate (Params: path, newPath).", "epic_call_tool"),
  },
  epic_exists: {
    epicSchema: S_epic_exists,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.exists", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.Exists", engines: ["5.9"], schema: S_epic_exists__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Determines if a folder or asset exists. Params: path. On UE 5.9 it calls EditorToolset.AssetToolset.Exists (Params: path).", "epic_call_tool"),
  },
  epic_export_png: {
    epicSchema: S_epic_export_png,
    epicTool: { toolset: "EditorToolset.TextureToolset", name: "EditorToolset.TextureToolset.ExportPng", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.TextureToolset] Exports a Texture to a PNG file on disk. Available on UE 5.9; absent from UE 5.8. Params: texture, filePath", "epic_call_tool"),
  },
  epic_export_texture_graph: {
    epicSchema: S_epic_export_texture_graph,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.ExportTextureGraph", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Exports every texture output node to a Texture2D asset in the given content folder. Triggers a full graph evaluation internally before writing assets. Available on UE 5.9; absent from UE 5.8. Params: graph, outputFolder", "epic_call_tool"),
  },
  epic_find_assets: {
    epicSchema: S_epic_find_assets,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.find_assets", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.FindAssets", engines: ["5.9"], schema: S_epic_find_assets__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Searches the project for assets that match specific criteria. Params: folder_path, name, asset_type?, recursive?, tags?. On UE 5.9 it calls EditorToolset.AssetToolset.FindAssets (Params: filter?).", "epic_call_tool"),
  },
  epic_find_similar: {
    epicSchema: S_epic_find_similar,
    epicTool: { toolset: "SemanticSearchToolset.SemanticSearchToolset", name: "SemanticSearchToolset.SemanticSearchToolset.FindSimilar", engines: ["5.8"] },
    ...bp("read", "[Epic SemanticSearchToolset.SemanticSearchToolset] Find assets whose embeddings are semantically similar to the given asset's embedding. Vector-only (no BM25). The source asset must already be indexed by the SemanticSearch plugin. Available on UE 5.8; absent from UE 5.9. Params: assetPath, classFilter, pathRegexes, k?", "epic_call_tool"),
  },
  epic_fix_up_redirectors: {
    epicSchema: S_epic_fix_up_redirectors,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.FixUpRedirectors", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.AssetToolset] Fixes up object redirectors at or under a path, rewiring the assets that reference them to point at each redirector's target. A folder also fixes up every other redirector under it, including ones a rename did not create - pass the specific asset path instead when only cleaning up after a move or rename. Available on UE 5.9; absent from UE 5.8. Params: path, bDeleteRedirectors?", "epic_call_tool"),
  },
  epic_generate_convex_collisions: {
    epicSchema: S_epic_generate_convex_collisions,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.generate_convex_collisions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GenerateConvexCollisions", engines: ["5.9"], schema: S_epic_generate_convex_collisions__1, rename: {"hull_count":"hullCount","max_hull_verts":"maxHullVerts","hull_precision":"hullPrecision"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Generates convex hull collision shapes for a static mesh. Convex hulls provide accurate collision for physics simulation. More hulls improve accuracy but increase runtime cost. Replaces any existing collision. Params: mesh, hull_count?, max_hull_verts?, hull_precision?. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GenerateConvexCollisions (Params: mesh, hullCount?, maxHullVerts?, hullPrecision?).", "epic_call_tool"),
  },
  epic_generate_lods: {
    epicSchema: S_epic_generate_lods,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.generate_lods", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GenerateLods", engines: ["5.9"], schema: S_epic_generate_lods__1, rename: {"triangle_percents":"trianglePercents"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Auto-generates LODs for a static mesh using triangle reduction. Each entry in triangle_percents creates one additional LOD. The value is the fraction of triangles to keep relative to LOD 0, from just above 0.0 (nearly empty) to 1.0 (full detail). For example, [0.5, 0.25] creates LOD1 with 50% of the original triangles and LOD2 with 25%. Params: mesh, triangle_percents. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GenerateLods (Params: mesh, trianglePercents).", "epic_call_tool"),
  },
  epic_get_alpha_stops: {
    epicSchema: S_epic_get_alpha_stops,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.GetAlphaStops", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.CurveAtlasToolset] Returns the alpha stops of a gradient in time order. Only keys with derived tangents form stops; GetChannelKeys reads any other gradient. Available on UE 5.9; absent from UE 5.8. Params: gradient", "epic_call_tool"),
  },
  epic_get_asset_class: {
    epicSchema: S_epic_get_asset_class,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.get_asset_class", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetAssetClass", engines: ["5.9"], schema: S_epic_get_asset_class__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Gets the class of an asset. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.GetAssetClass (Params: assetPath).", "epic_call_tool"),
  },
  epic_get_asset_content_roots: {
    epicSchema: S_epic_get_asset_content_roots,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetAssetContentRoots", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.AssetToolset] Returns the known content-browser roots in the current project. Available on UE 5.9; absent from UE 5.8. Params: contains?, bWritable?", "epic_call_tool"),
  },
  epic_get_asset_tags: {
    epicSchema: S_epic_get_asset_tags,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.get_asset_tags", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetAssetTags", engines: ["5.9"], schema: S_epic_get_asset_tags__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Gets the asset registry tags for an asset. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.GetAssetTags (Params: assetPath).", "epic_call_tool"),
  },
  epic_get_bounds: {
    epicSchema: S_epic_get_bounds,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_bounds", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetBounds", engines: ["5.9"], schema: S_epic_get_bounds__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the local-space bounding box of a static mesh. Params: mesh. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetBounds (Params: mesh).", "epic_call_tool"),
  },
  epic_get_channel_keys: {
    epicSchema: S_epic_get_channel_keys,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.GetChannelKeys", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.CurveAtlasToolset] Returns the raw keys of one channel of a gradient, including tangents. Available on UE 5.9; absent from UE 5.8. Params: gradient, channel", "epic_call_tool"),
  },
  epic_get_collision_complexity: {
    epicSchema: S_epic_get_collision_complexity,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetCollisionComplexity", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.StaticMeshToolset] Returns the collision trace behavior of a static mesh. Available on UE 5.9; absent from UE 5.8. Params: mesh", "epic_call_tool"),
  },
  epic_get_color_stops: {
    epicSchema: S_epic_get_color_stops,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.GetColorStops", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.CurveAtlasToolset] Returns the color stops of a gradient in time order. Only gradients whose red, green and blue curves share key times, interpolation modes and derived tangents form stops; GetChannelKeys reads any other gradient. Available on UE 5.9; absent from UE 5.8. Params: gradient", "epic_call_tool"),
  },
  epic_get_dependencies: {
    epicSchema: S_epic_get_dependencies,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.get_dependencies", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetDependencies", engines: ["5.9"], schema: S_epic_get_dependencies__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Lists assets that the specified asset depends on. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.GetDependencies (Params: assetPath).", "epic_call_tool"),
  },
  epic_get_entry: {
    epicSchema: S_epic_get_entry,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.get_entry", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.GetEntry", engines: ["5.9"], schema: S_epic_get_entry__1, rename: {"string_table":"stringTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Returns the source string for a specific key. Params: string_table, key. On UE 5.9 it calls EditorToolset.StringTableToolset.GetEntry (Params: stringTable, key).", "epic_call_tool"),
  },
  epic_get_enumerators: {
    epicSchema: S_epic_get_enumerators,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.GetEnumerators", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.UserDefinedEnumToolset] Returns the enumerators of an enum in order. Available on UE 5.9; absent from UE 5.8. Params: enum", "epic_call_tool"),
  },
  epic_get_gradients: {
    epicSchema: S_epic_get_gradients,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.GetGradients", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.CurveAtlasToolset] Returns the gradients of an atlas in row order. Available on UE 5.9; absent from UE 5.8. Params: atlas", "epic_call_tool"),
  },
  epic_get_graph: {
    epicSchema: S_epic_get_graph,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.GetGraph", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Returns every node in the graph, each with its input and output pins and, for a connected input pin, the node feeding it. Input parameters and outputs are ordinary nodes here, identified by their ExpressionClass. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_get_graph_issues: {
    epicSchema: S_epic_get_graph_issues,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.GetGraphIssues", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Validates the graph and returns every issue it reports: the errors and warnings the engine's validation surfaces (for example missing required inputs, unsupported types, material or subgraph errors, and node warnings). Use it to check a graph's health after editing. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_get_items: {
    epicSchema: S_epic_get_items,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.GetItems", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns cached item data. Items must be loaded in the registry cache to be returned. Params: registryName, itemNames", "epic_call_tool"),
  },
  epic_get_keys: {
    epicSchema: S_epic_get_keys,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.get_keys", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.GetKeys", engines: ["5.9"], schema: S_epic_get_keys__1, rename: {"curve_table":"curveTable","row_name":"rowName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Returns all keys for a row. Params: curve_table, row_name. On UE 5.9 it calls EditorToolset.CurveTableToolset.GetKeys (Params: curveTable, rowName).", "epic_call_tool"),
  },
  epic_get_live_cloth_simulation_stats: {
    epicSchema: S_epic_get_live_cloth_simulation_stats,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.GetLiveClothSimulationStats", engines: ["5.9"] },
    ...bp("read", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Returns the live solver counts for every clothing simulation running in PIE - the figures the cloth debug UI shows, without opening it. A simulation only reports counts once it is actually simulating. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_lod_count: {
    epicSchema: S_epic_get_lod_count,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_lod_count", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetLodCount", engines: ["5.9"], schema: S_epic_get_lod_count__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the number of LODs in a static mesh asset. Params: mesh. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetLodCount (Params: mesh).", "epic_call_tool"),
  },
  epic_get_lod_thresholds: {
    epicSchema: S_epic_get_lod_thresholds,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_lod_thresholds", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetLodThresholds", engines: ["5.9"], schema: S_epic_get_lod_thresholds__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's screen height to the viewport height. A value of 1.0 means the mesh fills the full viewport height; values above 1.0 are valid and mean the mesh must appear larger than the viewport before the next LOD activates. Each LOD activates when the mesh appears smaller than its threshold. Params: mesh. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetLodThresholds (Params: mesh).", "epic_call_tool"),
  },
  epic_get_material: {
    epicSchema: S_epic_get_material,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_material", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetMaterial", engines: ["5.9"], schema: S_epic_get_material__1, rename: {"slot_name":"slotName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the material assigned to a named slot on a static mesh. Params: mesh, slot_name. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetMaterial (Params: mesh, slotName).", "epic_call_tool"),
  },
  epic_get_material_slots: {
    epicSchema: S_epic_get_material_slots,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_material_slots", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetMaterialSlots", engines: ["5.9"], schema: S_epic_get_material_slots__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the names of all material slots in a static mesh. Material slot names are used when assigning materials to specific parts of the mesh. Use these names with get_material and set_material. Params: mesh. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetMaterialSlots (Params: mesh).", "epic_call_tool"),
  },
  epic_get_metadata_tags: {
    epicSchema: S_epic_get_metadata_tags,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.get_metadata_tags", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetMetadataTags", engines: ["5.9"], schema: S_epic_get_metadata_tags__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Gets the metadata tags for an asset. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.GetMetadataTags (Params: assetPath).", "epic_call_tool"),
  },
  epic_get_namespace: {
    epicSchema: S_epic_get_namespace,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.get_namespace", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.GetNamespace", engines: ["5.9"], schema: S_epic_get_namespace__1, rename: {"string_table":"stringTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Returns the namespace of a StringTable asset. Params: string_table. On UE 5.9 it calls EditorToolset.StringTableToolset.GetNamespace (Params: stringTable).", "epic_call_tool"),
  },
  epic_get_plugin_content_paths: {
    epicSchema: S_epic_get_plugin_content_paths,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.get_plugin_content_paths", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Returns the root content paths for plugins that have content. Available on UE 5.8; absent from UE 5.9. Params: include_engine?", "epic_call_tool"),
  },
  epic_get_quality_level_min_lod: {
    epicSchema: S_epic_get_quality_level_min_lod,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetQualityLevelMinLod", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.StaticMeshToolset] Returns the per-quality-level Min LOD overrides for a static mesh. Min LOD is the smallest LOD index the mesh may render, forcing lower hardware quality levels to skip its higher-detail, more expensive LODs. Available on UE 5.9; absent from UE 5.8. Params: mesh", "epic_call_tool"),
  },
  epic_get_quality_level_min_lod_default: {
    epicSchema: S_epic_get_quality_level_min_lod_default,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetQualityLevelMinLodDefault", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.StaticMeshToolset] Returns the fallback Min LOD for quality levels with no explicit override. Available on UE 5.9; absent from UE 5.8. Params: mesh", "epic_call_tool"),
  },
  epic_get_referencers: {
    epicSchema: S_epic_get_referencers,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.get_referencers", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetReferencers", engines: ["5.9"], schema: S_epic_get_referencers__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Lists assets that reference the specified asset. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.GetReferencers (Params: assetPath).", "epic_call_tool"),
  },
  epic_get_registry_info: {
    epicSchema: S_epic_get_registry_info,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.GetRegistryInfo", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns detailed information about a specific registry. Params: registryName", "epic_call_tool"),
  },
  epic_get_rows: {
    epicSchema: S_epic_get_rows,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.get_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.GetRows", engines: ["5.9"], schema: S_epic_get_rows__1, rename: {"data_table":"dataTable","row_names":"rowNames"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Returns the column values for one or more rows as a JSON string. Params: data_table, row_names. On UE 5.9 it calls EditorToolset.DataTableToolset.GetRows (Params: dataTable, rowNames).", "epic_call_tool"),
  },
  epic_get_schema: {
    epicSchema: S_epic_get_schema,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.GetSchema", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns the item struct schema as JSON. Params: registryName", "epic_call_tool"),
  },
  epic_get_schema__data_table_tools: {
    epicSchema: S_epic_get_schema__data_table_tools,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.get_schema", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.GetSchema", engines: ["5.9"], schema: S_epic_get_schema__data_table_tools__1, rename: {"data_table":"dataTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Returns the column schema of the data table as a JSON string. Params: data_table. On UE 5.9 it calls EditorToolset.DataTableToolset.GetSchema (Params: dataTable).", "epic_call_tool"),
  },
  epic_get_section_clothing: {
    epicSchema: S_epic_get_section_clothing,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.GetSectionClothing", engines: ["5.9"] },
    ...bp("read", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Returns the name of clothing bound for a specific LOD section (if any). Available on UE 5.9; absent from UE 5.8. Params: skeletalMeshPath, lodIndex, sectionIndex", "epic_call_tool"),
  },
  epic_get_size: {
    epicSchema: S_epic_get_size,
    epicTool: { toolset: "editor_toolset.toolsets.texture.TextureTools", name: "editor_toolset.toolsets.texture.TextureTools.get_size", engines: ["5.8"], successors: [{ toolset: "EditorToolset.TextureToolset", name: "EditorToolset.TextureToolset.GetSize", engines: ["5.9"], schema: S_epic_get_size__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.texture.TextureTools] Returns the dimensions of a Texture2D in pixels. Params: texture. On UE 5.9 it calls EditorToolset.TextureToolset.GetSize (Params: texture).", "epic_call_tool"),
  },
  epic_get_table_id: {
    epicSchema: S_epic_get_table_id,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.get_table_id", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.GetTableId", engines: ["5.9"], schema: S_epic_get_table_id__1, rename: {"string_table":"stringTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Returns the table ID for a StringTable asset. The table ID is derived from the asset's package path and is used to reference the string table in text properties and localisation. Params: string_table. On UE 5.9 it calls EditorToolset.StringTableToolset.GetTableId (Params: stringTable).", "epic_call_tool"),
  },
  epic_get_tooltip: {
    epicSchema: S_epic_get_tooltip,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.GetTooltip", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.UserDefinedStructToolset] Returns the struct-level developer tooltip. Available on UE 5.9; absent from UE 5.8. Params: struct", "epic_call_tool"),
  },
  epic_get_triangle_count: {
    epicSchema: S_epic_get_triangle_count,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_triangle_count", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetTriangleCount", engines: ["5.9"], schema: S_epic_get_triangle_count__1, rename: {"lod_index":"lodIndex"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the number of triangles in a specific LOD of a static mesh. Params: mesh, lod_index?. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetTriangleCount (Params: mesh, lodIndex?).", "epic_call_tool"),
  },
  epic_get_user_asset_tags: {
    epicSchema: S_epic_get_user_asset_tags,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetUserAssetTags", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.AssetToolset] Gets the User Asset Tags on an asset. User Asset Tags are user-authored labels stored in the asset's package metadata. They are surfaced by the Manage Tags editor and the Tagged Asset Browser, and are distinct from asset registry tags and metadata tags. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_get_vertex_count: {
    epicSchema: S_epic_get_vertex_count,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_vertex_count", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.GetVertexCount", engines: ["5.9"], schema: S_epic_get_vertex_count__1, rename: {"lod_index":"lodIndex"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the number of vertices in a specific LOD of a static mesh. Params: mesh, lod_index?. On UE 5.9 it calls EditorToolset.StaticMeshToolset.GetVertexCount (Params: mesh, lodIndex?).", "epic_call_tool"),
  },
  epic_get_virtual_paths: {
    epicSchema: S_epic_get_virtual_paths,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.GetVirtualPaths", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.AssetToolset] Converts internal asset/folder paths to their Content Browser virtual path. In most projects this is the same as the input. In UEFN/Valkyrie projects it can differ (e.g. an engine plugin's content grouped under a friendly display path) -- use this only to present a path to the user in conversation. The result may not be a valid input to other tools; pass the original path (e.g. from FindAssets) to other tools instead. Available on UE 5.9; absent from UE 5.8. Params: paths", "epic_call_tool"),
  },
  epic_import_file: {
    epicSchema: S_epic_import_file,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.import_file", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.ImportFile", engines: ["5.9"], schema: S_epic_import_file__1, rename: {"folder_path":"folderPath","asset_name":"assetName","source_file":"sourceFile","interp_mode":"interpMode"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Imports a file from disk as a CurveTable asset. The file's first column is the row name; subsequent columns are sample times and values. interp_mode controls how the imported keys are interpolated between samples. Params: folder_path, asset_name, source_file, interp_mode. On UE 5.9 it calls EditorToolset.CurveTableToolset.ImportFile (Params: folderPath, assetName, sourceFile, interpMode, bReplaceExisting?, bSave?).", "epic_call_tool"),
  },
  epic_import_file__data_table_tools: {
    epicSchema: S_epic_import_file__data_table_tools,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.import_file", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.ImportFile", engines: ["5.9"], schema: S_epic_import_file__data_table_tools__1, rename: {"folder_path":"folderPath","asset_name":"assetName","source_file":"sourceFile"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Imports a file from disk as a DataTable asset. The file's columns must match the property names in schema. Use search_row_structs to discover usable schema structs. Params: folder_path, asset_name, source_file, schema. On UE 5.9 it calls EditorToolset.DataTableToolset.ImportFile (Params: folderPath, assetName, sourceFile, schema, bReplaceExisting?, bSave?).", "epic_call_tool"),
  },
  epic_import_file__static_mesh_tools: {
    epicSchema: S_epic_import_file__static_mesh_tools,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.import_file", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.ImportFile", engines: ["5.9"], schema: S_epic_import_file__static_mesh_tools__1, rename: {"folder_path":"folderPath","asset_name":"assetName","source_file":"sourceFile","import_materials":"bImportMaterials","import_textures":"bImportTextures","combine_meshes":"bCombineMeshes"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Imports a mesh file from disk as a StaticMesh asset. Params: folder_path, asset_name, source_file, import_materials?, import_textures?, combine_meshes?. On UE 5.9 it calls EditorToolset.StaticMeshToolset.ImportFile (Params: folderPath, assetName, sourceFile, bImportMaterials?, bImportTextures?, bCombineMeshes?, bReplaceExisting?, bSave?).", "epic_call_tool"),
  },
  epic_import_file__string_table_tools: {
    epicSchema: S_epic_import_file__string_table_tools,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.import_file", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.ImportFile", engines: ["5.9"], schema: S_epic_import_file__string_table_tools__1, rename: {"folder_path":"folderPath","asset_name":"assetName","source_file":"sourceFile"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Imports a file from disk as a StringTable asset. The file must have a header row with at least 'Key' and 'SourceString' columns. Additional meta-data columns are imported but the namespace is not - the StringTable's namespace is derived from its asset path. Params: folder_path, asset_name, source_file. On UE 5.9 it calls EditorToolset.StringTableToolset.ImportFile (Params: folderPath, assetName, sourceFile, bReplaceExisting?, bSave?).", "epic_call_tool"),
  },
  epic_import_file__texture_tools: {
    epicSchema: S_epic_import_file__texture_tools,
    epicTool: { toolset: "editor_toolset.toolsets.texture.TextureTools", name: "editor_toolset.toolsets.texture.TextureTools.import_file", engines: ["5.8"], successors: [{ toolset: "EditorToolset.TextureToolset", name: "EditorToolset.TextureToolset.ImportFile", engines: ["5.9"], schema: S_epic_import_file__texture_tools__1, rename: {"folder_path":"folderPath","asset_name":"assetName","source_file":"sourceFile"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.texture.TextureTools] Imports an image file from disk as a Texture2D asset. Params: folder_path, asset_name, source_file. On UE 5.9 it calls EditorToolset.TextureToolset.ImportFile (Params: folderPath, assetName, sourceFile, bReplaceExisting?, bSave?).", "epic_call_tool"),
  },
  epic_is_bitflags: {
    epicSchema: S_epic_is_bitflags,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.IsBitflags", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.UserDefinedEnumToolset] Returns whether the enum is marked as a bitflags type. Available on UE 5.9; absent from UE 5.8. Params: enum", "epic_call_tool"),
  },
  epic_is_checked_out: {
    epicSchema: S_epic_is_checked_out,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.is_checked_out", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Checks whether an asset is checked out by the current user. Available on UE 5.8; absent from UE 5.9. Params: asset_path", "epic_call_tool"),
  },
  epic_is_dirty: {
    epicSchema: S_epic_is_dirty,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.is_dirty", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.IsDirty", engines: ["5.9"], schema: S_epic_is_dirty__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Checks whether an asset has unsaved changes. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.IsDirty (Params: assetPath).", "epic_call_tool"),
  },
  epic_is_nanite_enabled: {
    epicSchema: S_epic_is_nanite_enabled,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.is_nanite_enabled", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns whether Nanite is enabled for a static mesh. Nanite is Unreal's virtualized geometry system that renders highly detailed meshes efficiently. It is most beneficial for meshes with many triangles. Available on UE 5.8; absent from UE 5.9. Params: mesh", "epic_call_tool"),
  },
  epic_is_nanite_mesh: {
    epicSchema: S_epic_is_nanite_mesh,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.IsNaniteMesh", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.StaticMeshToolset] Returns whether this is a Nanite mesh. This reflects the mesh's current state, so it may briefly lag behind a just-changed Nanite setting while a background rebuild finishes. Available on UE 5.9; absent from UE 5.8. Params: mesh", "epic_call_tool"),
  },
  epic_list_clothing_assets: {
    epicSchema: S_epic_list_clothing_assets,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.ListClothingAssets", engines: ["5.9"] },
    ...bp("read", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Returns information about all Clothing Assets on a skeletal mesh. Use AssetName with AssignClothingToSection. Check bRequiresMatchingLodIndex to determine how to supply ClothingLodIndex: - true (ChaosClothAsset): pass the same value as LodIndex - false (UClothingAssetCommon): pass any value in [0, NumClothingLods-1] Available on UE 5.9; absent from UE 5.8. Params: skeletalMeshPath", "epic_call_tool"),
  },
  epic_list_data_sources: {
    epicSchema: S_epic_list_data_sources,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.ListDataSources", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns the editor-defined sources configured on a Data Registry. These are the sources as authored on the registry asset, before any runtime expansion of meta sources. Params: registryName", "epic_call_tool"),
  },
  epic_list_folders: {
    epicSchema: S_epic_list_folders,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.list_folders", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.ListFolders", engines: ["5.9"], schema: S_epic_list_folders__1, rename: {"root_path":"rootPath","recursive":"bRecursive"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Lists the folders contained within a folder. Params: root_path, recursive?. On UE 5.9 it calls EditorToolset.AssetToolset.ListFolders (Params: rootPath, bRecursive?).", "epic_call_tool"),
  },
  epic_list_input_params: {
    epicSchema: S_epic_list_input_params,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.ListInputParams", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Returns the graph's input-parameter nodes: the graph-level parameters such as Scalar, Color, or Vector. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_list_items: {
    epicSchema: S_epic_list_items,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.ListItems", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns all item names in a Data Registry. Params: registryName", "epic_call_tool"),
  },
  epic_list_keys: {
    epicSchema: S_epic_list_keys,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.list_keys", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.ListKeys", engines: ["5.9"], schema: S_epic_list_keys__1, rename: {"string_table":"stringTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Lists all keys in the string table. Params: string_table. On UE 5.9 it calls EditorToolset.StringTableToolset.ListKeys (Params: stringTable).", "epic_call_tool"),
  },
  epic_list_node_types: {
    epicSchema: S_epic_list_node_types,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.ListNodeTypes", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Returns every available TextureGraph expression node type, sorted. A returned class identifies the node type to add. Input-parameter and Output types are included but are rejected by the generic add; create those through the dedicated input-parameter and output tools. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_outputs: {
    epicSchema: S_epic_list_outputs,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.ListOutputs", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Returns the graph's Output nodes. Available on UE 5.9; absent from UE 5.8. Params: graph", "epic_call_tool"),
  },
  epic_list_registries: {
    epicSchema: S_epic_list_registries,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.ListRegistries", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns the names of all registered Data Registries. Params: structFilter?", "epic_call_tool"),
  },
  epic_list_rows: {
    epicSchema: S_epic_list_rows,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.list_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.ListRows", engines: ["5.9"], schema: S_epic_list_rows__1, rename: {"curve_table":"curveTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Lists the names of all rows in the curve table. Params: curve_table. On UE 5.9 it calls EditorToolset.CurveTableToolset.ListRows (Params: curveTable).", "epic_call_tool"),
  },
  epic_list_rows__data_table_tools: {
    epicSchema: S_epic_list_rows__data_table_tools,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.list_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.ListRows", engines: ["5.9"], schema: S_epic_list_rows__data_table_tools__1, rename: {"data_table":"dataTable"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Lists the names of all rows in the data table. Params: data_table. On UE 5.9 it calls EditorToolset.DataTableToolset.ListRows (Params: dataTable).", "epic_call_tool"),
  },
  epic_list_runtime_sources: {
    epicSchema: S_epic_list_runtime_sources,
    epicTool: { toolset: "DataRegistryToolset.DataRegistryTools", name: "DataRegistryToolset.DataRegistryTools.ListRuntimeSources", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic DataRegistryToolset.DataRegistryTools] Returns the runtime sources for a Data Registry. This is the expanded list including transient child sources generated from meta sources. Will equal ListDataSources when the registry has no meta sources. Params: registryName", "epic_call_tool"),
  },
  epic_list_variables: {
    epicSchema: S_epic_list_variables,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.ListVariables", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.UserDefinedStructToolset] Returns the variables of a struct in authored order. Available on UE 5.9; absent from UE 5.8. Params: struct", "epic_call_tool"),
  },
  epic_load_asset: {
    epicSchema: S_epic_load_asset,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.load_asset", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.LoadAsset", engines: ["5.9"], schema: S_epic_load_asset__1, rename: {"asset_path":"assetPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Loads an asset from the project. Params: asset_path. On UE 5.9 it calls EditorToolset.AssetToolset.LoadAsset (Params: assetPath).", "epic_call_tool"),
  },
  epic_move: {
    epicSchema: S_epic_move,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.move", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.Move", engines: ["5.9"], schema: S_epic_move__1, rename: {"new_path":"newPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Moves or renames an asset or folder. Params: path, new_path. On UE 5.9 it calls EditorToolset.AssetToolset.Move (Params: path, newPath).", "epic_call_tool"),
  },
  epic_move_enumerator: {
    epicSchema: S_epic_move_enumerator,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.MoveEnumerator", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedEnumToolset] Moves an enumerator to a new position, shifting the others to accommodate it. Available on UE 5.9; absent from UE 5.8. Params: enum, fromIndex, toIndex", "epic_call_tool"),
  },
  epic_move_variable: {
    epicSchema: S_epic_move_variable,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.MoveVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Moves a variable above or below another variable on the struct. Available on UE 5.9; absent from UE 5.8. Params: struct, variableName, relativeToName, bAbove", "epic_call_tool"),
  },
  epic_read_file: {
    epicSchema: S_epic_read_file,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.read_file", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.asset.AssetTools] Reads a text file from disk and returns its contents. Only files under /Game/, an enabled plugin's Content/ directory, or the project Saved/ directory may be read. Only plain text formats are supported. Available on UE 5.8; absent from UE 5.9. Params: file_path", "epic_call_tool"),
  },
  epic_read_texture: {
    epicSchema: S_epic_read_texture,
    epicTool: { toolset: "EditorToolset.TextureToolset", name: "EditorToolset.TextureToolset.ReadTexture", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.TextureToolset] Reads a Texture and returns it as an image. Available on UE 5.9; absent from UE 5.8. Params: texture", "epic_call_tool"),
  },
  epic_reload_asset: {
    epicSchema: S_epic_reload_asset,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.ReloadAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.AssetToolset] Reloads an asset from disk, discarding any unsaved in-memory changes. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_remove_clothing_from_section: {
    epicSchema: S_epic_remove_clothing_from_section,
    epicTool: { toolset: "ChaosClothAssetToolset.ChaosClothAssetToolset", name: "ChaosClothAssetToolset.ChaosClothAssetToolset.RemoveClothingFromSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ChaosClothAssetToolset.ChaosClothAssetToolset] Removes the clothing binding from a specific LOD section on the skeletal mesh. Mirrors the SkeletalMesh Editor's \"Remove Clothing Data\" action. Available on UE 5.9; absent from UE 5.8. Params: skeletalMeshPath, lodIndex, sectionIndex", "epic_call_tool"),
  },
  epic_remove_collisions: {
    epicSchema: S_epic_remove_collisions,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.remove_collisions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.RemoveCollisions", engines: ["5.9"], schema: S_epic_remove_collisions__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Removes all collision shapes from a static mesh. Params: mesh. On UE 5.9 it calls EditorToolset.StaticMeshToolset.RemoveCollisions (Params: mesh).", "epic_call_tool"),
  },
  epic_remove_entry: {
    epicSchema: S_epic_remove_entry,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.remove_entry", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.RemoveEntry", engines: ["5.9"], schema: S_epic_remove_entry__1, rename: {"string_table":"stringTable"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Removes an entry from the string table. Params: string_table, key. On UE 5.9 it calls EditorToolset.StringTableToolset.RemoveEntry (Params: stringTable, key).", "epic_call_tool"),
  },
  epic_remove_enumerator: {
    epicSchema: S_epic_remove_enumerator,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.RemoveEnumerator", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedEnumToolset] Removes the enumerator at the given index, shifting later enumerators down to fill the gap. Available on UE 5.9; absent from UE 5.8. Params: enum, index", "epic_call_tool"),
  },
  epic_remove_gradient: {
    epicSchema: S_epic_remove_gradient,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.RemoveGradient", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Removes a gradient from an atlas, shifting later rows up to fill the gap. Available on UE 5.9; absent from UE 5.8. Params: atlas, gradient", "epic_call_tool"),
  },
  epic_remove_input_param: {
    epicSchema: S_epic_remove_input_param,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.RemoveInputParam", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Removes the input-parameter node exposing the named parameter. Available on UE 5.9; absent from UE 5.8. Params: graph, paramName", "epic_call_tool"),
  },
  epic_remove_lods: {
    epicSchema: S_epic_remove_lods,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.remove_lods", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.RemoveLods", engines: ["5.9"], schema: S_epic_remove_lods__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Removes all auto-generated LODs from a static mesh, keeping only LOD 0. Params: mesh. On UE 5.9 it calls EditorToolset.StaticMeshToolset.RemoveLods (Params: mesh).", "epic_call_tool"),
  },
  epic_remove_node: {
    epicSchema: S_epic_remove_node,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.RemoveNode", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Removes a node and all its connected edges from the graph. Available on UE 5.9; absent from UE 5.8. Params: graph, node", "epic_call_tool"),
  },
  epic_remove_output: {
    epicSchema: S_epic_remove_output,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.RemoveOutput", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Removes the Output node exposing the named output. Available on UE 5.9; absent from UE 5.8. Params: graph, outputName", "epic_call_tool"),
  },
  epic_remove_row: {
    epicSchema: S_epic_remove_row,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.remove_row", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.RemoveRow", engines: ["5.9"], schema: S_epic_remove_row__1, rename: {"curve_table":"curveTable","row_name":"rowName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Removes a row from the curve table. Params: curve_table, row_name. On UE 5.9 it calls EditorToolset.CurveTableToolset.RemoveRow (Params: curveTable, rowName).", "epic_call_tool"),
  },
  epic_remove_rows: {
    epicSchema: S_epic_remove_rows,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.remove_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.RemoveRows", engines: ["5.9"], schema: S_epic_remove_rows__1, rename: {"data_table":"dataTable","row_names":"rowNames"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Removes rows from the data table. Params: data_table, row_names. On UE 5.9 it calls EditorToolset.DataTableToolset.RemoveRows (Params: dataTable, rowNames).", "epic_call_tool"),
  },
  epic_remove_user_asset_tags: {
    epicSchema: S_epic_remove_user_asset_tags,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.RemoveUserAssetTags", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.AssetToolset] Removes the given User Asset Tags from an asset. Pass tag names without the UAT. prefix. Available on UE 5.9; absent from UE 5.8. Params: assetPath, tags", "epic_call_tool"),
  },
  epic_remove_variable: {
    epicSchema: S_epic_remove_variable,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.RemoveVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Removes a variable from a struct. Available on UE 5.9; absent from UE 5.8. Params: struct, variableName", "epic_call_tool"),
  },
  epic_rename_row: {
    epicSchema: S_epic_rename_row,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.rename_row", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.RenameRow", engines: ["5.9"], schema: S_epic_rename_row__1, rename: {"curve_table":"curveTable","row_name":"rowName","new_row_name":"newRowName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Renames a row in the curve table. Params: curve_table, row_name, new_row_name. On UE 5.9 it calls EditorToolset.CurveTableToolset.RenameRow (Params: curveTable, rowName, newRowName).", "epic_call_tool"),
  },
  epic_rename_rows: {
    epicSchema: S_epic_rename_rows,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.rename_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.RenameRows", engines: ["5.9"], schema: S_epic_rename_rows__1, rename: {"data_table":"dataTable"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Renames one or more rows in the data table. Params: data_table, renames. On UE 5.9 it calls EditorToolset.DataTableToolset.RenameRows (Params: dataTable, renames).", "epic_call_tool"),
  },
  epic_rename_variable: {
    epicSchema: S_epic_rename_variable,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.RenameVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Renames a variable. Available on UE 5.9; absent from UE 5.8. Params: struct, oldName, newName", "epic_call_tool"),
  },
  epic_sample_gradient: {
    epicSchema: S_epic_sample_gradient,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.SampleGradient", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.CurveAtlasToolset] Evaluates a gradient with the asset's hue, saturation, brightness, vibrance and alpha-range adjustments applied, so the result can differ from the keyed values. Available on UE 5.9; absent from UE 5.8. Params: gradient, time", "epic_call_tool"),
  },
  epic_save_asset: {
    epicSchema: S_epic_save_asset,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.SaveAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.AssetToolset] Saves an asset to disk. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_save_assets: {
    epicSchema: S_epic_save_assets,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.save_assets", engines: ["5.8"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Saves assets to disk. Available on UE 5.8; absent from UE 5.9. Params: asset_paths", "epic_call_tool"),
  },
  epic_search: {
    epicSchema: S_epic_search,
    epicTool: { toolset: "SemanticSearchToolset.SemanticSearchToolset", name: "SemanticSearchToolset.SemanticSearchToolset.Search", engines: ["5.8"] },
    ...bp("read", "[Epic SemanticSearchToolset.SemanticSearchToolset] Run a semantic search over the Content Browser assets indexed by the SemanticSearch plugin. Available on UE 5.8; absent from UE 5.9. Params: query, classFilter, pathRegexes, k?", "epic_call_tool"),
  },
  epic_search_row_structs: {
    epicSchema: S_epic_search_row_structs,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.search_row_structs", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.SearchRowStructs", engines: ["5.9"], schema: S_epic_search_row_structs__1, rename: {"struct_name":"structName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Finds structs that can be used as a DataTable schema. Params: struct_name?. On UE 5.9 it calls EditorToolset.DataTableToolset.SearchRowStructs (Params: structName?).", "epic_call_tool"),
  },
  epic_set_alpha_stops: {
    epicSchema: S_epic_set_alpha_stops,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.SetAlphaStops", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Replaces the alpha stops of a gradient, leaving its color keys untouched. Available on UE 5.9; absent from UE 5.8. Params: gradient, stops", "epic_call_tool"),
  },
  epic_set_bitflags: {
    epicSchema: S_epic_set_bitflags,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.SetBitflags", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedEnumToolset] Sets whether the enum is treated as a bitflags type. Available on UE 5.9; absent from UE 5.8. Params: enum, bEnabled", "epic_call_tool"),
  },
  epic_set_channel_keys: {
    epicSchema: S_epic_set_channel_keys,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.SetChannelKeys", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Replaces the raw keys of one channel of a gradient, leaving the other channels untouched. Tangents of keys set to an automatic tangent mode are recomputed; authored tangents are kept. Available on UE 5.9; absent from UE 5.8. Params: gradient, channel, keys", "epic_call_tool"),
  },
  epic_set_collision_complexity: {
    epicSchema: S_epic_set_collision_complexity,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.SetCollisionComplexity", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.StaticMeshToolset] Sets the collision trace behavior of a static mesh. Available on UE 5.9; absent from UE 5.8. Params: mesh, complexity", "epic_call_tool"),
  },
  epic_set_color_stops: {
    epicSchema: S_epic_set_color_stops,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.SetColorStops", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Replaces the color stops of a gradient, leaving its alpha keys untouched. Available on UE 5.9; absent from UE 5.8. Params: gradient, stops", "epic_call_tool"),
  },
  epic_set_display_name: {
    epicSchema: S_epic_set_display_name,
    epicTool: { toolset: "EditorToolset.UserDefinedEnumToolset", name: "EditorToolset.UserDefinedEnumToolset.SetDisplayName", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedEnumToolset] Sets the display name of the enumerator at the given index. Available on UE 5.9; absent from UE 5.8. Params: enum, index, displayName", "epic_call_tool"),
  },
  epic_set_entry: {
    epicSchema: S_epic_set_entry,
    epicTool: { toolset: "editor_toolset.toolsets.string_table.StringTableTools", name: "editor_toolset.toolsets.string_table.StringTableTools.set_entry", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StringTableToolset", name: "EditorToolset.StringTableToolset.SetEntry", engines: ["5.9"], schema: S_epic_set_entry__1, rename: {"string_table":"stringTable"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.string_table.StringTableTools] Adds or updates an entry in the string table. If the key already exists its value is replaced; otherwise a new entry is created. Params: string_table, key, value. On UE 5.9 it calls EditorToolset.StringTableToolset.SetEntry (Params: stringTable, key, value).", "epic_call_tool"),
  },
  epic_set_gradients: {
    epicSchema: S_epic_set_gradients,
    epicTool: { toolset: "EditorToolset.CurveAtlasToolset", name: "EditorToolset.CurveAtlasToolset.SetGradients", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.CurveAtlasToolset] Replaces the gradients of an atlas, rebuilding its texture once. Available on UE 5.9; absent from UE 5.8. Params: atlas, gradients", "epic_call_tool"),
  },
  epic_set_keys: {
    epicSchema: S_epic_set_keys,
    epicTool: { toolset: "editor_toolset.toolsets.curve_table.CurveTableTools", name: "editor_toolset.toolsets.curve_table.CurveTableTools.set_keys", engines: ["5.8"], successors: [{ toolset: "EditorToolset.CurveTableToolset", name: "EditorToolset.CurveTableToolset.SetKeys", engines: ["5.9"], schema: S_epic_set_keys__1, rename: {"curve_table":"curveTable","row_name":"rowName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Replaces all keys in a row with the provided list. Params: curve_table, row_name, keys. On UE 5.9 it calls EditorToolset.CurveTableToolset.SetKeys (Params: curveTable, rowName, keys).", "epic_call_tool"),
  },
  epic_set_lod_thresholds: {
    epicSchema: S_epic_set_lod_thresholds,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_lod_thresholds", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.SetLodThresholds", engines: ["5.9"], schema: S_epic_set_lod_thresholds__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Sets the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's screen height to the viewport height. A value of 1.0 means the mesh fills the full viewport height; values above 1.0 are valid and mean the mesh must appear larger than the viewport before the next LOD activates. Thresholds must be in strictly descending order (LOD 0 has the largest threshold), and there must be exactly one threshold per LOD. Params: mesh, thresholds. On UE 5.9 it calls EditorToolset.StaticMeshToolset.SetLodThresholds (Params: mesh, thresholds).", "epic_call_tool"),
  },
  epic_set_material: {
    epicSchema: S_epic_set_material,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_material", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.SetMaterial", engines: ["5.9"], schema: S_epic_set_material__1, rename: {"slot_name":"slotName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Assigns a material to a named slot on a static mesh asset. This affects all instances of the mesh that do not override the slot material. Use set_component_material_override to change materials on a single instance. Params: mesh, slot_name, material. On UE 5.9 it calls EditorToolset.StaticMeshToolset.SetMaterial (Params: mesh, slotName, material).", "epic_call_tool"),
  },
  epic_set_nanite_enabled: {
    epicSchema: S_epic_set_nanite_enabled,
    epicTool: { toolset: "editor_toolset.toolsets.static_mesh.StaticMeshTools", name: "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_nanite_enabled", engines: ["5.8"], successors: [{ toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.SetNaniteEnabled", engines: ["5.9"], schema: S_epic_set_nanite_enabled__1, rename: {"enabled":"bEnabled"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Enables or disables Nanite for a static mesh. Changing this setting triggers a mesh rebuild. Nanite is most beneficial for high-polygon meshes. Low-polygon meshes may not benefit from Nanite. Params: mesh, enabled. On UE 5.9 it calls EditorToolset.StaticMeshToolset.SetNaniteEnabled (Params: mesh, bEnabled).", "epic_call_tool"),
  },
  epic_set_node_alias: {
    epicSchema: S_epic_set_node_alias,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.SetNodeAlias", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Renames a node by setting its primary pin's alias name. For Output nodes this additionally syncs OutputSettings.OutputName so the bake's export filename matches the new node name. For other node types, sets the alias on the node's first output pin (or first input pin for input-only nodes like Scalar/Color/Texture). Available on UE 5.9; absent from UE 5.8. Params: node, newAlias", "epic_call_tool"),
  },
  epic_set_output_settings: {
    epicSchema: S_epic_set_output_settings,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.SetOutputSettings", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Typed shortcut for the most common Output-node configuration. Sets BaseName, FolderPath, and TexturePresetType (cascading preset defaults for Compression/LODGroup/sRGB), then syncs the node's display name and export name to BaseName. Available on UE 5.9; absent from UE 5.8. Params: outputNode, baseName, folderPath, presetType", "epic_call_tool"),
  },
  epic_set_pin_value: {
    epicSchema: S_epic_set_pin_value,
    epicTool: { toolset: "TextureGraphToolset.TextureGraphTools", name: "TextureGraphToolset.TextureGraphTools.SetPinValue", engines: ["5.9"] },
    ...bp("unknown", "[Epic TextureGraphToolset.TextureGraphTools] Sets one input pin's value on a node, resolving the pin by argument name or alias. For a polymorphic Variant pin (which takes its value from a connected node, not a string) this creates an inline constant node matching the value (a scalar, color, or vector inferred from the value string) and connects it to the pin, replacing any existing source. Re-setting an ordinary pin to the value it already holds is an idempotent success. Available on UE 5.9; absent from UE 5.8. Params: node, pinName, value", "epic_call_tool"),
  },
  epic_set_quality_level_min_lod: {
    epicSchema: S_epic_set_quality_level_min_lod,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.SetQualityLevelMinLod", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.StaticMeshToolset] Merges per-quality-level Min LOD overrides into a static mesh. Quality levels not listed in Overrides keep their current value; pass -1 to clear a level's override. Leaves the fallback Min LOD unchanged. Available on UE 5.9; absent from UE 5.8. Params: mesh, overrides", "epic_call_tool"),
  },
  epic_set_quality_level_min_lod_default: {
    epicSchema: S_epic_set_quality_level_min_lod_default,
    epicTool: { toolset: "EditorToolset.StaticMeshToolset", name: "EditorToolset.StaticMeshToolset.SetQualityLevelMinLodDefault", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.StaticMeshToolset] Sets the fallback Min LOD for quality levels with no explicit override. Leaves existing per-quality-level overrides unchanged. Available on UE 5.9; absent from UE 5.8. Params: mesh, default", "epic_call_tool"),
  },
  epic_set_rows: {
    epicSchema: S_epic_set_rows,
    epicTool: { toolset: "editor_toolset.toolsets.data_table.DataTableTools", name: "editor_toolset.toolsets.data_table.DataTableTools.set_rows", engines: ["5.8"], successors: [{ toolset: "EditorToolset.DataTableToolset", name: "EditorToolset.DataTableToolset.SetRows", engines: ["5.9"], schema: S_epic_set_rows__1, rename: {"data_table":"dataTable"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.data_table.DataTableTools] Sets column values for one or more rows. Params: data_table, values. On UE 5.9 it calls EditorToolset.DataTableToolset.SetRows (Params: dataTable, values).", "epic_call_tool"),
  },
  epic_set_tooltip: {
    epicSchema: S_epic_set_tooltip,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.SetTooltip", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Sets the struct-level developer tooltip. Available on UE 5.9; absent from UE 5.8. Params: struct, tooltip", "epic_call_tool"),
  },
  epic_set_variable_default_value: {
    epicSchema: S_epic_set_variable_default_value,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.SetVariableDefaultValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Sets a variable's default value. Available on UE 5.9; absent from UE 5.8. Params: struct, variableName, defaultValue", "epic_call_tool"),
  },
  epic_set_variable_editable_on_instance: {
    epicSchema: S_epic_set_variable_editable_on_instance,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.SetVariableEditableOnInstance", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Sets whether a variable is editable on instances of the struct. Available on UE 5.9; absent from UE 5.8. Params: struct, variableName, bEditableOnInstance", "epic_call_tool"),
  },
  epic_set_variable_save_game: {
    epicSchema: S_epic_set_variable_save_game,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.SetVariableSaveGame", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Sets whether a variable participates in SaveGame archives. Available on UE 5.9; absent from UE 5.8. Params: struct, variableName, bSaveGame", "epic_call_tool"),
  },
  epic_set_variable_tooltip: {
    epicSchema: S_epic_set_variable_tooltip,
    epicTool: { toolset: "EditorToolset.UserDefinedStructToolset", name: "EditorToolset.UserDefinedStructToolset.SetVariableTooltip", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.UserDefinedStructToolset] Sets a variable's developer tooltip. Available on UE 5.9; absent from UE 5.8. Params: struct, variableName, tooltip", "epic_call_tool"),
  },
  epic_update_metadata_tags: {
    epicSchema: S_epic_update_metadata_tags,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.update_metadata_tags", engines: ["5.8"], successors: [{ toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.UpdateMetadataTags", engines: ["5.9"], schema: S_epic_update_metadata_tags__1, rename: {"asset_path":"assetPath","set_tags":"setTags","remove_tags":"removeTags"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Sets or removes metadata tags on an asset. Params: asset_path, set_tags?, remove_tags?. On UE 5.9 it calls EditorToolset.AssetToolset.UpdateMetadataTags (Params: assetPath, setTags?, removeTags?).", "epic_call_tool"),
  },
  epic_validate_assets: {
    epicSchema: S_epic_validate_assets,
    epicTool: { toolset: "EditorToolset.AssetToolset", name: "EditorToolset.AssetToolset.ValidateAssets", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.AssetToolset] Validates the given assets and returns the errors and warnings found. Available on UE 5.9; absent from UE 5.8. Params: assetPaths", "epic_call_tool"),
  },
  epic_write_file: {
    epicSchema: S_epic_write_file,
    epicTool: { toolset: "editor_toolset.toolsets.asset.AssetTools", name: "editor_toolset.toolsets.asset.AssetTools.write_file", engines: ["5.8"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.asset.AssetTools] Writes text content to a file on disk. Only files under /Game/, an enabled plugin's Content/ directory, or the project Saved/ directory may be written. Only plain text formats are supported. Overwrites the file if it already exists. Available on UE 5.8; absent from UE 5.9. Params: file_path, content", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset_name: z.string().optional(),
  asset_path: z.string().optional(),
  asset_paths: z.array(z.unknown()).optional(),
  asset_type: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  assetName: z.string().optional().describe("Name of the new asset; empty for default (\"CA_Converted_<source>\")."),
  assetPath: z.unknown().optional().describe("SoftObjectPath of the reference asset."),
  assetPaths: z.array(z.unknown()).optional().describe("The content paths of the assets to validate."),
  assetType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The specific kind of DataAsset to make."),
  atlas: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The atlas to modify."),
  bAbove: z.boolean().optional().describe("True to move above the anchor, false to move below."),
  base_radius: z.number().optional().describe("Radius at the base of the cone in Unreal units."),
  baseName: z.string().optional().describe("Exported asset filename (e.g. \"BaseColor\")."),
  bCombineMeshes: z.boolean().optional().describe("When true, all meshes in the file merge into a single StaticMesh; when false, each mesh produces its own."),
  bDeleteRedirectors: z.boolean().optional().describe("When true, also deletes each redirector after its references are fixed up."),
  bEditableOnInstance: z.boolean().optional().describe("Whether the variable can be edited on instances."),
  bEnabled: z.boolean().optional().describe("True to enable Nanite, false to disable it."),
  bImportMaterials: z.boolean().optional().describe("When true, create Material assets for materials referenced in the file; when false, meshes import with no materials assigned."),
  bImportTextures: z.boolean().optional().describe("When true, create Texture2D assets for textures referenced by imported materials; only effective when bImportMaterials is true."),
  bRecursive: z.boolean().optional().describe("Subfolders will be searched when this is set to true."),
  bReplaceExisting: z.boolean().optional().describe("Overwrite an existing CurveTable of the same name instead of failing. A file the CSV reader cannot read fails the import and leaves the existing table untouche…"),
  bSave: z.boolean().optional().describe("Save the imported asset; otherwise it is left unsaved."),
  bSaveGame: z.boolean().optional().describe("Whether the variable is included in SaveGame serialization."),
  bWritable: z.boolean().optional().describe("If true, only returns roots you can actually create/write to (e.g. a UEFN/Valkyrie project narrows this to plugins it owns) -- use this instead of trying Creat…"),
  channel: z.string().optional().describe("The channel to read."),
  chaosClothAssetPath: z.string().optional().describe("Object path to the source UChaosClothAssetBase (cloth or outfit asset)."),
  classFilter: z.array(z.unknown()).optional().describe("Same semantics as Search::ClassFilter. See Search for the list of currently-indexed base classes. Pass an empty array for no class filter."),
  clothingAssetName: z.string().optional().describe("Name of the Clothing Asset on the mesh (e.g., from CreateClothingAsset or ListClothingAssets)."),
  clothingLodIndex: z.number().optional().describe("LOD index within the clothing asset to bind. For ChaosClothAsset-derived types (bRequiresMatchingLodIndex == true in ListClothingAssets), pass the same value a…"),
  combine_meshes: z.boolean().optional(),
  complexity: z.string().optional().describe("The collision trace behavior to apply. CTF_UseDefault defers to the project's DefaultShapeComplexity setting. Value display names: CTF_UseDefault = \"Project De…"),
  component: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Target DynamicMeshComponent."),
  contains: z.string().optional().describe("If set, only returns roots containing this string (case-insensitive). A project can have well over a hundred roots (one per content plugin), so pass this to lo…"),
  content: z.string().optional(),
  curve_table: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  curveTable: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The CurveTable to modify."),
  data_table: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  dataTable: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The DataTable to modify."),
  default: z.number().optional().describe("Fallback Min LOD for quality levels with no explicit override."),
  default_value: z.number().optional(),
  defaultValue: z.unknown().optional().describe("The default value returned when sampling outside the key range. If unset, no default value is applied."),
  dimensions: z.record(z.string(), z.unknown()).optional().describe("Box size along x, y, z in Unreal units."),
  displayName: z.string().optional().describe("The display name for the new enumerator. If empty, a default name is used."),
  enabled: z.boolean().optional(),
  enum: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The enum to modify."),
  expressionClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("An input-parameter expression type (a UTG_Expression_InputParam subclass)."),
  file_path: z.string().optional(),
  filePath: z.string().optional().describe("The absolute path of the .png file to write."),
  filter: z.record(z.string(), z.unknown()).optional().describe("The search criteria. Defaults to searching the entire project with no filtering."),
  folder_path: z.string().optional(),
  folderPath: z.string().optional().describe("The content-browser folder to create the asset in."),
  fromIndex: z.number().optional().describe("The current index of the enumerator."),
  fromNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Source node (output side)."),
  fromPin: z.string().optional().describe("Name of the output pin on FromNode."),
  gradient: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The gradient to append. Must not already be in the atlas."),
  gradients: z.array(z.unknown()).optional().describe("The gradients to place in the atlas, in row order. Must not contain nulls or duplicates."),
  graph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The TextureGraph to modify."),
  height: z.number().optional().describe("Cone height along the local +Z axis in Unreal units."),
  hull_count: z.number().optional(),
  hull_precision: z.number().optional(),
  hullCount: z.number().optional().describe("The number of convex hulls to generate."),
  hullPrecision: z.number().optional().describe("Controls the voxel precision of the decomposition."),
  import_materials: z.boolean().optional(),
  import_textures: z.boolean().optional(),
  include_engine: z.boolean().optional(),
  index: z.number().optional().describe("The index of the enumerator to remove."),
  interp_mode: z.string().optional(),
  interpMode: z.string().optional().describe("Interpolation mode applied to every imported key. Must be RCIM_Linear or RCIM_Constant. Value display names: RCIM_Linear = \"Linear\", RCIM_Constant = \"Constant\"…"),
  itemNames: z.array(z.unknown()).optional().describe("Item names to retrieve."),
  k: z.number().optional().describe("Maximum number of results to return. Must be >= 1. Defaults to 10."),
  key: z.unknown().optional().describe("The key to add, containing the time and value."),
  keys: z.array(z.unknown()).optional().describe("The keys to set, ordered by strictly increasing time."),
  lod_index: z.number().optional(),
  lodIndex: z.number().optional().describe("Skeletal mesh LOD index to bind to."),
  major_radius: z.number().optional().describe("Distance from the torus centre to the centre of the tube, in Unreal units."),
  material: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  max_hull_verts: z.number().optional(),
  maxHullVerts: z.number().optional().describe("The maximum number of vertices per hull."),
  mesh: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  minor_radius: z.number().optional().describe("Radius of the tube cross-section, in Unreal units."),
  name: z.string().optional(),
  new_path: z.string().optional(),
  new_row_name: z.string().optional(),
  newAlias: z.string().optional().describe("The new display name."),
  newAssetPath: z.string().optional().describe("Content path to the new CurveTable."),
  newName: z.string().optional().describe("New friendly name. Must be non-empty and unique within the struct."),
  newPath: z.string().optional().describe("The location of the copy."),
  newRowName: z.string().optional().describe("The new name for the row."),
  node: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The node to remove."),
  objectClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The class of object this variable will reference."),
  oldAssetPath: z.string().optional().describe("Content path to the old CurveTable."),
  oldName: z.string().optional().describe("Current friendly name of the variable."),
  outputFolder: z.string().optional().describe("Content-browser folder for the baked textures (e.g. \"/Game/Textures/Baked\")."),
  outputName: z.string().optional().describe("Desired output name; None leaves the default name."),
  outputNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Node whose expression is UTG_Expression_Output."),
  outputPackagePath: z.string().optional().describe("Folder where the new asset should be created (e.g. \"/Game/Cloth/\")."),
  overrides: z.record(z.string(), z.unknown()).optional().describe("Mapping of quality level to Min LOD override, or -1 to clear that level's override."),
  paramName: z.string().optional().describe("Desired parameter name; None leaves the type's default name."),
  path: z.string().optional().describe("The project relative path at which to create the folder."),
  pathRegexes: z.array(z.unknown()).optional().describe("Same semantics as Search::PathRegexes. Pass an empty array for no path filter."),
  paths: z.array(z.unknown()).optional().describe("The internal asset/folder paths to convert."),
  pinName: z.string().optional().describe("Argument name or alias of the input pin."),
  posX: z.number().optional().describe("Editor-graph X position for the new node (defaults to 0)."),
  posY: z.number().optional().describe("Editor-graph Y position for the new node (defaults to 0)."),
  presetType: z.string().optional().describe("Which preset's defaults to cascade onto Compression/LODGroup/sRGB. ETG_TexturePresetType::None applies no cascade (leaves those settings untouched)."),
  query: z.string().optional().describe("Natural-language query. Must be non-empty."),
  radius: z.number().optional().describe("Cylinder radius in Unreal units."),
  recursive: z.boolean().optional(),
  registryName: z.string().optional().describe("The registry name."),
  relativeToName: z.string().optional().describe("Friendly name of the anchor variable."),
  remove_tags: z.array(z.unknown()).optional(),
  removeTags: z.array(z.unknown()).optional().describe("Tag names to remove. Removing a tag that does not exist is a no-op."),
  renames: z.record(z.string(), z.unknown()).optional().describe("A mapping of existing row names to their new names."),
  root_path: z.string().optional(),
  rootPath: z.string().optional().describe("The project relative path to the folder to search from."),
  row_name: z.string().optional(),
  row_names: z.array(z.unknown()).optional(),
  rowName: z.string().optional().describe("The name of the row to modify."),
  rowNames: z.array(z.unknown()).optional().describe("The names for the new rows."),
  schema: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  sectionIndex: z.number().optional().describe("Section index within the LOD to bind to."),
  set_tags: z.record(z.string(), z.unknown()).optional(),
  setTags: z.array(z.unknown()).optional().describe("Tag key/value pairs to set. Values are overwritten if the key already exists."),
  skeletalMeshPath: z.string().optional().describe("Object path to the target USkeletalMesh asset."),
  slot_name: z.string().optional(),
  slotName: z.string().optional().describe("The name of the material slot to query."),
  source_file: z.string().optional(),
  sourceFile: z.string().optional().describe("The absolute path to the source file on disk."),
  spec: z.record(z.string(), z.unknown()).optional().describe("Variable spec. Name must be non-empty and unique. ElementType is ignored."),
  stops: z.array(z.unknown()).optional().describe("The stops to set, ordered by strictly increasing time. Empty clears all alpha keys, leaving the gradient fully opaque."),
  string_table: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  stringTable: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The StringTable asset to query."),
  struct: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The struct to modify."),
  struct_name: z.string().optional(),
  structFilter: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("If non-null, only registries whose item struct inherits from this struct are returned."),
  structName: z.string().optional().describe("If set, will filter structs by name using wildcard match."),
  structType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The script-struct type for the new variable."),
  tags: z.unknown().optional().describe("Tag names to add."),
  texture: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  textureSize: z.number().optional().describe("Width in pixels that each gradient is baked to. Must be at least 2."),
  thresholds: z.array(z.unknown()).optional().describe("Screen-size threshold values, one per LOD, in descending order."),
  time: z.number().optional().describe("Position along the gradient; atlas rows bake the range 0 to 1."),
  toIndex: z.number().optional().describe("The index to move it to."),
  toNode: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Destination node (input side)."),
  tooltip: z.string().optional().describe("The new tooltip text."),
  top_radius: z.number().optional().describe("Radius at the top of the cone in Unreal units. Set to 0 for a pointed cone; positive values produce a truncated cone (frustum)."),
  toPin: z.string().optional().describe("Name of the input pin on ToNode."),
  transform: z.record(z.string(), z.unknown()).optional().describe("The primitive transform in the mesh's local space."),
  triangle_percents: z.array(z.unknown()).optional(),
  trianglePercents: z.array(z.unknown()).optional().describe("Triangle retention fraction for each generated LOD."),
  typeName: z.string().optional().describe("One of the supported type names above."),
  value: z.string().optional().describe("The source string value for the entry."),
  values: z.string().optional().describe("A JSON object mapping row names to objects of property names and values to update. Only specified properties are updated; others remain unchanged."),
  variableName: z.string().optional().describe("Friendly name of the variable to move."),
};
