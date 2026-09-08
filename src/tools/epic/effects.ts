/**
 * What each wrapped engine tool does to the editor it is addressed to.
 *
 * This file is the reviewed half of the Epic action surface. The catalog in
 * `tests/golden/epic-catalog.json` says what tools exist and what arguments
 * they take; Epic ships no annotation saying whether one reads or writes, so
 * that judgement is made here, by a person, once, and lives in a diff.
 *
 * It is hand-edited. `npm run epic:seed` only ADDS entries for tools that have
 * none, so re-recording a newer engine appends its new tools and never
 * overwrites a decision already made. `npm run epic:generate` refuses to run
 * while any tool in the catalog is missing from here.
 *
 * An entry marked `// SEEDED` was written by the seeder from the tool's own
 * description and has not been read by a person yet. `npm run epic:unreviewed`
 * lists them. Removing that comment is what "I have read this one" means.
 *
 *   read     observes. Landing it in the wrong editor returns the wrong answer
 *            and changes nothing.
 *   mutate   may change the editor, its project on disk, or its process.
 *   unknown  decided by an argument rather than by the tool. Gated as mutate.
 */
import type { ActionEffect } from "../../types.js";

/** Keyed by the tool's fully qualified registry name. */
export const EPIC_TOOL_EFFECTS: Record<string, ActionEffect> = {
  /* ── aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools ─── */
  // Returns the blackboard asset for this behavior tree.
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_blackboard": "read",
  // Returns direct child nodes of a composite node.
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_children": "read",
  // Returns the tree depth of a node by its list_nodes index.
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_node_depth": "read",
  // Returns tree depths for all nodes, matching list_nodes order.
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_node_depths": "read",
  // Returns root-level decorators on this tree.
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_root_decorators": "read",
  // Returns the sub-BT asset referenced by a RunBehavior task.
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.get_subtree": "read",
  // Returns a flat list of all node UObjects in tree order. Order: root decorators, then DFS (composite, services,
  "aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools.list_nodes": "read",

  /* ── animation_toolset.toolsets.conditions.SequencerConditionTools  */
  // Remove the condition from a section.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_section_condition": "mutate",
  // Remove the condition from a track.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_track_condition": "mutate",
  // Remove the condition from a specific track row.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_track_row_condition": "mutate",
  // Get the condition on a section. Returns the class path of the condition, or an empty string if no condition is
  "animation_toolset.toolsets.conditions.SequencerConditionTools.get_section_condition": "read",
  // Get the track-level condition.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.get_track_condition": "read",
  // Get the condition on a specific track row.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.get_track_row_condition": "read",
  // Set a condition on a section. Common condition classes: - /Script/MovieSceneTracks.MovieScenePlatformCondition
  "animation_toolset.toolsets.conditions.SequencerConditionTools.set_section_condition": "mutate",
  // Set a condition on a track.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.set_track_condition": "mutate",
  // Set a condition on a specific track row.
  "animation_toolset.toolsets.conditions.SequencerConditionTools.set_track_row_condition": "mutate",

  /* ── animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools  */
  // Add an animation layer from the currently selected objects in Sequencer.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.add_layer_from_selection": "mutate",
  // Bake Control Rig controls' space over a frame range.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_space": "mutate",
  // Bake existing animation on a binding into a Control Rig track.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_to_control_rig": "mutate",
  // Perform a blend operation on selected keys or controls.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.blend_values_on_selected": "mutate",
  // Clear the current Control Rig control selection.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.clear_selection": "mutate",
  // Collapse all sections and layers on a Control Rig track into one section.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.collapse_anim_layers": "mutate",
  // Delete an animation layer at the specified index.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_anim_layer": "mutate",
  // Delete a space-switch key at a specific frame. Performs compensation to the new space automatically.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_space": "mutate",
  // Duplicate an animation layer at the specified index.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.duplicate_anim_layer": "mutate",
  // Export an FBX file from a Control Rig section.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.export_fbx_from_rig": "mutate",
  // Add a Control Rig track to a binding using a Control Rig asset. This is the standard way to add a Control Rig 
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.find_or_create_track": "mutate",
  // Frame the viewport to the current Control Rig control selection.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.frame_selection": "mutate",
  // Get an actor's world transform at a specific frame. Finds the actor by name in the current editor world.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_actor_transform_at_frame": "read",
  // Get all animation layers from the active Sequencer.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_layers": "read",
  // Get the editor's transform gizmo size. Reads UTransformGizmoEditorSettings::TransformGizmoSize. The CR-specifi
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_gizmo_scale": "read",
  // Get whether Animation Mode hides all manipulators.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_hide_manips": "read",
  // Get whether the Animation Mode draws hierarchy lines/dots.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_hierarchy": "read",
  // Get whether multi-select transforms act in each control's own space.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_local_spaces": "read",
  // Get whether the Animation Mode draws nulls.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_nulls": "read",
  // Get whether Animation Mode restricts viewport selection to rig controls.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_only_rig_sel": "read",
  // Get a bool control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_bool": "read",
  // Get all Control Rigs currently in the sequence. Returns proxy objects with track and rig references.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_control_rigs": "read",
  // Get all controls on a Control Rig with their names and types. Returns a list of controls with name and type so
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_info": "read",
  // Check if a control is visible (unmasked) on a section.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_mask": "read",
  // Get an EulerTransform control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_euler_transform": "read",
  // Get a float control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_float": "read",
  // Get an integer control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_int": "read",
  // Get a position control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_position": "read",
  // Get the evaluation priority order of a Control Rig track.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_priority_order": "read",
  // Get a rotator control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_rotator": "read",
  // Get a scale control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_scale": "read",
  // Get the currently selected controls on a Control Rig.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_selected_controls": "read",
  // Get the transform value of a Control Rig control at a frame. Automatically detects whether the control is a Tr
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_transform": "read",
  // Get a Vector2D control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_vector2d": "read",
  // Get a control's world-space transform at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_world_transform": "read",
  // Hide all controls on a Control Rig section (mask everything).
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.hide_all_controls": "mutate",
  // Import an FBX file onto a Control Rig track.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.import_fbx_to_rig": "mutate",
  // Check if a Control Rig is an FK Control Rig.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.is_fk_control_rig": "read",
  // Check if a Control Rig in the sequence is in layered mode.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.is_layered_control_rig": "read",
  // Key the specified controls on the section at the current Sequencer time.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls": "mutate",
  // Key the specified controls at specific frame numbers.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls_at_frames": "mutate",
  // Load an animation sequence into a Control Rig section. Finds the skeletal mesh component from the binding asso
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.load_anim_into_rig": "mutate",
  // Merge specified animation layers into one. Merges onto the layer with the lowest index.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.merge_anim_layers": "mutate",
  // Apply a mirrored pose to the currently selected controls.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.mirror_selected_controls": "mutate",
  // Move a space-switch key from one frame to another.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.move_space": "mutate",
  // Move an animation layer from one index to another. Cannot move the base layer (index 0).
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.reorder_anim_layers": "mutate",
  // Select or deselect a control on a Control Rig.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.select_control": "mutate",
  // Select the mirrored counterparts of the currently selected controls. Replaces the current selection with mirro
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.select_mirrored_controls": "mutate",
  // Set the editor's transform gizmo size. Writes UTransformGizmoEditorSettings::TransformGizmoSize. The CR-specif
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_gizmo_scale": "mutate",
  // Toggle whether Animation Mode hides all manipulators.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_hide_manips": "mutate",
  // Toggle the Animation Mode hierarchy lines/dots display.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_hierarchy": "mutate",
  // Toggle multi-select transforms acting in each control's own space. When True, transforming multiple selected c
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_local_spaces": "mutate",
  // Toggle the Animation Mode nulls display.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_nulls": "mutate",
  // Toggle Animation Mode restricting viewport selection to rig controls.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_only_rig_sel": "mutate",
  // Set a bool control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_bool": "mutate",
  // Set the visibility mask for the specified controls on a section.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_controls_mask": "mutate",
  // Set an EulerTransform control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_euler_transform": "mutate",
  // Set a float control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_float": "mutate",
  // Set an integer control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_int": "mutate",
  // Set a Control Rig track to layered or absolute mode.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_layered_mode": "mutate",
  // Set a position control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_position": "mutate",
  // Set the evaluation priority order of a Control Rig track.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_priority_order": "mutate",
  // Set a rotator control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_rotator": "mutate",
  // Set a scale control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_scale": "mutate",
  // Set the space for a Control Rig control at a given frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_space": "mutate",
  // Set a transform value on a Control Rig control and optionally key it. Uses ControlRigSequencerLibrary.set_loca
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_transform": "mutate",
  // Set a Vector2D control value at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_vector2d": "mutate",
  // Set a control's world-space transform at a specific frame.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_world_transform": "mutate",
  // Show all controls on a Control Rig section (unmask everything).
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.show_all_controls": "mutate",
  // Snap Control Rig controls to a target actor over a frame range.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.snap_control_rig": "mutate",
  // Perform a tween operation on a Control Rig at the current Sequencer time. The tween blends between the previou
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.tween_control_rig": "mutate",
  // Reset Control Rig transforms to their default (usually zero) values.
  "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.zero_transforms": "mutate",

  /* ── animation_toolset.toolsets.controlrig.ControlRigTools ─────── */
  // Create a backward solve graph with InverseExecution event.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_backward_solve_graph": "mutate",
  // Add a bone to the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_bone": "mutate",
  // Add a control to the hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_control": "mutate",
  // Add a bone or null element to the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_element": "mutate",
  // Create a new graph with the specified event type.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_event_graph": "mutate",
  // Add an event node to the graph.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_event_node": "mutate",
  // Create a new empty graph in the Control Rig.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_graph": "mutate",
  // Create an interaction graph with InteractionExecution event.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_interaction_graph": "mutate",
  // Add a null (locator) to the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_null": "mutate",
  // Add a member variable to the Control Rig.
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_variable": "mutate",
  // Create a variable getter or setter node. The variable must already exist (created via add_variable first).
  "animation_toolset.toolsets.controlrig.ControlRigTools.add_variable_node": "mutate",
  // Change the type of an existing variable.
  "animation_toolset.toolsets.controlrig.ControlRigTools.change_variable_type": "mutate",
  // Connect two pins together.
  "animation_toolset.toolsets.controlrig.ControlRigTools.connect_pins": "mutate",
  // Creates a new Control Rig at the given location.
  "animation_toolset.toolsets.controlrig.ControlRigTools.create": "mutate",
  // Create a new RigUnit node in the graph.
  "animation_toolset.toolsets.controlrig.ControlRigTools.create_node": "mutate",
  // Delete a node from the graph.
  "animation_toolset.toolsets.controlrig.ControlRigTools.delete_node": "mutate",
  // Disconnect two pins.
  "animation_toolset.toolsets.controlrig.ControlRigTools.disconnect_pins": "mutate",
  // Get all bones in the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_bones": "read",
  // Get all controls in the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_controls": "read",
  // Get all nulls (locators) in the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_nulls": "read",
  // Get the backward solve graph. The backward solve graph contains the InverseExecution event and runs during IK 
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_backward_solve_graph": "read",
  // Get children of a hierarchy element.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_children": "read",
  // Get all pins connected to this pin.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_connected_pins": "read",
  // Get hierarchy elements of the specified type.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_elements": "read",
  // Get a graph containing the specified event type.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_event_graph": "read",
  // Get the forward solve graph (main execution graph). This graph contains the BeginExecution event and runs duri
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_forward_solve_graph": "read",
  // Get global transform of a hierarchy element.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_global_transform": "read",
  // Get a specific graph from the Control Rig by name.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_graph": "read",
  // Get the interaction graph. The interaction graph contains the InteractionExecution event and runs during user 
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_interaction_graph": "read",
  // Get local transform of a hierarchy element.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_local_transform": "read",
  // Get the position of a node in the graph editor.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_node_position": "read",
  // Get the parent of a hierarchy element.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_parent": "read",
  // Get the default value of a pin.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_pin_value": "read",
  // Get a specific variable by name.
  "animation_toolset.toolsets.controlrig.ControlRigTools.get_variable": "read",
  // Import bones from the given skeletal mesh to the Control Rig hierarchy.
  "animation_toolset.toolsets.controlrig.ControlRigTools.import_bones_from_asset": "mutate",
  // List all graphs in the Control Rig.
  "animation_toolset.toolsets.controlrig.ControlRigTools.list_graphs": "read",
  // List all nodes in a graph.
  "animation_toolset.toolsets.controlrig.ControlRigTools.list_nodes": "read",
  // List all pins on a node.
  "animation_toolset.toolsets.controlrig.ControlRigTools.list_pins": "read",
  // List all member variables in the Control Rig.
  "animation_toolset.toolsets.controlrig.ControlRigTools.list_variables": "read",
  // Remove a member variable from the Control Rig.
  "animation_toolset.toolsets.controlrig.ControlRigTools.remove_variable": "mutate",
  // Set global transform of a hierarchy element.
  "animation_toolset.toolsets.controlrig.ControlRigTools.set_global_transform": "mutate",
  // Set local transform of a hierarchy element.
  "animation_toolset.toolsets.controlrig.ControlRigTools.set_local_transform": "mutate",
  // Set the position of a node in the graph editor.
  "animation_toolset.toolsets.controlrig.ControlRigTools.set_node_position": "mutate",
  // Set the default value of a pin.
  "animation_toolset.toolsets.controlrig.ControlRigTools.set_pin_value": "mutate",

  /* ── animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools  */
  // Set the actor class for a spawnable or replaceable template.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.change_actor_template_class": "mutate",
  // Convert a binding to a custom binding type.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_custom_binding": "mutate",
  // Convert a spawnable binding to a possessable.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_possessable": "mutate",
  // Convert a possessable binding to a spawnable.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_spawnable": "mutate",
  // Get the custom binding instances for a binding.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_binding_objects": "read",
  // Get the custom binding class for a binding. Returns the class path of the custom binding type, or an empty str
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_binding_type": "read",
  // Find all bindings of a given custom type in the current sequence.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_bindings_of_type": "read",
  // Save the current state of a spawnable as its default.
  "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.save_default_spawnable_state": "mutate",

  /* ── animation_toolset.toolsets.import_export.SequencerImportExportTools  */
  // Export animation from a sequence binding to an AnimSequence asset.
  "animation_toolset.toolsets.import_export.SequencerImportExportTools.export_anim_sequence": "mutate",
  // Export a level sequence to FBX.
  "animation_toolset.toolsets.import_export.SequencerImportExportTools.export_fbx": "mutate",
  // Get content paths of all AnimSequences linked to a LevelSequence. Linked AnimSequences auto-update when the Le
  "animation_toolset.toolsets.import_export.SequencerImportExportTools.get_linked_anim_sequences": "read",
  // Get the content path of the LevelSequence linked to an AnimSequence.
  "animation_toolset.toolsets.import_export.SequencerImportExportTools.get_linked_level_sequence": "read",
  // Import FBX data into a level sequence.
  "animation_toolset.toolsets.import_export.SequencerImportExportTools.import_fbx": "mutate",
  // Link an AnimSequence asset to a level sequence binding. When the sequence is modified, the linked AnimSequence
  "animation_toolset.toolsets.import_export.SequencerImportExportTools.link_anim_sequence": "mutate",

  /* ── animation_toolset.toolsets.keyframing.SequencerKeyframingTools  */
  // Add a bool key to a channel on a section.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_bool": "mutate",
  // Add a float key to a channel on a section.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_float": "mutate",
  // Add an integer key to a channel on a section.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_integer": "mutate",
  // Add a string key to a channel on a section.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_string": "mutate",
  // Bake a channel's values over a frame range. Evaluates the channel curve at every frame in the range and return
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.bake_channel_keys": "mutate",
  // Close the Sequencer Curve Editor panel.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.close_curve_editor": "mutate",
  // Clear all key selection in the Curve Editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.curve_editor_empty_selection": "mutate",
  // Select keys by index in the Curve Editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.curve_editor_select_keys": "mutate",
  // Get the names of all channels on a section. For example, a 3D Transform section has channels named 'Location.X
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_channel_names": "read",
  // Get selected key indices for a channel in the Curve Editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_curve_editor_selected_keys": "read",
  // Get the default value of a float channel.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_default_value": "read",
  // Get all keys on a channel, returned as a JSON array. Each key entry includes its frame number and value.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys": "read",
  // Get specific keys on a channel by their indices, returned as JSON. Useful for resolving the keys behind a Curv
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys_by_index": "read",
  // Get the currently selected channels in the Sequencer editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_selected_channels": "read",
  // Get channels that have selected keys in the Curve Editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_selected_key_channels": "read",
  // Check whether the Curve Editor panel is currently open.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.is_curve_editor_open": "read",
  // Check if a curve is visible in the Curve Editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.is_curve_shown": "read",
  // Open the Sequencer Curve Editor panel.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.open_curve_editor": "mutate",
  // Remove a key at a specific frame from a channel.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.remove_key_at_frame": "mutate",
  // Set the channel selection in the Sequencer editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.select_channels": "mutate",
  // Set the default value of a channel.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.set_default_value": "mutate",
  // Show or hide a curve in the Curve Editor.
  "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.show_curve": "mutate",

  /* ── animation_toolset.toolsets.outliner.SequencerOutlinerTools ── */
  // Get the currently deactivated outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_deactivated_nodes": "read",
  // Get the currently locked outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_locked_nodes": "read",
  // Get the currently muted outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_muted_nodes": "read",
  // Get the display label of an outliner node.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_node_label": "read",
  // Get child nodes of an outliner node.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_children": "read",
  // Get the currently selected nodes in the outliner.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_selection": "read",
  // Get a full snapshot of the Sequencer outliner tree. Builds a recursive tree structure from the outliner root n
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_tree": "read",
  // Get the currently pinned outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_pinned_nodes": "read",
  // Get sections associated with the given outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_sections_for_nodes": "read",
  // Get the currently soloed outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_soloed_nodes": "read",
  // Check whether an outliner node is expanded.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.is_node_expanded": "read",
  // Deactivate or reactivate outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_deactivated": "mutate",
  // Expand or collapse outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_expanded": "mutate",
  // Lock or unlock outliner nodes for editing.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_locked": "mutate",
  // Mute or unmute outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_muted": "mutate",
  // Pin or unpin outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_pinned": "mutate",
  // Solo or unsolo outliner nodes.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_solo": "mutate",
  // Set the outliner selection.
  "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_outliner_selection": "mutate",

  /* ── animation_toolset.toolsets.sequencer.SequencerTools ───────── */
  // Add actors from the level to the currently open sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_actors": "mutate",
  // Add actors to the sequence by their names in the level. Finds actors by label in the current level and adds th
  "animation_toolset.toolsets.sequencer.SequencerTools.add_actors_by_name": "mutate",
  // Add actors to an existing binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_actors_to_binding": "mutate",
  // Add a binding into a folder for organization.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_binding_to_folder": "mutate",
  // Add an event repeater section to an event track.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_event_repeater_section": "mutate",
  // Add an event trigger section to an event track.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_event_trigger_section": "mutate",
  // Add a marked frame (bookmark) to the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_marked_frame": "mutate",
  // Create a new root-level folder in the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_root_folder": "mutate",
  // Add a new section to a track.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_section": "mutate",
  // Create a spawnable binding from an actor class.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_class": "mutate",
  // Create a spawnable binding from an existing object instance.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_instance": "mutate",
  // Add a track of the given type to a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_binding": "mutate",
  // Add a track into a folder for organization.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_folder": "mutate",
  // Add a sequence-level (master) track.
  "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_sequence": "mutate",
  // Bake transforms for the given bindings at every frame.
  "animation_toolset.toolsets.sequencer.SequencerTools.bake_transform": "mutate",
  // Close the currently open level sequence editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.close_sequence": "mutate",
  // Copy one or more bindings to the Sequencer clipboard. Returns a paste token that can be passed to paste_bindin
  "animation_toolset.toolsets.sequencer.SequencerTools.copy_bindings": "mutate",
  // Copy one or more folders to the Sequencer clipboard.
  "animation_toolset.toolsets.sequencer.SequencerTools.copy_folders": "mutate",
  // Copy one or more sections to the Sequencer clipboard.
  "animation_toolset.toolsets.sequencer.SequencerTools.copy_sections": "mutate",
  // Copy one or more tracks to the Sequencer clipboard.
  "animation_toolset.toolsets.sequencer.SequencerTools.copy_tracks": "mutate",
  // Create a new cine camera actor in the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.create_camera": "mutate",
  // Create a new Level Sequence asset. If an asset already exists at the given path, it will be deleted first to a
  "animation_toolset.toolsets.sequencer.SequencerTools.create_level_sequence": "mutate",
  // Delete all marked frames from the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.delete_all_marked_frames": "mutate",
  // Delete a marked frame by index.
  "animation_toolset.toolsets.sequencer.SequencerTools.delete_marked_frame": "mutate",
  // Clear all selection in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.empty_selection": "mutate",
  // Find a binding by its display name.
  "animation_toolset.toolsets.sequencer.SequencerTools.find_binding_by_name": "read",
  // Find the first binding with the given tag in the sequence. Tags are authored via tag_binding() in this toolset
  "animation_toolset.toolsets.sequencer.SequencerTools.find_binding_by_tag": "read",
  // Find all bindings with the given tag in the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.find_bindings_by_tag": "read",
  // Find a marked frame by label.
  "animation_toolset.toolsets.sequencer.SequencerTools.find_marked_frame_by_label": "read",
  // Find all tracks of a specific type on a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.find_tracks_by_type": "read",
  // Attempt to auto-fix broken actor references in the current sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.fix_actor_references": "mutate",
  // Navigate up one level in the sub-sequence hierarchy.
  "animation_toolset.toolsets.sequencer.SequencerTools.focus_parent_sequence": "mutate",
  // Navigate into a sub-sequence via its sub-section. Use get_sections() on a sub-track to find the sub-section, t
  "animation_toolset.toolsets.sequencer.SequencerTools.focus_sub_sequence": "mutate",
  // Force the Sequencer to evaluate and update the viewport.
  "animation_toolset.toolsets.sequencer.SequencerTools.force_evaluate": "mutate",
  // Get every tag name registered in the sequence. Uses the MovieSceneBindingTagExtensions C++ library.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_all_binding_tags": "read",
  // Get the binding ID for a binding proxy. The binding ID can be used with get_bound_objects to resolve what acto
  "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_id": "read",
  // Get the display name of a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_name": "read",
  // Get the tags currently attached to a specific binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_tags": "read",
  // Get all bindings in the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_bindings": "read",
  // Get the objects currently resolved by a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_bound_objects": "read",
  // Get component bindings under an actor binding. Actor bindings can own child possessable bindings for their com
  "animation_toolset.toolsets.sequencer.SequencerTools.get_child_possessables": "read",
  // Get the clock source for the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_clock_source": "read",
  // Get the root level sequence currently open in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_current_sequence": "read",
  // Get the display frame rate of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_display_rate": "read",
  // Get the evaluation type of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_evaluation_type": "read",
  // Get the currently focused level sequence in the hierarchy. When navigated into a sub-sequence, this returns th
  "animation_toolset.toolsets.sequencer.SequencerTools.get_focused_sequence": "read",
  // Get the tracks and bindings inside a folder.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_folder_contents": "read",
  // Get the current loop playback mode.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_loop_mode": "read",
  // Get all marked frames (bookmarks) in the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_marked_frames": "read",
  // Get the playback start and end frames of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_playback_range": "read",
  // Get the current playback speed multiplier.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_playback_speed": "read",
  // Get the current playhead position in display rate frames.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_playhead_frame": "read",
  // Get all root-level folders in the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_root_folders": "read",
  // Get the blend type of a section.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_blend_type": "read",
  // Get the completion mode of a section.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_completion_mode": "read",
  // Get the effective ease-in duration of a section in frames.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_ease_in": "read",
  // Get the effective ease-out duration of a section in frames.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_ease_out": "read",
  // Get the number of post-roll frames configured on a section.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_post_roll_frames": "read",
  // Get the number of pre-roll frames configured on a section.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_pre_roll_frames": "read",
  // Get all common properties of a section in a single call. Useful for testing and verification. Returns range, e
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_properties": "read",
  // Get the frame range of a section.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_range": "read",
  // Get the active section that receives new keys on a track. When keying properties, Sequencer writes to a specif
  "animation_toolset.toolsets.sequencer.SequencerTools.get_section_to_key": "read",
  // Get all sections on a track.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_sections": "read",
  // Get the currently selected bindings in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_bindings": "read",
  // Get the currently selected folders in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_folders": "read",
  // Get the currently selected sections in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_sections": "read",
  // Get the currently selected tracks in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_tracks": "read",
  // Get the selection range (green bar) start and end frames.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_selection_range": "read",
  // Check whether the current level sequence is locked.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_sequence_lock_state": "read",
  // Get the current sub-sequence hierarchy path. Returns a list of sub-sections from the root down to the currentl
  "animation_toolset.toolsets.sequencer.SequencerTools.get_sub_sequence_hierarchy": "read",
  // Get the internal tick resolution of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_tick_resolution": "read",
  // Get the display name of a track.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_track_display_name": "read",
  // Get all available track filter names.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_track_filter_names": "read",
  // Get all tracks on a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_binding": "read",
  // Get all sequence-level (master) tracks.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_sequence": "read",
  // Get the visible time range in the Sequencer timeline.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_view_range": "read",
  // Get the work range of the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.get_work_range": "read",
  // Check if a section has a bounded end frame (vs infinite).
  "animation_toolset.toolsets.sequencer.SequencerTools.has_section_end_frame": "read",
  // Check if a section has a bounded start frame (vs infinite).
  "animation_toolset.toolsets.sequencer.SequencerTools.has_section_start_frame": "read",
  // Check if the camera cut is locked to the viewport.
  "animation_toolset.toolsets.sequencer.SequencerTools.is_camera_cut_locked": "read",
  // Check if the playback range is locked.
  "animation_toolset.toolsets.sequencer.SequencerTools.is_playback_range_locked": "read",
  // Check whether the sequence is currently playing.
  "animation_toolset.toolsets.sequencer.SequencerTools.is_playing": "read",
  // Check if the current sequence and its descendants are locked.
  "animation_toolset.toolsets.sequencer.SequencerTools.is_sequence_locked": "read",
  // Check whether a track filter is currently active.
  "animation_toolset.toolsets.sequencer.SequencerTools.is_track_filter_active": "read",
  // Open a level sequence asset in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.open_sequence": "mutate",
  // Paste bindings from the clipboard (or a token returned by copy_bindings).
  "animation_toolset.toolsets.sequencer.SequencerTools.paste_bindings": "mutate",
  // Paste folders from the clipboard into the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.paste_folders": "mutate",
  // Paste sections from the clipboard onto the given tracks.
  "animation_toolset.toolsets.sequencer.SequencerTools.paste_sections": "mutate",
  // Paste tracks from the clipboard onto the given bindings.
  "animation_toolset.toolsets.sequencer.SequencerTools.paste_tracks": "mutate",
  // Pause playback of the current sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.pause": "mutate",
  // Start playback of the current sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.play": "mutate",
  // Play from the current position to a specific frame, then stop.
  "animation_toolset.toolsets.sequencer.SequencerTools.play_to": "mutate",
  // Rebind component bindings to a named component.
  "animation_toolset.toolsets.sequencer.SequencerTools.rebind_component": "mutate",
  // Force refresh the Sequencer editor UI on the next tick.
  "animation_toolset.toolsets.sequencer.SequencerTools.refresh_sequence": "mutate",
  // Remove specific actors from a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_actors_from_binding": "mutate",
  // Remove all bound actors from a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_all_bindings": "mutate",
  // Remove a binding from the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_binding": "mutate",
  // Remove a tag from the sequence entirely. Clears the tag from every binding that had it and unregisters the tag
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_binding_tag": "mutate",
  // Remove missing or broken actor references from a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_invalid_bindings": "mutate",
  // Remove a root-level folder from the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_root_folder": "mutate",
  // Remove a section from a track.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_section": "mutate",
  // Remove a track from a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_track": "mutate",
  // Remove a sequence-level (master) track.
  "animation_toolset.toolsets.sequencer.SequencerTools.remove_track_from_sequence": "mutate",
  // Replace all bound actors on a binding with new ones.
  "animation_toolset.toolsets.sequencer.SequencerTools.replace_binding_with_actors": "mutate",
  // Set the binding selection in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.select_bindings": "mutate",
  // Set the folder selection in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.select_folders": "mutate",
  // Set the section selection in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.select_sections": "mutate",
  // Set the track selection in the Sequencer editor.
  "animation_toolset.toolsets.sequencer.SequencerTools.select_tracks": "mutate",
  // Set the display name of a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_binding_name": "mutate",
  // Configure a byte track to use a specific enum type. Byte tracks can animate enum properties. Call this after a
  "animation_toolset.toolsets.sequencer.SequencerTools.set_byte_track_enum": "mutate",
  // Set which camera a camera cut section uses.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_camera_cut_binding": "mutate",
  // Lock or unlock the camera cut to the viewport.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_camera_lock": "mutate",
  // Set the clock source for the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_clock_source": "mutate",
  // Set the display frame rate of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_display_rate": "mutate",
  // Set the evaluation type of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_evaluation_type": "mutate",
  // Enable or disable loop playback.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_loop_mode": "mutate",
  // Set the playback start and end frames of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_range": "mutate",
  // Lock or unlock the playback range.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_range_locked": "mutate",
  // Set the playback speed multiplier.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_speed": "mutate",
  // Set the playhead position in display rate frames.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_playhead_frame": "mutate",
  // Configure a property track to animate a specific UProperty. This binds a generic property track (Float, Bool, 
  "animation_toolset.toolsets.sequencer.SequencerTools.set_property_name_and_path": "mutate",
  // Set the animation asset on a skeletal animation section. After adding a MovieSceneSkeletalAnimationTrack and s
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_animation": "mutate",
  // Set the blend type of a section. Valid values: 'Absolute', 'Additive', 'Relative', 'Override'.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_blend_type": "mutate",
  // Set the completion mode of a section. Valid values: 'KeepState', 'RestoreState', 'ProjectDefault'.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_completion_mode": "mutate",
  // Set the ease-in duration of a section in frames. Enables manual ease override if not already active.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_in": "mutate",
  // Set the ease-out duration of a section in frames. Enables manual ease override if not already active.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_out": "mutate",
  // Set whether the section end frame is bounded or infinite.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_end_bounded": "mutate",
  // Set the number of frames to post-roll this section after it ends. Post-roll continues evaluation after the sec
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_post_roll_frames": "mutate",
  // Set the number of frames to pre-roll this section before it starts. Pre-roll evaluates the section before its 
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_pre_roll_frames": "mutate",
  // Set the frame range of a section.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_range": "mutate",
  // Set whether the section start frame is bounded or infinite.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_section_start_bounded": "mutate",
  // Set the selection range (green bar) start and end frames.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_selection_range": "mutate",
  // Lock or unlock the current sequence and its descendants.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_sequence_locked": "mutate",
  // Set the internal tick resolution of a sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_tick_resolution": "mutate",
  // Set the display name of a track.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_track_display_name": "mutate",
  // Enable or disable a track filter.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_track_filter_active": "mutate",
  // Set the visible time range in the Sequencer timeline.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_view_range": "mutate",
  // Set the work range of the sequence.
  "animation_toolset.toolsets.sequencer.SequencerTools.set_work_range": "mutate",
  // Attach a tag to a binding. If the tag has not been seen in the sequence before, it is automatically registered
  "animation_toolset.toolsets.sequencer.SequencerTools.tag_binding": "mutate",
  // Remove a tag from a binding.
  "animation_toolset.toolsets.sequencer.SequencerTools.untag_binding": "mutate",

  /* ── AutomationTestToolset.AutomationTestToolset ───────────────── */
  // Initialize automation worker discovery and load the test list. Must be called once before ListTests or RunTest
  "AutomationTestToolset.AutomationTestToolset.DiscoverTests": "mutate",
  // Get detailed results for the current or most recent test run. Requires DiscoverTests() to have completed. Retu
  "AutomationTestToolset.AutomationTestToolset.GetTestResults": "read",
  // Get a lightweight status snapshot of the automation controller. Requires DiscoverTests() to have completed. Re
  "AutomationTestToolset.AutomationTestToolset.GetTestStatus": "read",
  // List available automation tests. Requires DiscoverTests() to have completed. Returns a JSON object: {"tests": 
  "AutomationTestToolset.AutomationTestToolset.ListTests": "read",
  // Run a set of automation tests by name. Requires DiscoverTests() to have completed. Starts executing the specif
  "AutomationTestToolset.AutomationTestToolset.RunTests": "mutate",
  // Run automation tests selected by a filter expression. Requires DiscoverTests() to have completed. Much faster 
  "AutomationTestToolset.AutomationTestToolset.RunTestsByFilter": "mutate",
  // Stop all currently running tests. Requires DiscoverTests() to have completed. If a RunTests async result is pe
  "AutomationTestToolset.AutomationTestToolset.StopTests": "mutate",

  /* ── ConfigSettingsToolset.ConfigSettingsToolset ───────────────── */
  // Returns the current values of the specified properties as a JSON object. Raises an error if the section does n
  "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionPropertyValues": "read",
  // Returns a JSON Schema describing the user-visible properties of a settings section. The schema maps each prope
  "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionSchema": "read",
  // Lists the names of all categories within a settings container, sorted alphabetically. Raises an error if the c
  "ConfigSettingsToolset.ConfigSettingsToolset.ListCategories": "read",
  // Lists the names of all known settings containers, sorted alphabetically. Common containers are "Editor" and "P
  "ConfigSettingsToolset.ConfigSettingsToolset.ListContainers": "read",
  // Lists the names of all sections within a settings category, sorted alphabetically. Raises an error if the cont
  "ConfigSettingsToolset.ConfigSettingsToolset.ListSections": "read",
  // Resets the settings in a section to their default values. Raises an error if the section does not exist or res
  "ConfigSettingsToolset.ConfigSettingsToolset.ResetSectionToDefaults": "mutate",
  // Saves the settings in a section. Raises an error if the section does not exist or saving is not supported.
  "ConfigSettingsToolset.ConfigSettingsToolset.SaveSection": "mutate",
  // Sets one or more properties on a settings section from a JSON object and saves. PropertiesJson must be a JSON 
  "ConfigSettingsToolset.ConfigSettingsToolset.SetSectionProperties": "mutate",

  /* ── conversation_toolset.toolsets.conversation.ConversationTools  */
  // Returns all reachable nodes in the conversation. Use ObjectTools.get_class and get_properties on each node to 
  "conversation_toolset.toolsets.conversation.ConversationTools.get_all_nodes": "read",
  // Returns a conversation node by its GUID.
  "conversation_toolset.toolsets.conversation.ConversationTools.get_node_by_guid": "read",
  // Returns output connection GUIDs for a conversation node.
  "conversation_toolset.toolsets.conversation.ConversationTools.get_node_connections": "read",
  // Returns GUIDs of all reachable nodes, in map iteration order. Use with get_node_by_guid to look up specific no
  "conversation_toolset.toolsets.conversation.ConversationTools.get_node_guids": "read",
  // Returns sub-nodes (requirements, choices) attached to a task node.
  "conversation_toolset.toolsets.conversation.ConversationTools.get_sub_nodes": "read",
  // Returns entry points (FConversationEntryList structs).
  "conversation_toolset.toolsets.conversation.ConversationTools.list_entry_points": "read",
  // Returns speaker/participant information.
  "conversation_toolset.toolsets.conversation.ConversationTools.list_speakers": "read",

  /* ── DataflowAgent.DataflowAgentToolset ────────────────────────── */
  // Adds a comment box around the given nodes.
  "DataflowAgent.DataflowAgentToolset.AddCommentBox": "mutate",
  // Adds a node of the given type to the Dataflow graph.
  "DataflowAgent.DataflowAgentToolset.AddNode": "mutate",
  // Adds a new variable to the Dataflow graph. Supported type strings: Primitives : "Bool", "Int32", "Int64", "Flo
  "DataflowAgent.DataflowAgentToolset.AddVariable": "mutate",
  // Assigns a Dataflow template to an existing Dataflow-compatible asset by duplicating the template graph and emb
  "DataflowAgent.DataflowAgentToolset.AssignDataflowTemplate": "mutate",
  // Connects an output pin of one node to an input pin of another.
  "DataflowAgent.DataflowAgentToolset.ConnectNodePins": "mutate",
  // Creates a new Dataflow-compatible asset (e.g. ChaosClothAsset, GeometryCollection, FleshAsset, GroomAsset) wit
  "DataflowAgent.DataflowAgentToolset.CreateDataflowCompatibleAsset": "mutate",
  // Creates a new Dataflow-compatible asset and initialises its embedded Dataflow graph from a registered template
  "DataflowAgent.DataflowAgentToolset.CreateDataflowCompatibleAssetFromTemplate": "mutate",
  // Creates a new saved Dataflow graph asset.
  "DataflowAgent.DataflowAgentToolset.CreateGraph": "mutate",
  // Removes the connection between two node pins.
  "DataflowAgent.DataflowAgentToolset.DisconnectNodePins": "mutate",
  // Returns the complete structure of a Dataflow graph including all nodes and connections.
  "DataflowAgent.DataflowAgentToolset.GetGraphStructure": "read",
  // Returns information about a node as a JSON object (name, type, position, pins).
  "DataflowAgent.DataflowAgentToolset.GetNodeInfo": "read",
  // Returns the schema for a Dataflow node type including its input/output pins and editable UPROPERTY parameters.
  "DataflowAgent.DataflowAgentToolset.GetNodeTypeSchema": "read",
  // Returns a JSON list of every UClass that can host an embedded Dataflow graph (i.e. implements IDataflowInstanc
  "DataflowAgent.DataflowAgentToolset.ListDataflowCompatibleAssetTypes": "read",
  // Returns a JSON list of Dataflow templates registered for the given asset class. Templates registered for paren
  "DataflowAgent.DataflowAgentToolset.ListDataflowTemplatesForAssetClass": "read",
  // Returns a JSON list of all registered Dataflow node types.
  "DataflowAgent.DataflowAgentToolset.ListNodeTypes": "read",
  // Returns all variables defined on the Dataflow graph as a JSON array. Each entry contains "name", "type", and "
  "DataflowAgent.DataflowAgentToolset.ListVariables": "read",
  // Removes a comment box node from the graph.
  "DataflowAgent.DataflowAgentToolset.RemoveCommentBox": "mutate",
  // Removes a node and all its connections from the Dataflow graph.
  "DataflowAgent.DataflowAgentToolset.RemoveNode": "mutate",
  // Removes a variable from the Dataflow graph.
  "DataflowAgent.DataflowAgentToolset.RemoveVariable": "mutate",
  // Moves a node to a new position in the graph editor.
  "DataflowAgent.DataflowAgentToolset.RepositionNode": "mutate",
  // Sets the value of an existing variable using its serialized string representation. The format depends on the v
  "DataflowAgent.DataflowAgentToolset.SetVariable": "mutate",
  // Updates an existing node's editable properties via JSON.
  "DataflowAgent.DataflowAgentToolset.UpdateNode": "mutate",

  /* ── DataRegistryToolset.DataRegistryTools ─────────────────────── */
  // Returns cached item data. Items must be loaded in the registry cache to be returned.
  "DataRegistryToolset.DataRegistryTools.GetItems": "read",
  // Returns detailed information about a specific registry.
  "DataRegistryToolset.DataRegistryTools.GetRegistryInfo": "read",
  // Returns the item struct schema as JSON.
  "DataRegistryToolset.DataRegistryTools.GetSchema": "read",
  // Returns the editor-defined sources configured on a Data Registry. These are the sources as authored on the reg
  "DataRegistryToolset.DataRegistryTools.ListDataSources": "read",
  // Returns all item names in a Data Registry.
  "DataRegistryToolset.DataRegistryTools.ListItems": "read",
  // Returns the names of all registered Data Registries.
  "DataRegistryToolset.DataRegistryTools.ListRegistries": "read",
  // Returns the runtime sources for a Data Registry. This is the expanded list including transient child sources g
  "DataRegistryToolset.DataRegistryTools.ListRuntimeSources": "read",

  /* ── editor_toolset.toolsets.actor.ActorTools ──────────────────── */
  // Adds a component to an actor instance or blueprint.
  "editor_toolset.toolsets.actor.ActorTools.add_component": "mutate",
  // Adds a tag to an actor.
  "editor_toolset.toolsets.actor.ActorTools.add_tag": "mutate",
  // Returns the bounding box of an actor.
  "editor_toolset.toolsets.actor.ActorTools.get_actor_bounds": "read",
  // Returns the position, rotation, and scale of an actor.
  "editor_toolset.toolsets.actor.ActorTools.get_actor_transform": "read",
  // Returns the actor that owns the specified component.
  "editor_toolset.toolsets.actor.ActorTools.get_component_actor": "read",
  // Returns the components that an actor contains.
  "editor_toolset.toolsets.actor.ActorTools.get_components": "read",
  // Returns the actor's human friendly name as it appears in the editor.
  "editor_toolset.toolsets.actor.ActorTools.get_label": "read",
  // Returns the parent component that this component is attached to, if any.
  "editor_toolset.toolsets.actor.ActorTools.get_parent_component": "read",
  // Returns the root component of an actor, if any.
  "editor_toolset.toolsets.actor.ActorTools.get_root_component": "read",
  // Returns the list of tags on an actor.
  "editor_toolset.toolsets.actor.ActorTools.get_tags": "read",
  // Returns whether an actor has a specific tag.
  "editor_toolset.toolsets.actor.ActorTools.has_tag": "read",
  // Rotates an actor so its forward vector points at a world-space position.
  "editor_toolset.toolsets.actor.ActorTools.look_at": "mutate",
  // Removes a component from an actor instance or blueprint.
  "editor_toolset.toolsets.actor.ActorTools.remove_component": "mutate",
  // Removes a tag from an actor.
  "editor_toolset.toolsets.actor.ActorTools.remove_tag": "mutate",
  // Updates the position, rotation, and/or scale of an actor.
  "editor_toolset.toolsets.actor.ActorTools.set_actor_transform": "mutate",
  // Sets the human-friendly name of the actor.
  "editor_toolset.toolsets.actor.ActorTools.set_label": "mutate",
  // Sets the parent for the specified scene component. For blueprint actors, passing a component as the parent of 
  "editor_toolset.toolsets.actor.ActorTools.set_parent_component": "mutate",

  /* ── editor_toolset.toolsets.asset.AssetTools ──────────────────── */
  // Checks whether an asset can be edited.
  "editor_toolset.toolsets.asset.AssetTools.can_edit_asset": "read",
  // Creates a folder at the specified path.
  "editor_toolset.toolsets.asset.AssetTools.create_folder": "mutate",
  // Deletes an asset or folder.
  "editor_toolset.toolsets.asset.AssetTools.delete": "mutate",
  // Makes a copy of a folder or asset.
  "editor_toolset.toolsets.asset.AssetTools.duplicate": "mutate",
  // Determines if a folder or asset exists.
  "editor_toolset.toolsets.asset.AssetTools.exists": "read",
  // Searches the project for assets that match specific criteria.
  "editor_toolset.toolsets.asset.AssetTools.find_assets": "read",
  // Gets the class of an asset.
  "editor_toolset.toolsets.asset.AssetTools.get_asset_class": "read",
  // Gets the asset registry tags for an asset.
  "editor_toolset.toolsets.asset.AssetTools.get_asset_tags": "read",
  // Lists assets that the specified asset depends on.
  "editor_toolset.toolsets.asset.AssetTools.get_dependencies": "read",
  // Gets the metadata tags for an asset.
  "editor_toolset.toolsets.asset.AssetTools.get_metadata_tags": "read",
  // Returns the root content paths for plugins that have content.
  "editor_toolset.toolsets.asset.AssetTools.get_plugin_content_paths": "read",
  // Lists assets that reference the specified asset.
  "editor_toolset.toolsets.asset.AssetTools.get_referencers": "read",
  // Checks whether an asset is checked out by the current user.
  "editor_toolset.toolsets.asset.AssetTools.is_checked_out": "read",
  // Checks whether an asset has unsaved changes.
  "editor_toolset.toolsets.asset.AssetTools.is_dirty": "read",
  // Lists the folders contained within a folder.
  "editor_toolset.toolsets.asset.AssetTools.list_folders": "read",
  // Loads an asset from the project.
  "editor_toolset.toolsets.asset.AssetTools.load_asset": "mutate",
  // Moves or renames an asset or folder.
  "editor_toolset.toolsets.asset.AssetTools.move": "mutate",
  // Reads a text file from disk and returns its contents. Only files under /Game/, an enabled plugin's Content/ di
  "editor_toolset.toolsets.asset.AssetTools.read_file": "read",
  // Saves assets to disk.
  "editor_toolset.toolsets.asset.AssetTools.save_assets": "mutate",
  // Sets or removes metadata tags on an asset.
  "editor_toolset.toolsets.asset.AssetTools.update_metadata_tags": "mutate",
  // Writes text content to a file on disk. Only files under /Game/, an enabled plugin's Content/ directory, or the
  "editor_toolset.toolsets.asset.AssetTools.write_file": "mutate",

  /* ── editor_toolset.toolsets.blueprint.BlueprintTools ──────────── */
  // Creates a component bound event node in the event graph.
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_component_bound_event": "mutate",
  // Adds an event node to the Blueprint's event graph. If event_name matches an inherited overridable event, the n
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_event": "mutate",
  // Adds an event dispatcher to a Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_event_dispatcher": "mutate",
  // Adds a function graph to the Blueprint. If graph_name matches an inherited overridable function, the new graph
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_function_graph": "mutate",
  // Adds an input or output to a function or event dispatcher
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_function_param": "mutate",
  // Adds a pin to a node that supports dynamic pin addition. Works for Switch nodes (adds one case pin), Sequence 
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_node_pin": "mutate",
  // Adds an object reference input or output to a function or event dispatcher.
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_object_function_param": "mutate",
  // Adds a member or local variable that holds an object reference to a Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_object_variable": "mutate",
  // Adds a struct input or output to a function or event dispatcher.
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_struct_function_param": "mutate",
  // Adds a member or local variable of a struct type to a Blueprint. Use this to add variables of any UStruct type
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_struct_variable": "mutate",
  // Adds a member or local variable to a Blueprint. Supported type names: Primitives: 'bool', 'int', 'float', 'byt
  "editor_toolset.toolsets.blueprint.BlueprintTools.add_variable": "mutate",
  // Arranges a list of nodes in a readable left-to-right layout. Organizes nodes into columns based on data/execut
  "editor_toolset.toolsets.blueprint.BlueprintTools.arrange_nodes": "mutate",
  // Breaks the connection between two pins.
  "editor_toolset.toolsets.blueprint.BlueprintTools.break_pins": "mutate",
  // Compiles the given Blueprint. Blueprints should be compiled after all graph modifications are complete.
  "editor_toolset.toolsets.blueprint.BlueprintTools.compile_blueprint": "mutate",
  // Makes a connection between source (output) and dest (input) pins.
  "editor_toolset.toolsets.blueprint.BlueprintTools.connect_pins": "mutate",
  // Creates a new Blueprint asset in the project.
  "editor_toolset.toolsets.blueprint.BlueprintTools.create": "mutate",
  // Adds a new node to the graph.
  "editor_toolset.toolsets.blueprint.BlueprintTools.create_node": "mutate",
  // Deletes the node from its graph.
  "editor_toolset.toolsets.blueprint.BlueprintTools.delete_node": "mutate",
  // Retrieves a list of available node categories in a Blueprint graph, optionally filtered by compatible input an
  "editor_toolset.toolsets.blueprint.BlueprintTools.find_node_categories": "read",
  // Finds node types that can be created in a particular graph meeting the search criteria.
  "editor_toolset.toolsets.blueprint.BlueprintTools.find_node_types": "read",
  // Finds nodes in a graph by title, class, and/or execution role. All filters are optional and ANDed together. Us
  "editor_toolset.toolsets.blueprint.BlueprintTools.find_nodes": "read",
  // Returns detailed information for all nodes connected to the given node. Use this alongside find_nodes to read 
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_connected_subgraph": "read",
  // Returns the function currently bound to a Create Event node.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_create_event_function": "read",
  // Returns the Class Default Object (CDO) for a Blueprint's. ObjectTools list/set/get property will get the CDO a
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_default_object": "read",
  // Retrieves a specific graph from a Blueprint asset by name.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_graph": "read",
  // Returns the full syntax reference for write_graph_dsl. Call this before using write_graph_dsl for the first ti
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_graph_dsl_docs": "read",
  // Retrieves detailed information for a list of Blueprint graph nodes.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_node_infos": "read",
  // Returns the pin names and types for a node type.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_node_type_pins": "read",
  // Returns the parent class of a Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_parent": "read",
  // Gets the value of a Blueprint graph pin.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_pin_value": "read",
  // Gets the user-defined category of a Blueprint member variable. Categories group variables in the My Blueprint 
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_variable_category": "read",
  // Gets the replication mode of a Blueprint member variable.
  "editor_toolset.toolsets.blueprint.BlueprintTools.get_variable_replication": "read",
  // Lists functions that can be bound to a Create Event node.
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_compatible_event_functions": "read",
  // Lists the bindable delegate events available on a component.
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_component_events": "read",
  // Lists all event dispatchers defined on a Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_event_dispatchers": "read",
  // Lists all events visible on the Blueprint - locally defined custom events plus inheritable events from the par
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_events": "read",
  // Lists all functions visible on the Blueprint - locally defined plus inheritable ones from the parent class cha
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_functions": "read",
  // Lists all graphs in the Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_graphs": "read",
  // Lists member or local variables defined on a Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.list_variables": "read",
  // Reads a Blueprint graph and returns a DSL script. The returned code uses the same syntax that write_graph_dsl 
  "editor_toolset.toolsets.blueprint.BlueprintTools.read_graph_dsl": "read",
  // Removes a function graph or event dispatcher from the Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.remove_function_graph": "mutate",
  // Removes an input or output from a function or event dispatcher.
  "editor_toolset.toolsets.blueprint.BlueprintTools.remove_function_param": "mutate",
  // Removes a specific pin from a node that supports dynamic pin removal. Works for Switch nodes (removes one case
  "editor_toolset.toolsets.blueprint.BlueprintTools.remove_node_pin": "mutate",
  // Removes a member or local variable from a Blueprint.
  "editor_toolset.toolsets.blueprint.BlueprintTools.remove_variable": "mutate",
  // Replaces a node's baked-in class reference from old_class to new_class in place. If the node's current class r
  "editor_toolset.toolsets.blueprint.BlueprintTools.retarget_node_class": "mutate",
  // Binds a function to a Create Event node. Use list_compatible_event_functions to find valid function names.
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_create_event_function": "mutate",
  // Sets a new position for the node.
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_node_position": "mutate",
  // Reparents a Blueprint to a new parent class. The Blueprint must be recompiled after reparenting.
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_parent": "mutate",
  // Sets the value of a Blueprint graph pin.
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_pin_value": "mutate",
  // Sets the user-defined category on a Blueprint member variable. Categories group variables in the My Blueprint 
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_category": "mutate",
  // Sets whether a member variable is editable per-instance on actors placed in the level.
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_instance_editable": "mutate",
  // Sets the replication mode on a Blueprint member variable. RepNotify will automatically create an OnRep_ functi
  "editor_toolset.toolsets.blueprint.BlueprintTools.set_variable_replication": "mutate",
  // Populates a Blueprint graph with nodes from a DSL script and compiles the Blueprint. Call get_graph_dsl_docs()
  "editor_toolset.toolsets.blueprint.BlueprintTools.write_graph_dsl": "mutate",

  /* ── editor_toolset.toolsets.curve_table.CurveTableTools ───────── */
  // Adds a key to a row.
  "editor_toolset.toolsets.curve_table.CurveTableTools.add_key": "mutate",
  // Adds a new row to the curve table with an optional default value.
  "editor_toolset.toolsets.curve_table.CurveTableTools.add_row": "mutate",
  // Creates a new CurveTable asset.
  "editor_toolset.toolsets.curve_table.CurveTableTools.create": "mutate",
  // Returns all keys for a row.
  "editor_toolset.toolsets.curve_table.CurveTableTools.get_keys": "read",
  // Imports a file from disk as a CurveTable asset. The file's first column is the row name; subsequent columns ar
  "editor_toolset.toolsets.curve_table.CurveTableTools.import_file": "mutate",
  // Lists the names of all rows in the curve table.
  "editor_toolset.toolsets.curve_table.CurveTableTools.list_rows": "read",
  // Removes a row from the curve table.
  "editor_toolset.toolsets.curve_table.CurveTableTools.remove_row": "mutate",
  // Renames a row in the curve table.
  "editor_toolset.toolsets.curve_table.CurveTableTools.rename_row": "mutate",
  // Replaces all keys in a row with the provided list.
  "editor_toolset.toolsets.curve_table.CurveTableTools.set_keys": "mutate",

  /* ── editor_toolset.toolsets.data_asset.DataAssetTools ─────────── */
  // Creates a new DataAsset asset in the project.
  "editor_toolset.toolsets.data_asset.DataAssetTools.create": "mutate",

  /* ── editor_toolset.toolsets.data_table.DataTableTools ─────────── */
  // Adds new rows with default values to the data table.
  "editor_toolset.toolsets.data_table.DataTableTools.add_rows": "mutate",
  // Creates a new DataTable asset with the specified column schema.
  "editor_toolset.toolsets.data_table.DataTableTools.create": "mutate",
  // Returns the column values for one or more rows as a JSON string.
  "editor_toolset.toolsets.data_table.DataTableTools.get_rows": "read",
  // Returns the column schema of the data table as a JSON string.
  "editor_toolset.toolsets.data_table.DataTableTools.get_schema": "read",
  // Imports a file from disk as a DataTable asset. The file's columns must match the property names in schema. Use
  "editor_toolset.toolsets.data_table.DataTableTools.import_file": "mutate",
  // Lists the names of all rows in the data table.
  "editor_toolset.toolsets.data_table.DataTableTools.list_rows": "read",
  // Removes rows from the data table.
  "editor_toolset.toolsets.data_table.DataTableTools.remove_rows": "mutate",
  // Renames one or more rows in the data table.
  "editor_toolset.toolsets.data_table.DataTableTools.rename_rows": "mutate",
  // Finds structs that can be used as a DataTable schema.
  "editor_toolset.toolsets.data_table.DataTableTools.search_row_structs": "read",
  // Sets column values for one or more rows.
  "editor_toolset.toolsets.data_table.DataTableTools.set_rows": "mutate",

  /* ── editor_toolset.toolsets.material_instance.MaterialInstanceTools  */
  // Clears all parameter overrides on a material instance, reverting to parent defaults.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.clear_parameters": "mutate",
  // Creates a new MaterialInstanceConstant asset derived from a parent material. Material instances expose the par
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.create": "mutate",
  // Gets the current value of a scalar parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_scalar_parameter": "read",
  // Gets the value of a static switch parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_static_switch_parameter": "read",
  // Gets the texture assigned to a texture parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_texture_parameter": "read",
  // Gets the current value of a vector parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_vector_parameter": "read",
  // Returns all parameters exposed by a material or instance, with their names and types.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.list_parameters": "read",
  // Enables or disables a parameter override on a material instance. Enabling sets the override to the current eff
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parameter_override": "mutate",
  // Changes the parent of a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parent": "mutate",
  // Sets the value of a scalar parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_scalar_parameter": "mutate",
  // Sets the value of a static switch parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_static_switch_parameter": "mutate",
  // Assigns a texture to a texture parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_texture_parameter": "mutate",
  // Sets the value of a vector parameter on a material instance.
  "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_vector_parameter": "mutate",

  /* ── editor_toolset.toolsets.material.MaterialTools ────────────── */
  // Adds a new expression node to a Material or MaterialFunction graph. Use list_expression_classes to discover av
  "editor_toolset.toolsets.material.MaterialTools.add_expression": "mutate",
  // Connects an expression node's output pin to another expression node's input pin.
  "editor_toolset.toolsets.material.MaterialTools.connect_expressions": "mutate",
  // Connects an expression node's output to one of the material's output properties.
  "editor_toolset.toolsets.material.MaterialTools.connect_to_output": "mutate",
  // Creates a new empty MaterialFunction asset.
  "editor_toolset.toolsets.material.MaterialTools.create_function": "mutate",
  // Creates a new empty Material asset. Warning: Each new Material increases shader compile times. Prefer creating
  "editor_toolset.toolsets.material.MaterialTools.create_material": "mutate",
  // Creates a new empty MaterialParameterCollection (MPC) asset. An MPC holds named Scalar and Vector parameters w
  "editor_toolset.toolsets.material.MaterialTools.create_parameter_collection": "mutate",
  // Removes an expression node from a Material or MaterialFunction graph.
  "editor_toolset.toolsets.material.MaterialTools.delete_expression": "mutate",
  // Removes a parameter group, ungrouping all parameters that belong to it. The parameter expressions themselves a
  "editor_toolset.toolsets.material.MaterialTools.delete_parameter_group": "mutate",
  // Deletes all expression nodes not connected to any material output. Useful for cleaning up a material graph aft
  "editor_toolset.toolsets.material.MaterialTools.delete_unused_expressions": "mutate",
  // Disconnects the input pin of an expression node, removing whatever is connected to it.
  "editor_toolset.toolsets.material.MaterialTools.disconnect_expressions": "mutate",
  // Disconnects the expression currently connected to a material output property.
  "editor_toolset.toolsets.material.MaterialTools.disconnect_from_output": "mutate",
  // Returns the names of all input pins on a material expression node. Use these names as to_input_name when calli
  "editor_toolset.toolsets.material.MaterialTools.get_expression_input_names": "read",
  // Returns the current wiring of each input pin on a material expression. Use after building or modifying a graph
  "editor_toolset.toolsets.material.MaterialTools.get_expression_inputs": "read",
  // Returns the names of all output pins on a material expression node. Use these names as from_output_name when c
  "editor_toolset.toolsets.material.MaterialTools.get_expression_output_names": "read",
  // Returns all expression nodes in a Material or MaterialFunction graph.
  "editor_toolset.toolsets.material.MaterialTools.get_expressions": "read",
  // Returns the expression and output pin feeding a material output property. Use to inspect what drives MP_Emissi
  "editor_toolset.toolsets.material.MaterialTools.get_property_input": "read",
  // Returns asset data for all Materials that reference this MaterialFunction.
  "editor_toolset.toolsets.material.MaterialTools.get_referencing_materials": "read",
  // Automatically arranges all expression nodes in a Material or MaterialFunction graph.
  "editor_toolset.toolsets.material.MaterialTools.layout_expressions": "mutate",
  // Returns MaterialExpression subclasses valid for the given context. Use the results with add_expression. Pass a
  "editor_toolset.toolsets.material.MaterialTools.list_expression_classes": "read",
  // Returns the unique parameter group names defined in a Material or MaterialFunction. Parameters are organised i
  "editor_toolset.toolsets.material.MaterialTools.list_parameter_groups": "read",
  // Recompiles a Material or MaterialFunction after edits. For Materials, raises if the shader fails to compile. F
  "editor_toolset.toolsets.material.MaterialTools.recompile": "mutate",
  // Renames a parameter group across all parameter expressions in a Material or MaterialFunction. All parameters c
  "editor_toolset.toolsets.material.MaterialTools.rename_parameter_group": "mutate",

  /* ── editor_toolset.toolsets.object.ObjectTools ────────────────── */
  // Returns the class of an Unreal object.
  "editor_toolset.toolsets.object.ObjectTools.get_class": "read",
  // Returns the values of one or more properties on an object.
  "editor_toolset.toolsets.object.ObjectTools.get_properties": "read",
  // Returns a list of properties that are on the specified object.
  "editor_toolset.toolsets.object.ObjectTools.list_properties": "read",
  // Resets one or more properties on an object to their default values, removing any per-instance overrides.
  "editor_toolset.toolsets.object.ObjectTools.reset_properties": "mutate",
  // Finds all subclasses of a given class.
  "editor_toolset.toolsets.object.ObjectTools.search_subclasses": "read",
  // Sets the values of properties on an object.
  "editor_toolset.toolsets.object.ObjectTools.set_properties": "mutate",

  /* ── editor_toolset.toolsets.primitive.PrimitiveTools ──────────── */
  // Adds a cone-shaped StaticMeshComponent to an actor.
  "editor_toolset.toolsets.primitive.PrimitiveTools.add_cone": "mutate",
  // Adds a cube-shaped StaticMeshComponent to an actor.
  "editor_toolset.toolsets.primitive.PrimitiveTools.add_cube": "mutate",
  // Adds a cylinder-shaped StaticMeshComponent to an actor.
  "editor_toolset.toolsets.primitive.PrimitiveTools.add_cylinder": "mutate",
  // Adds a sphere-shaped StaticMeshComponent to an actor.
  "editor_toolset.toolsets.primitive.PrimitiveTools.add_sphere": "mutate",

  /* ── editor_toolset.toolsets.programmatic.ProgrammaticToolset ──── */
  // Execute a Python script against the toolset APIs. Use this to batch multiple tool calls into a single script e
  "editor_toolset.toolsets.programmatic.ProgrammaticToolset.execute_tool_script": "mutate",
  // Get details about execution environment. This includes instructions on how to write scripts, and constraints, 
  "editor_toolset.toolsets.programmatic.ProgrammaticToolset.get_execution_environment": "read",

  /* ── editor_toolset.toolsets.scene.SceneTools ──────────────────── */
  // Creates a new actor in the scene from an asset.
  "editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset": "mutate",
  // Creates a new instance of the specified object at the specified transform.
  "editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class": "mutate",
  // Checks whether an actor can be edited.
  "editor_toolset.toolsets.scene.SceneTools.can_edit": "read",
  // Saves or discards edits to a level instance and exits edit mode.
  "editor_toolset.toolsets.scene.SceneTools.commit_level_instance": "mutate",
  // Creates a Level Instance actor in the scene referencing an existing level asset.
  "editor_toolset.toolsets.scene.SceneTools.create_level_instance": "mutate",
  // Deletes a folder from the outliner. Actors directly in the folder are moved to the parent folder. Sub-folders 
  "editor_toolset.toolsets.scene.SceneTools.delete_folder": "mutate",
  // Opens a level instance for editing. While in edit mode, scene tools such as add_to_scene_from_class and remove
  "editor_toolset.toolsets.scene.SceneTools.edit_level_instance": "mutate",
  // Searches the scene for actors that match specific criteria.
  "editor_toolset.toolsets.scene.SceneTools.find_actors": "read",
  // Returns the actors in the specified outliner folder.
  "editor_toolset.toolsets.scene.SceneTools.get_actors_in_folder": "read",
  // Returns all available collision channels for use with find_actors.
  "editor_toolset.toolsets.scene.SceneTools.get_collision_channels": "read",
  // Returns the path to the current level asset.
  "editor_toolset.toolsets.scene.SceneTools.get_current_level": "read",
  // Returns all folder paths currently in use in the outliner. Includes all intermediate parent paths. For example
  "editor_toolset.toolsets.scene.SceneTools.get_folders": "read",
  // Checks whether an actor is checked out by the current user.
  "editor_toolset.toolsets.scene.SceneTools.is_checked_out": "read",
  // Loads a level in the editor.
  "editor_toolset.toolsets.scene.SceneTools.load_level": "mutate",
  // Merges multiple StaticMesh actors into a single mesh asset and actor.
  "editor_toolset.toolsets.scene.SceneTools.merge_actors": "mutate",
  // Deletes an actor from the scene.
  "editor_toolset.toolsets.scene.SceneTools.remove_from_scene": "mutate",
  // Renames a folder in the outliner. Updates the folder path for all actors in the folder and any sub-folders. Fo
  "editor_toolset.toolsets.scene.SceneTools.rename_folder": "mutate",
  // Saves the actor to disk.
  "editor_toolset.toolsets.scene.SceneTools.save_actor": "mutate",
  // Assigns an actor to the specified folder in the outliner. Creates the folder implicitly if it does not already
  "editor_toolset.toolsets.scene.SceneTools.set_actor_folder": "mutate",
  // Traces a line through the world and returns the distance to the first hit.
  "editor_toolset.toolsets.scene.SceneTools.trace_world": "mutate",

  /* ── editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools ───── */
  // Adds a named socket to a skeletal mesh attached to a bone. Sockets are named attachment points used to attach 
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.add_socket": "mutate",
  // Assigns a physics asset to a skeletal mesh. The physics asset must be compatible with the mesh's skeleton. Use
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.assign_physics_asset": "mutate",
  // Returns the direct children of a bone.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_children": "read",
  // Returns the names of all bones in a skeletal mesh in hierarchy order. Bone names are used to target specific b
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_names": "read",
  // Returns the name of a bone's parent, or an empty string for the root bone.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_parent": "read",
  // Returns the local-space bounding volume of a skeletal mesh. The bounds represent the reference pose and do not
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bounds": "read",
  // Returns the number of LODs in a skeletal mesh asset.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_lod_count": "read",
  // Returns the material assigned to a named slot on a skeletal mesh.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_material": "read",
  // Returns the names of all material slots in a skeletal mesh. Material slot names are used when assigning materi
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_material_slots": "read",
  // Returns the names of all morph targets on a skeletal mesh. Morph targets (blend shapes) are per-vertex offsets
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_morph_target_names": "read",
  // Returns the physics asset assigned to a skeletal mesh. The physics asset defines the collision bodies and cons
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_physics_asset": "read",
  // Returns the number of sections in a specific LOD of a skeletal mesh. Sections correspond to individual materia
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_section_count": "read",
  // Returns the skeleton asset associated with a skeletal mesh. The skeleton defines the bone hierarchy shared acr
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_skeleton": "read",
  // Returns the name of the bone that a socket is attached to.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_bone": "read",
  // Returns the names of all sockets on a skeletal mesh. Sockets are named attachment points parented to bones. Th
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_names": "read",
  // Returns the local transform of a socket relative to its parent bone.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_transform": "read",
  // Returns the number of vertices in a specific LOD of a skeletal mesh.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_vertex_count": "read",
  // Imports a mesh file from disk as a SkeletalMesh asset. The source file must contain a skeleton hierarchy and s
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.import_file": "mutate",
  // Removes a named socket from a skeletal mesh.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.remove_socket": "mutate",
  // Renames a socket on a skeletal mesh.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.rename_socket": "mutate",
  // Assigns a material to a named slot on a skeletal mesh asset. This affects all instances of the mesh that do no
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.set_material": "mutate",
  // Sets the local transform of a socket relative to its parent bone.
  "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.set_socket_transform": "mutate",

  /* ── editor_toolset.toolsets.static_mesh.StaticMeshTools ───────── */
  // Generates convex hull collision shapes for a static mesh. Convex hulls provide accurate collision for physics 
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.generate_convex_collisions": "mutate",
  // Auto-generates LODs for a static mesh using triangle reduction. Each entry in triangle_percents creates one ad
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.generate_lods": "mutate",
  // Returns the local-space bounding box of a static mesh.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_bounds": "read",
  // Returns the number of LODs in a static mesh asset.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_lod_count": "read",
  // Returns the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's scre
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_lod_thresholds": "read",
  // Returns the material assigned to a named slot on a static mesh.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_material": "read",
  // Returns the names of all material slots in a static mesh. Material slot names are used when assigning material
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_material_slots": "read",
  // Returns the number of triangles in a specific LOD of a static mesh.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_triangle_count": "read",
  // Returns the number of vertices in a specific LOD of a static mesh.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.get_vertex_count": "read",
  // Imports a mesh file from disk as a StaticMesh asset.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.import_file": "mutate",
  // Returns whether Nanite is enabled for a static mesh. Nanite is Unreal's virtualized geometry system that rende
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.is_nanite_enabled": "read",
  // Removes all collision shapes from a static mesh.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.remove_collisions": "mutate",
  // Removes all auto-generated LODs from a static mesh, keeping only LOD 0.
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.remove_lods": "mutate",
  // Sets the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's screen 
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_lod_thresholds": "mutate",
  // Assigns a material to a named slot on a static mesh asset. This affects all instances of the mesh that do not 
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_material": "mutate",
  // Enables or disables Nanite for a static mesh. Changing this setting triggers a mesh rebuild. Nanite is most be
  "editor_toolset.toolsets.static_mesh.StaticMeshTools.set_nanite_enabled": "mutate",

  /* ── editor_toolset.toolsets.string_table.StringTableTools ─────── */
  // Creates a new StringTable asset.
  "editor_toolset.toolsets.string_table.StringTableTools.create": "mutate",
  // Returns the source string for a specific key.
  "editor_toolset.toolsets.string_table.StringTableTools.get_entry": "read",
  // Returns the namespace of a StringTable asset.
  "editor_toolset.toolsets.string_table.StringTableTools.get_namespace": "read",
  // Returns the table ID for a StringTable asset. The table ID is derived from the asset's package path and is use
  "editor_toolset.toolsets.string_table.StringTableTools.get_table_id": "read",
  // Imports a file from disk as a StringTable asset. The file must have a header row with at least 'Key' and 'Sour
  "editor_toolset.toolsets.string_table.StringTableTools.import_file": "mutate",
  // Lists all keys in the string table.
  "editor_toolset.toolsets.string_table.StringTableTools.list_keys": "read",
  // Removes an entry from the string table.
  "editor_toolset.toolsets.string_table.StringTableTools.remove_entry": "mutate",
  // Adds or updates an entry in the string table. If the key already exists its value is replaced; otherwise a new
  "editor_toolset.toolsets.string_table.StringTableTools.set_entry": "mutate",

  /* ── editor_toolset.toolsets.texture.TextureTools ──────────────── */
  // Returns the dimensions of a Texture2D in pixels.
  "editor_toolset.toolsets.texture.TextureTools.get_size": "read",
  // Imports an image file from disk as a Texture2D asset.
  "editor_toolset.toolsets.texture.TextureTools.import_file": "mutate",

  /* ── EditorToolset.EditorAppToolset ────────────────────────────── */
  // Renders a thumbnail for the specified asset (e.g. static meshes, skeletal meshes, skeletons, animations, monta
  "EditorToolset.EditorAppToolset.CaptureAssetImage": "mutate",
  // Captures an image of the entire editor application as the user sees it.
  "EditorToolset.EditorAppToolset.CaptureEditorImage": "mutate",
  // Captures the level viewport with optional annotations. Annotations rendering overlays a projected 3D world-spa
  "EditorToolset.EditorAppToolset.CaptureViewport": "mutate",
  // Repositions the level editor camera to focus on the specified actors. Cannot be called while PIE is active.
  "EditorToolset.EditorAppToolset.FocusOnActors": "mutate",
  // Returns the position and rotation of the level viewport camera.
  "EditorToolset.EditorAppToolset.GetCameraTransform": "read",
  // Gets the current path of the active content browser.
  "EditorToolset.EditorAppToolset.GetContentBrowserPath": "read",
  // Gets the list of assets currently open in asset editors.
  "EditorToolset.EditorAppToolset.GetOpenAssets": "read",
  // Gets the currently selected actors in the level editor.
  "EditorToolset.EditorAppToolset.GetSelectedActors": "read",
  // Gets the list of assets selected in the content browser.
  "EditorToolset.EditorAppToolset.GetSelectedAssets": "read",
  // Returns all actors in the current level whose bounds intersect the viewport frustum.
  "EditorToolset.EditorAppToolset.GetVisibleActors": "read",
  // Returns whether a Play In Editor session is currently running.
  "EditorToolset.EditorAppToolset.IsPIERunning": "read",
  // Opens an asset editor for the specified asset.
  "EditorToolset.EditorAppToolset.OpenEditorForAsset": "mutate",
  // Finds the world position of the nearest solid object at a given set of normalized view space coords.
  "EditorToolset.EditorAppToolset.ScreenCoordsToWorld": "read",
  // Finds all console variables that contain a given name.
  "EditorToolset.EditorAppToolset.SearchCVars": "read",
  // Selects the specified actors in the current scene.
  "EditorToolset.EditorAppToolset.SelectActors": "mutate",
  // Selects the specified assets in the content browser. Completes once the content browser has applied the select
  "EditorToolset.EditorAppToolset.SelectAssets": "mutate",
  // Sets the position and rotation of the level viewport camera.
  "EditorToolset.EditorAppToolset.SetCameraTransform": "mutate",
  // Navigates the active content browser to the specified folder path.
  "EditorToolset.EditorAppToolset.SetContentBrowserPath": "mutate",
  // Starts a Play-In-Editor or Simulate-In-Editor session using the current level. Completes after the engine fire
  "EditorToolset.EditorAppToolset.StartPIE": "mutate",
  // Stops the currently running play session (PIE or Simulate). Raises an error if no play session is running.
  "EditorToolset.EditorAppToolset.StopPIE": "mutate",
  // Converts a world-space position into normalized screen space based on the editor viewport camera.
  "EditorToolset.EditorAppToolset.WorldPosToScreenCoords": "read",

  /* ── EditorToolset.LogsToolset ─────────────────────────────────── */
  // Returns a sorted list of registered log categories.
  "EditorToolset.LogsToolset.GetLogCategories": "read",
  // Returns log entries from the current session's log file.
  "EditorToolset.LogsToolset.GetLogEntries": "read",
  // Returns the current verbosity level for a log category.
  "EditorToolset.LogsToolset.GetVerbosity": "read",
  // Sets the verbosity level for a log category.
  "EditorToolset.LogsToolset.SetVerbosity": "mutate",

  /* ── GameFeaturesToolset.GameFeaturesToolset ───────────────────── */
  // Gets the current state of a Game Feature Plugin.
  "GameFeaturesToolset.GameFeaturesToolset.GetGameFeatureState": "read",
  // Checks whether a Game Feature Plugin is active. Raises an error if the subsystem is unavailable or the plugin 
  "GameFeaturesToolset.GameFeaturesToolset.IsGameFeatureActive": "read",
  // Return whether or not a plugin is a Game Feature Plugin. Will error if no plugin of this name can be found by 
  "GameFeaturesToolset.GameFeaturesToolset.IsGameFeaturePlugin": "read",
  // Lists all discovered Game Feature Plugins sorted by name. This includes enabled and disabled plugins. Only ena
  "GameFeaturesToolset.GameFeaturesToolset.ListDiscoveredGameFeaturePlugins": "read",
  // Lists all enabled Game Feature Plugins sorted by name. Enabled plugins are the only plugins known by the Game 
  "GameFeaturesToolset.GameFeaturesToolset.ListEnabledGameFeaturePlugins": "read",
  // Requests activation of a Game Feature Plugin. Returns true if the activation request was submitted successfull
  "GameFeaturesToolset.GameFeaturesToolset.RequestActivateGameFeature": "mutate",
  // Requests deactivation of a Game Feature Plugin. Returns true if the deactivation request was submitted success
  "GameFeaturesToolset.GameFeaturesToolset.RequestDeactivateGameFeature": "mutate",

  /* ── GameplayTagsToolset.GameplayTagsToolset ───────────────────── */
  // Adds a new gameplay tag to the project. This should ONLY be called after getting explicit direction or permiss
  "GameplayTagsToolset.GameplayTagsToolset.AddTag": "mutate",
  // Returns assets that reference a gameplay tag.
  "GameplayTagsToolset.GameplayTagsToolset.FindReferencersByTag": "read",
  // Returns detailed information about a specific gameplay tag.
  "GameplayTagsToolset.GameplayTagsToolset.GetTagInfo": "read",
  // Returns gameplay tags registered in the project.
  "GameplayTagsToolset.GameplayTagsToolset.ListTags": "read",
  // Removes a gameplay tag from the project. This should ONLY be called after getting explicit direction or permis
  "GameplayTagsToolset.GameplayTagsToolset.RemoveTag": "mutate",
  // Renames a gameplay tag, updating all references in the project. This should ONLY be called after getting expli
  "GameplayTagsToolset.GameplayTagsToolset.RenameTag": "mutate",

  /* ── GASToolsets.AbilitySystemInspectorToolset ─────────────────── */
  // Returns all gameplay effects currently active on the actor's AbilitySystemComponent.
  "GASToolsets.AbilitySystemInspectorToolset.GetActiveEffects": "read",
  // Returns the gameplay tags currently owned by the actor's AbilitySystemComponent (includes loose tags, effect-g
  "GASToolsets.AbilitySystemInspectorToolset.GetActiveTags": "read",
  // Returns the current base and modified values of all gameplay attributes on the actor's AbilitySystemComponent.
  "GASToolsets.AbilitySystemInspectorToolset.GetAttributeValues": "read",
  // Returns all abilities granted to the actor's AbilitySystemComponent.
  "GASToolsets.AbilitySystemInspectorToolset.GetGrantedAbilities": "read",

  /* ── GASToolsets.AttributeSetToolset ───────────────────────────── */
  // Returns all AttributeSet subclasses found in the project, including their attributes. Covers both native C++ s
  "GASToolsets.AttributeSetToolset.FindAttributeSetClasses": "read",
  // Returns the gameplay attributes defined on a specific AttributeSet class.
  "GASToolsets.AttributeSetToolset.ListAttributes": "read",

  /* ── GASToolsets.GameplayCueToolset ────────────────────────────── */
  // Adds a new gameplay cue tag to the project. This should ONLY be called after getting explicit direction or per
  "GASToolsets.GameplayCueToolset.AddCueTag": "mutate",
  // Creates a new GameplayCueNotify Blueprint asset at the specified content browser location. This should ONLY be
  "GASToolsets.GameplayCueToolset.CreateCueNotifyAsset": "mutate",
  // Executes a gameplay cue non-replicated on the currently selected actor in the editor. Useful for previewing cu
  "GASToolsets.GameplayCueToolset.ExecuteCueOnSelectedActor": "mutate",
  // Returns all GameplayCueNotify assets found in the project via the asset registry.
  "GASToolsets.GameplayCueToolset.FindCueNotifyAssets": "read",
  // Returns gameplay cue tags that have no corresponding GameplayCueNotify asset in the project. Tags without noti
  "GASToolsets.GameplayCueToolset.FindCueTagsWithoutNotifies": "read",
  // Returns information about a specific gameplay cue, including its notify asset.
  "GASToolsets.GameplayCueToolset.GetCueInfo": "read",
  // Returns gameplay cue tags registered in the project.
  "GASToolsets.GameplayCueToolset.ListCues": "read",
  // Removes a gameplay cue tag from the project. This should ONLY be called after getting explicit direction or pe
  "GASToolsets.GameplayCueToolset.RemoveCueTag": "mutate",

  /* ── NiagaraToolsets.NiagaraToolset_Assets ─────────────────────── */
  // Searches for UNiagaraScript assets matching the given filters. Reads filterable metadata from asset registry t
  "NiagaraToolsets.NiagaraToolset_Assets.FindNiagaraScripts": "read",
  // Returns the project's configured asset discovery groups. Each group describes a content directory's purpose an
  "NiagaraToolsets.NiagaraToolset_Assets.GetAssetDiscoveryInfo": "read",
  // Returns the decoded asset-registry tag metadata for a Niagara script asset. Looks up the asset by object path 
  "NiagaraToolsets.NiagaraToolset_Assets.GetNiagaraScriptDigest": "read",

  /* ── NiagaraToolsets.NiagaraToolset_Blueprint ──────────────────── */
  // Creates a Blueprint actor wrapper from a Niagara Component. This generates a new Blueprint actor and preserves
  "NiagaraToolsets.NiagaraToolset_Blueprint.ConstructNiagaraBPWrapperFromComponent": "mutate",
  // Creates a Blueprint actor wrapper around a Niagara System. This generates a new Blueprint actor with a Niagara
  "NiagaraToolsets.NiagaraToolset_Blueprint.ConstructNiagaraBPWrapperFromSystem": "mutate",

  /* ── NiagaraToolsets.NiagaraToolset_Component ──────────────────── */
  // Returns all user variable values currently set on the component. This retrieves the current values of all user
  "NiagaraToolsets.NiagaraToolset_Component.GetUserVariables": "read",
  // Gets the current value of a specific user variable on the component. This retrieves the current value of a use
  "NiagaraToolsets.NiagaraToolset_Component.GetVariable": "read",
  // Sets the Niagara System for a component. Use this instead of setting the Asset property directly to ensure pro
  "NiagaraToolsets.NiagaraToolset_Component.SetSystem": "mutate",
  // Sets the value of a user variable on the component. This overrides the default value of a user-exposed paramet
  "NiagaraToolsets.NiagaraToolset_Component.SetVariable": "mutate",

  /* ── NiagaraToolsets.NiagaraToolset_Info ───────────────────────── */
  // Returns information about a UEnum and all its values. ALWAYS call this when working with a UEnum type to see v
  "NiagaraToolsets.NiagaraToolset_Info.UEnum_Info": "read",

  /* ── NiagaraToolsets.NiagaraToolset_System ─────────────────────── */
  // Adds an emitter to a Niagara System. The new emitter will be based on the template emitter, inheriting its con
  "NiagaraToolsets.NiagaraToolset_System.AddEmitter": "mutate",
  // Adds a module to a script stack. The module will be inserted into the specified script's execution stack. Retu
  "NiagaraToolsets.NiagaraToolset_System.AddModule": "mutate",
  // Adds a renderer to an emitter. Creates a new renderer of the specified type and adds it to the emitter's rende
  "NiagaraToolsets.NiagaraToolset_System.AddRenderer": "mutate",
  // Adds a single parameter to an existing SetParameters module. The module referenced by ModuleRef must be a SetP
  "NiagaraToolsets.NiagaraToolset_System.AddSetParameterEntry": "mutate",
  // Adds a SetParameters module to a script stack. Unlike AddModule which requires a script asset, a SetParameters
  "NiagaraToolsets.NiagaraToolset_System.AddSetParametersModule": "mutate",
  // Adds or updates user variables on a system. If a variable with the same name already exists, it will be replac
  "NiagaraToolsets.NiagaraToolset_System.AddUserVariables": "mutate",
  // Applies a Fix-style stack issue fix identified by IssueId and FixId. Link-style fixes are rejected. The fix is
  "NiagaraToolsets.NiagaraToolset_System.ApplyStackIssueFix": "mutate",
  // Creates a new Niagara System asset. The new system will be based on the template system, inheriting its config
  "NiagaraToolsets.NiagaraToolset_System.CreateNiagaraSystem": "mutate",
  // Returns all available Dynamic Input Module assets compatible with the given type. Dynamic inputs provide proce
  "NiagaraToolsets.NiagaraToolset_System.GetAvailableDynamicInputs": "read",
  // Returns property schema for a specific Data Interface class. Describes all available properties and their type
  "NiagaraToolsets.NiagaraToolset_System.GetDataInterfaceSchema": "read",
  // Returns the full recursive chain for a dynamic input: topology metadata and resolved values at every level. Th
  "NiagaraToolsets.NiagaraToolset_System.GetDynamicInputChain": "read",
  // Returns schema for a dynamic input module in the stack. Describes the inputs and configuration for a procedura
  "NiagaraToolsets.NiagaraToolset_System.GetDynamicInputSchema": "read",
  // Returns schema for a dynamic input asset. Standalone function that doesn't require a system context - useful f
  "NiagaraToolsets.NiagaraToolset_System.GetDynamicInputSchemaFromAsset": "read",
  // Returns emitter property values as a single JSON-string blob in PropertyValues. The blob contains the full FVe
  "NiagaraToolsets.NiagaraToolset_System.GetEmitterData": "read",
  // Returns all resolved input values for every module across all four emitter script stacks. One FNiagaraExt_Modu
  "NiagaraToolsets.NiagaraToolset_System.GetEmitterInputValues": "read",
  // Returns property schema for Niagara Emitter. Describes all available properties and their types that can be se
  "NiagaraToolsets.NiagaraToolset_System.GetEmitterSchema": "read",
  // Returns lightweight emitter metadata: name, enabled state, sim target, renderer classes. Use this when you onl
  "NiagaraToolsets.NiagaraToolset_System.GetEmitterSummary": "read",
  // Returns full emitter topology: four script stacks with all modules and inputs, renderer references. All fields
  "NiagaraToolsets.NiagaraToolset_System.GetEmitterTopology": "read",
  // Returns resolved input values for a single module. Use when you need values for one specific module without wa
  "NiagaraToolsets.NiagaraToolset_System.GetModuleInputValues": "read",
  // Returns schema for a module and all its inputs. Call this after seeing a module in topology to understand what
  "NiagaraToolsets.NiagaraToolset_System.GetModuleSchema": "read",
  // Returns schema for a module asset. Standalone function that doesn't require a system context - useful for brow
  "NiagaraToolsets.NiagaraToolset_System.GetModuleSchemaFromAsset": "read",
  // Returns module topology: metadata and all inputs (name/type/visibility only, no values). All fields always pop
  "NiagaraToolsets.NiagaraToolset_System.GetModuleTopology": "read",
  // Returns renderer property values. Retrieves the current values of all configurable renderer properties.
  "NiagaraToolsets.NiagaraToolset_System.GetRendererData": "read",
  // Returns property schema for a specific Renderer class. Describes all available properties and their types for 
  "NiagaraToolsets.NiagaraToolset_System.GetRendererSchema": "read",
  // Returns all resolved input values for every module in the given script stack. One FNiagaraExt_ModuleInputValue
  "NiagaraToolsets.NiagaraToolset_System.GetScriptStackInputValues": "read",
  // Returns script stack topology: all modules and their inputs in execution order. All fields always populated.
  "NiagaraToolsets.NiagaraToolset_System.GetScriptStackTopology": "read",
  // Returns the value of a stack module input. Retrieves the current value and configuration for a specific module
  "NiagaraToolsets.NiagaraToolset_System.GetStackInputData": "read",
  // Returns schema for a single module input in the stack. Describes the type, metadata, and configuration options
  "NiagaraToolsets.NiagaraToolset_System.GetStackInputSchema": "read",
  // Returns stack input topology: name, type, visibility, editability. No value payload. For the resolved value ca
  "NiagaraToolsets.NiagaraToolset_System.GetStackInputTopology": "read",
  // Returns all stack issues (errors, warnings, info) from the Niagara module stack, including dismissed ones. Wai
  "NiagaraToolsets.NiagaraToolset_System.GetStackIssues": "read",
  // Returns the current compile state of a Niagara System: aggregate status, per-script compile events, and summar
  "NiagaraToolsets.NiagaraToolset_System.GetSystemCompileState": "read",
  // Returns system property values. Retrieves the current values of all configurable system-level properties.
  "NiagaraToolsets.NiagaraToolset_System.GetSystemData": "read",
  // Returns the four Used* sets (renderers, data interfaces, modules, dynamic inputs) gathered across all emitters
  "NiagaraToolsets.NiagaraToolset_System.GetSystemDependencies": "read",
  // Returns property schema for Niagara System. Describes all available properties and their types that can be set
  "NiagaraToolsets.NiagaraToolset_System.GetSystemSchema": "read",
  // Returns lightweight system metadata: name, user variables, and one summary entry per emitter. Use this for fir
  "NiagaraToolsets.NiagaraToolset_System.GetSystemSummary": "read",
  // Returns all user variables defined on the system. User variables are parameters exposed for external control a
  "NiagaraToolsets.NiagaraToolset_System.GetUserVariables": "read",
  // Removes an emitter from a system. Deletes the specified emitter and all its associated scripts, modules, and r
  "NiagaraToolsets.NiagaraToolset_System.RemoveEmitter": "mutate",
  // Removes a module from a script stack. Deletes the specified module and all its inputs from the script's execut
  "NiagaraToolsets.NiagaraToolset_System.RemoveModule": "mutate",
  // Removes a renderer from an emitter. Deletes the specified renderer from the emitter's renderer list.
  "NiagaraToolsets.NiagaraToolset_System.RemoveRenderer": "mutate",
  // Removes a parameter from an existing SetParameters module by name. The module referenced by ModuleRef must be 
  "NiagaraToolsets.NiagaraToolset_System.RemoveSetParameterEntry": "mutate",
  // Removes user variables from a system. Deletes the specified user variables from the system's user parameter co
  "NiagaraToolsets.NiagaraToolset_System.RemoveUserVariables": "mutate",
  // Sets property values on a Niagara Emitter. Applies new values to emitter-level properties based on the provide
  "NiagaraToolsets.NiagaraToolset_System.SetEmitterData": "mutate",
  // Sets whether a module is enabled. Disabled modules remain in the stack but don't execute. Current state is vis
  "NiagaraToolsets.NiagaraToolset_System.SetModuleEnabled": "mutate",
  // Sets property values on a Niagara Renderer. Applies new values to renderer properties based on the provided da
  "NiagaraToolsets.NiagaraToolset_System.SetRendererData": "mutate",
  // Sets the value of a stack module input and returns the resulting stored value. Updates the value and configura
  "NiagaraToolsets.NiagaraToolset_System.SetStackInputData": "mutate",
  // Sets property values on a Niagara System. Applies new values to system-level properties based on the provided 
  "NiagaraToolsets.NiagaraToolset_System.SetSystemData": "mutate",

  /* ── PCGToolset.PCGSpatialToolset ──────────────────────────────── */
  // Runs an instant PCG graph with the specified parameters in fire-and-forget mode (Should be called directly: No
  "PCGToolset.PCGSpatialToolset.RunPCGInstantGraph": "mutate",

  /* ── PCGToolset.PCGToolset ─────────────────────────────────────── */
  // Adds a comment box around the given nodes.
  "PCGToolset.PCGToolset.AddCommentBox": "mutate",
  // Adds a native node to the graph.
  "PCGToolset.PCGToolset.AddNode": "mutate",
  // Adds a subgraph node to the graph.
  "PCGToolset.PCGToolset.AddSubgraphNode": "mutate",
  // Add an edge between two nodes connected to the specified pins.
  "PCGToolset.PCGToolset.ConnectNodePins": "mutate",
  // Creates a new saved PCG graph asset.
  "PCGToolset.PCGToolset.CreateGraph": "mutate",
  // Removes the edge between two nodes connected to the specified pins.
  "PCGToolset.PCGToolset.DisconnectNodePins": "mutate",
  // Triggers the user to draw a spline in the viewport to be used later in the world building. Waits for the user 
  "PCGToolset.PCGToolset.DrawSpline": "mutate",
  // Executes the graph instance and returns any issues encountered during execution.
  "PCGToolset.PCGToolset.ExecuteGraphInstance": "mutate",
  // Returns the description of a PCG graph.
  "PCGToolset.PCGToolset.GetGraphDescription": "read",
  // Gets the graph instance params of a specific actor, actor MUST have a graph instance
  "PCGToolset.PCGToolset.GetGraphInstanceParams": "read",
  // Returns the schema for a PCG Graph's graph parameters
  "PCGToolset.PCGToolset.GetGraphSchema": "read",
  // Returns the complete structure of a PCG graph including all nodes, connections, exposed parameters, and commen
  "PCGToolset.PCGToolset.GetGraphStructure": "read",
  // Returns the schema for a PCG node type including input/output pins, parameters, and their types.
  "PCGToolset.PCGToolset.GetNativeNodeSchema": "read",
  // Returns a JSON Data View of a specific node's output data from the last graph execution. On first call, enable
  "PCGToolset.PCGToolset.GetNodeDataView": "read",
  // Returns node details including name, position, and all parameter values.
  "PCGToolset.PCGToolset.GetNodeInfo": "read",
  // Lists the PCG graphs that can be used with the Subgraph native node. Only these graphs should be used with the
  "PCGToolset.PCGToolset.ListAvailableSubgraphs": "read",
  // Gets all actors with a PCG graph instance in the scene.
  "PCGToolset.PCGToolset.ListGraphInstances": "read",
  // Returns a list of available native PCG node type names.
  "PCGToolset.PCGToolset.ListNativeNodes": "read",
  // Removes a comment box from the graph. Does not affect the nodes it contains.
  "PCGToolset.PCGToolset.RemoveCommentBox": "mutate",
  // Removes graph parameters to a specific PCG graph, such that they are not overridable anymore.
  "PCGToolset.PCGToolset.RemoveGraphParams": "mutate",
  // Removes the node from the graph, will also remove edges connected to the node.
  "PCGToolset.PCGToolset.RemoveNode": "mutate",
  // Change the position of node.
  "PCGToolset.PCGToolset.RepositionNode": "mutate",
  // Resets the given graph instance params back to the graph's default values. Actor MUST have a graph instance.
  "PCGToolset.PCGToolset.ResetGraphInstanceParams": "mutate",
  // Set the description of a PCGGraph
  "PCGToolset.PCGToolset.SetGraphDescription": "mutate",
  // Sets the graph instance params of a specific actor, actor MUST have a graph instance
  "PCGToolset.PCGToolset.SetGraphInstanceParams": "mutate",
  // Adds one or more graph user parameters to a specific PCG graph, such that they will be overridable in per grap
  "PCGToolset.PCGToolset.SetGraphParams": "mutate",
  // Change the comment on the specified node.
  "PCGToolset.PCGToolset.SetNodeComment": "mutate",
  // Spawns a PCG Volume with associated Graph Instance into the scene, optionally with Graph Param overrides.
  "PCGToolset.PCGToolset.SpawnGraphInstance": "mutate",
  // Updates an existing comment box with new nodes and value.
  "PCGToolset.PCGToolset.UpdateCommentBox": "mutate",
  // Updates a node by changing its params and/or title.
  "PCGToolset.PCGToolset.UpdateNode": "mutate",

  /* ── PhysicsToolsets.PhysicsAssetToolset ───────────────────────── */
  // Adds a new empty body for the given bone.
  "PhysicsToolsets.PhysicsAssetToolset.AddBody": "mutate",
  // Adds a new constraint between two bodies. Both bodies must already exist.
  "PhysicsToolsets.PhysicsAssetToolset.AddConstraint": "mutate",
  // Creates a physics asset from a skeletal mesh, auto-generating collision bodies for each bone. The asset is pla
  "PhysicsToolsets.PhysicsAssetToolset.CreateFromMesh": "mutate",
  // Returns the mass-scale multiplier for the given body.
  "PhysicsToolsets.PhysicsAssetToolset.GetBodyMassScale": "read",
  // Returns the bone name for each rigid body in a physics asset.
  "PhysicsToolsets.PhysicsAssetToolset.GetBodyNames": "read",
  // Returns the physics simulation mode for the given body.
  "PhysicsToolsets.PhysicsAssetToolset.GetBodyPhysicsMode": "read",
  // Returns all collision shapes assigned to a body.
  "PhysicsToolsets.PhysicsAssetToolset.GetBodyShapes": "read",
  // Returns all constraints in the physics asset with their current angular limits.
  "PhysicsToolsets.PhysicsAssetToolset.GetConstraints": "read",
  // Removes the body for the given bone along with any constraints that reference it. Raises a script error if Phy
  "PhysicsToolsets.PhysicsAssetToolset.RemoveBody": "mutate",
  // Removes the constraint between two bodies.
  "PhysicsToolsets.PhysicsAssetToolset.RemoveConstraint": "mutate",
  // Removes a collision primitive from a body by name.
  "PhysicsToolsets.PhysicsAssetToolset.RemoveShape": "mutate",
  // Sets the mass-scale multiplier for the given body.
  "PhysicsToolsets.PhysicsAssetToolset.SetBodyMassScale": "mutate",
  // Sets the physics simulation mode for the given body.
  "PhysicsToolsets.PhysicsAssetToolset.SetBodyPhysicsMode": "mutate",
  // Adds or replaces a box collision primitive on a body. If any shape with the given name already exists on the b
  "PhysicsToolsets.PhysicsAssetToolset.SetBox": "mutate",
  // Adds or replaces a capsule collision primitive on a body. If any shape with the given name already exists on t
  "PhysicsToolsets.PhysicsAssetToolset.SetCapsule": "mutate",
  // Updates the angular limits for an existing constraint.
  "PhysicsToolsets.PhysicsAssetToolset.SetConstraintLimits": "mutate",
  // Adds or replaces a sphere collision primitive on a body. If any shape with the given name already exists on th
  "PhysicsToolsets.PhysicsAssetToolset.SetSphere": "mutate",

  /* ── PluginToolset.PluginToolset ───────────────────────────────── */
  // Adds a dependency entry to a plugin's Plugins array in its .uplugin file. No-ops if a dependency with that nam
  "PluginToolset.PluginToolset.AddPluginDependency": "mutate",
  // Creates a new plugin from a template and loads it into the editor. Use GetPluginTemplateDescriptions to obtain
  "PluginToolset.PluginToolset.CreatePlugin": "mutate",
  // Returns the dependency entries from a plugin's Plugins array in its .uplugin file.
  "PluginToolset.PluginToolset.GetPluginDependencies": "read",
  // Returns the names of all discovered plugins that declare a dependency on the given plugin.
  "PluginToolset.PluginToolset.GetPluginDependents": "read",
  // Gets the editable descriptor fields for a discovered plugin.
  "PluginToolset.PluginToolset.GetPluginDescriptor": "read",
  // Returns the name of the enabled plugin whose content mount point contains the given asset path. Accepts full a
  "PluginToolset.PluginToolset.GetPluginForAsset": "read",
  // Gets metadata for a discovered plugin, including description, version, base directory, content directory, desc
  "PluginToolset.PluginToolset.GetPluginInfo": "read",
  // Returns the list of available plugin templates. Pass one of the results to CreatePlugin to create a new plugin
  "PluginToolset.PluginToolset.GetPluginTemplateDescriptions": "read",
  // Checks whether a discovered plugin is currently enabled.
  "PluginToolset.PluginToolset.IsEnabled": "read",
  // Checks whether the editor settings permit plugin creation from the plugin browser.
  "PluginToolset.PluginToolset.IsPluginCreationAllowed": "read",
  // Checks whether the editor settings permit modifying plugins from the plugin browser.
  "PluginToolset.PluginToolset.IsPluginModificationAllowed": "read",
  // Lists the names of all discovered plugins (enabled and disabled), sorted alphabetically.
  "PluginToolset.PluginToolset.ListDiscoveredPlugins": "read",
  // Lists the names of all enabled plugins, sorted alphabetically.
  "PluginToolset.PluginToolset.ListEnabledPlugins": "read",
  // Removes a dependency entry from a plugin's Plugins array in its .uplugin file.
  "PluginToolset.PluginToolset.RemovePluginDependency": "mutate",
  // Enables or disables a plugin in the project config. The change takes effect on the next editor restart.
  "PluginToolset.PluginToolset.SetPluginEnabled": "mutate",
  // Updates a plugin's descriptor fields and writes them to its .uplugin file. Checks out the file via source cont
  "PluginToolset.PluginToolset.UpdatePluginDescriptor": "mutate",
  // Validates that PluginName and RelativePluginLocation are acceptable for a new plugin.
  "PluginToolset.PluginToolset.ValidateNewPluginNameAndLocation": "read",

  /* ── SemanticSearchToolset.SemanticSearchToolset ───────────────── */
  // Find assets whose embeddings are semantically similar to the given asset's embedding. Vector-only (no BM25). T
  "SemanticSearchToolset.SemanticSearchToolset.FindSimilar": "read",
  // Run a semantic search over the Content Browser assets indexed by the SemanticSearch plugin.
  "SemanticSearchToolset.SemanticSearchToolset.Search": "read",

  /* ── SlateInspectorToolset.SlateInspectorToolset ───────────────── */
  // Click a Slate widget identified by its ref.
  "SlateInspectorToolset.SlateInspectorToolset.Click": "mutate",
  // Drag from one Slate widget to another (mouse down, move, release).
  "SlateInspectorToolset.SlateInspectorToolset.Drag": "mutate",
  // Fill multiple Slate form fields at once.
  "SlateInspectorToolset.SlateInspectorToolset.FillForm": "mutate",
  // Hover over a Slate widget, triggering any hover state or tooltip.
  "SlateInspectorToolset.SlateInspectorToolset.Hover": "mutate",
  // List all active observers as a JSON array for debugging. Each entry includes the observer identifier, whether 
  "SlateInspectorToolset.SlateInspectorToolset.ListObservers": "read",
  // Register an observer on a widget subtree so its refs are continuously kept up to date (~100ms tick). Call this
  "SlateInspectorToolset.SlateInspectorToolset.Observe": "mutate",
  // Press and release a keyboard key on the currently focused Slate widget. Supports modifier prefixes: "Ctrl+C", 
  "SlateInspectorToolset.SlateInspectorToolset.PressKey": "mutate",
  // Screenshot a Slate widget or the active editor window. Prefer this over SceneTools.take_screenshot for Editor 
  "SlateInspectorToolset.SlateInspectorToolset.Screenshot": "mutate",
  // Select an option in a Slate combobox by its text label. Opens the dropdown, finds the matching text, and click
  "SlateInspectorToolset.SlateInspectorToolset.SelectOption": "mutate",
  // Capture a Slate UI accessibility snapshot. Use this to read the current widget tree and discover refs for acti
  "SlateInspectorToolset.SlateInspectorToolset.Snapshot": "read",
  // Type text into a Slate text input widget. Focuses the widget first, then sends one key event per character.
  "SlateInspectorToolset.SlateInspectorToolset.Type": "mutate",
  // Remove an observer by its identifier.
  "SlateInspectorToolset.SlateInspectorToolset.Unobserve": "mutate",
  // Check if text is present or absent in the Slate widget tree. Non-blocking: checks once and returns immediately
  "SlateInspectorToolset.SlateInspectorToolset.WaitFor": "read",
  // List, select, or close top-level Slate editor windows.
  "SlateInspectorToolset.SlateInspectorToolset.Windows": "unknown",

  /* ── state_tree_toolset.toolsets.state_tree.StateTreeTools ─────── */
  // Returns child states of a state.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_children": "read",
  // Returns the editor data for a StateTree asset.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_editor_data": "read",
  // Returns enter conditions on a state.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_enter_conditions": "read",
  // Returns global evaluators.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_evaluators": "read",
  // Returns global tasks that run across all states.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_global_tasks": "read",
  // Returns a human-readable description for a node.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_node_description": "read",
  // Returns top-level states of a StateTree.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_root_states": "read",
  // Returns tasks on a state.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_tasks": "read",
  // Returns transitions on a state.
  "state_tree_toolset.toolsets.state_tree.StateTreeTools.get_transitions": "read",

  /* ── ToolsetRegistry.AgentSkillToolset ─────────────────────────── */
  // Creates a new AgentSkill. This should ONLY be called after getting explicit direction or permission from the u
  "ToolsetRegistry.AgentSkillToolset.CreateSkill": "mutate",
  // Returns detailed information about a specific set of AgentSkills.
  "ToolsetRegistry.AgentSkillToolset.GetSkills": "read",
  // Gets a summary of all AgentSkills in the project.
  "ToolsetRegistry.AgentSkillToolset.ListSkills": "read",
  // Updates an existing AgentSkill. This should ONLY be called after getting explicit direction or permission from
  "ToolsetRegistry.AgentSkillToolset.UpdateSkill": "mutate",

  /* ── UMGToolSet.UMGToolSet ─────────────────────────────────────── */
  // Adds a UI component of the given class to the named widget.
  "UMGToolSet.UMGToolSet.AddUIComponent": "mutate",
  // Adds a widget to the tree at the specified position. Returns full widget info including Slot pointer. When Par
  "UMGToolSet.UMGToolSet.AddWidget": "mutate",
  // Adds a Blueprint event handler graph node bound to a widget's multicast delegate event, Typical events: UButto
  "UMGToolSet.UMGToolSet.BindToEventProperty": "mutate",
  // Compiles a widget blueprint. Returns false with error details if compilation fails. Errors include missing Bin
  "UMGToolSet.UMGToolSet.CompileWidgetBlueprint": "mutate",
  // Creates a new Widget Blueprint asset. Returns the blueprint or nullptr on failure.
  "UMGToolSet.UMGToolSet.CreateWidgetBlueprint": "mutate",
  // Returns named slot bindings (separate from tree hierarchy).
  "UMGToolSet.UMGToolSet.GetNamedSlots": "read",
  // Returns the Category, Description and if it's a Panel for a single widget class. Same per-entry data as ListWi
  "UMGToolSet.UMGToolSet.GetWidgetClassInfo": "read",
  // Full property dump of every widget in the tree. Each line: [N] Type Name Prop:Value ... slot:(SlotProp:Value .
  "UMGToolSet.UMGToolSet.GetWidgetDescription": "read",
  // Returns blueprint info and all widgets in depth-first order. Children within each parent are in their panel sl
  "UMGToolSet.UMGToolSet.GetWidgets": "read",
  // Returns the maximum depth of the widget tree. Depth: root with no children = 0; root + children = 1; etc.
  "UMGToolSet.UMGToolSet.GetWidgetTreeDepth": "read",
  // Lists widget blueprints in a content folder.
  "UMGToolSet.UMGToolSet.ListWidgetBlueprints": "read",
  // Lists available widget classes, optionally filtered by name substring.
  "UMGToolSet.UMGToolSet.ListWidgetClasses": "read",
  // Moves a UI component before or after another component on the same widget.
  "UMGToolSet.UMGToolSet.MoveUIComponent": "mutate",
  // Moves a widget to a new parent panel at the specified position. Returns updated widget info with new Slot.
  "UMGToolSet.UMGToolSet.MoveWidget": "mutate",
  // Removes a UI component of the given class from the named widget.
  "UMGToolSet.UMGToolSet.RemoveUIComponent": "mutate",
  // Removes a widget and its children from the tree.
  "UMGToolSet.UMGToolSet.RemoveWidget": "mutate",
  // Renames a widget. Returns updated widget info or empty on failure.
  "UMGToolSet.UMGToolSet.RenameWidget": "mutate",
  // Replaces a panel widget with its first child, removing the panel from the tree. The widget to replace must be 
  "UMGToolSet.UMGToolSet.ReplaceWidgetWithChild": "mutate",
  // Replaces a host widget with the content of one of its named slots. The host must implement INamedSlotInterface
  "UMGToolSet.UMGToolSet.ReplaceWidgetWithNamedSlot": "mutate",
  // Replaces a widget instance in the blueprint's widget tree with a new instance created from a different templat
  "UMGToolSet.UMGToolSet.ReplaceWidgetWithTemplate": "mutate",
  // Sets content for a named slot. Returns full widget info including Slot pointer.
  "UMGToolSet.UMGToolSet.SetNamedSlotContent": "mutate",
  // Sets the bIsVariable flag.
  "UMGToolSet.UMGToolSet.ToggleWidgetAsVariable": "mutate",
  // Wraps one or more widgets in a new panel widget of the specified class. Only the root-most widgets in the sele
  "UMGToolSet.UMGToolSet.WrapWidgets": "mutate",

  /* ── WorldConditionsToolset.WorldConditionTools ────────────────── */
  // Returns a human-readable description of a single world condition. The condition must be passed as an FInstance
  "WorldConditionsToolset.WorldConditionTools.GetConditionDescription": "read",
  // Returns a human-readable description of a world condition query.
  "WorldConditionsToolset.WorldConditionTools.GetQueryDescription": "read",
};
