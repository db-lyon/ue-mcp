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

const S_epic_add_key = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"key":{"type":"object"}},"required":["curve_table","row_name","key"]} as const;
const S_epic_add_row = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"default_value":{"type":"number"}},"required":["curve_table","row_name"]} as const;
const S_epic_add_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"row_names":{"type":"array"}},"required":["data_table","row_names"]} as const;
const S_epic_can_edit_asset = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_create = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create__data_asset_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"asset_type":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","asset_type"]} as const;
const S_epic_create__data_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"schema":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","schema"]} as const;
const S_epic_create__string_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create_folder = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_delete = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_duplicate = {"properties":{"path":{"type":"string"},"new_path":{"type":"string"}},"required":["path","new_path"]} as const;
const S_epic_exists = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_find_assets = {"properties":{"folder_path":{"type":"string"},"name":{"type":"string"},"asset_type":{"type":"object","properties":{"refPath":{}}},"recursive":{"type":"boolean"},"tags":{"type":"object"}},"required":["folder_path","name"]} as const;
const S_epic_find_similar = {"properties":{"assetPath":{"type":"object","properties":{"refPath":{}}},"classFilter":{"type":"array"},"pathRegexes":{"type":"array"},"k":{"type":"integer"}},"required":["assetPath","classFilter","pathRegexes"]} as const;
const S_epic_generate_convex_collisions = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"hull_count":{"type":"integer"},"max_hull_verts":{"type":"integer"},"hull_precision":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_generate_lods = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"triangle_percents":{"type":"array"}},"required":["mesh","triangle_percents"]} as const;
const S_epic_get_asset_class = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_asset_tags = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_bounds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_dependencies = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_entry = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"}},"required":["string_table","key"]} as const;
const S_epic_get_items = {"properties":{"registryName":{"type":"string"},"itemNames":{"type":"array"}},"required":["registryName","itemNames"]} as const;
const S_epic_get_keys = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"}},"required":["curve_table","row_name"]} as const;
const S_epic_get_lod_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_lod_thresholds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"}},"required":["mesh","slot_name"]} as const;
const S_epic_get_material_slots = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_metadata_tags = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_namespace = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}}},"required":["string_table"]} as const;
const S_epic_get_plugin_content_paths = {"properties":{"include_engine":{"type":"boolean"}}} as const;
const S_epic_get_referencers = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_get_registry_info = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_get_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"row_names":{"type":"array"}},"required":["data_table","row_names"]} as const;
const S_epic_get_schema = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_get_schema__data_table_tools = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}}},"required":["data_table"]} as const;
const S_epic_get_size = {"properties":{"texture":{"type":"object","properties":{"refPath":{}}}},"required":["texture"]} as const;
const S_epic_get_table_id = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}}},"required":["string_table"]} as const;
const S_epic_get_triangle_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_vertex_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_import_file = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"interp_mode":{"type":"string"}},"required":["folder_path","asset_name","source_file","interp_mode"]} as const;
const S_epic_import_file__data_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"schema":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","source_file","schema"]} as const;
const S_epic_import_file__static_mesh_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"import_materials":{"type":"boolean"},"import_textures":{"type":"boolean"},"combine_meshes":{"type":"boolean"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_import_file__string_table_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_import_file__texture_tools = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_is_checked_out = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_is_dirty = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_is_nanite_enabled = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_list_data_sources = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_list_folders = {"properties":{"root_path":{"type":"string"},"recursive":{"type":"boolean"}},"required":["root_path"]} as const;
const S_epic_list_items = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_list_keys = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}}},"required":["string_table"]} as const;
const S_epic_list_registries = {"properties":{"structFilter":{"type":"object","properties":{"refPath":{}}}}} as const;
const S_epic_list_rows = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}}},"required":["curve_table"]} as const;
const S_epic_list_rows__data_table_tools = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}}},"required":["data_table"]} as const;
const S_epic_list_runtime_sources = {"properties":{"registryName":{"type":"string"}},"required":["registryName"]} as const;
const S_epic_load_asset = {"properties":{"asset_path":{"type":"string"}},"required":["asset_path"]} as const;
const S_epic_move = {"properties":{"path":{"type":"string"},"new_path":{"type":"string"}},"required":["path","new_path"]} as const;
const S_epic_read_file = {"properties":{"file_path":{"type":"string"}},"required":["file_path"]} as const;
const S_epic_remove_collisions = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_remove_entry = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"}},"required":["string_table","key"]} as const;
const S_epic_remove_lods = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_remove_row = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"}},"required":["curve_table","row_name"]} as const;
const S_epic_remove_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"row_names":{"type":"array"}},"required":["data_table","row_names"]} as const;
const S_epic_rename_row = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"new_row_name":{"type":"string"}},"required":["curve_table","row_name","new_row_name"]} as const;
const S_epic_rename_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"renames":{"type":"object"}},"required":["data_table","renames"]} as const;
const S_epic_save_assets = {"properties":{"asset_paths":{"type":"array"}},"required":["asset_paths"]} as const;
const S_epic_search = {"properties":{"query":{"type":"string"},"classFilter":{"type":"array"},"pathRegexes":{"type":"array"},"k":{"type":"integer"}},"required":["query","classFilter","pathRegexes"]} as const;
const S_epic_search_row_structs = {"properties":{"struct_name":{"type":"string"}}} as const;
const S_epic_set_entry = {"properties":{"string_table":{"type":"object","properties":{"refPath":{}}},"key":{"type":"string"},"value":{"type":"string"}},"required":["string_table","key","value"]} as const;
const S_epic_set_keys = {"properties":{"curve_table":{"type":"object","properties":{"refPath":{}}},"row_name":{"type":"string"},"keys":{"type":"array"}},"required":["curve_table","row_name","keys"]} as const;
const S_epic_set_lod_thresholds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"thresholds":{"type":"array"}},"required":["mesh","thresholds"]} as const;
const S_epic_set_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"},"material":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","slot_name","material"]} as const;
const S_epic_set_nanite_enabled = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"enabled":{"type":"boolean"}},"required":["mesh","enabled"]} as const;
const S_epic_set_rows = {"properties":{"data_table":{"type":"object","properties":{"refPath":{}}},"values":{"type":"string"}},"required":["data_table","values"]} as const;
const S_epic_update_metadata_tags = {"properties":{"asset_path":{"type":"string"},"set_tags":{"type":"object"},"remove_tags":{"type":"array"}},"required":["asset_path"]} as const;
const S_epic_write_file = {"properties":{"file_path":{"type":"string"},"content":{"type":"string"}},"required":["file_path","content"]} as const;

