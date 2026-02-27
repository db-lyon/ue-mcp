export const SERVER_INSTRUCTIONS = `UE-MCP: Unreal Engine editor bridge — 18 category tools covering 220+ actions.

Every tool takes an "action" parameter that selects the operation. Call project(action="get_status") first.

═══ TOOLS & ACTIONS ═══

project — Project status, config INI, C++ source
  get_status, set_project, get_info, read_config, search_config, list_config_tags,
  set_config, read_cpp_header, read_module, list_modules, search_cpp

asset — Assets: list, search, CRUD, import, datatables, textures
  list, search, read, read_properties, duplicate, rename, move, delete, save,
  import_static_mesh, import_skeletal_mesh, import_animation, import_texture,
  read_datatable, create_datatable, reimport_datatable, list_textures,
  get_texture_info, set_texture_settings

blueprint — Blueprint reading, authoring, compilation
  read, list_variables, list_functions, read_graph, create, add_variable,
  set_variable_properties, create_function, delete_function, rename_function,
  add_node, delete_node, set_node_property, connect_pins, add_component,
  compile, list_node_types, search_node_types, create_interface, add_interface,
  add_event_dispatcher

level — Level actors, selection, components, volumes, lights, splines
  get_outliner, place_actor, delete_actor, get_actor_details, move_actor,
  select, get_selected, add_component, set_component_property,
  get_current, load, save, list, create,
  spawn_volume, list_volumes, set_volume_properties,
  spawn_light, set_light_properties, build_lighting,
  get_spline_info, set_spline_points

material — Materials, shading, and graph authoring
  read, list_parameters, set_parameter, create_instance, create,
  set_shading_model, set_base_color, connect_texture,
  add_expression, connect_expressions, connect_to_property,
  list_expressions, delete_expression, list_expression_types, recompile

animation — Anim assets, skeletons, montages, blendspaces
  read_anim_blueprint, read_montage, read_sequence, read_blendspace, list,
  create_montage, create_anim_blueprint, create_blendspace, add_notify,
  get_skeleton_info, list_sockets, list_skeletal_meshes, get_physics_asset

landscape — Terrain sculpting, painting, layers
  get_info, list_layers, sample, list_splines, get_component,
  sculpt, paint_layer, set_material, add_layer_info, import_heightmap

pcg — Procedural Content Generation graphs
  list_graphs, read_graph, read_node_settings, get_components,
  get_component_details, create_graph, add_node, connect_nodes,
  set_node_settings, remove_node, execute, add_volume

foliage — Foliage painting and types
  list_types, get_settings, sample, paint, erase, create_type, set_settings

niagara — VFX systems and graph authoring
  list, get_info, spawn, set_parameter, create,
  create_emitter, add_emitter, list_emitters, set_emitter_property,
  list_modules, get_emitter_info

audio — Sound assets and playback
  list, play_at_location, spawn_ambient, create_cue, create_metasound

widget — UMG widgets and editor utilities
  read_tree, get_details, set_property, list, read_animations, create,
  create_utility_widget, run_utility_widget,
  create_utility_blueprint, run_utility_blueprint

editor — Console, Python, PIE, viewport, sequencer, perf, build pipeline, logs
  execute_command, execute_python, set_property, play_in_editor,
  get_runtime_value, hot_reload, undo, redo,
  get_perf_stats, run_stat, set_scalability, capture_screenshot,
  get_viewport, set_viewport, focus_on_actor,
  create_sequence, get_sequence_info, add_sequence_track, play_sequence,
  build_all, build_geometry, build_hlod, validate_assets,
  get_build_status, cook_content,
  get_log, search_log, get_message_log

reflection — UE class/struct/enum reflection, gameplay tags
  reflect_class, reflect_struct, reflect_enum, list_classes,
  list_tags, create_tag

gameplay — Physics, collision, navigation, input, behavior trees, AI, game framework
  set_collision_profile, set_simulate_physics, set_collision_enabled,
  set_physics_properties, rebuild_navigation, get_navmesh_info,
  project_to_nav, spawn_nav_modifier,
  create_input_action, create_input_mapping, list_input_assets,
  list_behavior_trees, get_behavior_tree_info,
  create_blackboard, create_behavior_tree,
  create_eqs_query, list_eqs_queries,
  add_perception, configure_sense,
  create_state_tree, list_state_trees, add_state_tree_component,
  create_smart_object_def, add_smart_object_component,
  create_game_mode, create_game_state, create_player_controller,
  create_player_state, create_hud, set_world_game_mode, get_framework_info

gas — Gameplay Ability System
  add_asc, create_attribute_set, add_attribute,
  create_ability, set_ability_tags,
  create_effect, set_effect_modifier,
  create_cue, get_info

networking — Replication and networking
  set_replicates, set_property_replicated, configure_net_frequency,
  set_dormancy, set_net_load_on_client, set_always_relevant,
  set_only_relevant_to_owner, configure_cull_distance,
  set_priority, set_replicate_movement, get_info

demo — Neon Shrine demo scene
  step, cleanup

═══ TIPS ═══
• Start with level(action="get_outliner") or asset(action="list") to discover what's in the project.
• Use reflection(action="reflect_class") to understand any UE class's properties.
• asset(action="search", query="/Game/Characters/*") accepts wildcards.
• For BP scripting: blueprint(action="search_node_types") → blueprint(action="add_node") → blueprint(action="connect_pins").
• editor(action="execute_python") is the escape hatch for any Unreal Python API call.
• Animation tools need a skeleton path — use animation(action="list_skeletal_meshes") to find it.
`;
