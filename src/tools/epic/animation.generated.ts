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

const S_epic_add_actors = {"properties":{"actors":{"type":"array"}},"required":["actors"]} as const;
const S_epic_add_actors_by_name = {"properties":{"actor_names":{"type":"array"}},"required":["actor_names"]} as const;
const S_epic_add_actors_to_binding = {"properties":{"actors":{"type":"array"},"binding":{"type":"object"}},"required":["actors","binding"]} as const;
const S_epic_add_backward_solve_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig"]} as const;
const S_epic_add_binding_to_folder = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object"}},"required":["folder","binding"]} as const;
const S_epic_add_bone = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"parent":{"type":"object"},"transform":{"type":"object"}},"required":["control_rig","name"]} as const;
const S_epic_add_control = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"parent":{"type":"object"},"settings":{"type":"object"}},"required":["control_rig","name"]} as const;
const S_epic_add_element = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"element_type":{"type":"string"},"parent":{"type":"object"},"transform":{"type":"object"}},"required":["control_rig","name","element_type"]} as const;
const S_epic_add_event_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"event_type":{"type":"string"},"name":{"type":"string"}},"required":["control_rig","event_type"]} as const;
const S_epic_add_event_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"event_type":{"type":"string"},"position":{"type":"object"}},"required":["control_rig","graph","event_type"]} as const;
const S_epic_add_event_repeater_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_event_trigger_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig","name"]} as const;
const S_epic_add_interaction_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig"]} as const;
const S_epic_add_key_bool = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"}},"required":["section","channel_name","frame","value"]} as const;
const S_epic_add_key_float = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"number"},"interpolation":{"type":"string"}},"required":["section","channel_name","frame","value","interpolation"]} as const;
const S_epic_add_key_integer = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"}},"required":["section","channel_name","frame","value"]} as const;
const S_epic_add_key_string = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"string"}},"required":["section","channel_name","frame","value"]} as const;
const S_epic_add_layer_from_selection = {"properties":{}} as const;
const S_epic_add_marked_frame = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"integer"}},"required":["sequence","frame"]} as const;
const S_epic_add_null = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"parent":{"type":"object"},"transform":{"type":"object"}},"required":["control_rig","name"]} as const;
const S_epic_add_root_folder = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["sequence","name"]} as const;
const S_epic_add_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_socket = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"},"bone_name":{"type":"string"}},"required":["mesh","socket_name","bone_name"]} as const;
const S_epic_add_spawnable_from_class = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"actor_class_path":{"type":"string"}},"required":["sequence","actor_class_path"]} as const;
const S_epic_add_spawnable_from_instance = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"obj":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","obj"]} as const;
const S_epic_add_track_to_binding = {"properties":{"binding":{"type":"object"},"track_type":{"type":"object","properties":{"refPath":{}}}},"required":["binding","track_type"]} as const;
const S_epic_add_track_to_folder = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"track":{"type":"object","properties":{"refPath":{}}}},"required":["folder","track"]} as const;
const S_epic_add_track_to_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"track_type":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","track_type"]} as const;
const S_epic_add_variable = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"type_path":{"type":"string"},"is_public":{"type":"boolean"},"is_read_only":{"type":"boolean"},"default_value":{"type":"string"}},"required":["control_rig","name","type_path","default_value"]} as const;
const S_epic_add_variable_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"is_getter":{"type":"boolean"},"position":{"type":"object"}},"required":["control_rig","graph","variable_name"]} as const;
const S_epic_assign_physics_asset = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"physics_asset":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","physics_asset"]} as const;
const S_epic_bake_channel_keys = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["section","channel_name","start_frame","end_frame"]} as const;
const S_epic_bake_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_names":{"type":"array"},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"},"reduce_keys":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["sequence","control_rig_asset_path","control_names","start_frame","end_frame"]} as const;
const S_epic_bake_to_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object"},"control_rig_asset_path":{"type":"string"},"reduce_keys":{"type":"boolean"},"tolerance":{"type":"number"},"reset_controls":{"type":"boolean"}},"required":["sequence","binding","control_rig_asset_path"]} as const;
const S_epic_bake_transform = {"properties":{"bindings":{"type":"array"}},"required":["bindings"]} as const;
const S_epic_blend_values_on_selected = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"operation":{"type":"string"},"blend_value":{"type":"number"}},"required":["sequence","operation","blend_value"]} as const;
const S_epic_change_actor_template_class = {"properties":{"binding":{"type":"object"},"actor_class":{"type":"string"}},"required":["binding","actor_class"]} as const;
const S_epic_change_variable_type = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"new_type":{"type":"string"}},"required":["control_rig","name","new_type"]} as const;
const S_epic_clear_section_condition = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_clear_selection = {"properties":{}} as const;
const S_epic_clear_track_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_clear_track_row_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"row_index":{"type":"integer"}},"required":["track","row_index"]} as const;
const S_epic_close_curve_editor = {"properties":{}} as const;
const S_epic_close_sequence = {"properties":{}} as const;
const S_epic_collapse_anim_layers = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"reduce_keys":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_connect_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"source_pin":{"type":"object","properties":{"refPath":{}}},"target_pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph","source_pin","target_pin"]} as const;
const S_epic_convert_to_custom_binding = {"properties":{"binding":{"type":"object"},"binding_type_class":{"type":"string"}},"required":["binding","binding_type_class"]} as const;
const S_epic_convert_to_possessable = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_convert_to_spawnable = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_copy_bindings = {"properties":{"bindings":{"type":"array"}},"required":["bindings"]} as const;
const S_epic_copy_folders = {"properties":{"folders":{"type":"array"}},"required":["folders"]} as const;
const S_epic_copy_sections = {"properties":{"sections":{"type":"array"}},"required":["sections"]} as const;
const S_epic_copy_tracks = {"properties":{"tracks":{"type":"array"}},"required":["tracks"]} as const;
const S_epic_create = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_create_camera = {"properties":{"spawnable":{"type":"boolean"}}} as const;
const S_epic_create_level_sequence = {"properties":{"package_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["package_path","asset_name"]} as const;
const S_epic_create_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"node_type":{"type":"string"},"position":{"type":"object"},"node_name":{"type":"string"}},"required":["control_rig","graph","node_type","node_name"]} as const;
const S_epic_curve_editor_empty_selection = {"properties":{}} as const;
const S_epic_curve_editor_select_keys = {"properties":{"channel":{"type":"object"},"indices":{"type":"array"}},"required":["channel","indices"]} as const;
const S_epic_delete_all_marked_frames = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_delete_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_delete_marked_frame = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["sequence","index"]} as const;
const S_epic_delete_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph","node"]} as const;
const S_epic_delete_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_disconnect_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"source_pin":{"type":"object","properties":{"refPath":{}}},"target_pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph","source_pin","target_pin"]} as const;
const S_epic_duplicate_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_empty_selection = {"properties":{}} as const;
const S_epic_export_anim_sequence = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"anim_sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object"},"create_link":{"type":"boolean"}},"required":["world","sequence","anim_sequence","binding"]} as const;
const S_epic_export_fbx = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"bindings":{"type":"array"},"fbx_file_path":{"type":"string"},"override_options":{"type":"object","properties":{"refPath":{}}}},"required":["world","sequence","bindings","fbx_file_path"]} as const;
const S_epic_export_fbx_from_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"export_file_path":{"type":"string"},"ascii":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","export_file_path"]} as const;
const S_epic_find_binding_by_name = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["sequence","name"]} as const;
const S_epic_find_binding_by_tag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tag_name":{"type":"string"}},"required":["sequence","tag_name"]} as const;
const S_epic_find_bindings_by_tag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tag_name":{"type":"string"}},"required":["sequence","tag_name"]} as const;
const S_epic_find_marked_frame_by_label = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"label":{"type":"string"}},"required":["sequence","label"]} as const;
const S_epic_find_or_create_track = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object"},"control_rig_asset_path":{"type":"string"},"is_layered":{"type":"boolean"}},"required":["sequence","binding","control_rig_asset_path"]} as const;
const S_epic_find_tracks_by_type = {"properties":{"binding":{"type":"object"},"track_type":{"type":"object","properties":{"refPath":{}}}},"required":["binding","track_type"]} as const;
const S_epic_fix_actor_references = {"properties":{}} as const;
const S_epic_focus_parent_sequence = {"properties":{}} as const;
const S_epic_focus_sub_sequence = {"properties":{"sub_section":{"type":"object","properties":{"refPath":{}}}},"required":["sub_section"]} as const;
const S_epic_force_evaluate = {"properties":{}} as const;
const S_epic_frame_selection = {"properties":{}} as const;
const S_epic_get_actor_transform_at_frame = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"actor_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","actor_name","frame"]} as const;
const S_epic_get_all_binding_tags = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_all_bones = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_all_controls = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_all_nulls = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_anim_layers = {"properties":{}} as const;
const S_epic_get_anim_mode_gizmo_scale = {"properties":{}} as const;
const S_epic_get_anim_mode_hide_manips = {"properties":{}} as const;
const S_epic_get_anim_mode_hierarchy = {"properties":{}} as const;
const S_epic_get_anim_mode_local_spaces = {"properties":{}} as const;
const S_epic_get_anim_mode_nulls = {"properties":{}} as const;
const S_epic_get_anim_mode_only_rig_sel = {"properties":{}} as const;
const S_epic_get_backward_solve_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_binding_id = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object"}},"required":["sequence","binding"]} as const;
const S_epic_get_binding_name = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_binding_tags = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_bindings = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_bone_children = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"bone_name":{"type":"string"}},"required":["mesh","bone_name"]} as const;
const S_epic_get_bone_names = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_bone_parent = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"bone_name":{"type":"string"}},"required":["mesh","bone_name"]} as const;
const S_epic_get_bool = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_bound_objects = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_bounds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_channel_names = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_child_possessables = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_children = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object"},"recursive":{"type":"boolean"}},"required":["control_rig","item"]} as const;
const S_epic_get_clock_source = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_connected_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","pin"]} as const;
const S_epic_get_control_rigs = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_controls_info = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_get_controls_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_name":{"type":"string"}},"required":["section","control_name"]} as const;
const S_epic_get_current_sequence = {"properties":{}} as const;
const S_epic_get_curve_editor_selected_keys = {"properties":{"channel":{"type":"object"}},"required":["channel"]} as const;
const S_epic_get_custom_binding_objects = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_custom_binding_type = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_custom_bindings_of_type = {"properties":{"binding_type_class":{"type":"string"}},"required":["binding_type_class"]} as const;
const S_epic_get_deactivated_nodes = {"properties":{}} as const;
const S_epic_get_default_value = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"}},"required":["section","channel_name"]} as const;
const S_epic_get_display_rate = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_elements = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"element_type":{"type":"string"}},"required":["control_rig"]} as const;
const S_epic_get_euler_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_evaluation_type = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_event_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"event_type":{"type":"string"}},"required":["control_rig","event_type"]} as const;
const S_epic_get_float = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_focused_sequence = {"properties":{}} as const;
const S_epic_get_folder_contents = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_forward_solve_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_global_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object"},"initial":{"type":"boolean"}},"required":["control_rig","item"]} as const;
const S_epic_get_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["control_rig","graph_name"]} as const;
const S_epic_get_int = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_interaction_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_keys = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"}},"required":["section","channel_name"]} as const;
const S_epic_get_keys_by_index = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"indices":{"type":"array"}},"required":["section","channel_name","indices"]} as const;
const S_epic_get_linked_anim_sequences = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_linked_level_sequence = {"properties":{"anim_sequence":{"type":"object","properties":{"refPath":{}}}},"required":["anim_sequence"]} as const;
const S_epic_get_local_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object"},"initial":{"type":"boolean"}},"required":["control_rig","item"]} as const;
const S_epic_get_locked_nodes = {"properties":{}} as const;
const S_epic_get_lod_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_loop_mode = {"properties":{}} as const;
const S_epic_get_marked_frames = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"}},"required":["mesh","slot_name"]} as const;
const S_epic_get_material_slots = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_morph_target_names = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_muted_nodes = {"properties":{}} as const;
const S_epic_get_node_label = {"properties":{"node":{"type":"object"}},"required":["node"]} as const;
const S_epic_get_node_position = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","node"]} as const;
const S_epic_get_outliner_children = {"properties":{"node":{"type":"object"},"type_filter":{"type":"string"}},"required":["node"]} as const;
const S_epic_get_outliner_selection = {"properties":{}} as const;
const S_epic_get_outliner_tree = {"properties":{}} as const;
const S_epic_get_parent = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object"}},"required":["control_rig","item"]} as const;
const S_epic_get_physics_asset = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_pin_value = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","pin"]} as const;
const S_epic_get_pinned_nodes = {"properties":{}} as const;
const S_epic_get_playback_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_playback_speed = {"properties":{}} as const;
const S_epic_get_playhead_frame = {"properties":{}} as const;
const S_epic_get_position = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_priority_order = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_get_root_folders = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_rotator = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_scale = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_section_blend_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_completion_mode = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_condition = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_section_ease_in = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_ease_out = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_post_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_pre_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_properties = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_range = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_to_key = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_sections = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_sections_for_nodes = {"properties":{"nodes":{"type":"array"}},"required":["nodes"]} as const;
const S_epic_get_selected_bindings = {"properties":{}} as const;
const S_epic_get_selected_channels = {"properties":{}} as const;
const S_epic_get_selected_controls = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_get_selected_folders = {"properties":{}} as const;
const S_epic_get_selected_key_channels = {"properties":{}} as const;
const S_epic_get_selected_sections = {"properties":{}} as const;
const S_epic_get_selected_tracks = {"properties":{}} as const;
const S_epic_get_selection_range = {"properties":{}} as const;
const S_epic_get_sequence_lock_state = {"properties":{}} as const;
const S_epic_get_skeleton = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_socket_bone = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"}},"required":["mesh","socket_name"]} as const;
const S_epic_get_socket_names = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_socket_transform = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"}},"required":["mesh","socket_name"]} as const;
const S_epic_get_soloed_nodes = {"properties":{}} as const;
const S_epic_get_sub_sequence_hierarchy = {"properties":{}} as const;
const S_epic_get_tick_resolution = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_track_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_track_display_name = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_track_filter_names = {"properties":{}} as const;
const S_epic_get_track_row_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"row_index":{"type":"integer"}},"required":["track","row_index"]} as const;
const S_epic_get_tracks_on_binding = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_get_tracks_on_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_variable = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig","name"]} as const;
const S_epic_get_vector2d = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_vertex_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_view_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_work_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_world_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_has_section_end_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_has_section_start_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_hide_all_controls = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_import_bones_from_asset = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"skeletal_mesh":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","skeletal_mesh"]} as const;
const S_epic_import_fbx = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"bindings":{"type":"array"},"import_settings":{"type":"object","properties":{"refPath":{}}},"fbx_file_path":{"type":"string"}},"required":["world","sequence","bindings","import_settings","fbx_file_path"]} as const;
const S_epic_import_fbx_to_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"import_file_path":{"type":"string"},"selected_controls":{"type":"array"}},"required":["sequence","control_rig_asset_path","import_file_path","selected_controls"]} as const;
const S_epic_import_file = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"skeleton":{"type":"object","properties":{"refPath":{}}},"import_materials":{"type":"boolean"},"import_textures":{"type":"boolean"},"import_animations":{"type":"boolean"},"create_physics_asset":{"type":"boolean"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_is_camera_cut_locked = {"properties":{}} as const;
const S_epic_is_curve_editor_open = {"properties":{}} as const;
const S_epic_is_curve_shown = {"properties":{"channel":{"type":"object"}},"required":["channel"]} as const;
const S_epic_is_fk_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_is_layered_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_is_node_expanded = {"properties":{"node":{"type":"object"}},"required":["node"]} as const;
const S_epic_is_playback_range_locked = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_is_playing = {"properties":{}} as const;
const S_epic_is_sequence_locked = {"properties":{}} as const;
const S_epic_is_track_filter_active = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_key_controls = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_names":{"type":"array"}},"required":["section","control_names"]} as const;
const S_epic_key_controls_at_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_names":{"type":"array"},"frames":{"type":"array"}},"required":["section","control_names","frames"]} as const;
const S_epic_link_anim_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"anim_sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object"}},"required":["sequence","anim_sequence","binding"]} as const;
const S_epic_list_graphs = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_list_nodes = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph"]} as const;
const S_epic_list_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","node"]} as const;
const S_epic_list_variables = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_load_anim_into_rig = {"properties":{"cr_section":{"type":"object","properties":{"refPath":{}}},"anim_sequence_path":{"type":"string"},"start_frame":{"type":"integer"},"reset_controls":{"type":"boolean"},"key_reduce":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["cr_section","anim_sequence_path"]} as const;
const S_epic_merge_anim_layers = {"properties":{"indices":{"type":"array"}},"required":["indices"]} as const;
const S_epic_mirror_selected_controls = {"properties":{}} as const;
const S_epic_move_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"old_frame":{"type":"integer"},"new_frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","old_frame","new_frame"]} as const;
const S_epic_open_curve_editor = {"properties":{}} as const;
const S_epic_open_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_paste_bindings = {"properties":{"paste_token":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}},"parent_folder":{"type":"object","properties":{"refPath":{}}}},"required":["paste_token","sequence"]} as const;
const S_epic_paste_folders = {"properties":{"paste_token":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}},"parent_folder":{"type":"object","properties":{"refPath":{}}}},"required":["paste_token","sequence"]} as const;
const S_epic_paste_sections = {"properties":{"paste_token":{"type":"string"},"target_tracks":{"type":"array"},"paste_frame":{"type":"integer"}},"required":["paste_token","target_tracks"]} as const;
const S_epic_paste_tracks = {"properties":{"paste_token":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}},"target_bindings":{"type":"array"},"parent_folder":{"type":"object","properties":{"refPath":{}}}},"required":["paste_token","sequence","target_bindings"]} as const;
const S_epic_pause = {"properties":{}} as const;
const S_epic_play = {"properties":{}} as const;
const S_epic_play_to = {"properties":{"frame":{"type":"integer"}},"required":["frame"]} as const;
const S_epic_rebind_component = {"properties":{"component_bindings":{"type":"array"},"component_name":{"type":"string"}},"required":["component_bindings","component_name"]} as const;
const S_epic_refresh_sequence = {"properties":{}} as const;
const S_epic_remove_actors_from_binding = {"properties":{"actors":{"type":"array"},"binding":{"type":"object"}},"required":["actors","binding"]} as const;
const S_epic_remove_all_bindings = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_remove_binding = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_remove_binding_tag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tag_name":{"type":"string"}},"required":["sequence","tag_name"]} as const;
const S_epic_remove_invalid_bindings = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_remove_key_at_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"}},"required":["section","channel_name","frame"]} as const;
const S_epic_remove_root_folder = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"folder":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","folder"]} as const;
const S_epic_remove_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}}},"required":["track","section"]} as const;
const S_epic_remove_socket = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"}},"required":["mesh","socket_name"]} as const;
const S_epic_remove_track = {"properties":{"binding":{"type":"object"},"track":{"type":"object","properties":{"refPath":{}}}},"required":["binding","track"]} as const;
const S_epic_remove_track_from_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"track":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","track"]} as const;
const S_epic_remove_variable = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig","name"]} as const;
const S_epic_rename_socket = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["mesh","old_name","new_name"]} as const;
const S_epic_reorder_anim_layers = {"properties":{"old_index":{"type":"integer"},"new_index":{"type":"integer"}},"required":["old_index","new_index"]} as const;
const S_epic_replace_binding_with_actors = {"properties":{"actors":{"type":"array"},"binding":{"type":"object"}},"required":["actors","binding"]} as const;
const S_epic_save_default_spawnable_state = {"properties":{"binding":{"type":"object"}},"required":["binding"]} as const;
const S_epic_select_bindings = {"properties":{"bindings":{"type":"array"}},"required":["bindings"]} as const;
const S_epic_select_channels = {"properties":{"channels":{"type":"array"}},"required":["channels"]} as const;
const S_epic_select_control = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"selected":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name"]} as const;
const S_epic_select_folders = {"properties":{"folders":{"type":"array"}},"required":["folders"]} as const;
const S_epic_select_mirrored_controls = {"properties":{}} as const;
const S_epic_select_sections = {"properties":{"sections":{"type":"array"}},"required":["sections"]} as const;
const S_epic_select_tracks = {"properties":{"tracks":{"type":"array"}},"required":["tracks"]} as const;
const S_epic_set_anim_mode_gizmo_scale = {"properties":{"scale":{"type":"number"}},"required":["scale"]} as const;
const S_epic_set_anim_mode_hide_manips = {"properties":{"hide":{"type":"boolean"}},"required":["hide"]} as const;
const S_epic_set_anim_mode_hierarchy = {"properties":{"enabled":{"type":"boolean"}},"required":["enabled"]} as const;
const S_epic_set_anim_mode_local_spaces = {"properties":{"enabled":{"type":"boolean"}},"required":["enabled"]} as const;
const S_epic_set_anim_mode_nulls = {"properties":{"enabled":{"type":"boolean"}},"required":["enabled"]} as const;
const S_epic_set_anim_mode_only_rig_sel = {"properties":{"only_rig":{"type":"boolean"}},"required":["only_rig"]} as const;
const S_epic_set_binding_name = {"properties":{"binding":{"type":"object"},"name":{"type":"string"}},"required":["binding","name"]} as const;
const S_epic_set_bool = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]} as const;
const S_epic_set_byte_track_enum = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"enum_class_path":{"type":"string"}},"required":["track","enum_class_path"]} as const;
const S_epic_set_camera_cut_binding = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"camera_binding_id":{"type":"string"}},"required":["section","camera_binding_id"]} as const;
const S_epic_set_camera_lock = {"properties":{"lock":{"type":"boolean"}},"required":["lock"]} as const;
const S_epic_set_clock_source = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"clock_source":{"type":"string"}},"required":["sequence","clock_source"]} as const;
const S_epic_set_controls_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_names":{"type":"array"},"visible":{"type":"boolean"}},"required":["section","control_names","visible"]} as const;
const S_epic_set_default_value = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"value":{"type":"number"}},"required":["section","channel_name","value"]} as const;
const S_epic_set_display_rate = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"numerator":{"type":"integer"},"denominator":{"type":"integer"}},"required":["sequence","numerator"]} as const;
const S_epic_set_euler_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number"},"location_y":{"type":"number"},"location_z":{"type":"number"},"rotation_pitch":{"type":"number"},"rotation_yaw":{"type":"number"},"rotation_roll":{"type":"number"},"scale_x":{"type":"number"},"scale_y":{"type":"number"},"scale_z":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_evaluation_type = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"eval_type":{"type":"string"}},"required":["sequence","eval_type"]} as const;
const S_epic_set_float = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]} as const;
const S_epic_set_global_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object"},"transform":{"type":"object"},"initial":{"type":"boolean"}},"required":["control_rig","item","transform"]} as const;
const S_epic_set_int = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]} as const;
const S_epic_set_layered_mode = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"is_layered":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","is_layered"]} as const;
const S_epic_set_local_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object"},"transform":{"type":"object"},"initial":{"type":"boolean"}},"required":["control_rig","item","transform"]} as const;
const S_epic_set_loop_mode = {"properties":{"loop":{"type":"boolean"}},"required":["loop"]} as const;
const S_epic_set_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"},"material":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","slot_name","material"]} as const;
const S_epic_set_node_deactivated = {"properties":{"nodes":{"type":"array"},"deactivated":{"type":"boolean"}},"required":["nodes","deactivated"]} as const;
const S_epic_set_node_expanded = {"properties":{"nodes":{"type":"array"},"expanded":{"type":"boolean"}},"required":["nodes","expanded"]} as const;
const S_epic_set_node_locked = {"properties":{"nodes":{"type":"array"},"locked":{"type":"boolean"}},"required":["nodes","locked"]} as const;
const S_epic_set_node_muted = {"properties":{"nodes":{"type":"array"},"muted":{"type":"boolean"}},"required":["nodes","muted"]} as const;
const S_epic_set_node_pinned = {"properties":{"nodes":{"type":"array"},"pinned":{"type":"boolean"}},"required":["nodes","pinned"]} as const;
const S_epic_set_node_position = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}},"position":{"type":"object"}},"required":["control_rig","graph","node","position"]} as const;
const S_epic_set_node_solo = {"properties":{"nodes":{"type":"array"},"soloed":{"type":"boolean"}},"required":["nodes","soloed"]} as const;
const S_epic_set_outliner_selection = {"properties":{"nodes":{"type":"array"}},"required":["nodes"]} as const;
const S_epic_set_pin_value = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"refPath":{}}},"value":{"type":"string"}},"required":["control_rig","graph","pin","value"]} as const;
const S_epic_set_playback_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["sequence","start_frame","end_frame"]} as const;
const S_epic_set_playback_range_locked = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"locked":{"type":"boolean"}},"required":["sequence","locked"]} as const;
const S_epic_set_playback_speed = {"properties":{"speed":{"type":"number"}},"required":["speed"]} as const;
const S_epic_set_playhead_frame = {"properties":{"frame":{"type":"integer"}},"required":["frame"]} as const;
const S_epic_set_position = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_priority_order = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"order":{"type":"integer"}},"required":["sequence","control_rig_asset_path","order"]} as const;
const S_epic_set_property_name_and_path = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"display_name":{"type":"string"},"property_path":{"type":"string"}},"required":["track","display_name","property_path"]} as const;
const S_epic_set_rotator = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_scale = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_section_animation = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"anim_sequence_path":{"type":"string"}},"required":["section","anim_sequence_path"]} as const;
const S_epic_set_section_blend_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"blend_type":{"type":"string"}},"required":["section","blend_type"]} as const;
const S_epic_set_section_completion_mode = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"completion_mode":{"type":"string"}},"required":["section","completion_mode"]} as const;
const S_epic_set_section_condition = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"condition_class":{"type":"string"}},"required":["section","condition_class"]} as const;
const S_epic_set_section_ease_in = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"duration":{"type":"integer"}},"required":["section","duration"]} as const;
const S_epic_set_section_ease_out = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"duration":{"type":"integer"}},"required":["section","duration"]} as const;
const S_epic_set_section_end_bounded = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"bounded":{"type":"boolean"}},"required":["section","bounded"]} as const;
const S_epic_set_section_post_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"integer"}},"required":["section","frames"]} as const;
const S_epic_set_section_pre_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"integer"}},"required":["section","frames"]} as const;
const S_epic_set_section_range = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["section","start_frame","end_frame"]} as const;
const S_epic_set_section_start_bounded = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"bounded":{"type":"boolean"}},"required":["section","bounded"]} as const;
const S_epic_set_selection_range = {"properties":{"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["start_frame","end_frame"]} as const;
const S_epic_set_sequence_locked = {"properties":{"lock":{"type":"boolean"}},"required":["lock"]} as const;
const S_epic_set_socket_transform = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"},"transform":{"type":"object"}},"required":["mesh","socket_name","transform"]} as const;
const S_epic_set_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"space_type":{"type":"string"},"frame":{"type":"integer"},"space_target":{"type":"string"}},"required":["sequence","control_rig_asset_path","control_name","space_type","frame","space_target"]} as const;
const S_epic_set_tick_resolution = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"numerator":{"type":"integer"},"denominator":{"type":"integer"}},"required":["sequence","numerator"]} as const;
const S_epic_set_track_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"condition_class":{"type":"string"}},"required":["track","condition_class"]} as const;
const S_epic_set_track_display_name = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["track","name"]} as const;
const S_epic_set_track_filter_active = {"properties":{"name":{"type":"string"},"active":{"type":"boolean"}},"required":["name","active"]} as const;
const S_epic_set_track_row_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"row_index":{"type":"integer"},"condition_class":{"type":"string"}},"required":["track","row_index","condition_class"]} as const;
const S_epic_set_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number"},"location_y":{"type":"number"},"location_z":{"type":"number"},"rotation_pitch":{"type":"number"},"rotation_yaw":{"type":"number"},"rotation_roll":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_vector2d = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_view_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"start_seconds":{"type":"number"},"end_seconds":{"type":"number"}},"required":["sequence","start_seconds","end_seconds"]} as const;
const S_epic_set_work_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"start_seconds":{"type":"number"},"end_seconds":{"type":"number"}},"required":["sequence","start_seconds","end_seconds"]} as const;
const S_epic_set_world_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number"},"location_y":{"type":"number"},"location_z":{"type":"number"},"rotation_pitch":{"type":"number"},"rotation_yaw":{"type":"number"},"rotation_roll":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_show_all_controls = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_show_curve = {"properties":{"channel":{"type":"object"},"show":{"type":"boolean"}},"required":["channel","show"]} as const;
const S_epic_snap_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_names":{"type":"array"},"target_actor_name":{"type":"string"},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"},"keep_offset":{"type":"boolean"},"snap_position":{"type":"boolean"},"snap_rotation":{"type":"boolean"},"snap_scale":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_names","target_actor_name","start_frame","end_frame"]} as const;
const S_epic_tag_binding = {"properties":{"binding":{"type":"object"},"tag_name":{"type":"string"}},"required":["binding","tag_name"]} as const;
const S_epic_tween_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"tween_value":{"type":"number"}},"required":["sequence","control_rig_asset_path","tween_value"]} as const;
const S_epic_untag_binding = {"properties":{"binding":{"type":"object"},"tag_name":{"type":"string"}},"required":["binding","tag_name"]} as const;
const S_epic_zero_transforms = {"properties":{"selection_only":{"type":"boolean"},"include_channels":{"type":"boolean"}}} as const;