/** 76 wrapped engine tools routed to the `asset` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_key: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Adds a key to a row. Params: curve_table, row_name, key",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.add_key", S_epic_add_key, p),
  ),
  epic_add_row: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Adds a new row to the curve table with an optional default value. Params: curve_table, row_name, default_value?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.add_row", S_epic_add_row, p),
  ),
  epic_add_rows: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Adds new rows with default values to the data table. Params: data_table, row_names",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.add_rows", S_epic_add_rows, p),
  ),
  epic_can_edit_asset: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Checks whether an asset can be edited. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.can_edit_asset", S_epic_can_edit_asset, p),
  ),
  epic_create: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Creates a new CurveTable asset. Params: folder_path, asset_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.create", S_epic_create, p),
  ),
  epic_create__data_asset_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_asset.DataAssetTools] Creates a new DataAsset asset in the project. Params: folder_path, asset_name, asset_type",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_asset.DataAssetTools", "editor_toolset.toolsets.data_asset.DataAssetTools.create", S_epic_create__data_asset_tools, p),
  ),
  epic_create__data_table_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Creates a new DataTable asset with the specified column schema. Params: folder_path, asset_name, schema",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.create", S_epic_create__data_table_tools, p),
  ),
  epic_create__string_table_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Creates a new StringTable asset. Params: folder_path, asset_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.create", S_epic_create__string_table_tools, p),
  ),
  epic_create_folder: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Creates a folder at the specified path. Params: path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.create_folder", S_epic_create_folder, p),
  ),
  epic_delete: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Deletes an asset or folder. Params: path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.delete", S_epic_delete, p),
  ),
  epic_duplicate: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Makes a copy of a folder or asset. Params: path, new_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.duplicate", S_epic_duplicate, p),
  ),
  epic_exists: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Determines if a folder or asset exists. Params: path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.exists", S_epic_exists, p),
  ),
  epic_find_assets: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Searches the project for assets that match specific criteria. Params: folder_path, name, asset_type?, recursive?, tags?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.find_assets", S_epic_find_assets, p),
  ),
  epic_find_similar: bp(
    "read",
    "[Epic SemanticSearchToolset.SemanticSearchToolset] Find assets whose embeddings are semantically similar to the given asset's embedding. Vector-only (no BM25). The source asset must already be indexed by the SemanticSearch plugin. Params: assetPath, classFilter, pathRegexes, k?",
    "epic_call_tool",
    (p) => epicToolCall("SemanticSearchToolset.SemanticSearchToolset", "SemanticSearchToolset.SemanticSearchToolset.FindSimilar", S_epic_find_similar, p),
  ),
  epic_generate_convex_collisions: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Generates convex hull collision shapes for a static mesh. Convex hulls provide accurate collision for physics simulation. More hulls improve accuracy but increase runtime cost. Replaces any existing collision. Params: mesh, hull_count?, max_hull_verts?, hull_precision?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.generate_convex_collisions", S_epic_generate_convex_collisions, p),
  ),
  epic_generate_lods: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Auto-generates LODs for a static mesh using triangle reduction. Each entry in triangle_percents creates one additional LOD. The value is the fraction of triangles to keep relative to LOD 0, from just above 0.0 (nearly empty) to 1.0 (full detail). For example, [0.5, 0.25] creates LOD1 with 50% of the original triangles and LOD2 with 25%. Params: mesh, triangle_percents",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.generate_lods", S_epic_generate_lods, p),
  ),
  epic_get_asset_class: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Gets the class of an asset. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.get_asset_class", S_epic_get_asset_class, p),
  ),
  epic_get_asset_tags: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Gets the asset registry tags for an asset. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.get_asset_tags", S_epic_get_asset_tags, p),
  ),
  epic_get_bounds: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the local-space bounding box of a static mesh. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_bounds", S_epic_get_bounds, p),
  ),
  epic_get_dependencies: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Lists assets that the specified asset depends on. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.get_dependencies", S_epic_get_dependencies, p),
  ),
  epic_get_entry: bp(
    "read",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Returns the source string for a specific key. Params: string_table, key",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.get_entry", S_epic_get_entry, p),
  ),
  epic_get_items: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns cached item data. Items must be loaded in the registry cache to be returned. Params: registryName, itemNames",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.GetItems", S_epic_get_items, p),
  ),
  epic_get_keys: bp(
    "read",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Returns all keys for a row. Params: curve_table, row_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.get_keys", S_epic_get_keys, p),
  ),
  epic_get_lod_count: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the number of LODs in a static mesh asset. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_lod_count", S_epic_get_lod_count, p),
  ),
  epic_get_lod_thresholds: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's screen height to the viewport height. A value of 1.0 means the mesh fills the full viewport height; values above 1.0 are valid and mean the mesh must appear larger than the viewport before the next LOD activates. Each LOD activates when the mesh appears smaller than its threshold. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_lod_thresholds", S_epic_get_lod_thresholds, p),
  ),
  epic_get_material: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the material assigned to a named slot on a static mesh. Params: mesh, slot_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_material", S_epic_get_material, p),
  ),
  epic_get_material_slots: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the names of all material slots in a static mesh. Material slot names are used when assigning materials to specific parts of the mesh. Use these names with get_material and set_material. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_material_slots", S_epic_get_material_slots, p),
  ),
  epic_get_metadata_tags: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Gets the metadata tags for an asset. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.get_metadata_tags", S_epic_get_metadata_tags, p),
  ),
  epic_get_namespace: bp(
    "read",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Returns the namespace of a StringTable asset. Params: string_table",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.get_namespace", S_epic_get_namespace, p),
  ),
  epic_get_plugin_content_paths: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Returns the root content paths for plugins that have content. Params: include_engine?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.get_plugin_content_paths", S_epic_get_plugin_content_paths, p),
  ),
  epic_get_referencers: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Lists assets that reference the specified asset. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.get_referencers", S_epic_get_referencers, p),
  ),
  epic_get_registry_info: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns detailed information about a specific registry. Params: registryName",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.GetRegistryInfo", S_epic_get_registry_info, p),
  ),
  epic_get_rows: bp(
    "read",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Returns the column values for one or more rows as a JSON string. Params: data_table, row_names",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.get_rows", S_epic_get_rows, p),
  ),
  epic_get_schema: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns the item struct schema as JSON. Params: registryName",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.GetSchema", S_epic_get_schema, p),
  ),
  epic_get_schema__data_table_tools: bp(
    "read",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Returns the column schema of the data table as a JSON string. Params: data_table",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.get_schema", S_epic_get_schema__data_table_tools, p),
  ),
  epic_get_size: bp(
    "read",
    "[Epic editor_toolset.toolsets.texture.TextureTools] Returns the dimensions of a Texture2D in pixels. Params: texture",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.texture.TextureTools", "editor_toolset.toolsets.texture.TextureTools.get_size", S_epic_get_size, p),
  ),
  epic_get_table_id: bp(
    "read",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Returns the table ID for a StringTable asset. The table ID is derived from the asset's package path and is used to reference the string table in text properties and localisation. Params: string_table",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.get_table_id", S_epic_get_table_id, p),
  ),
  epic_get_triangle_count: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the number of triangles in a specific LOD of a static mesh. Params: mesh, lod_index?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_triangle_count", S_epic_get_triangle_count, p),
  ),
  epic_get_vertex_count: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns the number of vertices in a specific LOD of a static mesh. Params: mesh, lod_index?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_vertex_count", S_epic_get_vertex_count, p),
  ),
  epic_import_file: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Imports a file from disk as a CurveTable asset. The file's first column is the row name; subsequent columns are sample times and values. interp_mode controls how the imported keys are interpolated between samples. Params: folder_path, asset_name, source_file, interp_mode",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.import_file", S_epic_import_file, p),
  ),
  epic_import_file__data_table_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Imports a file from disk as a DataTable asset. The file's columns must match the property names in schema. Use search_row_structs to discover usable schema structs. Params: folder_path, asset_name, source_file, schema",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.import_file", S_epic_import_file__data_table_tools, p),
  ),
  epic_import_file__static_mesh_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Imports a mesh file from disk as a StaticMesh asset. Params: folder_path, asset_name, source_file, import_materials?, import_textures?, combine_meshes?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.import_file", S_epic_import_file__static_mesh_tools, p),
  ),
  epic_import_file__string_table_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Imports a file from disk as a StringTable asset. The file must have a header row with at least 'Key' and 'SourceString' columns. Additional meta-data columns are imported but the namespace is not - the StringTable's namespace is derived from its asset path. Params: folder_path, asset_name, source_file",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.import_file", S_epic_import_file__string_table_tools, p),
  ),
  epic_import_file__texture_tools: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.texture.TextureTools] Imports an image file from disk as a Texture2D asset. Params: folder_path, asset_name, source_file",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.texture.TextureTools", "editor_toolset.toolsets.texture.TextureTools.import_file", S_epic_import_file__texture_tools, p),
  ),
  epic_is_checked_out: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Checks whether an asset is checked out by the current user. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.is_checked_out", S_epic_is_checked_out, p),
  ),
  epic_is_dirty: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Checks whether an asset has unsaved changes. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.is_dirty", S_epic_is_dirty, p),
  ),
  epic_is_nanite_enabled: bp(
    "read",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Returns whether Nanite is enabled for a static mesh. Nanite is Unreal's virtualized geometry system that renders highly detailed meshes efficiently. It is most beneficial for meshes with many triangles. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.is_nanite_enabled", S_epic_is_nanite_enabled, p),
  ),
  epic_list_data_sources: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns the editor-defined sources configured on a Data Registry. These are the sources as authored on the registry asset, before any runtime expansion of meta sources. Params: registryName",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.ListDataSources", S_epic_list_data_sources, p),
  ),
  epic_list_folders: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Lists the folders contained within a folder. Params: root_path, recursive?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.list_folders", S_epic_list_folders, p),
  ),
  epic_list_items: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns all item names in a Data Registry. Params: registryName",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.ListItems", S_epic_list_items, p),
  ),
  epic_list_keys: bp(
    "read",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Lists all keys in the string table. Params: string_table",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.list_keys", S_epic_list_keys, p),
  ),
  epic_list_registries: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns the names of all registered Data Registries. Params: structFilter?",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.ListRegistries", S_epic_list_registries, p),
  ),
  epic_list_rows: bp(
    "read",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Lists the names of all rows in the curve table. Params: curve_table",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.list_rows", S_epic_list_rows, p),
  ),
  epic_list_rows__data_table_tools: bp(
    "read",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Lists the names of all rows in the data table. Params: data_table",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.list_rows", S_epic_list_rows__data_table_tools, p),
  ),
  epic_list_runtime_sources: bp(
    "read",
    "[Epic DataRegistryToolset.DataRegistryTools] Returns the runtime sources for a Data Registry. This is the expanded list including transient child sources generated from meta sources. Will equal ListDataSources when the registry has no meta sources. Params: registryName",
    "epic_call_tool",
    (p) => epicToolCall("DataRegistryToolset.DataRegistryTools", "DataRegistryToolset.DataRegistryTools.ListRuntimeSources", S_epic_list_runtime_sources, p),
  ),
  epic_load_asset: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Loads an asset from the project. Params: asset_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.load_asset", S_epic_load_asset, p),
  ),
  epic_move: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Moves or renames an asset or folder. Params: path, new_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.move", S_epic_move, p),
  ),
  epic_read_file: bp(
    "read",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Reads a text file from disk and returns its contents. Only files under /Game/, an enabled plugin's Content/ directory, or the project Saved/ directory may be read. Only plain text formats are supported. Params: file_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.read_file", S_epic_read_file, p),
  ),
  epic_remove_collisions: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Removes all collision shapes from a static mesh. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.remove_collisions", S_epic_remove_collisions, p),
  ),
  epic_remove_entry: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Removes an entry from the string table. Params: string_table, key",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.remove_entry", S_epic_remove_entry, p),
  ),
  epic_remove_lods: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Removes all auto-generated LODs from a static mesh, keeping only LOD 0. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.remove_lods", S_epic_remove_lods, p),
  ),
  epic_remove_row: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Removes a row from the curve table. Params: curve_table, row_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.remove_row", S_epic_remove_row, p),
  ),
  epic_remove_rows: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Removes rows from the data table. Params: data_table, row_names",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.remove_rows", S_epic_remove_rows, p),
  ),
  epic_rename_row: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Renames a row in the curve table. Params: curve_table, row_name, new_row_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.rename_row", S_epic_rename_row, p),
  ),
  epic_rename_rows: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Renames one or more rows in the data table. Params: data_table, renames",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.rename_rows", S_epic_rename_rows, p),
  ),
  epic_save_assets: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Saves assets to disk. Params: asset_paths",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.save_assets", S_epic_save_assets, p),
  ),
  epic_search: bp(
    "read",
    "[Epic SemanticSearchToolset.SemanticSearchToolset] Run a semantic search over the Content Browser assets indexed by the SemanticSearch plugin. Params: query, classFilter, pathRegexes, k?",
    "epic_call_tool",
    (p) => epicToolCall("SemanticSearchToolset.SemanticSearchToolset", "SemanticSearchToolset.SemanticSearchToolset.Search", S_epic_search, p),
  ),
  epic_search_row_structs: bp(
    "read",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Finds structs that can be used as a DataTable schema. Params: struct_name?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.search_row_structs", S_epic_search_row_structs, p),
  ),
  epic_set_entry: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.string_table.StringTableTools] Adds or updates an entry in the string table. If the key already exists its value is replaced; otherwise a new entry is created. Params: string_table, key, value",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.string_table.StringTableTools", "editor_toolset.toolsets.string_table.StringTableTools.set_entry", S_epic_set_entry, p),
  ),
  epic_set_keys: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.curve_table.CurveTableTools] Replaces all keys in a row with the provided list. Params: curve_table, row_name, keys",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.curve_table.CurveTableTools", "editor_toolset.toolsets.curve_table.CurveTableTools.set_keys", S_epic_set_keys, p),
  ),
  epic_set_lod_thresholds: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Sets the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's screen height to the viewport height. A value of 1.0 means the mesh fills the full viewport height; values above 1.0 are valid and mean the mesh must appear larger than the viewport before the next LOD activates. Thresholds must be in strictly descending order (LOD 0 has the largest threshold), and there must be exactly one threshold per LOD. Params: mesh, thresholds",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_lod_thresholds", S_epic_set_lod_thresholds, p),
  ),
  epic_set_material: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Assigns a material to a named slot on a static mesh asset. This affects all instances of the mesh that do not override the slot material. Use set_component_material_override to change materials on a single instance. Params: mesh, slot_name, material",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_material", S_epic_set_material, p),
  ),
  epic_set_nanite_enabled: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.static_mesh.StaticMeshTools] Enables or disables Nanite for a static mesh. Changing this setting triggers a mesh rebuild. Nanite is most beneficial for high-polygon meshes. Low-polygon meshes may not benefit from Nanite. Params: mesh, enabled",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.static_mesh.StaticMeshTools", "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_nanite_enabled", S_epic_set_nanite_enabled, p),
  ),
  epic_set_rows: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.data_table.DataTableTools] Sets column values for one or more rows. Params: data_table, values",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.data_table.DataTableTools", "editor_toolset.toolsets.data_table.DataTableTools.set_rows", S_epic_set_rows, p),
  ),
  epic_update_metadata_tags: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Sets or removes metadata tags on an asset. Params: asset_path, set_tags?, remove_tags?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.update_metadata_tags", S_epic_update_metadata_tags, p),
  ),
  epic_write_file: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.asset.AssetTools] Writes text content to a file on disk. Only files under /Game/, an enabled plugin's Content/ directory, or the project Saved/ directory may be written. Only plain text formats are supported. Overwrites the file if it already exists. Params: file_path, content",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.asset.AssetTools.write_file", S_epic_write_file, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset_name: z.string().optional(),
  asset_path: z.string().optional(),
  asset_paths: z.array(z.unknown()).optional(),
  asset_type: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  assetPath: z.union([z.string(), z.record(z.unknown())]).optional().describe("SoftObjectPath of the reference asset."),
  classFilter: z.array(z.unknown()).optional().describe("Same semantics as Search::ClassFilter. See Search for the list of currently-indexed base classes. Pass an empty array for no class filter."),
  combine_meshes: z.boolean().optional(),
  content: z.string().optional(),
  curve_table: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  data_table: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  default_value: z.number().optional(),
  enabled: z.boolean().optional(),
  file_path: z.string().optional(),
  folder_path: z.string().optional(),
  hull_count: z.number().optional(),
  hull_precision: z.number().optional(),
  import_materials: z.boolean().optional(),
  import_textures: z.boolean().optional(),
  include_engine: z.boolean().optional(),
  interp_mode: z.string().optional(),
  itemNames: z.array(z.unknown()).optional().describe("Item names to retrieve."),
  k: z.number().optional().describe("Maximum number of results to return. Must be >= 1. Defaults to 10."),
  key: z.unknown().optional(),
  keys: z.array(z.unknown()).optional(),
  lod_index: z.number().optional(),
  material: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  max_hull_verts: z.number().optional(),
  mesh: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  name: z.string().optional(),
  new_path: z.string().optional(),
  new_row_name: z.string().optional(),
  path: z.string().optional(),
  pathRegexes: z.array(z.unknown()).optional().describe("Same semantics as Search::PathRegexes. Pass an empty array for no path filter."),
  query: z.string().optional().describe("Natural-language query. Must be non-empty."),
  recursive: z.boolean().optional(),
  registryName: z.string().optional().describe("The registry name."),
  remove_tags: z.array(z.unknown()).optional(),
  renames: z.record(z.unknown()).optional(),
  root_path: z.string().optional(),
  row_name: z.string().optional(),
  row_names: z.array(z.unknown()).optional(),
  schema: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  set_tags: z.record(z.unknown()).optional(),
  slot_name: z.string().optional(),
  source_file: z.string().optional(),
  string_table: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  struct_name: z.string().optional(),
  structFilter: z.union([z.string(), z.record(z.unknown())]).optional().describe("If non-null, only registries whose item struct inherits from this struct are returned."),
  tags: z.record(z.unknown()).optional(),
  texture: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  thresholds: z.array(z.unknown()).optional(),
  triangle_percents: z.array(z.unknown()).optional(),
  value: z.string().optional(),
  values: z.string().optional(),
};
