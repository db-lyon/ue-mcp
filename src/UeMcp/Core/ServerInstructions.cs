namespace UeMcp.Core;

public static class ServerInstructions
{
    public const string Text = """
UE-MCP: Unreal Engine editor bridge with 185+ tools. Tools operate in two modes:

OFFLINE (read-only, no editor needed): read/inspect assets, configs, C++ headers, blueprints.
LIVE (editor connected via Python bridge): create/modify actors, blueprints, materials, run commands.

Call get_status first if unsure whether the editor is connected.

═══ TOOL DIRECTORY ═══

ASSET MANAGEMENT:
  Read: read_asset, read_asset_properties, list_assets, search_assets, asset_to_json
  Manage: duplicate_asset, rename_asset, move_asset, delete_asset

IMPORT:
  import_static_mesh (FBX/OBJ), import_skeletal_mesh (FBX), import_animation (FBX), import_texture

BLUEPRINTS (read & author):
  Read: read_blueprint, list_blueprint_variables, list_blueprint_functions, read_blueprint_graph
  Create: create_blueprint, add_blueprint_variable, create_blueprint_function, add_blueprint_component
  Edit: set_blueprint_variable_properties, delete_blueprint_function, rename_blueprint_function
  Nodes: add_blueprint_node, delete_blueprint_node, set_blueprint_node_property, connect_blueprint_pins
  Search: list_node_types, search_node_types
  Interfaces: create_blueprint_interface, add_blueprint_interface
  Events: add_event_dispatcher
  Compile: compile_blueprint

LEVELS & WORLD:
  Outliner: get_world_outliner, get_actor_details
  Actors: place_actor, move_actor, delete_actor
  Selection: select_actors, get_selected_actors
  Components: add_component_to_actor, set_component_property
  Levels: get_current_level, load_level, save_current_level, list_levels, create_new_level

MATERIALS:
  Create: create_material, create_material_instance
  Read: read_material, list_material_parameters
  Edit: set_material_parameter, set_material_shading_model, set_material_base_color, connect_texture_to_material

LANDSCAPE & TERRAIN:
  Info: get_landscape_info, list_landscape_layers, get_landscape_component
  Edit: sculpt_landscape, paint_landscape_layer, set_landscape_material, add_landscape_layer_info
  Import: import_landscape_heightmap
  Splines: list_landscape_splines, sample_landscape

PCG (Procedural Content Generation):
  Graphs: list_pcg_graphs, read_pcg_graph, create_pcg_graph
  Nodes: add_pcg_node, connect_pcg_nodes, set_pcg_node_settings, remove_pcg_node, read_pcg_node_settings
  Execute: execute_pcg_graph, add_pcg_volume, get_pcg_components, get_pcg_component_details

FOLIAGE:
  Create: create_foliage_type (from a StaticMesh)
  Read: list_foliage_types, get_foliage_type_settings, sample_foliage
  Edit: set_foliage_type_settings, paint_foliage, erase_foliage

LIGHTING:
  spawn_light, set_light_properties, build_lighting

SEQUENCER / CINEMATICS:
  create_level_sequence, get_sequence_info, add_sequence_track, play_sequence

NIAGARA (VFX):
  Create: create_niagara_system
  Read: list_niagara_systems, get_niagara_info
  Use: spawn_niagara_at_location, set_niagara_parameter

AUDIO:
  Create: create_sound_cue, create_metasound_source
  Read: list_sound_assets
  Use: play_sound_at_location, spawn_ambient_sound

ANIMATION:
  Create: create_anim_montage (from AnimSequence), create_anim_blueprint (needs skeleton), create_blendspace (needs skeleton)
  Read: read_anim_blueprint, read_anim_montage, read_anim_sequence, read_blendspace, list_anim_assets
  Edit: add_anim_notify

SKELETON & PHYSICS ASSETS (read-only — skeletons come from mesh imports):
  get_skeleton_info, list_sockets, list_skeletal_meshes, get_physics_asset_info

PHYSICS & COLLISION:
  set_collision_profile, set_simulate_physics, set_collision_enabled, set_physics_properties

WIDGETS / UMG:
  Create: create_widget_blueprint
  Read: read_widget_tree, get_widget_details, read_widget_animations, list_widget_blueprints
  Edit: set_widget_property, get_widget_tree

EDITOR UTILITIES (custom editor tools):
  Create: create_editor_utility_widget (dockable UI panel), create_editor_utility_blueprint (headless script)
  Run: run_editor_utility_widget (open as tab), run_editor_utility_blueprint (execute script)

NAVIGATION:
  rebuild_navigation, get_navmesh_info, project_point_to_navigation, spawn_nav_modifier_volume

SPLINES:
  get_spline_info, set_spline_points

VOLUMES:
  spawn_volume, list_volumes, set_volume_properties

INPUT (Enhanced Input):
  create_input_action, create_input_mapping_context, list_input_assets

BEHAVIOR TREES (AI):
  create_behavior_tree, create_blackboard, list_behavior_trees, get_behavior_tree_info

TEXTURES:
  Import: import_texture
  Read: list_textures, get_texture_info
  Edit: set_texture_settings

DATA TABLES:
  Create: create_datatable (needs row struct name)
  Read: read_datatable
  Import: reimport_datatable (from JSON)

CONFIG / INI:
  Read: read_config, search_config, list_config_tags
  Write: set_config (modify DefaultEngine.ini, DefaultGame.ini, etc.)

C++ / REFLECTION:
  Headers: read_cpp_header, read_module, list_modules, search_cpp
  Reflect: reflect_class, reflect_struct, reflect_enum, list_classes
  Tags: list_gameplay_tags, create_gameplay_tag

EDITOR COMMANDS:
  General: editor_execute, execute_python, hot_reload, undo, redo, save_asset
  PIE: play_in_editor, get_runtime_value
  Properties: set_property

PERFORMANCE & VIEWPORT:
  get_editor_performance_stats, run_stat_command, set_scalability
  capture_screenshot, get_viewport_info, set_viewport_camera, focus_viewport_on_actor

STATUS & PROJECT:
  get_status, set_project, get_project_info

DEMO:
  demo_scene_from_nothing, demo_cleanup

═══ TIPS ═══
• Start with get_world_outliner or list_assets to discover what's in the project.
• Use reflect_class to understand any UE class's properties before using set_property.
• search_assets accepts wildcards: search_assets(query="/Game/Characters/*")
• For blueprint scripting: search_node_types to find the right node, add_blueprint_node to place it, connect_blueprint_pins to wire it up.
• For blueprint interfaces: create_blueprint_interface, then add_blueprint_interface to a BP.
• execute_python is the escape hatch — any Unreal Python API call that doesn't have a dedicated tool.
• Most "create_*" tools need a package path and asset name. They auto-save the new asset.
• import_static_mesh / import_skeletal_mesh / import_animation take absolute filesystem paths to FBX/OBJ files.
• duplicate_asset / rename_asset / move_asset take content paths (e.g. '/Game/Meshes/SM_Rock').
• Animation tools (create_anim_blueprint, create_blendspace) require a skeleton path — use list_skeletal_meshes to find it.
• select_actors takes an array of labels — use get_world_outliner to find actor labels first.
• set_config writes to INI files. Changes to rendering/physics settings may need editor restart.
""";
}