/** 341 wrapped engine tools routed to the `animation` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_actors: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add actors from the level to the currently open sequence. Params: actors",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_actors", S_epic_add_actors, p),
  ),
  epic_add_actors_by_name: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add actors to the sequence by their names in the level. Finds actors by label in the current level and adds them to the currently open sequence as possessable bindings. Params: actor_names",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_actors_by_name", S_epic_add_actors_by_name, p),
  ),
  epic_add_actors_to_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add actors to an existing binding. Params: actors, binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_actors_to_binding", S_epic_add_actors_to_binding, p),
  ),
  epic_add_backward_solve_graph: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a backward solve graph with InverseExecution event. Params: control_rig, name?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_backward_solve_graph", S_epic_add_backward_solve_graph, p),
  ),
  epic_add_binding_to_folder: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a binding into a folder for organization. Params: folder, binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_binding_to_folder", S_epic_add_binding_to_folder, p),
  ),
  epic_add_bone: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a bone to the Control Rig hierarchy. Params: control_rig, name, parent?, transform?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_bone", S_epic_add_bone, p),
  ),
  epic_add_control: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a control to the hierarchy. Params: control_rig, name, parent?, settings?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_control", S_epic_add_control, p),
  ),
  epic_add_element: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a bone or null element to the Control Rig hierarchy. Params: control_rig, name, element_type, parent?, transform?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_element", S_epic_add_element, p),
  ),
  epic_add_event_graph: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a new graph with the specified event type. Params: control_rig, event_type, name?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_event_graph", S_epic_add_event_graph, p),
  ),
  epic_add_event_node: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add an event node to the graph. Params: control_rig, graph, event_type, position?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_event_node", S_epic_add_event_node, p),
  ),
  epic_add_event_repeater_section: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add an event repeater section to an event track. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_event_repeater_section", S_epic_add_event_repeater_section, p),
  ),
  epic_add_event_trigger_section: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add an event trigger section to an event track. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_event_trigger_section", S_epic_add_event_trigger_section, p),
  ),
  epic_add_graph: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a new empty graph in the Control Rig. Params: control_rig, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_graph", S_epic_add_graph, p),
  ),
  epic_add_interaction_graph: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create an interaction graph with InteractionExecution event. Params: control_rig, name?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_interaction_graph", S_epic_add_interaction_graph, p),
  ),
  epic_add_key_bool: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add a bool key to a channel on a section. Params: section, channel_name, frame, value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_bool", S_epic_add_key_bool, p),
  ),
  epic_add_key_float: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add a float key to a channel on a section. Params: section, channel_name, frame, value, interpolation",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_float", S_epic_add_key_float, p),
  ),
  epic_add_key_integer: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add an integer key to a channel on a section. Params: section, channel_name, frame, value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_integer", S_epic_add_key_integer, p),
  ),
  epic_add_key_string: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add a string key to a channel on a section. Params: section, channel_name, frame, value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_string", S_epic_add_key_string, p),
  ),
  epic_add_layer_from_selection: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Add an animation layer from the currently selected objects in Sequencer. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.add_layer_from_selection", S_epic_add_layer_from_selection, p),
  ),
  epic_add_marked_frame: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a marked frame (bookmark) to the sequence. Params: sequence, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_marked_frame", S_epic_add_marked_frame, p),
  ),
  epic_add_null: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a null (locator) to the Control Rig hierarchy. Params: control_rig, name, parent?, transform?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_null", S_epic_add_null, p),
  ),
  epic_add_root_folder: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a new root-level folder in the sequence. Params: sequence, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_root_folder", S_epic_add_root_folder, p),
  ),
  epic_add_section: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a new section to a track. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_section", S_epic_add_section, p),
  ),
  epic_add_socket: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Adds a named socket to a skeletal mesh attached to a bone. Sockets are named attachment points used to attach weapons, accessories, or effects at a consistent position relative to a bone. Params: mesh, socket_name, bone_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.add_socket", S_epic_add_socket, p),
  ),
  epic_add_spawnable_from_class: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a spawnable binding from an actor class. Params: sequence, actor_class_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_class", S_epic_add_spawnable_from_class, p),
  ),
  epic_add_spawnable_from_instance: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a spawnable binding from an existing object instance. Params: sequence, obj",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_instance", S_epic_add_spawnable_from_instance, p),
  ),
  epic_add_track_to_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a track of the given type to a binding. Params: binding, track_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_binding", S_epic_add_track_to_binding, p),
  ),
  epic_add_track_to_folder: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a track into a folder for organization. Params: folder, track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_folder", S_epic_add_track_to_folder, p),
  ),
  epic_add_track_to_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a sequence-level (master) track. Params: sequence, track_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_sequence", S_epic_add_track_to_sequence, p),
  ),
  epic_add_variable: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a member variable to the Control Rig. Params: control_rig, name, type_path, is_public?, is_read_only?, default_value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_variable", S_epic_add_variable, p),
  ),
  epic_add_variable_node: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a variable getter or setter node. The variable must already exist (created via add_variable first). Params: control_rig, graph, variable_name, is_getter?, position?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.add_variable_node", S_epic_add_variable_node, p),
  ),
  epic_assign_physics_asset: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Assigns a physics asset to a skeletal mesh. The physics asset must be compatible with the mesh's skeleton. Use this to swap physics assets or assign one to a mesh that has none. Params: mesh, physics_asset",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.assign_physics_asset", S_epic_assign_physics_asset, p),
  ),
  epic_bake_channel_keys: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Bake a channel's values over a frame range. Evaluates the channel curve at every frame in the range and returns the computed values. Useful for extracting animation data or verifying interpolation results. Params: section, channel_name, start_frame, end_frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.bake_channel_keys", S_epic_bake_channel_keys, p),
  ),
  epic_bake_space: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Bake Control Rig controls' space over a frame range. Params: sequence, control_rig_asset_path, control_names, start_frame, end_frame, reduce_keys?, tolerance?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_space", S_epic_bake_space, p),
  ),
  epic_bake_to_control_rig: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Bake existing animation on a binding into a Control Rig track. Params: sequence, binding, control_rig_asset_path, reduce_keys?, tolerance?, reset_controls?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_to_control_rig", S_epic_bake_to_control_rig, p),
  ),
  epic_bake_transform: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Bake transforms for the given bindings at every frame. Params: bindings",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.bake_transform", S_epic_bake_transform, p),
  ),
  epic_blend_values_on_selected: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Perform a blend operation on selected keys or controls. Params: sequence, operation, blend_value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.blend_values_on_selected", S_epic_blend_values_on_selected, p),
  ),
  epic_change_actor_template_class: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Set the actor class for a spawnable or replaceable template. Params: binding, actor_class",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.change_actor_template_class", S_epic_change_actor_template_class, p),
  ),
  epic_change_variable_type: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Change the type of an existing variable. Params: control_rig, name, new_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.change_variable_type", S_epic_change_variable_type, p),
  ),
  epic_clear_section_condition: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Remove the condition from a section. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_section_condition", S_epic_clear_section_condition, p),
  ),
  epic_clear_selection: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Clear the current Control Rig control selection. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.clear_selection", S_epic_clear_selection, p),
  ),
  epic_clear_track_condition: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Remove the condition from a track. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_track_condition", S_epic_clear_track_condition, p),
  ),
  epic_clear_track_row_condition: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Remove the condition from a specific track row. Params: track, row_index",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_track_row_condition", S_epic_clear_track_row_condition, p),
  ),
  epic_close_curve_editor: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Close the Sequencer Curve Editor panel. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.close_curve_editor", S_epic_close_curve_editor, p),
  ),
  epic_close_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Close the currently open level sequence editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.close_sequence", S_epic_close_sequence, p),
  ),
  epic_collapse_anim_layers: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Collapse all sections and layers on a Control Rig track into one section. Params: sequence, control_rig_asset_path, reduce_keys?, tolerance?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.collapse_anim_layers", S_epic_collapse_anim_layers, p),
  ),
  epic_connect_pins: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Connect two pins together. Params: control_rig, graph, source_pin, target_pin",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.connect_pins", S_epic_connect_pins, p),
  ),
  epic_convert_to_custom_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Convert a binding to a custom binding type. Params: binding, binding_type_class",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_custom_binding", S_epic_convert_to_custom_binding, p),
  ),
  epic_convert_to_possessable: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Convert a spawnable binding to a possessable. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_possessable", S_epic_convert_to_possessable, p),
  ),
  epic_convert_to_spawnable: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Convert a possessable binding to a spawnable. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_spawnable", S_epic_convert_to_spawnable, p),
  ),
  epic_copy_bindings: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more bindings to the Sequencer clipboard. Returns a paste token that can be passed to paste_bindings, or an empty string to consume from the clipboard. The token also lands in the editor clipboard for interactive use. Params: bindings",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.copy_bindings", S_epic_copy_bindings, p),
  ),
  epic_copy_folders: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more folders to the Sequencer clipboard. Params: folders",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.copy_folders", S_epic_copy_folders, p),
  ),
  epic_copy_sections: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more sections to the Sequencer clipboard. Params: sections",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.copy_sections", S_epic_copy_sections, p),
  ),
  epic_copy_tracks: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more tracks to the Sequencer clipboard. Params: tracks",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.copy_tracks", S_epic_copy_tracks, p),
  ),
  epic_create: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Creates a new Control Rig at the given location. Params: path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.create", S_epic_create, p),
  ),
  epic_create_camera: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a new cine camera actor in the sequence. Params: spawnable?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.create_camera", S_epic_create_camera, p),
  ),
  epic_create_level_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a new Level Sequence asset. If an asset already exists at the given path, it will be deleted first to avoid triggering a modal overwrite dialog. Params: package_path, asset_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.create_level_sequence", S_epic_create_level_sequence, p),
  ),
  epic_create_node: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a new RigUnit node in the graph. Params: control_rig, graph, node_type, position?, node_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.create_node", S_epic_create_node, p),
  ),
  epic_curve_editor_empty_selection: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Clear all key selection in the Curve Editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.curve_editor_empty_selection", S_epic_curve_editor_empty_selection, p),
  ),
  epic_curve_editor_select_keys: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Select keys by index in the Curve Editor. Params: channel, indices",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.curve_editor_select_keys", S_epic_curve_editor_select_keys, p),
  ),
  epic_delete_all_marked_frames: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Delete all marked frames from the sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.delete_all_marked_frames", S_epic_delete_all_marked_frames, p),
  ),
  epic_delete_anim_layer: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Delete an animation layer at the specified index. Params: index",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_anim_layer", S_epic_delete_anim_layer, p),
  ),
  epic_delete_marked_frame: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Delete a marked frame by index. Params: sequence, index",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.delete_marked_frame", S_epic_delete_marked_frame, p),
  ),
  epic_delete_node: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Delete a node from the graph. Params: control_rig, graph, node",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.delete_node", S_epic_delete_node, p),
  ),
  epic_delete_space: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Delete a space-switch key at a specific frame. Performs compensation to the new space automatically. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_space", S_epic_delete_space, p),
  ),
  epic_disconnect_pins: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Disconnect two pins. Params: control_rig, graph, source_pin, target_pin",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.disconnect_pins", S_epic_disconnect_pins, p),
  ),
  epic_duplicate_anim_layer: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Duplicate an animation layer at the specified index. Params: index",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.duplicate_anim_layer", S_epic_duplicate_anim_layer, p),
  ),
  epic_empty_selection: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Clear all selection in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.empty_selection", S_epic_empty_selection, p),
  ),
  epic_export_anim_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Export animation from a sequence binding to an AnimSequence asset. Params: world, sequence, anim_sequence, binding, create_link?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.import_export.SequencerImportExportTools", "animation_toolset.toolsets.import_export.SequencerImportExportTools.export_anim_sequence", S_epic_export_anim_sequence, p),
  ),
  epic_export_fbx: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Export a level sequence to FBX. Params: world, sequence, bindings, fbx_file_path, override_options?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.import_export.SequencerImportExportTools", "animation_toolset.toolsets.import_export.SequencerImportExportTools.export_fbx", S_epic_export_fbx, p),
  ),
  epic_export_fbx_from_rig: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Export an FBX file from a Control Rig section. Params: sequence, control_rig_asset_path, export_file_path, ascii?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.export_fbx_from_rig", S_epic_export_fbx_from_rig, p),
  ),
  epic_find_binding_by_name: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find a binding by its display name. Params: sequence, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.find_binding_by_name", S_epic_find_binding_by_name, p),
  ),
  epic_find_binding_by_tag: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find the first binding with the given tag in the sequence. Tags are authored via tag_binding() in this toolset, or via RMB -> Expose on a binding in the Sequencer editor. Params: sequence, tag_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.find_binding_by_tag", S_epic_find_binding_by_tag, p),
  ),
  epic_find_bindings_by_tag: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find all bindings with the given tag in the sequence. Params: sequence, tag_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.find_bindings_by_tag", S_epic_find_bindings_by_tag, p),
  ),
  epic_find_marked_frame_by_label: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find a marked frame by label. Params: sequence, label",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.find_marked_frame_by_label", S_epic_find_marked_frame_by_label, p),
  ),
  epic_find_or_create_track: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Add a Control Rig track to a binding using a Control Rig asset. This is the standard way to add a Control Rig to Sequencer. Uses ControlRigSequencerLibrary.find_or_create_control_rig_track. Params: sequence, binding, control_rig_asset_path, is_layered?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.find_or_create_track", S_epic_find_or_create_track, p),
  ),
  epic_find_tracks_by_type: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find all tracks of a specific type on a binding. Params: binding, track_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.find_tracks_by_type", S_epic_find_tracks_by_type, p),
  ),
  epic_fix_actor_references: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Attempt to auto-fix broken actor references in the current sequence. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.fix_actor_references", S_epic_fix_actor_references, p),
  ),
  epic_focus_parent_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Navigate up one level in the sub-sequence hierarchy. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.focus_parent_sequence", S_epic_focus_parent_sequence, p),
  ),
  epic_focus_sub_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Navigate into a sub-sequence via its sub-section. Use get_sections() on a sub-track to find the sub-section, then pass it here to focus the sub-sequence it references. Params: sub_section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.focus_sub_sequence", S_epic_focus_sub_sequence, p),
  ),
  epic_force_evaluate: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Force the Sequencer to evaluate and update the viewport. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.force_evaluate", S_epic_force_evaluate, p),
  ),
  epic_frame_selection: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Frame the viewport to the current Control Rig control selection. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.frame_selection", S_epic_frame_selection, p),
  ),
  epic_get_actor_transform_at_frame: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get an actor's world transform at a specific frame. Finds the actor by name in the current editor world. Params: sequence, actor_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_actor_transform_at_frame", S_epic_get_actor_transform_at_frame, p),
  ),
  epic_get_all_binding_tags: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get every tag name registered in the sequence. Uses the MovieSceneBindingTagExtensions C++ library. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_all_binding_tags", S_epic_get_all_binding_tags, p),
  ),
  epic_get_all_bones: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all bones in the Control Rig hierarchy. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_bones", S_epic_get_all_bones, p),
  ),
  epic_get_all_controls: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all controls in the Control Rig hierarchy. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_controls", S_epic_get_all_controls, p),
  ),
  epic_get_all_nulls: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all nulls (locators) in the Control Rig hierarchy. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_nulls", S_epic_get_all_nulls, p),
  ),
  epic_get_anim_layers: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get all animation layers from the active Sequencer. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_layers", S_epic_get_anim_layers, p),
  ),
  epic_get_anim_mode_gizmo_scale: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the editor's transform gizmo size. Reads UTransformGizmoEditorSettings::TransformGizmoSize. The CR-specific gizmo scale was removed in UE 5.8 in favor of this editor-wide setting, so this affects every transform gizmo (level, sequencer, CR, etc.). Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_gizmo_scale", S_epic_get_anim_mode_gizmo_scale, p),
  ),
  epic_get_anim_mode_hide_manips: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether Animation Mode hides all manipulators. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_hide_manips", S_epic_get_anim_mode_hide_manips, p),
  ),
  epic_get_anim_mode_hierarchy: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether the Animation Mode draws hierarchy lines/dots. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_hierarchy", S_epic_get_anim_mode_hierarchy, p),
  ),
  epic_get_anim_mode_local_spaces: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether multi-select transforms act in each control's own space. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_local_spaces", S_epic_get_anim_mode_local_spaces, p),
  ),
  epic_get_anim_mode_nulls: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether the Animation Mode draws nulls. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_nulls", S_epic_get_anim_mode_nulls, p),
  ),
  epic_get_anim_mode_only_rig_sel: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether Animation Mode restricts viewport selection to rig controls. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_only_rig_sel", S_epic_get_anim_mode_only_rig_sel, p),
  ),
  epic_get_backward_solve_graph: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the backward solve graph. The backward solve graph contains the InverseExecution event and runs during IK operations. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_backward_solve_graph", S_epic_get_backward_solve_graph, p),
  ),
  epic_get_binding_id: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the binding ID for a binding proxy. The binding ID can be used with get_bound_objects to resolve what actor or component the binding references at runtime. Params: sequence, binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_id", S_epic_get_binding_id, p),
  ),
  epic_get_binding_name: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the display name of a binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_name", S_epic_get_binding_name, p),
  ),
  epic_get_binding_tags: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the tags currently attached to a specific binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_tags", S_epic_get_binding_tags, p),
  ),
  epic_get_bindings: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all bindings in the sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_bindings", S_epic_get_bindings, p),
  ),
  epic_get_bone_children: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the direct children of a bone. Params: mesh, bone_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_children", S_epic_get_bone_children, p),
  ),
  epic_get_bone_names: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all bones in a skeletal mesh in hierarchy order. Bone names are used to target specific bones for socket attachment, physics constraints, and animation retargeting. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_names", S_epic_get_bone_names, p),
  ),
  epic_get_bone_parent: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the name of a bone's parent, or an empty string for the root bone. Params: mesh, bone_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_parent", S_epic_get_bone_parent, p),
  ),
  epic_get_bool: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a bool control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_bool", S_epic_get_bool, p),
  ),
  epic_get_bound_objects: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the objects currently resolved by a binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_bound_objects", S_epic_get_bound_objects, p),
  ),
  epic_get_bounds: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the local-space bounding volume of a skeletal mesh. The bounds represent the reference pose and do not account for animation. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bounds", S_epic_get_bounds, p),
  ),
  epic_get_channel_names: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get the names of all channels on a section. For example, a 3D Transform section has channels named 'Location.X', 'Location.Y', 'Location.Z', 'Rotation.X', etc. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_channel_names", S_epic_get_channel_names, p),
  ),
  epic_get_child_possessables: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get component bindings under an actor binding. Actor bindings can own child possessable bindings for their components (e.g. SkeletalMeshComponent, CameraComponent). Use this to find the component binding when you need to add tracks to a specific component rather than the actor. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_child_possessables", S_epic_get_child_possessables, p),
  ),
  epic_get_children: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get children of a hierarchy element. Params: control_rig, item, recursive?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_children", S_epic_get_children, p),
  ),
  epic_get_clock_source: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the clock source for the sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_clock_source", S_epic_get_clock_source, p),
  ),
  epic_get_connected_pins: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all pins connected to this pin. Params: control_rig, pin",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_connected_pins", S_epic_get_connected_pins, p),
  ),
  epic_get_control_rigs: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get all Control Rigs currently in the sequence. Returns proxy objects with track and rig references. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_control_rigs", S_epic_get_control_rigs, p),
  ),
  epic_get_controls_info: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get all controls on a Control Rig with their names and types. Returns a list of controls with name and type so the caller can find controls of a specific type (e.g. find a Float control to use with set_float). Possible types: Bool, Float, Integer, Vector2D, Position, Rotator, Scale, Transform, TransformNoScale, EulerTransform. Params: sequence, control_rig_asset_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_info", S_epic_get_controls_info, p),
  ),
  epic_get_controls_mask: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Check if a control is visible (unmasked) on a section. Params: section, control_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_mask", S_epic_get_controls_mask, p),
  ),
  epic_get_current_sequence: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the root level sequence currently open in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_current_sequence", S_epic_get_current_sequence, p),
  ),
  epic_get_curve_editor_selected_keys: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get selected key indices for a channel in the Curve Editor. Params: channel",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_curve_editor_selected_keys", S_epic_get_curve_editor_selected_keys, p),
  ),
  epic_get_custom_binding_objects: bp(
    "read",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Get the custom binding instances for a binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_binding_objects", S_epic_get_custom_binding_objects, p),
  ),
  epic_get_custom_binding_type: bp(
    "read",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Get the custom binding class for a binding. Returns the class path of the custom binding type, or an empty string for standard possessable bindings. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_binding_type", S_epic_get_custom_binding_type, p),
  ),
  epic_get_custom_bindings_of_type: bp(
    "read",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Find all bindings of a given custom type in the current sequence. Params: binding_type_class",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_bindings_of_type", S_epic_get_custom_bindings_of_type, p),
  ),
  epic_get_deactivated_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently deactivated outliner nodes. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_deactivated_nodes", S_epic_get_deactivated_nodes, p),
  ),
  epic_get_default_value: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get the default value of a float channel. Params: section, channel_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_default_value", S_epic_get_default_value, p),
  ),
  epic_get_display_rate: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the display frame rate of a sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_display_rate", S_epic_get_display_rate, p),
  ),
  epic_get_elements: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get hierarchy elements of the specified type. Params: control_rig, element_type?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_elements", S_epic_get_elements, p),
  ),
  epic_get_euler_transform: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get an EulerTransform control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_euler_transform", S_epic_get_euler_transform, p),
  ),
  epic_get_evaluation_type: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the evaluation type of a sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_evaluation_type", S_epic_get_evaluation_type, p),
  ),
  epic_get_event_graph: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get a graph containing the specified event type. Params: control_rig, event_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_event_graph", S_epic_get_event_graph, p),
  ),
  epic_get_float: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a float control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_float", S_epic_get_float, p),
  ),
  epic_get_focused_sequence: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently focused level sequence in the hierarchy. When navigated into a sub-sequence, this returns the sub-sequence rather than the root sequence. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_focused_sequence", S_epic_get_focused_sequence, p),
  ),
  epic_get_folder_contents: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the tracks and bindings inside a folder. Params: folder",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_folder_contents", S_epic_get_folder_contents, p),
  ),
  epic_get_forward_solve_graph: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the forward solve graph (main execution graph). This graph contains the BeginExecution event and runs during normal animation evaluation. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_forward_solve_graph", S_epic_get_forward_solve_graph, p),
  ),
  epic_get_global_transform: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get global transform of a hierarchy element. Params: control_rig, item, initial?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_global_transform", S_epic_get_global_transform, p),
  ),
  epic_get_graph: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get a specific graph from the Control Rig by name. Params: control_rig, graph_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_graph", S_epic_get_graph, p),
  ),
  epic_get_int: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get an integer control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_int", S_epic_get_int, p),
  ),
  epic_get_interaction_graph: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the interaction graph. The interaction graph contains the InteractionExecution event and runs during user interaction with controls. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_interaction_graph", S_epic_get_interaction_graph, p),
  ),
  epic_get_keys: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get all keys on a channel, returned as a JSON array. Each key entry includes its frame number and value. Params: section, channel_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys", S_epic_get_keys, p),
  ),
  epic_get_keys_by_index: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get specific keys on a channel by their indices, returned as JSON. Useful for resolving the keys behind a Curve Editor selection, which provides indices rather than key objects. Params: section, channel_name, indices",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys_by_index", S_epic_get_keys_by_index, p),
  ),
  epic_get_linked_anim_sequences: bp(
    "read",
    "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Get content paths of all AnimSequences linked to a LevelSequence. Linked AnimSequences auto-update when the LevelSequence changes. They are created via export_anim_sequence with create_link=True. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.import_export.SequencerImportExportTools", "animation_toolset.toolsets.import_export.SequencerImportExportTools.get_linked_anim_sequences", S_epic_get_linked_anim_sequences, p),
  ),
  epic_get_linked_level_sequence: bp(
    "read",
    "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Get the content path of the LevelSequence linked to an AnimSequence. Params: anim_sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.import_export.SequencerImportExportTools", "animation_toolset.toolsets.import_export.SequencerImportExportTools.get_linked_level_sequence", S_epic_get_linked_level_sequence, p),
  ),
  epic_get_local_transform: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get local transform of a hierarchy element. Params: control_rig, item, initial?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_local_transform", S_epic_get_local_transform, p),
  ),
  epic_get_locked_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently locked outliner nodes. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_locked_nodes", S_epic_get_locked_nodes, p),
  ),
  epic_get_lod_count: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the number of LODs in a skeletal mesh asset. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_lod_count", S_epic_get_lod_count, p),
  ),
  epic_get_loop_mode: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current loop playback mode. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_loop_mode", S_epic_get_loop_mode, p),
  ),
  epic_get_marked_frames: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all marked frames (bookmarks) in the sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_marked_frames", S_epic_get_marked_frames, p),
  ),
  epic_get_material: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the material assigned to a named slot on a skeletal mesh. Params: mesh, slot_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_material", S_epic_get_material, p),
  ),
  epic_get_material_slots: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all material slots in a skeletal mesh. Material slot names are used when assigning materials to specific parts of the mesh. Use these names with get_material and set_material. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_material_slots", S_epic_get_material_slots, p),
  ),
  epic_get_morph_target_names: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all morph targets on a skeletal mesh. Morph targets (blend shapes) are per-vertex offsets used to deform the mesh, commonly used for facial expressions and cloth simulation. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_morph_target_names", S_epic_get_morph_target_names, p),
  ),
  epic_get_muted_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently muted outliner nodes. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_muted_nodes", S_epic_get_muted_nodes, p),
  ),
  epic_get_node_label: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the display label of an outliner node. Params: node",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_node_label", S_epic_get_node_label, p),
  ),
  epic_get_node_position: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the position of a node in the graph editor. Params: control_rig, node",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_node_position", S_epic_get_node_position, p),
  ),
  epic_get_outliner_children: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get child nodes of an outliner node. Params: node, type_filter?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_children", S_epic_get_outliner_children, p),
  ),
  epic_get_outliner_selection: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently selected nodes in the outliner. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_selection", S_epic_get_outliner_selection, p),
  ),
  epic_get_outliner_tree: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get a full snapshot of the Sequencer outliner tree. Builds a recursive tree structure from the outliner root nodes. Useful for testing and UI verification. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_tree", S_epic_get_outliner_tree, p),
  ),
  epic_get_parent: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the parent of a hierarchy element. Params: control_rig, item",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_parent", S_epic_get_parent, p),
  ),
  epic_get_physics_asset: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the physics asset assigned to a skeletal mesh. The physics asset defines the collision bodies and constraints used for ragdoll simulation and per-bone physics. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_physics_asset", S_epic_get_physics_asset, p),
  ),
  epic_get_pin_value: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the default value of a pin. Params: control_rig, pin",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_pin_value", S_epic_get_pin_value, p),
  ),
  epic_get_pinned_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently pinned outliner nodes. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_pinned_nodes", S_epic_get_pinned_nodes, p),
  ),
  epic_get_playback_range: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the playback start and end frames of a sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_playback_range", S_epic_get_playback_range, p),
  ),
  epic_get_playback_speed: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current playback speed multiplier. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_playback_speed", S_epic_get_playback_speed, p),
  ),
  epic_get_playhead_frame: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current playhead position in display rate frames. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_playhead_frame", S_epic_get_playhead_frame, p),
  ),
  epic_get_position: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a position control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_position", S_epic_get_position, p),
  ),
  epic_get_priority_order: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the evaluation priority order of a Control Rig track. Params: sequence, control_rig_asset_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_priority_order", S_epic_get_priority_order, p),
  ),
  epic_get_root_folders: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all root-level folders in the sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_root_folders", S_epic_get_root_folders, p),
  ),
  epic_get_rotator: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a rotator control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_rotator", S_epic_get_rotator, p),
  ),
  epic_get_scale: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a scale control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_scale", S_epic_get_scale, p),
  ),
  epic_get_section_blend_type: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the blend type of a section. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_blend_type", S_epic_get_section_blend_type, p),
  ),
  epic_get_section_completion_mode: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the completion mode of a section. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_completion_mode", S_epic_get_section_completion_mode, p),
  ),
  epic_get_section_condition: bp(
    "read",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Get the condition on a section. Returns the class path of the condition, or an empty string if no condition is set. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.get_section_condition", S_epic_get_section_condition, p),
  ),
  epic_get_section_count: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the number of sections in a specific LOD of a skeletal mesh. Sections correspond to individual material slots rendered by a single draw call. A mesh may have more sections than material slots if multiple sections share the same material. Params: mesh, lod_index?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_section_count", S_epic_get_section_count, p),
  ),
  epic_get_section_ease_in: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the effective ease-in duration of a section in frames. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_ease_in", S_epic_get_section_ease_in, p),
  ),
  epic_get_section_ease_out: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the effective ease-out duration of a section in frames. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_ease_out", S_epic_get_section_ease_out, p),
  ),
  epic_get_section_post_roll_frames: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the number of post-roll frames configured on a section. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_post_roll_frames", S_epic_get_section_post_roll_frames, p),
  ),
  epic_get_section_pre_roll_frames: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the number of pre-roll frames configured on a section. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_pre_roll_frames", S_epic_get_section_pre_roll_frames, p),
  ),
  epic_get_section_properties: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all common properties of a section in a single call. Useful for testing and verification. Returns range, easing, blend type, and completion mode. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_properties", S_epic_get_section_properties, p),
  ),
  epic_get_section_range: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the frame range of a section. Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_range", S_epic_get_section_range, p),
  ),
  epic_get_section_to_key: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the active section that receives new keys on a track. When keying properties, Sequencer writes to a specific section. This returns that section, which is usually the first section or the one the user has designated. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_section_to_key", S_epic_get_section_to_key, p),
  ),
  epic_get_sections: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all sections on a track. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_sections", S_epic_get_sections, p),
  ),
  epic_get_sections_for_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get sections associated with the given outliner nodes. Params: nodes",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_sections_for_nodes", S_epic_get_sections_for_nodes, p),
  ),
  epic_get_selected_bindings: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected bindings in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_bindings", S_epic_get_selected_bindings, p),
  ),
  epic_get_selected_channels: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get the currently selected channels in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_selected_channels", S_epic_get_selected_channels, p),
  ),
  epic_get_selected_controls: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the currently selected controls on a Control Rig. Params: sequence, control_rig_asset_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_selected_controls", S_epic_get_selected_controls, p),
  ),
  epic_get_selected_folders: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected folders in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_folders", S_epic_get_selected_folders, p),
  ),
  epic_get_selected_key_channels: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get channels that have selected keys in the Curve Editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_selected_key_channels", S_epic_get_selected_key_channels, p),
  ),
  epic_get_selected_sections: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected sections in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_sections", S_epic_get_selected_sections, p),
  ),
  epic_get_selected_tracks: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected tracks in the Sequencer editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_tracks", S_epic_get_selected_tracks, p),
  ),
  epic_get_selection_range: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the selection range (green bar) start and end frames. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_selection_range", S_epic_get_selection_range, p),
  ),
  epic_get_sequence_lock_state: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check whether the current level sequence is locked. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_sequence_lock_state", S_epic_get_sequence_lock_state, p),
  ),
  epic_get_skeleton: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the skeleton asset associated with a skeletal mesh. The skeleton defines the bone hierarchy shared across all meshes and animations that use it. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_skeleton", S_epic_get_skeleton, p),
  ),
  epic_get_socket_bone: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the name of the bone that a socket is attached to. Params: mesh, socket_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_bone", S_epic_get_socket_bone, p),
  ),
  epic_get_socket_names: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all sockets on a skeletal mesh. Sockets are named attachment points parented to bones. They are used to attach weapons, accessories, or effects at a consistent location. Params: mesh",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_names", S_epic_get_socket_names, p),
  ),
  epic_get_socket_transform: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the local transform of a socket relative to its parent bone. Params: mesh, socket_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_transform", S_epic_get_socket_transform, p),
  ),
  epic_get_soloed_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently soloed outliner nodes. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_soloed_nodes", S_epic_get_soloed_nodes, p),
  ),
  epic_get_sub_sequence_hierarchy: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current sub-sequence hierarchy path. Returns a list of sub-sections from the root down to the currently focused sub-sequence. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_sub_sequence_hierarchy", S_epic_get_sub_sequence_hierarchy, p),
  ),
  epic_get_tick_resolution: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the internal tick resolution of a sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_tick_resolution", S_epic_get_tick_resolution, p),
  ),
  epic_get_track_condition: bp(
    "read",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Get the track-level condition. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.get_track_condition", S_epic_get_track_condition, p),
  ),
  epic_get_track_display_name: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the display name of a track. Params: track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_track_display_name", S_epic_get_track_display_name, p),
  ),
  epic_get_track_filter_names: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all available track filter names. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_track_filter_names", S_epic_get_track_filter_names, p),
  ),
  epic_get_track_row_condition: bp(
    "read",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Get the condition on a specific track row. Params: track, row_index",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.get_track_row_condition", S_epic_get_track_row_condition, p),
  ),
  epic_get_tracks_on_binding: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all tracks on a binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_binding", S_epic_get_tracks_on_binding, p),
  ),
  epic_get_tracks_on_sequence: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all sequence-level (master) tracks. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_sequence", S_epic_get_tracks_on_sequence, p),
  ),
  epic_get_transform: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the transform value of a Control Rig control at a frame. Automatically detects whether the control is a Transform or EulerTransform type and uses the appropriate API. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_transform", S_epic_get_transform, p),
  ),
  epic_get_variable: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get a specific variable by name. Params: control_rig, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.get_variable", S_epic_get_variable, p),
  ),
  epic_get_vector2d: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a Vector2D control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_vector2d", S_epic_get_vector2d, p),
  ),
  epic_get_vertex_count: bp(
    "read",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the number of vertices in a specific LOD of a skeletal mesh. Params: mesh, lod_index?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_vertex_count", S_epic_get_vertex_count, p),
  ),
  epic_get_view_range: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the visible time range in the Sequencer timeline. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_view_range", S_epic_get_view_range, p),
  ),
  epic_get_work_range: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the work range of the sequence. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.get_work_range", S_epic_get_work_range, p),
  ),
  epic_get_world_transform: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a control's world-space transform at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_world_transform", S_epic_get_world_transform, p),
  ),
  epic_has_section_end_frame: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if a section has a bounded end frame (vs infinite). Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.has_section_end_frame", S_epic_has_section_end_frame, p),
  ),
  epic_has_section_start_frame: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if a section has a bounded start frame (vs infinite). Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.has_section_start_frame", S_epic_has_section_start_frame, p),
  ),
  epic_hide_all_controls: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Hide all controls on a Control Rig section (mask everything). Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.hide_all_controls", S_epic_hide_all_controls, p),
  ),
  epic_import_bones_from_asset: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Import bones from the given skeletal mesh to the Control Rig hierarchy. Params: control_rig, skeletal_mesh",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.import_bones_from_asset", S_epic_import_bones_from_asset, p),
  ),
  epic_import_fbx: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Import FBX data into a level sequence. Params: world, sequence, bindings, import_settings, fbx_file_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.import_export.SequencerImportExportTools", "animation_toolset.toolsets.import_export.SequencerImportExportTools.import_fbx", S_epic_import_fbx, p),
  ),
  epic_import_fbx_to_rig: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Import an FBX file onto a Control Rig track. Params: sequence, control_rig_asset_path, import_file_path, selected_controls",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.import_fbx_to_rig", S_epic_import_fbx_to_rig, p),
  ),
  epic_import_file: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Imports a mesh file from disk as a SkeletalMesh asset. The source file must contain a skeleton hierarchy and skinned mesh data. Params: folder_path, asset_name, source_file, skeleton?, import_materials?, import_textures?, import_animations?, create_physics_asset?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.import_file", S_epic_import_file, p),
  ),
  epic_is_camera_cut_locked: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if the camera cut is locked to the viewport. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.is_camera_cut_locked", S_epic_is_camera_cut_locked, p),
  ),
  epic_is_curve_editor_open: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Check whether the Curve Editor panel is currently open. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.is_curve_editor_open", S_epic_is_curve_editor_open, p),
  ),
  epic_is_curve_shown: bp(
    "read",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Check if a curve is visible in the Curve Editor. Params: channel",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.is_curve_shown", S_epic_is_curve_shown, p),
  ),
  epic_is_fk_control_rig: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Check if a Control Rig is an FK Control Rig. Params: sequence, control_rig_asset_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.is_fk_control_rig", S_epic_is_fk_control_rig, p),
  ),
  epic_is_layered_control_rig: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Check if a Control Rig in the sequence is in layered mode. Params: sequence, control_rig_asset_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.is_layered_control_rig", S_epic_is_layered_control_rig, p),
  ),
  epic_is_node_expanded: bp(
    "read",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Check whether an outliner node is expanded. Params: node",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.is_node_expanded", S_epic_is_node_expanded, p),
  ),
  epic_is_playback_range_locked: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if the playback range is locked. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.is_playback_range_locked", S_epic_is_playback_range_locked, p),
  ),
  epic_is_playing: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check whether the sequence is currently playing. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.is_playing", S_epic_is_playing, p),
  ),
  epic_is_sequence_locked: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if the current sequence and its descendants are locked. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.is_sequence_locked", S_epic_is_sequence_locked, p),
  ),
  epic_is_track_filter_active: bp(
    "read",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check whether a track filter is currently active. Params: name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.is_track_filter_active", S_epic_is_track_filter_active, p),
  ),
  epic_key_controls: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Key the specified controls on the section at the current Sequencer time. Params: section, control_names",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls", S_epic_key_controls, p),
  ),
  epic_key_controls_at_frames: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Key the specified controls at specific frame numbers. Params: section, control_names, frames",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls_at_frames", S_epic_key_controls_at_frames, p),
  ),
  epic_link_anim_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Link an AnimSequence asset to a level sequence binding. When the sequence is modified, the linked AnimSequence can be automatically updated. Params: sequence, anim_sequence, binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.import_export.SequencerImportExportTools", "animation_toolset.toolsets.import_export.SequencerImportExportTools.link_anim_sequence", S_epic_link_anim_sequence, p),
  ),
  epic_list_graphs: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all graphs in the Control Rig. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.list_graphs", S_epic_list_graphs, p),
  ),
  epic_list_nodes: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all nodes in a graph. Params: control_rig, graph",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.list_nodes", S_epic_list_nodes, p),
  ),
  epic_list_pins: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all pins on a node. Params: control_rig, node",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.list_pins", S_epic_list_pins, p),
  ),
  epic_list_variables: bp(
    "read",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all member variables in the Control Rig. Params: control_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.list_variables", S_epic_list_variables, p),
  ),
  epic_load_anim_into_rig: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Load an animation sequence into a Control Rig section. Finds the skeletal mesh component from the binding associated with the section's track. Params: cr_section, anim_sequence_path, start_frame?, reset_controls?, key_reduce?, tolerance?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.load_anim_into_rig", S_epic_load_anim_into_rig, p),
  ),
  epic_merge_anim_layers: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Merge specified animation layers into one. Merges onto the layer with the lowest index. Params: indices",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.merge_anim_layers", S_epic_merge_anim_layers, p),
  ),
  epic_mirror_selected_controls: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Apply a mirrored pose to the currently selected controls. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.mirror_selected_controls", S_epic_mirror_selected_controls, p),
  ),
  epic_move_space: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Move a space-switch key from one frame to another. Params: sequence, control_rig_asset_path, control_name, old_frame, new_frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.move_space", S_epic_move_space, p),
  ),
  epic_open_curve_editor: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Open the Sequencer Curve Editor panel. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.open_curve_editor", S_epic_open_curve_editor, p),
  ),
  epic_open_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Open a level sequence asset in the Sequencer editor. Params: sequence",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.open_sequence", S_epic_open_sequence, p),
  ),
  epic_paste_bindings: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste bindings from the clipboard (or a token returned by copy_bindings). Params: paste_token, sequence, parent_folder?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.paste_bindings", S_epic_paste_bindings, p),
  ),
  epic_paste_folders: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste folders from the clipboard into the sequence. Params: paste_token, sequence, parent_folder?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.paste_folders", S_epic_paste_folders, p),
  ),
  epic_paste_sections: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste sections from the clipboard onto the given tracks. Params: paste_token, target_tracks, paste_frame?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.paste_sections", S_epic_paste_sections, p),
  ),
  epic_paste_tracks: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste tracks from the clipboard onto the given bindings. Params: paste_token, sequence, target_bindings, parent_folder?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.paste_tracks", S_epic_paste_tracks, p),
  ),
  epic_pause: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Pause playback of the current sequence. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.pause", S_epic_pause, p),
  ),
  epic_play: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Start playback of the current sequence. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.play", S_epic_play, p),
  ),
  epic_play_to: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Play from the current position to a specific frame, then stop. Params: frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.play_to", S_epic_play_to, p),
  ),
  epic_rebind_component: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Rebind component bindings to a named component. Params: component_bindings, component_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.rebind_component", S_epic_rebind_component, p),
  ),
  epic_refresh_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Force refresh the Sequencer editor UI on the next tick. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.refresh_sequence", S_epic_refresh_sequence, p),
  ),
  epic_remove_actors_from_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove specific actors from a binding. Params: actors, binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_actors_from_binding", S_epic_remove_actors_from_binding, p),
  ),
  epic_remove_all_bindings: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove all bound actors from a binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_all_bindings", S_epic_remove_all_bindings, p),
  ),
  epic_remove_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a binding from the sequence. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_binding", S_epic_remove_binding, p),
  ),
  epic_remove_binding_tag: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a tag from the sequence entirely. Clears the tag from every binding that had it and unregisters the tag name from the sequence. Params: sequence, tag_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_binding_tag", S_epic_remove_binding_tag, p),
  ),
  epic_remove_invalid_bindings: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove missing or broken actor references from a binding. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_invalid_bindings", S_epic_remove_invalid_bindings, p),
  ),
  epic_remove_key_at_frame: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Remove a key at a specific frame from a channel. Params: section, channel_name, frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.remove_key_at_frame", S_epic_remove_key_at_frame, p),
  ),
  epic_remove_root_folder: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a root-level folder from the sequence. Params: sequence, folder",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_root_folder", S_epic_remove_root_folder, p),
  ),
  epic_remove_section: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a section from a track. Params: track, section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_section", S_epic_remove_section, p),
  ),
  epic_remove_socket: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Removes a named socket from a skeletal mesh. Params: mesh, socket_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.remove_socket", S_epic_remove_socket, p),
  ),
  epic_remove_track: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a track from a binding. Params: binding, track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_track", S_epic_remove_track, p),
  ),
  epic_remove_track_from_sequence: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a sequence-level (master) track. Params: sequence, track",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.remove_track_from_sequence", S_epic_remove_track_from_sequence, p),
  ),
  epic_remove_variable: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Remove a member variable from the Control Rig. Params: control_rig, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.remove_variable", S_epic_remove_variable, p),
  ),
  epic_rename_socket: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Renames a socket on a skeletal mesh. Params: mesh, old_name, new_name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.rename_socket", S_epic_rename_socket, p),
  ),
  epic_reorder_anim_layers: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Move an animation layer from one index to another. Cannot move the base layer (index 0). Params: old_index, new_index",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.reorder_anim_layers", S_epic_reorder_anim_layers, p),
  ),
  epic_replace_binding_with_actors: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Replace all bound actors on a binding with new ones. Params: actors, binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.replace_binding_with_actors", S_epic_replace_binding_with_actors, p),
  ),
  epic_save_default_spawnable_state: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Save the current state of a spawnable as its default. Params: binding",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.save_default_spawnable_state", S_epic_save_default_spawnable_state, p),
  ),
  epic_select_bindings: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the binding selection in the Sequencer editor. Params: bindings",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.select_bindings", S_epic_select_bindings, p),
  ),
  epic_select_channels: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Set the channel selection in the Sequencer editor. Params: channels",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.select_channels", S_epic_select_channels, p),
  ),
  epic_select_control: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Select or deselect a control on a Control Rig. Params: sequence, control_rig_asset_path, control_name, selected?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.select_control", S_epic_select_control, p),
  ),
  epic_select_folders: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the folder selection in the Sequencer editor. Params: folders",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.select_folders", S_epic_select_folders, p),
  ),
  epic_select_mirrored_controls: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Select the mirrored counterparts of the currently selected controls. Replaces the current selection with mirrored controls. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.select_mirrored_controls", S_epic_select_mirrored_controls, p),
  ),
  epic_select_sections: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the section selection in the Sequencer editor. Params: sections",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.select_sections", S_epic_select_sections, p),
  ),
  epic_select_tracks: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the track selection in the Sequencer editor. Params: tracks",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.select_tracks", S_epic_select_tracks, p),
  ),
  epic_set_anim_mode_gizmo_scale: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the editor's transform gizmo size. Writes UTransformGizmoEditorSettings::TransformGizmoSize. The CR-specific gizmo scale was removed in UE 5.8 in favor of this editor-wide setting. Params: scale",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_gizmo_scale", S_epic_set_anim_mode_gizmo_scale, p),
  ),
  epic_set_anim_mode_hide_manips: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle whether Animation Mode hides all manipulators. Params: hide",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_hide_manips", S_epic_set_anim_mode_hide_manips, p),
  ),
  epic_set_anim_mode_hierarchy: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle the Animation Mode hierarchy lines/dots display. Params: enabled",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_hierarchy", S_epic_set_anim_mode_hierarchy, p),
  ),
  epic_set_anim_mode_local_spaces: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle multi-select transforms acting in each control's own space. When True, transforming multiple selected controls respects each control's own local space. When False, all use a shared reference. Params: enabled",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_local_spaces", S_epic_set_anim_mode_local_spaces, p),
  ),
  epic_set_anim_mode_nulls: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle the Animation Mode nulls display. Params: enabled",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_nulls", S_epic_set_anim_mode_nulls, p),
  ),
  epic_set_anim_mode_only_rig_sel: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle Animation Mode restricting viewport selection to rig controls. Params: only_rig",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_only_rig_sel", S_epic_set_anim_mode_only_rig_sel, p),
  ),
  epic_set_binding_name: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the display name of a binding. Params: binding, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_binding_name", S_epic_set_binding_name, p),
  ),
  epic_set_bool: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a bool control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, value, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_bool", S_epic_set_bool, p),
  ),
  epic_set_byte_track_enum: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Configure a byte track to use a specific enum type. Byte tracks can animate enum properties. Call this after adding the track and before setting property_name_and_path. Params: track, enum_class_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_byte_track_enum", S_epic_set_byte_track_enum, p),
  ),
  epic_set_camera_cut_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set which camera a camera cut section uses. Params: section, camera_binding_id",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_camera_cut_binding", S_epic_set_camera_cut_binding, p),
  ),
  epic_set_camera_lock: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Lock or unlock the camera cut to the viewport. Params: lock",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_camera_lock", S_epic_set_camera_lock, p),
  ),
  epic_set_clock_source: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the clock source for the sequence. Params: sequence, clock_source",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_clock_source", S_epic_set_clock_source, p),
  ),
  epic_set_controls_mask: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the visibility mask for the specified controls on a section. Params: section, control_names, visible",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_controls_mask", S_epic_set_controls_mask, p),
  ),
  epic_set_default_value: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Set the default value of a channel. Params: section, channel_name, value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.set_default_value", S_epic_set_default_value, p),
  ),
  epic_set_display_rate: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the display frame rate of a sequence. Params: sequence, numerator, denominator?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_display_rate", S_epic_set_display_rate, p),
  ),
  epic_set_euler_transform: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set an EulerTransform control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, location_x?, location_y?, location_z?, rotation_pitch?, rotation_yaw?, rotation_roll?, scale_x?, scale_y?, scale_z?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_euler_transform", S_epic_set_euler_transform, p),
  ),
  epic_set_evaluation_type: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the evaluation type of a sequence. Params: sequence, eval_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_evaluation_type", S_epic_set_evaluation_type, p),
  ),
  epic_set_float: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a float control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, value, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_float", S_epic_set_float, p),
  ),
  epic_set_global_transform: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set global transform of a hierarchy element. Params: control_rig, item, transform, initial?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.set_global_transform", S_epic_set_global_transform, p),
  ),
  epic_set_int: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set an integer control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, value, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_int", S_epic_set_int, p),
  ),
  epic_set_layered_mode: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a Control Rig track to layered or absolute mode. Params: sequence, control_rig_asset_path, is_layered",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_layered_mode", S_epic_set_layered_mode, p),
  ),
  epic_set_local_transform: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set local transform of a hierarchy element. Params: control_rig, item, transform, initial?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.set_local_transform", S_epic_set_local_transform, p),
  ),
  epic_set_loop_mode: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Enable or disable loop playback. Params: loop",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_loop_mode", S_epic_set_loop_mode, p),
  ),
  epic_set_material: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Assigns a material to a named slot on a skeletal mesh asset. This affects all instances of the mesh that do not override the slot material. Params: mesh, slot_name, material",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.set_material", S_epic_set_material, p),
  ),
  epic_set_node_deactivated: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Deactivate or reactivate outliner nodes. Params: nodes, deactivated",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_deactivated", S_epic_set_node_deactivated, p),
  ),
  epic_set_node_expanded: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Expand or collapse outliner nodes. Params: nodes, expanded",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_expanded", S_epic_set_node_expanded, p),
  ),
  epic_set_node_locked: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Lock or unlock outliner nodes for editing. Params: nodes, locked",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_locked", S_epic_set_node_locked, p),
  ),
  epic_set_node_muted: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Mute or unmute outliner nodes. Params: nodes, muted",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_muted", S_epic_set_node_muted, p),
  ),
  epic_set_node_pinned: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Pin or unpin outliner nodes. Params: nodes, pinned",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_pinned", S_epic_set_node_pinned, p),
  ),
  epic_set_node_position: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set the position of a node in the graph editor. Params: control_rig, graph, node, position",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.set_node_position", S_epic_set_node_position, p),
  ),
  epic_set_node_solo: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Solo or unsolo outliner nodes. Params: nodes, soloed",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_solo", S_epic_set_node_solo, p),
  ),
  epic_set_outliner_selection: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Set the outliner selection. Params: nodes",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.outliner.SequencerOutlinerTools", "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_outliner_selection", S_epic_set_outliner_selection, p),
  ),
  epic_set_pin_value: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set the default value of a pin. Params: control_rig, graph, pin, value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig.ControlRigTools", "animation_toolset.toolsets.controlrig.ControlRigTools.set_pin_value", S_epic_set_pin_value, p),
  ),
  epic_set_playback_range: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the playback start and end frames of a sequence. Params: sequence, start_frame, end_frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_range", S_epic_set_playback_range, p),
  ),
  epic_set_playback_range_locked: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Lock or unlock the playback range. Params: sequence, locked",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_range_locked", S_epic_set_playback_range_locked, p),
  ),
  epic_set_playback_speed: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the playback speed multiplier. Params: speed",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_speed", S_epic_set_playback_speed, p),
  ),
  epic_set_playhead_frame: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the playhead position in display rate frames. Params: frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_playhead_frame", S_epic_set_playhead_frame, p),
  ),
  epic_set_position: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a position control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, x?, y?, z?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_position", S_epic_set_position, p),
  ),
  epic_set_priority_order: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the evaluation priority order of a Control Rig track. Params: sequence, control_rig_asset_path, order",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_priority_order", S_epic_set_priority_order, p),
  ),
  epic_set_property_name_and_path: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Configure a property track to animate a specific UProperty. This binds a generic property track (Float, Bool, Byte, etc.) to a specific property on the bound object. For nested properties use dot notation in the path. Examples: display_name=\"Intensity\", property_path=\"Intensity\" display_name=\"Focus Distance\", property_path=\"FocusSettings.ManualFocusDistance\" display_name=\"Animation Mode\", property_path=\"AnimationMode\" Params: track, display_name, property_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_property_name_and_path", S_epic_set_property_name_and_path, p),
  ),
  epic_set_rotator: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a rotator control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, pitch?, yaw?, roll?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_rotator", S_epic_set_rotator, p),
  ),
  epic_set_scale: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a scale control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, x?, y?, z?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_scale", S_epic_set_scale, p),
  ),
  epic_set_section_animation: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the animation asset on a skeletal animation section. After adding a MovieSceneSkeletalAnimationTrack and section, call this to assign which AnimSequence plays in that section. Params: section, anim_sequence_path",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_animation", S_epic_set_section_animation, p),
  ),
  epic_set_section_blend_type: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the blend type of a section. Valid values: 'Absolute', 'Additive', 'Relative', 'Override'. Params: section, blend_type",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_blend_type", S_epic_set_section_blend_type, p),
  ),
  epic_set_section_completion_mode: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the completion mode of a section. Valid values: 'KeepState', 'RestoreState', 'ProjectDefault'. Params: section, completion_mode",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_completion_mode", S_epic_set_section_completion_mode, p),
  ),
  epic_set_section_condition: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Set a condition on a section. Common condition classes: - /Script/MovieSceneTracks.MovieScenePlatformCondition - /Script/MovieSceneTracks.MovieSceneDirectorBlueprintCondition - /Script/MovieScene.MovieSceneGroupCondition Params: section, condition_class",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.set_section_condition", S_epic_set_section_condition, p),
  ),
  epic_set_section_ease_in: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the ease-in duration of a section in frames. Enables manual ease override if not already active. Params: section, duration",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_in", S_epic_set_section_ease_in, p),
  ),
  epic_set_section_ease_out: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the ease-out duration of a section in frames. Enables manual ease override if not already active. Params: section, duration",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_out", S_epic_set_section_ease_out, p),
  ),
  epic_set_section_end_bounded: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set whether the section end frame is bounded or infinite. Params: section, bounded",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_end_bounded", S_epic_set_section_end_bounded, p),
  ),
  epic_set_section_post_roll_frames: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the number of frames to post-roll this section after it ends. Post-roll continues evaluation after the section's real end, useful for simulations that need to settle. Params: section, frames",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_post_roll_frames", S_epic_set_section_post_roll_frames, p),
  ),
  epic_set_section_pre_roll_frames: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the number of frames to pre-roll this section before it starts. Pre-roll evaluates the section before its real start so physics, cloth, or simulation state can warm up. The pre-roll frames do not affect the rendered output of the section. Params: section, frames",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_pre_roll_frames", S_epic_set_section_pre_roll_frames, p),
  ),
  epic_set_section_range: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the frame range of a section. Params: section, start_frame, end_frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_range", S_epic_set_section_range, p),
  ),
  epic_set_section_start_bounded: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set whether the section start frame is bounded or infinite. Params: section, bounded",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_section_start_bounded", S_epic_set_section_start_bounded, p),
  ),
  epic_set_selection_range: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the selection range (green bar) start and end frames. Params: start_frame, end_frame",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_selection_range", S_epic_set_selection_range, p),
  ),
  epic_set_sequence_locked: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Lock or unlock the current sequence and its descendants. Params: lock",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_sequence_locked", S_epic_set_sequence_locked, p),
  ),
  epic_set_socket_transform: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Sets the local transform of a socket relative to its parent bone. Params: mesh, socket_name, transform",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.set_socket_transform", S_epic_set_socket_transform, p),
  ),
  epic_set_space: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the space for a Control Rig control at a given frame. Params: sequence, control_rig_asset_path, control_name, space_type, frame, space_target",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_space", S_epic_set_space, p),
  ),
  epic_set_tick_resolution: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the internal tick resolution of a sequence. Params: sequence, numerator, denominator?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_tick_resolution", S_epic_set_tick_resolution, p),
  ),
  epic_set_track_condition: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Set a condition on a track. Params: track, condition_class",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.set_track_condition", S_epic_set_track_condition, p),
  ),
  epic_set_track_display_name: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the display name of a track. Params: track, name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_track_display_name", S_epic_set_track_display_name, p),
  ),
  epic_set_track_filter_active: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Enable or disable a track filter. Params: name, active",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_track_filter_active", S_epic_set_track_filter_active, p),
  ),
  epic_set_track_row_condition: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Set a condition on a specific track row. Params: track, row_index, condition_class",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.conditions.SequencerConditionTools", "animation_toolset.toolsets.conditions.SequencerConditionTools.set_track_row_condition", S_epic_set_track_row_condition, p),
  ),
  epic_set_transform: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a transform value on a Control Rig control and optionally key it. Uses ControlRigSequencerLibrary.set_local_control_rig_transform. Params: sequence, control_rig_asset_path, control_name, frame, location_x?, location_y?, location_z?, rotation_pitch?, rotation_yaw?, rotation_roll?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_transform", S_epic_set_transform, p),
  ),
  epic_set_vector2d: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a Vector2D control value at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, x?, y?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_vector2d", S_epic_set_vector2d, p),
  ),
  epic_set_view_range: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the visible time range in the Sequencer timeline. Params: sequence, start_seconds, end_seconds",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_view_range", S_epic_set_view_range, p),
  ),
  epic_set_work_range: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the work range of the sequence. Params: sequence, start_seconds, end_seconds",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.set_work_range", S_epic_set_work_range, p),
  ),
  epic_set_world_transform: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a control's world-space transform at a specific frame. Params: sequence, control_rig_asset_path, control_name, frame, location_x?, location_y?, location_z?, rotation_pitch?, rotation_yaw?, rotation_roll?, set_key?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_world_transform", S_epic_set_world_transform, p),
  ),
  epic_show_all_controls: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Show all controls on a Control Rig section (unmask everything). Params: section",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.show_all_controls", S_epic_show_all_controls, p),
  ),
  epic_show_curve: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Show or hide a curve in the Curve Editor. Params: channel, show",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.keyframing.SequencerKeyframingTools", "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.show_curve", S_epic_show_curve, p),
  ),
  epic_snap_control_rig: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Snap Control Rig controls to a target actor over a frame range. Params: sequence, control_rig_asset_path, control_names, target_actor_name, start_frame, end_frame, keep_offset?, snap_position?, snap_rotation?, snap_scale?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.snap_control_rig", S_epic_snap_control_rig, p),
  ),
  epic_tag_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Attach a tag to a binding. If the tag has not been seen in the sequence before, it is automatically registered. Params: binding, tag_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.tag_binding", S_epic_tag_binding, p),
  ),
  epic_tween_control_rig: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Perform a tween operation on a Control Rig at the current Sequencer time. The tween blends between the previous and next keyframe values. Params: sequence, control_rig_asset_path, tween_value",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.tween_control_rig", S_epic_tween_control_rig, p),
  ),
  epic_untag_binding: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a tag from a binding. Params: binding, tag_name",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.sequencer.SequencerTools", "animation_toolset.toolsets.sequencer.SequencerTools.untag_binding", S_epic_untag_binding, p),
  ),
  epic_zero_transforms: bp(
    "mutate",
    "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Reset Control Rig transforms to their default (usually zero) values. Params: selection_only?, include_channels?",
    "epic_call_tool",
    (p) => epicToolCall("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.zero_transforms", S_epic_zero_transforms, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  active: z.boolean().optional(),
  actor_class: z.string().optional(),
  actor_class_path: z.string().optional(),
  actor_name: z.string().optional(),
  actor_names: z.array(z.unknown()).optional(),
  actors: z.array(z.unknown()).optional(),
  anim_sequence: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  anim_sequence_path: z.string().optional(),
  ascii: z.boolean().optional(),
  asset_name: z.string().optional(),
  binding: z.record(z.unknown()).optional(),
  binding_type_class: z.string().optional(),
  bindings: z.array(z.unknown()).optional(),
  blend_type: z.string().optional(),
  blend_value: z.number().optional(),
  bone_name: z.string().optional(),
  bounded: z.boolean().optional(),
  camera_binding_id: z.string().optional(),
  channel: z.record(z.unknown()).optional(),
  channel_name: z.string().optional(),
  channels: z.array(z.unknown()).optional(),
  clock_source: z.string().optional(),
  completion_mode: z.string().optional(),
  component_bindings: z.array(z.unknown()).optional(),
  component_name: z.string().optional(),
  condition_class: z.string().optional(),
  control_name: z.string().optional(),
  control_names: z.array(z.unknown()).optional(),
  control_rig: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  control_rig_asset_path: z.string().optional(),
  cr_section: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  create_link: z.boolean().optional(),
  create_physics_asset: z.boolean().optional(),
  deactivated: z.boolean().optional(),
  default_value: z.string().optional(),
  denominator: z.number().optional(),
  display_name: z.string().optional(),
  duration: z.number().optional(),
  element_type: z.string().optional(),
  enabled: z.boolean().optional(),
  end_frame: z.number().optional(),
  end_seconds: z.number().optional(),
  enum_class_path: z.string().optional(),
  eval_type: z.string().optional(),
  event_type: z.string().optional(),
  expanded: z.boolean().optional(),
  export_file_path: z.string().optional(),
  fbx_file_path: z.string().optional(),
  folder: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  folder_path: z.string().optional(),
  folders: z.array(z.unknown()).optional(),
  frame: z.number().optional(),
  frames: z.unknown().optional(),
  graph: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  graph_name: z.string().optional(),
  hide: z.boolean().optional(),
  import_animations: z.boolean().optional(),
  import_file_path: z.string().optional(),
  import_materials: z.boolean().optional(),
  import_settings: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  import_textures: z.boolean().optional(),
  include_channels: z.boolean().optional(),
  index: z.number().optional(),
  indices: z.array(z.unknown()).optional(),
  initial: z.boolean().optional(),
  interpolation: z.string().optional(),
  is_getter: z.boolean().optional(),
  is_layered: z.boolean().optional(),
  is_public: z.boolean().optional(),
  is_read_only: z.boolean().optional(),
  item: z.record(z.unknown()).optional(),
  keep_offset: z.boolean().optional(),
  key_reduce: z.boolean().optional(),
  label: z.string().optional(),
  location_x: z.number().optional(),
  location_y: z.number().optional(),
  location_z: z.number().optional(),
  lock: z.boolean().optional(),
  locked: z.boolean().optional(),
  lod_index: z.number().optional(),
  loop: z.boolean().optional(),
  material: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  mesh: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  muted: z.boolean().optional(),
  name: z.string().optional(),
  new_frame: z.number().optional(),
  new_index: z.number().optional(),
  new_name: z.string().optional(),
  new_type: z.string().optional(),
  node: z.unknown().optional().describe("Represents a reference to a UObject or UClass."),
  node_name: z.string().optional(),
  node_type: z.string().optional(),
  nodes: z.array(z.unknown()).optional(),
  numerator: z.number().optional(),
  obj: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  old_frame: z.number().optional(),
  old_index: z.number().optional(),
  old_name: z.string().optional(),
  only_rig: z.boolean().optional(),
  operation: z.string().optional(),
  order: z.number().optional(),
  override_options: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  package_path: z.string().optional(),
  parent: z.record(z.unknown()).optional(),
  parent_folder: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  paste_frame: z.number().optional(),
  paste_token: z.string().optional(),
  path: z.string().optional(),
  physics_asset: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  pin: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  pinned: z.boolean().optional(),
  pitch: z.number().optional(),
  position: z.record(z.unknown()).optional(),
  property_path: z.string().optional(),
  recursive: z.boolean().optional(),
  reduce_keys: z.boolean().optional(),
  reset_controls: z.boolean().optional(),
  roll: z.number().optional(),
  rotation_pitch: z.number().optional(),
  rotation_roll: z.number().optional(),
  rotation_yaw: z.number().optional(),
  row_index: z.number().optional(),
  scale: z.number().optional(),
  scale_x: z.number().optional(),
  scale_y: z.number().optional(),
  scale_z: z.number().optional(),
  section: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  sections: z.array(z.unknown()).optional(),
  selected: z.boolean().optional(),
  selected_controls: z.array(z.unknown()).optional(),
  selection_only: z.boolean().optional(),
  sequence: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  set_key: z.boolean().optional(),
  settings: z.record(z.unknown()).optional(),
  show: z.boolean().optional(),
  skeletal_mesh: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  skeleton: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  slot_name: z.string().optional(),
  snap_position: z.boolean().optional(),
  snap_rotation: z.boolean().optional(),
  snap_scale: z.boolean().optional(),
  socket_name: z.string().optional(),
  soloed: z.boolean().optional(),
  source_file: z.string().optional(),
  source_pin: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  space_target: z.string().optional(),
  space_type: z.string().optional(),
  spawnable: z.boolean().optional(),
  speed: z.number().optional(),
  start_frame: z.number().optional(),
  start_seconds: z.number().optional(),
  sub_section: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  tag_name: z.string().optional(),
  target_actor_name: z.string().optional(),
  target_bindings: z.array(z.unknown()).optional(),
  target_pin: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  target_tracks: z.array(z.unknown()).optional(),
  tolerance: z.number().optional(),
  track: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  track_type: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  tracks: z.array(z.unknown()).optional(),
  transform: z.record(z.unknown()).optional().describe("Represents a 3D transformation with optional location, rotation, and scale. Unset fields mean \"identity\" when creating objects and \"don't change\" when modifyin…"),
  tween_value: z.number().optional(),
  type_filter: z.string().optional(),
  type_path: z.string().optional(),
  value: z.unknown().optional(),
  variable_name: z.string().optional(),
  visible: z.boolean().optional(),
  world: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  x: z.number().optional(),
  y: z.number().optional(),
  yaw: z.number().optional(),
  z: z.number().optional(),
};
