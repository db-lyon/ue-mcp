// @session independent: Generated action facts from the recorded C++ registrations; data only, with no editor state.
// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json
// (actionFacts), recorded from get_bridge_capabilities. To change one, change the
// action's C++ registration (FMCPSpecRules: Timeout, ModalSafe, RefusedInBatch,
// ServerInternal), re-record and regenerate.

/** How the bridge runs an action. Only what differs from the common case is present. */
export interface ActionFacts {
  /** Game-thread budget in seconds; absent is the bridge default. */
  timeoutSeconds?: number;
  /** Runs while a modal dialog holds the game thread. */
  modalSafe?: boolean;
  /** Answered on the socket thread, before the dialog gate. */
  servedOffGameThread?: boolean;
  /** Left out of the parameter echo. */
  notEchoed?: boolean;
  /** Why execute_batch refuses to carry it. */
  refusedInBatch?: string;
  /** Who calls it instead of a tool. */
  serverInternal?: string;
}

/** Every bridge method whose facts differ from the default, keyed by method. */
export const ACTION_FACTS: Readonly<Record<string, ActionFacts>> = {
  "acquire_lock": {
    "serverInternal": "the server's asset locks (src/dispatch/locking.ts) and asset(lock, unlock, unlock_all)"
  },
  "add_widget": {
    "timeoutSeconds": 120
  },
  "analyze_insights_trace": {
    "timeoutSeconds": 600
  },
  "analyze_landscape_terrain": {
    "timeoutSeconds": 120
  },
  "apply_landscape_erosion": {
    "timeoutSeconds": 600
  },
  "apply_landscape_splines": {
    "timeoutSeconds": 300
  },
  "apply_mesh_fracture": {
    "timeoutSeconds": 600
  },
  "apply_mesh_hole_fill": {
    "timeoutSeconds": 300
  },
  "apply_mesh_mirror": {
    "timeoutSeconds": 300
  },
  "apply_mesh_remesh": {
    "timeoutSeconds": 300
  },
  "apply_mesh_simplify": {
    "timeoutSeconds": 300
  },
  "audit_asset_hygiene": {
    "timeoutSeconds": 300
  },
  "audit_blueprint_dead_code": {
    "timeoutSeconds": 600
  },
  "audit_level_performance": {
    "timeoutSeconds": 300
  },
  "audit_replication": {
    "timeoutSeconds": 300
  },
  "batch_retarget_animations": {
    "timeoutSeconds": 300
  },
  "batch_set_actor_properties": {
    "timeoutSeconds": 300
  },
  "batch_set_foliage_settings_where": {
    "timeoutSeconds": 300
  },
  "begin_editor_transaction": {
    "refusedInBatch": "a batch is already one transaction, and this would split or abort it; leave the batch to record the transaction"
  },
  "build_hlod_actors": {
    "timeoutSeconds": 1800
  },
  "build_tree": {
    "timeoutSeconds": 120
  },
  "build_world_partition_hlods": {
    "timeoutSeconds": 3600
  },
  "build_world_partition_minimap": {
    "timeoutSeconds": 3600
  },
  "bulk_read_asset_properties": {
    "timeoutSeconds": 300
  },
  "bulk_restore_data_assets": {
    "timeoutSeconds": 120
  },
  "bulk_set_component_property": {
    "timeoutSeconds": 300
  },
  "bulk_upsert_data_assets": {
    "timeoutSeconds": 120
  },
  "cancel_editor_transaction": {
    "refusedInBatch": "a batch is already one transaction, and this would split or abort it; leave the batch to record the transaction"
  },
  "cancel_request": {
    "servedOffGameThread": true,
    "serverInternal": "the server's bridge client (src/bridge)"
  },
  "checkpoint_diff_asset": {
    "timeoutSeconds": 300,
    "serverInternal": "the server's checkpoint service (src/checkpoints/service.ts)"
  },
  "checkpoint_diff_blueprint": {
    "serverInternal": "the server's checkpoint service (src/checkpoints/service.ts)"
  },
  "checkpoint_finish_restore": {
    "timeoutSeconds": 300
  },
  "checkpoint_open_native_diff": {
    "timeoutSeconds": 120,
    "serverInternal": "the server's checkpoint service (src/checkpoints/service.ts)"
  },
  "clear_dialog_policy": {
    "modalSafe": true
  },
  "clear_param_echo": {
    "servedOffGameThread": true,
    "notEchoed": true,
    "serverInternal": "the server's bridge client (src/bridge)"
  },
  "close_window": {
    "modalSafe": true
  },
  "compile_customizable_object": {
    "timeoutSeconds": 600
  },
  "compile_localization_target": {
    "timeoutSeconds": 1800
  },
  "convert_brushes_to_static_mesh": {
    "timeoutSeconds": 600
  },
  "create_cpp_class": {
    "timeoutSeconds": 300
  },
  "datasmith_import": {
    "timeoutSeconds": 600
  },
  "datasmith_inspect": {
    "timeoutSeconds": 300
  },
  "datasmith_reimport": {
    "timeoutSeconds": 600
  },
  "delete_exact_labeled_actors_in_levels": {
    "timeoutSeconds": 300
  },
  "delete_world_partition_hlods": {
    "timeoutSeconds": 3600
  },
  "end_editor_transaction": {
    "refusedInBatch": "a batch is already one transaction, and this would split or abort it; leave the batch to record the transaction"
  },
  "epic_call_mcp_tool": {
    "timeoutSeconds": 300
  },
  "epic_call_tool": {
    "timeoutSeconds": 300
  },
  "execute_batch": {
    "refusedInBatch": "a nested batch is the same game-thread job twice; put its steps in this batch instead",
    "serverInternal": "editor(batch), which builds the steps from category actions"
  },
  "export_blueprint_batch": {
    "timeoutSeconds": 600
  },
  "export_landscape_heightmap": {
    "timeoutSeconds": 300
  },
  "fix_asset_hygiene": {
    "timeoutSeconds": 300
  },
  "fixup_redirectors": {
    "timeoutSeconds": 300
  },
  "flush_blueprint_component_templates": {
    "timeoutSeconds": 120
  },
  "gather_localization_target": {
    "timeoutSeconds": 1800
  },
  "generate_mesh_collision": {
    "timeoutSeconds": 300
  },
  "generate_skeletal_mesh_lods": {
    "timeoutSeconds": 600
  },
  "get_blueprint_connections": {
    "timeoutSeconds": 600
  },
  "get_bridge_capabilities": {
    "servedOffGameThread": true,
    "serverInternal": "the server's bridge client (src/bridge)"
  },
  "get_dialog_policy": {
    "modalSafe": true
  },
  "get_engine_state": {
    "servedOffGameThread": true,
    "serverInternal": "the server's bridge client (src/bridge)"
  },
  "get_landscape_height_region": {
    "timeoutSeconds": 120
  },
  "get_landscape_holes": {
    "timeoutSeconds": 120
  },
  "get_landscape_layer_weight_region": {
    "timeoutSeconds": 120
  },
  "get_landscape_slope_map": {
    "timeoutSeconds": 120
  },
  "get_param_echo": {
    "servedOffGameThread": true,
    "notEchoed": true,
    "serverInternal": "the server's bridge client (src/bridge)"
  },
  "get_skinned_vertices": {
    "timeoutSeconds": 300
  },
  "get_static_mesh_geometry": {
    "timeoutSeconds": 300
  },
  "import_landscape_heightmap": {
    "timeoutSeconds": 600
  },
  "invoke_object_functions": {
    "timeoutSeconds": 300
  },
  "list_dialogs": {
    "modalSafe": true
  },
  "list_windows": {
    "modalSafe": true
  },
  "live_coding_compile": {
    "timeoutSeconds": 300
  },
  "load_actor_descs": {
    "timeoutSeconds": 300
  },
  "load_region": {
    "timeoutSeconds": 300
  },
  "merge_landscape_edit_layers": {
    "timeoutSeconds": 300
  },
  "mesh_boolean": {
    "timeoutSeconds": 300
  },
  "metahuman_assemble": {
    "timeoutSeconds": 900
  },
  "metahuman_read_character": {
    "timeoutSeconds": 300
  },
  "metahuman_request_rig": {
    "timeoutSeconds": 900
  },
  "metahuman_request_textures": {
    "timeoutSeconds": 900
  },
  "metahuman_set_body": {
    "timeoutSeconds": 300
  },
  "metahuman_set_wardrobe": {
    "timeoutSeconds": 300
  },
  "mrq_render": {
    "timeoutSeconds": 1800
  },
  "paint_landscape_layer": {
    "timeoutSeconds": 120
  },
  "pie_start_ignoring_blueprint_errors": {
    "serverInternal": "editor(play_in_editor_ignore_blueprint_errors), after the server authorizes the launch"
  },
  "plan_real_world_landscape": {
    "timeoutSeconds": 120
  },
  "press_window_button": {
    "modalSafe": true
  },
  "query_components": {
    "timeoutSeconds": 300
  },
  "read_blueprint_graph": {
    "timeoutSeconds": 180
  },
  "read_skeletal_mesh_vertices": {
    "timeoutSeconds": 300
  },
  "rebuild_water_zone": {
    "timeoutSeconds": 120
  },
  "recreate_physics_state": {
    "timeoutSeconds": 300
  },
  "redo": {
    "refusedInBatch": "inside a batch it would undo or redo the batch's own earlier steps; call it after the batch"
  },
  "refresh_landscape_physical_material_collision": {
    "timeoutSeconds": 600
  },
  "release_lock": {
    "serverInternal": "the server's asset locks (src/dispatch/locking.ts) and asset(lock, unlock, unlock_all)"
  },
  "release_session_locks": {
    "serverInternal": "the server's asset locks (src/dispatch/locking.ts) and asset(lock, unlock, unlock_all)"
  },
  "remove_components_by_class": {
    "timeoutSeconds": 300
  },
  "remove_widget": {
    "timeoutSeconds": 120
  },
  "rename_widget": {
    "timeoutSeconds": 120
  },
  "render_mesh_studio": {
    "timeoutSeconds": 600
  },
  "render_sequence_frames": {
    "timeoutSeconds": 600
  },
  "rerun_construction_scripts": {
    "timeoutSeconds": 300
  },
  "resave_mesh_users": {
    "timeoutSeconds": 300
  },
  "respond_to_dialog": {
    "modalSafe": true
  },
  "restore_texture_import": {
    "timeoutSeconds": 120
  },
  "run_automation_tests": {
    "timeoutSeconds": 300
  },
  "scale_mesh_geometry": {
    "timeoutSeconds": 300
  },
  "sculpt_landscape": {
    "timeoutSeconds": 120
  },
  "sculpt_landscape_region": {
    "timeoutSeconds": 300
  },
  "search_blueprint_call_sites": {
    "timeoutSeconds": 600
  },
  "search_blueprint_nodes": {
    "timeoutSeconds": 600
  },
  "semantic_search": {
    "timeoutSeconds": 120
  },
  "set_actor_hlod_layer": {
    "timeoutSeconds": 300
  },
  "set_blueprint_component_properties_batch": {
    "timeoutSeconds": 600
  },
  "set_component_materials": {
    "timeoutSeconds": 300
  },
  "set_dialog_policy": {
    "modalSafe": true
  },
  "set_landscape_height_region": {
    "timeoutSeconds": 300
  },
  "set_landscape_holes": {
    "timeoutSeconds": 300
  },
  "set_landscape_layer_weight_region": {
    "timeoutSeconds": 300
  },
  "set_mesh_simple_collision": {
    "timeoutSeconds": 300
  },
  "simulate_procedural_foliage": {
    "timeoutSeconds": 600
  },
  "snap_instances_to_surface": {
    "timeoutSeconds": 300
  },
  "source_control_connect": {
    "timeoutSeconds": 120
  },
  "source_control_diff": {
    "timeoutSeconds": 120
  },
  "source_control_get_status": {
    "timeoutSeconds": 120
  },
  "source_control_history": {
    "timeoutSeconds": 120
  },
  "source_control_list_changes": {
    "timeoutSeconds": 120
  },
  "source_control_submit": {
    "timeoutSeconds": 120
  },
  "source_control_sync": {
    "timeoutSeconds": 300
  },
  "spawn_actors_batch": {
    "timeoutSeconds": 300
  },
  "start_csv_profile": {
    "timeoutSeconds": 30
  },
  "stop_csv_profile": {
    "timeoutSeconds": 90
  },
  "summarize_static_mesh_usage": {
    "timeoutSeconds": 300
  },
  "transform_skinned_part": {
    "timeoutSeconds": 600
  },
  "trigger_hitch": {
    "timeoutSeconds": 30
  },
  "undo": {
    "refusedInBatch": "inside a batch it would undo or redo the batch's own earlier steps; call it after the batch"
  },
  "undo_redo_steps": {
    "refusedInBatch": "inside a batch it would undo or redo the batch's own earlier steps; call it after the batch"
  },
  "unload_loaded_regions": {
    "timeoutSeconds": 300
  }
};
