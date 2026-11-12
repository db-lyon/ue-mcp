/**
 * What each wrapped engine tool does to the editor it is addressed to.
 *
 * This file is the reviewed half of the Epic action surface. The catalog in
 * `tests/golden/epic-catalog/`, one snapshot per engine version, says what
 * tools exist and what arguments they take. Through 5.8 Epic shipped no
 * annotation saying whether a tool reads or writes, so that judgement is made
 * here, by a person, once, and lives in a diff. From 5.9 every tool declares an
 * accessMode (read, write or execute), and a tool first seen with one takes it.
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
 * An entry marked `// EPIC accessMode=<mode>` took its effect from Epic's own
 * annotation: read is read, write is mutate, execute is unknown. It needs no
 * review; a person can still override one by editing the effect and its marker.
 *
 *   read     observes. Landing it in the wrong editor returns the wrong answer
 *            and changes nothing.
 *   mutate   may change the editor, its project on disk, or its process.
 *   unknown  decided by an argument rather than by the tool. Gated as mutate.
 */
import type { ActionEffect } from "../../core/types.js";

/** Keyed by the tool's fully qualified registry name. */
export const EPIC_TOOL_EFFECTS: Record<string, ActionEffect> = {
  /* ── AgentPluginToolset.AgentPluginToolset ─────────────────────── */
  // Returns details for specific capabilities. Discovery fails after 60 seconds.
  "AgentPluginToolset.AgentPluginToolset.GetCapabilities": "read", // EPIC accessMode=read
  // Lists available capabilities from the current agent plugin directories and working directory. Discovery fails 
  "AgentPluginToolset.AgentPluginToolset.ListCapabilities": "read", // EPIC accessMode=read

  /* ── AIAssistantEditor.AIAssistantToolset ──────────────────────── */
  // Returns the UE tab layout and editor context in the window hosting the assistant. Tab information is available
  "AIAssistantEditor.AIAssistantToolset.GetDockedContext": "read", // EPIC accessMode=read
  // Returns important information about the current project and/or current user.
  "AIAssistantEditor.AIAssistantToolset.GetProjectContext": "read", // EPIC accessMode=read

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

  /* ── AIModuleToolset.BehaviorTreeToolset ───────────────────────── */
  // Adds a key to the blackboard.
  "AIModuleToolset.BehaviorTreeToolset.AddBlackboardKey": "mutate", // EPIC accessMode=write
  // Adds a composite as a child of a composite (or root) parent.
  "AIModuleToolset.BehaviorTreeToolset.AddComposite": "mutate", // EPIC accessMode=write
  // Adds a decorator subnode to an owner.
  "AIModuleToolset.BehaviorTreeToolset.AddDecorator": "mutate", // EPIC accessMode=write
  // Adds the tree's single root composite.
  "AIModuleToolset.BehaviorTreeToolset.AddRootComposite": "mutate", // EPIC accessMode=write
  // Adds a root-level decorator, used when the tree is referenced as a subtree.
  "AIModuleToolset.BehaviorTreeToolset.AddRootDecorator": "mutate", // EPIC accessMode=write
  // Adds a service subnode to a composite owner.
  "AIModuleToolset.BehaviorTreeToolset.AddService": "mutate", // EPIC accessMode=write
  // Adds a task as a child of a composite parent.
  "AIModuleToolset.BehaviorTreeToolset.AddTask": "mutate", // EPIC accessMode=write
  // Creates a new BehaviorTree asset, ready to edit.
  "AIModuleToolset.BehaviorTreeToolset.CreateBehaviorTree": "mutate", // EPIC accessMode=write
  // Creates a new BlackboardData asset.
  "AIModuleToolset.BehaviorTreeToolset.CreateBlackboardData": "mutate", // EPIC accessMode=write
  // Returns the blackboard keys a node's key selector accepts, matching the editor's key dropdown: the tree blackb
  "AIModuleToolset.BehaviorTreeToolset.GetAllowedBlackboardKeys": "read", // EPIC accessMode=read
  // Returns the tree's blackboard asset.
  "AIModuleToolset.BehaviorTreeToolset.GetBlackboard": "read", // EPIC accessMode=read
  // Returns the blackboard's own key names, not including parent-chain keys.
  "AIModuleToolset.BehaviorTreeToolset.GetBlackboardKeyNames": "read", // EPIC accessMode=read
  // Returns the child graph nodes of a composite (or root) parent, in sibling order.
  "AIModuleToolset.BehaviorTreeToolset.GetChildGraphNodes": "read", // EPIC accessMode=read
  // Returns the decorator subnode graph nodes attached to a graph node.
  "AIModuleToolset.BehaviorTreeToolset.GetDecoratorGraphNodes": "read", // EPIC accessMode=read
  // Returns the behavior node instance that a graph node wraps. This is the object that holds the node's settings.
  "AIModuleToolset.BehaviorTreeToolset.GetNodeInstance": "read", // EPIC accessMode=read
  // Returns the tree's root graph node.
  "AIModuleToolset.BehaviorTreeToolset.GetRootGraphNode": "read", // EPIC accessMode=read
  // Returns the service subnode graph nodes attached to a graph node.
  "AIModuleToolset.BehaviorTreeToolset.GetServiceGraphNodes": "read", // EPIC accessMode=read
  // Returns the BehaviorTree run by a RunBehavior task node instance.
  "AIModuleToolset.BehaviorTreeToolset.GetSubtree": "read", // EPIC accessMode=read
  // Reorders a child among its siblings.
  "AIModuleToolset.BehaviorTreeToolset.MoveGraphNode": "mutate", // EPIC accessMode=write
  // Reorders an existing decorator or service among its siblings without recreating it, so its configured settings
  "AIModuleToolset.BehaviorTreeToolset.MoveSubnode": "mutate", // EPIC accessMode=write
  // Removes a key from the blackboard.
  "AIModuleToolset.BehaviorTreeToolset.RemoveBlackboardKey": "mutate", // EPIC accessMode=write
  // Removes a composite/task graph node, with its children and subnodes. The root cannot be removed.
  "AIModuleToolset.BehaviorTreeToolset.RemoveGraphNode": "mutate", // EPIC accessMode=write
  // Removes a decorator or service subnode from its owner.
  "AIModuleToolset.BehaviorTreeToolset.RemoveSubnode": "mutate", // EPIC accessMode=write
  // Sets the tree's blackboard asset.
  "AIModuleToolset.BehaviorTreeToolset.SetBlackboard": "mutate", // EPIC accessMode=write
  // Sets which blackboard key a node's key selector points at, by name, resolving the binding against the tree's b
  "AIModuleToolset.BehaviorTreeToolset.SetNodeBlackboardKey": "mutate", // EPIC accessMode=write

  /* ── AIModuleToolset.EnvironmentQueryToolset ───────────────────── */
  // Adds an option to the query. An option wraps one generator that produces the items to score.
  "AIModuleToolset.EnvironmentQueryToolset.AddOption": "mutate", // EPIC accessMode=write
  // Adds a test to an option. Tests filter and score the option's generated items.
  "AIModuleToolset.EnvironmentQueryToolset.AddTest": "mutate", // EPIC accessMode=write
  // Creates a new Environment Query asset.
  "AIModuleToolset.EnvironmentQueryToolset.CreateEnvironmentQuery": "mutate", // EPIC accessMode=write
  // Returns the generator instance wrapped by an option. This is the object that holds the generator's settings.
  "AIModuleToolset.EnvironmentQueryToolset.GetGenerator": "read", // EPIC accessMode=read
  // Returns the query's option graph nodes, in order.
  "AIModuleToolset.EnvironmentQueryToolset.GetOptionGraphNodes": "read", // EPIC accessMode=read
  // Returns the option instance a graph node wraps.
  "AIModuleToolset.EnvironmentQueryToolset.GetOptionInstance": "read", // EPIC accessMode=read
  // Returns the query's root graph node. Options connect to it.
  "AIModuleToolset.EnvironmentQueryToolset.GetRootGraphNode": "read", // EPIC accessMode=read
  // Returns the test subnode graph nodes attached to an option, in order.
  "AIModuleToolset.EnvironmentQueryToolset.GetTestGraphNodes": "read", // EPIC accessMode=read
  // Returns the test instance a graph node wraps. This is the object that holds the test's settings.
  "AIModuleToolset.EnvironmentQueryToolset.GetTestInstance": "read", // EPIC accessMode=read
  // Reorders an option among its siblings. Option order is the tie-break order the query runs them in.
  "AIModuleToolset.EnvironmentQueryToolset.MoveOption": "mutate", // EPIC accessMode=write
  // Reorders a test among its siblings without recreating it, so its settings are kept. Test order is the order th
  "AIModuleToolset.EnvironmentQueryToolset.MoveTest": "mutate", // EPIC accessMode=write
  // Removes an option and its tests.
  "AIModuleToolset.EnvironmentQueryToolset.RemoveOption": "mutate", // EPIC accessMode=write
  // Removes a test from its option.
  "AIModuleToolset.EnvironmentQueryToolset.RemoveTest": "mutate", // EPIC accessMode=write
  // Enables or disables a test. A disabled test stays on the option but is excluded from the built query.
  "AIModuleToolset.EnvironmentQueryToolset.SetTestEnabled": "mutate", // EPIC accessMode=write

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

  /* ── AnimationAssistantToolset.SequencerDirectorToolset ────────── */
  // Author a complete event key end-to-end on an Event track section. Equivalent of the three-step manual flow: 1.
  "AnimationAssistantToolset.SequencerDirectorToolset.AddEventKey": "mutate", // EPIC accessMode=write
  // Create a Director Blueprint Condition function endpoint on the sequence's Director Blueprint and bind it to a 
  "AnimationAssistantToolset.SequencerDirectorToolset.CreateConditionEndpoint": "mutate", // EPIC accessMode=write
  // Create a dynamic-binding resolver function endpoint on the sequence's Director Blueprint and wire it to the bi
  "AnimationAssistantToolset.SequencerDirectorToolset.CreateDynamicBindingResolver": "mutate", // EPIC accessMode=write
  // Return the Director Blueprint owned by the sequence, creating it if one does not yet exist. Returns nullptr if
  "AnimationAssistantToolset.SequencerDirectorToolset.GetOrCreateDirectorBlueprint": "mutate", // EPIC accessMode=write

  /* ── AutomationTestToolset.AutomationTestToolset ───────────────── */
  // Initialize automation worker discovery and load the test list. Must be called once before ListTests or RunTest
  "AutomationTestToolset.AutomationTestToolset.DiscoverTests": "mutate",
  // Get detailed results for the current or most recent test run. Requires DiscoverTests() to have completed.
  "AutomationTestToolset.AutomationTestToolset.GetTestResults": "read",
  // Get a lightweight status snapshot of the automation controller. Requires DiscoverTests() to have completed.
  "AutomationTestToolset.AutomationTestToolset.GetTestStatus": "read",
  // List available automation tests. Requires DiscoverTests() to have completed.
  "AutomationTestToolset.AutomationTestToolset.ListTests": "read",
  // Run a set of automation tests by name. Requires DiscoverTests() to have completed. Starts executing the specif
  "AutomationTestToolset.AutomationTestToolset.RunTests": "mutate",
  // Run automation tests selected by a filter expression. Requires DiscoverTests() to have completed. Much faster 
  "AutomationTestToolset.AutomationTestToolset.RunTestsByFilter": "mutate",
  // Stop all currently running tests. Requires DiscoverTests() to have completed. If a RunTests async result is pe
  "AutomationTestToolset.AutomationTestToolset.StopTests": "mutate",

  /* ── BlueprintToolsets.BlueprintDslToolset ─────────────────────── */
  // Returns a line diff between two Blueprints' graphs, as DSL, and their class defaults. Empty when they match.
  "BlueprintToolsets.BlueprintDslToolset.Diff": "read", // EPIC accessMode=read
  // Returns the common grammar followed by the Blueprint dialect reference. Read it before the first Write or Edit
  "BlueprintToolsets.BlueprintDslToolset.Docs": "unknown", // EPIC accessMode=execute
  // Applies ordered exact replacements to the latest session text. Revision must come from the latest Read, Write,
  "BlueprintToolsets.BlueprintDslToolset.Edit": "unknown", // EPIC accessMode=execute
  // Returns Graph DSL node references whose palette name, category or reference contains the query (case and space
  "BlueprintToolsets.BlueprintDslToolset.FindNodeReferences": "read", // EPIC accessMode=read
  // Reads a Blueprint scope as canonical DSL and returns the revision required by Edit. Scope is empty for the who
  "BlueprintToolsets.BlueprintDslToolset.Read": "unknown", // EPIC accessMode=execute
  // Returns one node type's schema as a DSL form. Query unfamiliar node types before writing: (node `Type` (in (`P
  "BlueprintToolsets.BlueprintDslToolset.Schema": "unknown", // EPIC accessMode=execute
  // Transactionally applies a DSL document and returns its canonical form and a new revision. A failure leaves the
  "BlueprintToolsets.BlueprintDslToolset.Write": "unknown", // EPIC accessMode=execute

  /* ── BlueprintToolsets.BlueprintToolset ────────────────────────── */
  // Adds a comment box (the resizable labeled rectangle) to a Blueprint graph. Pass Nodes to wrap them in a box si
  "BlueprintToolsets.BlueprintToolset.AddCommentBox": "mutate", // EPIC accessMode=write
  // Creates a component bound event node in the event graph.
  "BlueprintToolsets.BlueprintToolset.AddComponentBoundEvent": "mutate", // EPIC accessMode=write
  // Adds an event node to the Blueprint's event graph. If EventName matches an inherited overridable event, the ne
  "BlueprintToolsets.BlueprintToolset.AddEvent": "mutate", // EPIC accessMode=write
  // Adds an event dispatcher to a Blueprint.
  "BlueprintToolsets.BlueprintToolset.AddEventDispatcher": "mutate", // EPIC accessMode=write
  // Adds a function graph to the Blueprint. If GraphName matches an inherited overridable function, the new graph 
  "BlueprintToolsets.BlueprintToolset.AddFunctionGraph": "mutate", // EPIC accessMode=write
  // Adds an input or output to a function or event dispatcher. Output params are not supported on event dispatcher
  "BlueprintToolsets.BlueprintToolset.AddFunctionParam": "mutate", // EPIC accessMode=write
  // Adds an interface to a Blueprint's implemented interfaces. The interface's function-shape members become funct
  "BlueprintToolsets.BlueprintToolset.AddInterface": "mutate", // EPIC accessMode=write
  // Adds a custom event matching a Create Event node's delegate signature and binds it. The generated event's para
  "BlueprintToolsets.BlueprintToolset.AddMatchingDelegateEvent": "mutate", // EPIC accessMode=write
  // Adds a function graph matching a Create Event node's delegate signature and binds it. The generated function's
  "BlueprintToolsets.BlueprintToolset.AddMatchingDelegateFunction": "mutate", // EPIC accessMode=write
  // Adds a pin to a node that supports dynamic pin addition (Switch, Sequence, commutative binary operators like A
  "BlueprintToolsets.BlueprintToolset.AddNodePin": "mutate", // EPIC accessMode=write
  // Adds an object reference input or output to a function or event dispatcher. Output params are not supported on
  "BlueprintToolsets.BlueprintToolset.AddObjectFunctionParam": "mutate", // EPIC accessMode=write
  // Adds a member or local variable that holds an object reference to a Blueprint.
  "BlueprintToolsets.BlueprintToolset.AddObjectVariable": "mutate", // EPIC accessMode=write
  // Adds a struct input or output to a function or event dispatcher. Output params are not supported on event disp
  "BlueprintToolsets.BlueprintToolset.AddStructFunctionParam": "mutate", // EPIC accessMode=write
  // Adds a member or local variable of a struct type to a Blueprint. Use this to add variables of any UStruct type
  "BlueprintToolsets.BlueprintToolset.AddStructVariable": "mutate", // EPIC accessMode=write
  // Adds a member or local variable to a Blueprint. Supported type names: primitives (bool, int, float, byte, stri
  "BlueprintToolsets.BlueprintToolset.AddVariable": "mutate", // EPIC accessMode=write
  // Arranges a whole graph in a readable left-to-right layout, per connected component: each component becomes a h
  "BlueprintToolsets.BlueprintToolset.ArrangeGraphLayered": "mutate", // EPIC accessMode=write
  // Arranges Nodes in a readable left-to-right layout, using every other node in Graph as a fixed anchor so the ar
  "BlueprintToolsets.BlueprintToolset.ArrangeNodes": "mutate", // EPIC accessMode=write
  // Compiles the given Blueprint. Blueprints should be compiled after all graph modifications are complete.
  "BlueprintToolsets.BlueprintToolset.CompileBlueprint": "mutate", // EPIC accessMode=write
  // Makes a connection between source (output) and dest (input) pins.
  "BlueprintToolsets.BlueprintToolset.ConnectPins": "mutate", // EPIC accessMode=write
  // Creates a new Blueprint asset in the project.
  "BlueprintToolsets.BlueprintToolset.Create": "mutate", // EPIC accessMode=write
  // Adds a new node to the graph.
  "BlueprintToolsets.BlueprintToolset.CreateNode": "mutate", // EPIC accessMode=write
  // Deletes the node from its graph.
  "BlueprintToolsets.BlueprintToolset.DeleteNode": "mutate", // EPIC accessMode=write
  // Compares two versions of a graph with the same matching the editor's Diff window uses.
  "BlueprintToolsets.BlueprintToolset.DiffGraphs": "read", // EPIC accessMode=read
  // Breaks two connected pins.
  "BlueprintToolsets.BlueprintToolset.DisconnectPins": "mutate", // EPIC accessMode=write
  // Retrieves the available node categories in a Blueprint graph, optionally filtered by compatible pin types. Cat
  "BlueprintToolsets.BlueprintToolset.FindNodeCategories": "read", // EPIC accessMode=read
  // Finds nodes in a graph by title, class, and/or execution role. All filters are optional and ANDed together.
  "BlueprintToolsets.BlueprintToolset.FindNodes": "read", // EPIC accessMode=read
  // Finds node types that can be created in a particular graph matching the search criteria. To list all nodes wit
  "BlueprintToolsets.BlueprintToolset.FindNodeTypes": "read", // EPIC accessMode=read
  // Returns the text of a comment box.
  "BlueprintToolsets.BlueprintToolset.GetCommentBoxText": "read", // EPIC accessMode=read
  // Returns detailed information for all nodes connected to the given node, including itself.
  "BlueprintToolsets.BlueprintToolset.GetConnectedSubgraph": "read", // EPIC accessMode=read
  // Returns the function currently bound to a Create Event node.
  "BlueprintToolsets.BlueprintToolset.GetCreateEventFunction": "read", // EPIC accessMode=read
  // Returns the Class Default Object (CDO) for a Blueprint.
  "BlueprintToolsets.BlueprintToolset.GetDefaultObject": "read", // EPIC accessMode=read
  // Returns the signature a Create Event node's bound function must implement. Each param is listed on its own lin
  "BlueprintToolsets.BlueprintToolset.GetDelegateSignature": "read", // EPIC accessMode=read
  // Returns the detail-panel settings of a Blueprint function.
  "BlueprintToolsets.BlueprintToolset.GetFunctionFlags": "read", // EPIC accessMode=read
  // Retrieves a specific graph from a Blueprint asset by name.
  "BlueprintToolsets.BlueprintToolset.GetGraph": "read", // EPIC accessMode=read
  // Retrieves detailed information for a list of Blueprint graph nodes.
  "BlueprintToolsets.BlueprintToolset.GetNodeInfos": "read", // EPIC accessMode=read
  // Returns the note text on a Blueprint node (the small comment bubble shown over an individual node).
  "BlueprintToolsets.BlueprintToolset.GetNodeNote": "read", // EPIC accessMode=read
  // Returns the nodes grouped inside a comment box.
  "BlueprintToolsets.BlueprintToolset.GetNodesInCommentBox": "read", // EPIC accessMode=read
  // Returns the pin names and types for a node type.
  "BlueprintToolsets.BlueprintToolset.GetNodeTypePins": "read", // EPIC accessMode=read
  // Returns the parent class of a Blueprint.
  "BlueprintToolsets.BlueprintToolset.GetParent": "read", // EPIC accessMode=read
  // Gets the value of a Blueprint graph pin. Must be an input pin that supports default values.
  "BlueprintToolsets.BlueprintToolset.GetPinValue": "read", // EPIC accessMode=read
  // Gets the user-defined category of a Blueprint variable. Categories group variables in the My Blueprint panel. 
  "BlueprintToolsets.BlueprintToolset.GetVariableCategory": "read", // EPIC accessMode=read
  // Gets whether a member variable is Blueprint Read Only (graphs can read but not write it).
  "BlueprintToolsets.BlueprintToolset.GetVariableReadOnly": "read", // EPIC accessMode=read
  // Gets the replication mode of a Blueprint member variable.
  "BlueprintToolsets.BlueprintToolset.GetVariableReplication": "read", // EPIC accessMode=read
  // Gets the tooltip shown for a Blueprint variable in the editor.
  "BlueprintToolsets.BlueprintToolset.GetVariableTooltip": "read", // EPIC accessMode=read
  // Returns whether a node's note bubble is pinned (always visible).
  "BlueprintToolsets.BlueprintToolset.IsNodeNotePinned": "read", // EPIC accessMode=read
  // Lists all comment boxes in a Blueprint graph.
  "BlueprintToolsets.BlueprintToolset.ListCommentBoxes": "read", // EPIC accessMode=read
  // Lists functions that can be bound to a Create Event node.
  "BlueprintToolsets.BlueprintToolset.ListCompatibleEventFunctions": "read", // EPIC accessMode=read
  // Lists the bindable delegate events available on a component.
  "BlueprintToolsets.BlueprintToolset.ListComponentEvents": "read", // EPIC accessMode=read
  // Lists all event dispatchers defined on a Blueprint.
  "BlueprintToolsets.BlueprintToolset.ListEventDispatchers": "read", // EPIC accessMode=read
  // Lists all events visible on the Blueprint - locally defined custom events plus inheritable events from the par
  "BlueprintToolsets.BlueprintToolset.ListEvents": "read", // EPIC accessMode=read
  // Lists all functions visible on the Blueprint - locally defined plus inheritable ones from the parent class cha
  "BlueprintToolsets.BlueprintToolset.ListFunctions": "read", // EPIC accessMode=read
  // Lists all graphs in the Blueprint.
  "BlueprintToolsets.BlueprintToolset.ListGraphs": "read", // EPIC accessMode=read
  // Lists the interfaces implemented by a Blueprint.
  "BlueprintToolsets.BlueprintToolset.ListInterfaces": "read", // EPIC accessMode=read
  // Lists a Blueprint's variables in one scope, leaving out ones a graph cannot read.
  "BlueprintToolsets.BlueprintToolset.ListVariables": "read", // EPIC accessMode=read
  // Removes a comment box from its graph.
  "BlueprintToolsets.BlueprintToolset.RemoveCommentBox": "mutate", // EPIC accessMode=write
  // Removes a function graph or event dispatcher from the Blueprint.
  "BlueprintToolsets.BlueprintToolset.RemoveFunctionGraph": "mutate", // EPIC accessMode=write
  // Removes an input or output from a function or event dispatcher. Output params are not supported on event dispa
  "BlueprintToolsets.BlueprintToolset.RemoveFunctionParam": "mutate", // EPIC accessMode=write
  // Removes an implemented interface from a Blueprint. The Blueprint must be recompiled afterward.
  "BlueprintToolsets.BlueprintToolset.RemoveInterface": "mutate", // EPIC accessMode=write
  // Removes a specific pin from a node that supports dynamic pin removal.
  "BlueprintToolsets.BlueprintToolset.RemoveNodePin": "mutate", // EPIC accessMode=write
  // Removes a member or local variable from a Blueprint.
  "BlueprintToolsets.BlueprintToolset.RemoveVariable": "mutate", // EPIC accessMode=write
  // Replaces a node's baked-in class reference from OldClass to NewClass in place, and reconstructs the node so it
  "BlueprintToolsets.BlueprintToolset.RetargetNodeClass": "mutate", // EPIC accessMode=write
  // Sets the text on a comment box.
  "BlueprintToolsets.BlueprintToolset.SetCommentBoxText": "mutate", // EPIC accessMode=write
  // Binds a function to a Create Event node. Use ListCompatibleEventFunctions to find valid function names.
  "BlueprintToolsets.BlueprintToolset.SetCreateEventFunction": "mutate", // EPIC accessMode=write
  // Sets detail-panel settings on a Blueprint function. Only the fields set on Flags are applied; leave a field un
  "BlueprintToolsets.BlueprintToolset.SetFunctionFlags": "mutate", // EPIC accessMode=write
  // Sets the note text on a Blueprint node (its comment bubble). The bubble is made immediately visible in the gra
  "BlueprintToolsets.BlueprintToolset.SetNodeNote": "mutate", // EPIC accessMode=write
  // Sets whether a node's note bubble is pinned (always visible), vs. only shown on hover.
  "BlueprintToolsets.BlueprintToolset.SetNodeNotePinned": "mutate", // EPIC accessMode=write
  // Sets a new position for the node.
  "BlueprintToolsets.BlueprintToolset.SetNodePosition": "mutate", // EPIC accessMode=write
  // Reparents a Blueprint to a new parent class. The Blueprint must be recompiled afterward.
  "BlueprintToolsets.BlueprintToolset.SetParent": "mutate", // EPIC accessMode=write
  // Sets the value of a Blueprint graph pin. Must be an input pin that supports default values.
  "BlueprintToolsets.BlueprintToolset.SetPinValue": "mutate", // EPIC accessMode=write
  // Sets the user-defined category on a Blueprint variable. Pass an empty string to reset to the default (the Blue
  "BlueprintToolsets.BlueprintToolset.SetVariableCategory": "mutate", // EPIC accessMode=write
  // Sets whether a member variable is editable per-instance on actors placed in the level.
  "BlueprintToolsets.BlueprintToolset.SetVariableInstanceEditable": "mutate", // EPIC accessMode=write
  // Sets whether a member variable is Blueprint Read Only. A read-only variable can still be read by graphs, but a
  "BlueprintToolsets.BlueprintToolset.SetVariableReadOnly": "mutate", // EPIC accessMode=write
  // Sets the replication mode on a Blueprint member variable. REP_NOTIFY will automatically create an OnRep_ funct
  "BlueprintToolsets.BlueprintToolset.SetVariableReplication": "mutate", // EPIC accessMode=write
  // Sets the tooltip shown for a Blueprint variable in the editor.
  "BlueprintToolsets.BlueprintToolset.SetVariableTooltip": "mutate", // EPIC accessMode=write

  /* ── ChaosClothAssetToolset.ChaosClothAssetToolset ─────────────── */
  // Binds a clothing asset to a specific LOD and section on the skeletal mesh. Mirrors the SkeletalMesh Editor's "
  "ChaosClothAssetToolset.ChaosClothAssetToolset.AssignClothingToSection": "mutate", // EPIC accessMode=write
  // Converts a legacy UClothingAssetCommon (the cloth asset created via UChaosClothingSimulationFactory) into a ne
  "ChaosClothAssetToolset.ChaosClothAssetToolset.ConvertClothingAssetCommonToChaosClothAsset": "mutate", // EPIC accessMode=write
  // Creates a UChaosClothAssetSKMClothingAsset from a Chaos Cloth/Outfit Asset and adds it to the skeletal mesh's 
  "ChaosClothAssetToolset.ChaosClothAssetToolset.CreateClothingAsset": "mutate", // EPIC accessMode=write
  // Returns the live solver counts for every clothing simulation running in PIE - the figures the cloth debug UI s
  "ChaosClothAssetToolset.ChaosClothAssetToolset.GetLiveClothSimulationStats": "read", // EPIC accessMode=read
  // Returns the name of clothing bound for a specific LOD section (if any).
  "ChaosClothAssetToolset.ChaosClothAssetToolset.GetSectionClothing": "read", // EPIC accessMode=read
  // Returns information about all Clothing Assets on a skeletal mesh. Use AssetName with AssignClothingToSection. 
  "ChaosClothAssetToolset.ChaosClothAssetToolset.ListClothingAssets": "read", // EPIC accessMode=read
  // Removes the clothing binding from a specific LOD section on the skeletal mesh. Mirrors the SkeletalMesh Editor
  "ChaosClothAssetToolset.ChaosClothAssetToolset.RemoveClothingFromSection": "mutate", // EPIC accessMode=write

  /* ── CheatsToolset.CheatsToolset ───────────────────────────────── */
  // Runs a cheat on the local player, exactly as if it were typed into the console.
  "CheatsToolset.CheatsToolset.CallCheat": "mutate", // EPIC accessMode=write
  // Lists the cheat commands available on the local player.
  "CheatsToolset.CheatsToolset.ListCheats": "read", // EPIC accessMode=read

  /* ── CommonUIToolset.CommonUIToolset ───────────────────────────── */
  // Activate a live activatable widget, as if the game had activated it.
  "CommonUIToolset.CommonUIToolset.ActivateWidget": "mutate", // EPIC accessMode=write
  // Deactivate a live activatable widget. Same addressing rules as ActivateWidget, but matches only currently-acti
  "CommonUIToolset.CommonUIToolset.DeactivateWidget": "mutate", // EPIC accessMode=write
  // All UI action bindings currently active (reachable by input) for a player.
  "CommonUIToolset.CommonUIToolset.GetActionBindings": "read", // EPIC accessMode=read
  // Snapshot of the CommonUI activatable tree: root nodes, action-domain roots with children and bindings, active 
  "CommonUIToolset.CommonUIToolset.GetActivatableTree": "read", // EPIC accessMode=read
  // The current status of the logger: on/off, event count, dropped count, time span, subscribed players.
  "CommonUIToolset.CommonUIToolset.GetEventLoggingStatus": "read", // EPIC accessMode=read
  // Chronological digest of logged events, oldest first. Never carries callstacks; use GetLoggedEvents when those 
  "CommonUIToolset.CommonUIToolset.GetEventTimeline": "read", // EPIC accessMode=read
  // Where Slate focus actually is for a player and where CommonUI intends it to be: the focused Slate widget, its 
  "CommonUIToolset.CommonUIToolset.GetFocusState": "read", // EPIC accessMode=read
  // Everything governing whether input reaches game vs UI for a player: active input mode and config (mouse captur
  "CommonUIToolset.CommonUIToolset.GetInputRoutingState": "read", // EPIC accessMode=read
  // Logged events, filtered.
  "CommonUIToolset.CommonUIToolset.GetLoggedEvents": "read", // EPIC accessMode=read
  // Ask the router to re-apply focus to the active root's desired focus target.
  "CommonUIToolset.CommonUIToolset.RefreshActiveRootFocus": "mutate", // EPIC accessMode=write
  // Remove an input type lock previously added with SetInputTypeLock.
  "CommonUIToolset.CommonUIToolset.RemoveInputTypeLock": "mutate", // EPIC accessMode=write
  // Lock the effective input type for a player until the lock is removed.
  "CommonUIToolset.CommonUIToolset.SetInputTypeLock": "mutate", // EPIC accessMode=write
  // Toggle an in-viewport overlay that outlines activatable widgets, color-coded by activation/focus state: green 
  "CommonUIToolset.CommonUIToolset.SetTreeVisualization": "mutate", // EPIC accessMode=write
  // Begin logging CommonUI events (widget activation/deactivation/rebuild, input mode/config changes, input method
  "CommonUIToolset.CommonUIToolset.StartEventLogging": "mutate", // EPIC accessMode=write
  // Stop logging; the buffer is retained for querying. Does nothing when not logging.
  "CommonUIToolset.CommonUIToolset.StopEventLogging": "mutate", // EPIC accessMode=write

  /* ── ConfigSettingsToolset.ConfigSettingsToolset ───────────────── */
  // Returns the current values of the specified properties as a JSON object.
  "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionPropertyValues": "read",
  // Returns a JSON Schema describing the user-visible properties of a settings section. The schema maps each prope
  "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionSchema": "read",
  // Returns the platforms the project declares support for in its .uproject file.
  "ConfigSettingsToolset.ConfigSettingsToolset.GetSupportedPlatforms": "read", // EPIC accessMode=read
  // Lists the platforms a project can declare support for, the choices in the Project Settings Supported Platforms
  "ConfigSettingsToolset.ConfigSettingsToolset.ListAvailablePlatforms": "read", // EPIC accessMode=read
  // Lists the names of all categories within a settings container, sorted alphabetically.
  "ConfigSettingsToolset.ConfigSettingsToolset.ListCategories": "read",
  // Lists the names of all known settings containers, sorted alphabetically. Common containers are "Editor" and "P
  "ConfigSettingsToolset.ConfigSettingsToolset.ListContainers": "read",
  // Lists the names of all sections within a settings category, sorted alphabetically.
  "ConfigSettingsToolset.ConfigSettingsToolset.ListSections": "read",
  // Resets the settings in a section to their default values. Project sections saved to a Default*.ini cannot be r
  "ConfigSettingsToolset.ConfigSettingsToolset.ResetSectionToDefaults": "mutate",
  // Saves the settings in a section. The default config file is checked out first when source control is enabled.
  "ConfigSettingsToolset.ConfigSettingsToolset.SaveSection": "mutate",
  // Finds settings by the label shown in the Settings editor, the property name, the tooltip, or the console varia
  "ConfigSettingsToolset.ConfigSettingsToolset.SearchSettings": "read", // EPIC accessMode=read
  // Sets one or more properties on a settings section from a JSON object and saves. PropertiesJson must be a JSON 
  "ConfigSettingsToolset.ConfigSettingsToolset.SetSectionProperties": "mutate",
  // Replaces the platforms the project declares support for and saves the .uproject file, checking it out first wh
  "ConfigSettingsToolset.ConfigSettingsToolset.SetSupportedPlatforms": "mutate", // EPIC accessMode=write

  /* ── ConsoleToolset.ConsoleToolset ─────────────────────────────── */
  // Reads the current value of a console variable as its canonical string representation (e.g. "true"/"false" for 
  "ConsoleToolset.ConsoleToolset.GetCVarValue": "read", // EPIC accessMode=read
  // Executes an arbitrary UE exec command. Output is written to the session log. Python is refused: any command co
  "ConsoleToolset.ConsoleToolset.RunConsoleCommand": "unknown", // EPIC accessMode=execute
  // Returns exec commands whose name contains the search string (case-insensitive).
  "ConsoleToolset.ConsoleToolset.SearchConsoleCommands": "read", // EPIC accessMode=read
  // Finds all console variables that contain a given name.
  "ConsoleToolset.ConsoleToolset.SearchCVars": "read", // EPIC accessMode=read
  // Sets the value of a console variable. The value is parsed according to the variable's declared type.
  "ConsoleToolset.ConsoleToolset.SetCVarValue": "mutate", // EPIC accessMode=write

  /* ── ConsoleToolset.LogsToolset ────────────────────────────────── */
  // Returns a sorted list of log categories.
  "ConsoleToolset.LogsToolset.GetLogCategories": "read", // EPIC accessMode=read
  // Returns log entries from a log file. Reads the current session's log file by default, but can read other log f
  "ConsoleToolset.LogsToolset.GetLogEntries": "read", // EPIC accessMode=read
  // Returns the current verbosity level for a log category.
  "ConsoleToolset.LogsToolset.GetVerbosity": "read", // EPIC accessMode=read
  // Sets the verbosity level for a log category.
  "ConsoleToolset.LogsToolset.SetVerbosity": "mutate", // EPIC accessMode=write

  /* ── ControlRigEditor.ControlRigSequencerEditorLibrary ─────────── */
  // Add an anim layer containing the controls and objects selected in Sequencer, adding the Base layer first if th
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddAnimLayerFromSelection": "mutate", // EPIC accessMode=write
  // Set up auto-bake recorders for the linked anim sequences on the given bindings. Equivalent to the "Add AutoBak
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddAutoBake": "mutate", // EPIC accessMode=write
  // Same as AddAutoBake, but links pre-created anim sequences instead of prompting for new ones. No asset creation
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddAutoBakeWithAnimSequences": "mutate", // EPIC accessMode=write
  // Add a constraint. When a level sequence is open in Sequencer the constraint is keyed active at the current tim
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddConstraint": "mutate", // EPIC accessMode=write
  // Add a constraint between two objects, creating their transformable handles; the constraint owns the handles. E
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddConstraintBetween": "mutate", // EPIC accessMode=write
  // Add a space to the end of a control's space picker list, as the level editor's Add Space does. Raises a script
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddControlRigAvailableSpace": "mutate", // EPIC accessMode=write
  // Add the mirrored counterparts of the selected controls to the selection. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddMirroredControlsToSelection": "mutate", // EPIC accessMode=write
  // Add the controls and objects selected in Sequencer to an anim layer, as the panel's Add Selected does. Raises 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddSelectedToAnimLayer": "mutate", // EPIC accessMode=write
  // Add the controls and actors selected in Sequencer to a selection set. Raises a script error if no level sequen
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AddSelectionToSelectionSet": "mutate", // EPIC accessMode=write
  // Adjustment blend an anim layer into the Base layer, as the Animation Layers panel's Adjustment Blend does.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.AdjustmentBlendAnimLayer": "mutate", // EPIC accessMode=write
  // Bake the constraint to keys based on the passed in frames. This will use the open sequencer to bake. See Const
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeConstraint": "mutate", // EPIC accessMode=write
  // Bake transform constraints to keys over the frame range in Settings, using the level sequence open in Sequence
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeConstraintsWithSettings": "mutate", // EPIC accessMode=write
  // BakeConstraintsWithSettings as an async operation. See BakeConstraintsWithSettings for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeConstraintsWithSettingsAsync": "mutate", // EPIC accessMode=write
  // Bake specified Control Rig Controls to a specified Space based upon the current settings. Every control needs 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeControlRigSpace": "mutate", // EPIC accessMode=write
  // BakeControlRigSpace as an async operation. See BakeControlRigSpace for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeControlRigSpaceAsync": "mutate", // EPIC accessMode=write
  // Bake (resample) all keys on the specified control rig section at a fixed display-frame interval.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeKeys": "mutate", // EPIC accessMode=write
  // Bake a section's time warp into its keys and remove the time warp
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeTimeWarp": "mutate", // EPIC accessMode=write
  // Bake the current animation in the binding to a Control Rig track, replacing any existing Control Rig tracks on
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeToControlRigFromReference": "mutate", // EPIC accessMode=write
  // BakeToControlRigFromReference as an async operation. See BakeToControlRigFromReference for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeToControlRigFromReferenceAsync": "mutate", // EPIC accessMode=write
  // Get an array per frame of specified control rig transforms for the specified control names.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BatchGetControlTransforms": "read", // EPIC accessMode=read
  // BatchGetControlTransforms read after the next editor world tick. See BatchGetControlTransforms for the paramet
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BatchGetControlTransformsAsync": "read", // EPIC accessMode=read
  // Set from Control transforms for different controls in the specified space at different frames
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BatchSetControlTransforms": "mutate", // EPIC accessMode=write
  // Blend a Control Rig's selected controls from their current values toward a pose asset, as dragging the Pose Li
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BlendControlRigPoseAsset": "mutate", // EPIC accessMode=write
  // Perform the specified blend operation on the selected keys in the curve editor, or on the selected Control Rig
  "ControlRigEditor.ControlRigSequencerEditorLibrary.BlendValuesOnSelected": "mutate", // EPIC accessMode=write
  // Clear a control's rotation order override, returning it to the authored order; its keys are re-baked so the ro
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ClearControlRigRotationOrderOverride": "mutate", // EPIC accessMode=write
  // Clear the Control Rig control selection. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ClearSelection": "mutate", // EPIC accessMode=write
  // Closes the Control Rig Pose Library panel. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ClosePoseLibraryTab": "mutate", // EPIC accessMode=write
  // Collapse and bake all sections and layers on a control rig track to just one section, over the focused sequenc
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayers": "mutate", // EPIC accessMode=write
  // Collapse and bake all sections and layers on a control rig track to just one section using passed in settings.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayersWithSettings": "mutate", // EPIC accessMode=write
  // CollapseControlRigAnimLayersWithSettings as an async operation. See CollapseControlRigAnimLayersWithSettings f
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayersWithSettingsAsync": "mutate", // EPIC accessMode=write
  // Compensate the constrained animation at the specified time so the child keeps its world transform across the c
  "ControlRigEditor.ControlRigSequencerEditorLibrary.Compensate": "mutate", // EPIC accessMode=write
  // Compensate the constrained animation at every key of the constraint's active channel.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CompensateAll": "mutate", // EPIC accessMode=write
  // Copy the internal parameter curves from vector control rig curves to transform curves, this is needed if a vec
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ControlRigCopyVectorParameterCurvesToTransform": "mutate", // EPIC accessMode=write
  // Create anim layers from the rows of the tracks selected in Sequencer, same-named rows share a layer
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateAnimLayersFromSelectedTracks": "mutate", // EPIC accessMode=write
  // Create a pose asset from a Control Rig's controls, as the Pose Library's Create Pose does; a unique name is us
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateControlRigPoseAsset": "mutate", // EPIC accessMode=write
  // Create a selection set with the mirrored control names of an existing set, as the panel's Create Mirror does. 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateMirroredSelectionSet": "mutate", // EPIC accessMode=write
  // Create a selection set from the controls and actors selected in Sequencer, as the Selection Sets panel's Add S
  "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateSelectionSetFromSelection": "mutate", // EPIC accessMode=write
  // Delete every selection set of the focused level sequence, showing their controls first. Raises a script error 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteAllSelectionSets": "mutate", // EPIC accessMode=write
  // Delete anim layer at specified index in the level sequence open in Sequencer. Deleting the last layer above th
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteAnimLayer": "mutate", // EPIC accessMode=write
  // Delete the Key for the Constraint at the specified time, as deleting it in Sequencer does: the child's compens
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteConstraintKey": "mutate", // EPIC accessMode=write
  // Remove every key on controls, as Anim Details' Remove All Keys does: in the section keyed for each control, ea
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteControlRigControlKeys": "mutate", // EPIC accessMode=write
  // Delete the Control Rig Space Key for the Control at the specified time. This will delete any attached Control 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteControlRigSpace": "mutate", // EPIC accessMode=write
  // Remove the links between a binding and every AnimSequence linked to it, on both assets. Raises a script error 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteLinkedAnimSequence": "mutate", // EPIC accessMode=write
  // Remove the linked animation track and its linked animation sequence for the given binding.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteLinkedAnimTrack": "mutate", // EPIC accessMode=write
  // Delete a selection set, showing its controls first. Raises a script error if no level sequence is open in Sequ
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteSelectionSet": "mutate", // EPIC accessMode=write
  // Remove the time warp from a section
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteTimeWarp": "mutate", // EPIC accessMode=write
  // Duplicate anim layer at specified index in the level sequence open in Sequencer
  "ControlRigEditor.ControlRigSequencerEditorLibrary.DuplicateAnimLayer": "mutate", // EPIC accessMode=write
  // Export the binding's animation to an Anim Sequence, using the level sequence focused in Sequencer. Use this in
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportAnimSequenceFromSequencer": "mutate", // EPIC accessMode=write
  // ExportAnimSequenceFromSequencer as an async operation. See ExportAnimSequenceFromSequencer for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportAnimSequenceFromSequencerAsync": "mutate", // EPIC accessMode=write
  // Export the given Control Rig section's animation to an FBX file. Hidden from restricted (UEFN) AI hosts.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportFBXFromControlRigSection": "mutate", // EPIC accessMode=write
  // ExportFBXFromControlRigSection as an async operation. See ExportFBXFromControlRigSection for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportFBXFromControlRigSectionAsync": "mutate", // EPIC accessMode=write
  // Write every selection set of the focused level sequence to a JSON string, as Export Sets does. Raises a script
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportSelectionSetsToJson": "read", // EPIC accessMode=read
  // Extract the controls/objects currently selected in the editor out of the anim layer at the specified index int
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ExtractAnimLayer": "mutate", // EPIC accessMode=write
  // Find the Control Rig tracks in a level sequence whose Control Rig comes from the given reference.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.FindControlRigTracks": "read", // EPIC accessMode=read
  // Find or create a Control Rig track for each Control Rig component on the bound actor Raises, adding no track, 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.FindOrCreateControlRigComponentTrack": "mutate", // EPIC accessMode=write
  // Find or create a Control Rig Track from a Control Rig asset reference. Handles both the class-based and runtim
  "ControlRigEditor.ControlRigSequencerEditorLibrary.FindOrCreateControlRigTrackFromReference": "mutate", // EPIC accessMode=write
  // Frame the viewport on the selected controls. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.FrameSelection": "mutate", // EPIC accessMode=write
  // Get Actors World Transform at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetActorWorldTransform": "read", // EPIC accessMode=read
  // Get Actors World Transforms at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetActorWorldTransforms": "read", // EPIC accessMode=read
  // GetActorWorldTransforms read after the next editor world tick. See GetActorWorldTransforms for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetActorWorldTransformsAsync": "read", // EPIC accessMode=read
  // Get the Animation Mode viewport display preferences. These are editor-wide user preferences and never change s
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimationModeSettings": "read", // EPIC accessMode=read
  // Get the index of an anim layer in the level sequence open in Sequencer
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimLayerIndex": "read", // EPIC accessMode=read
  // Get an anim layer's name, mute, lock, weight, type and keyed state, as the Animation Layers panel shows them. 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimLayerProperties": "read", // EPIC accessMode=read
  // Get the anim layers of the level sequence open in Sequencer. With the default bAddIfDoesNotExist this adds ani
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimLayers": "mutate", // EPIC accessMode=write
  // Get the constraint keys for the specified constraint
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetConstraintKeys": "read", // EPIC accessMode=read
  // Get all constraints for this object, which is described by a transformable handle
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetConstraintsForHandle": "read", // EPIC accessMode=read
  // Get the constraints whose child is an object, identified the same way as in AddConstraintBetween. Raises a scr
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetConstraintsForObject": "read", // EPIC accessMode=read
  // Get the spaces the level editor's space picker lists for a control after its default parent and world, in pick
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigAvailableSpaces": "read", // EPIC accessMode=read
  // Get the controls of a Control Rig in hierarchy order, with each control's value type.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigControls": "read", // EPIC accessMode=read
  // Whether a Control Rig's control shapes are visible in the viewport; with ModuleName, a module of a modular rig
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigControlsVisible": "read", // EPIC accessMode=read
  // Get the control setting overrides on a Control Rig instance, from every override asset linked to it. Raises a 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigOverrides": "read", // EPIC accessMode=read
  // Get the names of the controls stored in a pose asset. Raises a script error if the pose asset is null.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigPoseAssetControls": "read", // EPIC accessMode=read
  // Get the Control Rig track's priority order; tracks with a higher priority evaluate first.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigPriorityOrder": "read", // EPIC accessMode=read
  // Get a control's rotation order in its Control Rig track: its override, if any, and the order the rig asset aut
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigRotationOrder": "read", // EPIC accessMode=read
  // Get all of the control rigs and their bindings in the level sequence
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigs": "read", // EPIC accessMode=read
  // Get which transform and weight channels a Control Rig section animates (its Active Channels).
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigSectionTransformMask": "read", // EPIC accessMode=read
  // Get the control's space keys in time order, each with the space as SetControlRigSpace takes it. Raises a scrip
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigSpaceKeys": "read", // EPIC accessMode=read
  // Get ControlRig Control's World Transform at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigWorldTransform": "read", // EPIC accessMode=read
  // Get ControlRig Control's World Transforms at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigWorldTransforms": "read", // EPIC accessMode=read
  // GetControlRigWorldTransforms read after the next editor world tick. See GetControlRigWorldTransforms for the p
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigWorldTransformsAsync": "read", // EPIC accessMode=read
  // Whether the section animates the given control (true) or has it masked out (false).
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlsMask": "read", // EPIC accessMode=read
  // Get an array per frame of specified control rig values for the specified control names. Use URigHierarchy::Get
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlValues": "read", // EPIC accessMode=read
  // GetControlValues read after the next editor world tick. See GetControlValues for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlValuesAsync": "read", // EPIC accessMode=read
  // Get the key that names a control's default parent, for use as a space in SetControlRigSpace.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetDefaultParentKey": "read", // EPIC accessMode=read
  // Get the controls an FK Control Rig animates, as its Select Bones To Animate dialog shows them checked. Raises 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetFKControlRigActiveControls": "read", // EPIC accessMode=read
  // Get FKControlRig Apply Mode. The rig must be an FK Control Rig.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetFKControlRigApplyMode": "read", // EPIC accessMode=read
  // Get the Anim Sequences linked to a binding, with each link's export settings, as Create Linked Animation Seque
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLinkedAnimSequences": "read", // EPIC accessMode=read
  // Get the linked animation track for the given binding (read-only lookup).
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLinkedAnimTrack": "read", // EPIC accessMode=read
  // Get ControlRig Control's bool value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigBool": "read", // EPIC accessMode=read
  // Get ControlRig Control's bool values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigBools": "read", // EPIC accessMode=read
  // Get ControlRig Control's EulerTransform value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigEulerTransform": "read", // EPIC accessMode=read
  // Get ControlRig Control's EulerTransform values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigEulerTransforms": "read", // EPIC accessMode=read
  // Get ControlRig Control's float value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigFloat": "read", // EPIC accessMode=read
  // Get ControlRig Control's float values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigFloats": "read", // EPIC accessMode=read
  // Get ControlRig Control's integer value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigInt": "read", // EPIC accessMode=read
  // Get ControlRig Control's integer values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigInts": "read", // EPIC accessMode=read
  // Get ControlRig Control's Position value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigPosition": "read", // EPIC accessMode=read
  // Get ControlRig Control's Position values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigPositions": "read", // EPIC accessMode=read
  // Get ControlRig Control's Rotator value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigRotator": "read", // EPIC accessMode=read
  // Get ControlRig Control's Rotator values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigRotators": "read", // EPIC accessMode=read
  // Get ControlRig Control's Scale value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigScale": "read", // EPIC accessMode=read
  // Get ControlRig Control's Scale values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigScales": "read", // EPIC accessMode=read
  // Get ControlRig Control's Transform value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransform": "read", // EPIC accessMode=read
  // Get ControlRig Control's TransformNoScale value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransformNoScale": "read", // EPIC accessMode=read
  // Get ControlRig Control's TransformNoScale values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransformNoScales": "read", // EPIC accessMode=read
  // Get ControlRig Control's Transform values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransforms": "read", // EPIC accessMode=read
  // Get ControlRig Control's Vector2D value at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigVector2D": "read", // EPIC accessMode=read
  // Get ControlRig Control's Vector2D values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigVector2Ds": "read", // EPIC accessMode=read
  // Find or create the linked animation track for the given binding. Same as Configure AutoBake in the UI. Creates
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetOrCreateLinkedAnimTrack": "mutate", // EPIC accessMode=write
  // Get the names of a Control Rig's selected controls.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectedControlRigControls": "read", // EPIC accessMode=read
  // Get the actors selection sets act on in the focused level sequence. Raises a script error if no level sequence
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectionSetActors": "read", // EPIC accessMode=read
  // Get the selection sets of the level sequence focused in Sequencer, sorted by row. Raises a script error if no 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectionSetInfos": "read", // EPIC accessMode=read
  // Get the selection sets of the level sequence open in Sequencer. With the default bAddIfDoesNotExist this adds 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectionSets": "mutate", // EPIC accessMode=write
  // Get SkeletalMeshComponent World Transform at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSkeletalMeshComponentWorldTransform": "read", // EPIC accessMode=read
  // Get SkeletalMeshComponents World Transforms at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSkeletalMeshComponentWorldTransforms": "read", // EPIC accessMode=read
  // GetSkeletalMeshComponentWorldTransforms read after the next editor world tick. See GetSkeletalMeshComponentWor
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSkeletalMeshComponentWorldTransformsAsync": "read", // EPIC accessMode=read
  // Get the space channel for the specified control rig and control name. Spaces always live on the first section.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSpaceChannel": "mutate", // EPIC accessMode=write
  // Get the Control Rigs shown by Animation Mode in the level editor. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetVisibleControlRigs": "read", // EPIC accessMode=read
  // Get the key that names world space, for use as a space in SetControlRigSpace.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.GetWorldSpaceReferenceKey": "read", // EPIC accessMode=read
  // Whether Animation Mode's pose clipboard holds a saved pose. Raises a script error if Animation Mode is not act
  "ControlRigEditor.ControlRigSequencerEditorLibrary.HasControlRigPoseInClipboard": "read", // EPIC accessMode=read
  // Mask out every control on the section so none of them are animated.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.HideAllControls": "mutate", // EPIC accessMode=write
  // Import FBX animation onto a control rig with the specified track and section.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ImportFBXToControlRigTrack": "mutate", // EPIC accessMode=write
  // ImportFBXToControlRigTrack as an async operation. See ImportFBXToControlRigTrack for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ImportFBXToControlRigTrackAsync": "mutate", // EPIC accessMode=write
  // Add the selection sets in a JSON string written by ExportSelectionSetsToJson, as Import Sets does. Raises a sc
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ImportSelectionSetsFromJson": "mutate", // EPIC accessMode=write
  // Invert Input Pose, this will set layered controls rigs to default pose, for non-layered same as ZeroTransforms
  "ControlRigEditor.ControlRigSequencerEditorLibrary.InvertInputPose": "mutate", // EPIC accessMode=write
  // Whether one of Animation Mode's tools is open. Raises a script error if Animation Mode is not active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.IsAnimationModeToolOpen": "read", // EPIC accessMode=read
  // Whether or not the control rig is an FK Control Rig.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.IsFKControlRig": "read", // EPIC accessMode=read
  // Whether or not the control rig is an Layered Control Rig.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.IsLayeredControlRig": "read", // EPIC accessMode=read
  // Isolate or un-isolate the linked animation track for the given binding. When isolating: enables the linked ani
  "ControlRigEditor.ControlRigSequencerEditorLibrary.IsolateLinkedAnimTrack": "mutate", // EPIC accessMode=write
  // Isolate or un-isolate the linked animation tracks for every binding in the focused Sequencer movie scene.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.IsolateLinkedAnimTracksForAllBindings": "mutate", // EPIC accessMode=write
  // Key a Control Rig section's Weight channel at a frame, enabling the Weight channel if the section's mask leave
  "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControlRigSectionWeight": "mutate", // EPIC accessMode=write
  // Key every control on a Control Rig section, or only the selected controls, to its zero value at a frame.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControlRigSectionZeroValue": "mutate", // EPIC accessMode=write
  // Key the controls on the section at the current Sequencer time, using the values their channels evaluate to at 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControls": "mutate", // EPIC accessMode=write
  // Key the controls on the section at each of the specified times, using the values the section evaluates to at t
  "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControlsAtFrames": "mutate", // EPIC accessMode=write
  // Key a selection set's controls and actors on the actors sets act on, at the current Sequencer time. Raises a s
  "ControlRigEditor.ControlRigSequencerEditorLibrary.KeySelectionSet": "mutate", // EPIC accessMode=write
  // Load anim sequence into this control rig section. For the load dialog's Smart Reduce or Bake reduce types, pas
  "ControlRigEditor.ControlRigSequencerEditorLibrary.LoadAnimSequenceIntoControlRigSectionWithRange": "mutate", // EPIC accessMode=write
  // LoadAnimSequenceIntoControlRigSectionWithRange as an async operation. See LoadAnimSequenceIntoControlRigSectio
  "ControlRigEditor.ControlRigSequencerEditorLibrary.LoadAnimSequenceIntoControlRigSectionWithRangeAsync": "mutate", // EPIC accessMode=write
  // Make a Control Rig reference from an asset or class path, for use with FindOrCreateControlRigTrackFromReferenc
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MakeControlRigReference": "read", // EPIC accessMode=read
  // Merge specified anim layers into one layer. Will merge onto the anim layer with the lowest index
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MergeAnimLayersWithSettings": "mutate", // EPIC accessMode=write
  // MergeAnimLayersWithSettings as an async operation. See MergeAnimLayersWithSettings for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MergeAnimLayersWithSettingsAsync": "mutate", // EPIC accessMode=write
  // Set the selected controls to the mirror of their counterparts' pose. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MirrorSelectedControls": "mutate", // EPIC accessMode=write
  // Set the unselected counterparts to the mirror of the selected controls' pose. Requires Animation Mode to be ac
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MirrorUnselectedControls": "mutate", // EPIC accessMode=write
  // Move the constraint active key in the current open Sequencer, as dragging it does: the child's compensation ke
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MoveConstraintKey": "mutate", // EPIC accessMode=write
  // Move the Control Rig Space Key for the Control at the specified time to the new time. This will also move any 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.MoveControlRigSpace": "mutate", // EPIC accessMode=write
  // Opens the Control Rig Pose Library panel. Requires Animation Mode to be active.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.OpenPoseLibraryTab": "mutate", // EPIC accessMode=write
  // Paste a pose asset onto a Control Rig's selected controls, as the Pose Library's Paste Pose and Paste Mirror P
  "ControlRigEditor.ControlRigSequencerEditorLibrary.PasteControlRigPoseAsset": "mutate", // EPIC accessMode=write
  // Paste the pose in Animation Mode's pose clipboard onto the selected controls, as Paste Pose and Paste Mirror P
  "ControlRigEditor.ControlRigSequencerEditorLibrary.PasteControlRigPoseFromClipboard": "mutate", // EPIC accessMode=write
  // Export the binding's animation again into every Anim Sequence linked to it, each with its link's settings, as 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RebakeLinkedAnimSequences": "mutate", // EPIC accessMode=write
  // RebakeLinkedAnimSequences as an async operation; the result holds the number of Anim Sequences re-baked. See R
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RebakeLinkedAnimSequencesAsync": "mutate", // EPIC accessMode=write
  // Recreate a control rig track with new settings
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RecreateControlRigWithNewSettings": "mutate", // EPIC accessMode=write
  // Remove a constraint, as deleting it in the Constraints panel does. With bCompensate the child keeps its world 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveConstraint": "mutate", // EPIC accessMode=write
  // Remove a space from a control's space picker list, as the level editor's Delete does; a space the rig asset li
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveControlRigAvailableSpace": "mutate", // EPIC accessMode=write
  // Remove a control setting override from the Control Rig's override assets, as Anim Details' Clear Override does
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveControlRigOverride": "mutate", // EPIC accessMode=write
  // Remove the controls and objects selected in Sequencer from an anim layer, as the panel's Remove Selected does.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveSelectedFromAnimLayer": "mutate", // EPIC accessMode=write
  // Remove the controls and actors selected in Sequencer from a selection set. Raises a script error if no level s
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveSelectionFromSelectionSet": "mutate", // EPIC accessMode=write
  // Rename the Control Rig Channels in Sequencer to the specified new control names, which should be present on th
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RenameControlRigControlChannels": "mutate", // EPIC accessMode=write
  // Rename a control stored in a pose asset, for pasting onto a rig with other control names, as the Pose Library'
  "ControlRigEditor.ControlRigSequencerEditorLibrary.RenameControlRigPoseAssetControl": "mutate", // EPIC accessMode=write
  // Move Anim Layer to new index, this will also change evaluation order
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ReorderAnimLayers": "mutate", // EPIC accessMode=write
  // Replace the Control Rig on the track with a new Control Rig. Note it will now be owned by the track. When the 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ReplaceControlRig": "mutate", // EPIC accessMode=write
  // Replace the Control Rig on a Control Rig track with a new rig created from a Control Rig class or asset, as Fi
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ReplaceControlRigFromReference": "mutate", // EPIC accessMode=write
  // Set controls to their initial values at the current Sequencer time, as Anim Details' Reset To Default does; th
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ResetControlRigControlsToDefault": "mutate", // EPIC accessMode=write
  // Save the pose of the selected controls to Animation Mode's pose clipboard, as Save Pose does; with bWholePose,
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SaveControlRigPoseToClipboard": "mutate", // EPIC accessMode=write
  // Select every control on the Control Rigs in Animation Mode. Raises a script error if Animation Mode is not act
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectAllControlRigControls": "mutate", // EPIC accessMode=write
  // Select the controls and objects in an anim layer, as the panel's Select Objects does. Raises a script error if
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectAnimLayerObjects": "mutate", // EPIC accessMode=write
  // Select an anim layer's weight channels in Sequencer, as the panel's weight button does. Raises a script error 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectAnimLayerWeightChannel": "mutate", // EPIC accessMode=write
  // Select or deselect controls on a Control Rig. Selection drives the selection-based functions such as TweenCont
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectControlRigControls": "mutate", // EPIC accessMode=write
  // Select the controls stored in a pose asset, as the Pose Library's Select Controls does. Raises a script error 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectControlRigPoseAssetControls": "mutate", // EPIC accessMode=write
  // Select the controls in Animation Mode's pose clipboard, as Select Pose and Select Mirror Pose do. Raises a scr
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectControlRigPoseClipboardControls": "mutate", // EPIC accessMode=write
  // Replace the selection with the mirrored counterparts of the selected controls. Requires Animation Mode to be a
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectMirroredControls": "mutate", // EPIC accessMode=write
  // Select or deselect a selection set's controls and actors on the actors sets act on, as clicking the set does. 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectSelectionSet": "mutate", // EPIC accessMode=write
  // Key an actor's world transform at a frame in the level sequence focused in Sequencer. The key is expressed in 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetActorWorldTransform": "mutate", // EPIC accessMode=write
  // Key an actor's world transforms at frames in the level sequence focused in Sequencer, adding a Transform track
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetActorWorldTransforms": "mutate", // EPIC accessMode=write
  // SetActorWorldTransforms as an async operation. See SetActorWorldTransforms for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetActorWorldTransformsAsync": "mutate", // EPIC accessMode=write
  // Set every Animation Mode preference and save them to the user's config. Open viewports update immediately. To 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimationModeSettings": "mutate", // EPIC accessMode=write
  // Open or close one of Animation Mode's tools, as its toolbar button or command does. Raises a script error if A
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimationModeToolOpen": "mutate", // EPIC accessMode=write
  // Key an anim layer's current values at the current Sequencer time, on the layer's selected controls and objects
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimLayerKey": "mutate", // EPIC accessMode=write
  // Set a passthrough key on an anim layer at the current Sequencer time, on the layer's selected controls and obj
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimLayerPassthroughKey": "mutate", // EPIC accessMode=write
  // Set an anim layer's name, mute, lock, weight, type and keyed state; only the properties that differ from the l
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimLayerProperties": "mutate", // EPIC accessMode=write
  // Set the constraint active key in the current open Sequencer
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetConstraintActiveKey": "mutate", // EPIC accessMode=write
  // Set the FK Control Rig to apply mode
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigApplyMode": "mutate", // EPIC accessMode=write
  // Reorder a control's space picker list, as moving spaces up and down in the level editor does. Raises a script 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigAvailableSpacesOrder": "mutate", // EPIC accessMode=write
  // Show or hide a Control Rig's control shapes in the viewport, as Toggle Controls Visibility does for the select
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigControlsVisible": "mutate", // EPIC accessMode=write
  // Convert the control rig track into absolute or layered rig. This is destructive: non-FK rigs reset their contr
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigLayeredMode": "mutate", // EPIC accessMode=write
  // Override a control setting on a Control Rig instance, as Anim Details' override view does: with an empty Value
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigOverride": "mutate", // EPIC accessMode=write
  // Set the Control Rig track's priority order; tracks with a higher priority evaluate first. Raises a script erro
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigPriorityOrder": "mutate", // EPIC accessMode=write
  // Override a control's rotation order, as the Anim Details rotation order dropdown does: its keys are re-baked i
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigRotationOrderOverride": "mutate", // EPIC accessMode=write
  // Set which transform and weight channels a Control Rig section animates (its Active Channels). Disabled channel
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigSectionTransformMask": "mutate", // EPIC accessMode=write
  // Key the Control Rig Space for the Control at the specified time, compensating so the control keeps its world t
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigSpace": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's World Transform at a specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigWorldTransform": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's World Transforms at a specific times.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigWorldTransforms": "mutate", // EPIC accessMode=write
  // Set whether the section animates the given controls (bVisible true) or masks them out (false).
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlsMask": "mutate", // EPIC accessMode=write
  // Set whether an FK Control Rig animates controls, as its Select Bones To Animate dialog does: inactive controls
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetFKControlRigControlsActive": "mutate", // EPIC accessMode=write
  // Set a linked Anim Sequence's export settings, as Edit in Sequencer's Modify Level Sequence Link Settings does;
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLinkedAnimSequenceSettings": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's bool value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigBool": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's bool values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigBools": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's EulerTransform value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigEulerTransform": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's EulerTransform values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigEulerTransforms": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's float value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigFloat": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's float values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigFloats": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's int value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigInt": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's int values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigInts": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Position value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigPosition": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Position values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigPositions": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Rotator value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigRotator": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Rotator values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigRotators": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Scale value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigScale": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Scale values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigScales": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Transform value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransform": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's TransformNoScale value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransformNoScale": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's TransformNoScale values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransformNoScales": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Transform values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransforms": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Vector2D value at specific time
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigVector2D": "mutate", // EPIC accessMode=write
  // Set ControlRig Control's Vector2D values at specific times
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigVector2Ds": "mutate", // EPIC accessMode=write
  // Mark an actor active or inactive for selection sets, as its checkbox in the panel does. Ignored while sets use
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetActorActive": "mutate", // EPIC accessMode=write
  // Set a selection set's name, color and row. Only the fields that differ are applied, in one undo step. Raises a
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetProperties": "mutate", // EPIC accessMode=write
  // Set whether selection sets act on the actors selected in the viewport, or on the actors marked active. Raises 
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetsUseViewportSelection": "mutate", // EPIC accessMode=write
  // Show, hide or isolate a selection set's controls on the actors selected in the viewport, as the set's Hide, Sh
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetVisibility": "mutate", // EPIC accessMode=write
  // Unmask every control on the section so all of them are animated.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ShowAllControls": "mutate", // EPIC accessMode=write
  // Show every control on an actor's Control Rigs, as the actor's Show All in the Selection Sets panel does. Not u
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ShowAllSelectionSetActorControls": "mutate", // EPIC accessMode=write
  // Perform the Smart Reduce filter over a Control Rig section in the level sequence open in Sequencer.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SmartReduceSection": "mutate", // EPIC accessMode=write
  // Peform a Snap operation to snap the children to the parent.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SnapControlRig": "mutate", // EPIC accessMode=write
  // SnapControlRig as an async operation. See SnapControlRig for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SnapControlRigAsync": "mutate", // EPIC accessMode=write
  // Perform compensation for any spaces at the specified time for the specified control rig
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SpaceCompensate": "mutate", // EPIC accessMode=write
  // Perform compensation for all spaces for the specified control rig
  "ControlRigEditor.ControlRigSequencerEditorLibrary.SpaceCompensateAll": "mutate", // EPIC accessMode=write
  // Move, rotate or scale Control Rig controls about a pivot, keying them at each frame. At every frame a control'
  "ControlRigEditor.ControlRigSequencerEditorLibrary.TransformControlsAboutPivot": "mutate", // EPIC accessMode=write
  // TransformControlsAboutPivot as an async operation. See TransformControlsAboutPivot for the parameters.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.TransformControlsAboutPivotAsync": "mutate", // EPIC accessMode=write
  // Perform a Tween operation on the Control Rig's selected controls at the current Sequencer time.
  "ControlRigEditor.ControlRigSequencerEditorLibrary.TweenControlRig": "mutate", // EPIC accessMode=write
  // Remove the link between a binding and one linked Anim Sequence, on both assets. Raises a script error if the b
  "ControlRigEditor.ControlRigSequencerEditorLibrary.UnlinkAnimSequence": "mutate", // EPIC accessMode=write
  // Replace a pose asset's pose with a Control Rig's current values, as the Pose Library's Update Pose does. Raise
  "ControlRigEditor.ControlRigSequencerEditorLibrary.UpdateControlRigPoseAsset": "mutate", // EPIC accessMode=write
  // Reset Transforms to default values, usually zero values but which may not be zero if initial values are presen
  "ControlRigEditor.ControlRigSequencerEditorLibrary.ZeroTransforms": "mutate", // EPIC accessMode=write

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
  // Runs a node function, as if its details panel button was pressed. Undoable. On a standalone graph, the functio
  "DataflowAgent.DataflowAgentToolset.ExecuteNodeFunction": "mutate", // EPIC accessMode=write
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

  /* ── DataflowEditor.DataflowScriptingSubsystem ─────────────────── */
  // 
  "DataflowEditor.DataflowScriptingSubsystem.AddComment": "mutate", // EPIC accessMode=write
  // Adds a sub-graph. Add its "FDataflowSubGraphInputNode" and "FDataflowSubGraphOutputNode" nodes with AddNode, i
  "DataflowEditor.DataflowScriptingSubsystem.AddGraph": "mutate", // EPIC accessMode=write
  // Adds a binding to the signature of a sub-graph: an output pin of its input node (Direction "Input") or an inpu
  "DataflowEditor.DataflowScriptingSubsystem.AddGraphBinding": "mutate", // EPIC accessMode=write
  // Links an output pin to an input pin, replacing the input pin's existing link if any, as the graph editor does:
  "DataflowEditor.DataflowScriptingSubsystem.AddLink": "mutate", // EPIC accessMode=write
  // Adds a pin to a node that supports it (e.g. the input or output node of a sub-graph) for the type of SourcePin
  "DataflowEditor.DataflowScriptingSubsystem.AddLinkWithNewPin": "mutate", // EPIC accessMode=write
  // Adds a node. Use AddComment for comments.
  "DataflowEditor.DataflowScriptingSubsystem.AddNode": "mutate", // EPIC accessMode=write
  // Adds pins to a node whose FDataflowNodeInfo::bCanAddPins is true, as its "Add Pin" menu does.
  "DataflowEditor.DataflowScriptingSubsystem.AddNodePins": "mutate", // EPIC accessMode=write
  // 
  "DataflowEditor.DataflowScriptingSubsystem.AddVariable": "mutate", // EPIC accessMode=write
  // Makes the asset use the preview Blueprint's default value of a variable again.
  "DataflowEditor.DataflowScriptingSubsystem.ClearPreviewVariable": "mutate", // EPIC accessMode=write
  // Makes the asset use the graph's value of a variable again.
  "DataflowEditor.DataflowScriptingSubsystem.ClearVariableOverride": "mutate", // EPIC accessMode=write
  // Creates an asset generated by an embedded Dataflow graph (e.g. a cloth asset or a geometry collection). Get th
  "DataflowEditor.DataflowScriptingSubsystem.CreateAssetWithDataflow": "mutate", // EPIC accessMode=write
  // Creates a Dataflow asset with an empty main graph.
  "DataflowEditor.DataflowScriptingSubsystem.CreateDataflow": "mutate", // EPIC accessMode=write
  // Runs a function of a node, as pressing its button in the details panel does (e.g. "Import"). On a standalone g
  "DataflowEditor.DataflowScriptingSubsystem.ExecuteNodeFunction": "mutate", // EPIC accessMode=write
  // Returns the Dataflow graph an asset is generated from, embedded or referenced (e.g. the graph of a cloth asset
  "DataflowEditor.DataflowScriptingSubsystem.GetDataflowFromAsset": "read", // EPIC accessMode=read
  // Returns the graphs and variables of a Dataflow asset.
  "DataflowEditor.DataflowScriptingSubsystem.GetDataflowInfo": "read", // EPIC accessMode=read
  // Returns what an asset generated by a Dataflow graph keeps besides the graph: its terminal node and the values 
  "DataflowEditor.DataflowScriptingSubsystem.GetDataflowInstanceInfo": "read", // EPIC accessMode=read
  // Graphs
  "DataflowEditor.DataflowScriptingSubsystem.GetGraphInfo": "read", // EPIC accessMode=read
  // Nodes
  "DataflowEditor.DataflowScriptingSubsystem.GetNodeInfo": "read", // EPIC accessMode=read
  // Returns a registered node type with the pins and editable properties of a new node of that type.
  "DataflowEditor.DataflowScriptingSubsystem.GetNodeTypeInfo": "read", // EPIC accessMode=read
  // Lists the asset classes whose assets can have a Dataflow graph: those generated by one (e.g. a cloth asset), a
  "DataflowEditor.DataflowScriptingSubsystem.ListDataflowAssetClasses": "read", // EPIC accessMode=read
  // Lists the templates registered for an asset class, including those registered for its parent classes.
  "DataflowEditor.DataflowScriptingSubsystem.ListDataflowTemplates": "read", // EPIC accessMode=read
  // Lists the node types that can be added, including the nodes running the sub-graphs and reading the variables o
  "DataflowEditor.DataflowScriptingSubsystem.ListNodeTypes": "read", // EPIC accessMode=read
  // 
  "DataflowEditor.DataflowScriptingSubsystem.RemoveAllLinks": "mutate", // EPIC accessMode=write
  // Removes a sub-graph and its nodes.
  "DataflowEditor.DataflowScriptingSubsystem.RemoveGraph": "mutate", // EPIC accessMode=write
  // Removes a binding from the signature of a sub-graph, with its pins and links.
  "DataflowEditor.DataflowScriptingSubsystem.RemoveGraphBinding": "mutate", // EPIC accessMode=write
  // Remove Link
  "DataflowEditor.DataflowScriptingSubsystem.RemoveLink": "mutate", // EPIC accessMode=write
  // Removes the last added pins of a node whose FDataflowNodeInfo::bCanRemovePins is true, with their links.
  "DataflowEditor.DataflowScriptingSubsystem.RemoveNodePins": "mutate", // EPIC accessMode=write
  // Removes nodes and comments, and their links, as one undo step. Nothing is removed if any path is invalid.
  "DataflowEditor.DataflowScriptingSubsystem.RemoveNodes": "mutate", // EPIC accessMode=write
  // Remove Variable
  "DataflowEditor.DataflowScriptingSubsystem.RemoveVariable": "mutate", // EPIC accessMode=write
  // Renames a sub-graph, and the nodes running it. The main graph has no name.
  "DataflowEditor.DataflowScriptingSubsystem.RenameGraph": "mutate", // EPIC accessMode=write
  // Renames a binding of the signature of a sub-graph, as its details panel does: its pins keep their links, also 
  "DataflowEditor.DataflowScriptingSubsystem.RenameGraphBinding": "mutate", // EPIC accessMode=write
  // Renames a node, as editing its title in the graph editor does. What refers to the node by name keeps the old n
  "DataflowEditor.DataflowScriptingSubsystem.RenameNode": "mutate", // EPIC accessMode=write
  // Renames a variable, and updates the nodes reading it.
  "DataflowEditor.DataflowScriptingSubsystem.RenameVariable": "mutate", // EPIC accessMode=write
  // Set Comment
  "DataflowEditor.DataflowScriptingSubsystem.SetComment": "mutate", // EPIC accessMode=write
  // Sets how a comment shows, as its details panel does.
  "DataflowEditor.DataflowScriptingSubsystem.SetCommentStyle": "mutate", // EPIC accessMode=write
  // Gives an asset a new embedded Dataflow graph, copied from a template, replacing the graph it had. An asset of 
  "DataflowEditor.DataflowScriptingSubsystem.SetDataflowFromTemplate": "mutate", // EPIC accessMode=write
  // 
  "DataflowEditor.DataflowScriptingSubsystem.SetDataflowTerminal": "mutate", // EPIC accessMode=write
  // Changes the type of a binding of the signature of a sub-graph, as its details panel does: the links of its pin
  "DataflowEditor.DataflowScriptingSubsystem.SetGraphBindingDataType": "mutate", // EPIC accessMode=write
  // 
  "DataflowEditor.DataflowScriptingSubsystem.SetGraphForEach": "mutate", // EPIC accessMode=write
  // 
  "DataflowEditor.DataflowScriptingSubsystem.SetNodePosition": "mutate", // EPIC accessMode=write
  // Sets properties of a node, as listed in FDataflowNodeInfo::Properties, as one undo step. The node updates as w
  "DataflowEditor.DataflowScriptingSubsystem.SetNodeProperties": "mutate", // EPIC accessMode=write
  // Sets the type of an unlinked output pin of any type, as its "Convert to" menu does (e.g. the Result of a math 
  "DataflowEditor.DataflowScriptingSubsystem.SetPinType": "mutate", // EPIC accessMode=write
  // Sets the Blueprint actor class the Dataflow editor previews the graph with (e.g. a character wearing a cloth a
  "DataflowEditor.DataflowScriptingSubsystem.SetPreviewBlueprint": "mutate", // EPIC accessMode=write
  // Sets, for an asset, a variable of the Blueprint its graph is previewed with (e.g. "SkeletalMesh", the mesh a c
  "DataflowEditor.DataflowScriptingSubsystem.SetPreviewVariable": "mutate", // EPIC accessMode=write
  // Set Variable Data Type
  "DataflowEditor.DataflowScriptingSubsystem.SetVariableDataType": "mutate", // EPIC accessMode=write
  // Set Variable Default Value
  "DataflowEditor.DataflowScriptingSubsystem.SetVariableDefaultValue": "mutate", // EPIC accessMode=write
  // Gives a graph variable a value of its own for an asset generated by the graph.
  "DataflowEditor.DataflowScriptingSubsystem.SetVariableOverride": "mutate", // EPIC accessMode=write

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

  /* ── editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools ─── */
  // Diffs the workspace version of a Blueprint against a depot revision and writes HTML.
  "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools.diff_blueprint_html": "mutate", // EPIC accessMode=write
  // Diffs two depot revisions of a Blueprint and writes HTML.
  "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools.diff_blueprint_revisions_html": "mutate", // EPIC accessMode=write
  // Writes one HTML diff per Blueprint asset that a changelist modifies. Works for submitted and shelved changelis
  "editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools.diff_changelist_blueprints_html": "mutate", // EPIC accessMode=write

  /* ── editor_toolset.toolsets.blueprint.BlueprintDslTools ───────── */
  // Returns a unified text diff between two Blueprint assets.
  "editor_toolset.toolsets.blueprint.BlueprintDslTools.diff": "read", // EPIC accessMode=read
  // Returns the full syntax reference for write_graph_dsl. Call this before using write_graph_dsl for the first ti
  "editor_toolset.toolsets.blueprint.BlueprintDslTools.get_graph_dsl_docs": "read", // EPIC accessMode=read
  // Reads a Blueprint graph and returns a DSL script. The returned code uses the same syntax that write_graph_dsl 
  "editor_toolset.toolsets.blueprint.BlueprintDslTools.read_graph_dsl": "read", // EPIC accessMode=read
  // Makes a Blueprint graph match a DSL script and compiles the Blueprint. Statements whose DSL is unchanged from 
  "editor_toolset.toolsets.blueprint.BlueprintDslTools.write_graph_dsl": "mutate", // EPIC accessMode=write

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

  /* ── editor_toolset.toolsets.python_scripting.PythonScriptingTools  */
  // Runs a Python script in the editor's interpreter on the game thread. IMPORTANT: Call `read_me` once per conver
  "editor_toolset.toolsets.python_scripting.PythonScriptingTools.execute_python": "unknown", // EPIC accessMode=execute
  // Returns the documentation of a registered tool, or of a type, function, method, or property in the `unreal` Py
  "editor_toolset.toolsets.python_scripting.PythonScriptingTools.get_symbol": "read", // EPIC accessMode=read
  // Usage instructions for execute_python. Call this once per conversation before using execute_python. Explains t
  "editor_toolset.toolsets.python_scripting.PythonScriptingTools.read_me": "read", // EPIC accessMode=read
  // Searches the registered tools and the `unreal` Python API for types, functions, methods, and properties. Searc
  "editor_toolset.toolsets.python_scripting.PythonScriptingTools.search_symbols": "read", // EPIC accessMode=read

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

  /* ── EditorToolset.ActorToolset ────────────────────────────────── */
  // Adds a component to an actor instance or blueprint.
  "EditorToolset.ActorToolset.AddComponent": "mutate", // EPIC accessMode=write
  // Adds an Actor tag to an actor.
  "EditorToolset.ActorToolset.AddTag": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two actors. Compares the actors' classes, top-level properties, and
  "EditorToolset.ActorToolset.Diff": "read", // EPIC accessMode=read
  // Returns the bounding box of an actor.
  "EditorToolset.ActorToolset.GetActorBounds": "read", // EPIC accessMode=read
  // Returns the position, rotation, and scale of an actor.
  "EditorToolset.ActorToolset.GetActorTransform": "read", // EPIC accessMode=read
  // Returns a JSON-safe canonical form of an actor (or a Blueprint's CDO) for diffing. Captures the actor's class,
  "EditorToolset.ActorToolset.GetCanonicalActorState": "read", // EPIC accessMode=read
  // Returns the actor that owns the specified component.
  "EditorToolset.ActorToolset.GetComponentActor": "read", // EPIC accessMode=read
  // Returns the components that an actor contains.
  "EditorToolset.ActorToolset.GetComponents": "read", // EPIC accessMode=read
  // Returns the position, rotation, and scale of a scene component.
  "EditorToolset.ActorToolset.GetComponentTransform": "read", // EPIC accessMode=read
  // Returns the actor's human friendly name as it appears in the editor.
  "EditorToolset.ActorToolset.GetLabel": "read", // EPIC accessMode=read
  // Returns the parent component that this component is attached to, if any.
  "EditorToolset.ActorToolset.GetParentComponent": "read", // EPIC accessMode=read
  // Returns the root component of an actor, if any.
  "EditorToolset.ActorToolset.GetRootComponent": "read", // EPIC accessMode=read
  // Returns the list of Actor tags on an actor.
  "EditorToolset.ActorToolset.GetTags": "read", // EPIC accessMode=read
  // Returns whether an actor has a specific Actor tag.
  "EditorToolset.ActorToolset.HasTag": "read", // EPIC accessMode=read
  // Rotates an actor so its forward vector points at a world-space position.
  "EditorToolset.ActorToolset.LookAt": "mutate", // EPIC accessMode=write
  // Removes a component from an actor instance or blueprint.
  "EditorToolset.ActorToolset.RemoveComponent": "mutate", // EPIC accessMode=write
  // Removes an Actor tag from an actor.
  "EditorToolset.ActorToolset.RemoveTag": "mutate", // EPIC accessMode=write
  // Updates the position, rotation, and/or scale of an actor. Anything not provided keeps its current value.
  "EditorToolset.ActorToolset.SetActorTransform": "mutate", // EPIC accessMode=write
  // Updates the position, rotation, and/or scale of a scene component. Anything not provided keeps its current val
  "EditorToolset.ActorToolset.SetComponentTransform": "mutate", // EPIC accessMode=write
  // Sets the human-friendly name of the actor.
  "EditorToolset.ActorToolset.SetLabel": "mutate", // EPIC accessMode=write
  // Sets the parent for the specified scene component. For blueprint actors, passing a component as the parent of 
  "EditorToolset.ActorToolset.SetParentComponent": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.AssetToolset ────────────────────────────────── */
  // Adds the given User Asset Tags to an asset. Pass tag names without the UAT. prefix; the prefix is added intern
  "EditorToolset.AssetToolset.AddUserAssetTags": "mutate", // EPIC accessMode=write
  // Creates a folder at the specified path.
  "EditorToolset.AssetToolset.CreateFolder": "mutate", // EPIC accessMode=write
  // Deletes an asset or folder.
  "EditorToolset.AssetToolset.Delete": "mutate", // EPIC accessMode=write
  // Makes a copy of a folder or asset.
  "EditorToolset.AssetToolset.Duplicate": "mutate", // EPIC accessMode=write
  // Determines if a folder or asset exists.
  "EditorToolset.AssetToolset.Exists": "read", // EPIC accessMode=read
  // Searches the project for assets that match specific criteria. Only considers assets saved to disk; an asset th
  "EditorToolset.AssetToolset.FindAssets": "read", // EPIC accessMode=read
  // Fixes up object redirectors at or under a path, rewiring the assets that reference them to point at each redir
  "EditorToolset.AssetToolset.FixUpRedirectors": "mutate", // EPIC accessMode=write
  // Gets the class of an asset.
  "EditorToolset.AssetToolset.GetAssetClass": "read", // EPIC accessMode=read
  // Returns the known content-browser roots in the current project.
  "EditorToolset.AssetToolset.GetAssetContentRoots": "read", // EPIC accessMode=read
  // Gets the asset registry tags for an asset.
  "EditorToolset.AssetToolset.GetAssetTags": "read", // EPIC accessMode=read
  // Lists assets that the specified asset depends on.
  "EditorToolset.AssetToolset.GetDependencies": "read", // EPIC accessMode=read
  // Gets the metadata tags for an asset.
  "EditorToolset.AssetToolset.GetMetadataTags": "read", // EPIC accessMode=read
  // Lists assets that reference the specified asset.
  "EditorToolset.AssetToolset.GetReferencers": "read", // EPIC accessMode=read
  // Gets the User Asset Tags on an asset. User Asset Tags are user-authored labels stored in the asset's package m
  "EditorToolset.AssetToolset.GetUserAssetTags": "read", // EPIC accessMode=read
  // Converts internal asset/folder paths to their Content Browser virtual path. In most projects this is the same 
  "EditorToolset.AssetToolset.GetVirtualPaths": "read", // EPIC accessMode=read
  // Checks whether an asset has unsaved changes.
  "EditorToolset.AssetToolset.IsDirty": "read", // EPIC accessMode=read
  // Lists the folders contained within a folder.
  "EditorToolset.AssetToolset.ListFolders": "read", // EPIC accessMode=read
  // Loads an asset from the project.
  "EditorToolset.AssetToolset.LoadAsset": "read", // EPIC accessMode=read
  // Moves or renames an asset or folder.
  "EditorToolset.AssetToolset.Move": "mutate", // EPIC accessMode=write
  // Reloads an asset from disk, discarding any unsaved in-memory changes.
  "EditorToolset.AssetToolset.ReloadAsset": "mutate", // EPIC accessMode=write
  // Removes the given User Asset Tags from an asset. Pass tag names without the UAT. prefix.
  "EditorToolset.AssetToolset.RemoveUserAssetTags": "mutate", // EPIC accessMode=write
  // Saves an asset to disk.
  "EditorToolset.AssetToolset.SaveAsset": "mutate", // EPIC accessMode=write
  // Sets or removes metadata tags on an asset.
  "EditorToolset.AssetToolset.UpdateMetadataTags": "mutate", // EPIC accessMode=write
  // Validates the given assets and returns the errors and warnings found.
  "EditorToolset.AssetToolset.ValidateAssets": "read", // EPIC accessMode=read

  /* ── EditorToolset.CurveAtlasToolset ───────────────────────────── */
  // Appends a gradient to an atlas as a new final row.
  "EditorToolset.CurveAtlasToolset.AddGradient": "mutate", // EPIC accessMode=write
  // Creates a new curve atlas asset containing no gradients.
  "EditorToolset.CurveAtlasToolset.CreateAtlas": "mutate", // EPIC accessMode=write
  // Creates a new linear color gradient asset, seeded with an opaque black-to-white ramp.
  "EditorToolset.CurveAtlasToolset.CreateGradient": "mutate", // EPIC accessMode=write
  // Returns the alpha stops of a gradient in time order. Only keys with derived tangents form stops; GetChannelKey
  "EditorToolset.CurveAtlasToolset.GetAlphaStops": "read", // EPIC accessMode=read
  // Returns the raw keys of one channel of a gradient, including tangents.
  "EditorToolset.CurveAtlasToolset.GetChannelKeys": "read", // EPIC accessMode=read
  // Returns the color stops of a gradient in time order. Only gradients whose red, green and blue curves share key
  "EditorToolset.CurveAtlasToolset.GetColorStops": "read", // EPIC accessMode=read
  // Returns the gradients of an atlas in row order.
  "EditorToolset.CurveAtlasToolset.GetGradients": "read", // EPIC accessMode=read
  // Removes a gradient from an atlas, shifting later rows up to fill the gap.
  "EditorToolset.CurveAtlasToolset.RemoveGradient": "mutate", // EPIC accessMode=write
  // Evaluates a gradient with the asset's hue, saturation, brightness, vibrance and alpha-range adjustments applie
  "EditorToolset.CurveAtlasToolset.SampleGradient": "read", // EPIC accessMode=read
  // Replaces the alpha stops of a gradient, leaving its color keys untouched.
  "EditorToolset.CurveAtlasToolset.SetAlphaStops": "mutate", // EPIC accessMode=write
  // Replaces the raw keys of one channel of a gradient, leaving the other channels untouched. Tangents of keys set
  "EditorToolset.CurveAtlasToolset.SetChannelKeys": "mutate", // EPIC accessMode=write
  // Replaces the color stops of a gradient, leaving its alpha keys untouched.
  "EditorToolset.CurveAtlasToolset.SetColorStops": "mutate", // EPIC accessMode=write
  // Replaces the gradients of an atlas, rebuilding its texture once.
  "EditorToolset.CurveAtlasToolset.SetGradients": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.CurveTableToolset ───────────────────────────── */
  // Adds a key to a row.
  "EditorToolset.CurveTableToolset.AddKey": "mutate", // EPIC accessMode=write
  // Adds a new row to the curve table with an optional default value.
  "EditorToolset.CurveTableToolset.AddRow": "mutate", // EPIC accessMode=write
  // Creates a new CurveTable asset.
  "EditorToolset.CurveTableToolset.Create": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two CurveTable assets.
  "EditorToolset.CurveTableToolset.Diff": "read", // EPIC accessMode=read
  // Returns all keys for a row.
  "EditorToolset.CurveTableToolset.GetKeys": "read", // EPIC accessMode=read
  // Imports a file from disk as a CurveTable asset. The file's first column is the row name; subsequent columns ar
  "EditorToolset.CurveTableToolset.ImportFile": "mutate", // EPIC accessMode=write
  // Lists the names of all rows in the curve table.
  "EditorToolset.CurveTableToolset.ListRows": "read", // EPIC accessMode=read
  // Removes a row from the curve table.
  "EditorToolset.CurveTableToolset.RemoveRow": "mutate", // EPIC accessMode=write
  // Renames a row in the curve table.
  "EditorToolset.CurveTableToolset.RenameRow": "mutate", // EPIC accessMode=write
  // Replaces all keys in a row with the provided list.
  "EditorToolset.CurveTableToolset.SetKeys": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.DataAssetToolset ────────────────────────────── */
  // Creates a new DataAsset asset in the project.
  "EditorToolset.DataAssetToolset.Create": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two DataAsset assets.
  "EditorToolset.DataAssetToolset.Diff": "read", // EPIC accessMode=read

  /* ── EditorToolset.DataTableToolset ────────────────────────────── */
  // Adds new rows with default values to the data table.
  "EditorToolset.DataTableToolset.AddRows": "mutate", // EPIC accessMode=write
  // Creates a new DataTable asset with the specified column schema.
  "EditorToolset.DataTableToolset.Create": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two DataTable assets.
  "EditorToolset.DataTableToolset.Diff": "read", // EPIC accessMode=read
  // Returns the column values for one or more rows as a JSON string.
  "EditorToolset.DataTableToolset.GetRows": "read", // EPIC accessMode=read
  // Returns the column schema of the data table as a JSON string.
  "EditorToolset.DataTableToolset.GetSchema": "read", // EPIC accessMode=read
  // Imports a file from disk as a DataTable asset. The file's columns must match the property names in Schema. Use
  "EditorToolset.DataTableToolset.ImportFile": "mutate", // EPIC accessMode=write
  // Lists the names of all rows in the data table.
  "EditorToolset.DataTableToolset.ListRows": "read", // EPIC accessMode=read
  // Removes rows from the data table.
  "EditorToolset.DataTableToolset.RemoveRows": "mutate", // EPIC accessMode=write
  // Renames one or more rows in the data table.
  "EditorToolset.DataTableToolset.RenameRows": "mutate", // EPIC accessMode=write
  // Finds structs that can be used as a DataTable schema.
  "EditorToolset.DataTableToolset.SearchRowStructs": "read", // EPIC accessMode=read
  // Sets column values for one or more rows.
  "EditorToolset.DataTableToolset.SetRows": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.EditorAppToolset ────────────────────────────── */
  // Adds assets to the specified collection.
  "EditorToolset.EditorAppToolset.AddAssetsToCollection": "mutate", // EPIC accessMode=write
  // Renders a thumbnail for the specified asset (e.g. static meshes, skeletal meshes, skeletons, animations, monta
  "EditorToolset.EditorAppToolset.CaptureAssetImage": "mutate",
  // Captures an image of the entire editor application as the user sees it.
  "EditorToolset.EditorAppToolset.CaptureEditorImage": "mutate",
  // Captures the level viewport with optional annotations. Annotations rendering overlays a projected 3D world-spa
  "EditorToolset.EditorAppToolset.CaptureViewport": "mutate",
  // Closes every asset editor open for the specified asset.
  "EditorToolset.EditorAppToolset.CloseEditorForAsset": "mutate", // EPIC accessMode=write
  // Creates a new static collection.
  "EditorToolset.EditorAppToolset.CreateCollection": "mutate", // EPIC accessMode=write
  // Deletes the specified collection.
  "EditorToolset.EditorAppToolset.DestroyCollection": "mutate", // EPIC accessMode=write
  // Requests a shutdown of the editor application. The request is queued for the next editor tick so this call can
  "EditorToolset.EditorAppToolset.ExitEditor": "unknown", // EPIC accessMode=execute
  // Repositions the level editor camera to focus on the specified actors. Cannot be called while PIE is active.
  "EditorToolset.EditorAppToolset.FocusOnActors": "mutate",
  // Returns the IDs of the currently active editor modes. More than one mode can be active at once; the default pl
  "EditorToolset.EditorAppToolset.GetActiveEditorModes": "read", // EPIC accessMode=read
  // Gets what is selected inside the asset editor for the given asset (e.g. selected graph nodes in a Blueprint or
  "EditorToolset.EditorAppToolset.GetAssetEditorSelection": "read", // EPIC accessMode=read
  // Returns the assets' existing cached thumbnails (the images the Content Browser shows) without loading or rende
  "EditorToolset.EditorAppToolset.GetAssetThumbnails": "read", // EPIC accessMode=read
  // Returns the position and rotation of the level viewport camera.
  "EditorToolset.EditorAppToolset.GetCameraTransform": "read",
  // Returns the assets in the specified collection.
  "EditorToolset.EditorAppToolset.GetCollectionAssets": "read", // EPIC accessMode=read
  // Gets the current path of the active content browser.
  "EditorToolset.EditorAppToolset.GetContentBrowserPath": "read",
  // Gets the list of assets currently open in asset editors.
  "EditorToolset.EditorAppToolset.GetOpenAssets": "read",
  // Gets the current location of the editor's transform-gizmo pivot.
  "EditorToolset.EditorAppToolset.GetPivotLocation": "read", // EPIC accessMode=read
  // Gets the currently selected actors in the level editor.
  "EditorToolset.EditorAppToolset.GetSelectedActors": "read",
  // Gets the list of assets selected in the content browser.
  "EditorToolset.EditorAppToolset.GetSelectedAssets": "read",
  // Gets the folder paths selected across all open scene outliners.
  "EditorToolset.EditorAppToolset.GetSelectedOutlinerFolders": "read", // EPIC accessMode=read
  // Reads whether a named engine show flag is currently enabled on the active level editor viewport.
  "EditorToolset.EditorAppToolset.GetShowFlag": "read", // EPIC accessMode=read
  // Returns all actors in the current level whose bounds intersect the viewport frustum.
  "EditorToolset.EditorAppToolset.GetVisibleActors": "read",
  // True for active PIE or simulation, including online login before world creation. A queued start alone returns 
  "EditorToolset.EditorAppToolset.IsPIERunning": "read",
  // Lists all collections in the content browser.
  "EditorToolset.EditorAppToolset.ListCollections": "read", // EPIC accessMode=read
  // Lists the IDs of all registered editor modes (e.g. "EM_Default", "EM_Landscape", "EM_Foliage"), ordered by mod
  "EditorToolset.EditorAppToolset.ListEditorModes": "read", // EPIC accessMode=read
  // Lists every engine show flag with its display name and editor group.
  "EditorToolset.EditorAppToolset.ListShowFlags": "read", // EPIC accessMode=read
  // Opens an asset editor for the specified asset.
  "EditorToolset.EditorAppToolset.OpenEditorForAsset": "mutate",
  // Removes assets from the specified collection.
  "EditorToolset.EditorAppToolset.RemoveAssetsFromCollection": "mutate", // EPIC accessMode=write
  // Finds the world position of the nearest solid object at a given set of normalized view space coords.
  "EditorToolset.EditorAppToolset.ScreenCoordsToWorld": "read",
  // Finds all console variables that contain a given name.
  "EditorToolset.EditorAppToolset.SearchCVars": "read",
  // Selects the specified actors in the current scene.
  "EditorToolset.EditorAppToolset.SelectActors": "mutate",
  // Sets what is selected inside the asset editor for the given asset, replacing the current selection. Supported 
  "EditorToolset.EditorAppToolset.SelectAssetEditorObjects": "mutate", // EPIC accessMode=write
  // Selects the specified assets in the content browser. Completes once the content browser has applied the select
  "EditorToolset.EditorAppToolset.SelectAssets": "mutate",
  // Selects the specified folders in every open scene outliner. Replaces the existing folder selection; non-folder
  "EditorToolset.EditorAppToolset.SelectOutlinerFolders": "mutate", // EPIC accessMode=write
  // Sets the position and/or rotation of the level viewport camera. Anything not provided keeps its current value.
  "EditorToolset.EditorAppToolset.SetCameraTransform": "mutate",
  // Navigates the active content browser to the specified folder path.
  "EditorToolset.EditorAppToolset.SetContentBrowserPath": "mutate",
  // Activates the specified editor mode. Pass "EM_Default" (or an empty string) to return to the default placement
  "EditorToolset.EditorAppToolset.SetEditorMode": "mutate", // EPIC accessMode=write
  // Sets the location of the editor's transform-gizmo pivot.
  "EditorToolset.EditorAppToolset.SetPivotLocation": "mutate", // EPIC accessMode=write
  // Toggles a named engine show flag on the active level editor viewport (e.g. "Navigation", "Collision", "Grid", 
  "EditorToolset.EditorAppToolset.SetShowFlag": "mutate", // EPIC accessMode=write
  // Sets the view mode of the active level editor viewport (e.g. Lit, Unlit, Wireframe, Lighting-only, complexity 
  "EditorToolset.EditorAppToolset.SetViewportViewMode": "mutate", // EPIC accessMode=write
  // Shows a transient editor notification (Slate toast, bottom-right).
  "EditorToolset.EditorAppToolset.ShowNotification": "mutate", // EPIC accessMode=write
  // Starts PIE or simulation in the current level, then waits for warmup. Local single-player worlds usually exist
  "EditorToolset.EditorAppToolset.StartPIE": "mutate",
  // Stops PIE or simulation and waits for shutdown. Normally handled on an editor tick; pending online login defer
  "EditorToolset.EditorAppToolset.StopPIE": "mutate",
  // Converts a world-space position into normalized screen space based on the editor viewport camera.
  "EditorToolset.EditorAppToolset.WorldPosToScreenCoords": "read",

  /* ── EditorToolset.LevelToolset ────────────────────────────────── */
  // Adds actors to a data layer, creating its instance in the current world if it has none yet. Actors already in 
  "EditorToolset.LevelToolset.AddActorsToDataLayer": "mutate", // EPIC accessMode=write
  // Creates a new actor in the scene from an asset. An asset the project's permissions hide is refused like a miss
  "EditorToolset.LevelToolset.AddToSceneFromAsset": "mutate", // EPIC accessMode=write
  // Creates a new instance of the specified Actor class at the specified transform. A class the project's class fi
  "EditorToolset.LevelToolset.AddToSceneFromClass": "mutate", // EPIC accessMode=write
  // Saves or discards edits to a level instance and exits edit mode.
  "EditorToolset.LevelToolset.CommitLevelInstance": "mutate", // EPIC accessMode=write
  // Creates a new Data Layer asset in the content browser. A Data Layer asset is reusable across levels; assign it
  "EditorToolset.LevelToolset.CreateDataLayerAsset": "mutate", // EPIC accessMode=write
  // Creates a new empty level asset and opens it in the editor.
  "EditorToolset.LevelToolset.CreateLevel": "mutate", // EPIC accessMode=write
  // Creates a Level Instance actor in the scene referencing an existing level asset.
  "EditorToolset.LevelToolset.CreateLevelInstance": "mutate", // EPIC accessMode=write
  // Deletes a folder from the outliner. Applies to the world's own folders only. Actors directly in the folder are
  "EditorToolset.LevelToolset.DeleteFolder": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two scene asset packages, each expected to contain a single externa
  "EditorToolset.LevelToolset.Diff": "read", // EPIC accessMode=read
  // Opens a level instance for editing. While in edit mode, scene tools that add and remove actors operate within 
  "EditorToolset.LevelToolset.EditLevelInstance": "mutate", // EPIC accessMode=write
  // Searches the scene for actors that match specific criteria. Actors removed from the scene are not returned, ev
  "EditorToolset.LevelToolset.FindActors": "read", // EPIC accessMode=read
  // Returns the content path that identifies this actor on disk.
  "EditorToolset.LevelToolset.GetActorAssetPath": "read", // EPIC accessMode=read
  // Returns descriptors for the loaded actors assigned to a data layer.
  "EditorToolset.LevelToolset.GetActorsInDataLayer": "read", // EPIC accessMode=read
  // Returns descriptors for the actors in the specified outliner folder.
  "EditorToolset.LevelToolset.GetActorsInFolder": "read", // EPIC accessMode=read
  // Returns all available collision channels, for use in collision-based actor searches.
  "EditorToolset.LevelToolset.GetCollisionChannels": "read", // EPIC accessMode=read
  // Returns the path to the current level asset.
  "EditorToolset.LevelToolset.GetCurrentLevel": "read", // EPIC accessMode=read
  // Returns the data layer instances in the current world.
  "EditorToolset.LevelToolset.GetDataLayers": "read", // EPIC accessMode=read
  // Returns all folders currently in use in the outliner, including those inside level instances. Includes all int
  "EditorToolset.LevelToolset.GetFolders": "read", // EPIC accessMode=read
  // Returns the level instance that directly contains an actor, loaded or not.
  "EditorToolset.LevelToolset.GetParentLevelInstance": "read", // EPIC accessMode=read
  // Returns whether the actor is hidden in the editor viewport and outliner.
  "EditorToolset.LevelToolset.IsActorHidden": "read", // EPIC accessMode=read
  // Returns whether a level instance's level is loaded. Level instances created by other tools, or that came with 
  "EditorToolset.LevelToolset.IsLevelInstanceLoaded": "read", // EPIC accessMode=read
  // Loads the actors described by the given descriptors into the editor and returns them.
  "EditorToolset.LevelToolset.LoadActors": "mutate", // EPIC accessMode=write
  // Loads a level in the editor, closing the current one.
  "EditorToolset.LevelToolset.LoadLevel": "mutate", // EPIC accessMode=write
  // Loads a level instance's level, making its actors available as loaded actors.
  "EditorToolset.LevelToolset.LoadLevelInstance": "mutate", // EPIC accessMode=write
  // Merges multiple StaticMesh actors into a single mesh asset and actor.
  "EditorToolset.LevelToolset.MergeActors": "mutate", // EPIC accessMode=write
  // Removes actors from a data layer. Actors not in the layer are left unchanged.
  "EditorToolset.LevelToolset.RemoveActorsFromDataLayer": "mutate", // EPIC accessMode=write
  // Deletes an actor from the scene.
  "EditorToolset.LevelToolset.RemoveFromScene": "mutate", // EPIC accessMode=write
  // Renames a folder in the outliner. Applies to the world's own folders only. Updates the folder path for all act
  "EditorToolset.LevelToolset.RenameFolder": "mutate", // EPIC accessMode=write
  // Replaces actors in the scene with new actors created from an asset (e.g. a static mesh, Blueprint, or particle
  "EditorToolset.LevelToolset.ReplaceActorsWithAsset": "mutate", // EPIC accessMode=write
  // Replaces actors in the scene with new instances of a native Actor class, matching the editor's "Replace Select
  "EditorToolset.LevelToolset.ReplaceActorsWithClass": "mutate", // EPIC accessMode=write
  // Saves the actor to disk.
  "EditorToolset.LevelToolset.SaveActor": "mutate", // EPIC accessMode=write
  // Assigns an actor to the specified folder in the outliner. Creates the folder implicitly if it does not already
  "EditorToolset.LevelToolset.SetActorFolder": "mutate", // EPIC accessMode=write
  // Shows or hides the actor in the editor viewport and outliner. This is the same transient, editor-only visibili
  "EditorToolset.LevelToolset.SetActorHidden": "mutate", // EPIC accessMode=write
  // Traces a line through the world and returns the distance to the first hit.
  "EditorToolset.LevelToolset.TraceWorld": "read", // EPIC accessMode=read
  // Unloads previously loaded World Partition actors from the editor.
  "EditorToolset.LevelToolset.UnloadActors": "mutate", // EPIC accessMode=write
  // Unloads a level instance's level.
  "EditorToolset.LevelToolset.UnloadLevelInstance": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.LogsToolset ─────────────────────────────────── */
  // Returns a sorted list of registered log categories.
  "EditorToolset.LogsToolset.GetLogCategories": "read",
  // Returns log entries from the current session's log file.
  "EditorToolset.LogsToolset.GetLogEntries": "read",
  // Returns the current verbosity level for a log category.
  "EditorToolset.LogsToolset.GetVerbosity": "read",
  // Sets the verbosity level for a log category.
  "EditorToolset.LogsToolset.SetVerbosity": "mutate",

  /* ── EditorToolset.MaterialInstanceToolset ─────────────────────── */
  // Clears all parameter overrides on a material instance, reverting to parent defaults.
  "EditorToolset.MaterialInstanceToolset.ClearParameters": "mutate", // EPIC accessMode=write
  // Creates a new MaterialInstanceConstant asset derived from a parent material. Material instances expose the par
  "EditorToolset.MaterialInstanceToolset.Create": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two MaterialInstanceConstant assets.
  "EditorToolset.MaterialInstanceToolset.Diff": "read", // EPIC accessMode=read
  // Gets the current value of a scalar parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.GetScalarParameter": "read", // EPIC accessMode=read
  // Gets the value of a static switch parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.GetStaticSwitchParameter": "read", // EPIC accessMode=read
  // Gets the texture assigned to a texture parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.GetTextureParameter": "read", // EPIC accessMode=read
  // Gets the current value of a vector parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.GetVectorParameter": "read", // EPIC accessMode=read
  // Returns all parameters exposed by a material or instance, with their names and types.
  "EditorToolset.MaterialInstanceToolset.ListParameters": "read", // EPIC accessMode=read
  // Enables or disables a parameter override on a material instance. Enabling sets the override to the current eff
  "EditorToolset.MaterialInstanceToolset.SetParameterOverride": "mutate", // EPIC accessMode=write
  // Changes the parent of a material instance.
  "EditorToolset.MaterialInstanceToolset.SetParent": "mutate", // EPIC accessMode=write
  // Sets the value of a scalar parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.SetScalarParameter": "mutate", // EPIC accessMode=write
  // Sets the value of a static switch parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.SetStaticSwitchParameter": "mutate", // EPIC accessMode=write
  // Assigns a texture to a texture parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.SetTextureParameter": "mutate", // EPIC accessMode=write
  // Sets the value of a vector parameter on a material instance.
  "EditorToolset.MaterialInstanceToolset.SetVectorParameter": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.MaterialToolset ─────────────────────────────── */
  // Adds a new expression node to a Material or MaterialFunction graph.
  "EditorToolset.MaterialToolset.AddExpression": "mutate", // EPIC accessMode=write
  // Connects an expression node's output pin to another expression node's input pin.
  "EditorToolset.MaterialToolset.ConnectExpressions": "mutate", // EPIC accessMode=write
  // Connects an expression node's output to one of the material's output properties.
  "EditorToolset.MaterialToolset.ConnectToOutput": "mutate", // EPIC accessMode=write
  // Creates a new empty MaterialFunction asset.
  "EditorToolset.MaterialToolset.CreateFunction": "mutate", // EPIC accessMode=write
  // Creates a new empty Material asset. Warning: Each new Material increases shader compile times. Prefer creating
  "EditorToolset.MaterialToolset.CreateMaterial": "mutate", // EPIC accessMode=write
  // Creates a new empty MaterialParameterCollection (MPC) asset. An MPC holds named Scalar and Vector parameters w
  "EditorToolset.MaterialToolset.CreateParameterCollection": "mutate", // EPIC accessMode=write
  // Removes an expression node from a Material or MaterialFunction graph.
  "EditorToolset.MaterialToolset.DeleteExpression": "mutate", // EPIC accessMode=write
  // Removes a parameter group, ungrouping all parameters that belong to it. The parameter expressions themselves a
  "EditorToolset.MaterialToolset.DeleteParameterGroup": "mutate", // EPIC accessMode=write
  // Deletes all expression nodes not connected to any material output. Useful for cleaning up a material graph aft
  "EditorToolset.MaterialToolset.DeleteUnusedExpressions": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two MaterialFunction assets.
  "EditorToolset.MaterialToolset.DiffFunction": "read", // EPIC accessMode=read
  // Returns a human-readable text diff between two Material assets.
  "EditorToolset.MaterialToolset.DiffMaterial": "read", // EPIC accessMode=read
  // Disconnects the input pin of an expression node, removing whatever is connected to it.
  "EditorToolset.MaterialToolset.DisconnectExpressions": "mutate", // EPIC accessMode=write
  // Disconnects the expression currently connected to a material output property.
  "EditorToolset.MaterialToolset.DisconnectFromOutput": "mutate", // EPIC accessMode=write
  // Returns the names of all input pins on a material expression node. Use these names as ToInputName when calling
  "EditorToolset.MaterialToolset.GetExpressionInputNames": "read", // EPIC accessMode=read
  // Returns the current wiring of each input pin on a material expression. Use after building or modifying a graph
  "EditorToolset.MaterialToolset.GetExpressionInputs": "read", // EPIC accessMode=read
  // Returns the names of all output pins on a material expression node. Use these names as FromOutputName when cal
  "EditorToolset.MaterialToolset.GetExpressionOutputNames": "read", // EPIC accessMode=read
  // Returns all expression nodes in a Material or MaterialFunction graph.
  "EditorToolset.MaterialToolset.GetExpressions": "read", // EPIC accessMode=read
  // Returns the expression and output pin feeding a material output property. Use to inspect what drives MP_Emissi
  "EditorToolset.MaterialToolset.GetPropertyInput": "read", // EPIC accessMode=read
  // Returns asset data for all Materials that reference this MaterialFunction.
  "EditorToolset.MaterialToolset.GetReferencingMaterials": "read", // EPIC accessMode=read
  // Returns compiled shader statistics for a Material or MaterialInstance on the current shader platform: instruct
  "EditorToolset.MaterialToolset.GetStatistics": "read", // EPIC accessMode=read
  // Automatically arranges all expression nodes in a Material or MaterialFunction graph.
  "EditorToolset.MaterialToolset.LayoutExpressions": "mutate", // EPIC accessMode=write
  // Returns MaterialExpression subclasses valid for the given context. Use the results with AddExpression. Pass a 
  "EditorToolset.MaterialToolset.ListExpressionClasses": "read", // EPIC accessMode=read
  // Returns the unique parameter group names defined in a Material or MaterialFunction. Parameters are organised i
  "EditorToolset.MaterialToolset.ListParameterGroups": "read", // EPIC accessMode=read
  // Recompiles a Material or MaterialFunction after edits. For MaterialFunctions, also recompiles any Materials th
  "EditorToolset.MaterialToolset.Recompile": "mutate", // EPIC accessMode=write
  // Renames a parameter group across all parameter expressions in a Material or MaterialFunction. All parameters c
  "EditorToolset.MaterialToolset.RenameParameterGroup": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.ObjectToolset ───────────────────────────────── */
  // Returns the class of an Unreal object, resolving a Blueprint to its generated class and returning a class unch
  "EditorToolset.ObjectToolset.GetClass": "read", // EPIC accessMode=read
  // Returns the values of one or more properties on an object.
  "EditorToolset.ObjectToolset.GetProperties": "read", // EPIC accessMode=read
  // Returns a JSON Schema of the properties on an object, with each property's type and description. Much larger t
  "EditorToolset.ObjectToolset.GetPropertySchema": "read", // EPIC accessMode=read
  // Returns the property names on an object as dotted paths relative to Root, one element per property MaxDepth re
  "EditorToolset.ObjectToolset.ListProperties": "read", // EPIC accessMode=read
  // Resets one or more properties on an object to their default values, removing any per-instance overrides.
  "EditorToolset.ObjectToolset.ResetProperties": "mutate", // EPIC accessMode=write
  // Finds all subclasses of a given class.
  "EditorToolset.ObjectToolset.SearchSubclasses": "read", // EPIC accessMode=read
  // Sets the values of properties on an object.
  "EditorToolset.ObjectToolset.SetProperties": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.PrimitiveToolset ────────────────────────────── */
  // Adds a cone-shaped StaticMeshComponent to an actor.
  "EditorToolset.PrimitiveToolset.AddCone": "mutate", // EPIC accessMode=write
  // Adds a cube-shaped StaticMeshComponent to an actor.
  "EditorToolset.PrimitiveToolset.AddCube": "mutate", // EPIC accessMode=write
  // Adds a cylinder-shaped StaticMeshComponent to an actor.
  "EditorToolset.PrimitiveToolset.AddCylinder": "mutate", // EPIC accessMode=write
  // Adds a sphere-shaped StaticMeshComponent to an actor.
  "EditorToolset.PrimitiveToolset.AddSphere": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.SkeletalMeshToolset ─────────────────────────── */
  // Adds a named socket to a skeletal mesh attached to a bone. Sockets are named attachment points used to attach 
  "EditorToolset.SkeletalMeshToolset.AddSocket": "mutate", // EPIC accessMode=write
  // Assigns a physics asset to a skeletal mesh. The physics asset must be compatible with the mesh's skeleton. Use
  "EditorToolset.SkeletalMeshToolset.AssignPhysicsAsset": "mutate", // EPIC accessMode=write
  // Returns the direct children of a bone.
  "EditorToolset.SkeletalMeshToolset.GetBoneChildren": "read", // EPIC accessMode=read
  // Returns the names of all bones in a skeletal mesh in hierarchy order. Bone names are used to target specific b
  "EditorToolset.SkeletalMeshToolset.GetBoneNames": "read", // EPIC accessMode=read
  // Returns the name of a bone's parent, or an empty string for the root bone.
  "EditorToolset.SkeletalMeshToolset.GetBoneParent": "read", // EPIC accessMode=read
  // Returns the local-space bounding volume of a skeletal mesh. The bounds represent the reference pose and do not
  "EditorToolset.SkeletalMeshToolset.GetBounds": "read", // EPIC accessMode=read
  // Returns the number of LODs in a skeletal mesh asset.
  "EditorToolset.SkeletalMeshToolset.GetLodCount": "read", // EPIC accessMode=read
  // Returns the material assigned to a named slot on a skeletal mesh.
  "EditorToolset.SkeletalMeshToolset.GetMaterial": "read", // EPIC accessMode=read
  // Returns the names of all material slots in a skeletal mesh. Material slot names are used when assigning materi
  "EditorToolset.SkeletalMeshToolset.GetMaterialSlots": "read", // EPIC accessMode=read
  // Returns the names of all morph targets on a skeletal mesh. Morph targets (blend shapes) are per-vertex offsets
  "EditorToolset.SkeletalMeshToolset.GetMorphTargetNames": "read", // EPIC accessMode=read
  // Returns the physics asset assigned to a skeletal mesh. The physics asset defines the collision bodies and cons
  "EditorToolset.SkeletalMeshToolset.GetPhysicsAsset": "read", // EPIC accessMode=read
  // Returns the number of sections in a specific LOD of a skeletal mesh. Sections correspond to individual materia
  "EditorToolset.SkeletalMeshToolset.GetSectionCount": "read", // EPIC accessMode=read
  // Returns the skeleton asset associated with a skeletal mesh. The skeleton defines the bone hierarchy shared acr
  "EditorToolset.SkeletalMeshToolset.GetSkeleton": "read", // EPIC accessMode=read
  // Returns the name of the bone that a socket is attached to.
  "EditorToolset.SkeletalMeshToolset.GetSocketBone": "read", // EPIC accessMode=read
  // Returns the names of all sockets on a skeletal mesh. Sockets are named attachment points parented to bones. Th
  "EditorToolset.SkeletalMeshToolset.GetSocketNames": "read", // EPIC accessMode=read
  // Returns the local transform of a socket relative to its parent bone.
  "EditorToolset.SkeletalMeshToolset.GetSocketTransform": "read", // EPIC accessMode=read
  // Returns the number of vertices in a specific LOD of a skeletal mesh.
  "EditorToolset.SkeletalMeshToolset.GetVertexCount": "read", // EPIC accessMode=read
  // Imports a mesh file from disk as a SkeletalMesh asset. Imports through Interchange with the project's import s
  "EditorToolset.SkeletalMeshToolset.ImportFile": "mutate", // EPIC accessMode=write
  // Removes a named socket from a skeletal mesh.
  "EditorToolset.SkeletalMeshToolset.RemoveSocket": "mutate", // EPIC accessMode=write
  // Renames a socket on a skeletal mesh.
  "EditorToolset.SkeletalMeshToolset.RenameSocket": "mutate", // EPIC accessMode=write
  // Assigns a material to a named slot on a skeletal mesh asset. This affects all instances of the mesh that do no
  "EditorToolset.SkeletalMeshToolset.SetMaterial": "mutate", // EPIC accessMode=write
  // Sets the local transform of a socket relative to its parent bone. Anything not provided keeps its current valu
  "EditorToolset.SkeletalMeshToolset.SetSocketTransform": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.SoundWaveToolset ────────────────────────────── */
  // Exports a SoundWave to a .wav file on disk.
  "EditorToolset.SoundWaveToolset.ExportWav": "mutate", // EPIC accessMode=write
  // Imports an audio file from disk as a SoundWave asset. Accepts the extensions SoundFactory declares: wav, aif, 
  "EditorToolset.SoundWaveToolset.ImportFile": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.StaticMeshToolset ───────────────────────────── */
  // Generates convex hull collision shapes for a static mesh. Convex hulls provide accurate collision for physics 
  "EditorToolset.StaticMeshToolset.GenerateConvexCollisions": "mutate", // EPIC accessMode=write
  // Auto-generates LODs for a static mesh using triangle reduction. Each entry in TrianglePercents creates one add
  "EditorToolset.StaticMeshToolset.GenerateLods": "mutate", // EPIC accessMode=write
  // Returns the local-space bounding box of a static mesh.
  "EditorToolset.StaticMeshToolset.GetBounds": "read", // EPIC accessMode=read
  // Returns the collision trace behavior of a static mesh.
  "EditorToolset.StaticMeshToolset.GetCollisionComplexity": "read", // EPIC accessMode=read
  // Returns the number of LODs in a static mesh asset.
  "EditorToolset.StaticMeshToolset.GetLodCount": "read", // EPIC accessMode=read
  // Returns the screen-size thresholds at which each LOD becomes active. Screen size is a ratio of the mesh's scre
  "EditorToolset.StaticMeshToolset.GetLodThresholds": "read", // EPIC accessMode=read
  // Returns the material assigned to a named slot on a static mesh.
  "EditorToolset.StaticMeshToolset.GetMaterial": "read", // EPIC accessMode=read
  // Returns the names of all material slots in a static mesh. Material slot names are used when assigning material
  "EditorToolset.StaticMeshToolset.GetMaterialSlots": "read", // EPIC accessMode=read
  // Returns the per-quality-level Min LOD overrides for a static mesh. Min LOD is the smallest LOD index the mesh 
  "EditorToolset.StaticMeshToolset.GetQualityLevelMinLod": "read", // EPIC accessMode=read
  // Returns the fallback Min LOD for quality levels with no explicit override.
  "EditorToolset.StaticMeshToolset.GetQualityLevelMinLodDefault": "read", // EPIC accessMode=read
  // Returns the number of triangles in a specific LOD of a static mesh.
  "EditorToolset.StaticMeshToolset.GetTriangleCount": "read", // EPIC accessMode=read
  // Returns the number of vertices in a specific LOD of a static mesh.
  "EditorToolset.StaticMeshToolset.GetVertexCount": "read", // EPIC accessMode=read
  // Imports a mesh file from disk as a StaticMesh asset. Imports through Interchange with the project's import set
  "EditorToolset.StaticMeshToolset.ImportFile": "mutate", // EPIC accessMode=write
  // Returns whether this is a Nanite mesh. This reflects the mesh's current state, so it may briefly lag behind a 
  "EditorToolset.StaticMeshToolset.IsNaniteMesh": "read", // EPIC accessMode=read
  // Removes all collision shapes from a static mesh.
  "EditorToolset.StaticMeshToolset.RemoveCollisions": "mutate", // EPIC accessMode=write
  // Removes all auto-generated LODs from a static mesh, keeping only LOD 0.
  "EditorToolset.StaticMeshToolset.RemoveLods": "mutate", // EPIC accessMode=write
  // Sets the collision trace behavior of a static mesh.
  "EditorToolset.StaticMeshToolset.SetCollisionComplexity": "mutate", // EPIC accessMode=write
  // Sets the screen-size thresholds at which each LOD becomes active. Thresholds must be in strictly descending or
  "EditorToolset.StaticMeshToolset.SetLodThresholds": "mutate", // EPIC accessMode=write
  // Assigns a material to a named slot on a static mesh asset. This affects all instances of the mesh that do not 
  "EditorToolset.StaticMeshToolset.SetMaterial": "mutate", // EPIC accessMode=write
  // Enables or disables Nanite for a static mesh. Changing this setting triggers a mesh rebuild. Nanite is most be
  "EditorToolset.StaticMeshToolset.SetNaniteEnabled": "mutate", // EPIC accessMode=write
  // Merges per-quality-level Min LOD overrides into a static mesh. Quality levels not listed in Overrides keep the
  "EditorToolset.StaticMeshToolset.SetQualityLevelMinLod": "mutate", // EPIC accessMode=write
  // Sets the fallback Min LOD for quality levels with no explicit override. Leaves existing per-quality-level over
  "EditorToolset.StaticMeshToolset.SetQualityLevelMinLodDefault": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.StringTableToolset ──────────────────────────── */
  // Creates a new StringTable asset.
  "EditorToolset.StringTableToolset.Create": "mutate", // EPIC accessMode=write
  // Returns a human-readable text diff between two StringTable assets.
  "EditorToolset.StringTableToolset.Diff": "read", // EPIC accessMode=read
  // Returns the source string for a specific key.
  "EditorToolset.StringTableToolset.GetEntry": "read", // EPIC accessMode=read
  // Returns the namespace of a StringTable asset.
  "EditorToolset.StringTableToolset.GetNamespace": "read", // EPIC accessMode=read
  // Returns the table ID for a StringTable asset. The table ID is derived from the asset's package path and is use
  "EditorToolset.StringTableToolset.GetTableId": "read", // EPIC accessMode=read
  // Imports a file from disk as a StringTable asset. The file must have a header row with at least 'Key' and 'Sour
  "EditorToolset.StringTableToolset.ImportFile": "mutate", // EPIC accessMode=write
  // Lists all keys in the string table.
  "EditorToolset.StringTableToolset.ListKeys": "read", // EPIC accessMode=read
  // Removes an entry from the string table.
  "EditorToolset.StringTableToolset.RemoveEntry": "mutate", // EPIC accessMode=write
  // Adds or updates an entry in the string table. If the key already exists its value is replaced; otherwise a new
  "EditorToolset.StringTableToolset.SetEntry": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.TextureToolset ──────────────────────────────── */
  // Exports a Texture to a PNG file on disk.
  "EditorToolset.TextureToolset.ExportPng": "mutate", // EPIC accessMode=write
  // Returns the dimensions of a Texture2D in pixels.
  "EditorToolset.TextureToolset.GetSize": "read", // EPIC accessMode=read
  // Imports an image file from disk as a Texture2D asset.
  "EditorToolset.TextureToolset.ImportFile": "mutate", // EPIC accessMode=write
  // Reads a Texture and returns it as an image.
  "EditorToolset.TextureToolset.ReadTexture": "read", // EPIC accessMode=read

  /* ── EditorToolset.UndoHistoryToolset ──────────────────────────── */
  // Returns transactions from the undo history, newest first.
  "EditorToolset.UndoHistoryToolset.GetRecentTransactions": "unknown", // EPIC accessMode=execute
  // Returns detailed information about a single transaction
  "EditorToolset.UndoHistoryToolset.GetTransactionDetails": "unknown", // EPIC accessMode=execute
  // Returns how far back a transaction is in the undo history, counted newest-first: 0 is the most recent transact
  "EditorToolset.UndoHistoryToolset.GetTransactionRecencyIndex": "unknown", // EPIC accessMode=execute
  // Searches the undo history by transaction label using a fuzzy (edit-distance) match, closest match first. When 
  "EditorToolset.UndoHistoryToolset.SearchTransactions": "unknown", // EPIC accessMode=execute

  /* ── EditorToolset.UserDefinedEnumToolset ──────────────────────── */
  // Appends a new enumerator to an enum.
  "EditorToolset.UserDefinedEnumToolset.AddEnumerator": "mutate", // EPIC accessMode=write
  // Creates a new user-defined enum asset with no enumerators.
  "EditorToolset.UserDefinedEnumToolset.Create": "mutate", // EPIC accessMode=write
  // Returns the enumerators of an enum in order.
  "EditorToolset.UserDefinedEnumToolset.GetEnumerators": "read", // EPIC accessMode=read
  // Returns whether the enum is marked as a bitflags type.
  "EditorToolset.UserDefinedEnumToolset.IsBitflags": "read", // EPIC accessMode=read
  // Moves an enumerator to a new position, shifting the others to accommodate it.
  "EditorToolset.UserDefinedEnumToolset.MoveEnumerator": "mutate", // EPIC accessMode=write
  // Removes the enumerator at the given index, shifting later enumerators down to fill the gap.
  "EditorToolset.UserDefinedEnumToolset.RemoveEnumerator": "mutate", // EPIC accessMode=write
  // Sets whether the enum is treated as a bitflags type.
  "EditorToolset.UserDefinedEnumToolset.SetBitflags": "mutate", // EPIC accessMode=write
  // Sets the display name of the enumerator at the given index.
  "EditorToolset.UserDefinedEnumToolset.SetDisplayName": "mutate", // EPIC accessMode=write

  /* ── EditorToolset.UserDefinedStructToolset ────────────────────── */
  // Adds a UObject-reference variable, applying all fields of Spec atomically.
  "EditorToolset.UserDefinedStructToolset.AddObjectVariable": "mutate", // EPIC accessMode=write
  // Adds a variable of an arbitrary script-struct type, applying all fields of Spec atomically.
  "EditorToolset.UserDefinedStructToolset.AddStructVariable": "mutate", // EPIC accessMode=write
  // Adds a variable of a primitive or basic struct type, applying all fields of Spec atomically. Supported TypeNam
  "EditorToolset.UserDefinedStructToolset.AddVariable": "mutate", // EPIC accessMode=write
  // Creates a new user-defined struct asset. The new struct starts with one default bool variable (engine conventi
  "EditorToolset.UserDefinedStructToolset.Create": "mutate", // EPIC accessMode=write
  // Returns the struct-level developer tooltip.
  "EditorToolset.UserDefinedStructToolset.GetTooltip": "read", // EPIC accessMode=read
  // Returns the variables of a struct in authored order.
  "EditorToolset.UserDefinedStructToolset.ListVariables": "read", // EPIC accessMode=read
  // Moves a variable above or below another variable on the struct.
  "EditorToolset.UserDefinedStructToolset.MoveVariable": "mutate", // EPIC accessMode=write
  // Removes a variable from a struct.
  "EditorToolset.UserDefinedStructToolset.RemoveVariable": "mutate", // EPIC accessMode=write
  // Renames a variable.
  "EditorToolset.UserDefinedStructToolset.RenameVariable": "mutate", // EPIC accessMode=write
  // Sets the struct-level developer tooltip.
  "EditorToolset.UserDefinedStructToolset.SetTooltip": "mutate", // EPIC accessMode=write
  // Sets a variable's default value.
  "EditorToolset.UserDefinedStructToolset.SetVariableDefaultValue": "mutate", // EPIC accessMode=write
  // Sets whether a variable is editable on instances of the struct.
  "EditorToolset.UserDefinedStructToolset.SetVariableEditableOnInstance": "mutate", // EPIC accessMode=write
  // Sets whether a variable participates in SaveGame archives.
  "EditorToolset.UserDefinedStructToolset.SetVariableSaveGame": "mutate", // EPIC accessMode=write
  // Sets a variable's developer tooltip.
  "EditorToolset.UserDefinedStructToolset.SetVariableTooltip": "mutate", // EPIC accessMode=write

  /* ── FabToolset.FabToolset ─────────────────────────────────────── */
  // Starts Fab sign-in by opening the Epic account portal in the user's system browser. Only needed when another t
  "FabToolset.FabToolset.FabSignIn": "mutate", // EPIC accessMode=write
  // Reads the full details of one Fab listing: description, tags, licenses and their prices, rating. Use this afte
  "FabToolset.FabToolset.GetFabListingDetails": "read", // EPIC accessMode=read
  // Installs a Fab asset into the running editor: one the user owns, or a free one. Returns JSON: { listingId, ass
  "FabToolset.FabToolset.InstallFabListing": "mutate", // EPIC accessMode=write
  // Searches the whole Fab marketplace, as JSON: { query, results: [...], nextCursor }. Use this when the user wan
  "FabToolset.FabToolset.SearchFabListings": "read", // EPIC accessMode=read
  // Searches everything the user has a license to on Fab, as JSON: { query, results: [...], nextCursor }. Only the
  "FabToolset.FabToolset.SearchFabMyLibrary": "read", // EPIC accessMode=read

  /* ── FileSystemToolset.FileSystemToolset ───────────────────────── */
  // Creates a directory and any missing parents. A directory that already exists is a success.
  "FileSystemToolset.FileSystemToolset.CreateDirectory": "mutate", // EPIC accessMode=write
  // Deletes one file. Directories are rejected. Recursive deletion is unavailable. Source-controlled files are mar
  "FileSystemToolset.FileSystemToolset.DeleteFile": "mutate", // EPIC accessMode=write
  // Downloads an HTTPS URL to a file. Redirects are not followed. Responses above 1 GiB are rejected. Local, priva
  "FileSystemToolset.FileSystemToolset.DownloadUrl": "mutate", // EPIC accessMode=write
  // Edits the unique occurrence of a piece of text. Source files and results above 1 MiB are rejected. Big-endian 
  "FileSystemToolset.FileSystemToolset.EditTextByAnchor": "mutate", // EPIC accessMode=write
  // Edits an inclusive range of one-based lines. Source files and results above 1 MiB are rejected. Big-endian UTF
  "FileSystemToolset.FileSystemToolset.EditTextByLineRange": "mutate", // EPIC accessMode=write
  // Lists a range from the immediate contents of a directory. Directories with more than 100000 immediate entries 
  "FileSystemToolset.FileSystemToolset.ListDirectory": "read", // EPIC accessMode=read
  // Reads lines or fragments without line numbers, budgeting the complete JSON response for EDA. Decode Contents o
  "FileSystemToolset.FileSystemToolset.ReadTextFile": "read", // EPIC accessMode=read
  // Resolves a native path, Unreal virtual-content path, or plugin path that the plugin manager finds to an absolu
  "FileSystemToolset.FileSystemToolset.ResolvePath": "read", // EPIC accessMode=read
  // Writes a complete UTF-8 text file and creates missing parent directories. Existing read-only files are checked
  "FileSystemToolset.FileSystemToolset.WriteTextFile": "mutate", // EPIC accessMode=write

  /* ── GameFeaturesToolset.GameFeaturesToolset ───────────────────── */
  // Lists the additional metadata keys the GameFeatures subsystem is configured to read from each .uplugin (UGameF
  "GameFeaturesToolset.GameFeaturesToolset.GetConfiguredAdditionalMetadataKeys": "read", // EPIC accessMode=read
  // Reads the editable metadata on a Game Feature Plugin's .uplugin descriptor. Returns the BuiltInInitialFeatureS
  "GameFeaturesToolset.GameFeaturesToolset.GetGameFeaturePluginDetails": "read", // EPIC accessMode=read
  // Gets the current state of a Game Feature Plugin.
  "GameFeaturesToolset.GameFeaturesToolset.GetGameFeatureState": "read",
  // Checks whether a Game Feature Plugin is active. Raises an error if the subsystem is unavailable or the plugin 
  "GameFeaturesToolset.GameFeaturesToolset.IsGameFeatureActive": "read",
  // Return whether or not a plugin is a Game Feature Plugin. Will error if no plugin of this name can be found by 
  "GameFeaturesToolset.GameFeaturesToolset.IsGameFeaturePlugin": "read",
  // Lists all discovered Game Feature Plugins sorted by name. This includes disabled plugins and plugins the Game 
  "GameFeaturesToolset.GameFeaturesToolset.ListDiscoveredGameFeaturePlugins": "read",
  // Lists enabled Game Feature Plugins that the Game Features system has registered, sorted by name. The project's
  "GameFeaturesToolset.GameFeaturesToolset.ListEnabledGameFeaturePlugins": "read",
  // Removes one of the configured additional metadata keys from the GFP's .uplugin. After removal the key round-tr
  "GameFeaturesToolset.GameFeaturesToolset.RemoveGameFeaturePluginMetadataValue": "mutate", // EPIC accessMode=write
  // Requests activation of a Game Feature Plugin, loading it first if it is not already loaded.
  "GameFeaturesToolset.GameFeaturesToolset.RequestActivateGameFeature": "mutate",
  // Requests deactivation of a Game Feature Plugin, which leaves it loaded.
  "GameFeaturesToolset.GameFeaturesToolset.RequestDeactivateGameFeature": "mutate",
  // Requests load of a Game Feature Plugin without activating it. Use RequestActivateGameFeature instead to make t
  "GameFeaturesToolset.GameFeaturesToolset.RequestLoadGameFeature": "mutate", // EPIC accessMode=write
  // Requests unload of a Game Feature Plugin, taking it out of the Loaded state.
  "GameFeaturesToolset.GameFeaturesToolset.RequestUnloadGameFeature": "mutate", // EPIC accessMode=write
  // Sets the BuiltInInitialFeatureState on the GFP's .uplugin descriptor and writes the descriptor back to disk. E
  "GameFeaturesToolset.GameFeaturesToolset.SetGameFeaturePluginBuiltInAutoState": "mutate", // EPIC accessMode=write
  // Writes a value for one of the configured additional metadata keys to the GFP's .uplugin. JsonValueLiteral must
  "GameFeaturesToolset.GameFeaturesToolset.SetGameFeaturePluginMetadataValue": "mutate", // EPIC accessMode=write

  /* ── GameplayTagsToolset.GameplayTagsToolset ───────────────────── */
  // Adds a new gameplay tag to the project. This should ONLY be called after getting explicit direction or permiss
  "GameplayTagsToolset.GameplayTagsToolset.AddTag": "mutate",
  // Creates a new tag list source that tags can then be added to. Its file is written when the first tag is added.
  "GameplayTagsToolset.GameplayTagsToolset.AddTagSource": "mutate", // EPIC accessMode=write
  // Returns assets that reference a gameplay tag.
  "GameplayTagsToolset.GameplayTagsToolset.FindReferencersByTag": "read",
  // Returns detailed information about a specific gameplay tag.
  "GameplayTagsToolset.GameplayTagsToolset.GetTagInfo": "read",
  // Returns gameplay tags registered in the project.
  "GameplayTagsToolset.GameplayTagsToolset.ListTags": "read",
  // Returns all gameplay tags defined in a specific tag source.
  "GameplayTagsToolset.GameplayTagsToolset.ListTagsInSource": "read", // EPIC accessMode=read
  // Returns the directories a new tag source can be created in: the project's Config/Tags directory and each regis
  "GameplayTagsToolset.GameplayTagsToolset.ListTagSourceRoots": "read", // EPIC accessMode=read
  // Returns every gameplay tag source, sorted by name.
  "GameplayTagsToolset.GameplayTagsToolset.ListTagSources": "read", // EPIC accessMode=read
  // Moves a set of gameplay tags from their current INI source to a different one. This should ONLY be called afte
  "GameplayTagsToolset.GameplayTagsToolset.MoveTags": "mutate", // EPIC accessMode=write
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

  /* ── GASToolsets.GameplayAbilityToolset ────────────────────────── */
  // Creates a new Gameplay Ability Blueprint asset, the same as the Content Browser's Gameplay Ability Blueprint f
  "GASToolsets.GameplayAbilityToolset.CreateGameplayAbility": "mutate", // EPIC accessMode=write

  /* ── GASToolsets.GameplayCueToolset ────────────────────────────── */
  // Adds a new gameplay cue tag to the project. This should ONLY be called after getting explicit direction or per
  "GASToolsets.GameplayCueToolset.AddCueTag": "mutate",
  // Creates a new GameplayCueNotify Blueprint asset at the specified content browser location. This should ONLY be
  "GASToolsets.GameplayCueToolset.CreateCueNotifyAsset": "mutate",
  // Executes a gameplay cue non-replicated on the currently selected actor in the editor. Useful for previewing cu
  "GASToolsets.GameplayCueToolset.ExecuteCueOnSelectedActor": "mutate",
  // Returns all GameplayCueNotify assets found in the project via the asset registry.
  "GASToolsets.GameplayCueToolset.FindCueNotifyAssets": "read",
  // Returns gameplay cue tags that have no GameplayCueNotify of their own.
  "GASToolsets.GameplayCueToolset.FindCueTagsWithoutNotifies": "read",
  // Returns information about a specific gameplay cue, including its notify asset. Only notifies in the project's 
  "GASToolsets.GameplayCueToolset.GetCueInfo": "read",
  // Returns gameplay cue tags registered in the project.
  "GASToolsets.GameplayCueToolset.ListCues": "read",
  // Removes a gameplay cue tag from the project. This should ONLY be called after getting explicit direction or pe
  "GASToolsets.GameplayCueToolset.RemoveCueTag": "mutate",

  /* ── geometry_toolset.geometry_ops.GeometryTools ───────────────── */
  // Appends a box of the given dimensions to the component's mesh.
  "geometry_toolset.geometry_ops.GeometryTools.append_box": "mutate", // EPIC accessMode=write
  // Appends a cone (or truncated cone) to the component's mesh.
  "geometry_toolset.geometry_ops.GeometryTools.append_cone": "mutate", // EPIC accessMode=write
  // Appends a cylinder of the given radius and height to the component's mesh.
  "geometry_toolset.geometry_ops.GeometryTools.append_cylinder": "mutate", // EPIC accessMode=write
  // Appends a flat disc to the component's mesh.
  "geometry_toolset.geometry_ops.GeometryTools.append_disc": "mutate", // EPIC accessMode=write
  // Appends a sphere of the given radius to the component's mesh.
  "geometry_toolset.geometry_ops.GeometryTools.append_sphere": "mutate", // EPIC accessMode=write
  // Appends a torus to the component's mesh.
  "geometry_toolset.geometry_ops.GeometryTools.append_torus": "mutate", // EPIC accessMode=write

  /* ── InputToolset.InputToolset ─────────────────────────────────── */
  // Presses one or more actions/keys together, holds them for DurationSeconds, then releases them, completing once
  "InputToolset.InputToolset.ApplyInput": "mutate", // EPIC accessMode=write
  // Reports whether each entry is currently active. Only Key reads player input.
  "InputToolset.InputToolset.IsInputApplied": "read", // EPIC accessMode=read

  /* ── KnowledgeBaseToolset.KnowledgeBaseTools ───────────────────── */
  // Creates an empty markdown base under the project's Content/KnowledgeBases folder, writes the marker file that 
  "KnowledgeBaseToolset.KnowledgeBaseTools.CreateMarkdownKnowledgeBase": "mutate", // EPIC accessMode=write
  // Returns one entry by id with its full body. Ids are exact, so take them from QueryKnowledgeBase hits rather th
  "KnowledgeBaseToolset.KnowledgeBaseTools.GetKnowledgeEntry": "read", // EPIC accessMode=read
  // Lists every registered knowledge base. Call this first: aliases are the handle every other tool takes and they
  "KnowledgeBaseToolset.KnowledgeBaseTools.ListKnowledgeBases": "read", // EPIC accessMode=read
  // Lists the tags a base's entries carry, most common first, so a Tags filter is spelled from what exists rather 
  "KnowledgeBaseToolset.KnowledgeBaseTools.ListKnowledgeBaseTags": "read", // EPIC accessMode=read
  // Searches one base. Text is matched as plain words and ranked; Tags keep only entries carrying all of them; bot
  "KnowledgeBaseToolset.KnowledgeBaseTools.QueryKnowledgeBase": "read", // EPIC accessMode=read
  // Writes a structured entry into a markdown base as a JSON block, preserving the prose and other blocks in the f
  "KnowledgeBaseToolset.KnowledgeBaseTools.WriteKnowledge": "mutate", // EPIC accessMode=write

  /* ── LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary ───── */
  // Clear the focused sequence's selection range as an undoable step, as Clear Selection Range in the time slider 
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.ClearSelectionRange": "mutate", // EPIC accessMode=write
  // Create a new Level Sequence asset without allowing an existing asset to be overwritten.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.CreateLevelSequence": "mutate", // EPIC accessMode=write
  // Empties the current selection. Does nothing when no Sequencer is open.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.EmptySelection": "mutate", // EPIC accessMode=write
  // Get the currently opened root level sequence asset
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetCurrentLevelSequence": "read", // EPIC accessMode=read
  // Gets only folders explicitly selected in the outliner. Returns an empty array when no Sequencer is open.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetDirectlySelectedFolders": "read", // EPIC accessMode=read
  // Get the focused level sequence, which may be a subsequence of the opened root sequence. Returns nullptr when n
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetFocusedLevelSequence": "read", // EPIC accessMode=read
  // Get the current global playhead position
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetGlobalPosition": "read", // EPIC accessMode=read
  // Get loop mode (note this is a per user preference)
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetLoopMode": "read", // EPIC accessMode=read
  // Get playback speed of the current level sequence
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetPlaybackSpeed": "read", // EPIC accessMode=read
  // Gets the currently selected object bindings for the focused sequence.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedBindings": "read", // EPIC accessMode=read
  // Gets the currently selected channels.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedChannels": "read", // EPIC accessMode=read
  // Gets selected folders, including parent folders of selected tracks or bindings. Returns an empty array when no
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedFolders": "read", // EPIC accessMode=read
  // Gets the currently selected sections, or an empty array when no Sequencer is open.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedSections": "read", // EPIC accessMode=read
  // Gets the currently selected tracks, or an empty array when no Sequencer is open.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedTracks": "read", // EPIC accessMode=read
  // Get the selection range end frame.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectionRangeEnd": "read", // EPIC accessMode=read
  // Get the selection range start frame.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectionRangeStart": "read", // EPIC accessMode=read
  // Get the current sub section hierarchy from the current sequence to the section associated with the focused seq
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSubSequenceHierarchy": "read", // EPIC accessMode=read
  // Gets available localized filter display names, or an empty array when no Sequencer is open.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetTrackFilterNames": "read", // EPIC accessMode=read
  // Gets the filter bar's search text. Raises an error when no Sequencer is open.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetTrackFilterText": "read", // EPIC accessMode=read
  // Whether the focused sequence has a selection range. When it does not, the selection range start and end are bo
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.HasSelectionRange": "read", // EPIC accessMode=read
  // Check whether the lock for the viewport to the camera cuts is enabled.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsCameraCutLockedToViewport": "read", // EPIC accessMode=read
  // Check whether the current level sequence and its descendants are locked for editing.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsLevelSequenceLocked": "read", // EPIC accessMode=read
  // Check whether the sequence is actively playing.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsPlaying": "read", // EPIC accessMode=read
  // Checks active state by the filter's localized display name. Unknown names and a closed Sequencer return false.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsTrackFilterActive": "read", // EPIC accessMode=read
  // Adds bindings of the focused sequence to the selection without clearing it. Raises an error when no Sequencer 
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectBindings": "mutate", // EPIC accessMode=write
  // Adds channels to the selection without clearing it. A channel name matches a channel of the section or the lab
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectChannels": "mutate", // EPIC accessMode=write
  // Adds folders from the focused sequence to the selection without clearing it. Raises an error when no Sequencer
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectFolders": "mutate", // EPIC accessMode=write
  // Adds sections from the focused sequence to the selection without clearing it. Raises an error when no Sequence
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectSections": "mutate", // EPIC accessMode=write
  // Adds tracks from the focused sequence to the selection without clearing it. Raises an error when no Sequencer 
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectTracks": "mutate", // EPIC accessMode=write
  // Sets the lock for the current level sequence and its descendants for editing.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetLockLevelSequence": "mutate", // EPIC accessMode=write
  // Set loop mode (note this is a per user preference)
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetLoopMode": "mutate", // EPIC accessMode=write
  // Set playback speed of the current level sequence. Negative speeds play in reverse; zero and non-finite speeds 
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetPlaybackSpeed": "mutate", // EPIC accessMode=write
  // Set the selection range end frame.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetSelectionRangeEnd": "mutate", // EPIC accessMode=write
  // Set the selection range start frame.
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetSelectionRangeStart": "mutate", // EPIC accessMode=write
  // Sets the specified track filter to be on or off by display name, ignoring case. Raises an error when no Sequen
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetTrackFilterActive": "mutate", // EPIC accessMode=write
  // Sets the filter bar's search text, as typing in the search box does. An empty string clears it. Raises an erro
  "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetTrackFilterText": "mutate", // EPIC accessMode=write

  /* ── LevelSequenceEditor.LevelSequenceEditorSubsystem ──────────── */
  // Add existing actors to Sequencer. Tracks will be automatically added based on default track settings.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddActors": "mutate", // EPIC accessMode=write
  // Add all actors in the Sequencer playback world whose labels match the supplied names. Raises an error when no 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddActorsByName": "mutate", // EPIC accessMode=write
  // Assigns the given actors to the binding
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddActorsToBinding": "mutate", // EPIC accessMode=write
  // Add folders, object bindings, tracks or track rows to a Sequencer Group of the focused sequence, as the Group 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddNodesToNodeGroup": "mutate", // EPIC accessMode=write
  // Add a new binding to this sequence that will spawn the specified class.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddSpawnableFromClass": "mutate", // EPIC accessMode=write
  // Add a new binding to this sequence that will spawn the specified object.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddSpawnableFromInstance": "mutate", // EPIC accessMode=write
  // Bake transform
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.BakeTransformWithSettings": "mutate", // EPIC accessMode=write
  // Sets the actor class for the spawnable or replaceable template, in the case those binding types support templa
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ChangeActorTemplateClass": "mutate", // EPIC accessMode=write
  // Switch every Cinematic Viewport back to the type its slot had before OpenCinematicViewport switched it, or to 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CloseCinematicViewport": "mutate", // EPIC accessMode=write
  // Close the Curve Editor for the active Sequencer. Requires an active Sequencer.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CloseCurveEditor": "mutate", // EPIC accessMode=write
  // Close the current sequence and wait for the editor world to tick.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CloseLevelSequenceAsync": "mutate", // EPIC accessMode=write
  // Collapse every item in the active Sequencer's open Curve Editor tree.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CollapseAllCurveEditorNodes": "mutate", // EPIC accessMode=write
  // Convert to a custom binding of the given binding type. Raises an error when the binding cannot be converted, i
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToCustomBinding": "mutate", // EPIC accessMode=write
  // Convert to possessable. If there are multiple objects assigned to the spawnable.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToPossessable": "mutate", // EPIC accessMode=write
  // Convert to spawnable. If there are multiple objects assigned to the possessable, multiple spawnables will be c
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToSpawnable": "mutate", // EPIC accessMode=write
  // Copy bindings of the focused sequence to the clipboard and return the text accepted by PasteBindingsFromText. 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopyBindingsToText": "mutate", // EPIC accessMode=write
  // Copy folders of the focused sequence to the clipboard and return all three text payloads. Raises when no Seque
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopyFoldersToText": "mutate", // EPIC accessMode=write
  // Copy sections to the clipboard and return the text accepted by PasteSectionsFromText.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopySectionsToText": "mutate", // EPIC accessMode=write
  // Copy tracks to the clipboard and return the text accepted by PasteTracksFromText.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopyTracksToText": "mutate", // EPIC accessMode=write
  // Create a cine camera in the active Sequencer and return its binding. A spawnable camera is owned by the sequen
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CreateCameraBinding": "mutate", // EPIC accessMode=write
  // Create a Sequencer Group in the focused sequence holding Nodes, which may be empty. Nodes must be folders, obj
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.CreateNodeGroup": "mutate", // EPIC accessMode=write
  // Delete a Sequencer Group of the focused sequence
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.DeleteNodeGroup": "mutate", // EPIC accessMode=write
  // Clear the active Curve Editor's selected keys.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.EmptyCurveEditorSelection": "mutate", // EPIC accessMode=write
  // Expand every item in the active Sequencer's open Curve Editor tree.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ExpandAllCurveEditorNodes": "mutate", // EPIC accessMode=write
  // Expand or collapse every row that has a path, including rows whose path is ambiguous, returning the number upd
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ExpandAllOutlinerNodes": "mutate", // EPIC accessMode=write
  // Attempts to automatically fix up broken actor references in the current scene
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.FixActorReferences": "mutate", // EPIC accessMode=write
  // Focus a sub-sequence and wait for its editor world evaluation. Fails for sub-sequences hidden from scripting i
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.FocusLevelSequenceAsync": "mutate", // EPIC accessMode=write
  // Focus the parent sequence and wait for its editor world evaluation. Fails when the parent is hidden from scrip
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.FocusParentSequenceAsync": "mutate", // EPIC accessMode=write
  // Evaluate the current sequence and wait for bound skeletal meshes to refresh.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ForceUpdateAsync": "mutate", // EPIC accessMode=write
  // Frame the active Sequencer's open Curve Editor on its time bounds and the values of every curve shown.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.FrameAllCurves": "mutate", // EPIC accessMode=write
  // Frame the keys selected in the active Sequencer's open Curve Editor.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.FrameCurveEditorSelection": "mutate", // EPIC accessMode=write
  // The active Cinematic Viewport, or the first one in the level editor's viewport order, by tab then slot, when t
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetActiveCinematicViewport": "read", // EPIC accessMode=read
  // Resolve a binding in the active editor's focused sequence context. Cross-sequence bindings require unique inst
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetBoundObjectsForBinding": "read", // EPIC accessMode=read
  // Return channel proxies containing selected Curve Editor keys.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorChannelsWithSelectedKeys": "read", // EPIC accessMode=read
  // Return the channels whose curves are pinned in the active Sequencer's Curve Editor.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorPinnedChannels": "read", // EPIC accessMode=read
  // Return selected key indices for a channel proxy in the active Curve Editor.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorSelectedKeys": "read", // EPIC accessMode=read
  // Report how the active Sequencer's Curve Editor lays out its curves.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorViewMode": "read", // EPIC accessMode=read
  // In the case that the given binding proxy holds custom bindings, returns an array of the binding objects so pro
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingObjects": "read", // EPIC accessMode=read
  // Returns all of the bindings in the sequence of the given custom type.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingsOfType": "read", // EPIC accessMode=read
  // Returns the custom binding type for the given binding, or nullptr for possessables
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingType": "read", // EPIC accessMode=read
  // Get Deactivated Nodes
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetDeactivatedNodes": "read", // EPIC accessMode=read
  // The network mode the active Sequencer's root sequence emulates during playback. Session state; not saved with 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetEmulatedNetworkMask": "read", // EPIC accessMode=read
  // The frame overlays currently enabled
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetEnabledFilmOverlayFrames": "read", // EPIC accessMode=read
  // The composition overlay of a Cinematic Viewport, or None when it shows none. Raises when the viewport is not o
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlay": "read", // EPIC accessMode=read
  // Names of the frame overlays, such as TitleSafe and LetterBox, any number of which every Cinematic Viewport sho
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayFrameNames": "read", // EPIC accessMode=read
  // The color a frame overlay draws with. Raises when the name is unknown.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayFrameTint": "read", // EPIC accessMode=read
  // Names of the composition overlays a Cinematic Viewport shows one of at a time, such as Grid3x3
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayNames": "read", // EPIC accessMode=read
  // The tint a Cinematic Viewport applies to the composition overlay it shows. Raises when the viewport is not ope
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayTint": "read", // EPIC accessMode=read
  // Nodes hidden through SetNodeHidden, without the descendants hidden with them
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetHiddenNodes": "read", // EPIC accessMode=read
  // Nodes isolated through SetNodeIsolated, without the descendants shown with them
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetIsolatedNodes": "read", // EPIC accessMode=read
  // The width and height terms of the ratio the LetterBox frame overlay masks the viewport to, as X and Y, as the 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetLetterboxAspectRatio": "read", // EPIC accessMode=read
  // Get Locked Nodes
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetLockedNodes": "read", // EPIC accessMode=read
  // Get Muted Nodes
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetMutedNodes": "read", // EPIC accessMode=read
  // Get Node Group Name
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroupName": "read", // EPIC accessMode=read
  // The members of a Sequencer Group that the outliner currently shows with a unique path, in tree order
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroupNodes": "read", // EPIC accessMode=read
  // Every node path stored in a Sequencer Group, in stored order, including paths of nodes the outliner no longer 
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroupPaths": "read", // EPIC accessMode=read
  // The Sequencer Groups of the focused sequence, as the Group Manager lists them
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroups": "read", // EPIC accessMode=read
  // Get Node Label
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeLabel": "read", // EPIC accessMode=read
  // The Cinematic Viewports the level editor shows, in its viewport order, by tab then slot
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOpenCinematicViewports": "read", // EPIC accessMode=read
  // Get immediate children with a unique path, optionally matching a node type name.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerChildren": "read", // EPIC accessMode=read
  // List addressable nodes in tree order, including their current labels and types. Nodes that share a path with a
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerNodes": "read", // EPIC accessMode=read
  // List the unique node paths in tree order; paths shared by several nodes are omitted.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerPaths": "read", // EPIC accessMode=read
  // Get selected nodes that have a unique path.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerSelection": "read", // EPIC accessMode=read
  // Snapshot the whole hierarchy in tree order, with parent indices into the returned array. Entries whose path an
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerTree": "read", // EPIC accessMode=read
  // Get Pinned Nodes
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPinnedNodes": "read", // EPIC accessMode=read
  // Get the world and actor the open Level Sequence editor currently evaluates in. Raises when no Level Sequence e
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPlaybackContext": "read", // EPIC accessMode=read
  // List the worlds and actors the open Level Sequence editor can evaluate in, as its world picker does. Raises wh
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPlaybackContextOptions": "read", // EPIC accessMode=read
  // The culture this sequence previews localized assets in, or empty when its sections preview their unlocalized a
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPreviewLanguage": "read", // EPIC accessMode=read
  // The inset of a safe frame overlay as a percentage of the viewport. Raises when the name is not a safe frame.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSafeFrameSize": "read", // EPIC accessMode=read
  // Collect unique sections from these nodes and their descendants without changing selection.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSectionsForNodes": "read", // EPIC accessMode=read
  // Get Soloed Nodes
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSoloedNodes": "read", // EPIC accessMode=read
  // Whether a level editor viewport is currently a Cinematic Viewport
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCinematicViewportOpen": "read", // EPIC accessMode=read
  // Report whether the active Sequencer's Curve Editor is open.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCurveEditorOpen": "read", // EPIC accessMode=read
  // Report whether a channel's curve is pinned in the active Sequencer's Curve Editor.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCurvePinned": "read", // EPIC accessMode=read
  // Report whether a channel's curve is displayed in the active Curve Editor.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCurveShown": "read", // EPIC accessMode=read
  // Whether a frame overlay is enabled. Raises when the name is unknown.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsFilmOverlayFrameEnabled": "read", // EPIC accessMode=read
  // Is Node Expanded
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeExpanded": "read", // EPIC accessMode=read
  // Read expansion state; missing or ambiguous paths raise an error.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeExpandedByPath": "read", // EPIC accessMode=read
  // Whether the outliner is filtered to the members of a Sequencer Group
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeGroupFilterEnabled": "read", // EPIC accessMode=read
  // Whether the node is hidden, through SetNodeHidden on it or on an ancestor
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeHidden": "read", // EPIC accessMode=read
  // Whether the node is isolated, through SetNodeIsolated on it or on an ancestor
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeIsolated": "read", // EPIC accessMode=read
  // Switch the active level editor viewport to the Cinematic Viewport type unless one is already open, which persi
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.OpenCinematicViewport": "mutate", // EPIC accessMode=write
  // Open the Curve Editor for the active Sequencer. Requires an active Sequencer.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.OpenCurveEditor": "mutate", // EPIC accessMode=write
  // Open a sequence and wait for its first editor world evaluation. Fails for sequences hidden from scripting in t
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.OpenLevelSequenceAsync": "mutate", // EPIC accessMode=write
  // Paste bindings and return the new objects, raising a script error if nothing was pasted. Empty text uses the c
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteBindingsFromText": "mutate", // EPIC accessMode=write
  // Paste folders and return the new objects, raising a script error if nothing was pasted. Empty text uses the cl
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteFoldersFromText": "mutate", // EPIC accessMode=write
  // Paste sections and return the new objects, raising a script error if nothing was pasted. Empty text uses the c
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteSectionsFromText": "mutate", // EPIC accessMode=write
  // Paste tracks and return the new objects, raising a script error if nothing was pasted. Empty text uses the cli
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteTracksFromText": "mutate", // EPIC accessMode=write
  // Pause playback and wait for the stopped pose to evaluate.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PauseAsync": "mutate", // EPIC accessMode=write
  // Pin or unpin a channel's curve in the active Sequencer's open Curve Editor tree, as the pin button on the chan
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PinCurve": "mutate", // EPIC accessMode=write
  // Start playback and wait for the editor world to evaluate.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PlayAsync": "mutate", // EPIC accessMode=write
  // Play to a frame of the focused sequence, in its local display-rate time, and wait for the final pose. Fails if
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.PlayToFrameAsync": "mutate", // EPIC accessMode=write
  // Rebind the component binding to the requested component
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RebindComponent": "mutate", // EPIC accessMode=write
  // Refresh the current sequence and wait for the editor world to tick.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RefreshCurrentLevelSequenceAsync": "mutate", // EPIC accessMode=write
  // Removes the given actors from the binding. Raises an error when the list is empty or any actor is not bound to
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveActorsFromBinding": "mutate", // EPIC accessMode=write
  // Remove all bound actors from this track
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveAllBindings": "mutate", // EPIC accessMode=write
  // Remove missing objects bound to this track
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveInvalidBindings": "mutate", // EPIC accessMode=write
  // Remove stored node paths from a Sequencer Group of the focused sequence without resolving them, so paths of de
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveNodePathsFromNodeGroup": "mutate", // EPIC accessMode=write
  // Remove nodes from a Sequencer Group of the focused sequence. A node that is not in the group is left alone.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveNodesFromNodeGroup": "mutate", // EPIC accessMode=write
  // Place nodes among their siblings in the order given, as the outliner's Sort by Selection Order does, storing t
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ReorderNodes": "mutate", // EPIC accessMode=write
  // Replaces the binding with the given actors
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ReplaceBindingWithActors": "mutate", // EPIC accessMode=write
  // Let the open Level Sequence editor choose its world automatically again, from the auto-bind to PIE and Simulat
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ResetPlaybackContext": "mutate", // EPIC accessMode=write
  // Restore captured pre-animated state and wait for the editor world to tick.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.RestorePreAnimatedStateAsync": "mutate", // EPIC accessMode=write
  // Save the default state of the spawnable.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SaveDefaultSpawnableState": "mutate", // EPIC accessMode=write
  // Add the specified key indices to the active Curve Editor selection.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SelectCurveEditorKeys": "mutate", // EPIC accessMode=write
  // Set how the active Sequencer's Curve Editor lays out its curves.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetCurveEditorViewMode": "mutate", // EPIC accessMode=write
  // Set the network mode the active Sequencer's root sequence emulates during playback and recompile to match. Rai
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetEmulatedNetworkMask": "mutate", // EPIC accessMode=write
  // Set the network mode the active Sequencer's root sequence emulates during playback and wait for evaluation.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetEmulatedNetworkMaskAsync": "mutate", // EPIC accessMode=write
  // Show one of GetFilmOverlayNames in a Cinematic Viewport, or None for no composition overlay. Raises when the v
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlay": "mutate", // EPIC accessMode=write
  // Enable or disable a frame overlay, which every Cinematic Viewport shows while it is enabled. Raises when the n
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlayFrameEnabled": "mutate", // EPIC accessMode=write
  // Set the color a frame overlay draws with. Raises when the name is unknown.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlayFrameTint": "mutate", // EPIC accessMode=write
  // Set the tint a Cinematic Viewport applies to the composition overlay it shows, like the Film Overlay menu's ti
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlayTint": "mutate", // EPIC accessMode=write
  // Set the width and height terms of the ratio the LetterBox frame overlay masks the viewport to. Raises when eit
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetLetterboxAspectRatio": "mutate", // EPIC accessMode=write
  // Set camera-cut viewport locking and wait for evaluation.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetLockCameraCutToViewportAsync": "mutate", // EPIC accessMode=write
  // Set Node Deactivated
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeDeactivated": "mutate", // EPIC accessMode=write
  // Set Node Expanded
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeExpanded": "mutate", // EPIC accessMode=write
  // Filter the outliner to the members of a Sequencer Group, as the Group Manager's filter toggle does, or stop fi
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeGroupFilterEnabled": "mutate", // EPIC accessMode=write
  // Rename a Sequencer Group of the focused sequence. Name is stored without surrounding whitespace and must not b
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeGroupName": "mutate", // EPIC accessMode=write
  // Hide nodes from the outliner, or show nodes hidden this way again. Hiding a node hides its descendants with it
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeHidden": "mutate", // EPIC accessMode=write
  // Isolate nodes so the outliner shows only isolated nodes with their ancestors and descendants, or stop isolatin
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeIsolated": "mutate", // EPIC accessMode=write
  // Set Node Locked
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeLocked": "mutate", // EPIC accessMode=write
  // Set Node Muted
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeMuted": "mutate", // EPIC accessMode=write
  // Set Node Pinned
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodePinned": "mutate", // EPIC accessMode=write
  // Expand or collapse paths atomically. Missing or ambiguous paths raise an error.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodesExpandedByPath": "mutate", // EPIC accessMode=write
  // Set Node Solo
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeSolo": "mutate", // EPIC accessMode=write
  // Clears selection when Nodes is empty. All handles must resolve before selection changes.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetOutlinerSelection": "mutate", // EPIC accessMode=write
  // Evaluate the open Level Sequence editor in World, previewing through Client, which must match one of GetPlayba
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetPlaybackContext": "mutate", // EPIC accessMode=write
  // Move the global playhead to a frame of the root sequence, in its display rate, after pending structure changes
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetPlayheadFrameAsync": "mutate", // EPIC accessMode=write
  // Set the culture, such as "fr", whose localized sounds and animations this sequence's own sections preview in t
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetPreviewLanguage": "mutate", // EPIC accessMode=write
  // Set the inset of a safe frame overlay, from 1 to 99 percent of the viewport. Raises when the name is not a saf
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetSafeFrameSize": "mutate", // EPIC accessMode=write
  // Show every hidden and isolated node again, as the filter bar's Show All Tracks does
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ShowAllNodes": "mutate", // EPIC accessMode=write
  // Show or hide a channel's curve in the active Curve Editor.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.ShowCurve": "mutate", // EPIC accessMode=write
  // Sort the items of the active Sequencer's open Curve Editor tree.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SortAllCurveEditorNodes": "mutate", // EPIC accessMode=write
  // Sort every node by type and then by label, discarding stored sorting orders, as Sequencer's Sort All Nodes com
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SortAllNodes": "mutate", // EPIC accessMode=write
  // Sort nodes among their siblings by the start of their earliest section, as the outliner's Sort by Start Time d
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.SortNodesByStartTime": "mutate", // EPIC accessMode=write
  // Wait for a complete editor world tick after this call. TimeoutSeconds must be positive.
  "LevelSequenceEditor.LevelSequenceEditorSubsystem.WaitForEvaluationAsync": "mutate", // EPIC accessMode=write

  /* ── LiveCodingToolset.LiveCodingToolset ───────────────────────── */
  // Triggers a Live Coding compile and returns a pending result that resolves once the compile finishes. Live Codi
  "LiveCodingToolset.LiveCodingToolset.CompileLiveCoding": "unknown", // EPIC accessMode=execute
  // Call before CompileLiveCoding on a session launched with Live Coding off; starts the Broker/Agent.
  "LiveCodingToolset.LiveCodingToolset.EnableLiveCoding": "unknown", // EPIC accessMode=execute
  // Poll before calling CompileLiveCoding to avoid blocking on the Broker's initial module load.
  "LiveCodingToolset.LiveCodingToolset.IsLiveCodingReady": "read", // EPIC accessMode=read

  /* ── MediaToolset.MediaToolset ─────────────────────────────────── */
  // Reads container metadata. Windows Media Foundation is required.
  "MediaToolset.MediaToolset.GetVideoInfo": "read", // EPIC accessMode=read
  // Reads up to 64 MiB of encoded PNG, JPEG, GIF, BMP, or WebP data without re-encoding.
  "MediaToolset.MediaToolset.ReadImageFile": "read", // EPIC accessMode=read
  // Samples midpoint-spaced frames in a time range. Windows only.
  "MediaToolset.MediaToolset.SampleFrames": "read", // EPIC accessMode=read
  // Samples midpoint-spaced frames over the video's known duration. Windows only.
  "MediaToolset.MediaToolset.SkimVideo": "read", // EPIC accessMode=read

  /* ── MetaSoundToolset.MetaSoundAuthoringToolset ────────────────── */
  // Declares graph inputs. Each becomes a node whose ref is the input's own name.
  "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphInputs": "mutate", // EPIC accessMode=write
  // Declares graph outputs. Each becomes a node whose ref is the output's own name. A Source's audio output is alr
  "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphOutputs": "mutate", // EPIC accessMode=write
  // Adds a paged graph. Page must be declared (FindRegisteredPages) and not already carried (the record's Pages). 
  "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphPage": "mutate", // EPIC accessMode=write
  // Declares graph variables, which carry state across blocks. Referenced by name thereafter.
  "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphVariables": "mutate", // EPIC accessMode=write
  // Adds nodes by registered class. Returns each new node's ref in the report's Nodes.
  "MetaSoundToolset.MetaSoundAuthoringToolset.AddNodes": "mutate", // EPIC accessMode=write
  // Adds Set / Get / Delayed-Get nodes for already-declared graph variables.
  "MetaSoundToolset.MetaSoundAuthoringToolset.AddVariableNodes": "mutate", // EPIC accessMode=write
  // Attaches an editable builder to an existing MetaSound under BuilderName, targeting Page. The builder is the en
  "MetaSoundToolset.MetaSoundAuthoringToolset.AttachBuilderToAsset": "mutate", // EPIC accessMode=write
  // Writes the builder to a NEW asset on disk. Returns the new asset's object path. On an attached builder this fo
  "MetaSoundToolset.MetaSoundAuthoringToolset.BuildToAsset": "mutate", // EPIC accessMode=write
  // Removes every edge touching the named pins, without knowing what is wired to them. An input takes at most one 
  "MetaSoundToolset.MetaSoundAuthoringToolset.ClearPins": "mutate", // EPIC accessMode=write
  // Saves an attached builder's edits back to its source asset and re-registers its graph, so the next play of the
  "MetaSoundToolset.MetaSoundAuthoringToolset.CommitBuilderToAsset": "mutate", // EPIC accessMode=write
  // Wires outputs to inputs. Every edge is resolved before any is made. Direction is not checked beyond type: same
  "MetaSoundToolset.MetaSoundAuthoringToolset.Connect": "mutate", // EPIC accessMode=write
  // Creates a Patch builder registered under BuilderName. Persists for the session. No built-in interface; its gra
  "MetaSoundToolset.MetaSoundAuthoringToolset.CreatePatchBuilder": "mutate", // EPIC accessMode=write
  // Same contract as CreateSourcePresetBuilder, for MetaSoundPatch parents.
  "MetaSoundToolset.MetaSoundAuthoringToolset.CreatePatchPresetBuilder": "mutate", // EPIC accessMode=write
  // Creates a Source builder registered under BuilderName. Persists for the session. The returned record already h
  "MetaSoundToolset.MetaSoundAuthoringToolset.CreateSourceBuilder": "mutate", // EPIC accessMode=write
  // Creates a Source preset over an existing MetaSoundSource. The returned record's inputs are the whole overridab
  "MetaSoundToolset.MetaSoundAuthoringToolset.CreateSourcePresetBuilder": "mutate", // EPIC accessMode=write
  // Unregisters an attached builder and drops its attachment record. Returns BuilderName. Uncommitted edits are lo
  "MetaSoundToolset.MetaSoundAuthoringToolset.DetachBuilderFromAsset": "mutate", // EPIC accessMode=write
  // Removes specific edges. Takes the same records a read reports.
  "MetaSoundToolset.MetaSoundAuthoringToolset.Disconnect": "mutate", // EPIC accessMode=write
  // MetaSound assets in content, ordered by path. Where preset authoring starts: each can parent a preset, and a p
  "MetaSoundToolset.MetaSoundAuthoringToolset.FindMetaSoundAssets": "read", // EPIC accessMode=read
  // Arranges the targeted page left to right by connection depth, discarding manual arrangement. Applied counts th
  "MetaSoundToolset.MetaSoundAuthoringToolset.LayoutGraph": "mutate", // EPIC accessMode=write
  // Removes a paged graph. Removing the targeted page moves the builder to an adjacent one unreported -- read the 
  "MetaSoundToolset.MetaSoundAuthoringToolset.RemoveGraphPage": "mutate", // EPIC accessMode=write
  // Removes graph variables, every Set and Get node bound to them, and the edges those carried. Refs to those node
  "MetaSoundToolset.MetaSoundAuthoringToolset.RemoveGraphVariables": "mutate", // EPIC accessMode=write
  // Removes nodes and every edge touching them. Refs are stale afterwards. Removing an interface-provided node lea
  "MetaSoundToolset.MetaSoundAuthoringToolset.RemoveNodes": "mutate", // EPIC accessMode=write
  // Moves the builder to another page the asset carries. Every later edit lands there. Shared with the MetaSound e
  "MetaSoundToolset.MetaSoundAuthoringToolset.SetBuildPage": "mutate", // EPIC accessMode=write
  // Sets default values on node pins and graph inputs. A node pin on a preset aborts the whole call rather than fa
  "MetaSoundToolset.MetaSoundAuthoringToolset.SetDefaults": "mutate", // EPIC accessMode=write

  /* ── MetaSoundToolset.MetaSoundDiscoveryToolset ────────────────── */
  // A registered class's default pins, without adding a node. Resolves the highest version. Errors when no class h
  "MetaSoundToolset.MetaSoundDiscoveryToolset.DescribeNodeClass": "read", // EPIC accessMode=read
  // Registered MetaSound data types matching Pattern, exact name first. TypeName is the spelling every data-type a
  "MetaSoundToolset.MetaSoundDiscoveryToolset.FindDataTypes": "read", // EPIC accessMode=read
  // Every registered MetaSound interface, at its highest registered version.
  "MetaSoundToolset.MetaSoundDiscoveryToolset.FindInterfaces": "read", // EPIC accessMode=read
  // Registered node classes matching Pattern, best first, one group per node. Highest versions only; no deprecated
  "MetaSoundToolset.MetaSoundDiscoveryToolset.FindNodeClasses": "read", // EPIC accessMode=read
  // Page names the project declares, Default first -- the set AddGraphPage draws from. A name means the same page 
  "MetaSoundToolset.MetaSoundDiscoveryToolset.FindRegisteredPages": "read", // EPIC accessMode=read

  /* ── MetaSoundToolset.MetaSoundInspectionToolset ───────────────── */
  // The whole document in one call: interfaces, graph I/O with defaults, variables, nodes with their pins and auth
  "MetaSoundToolset.MetaSoundInspectionToolset.DescribeGraph": "read", // EPIC accessMode=read
  // One node with its pins and the values authored onto them -- the record a document read reports. Errors when th
  "MetaSoundToolset.MetaSoundInspectionToolset.DescribeNode": "read", // EPIC accessMode=read
  // Compares two pages across topology and paged input defaults. Every array empty means they match. Matches by cl
  "MetaSoundToolset.MetaSoundInspectionToolset.DiffPages": "read", // EPIC accessMode=read
  // Every edge on Page, addressed by node ref. Edges whose pin names do not resolve against the document are dropp
  "MetaSoundToolset.MetaSoundInspectionToolset.FindAllConnections": "read", // EPIC accessMode=read
  // Every node on Page, interface and graph-I/O nodes included. Read DisplayName and AssetPath to tell what a node
  "MetaSoundToolset.MetaSoundInspectionToolset.FindAllNodes": "read", // EPIC accessMode=read
  // Interfaces declared on this builder.
  "MetaSoundToolset.MetaSoundInspectionToolset.FindBuilderInterfaces": "read", // EPIC accessMode=read
  // Graph-input defaults on Page. Inputs with no default there are omitted.
  "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphInputDefaults": "read", // EPIC accessMode=read
  // Graph inputs as Name / DataType, with the default set on Page.
  "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphInputs": "read", // EPIC accessMode=read
  // Graph outputs as Name / DataType. Outputs carry no default, so Default is empty.
  "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphOutputs": "read", // EPIC accessMode=read
  // Pages this asset carries, naming which one mutations target. For the wider set a page can be added from, call 
  "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphPages": "read", // EPIC accessMode=read
  // Graph variables on Page as Name / DataType / Default. Their Set and Get nodes are in the node listing, named "
  "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphVariables": "read", // EPIC accessMode=read
  // Ids of the input nodes an interface contributes on the build page; empty when undeclared.
  "MetaSoundToolset.MetaSoundInspectionToolset.FindInterfaceInputNodes": "read", // EPIC accessMode=read
  // Ids of the output nodes an interface contributes on the build page; empty when undeclared.
  "MetaSoundToolset.MetaSoundInspectionToolset.FindInterfaceOutputNodes": "read", // EPIC accessMode=read

  /* ── MVVMToolset.MVVMToolset ───────────────────────────────────── */
  // Adds a condition to the WidgetBlueprint's View: when the source value changes and compares true against Value 
  "MVVMToolset.MVVMToolset.AddViewCondition": "mutate", // EPIC accessMode=write
  // Add a property to an existing ViewModel. The ViewModel is compiled automatically when it is next added to a wi
  "MVVMToolset.MVVMToolset.AddViewModelProperty": "mutate", // EPIC accessMode=write
  // Adds a ViewModel to the WidgetBlueprint
  "MVVMToolset.MVVMToolset.AddViewModelToWidget": "mutate", // EPIC accessMode=write
  // Removes the property path bound to an input pin of a binding's conversion function, resetting the pin to its d
  "MVVMToolset.MVVMToolset.ClearConversionFunctionArgumentPath": "mutate", // EPIC accessMode=write
  // Creates a View Binding from the SourceProperty to the Destination property. If ConversionName is given, that c
  "MVVMToolset.MVVMToolset.CreateViewBinding": "mutate", // EPIC accessMode=write
  // Creates a View Event Binding from the EventProperty to the Destination property.
  "MVVMToolset.MVVMToolset.CreateViewEventBinding": "unknown", // EPIC accessMode=execute
  // Creates a new ViewModel asset
  "MVVMToolset.MVVMToolset.CreateViewModel": "mutate", // EPIC accessMode=write
  // Attempts to fixup MVVM data. This function can be called if the MVVM Generated Blueprint graphs like conversio
  "MVVMToolset.MVVMToolset.FixupMVVMData": "unknown", // EPIC accessMode=execute
  // Reads the View Bindings design-time preview toggle state for a WidgetBlueprint.
  "MVVMToolset.MVVMToolset.GetWidgetPreviewActive": "read", // EPIC accessMode=read
  // Returns the MVVM View configuration.
  "MVVMToolset.MVVMToolset.GetWidgetViewConfig": "read", // EPIC accessMode=read
  // Lists the bindable property paths available on a widget (binding source/destination candidates). Names aren't 
  "MVVMToolset.MVVMToolset.ListBindableWidgetProperties": "read", // EPIC accessMode=read
  // Reports whether SourcePropertyPath and DestinationPropertyPath bind directly, and/or which conversion function
  "MVVMToolset.MVVMToolset.ListBindingConversionOptions": "read", // EPIC accessMode=read
  // Lists every field on TargetClass valid as an MVVM binding source or destination.
  "MVVMToolset.MVVMToolset.ListClassBindableFields": "read", // EPIC accessMode=read
  // List Available Conversion functions for a WidgetBlueprint that bind a Source Property to a Destination Propert
  "MVVMToolset.MVVMToolset.ListConversionFunctions": "read", // EPIC accessMode=read
  // Resolves PropertyPath against Context and describes that single field. Unlike ListClassBindableFields, this do
  "MVVMToolset.MVVMToolset.ListPropertyPathBindableFields": "read", // EPIC accessMode=read
  // List all conditions on the WidgetBlueprint's View.
  "MVVMToolset.MVVMToolset.ListViewConditions": "read", // EPIC accessMode=read
  // List all ViewModel under the search path
  "MVVMToolset.MVVMToolset.ListViewModels": "read", // EPIC accessMode=read
  // List all view bindings on the WidgetBlueprint (including possible conversion functions).
  "MVVMToolset.MVVMToolset.ListWidgetViewBindings": "read", // EPIC accessMode=read
  // List all event bindings currently configured on the WidgetBlueprint's View, along with their context informati
  "MVVMToolset.MVVMToolset.ListWidgetViewEventBindings": "read", // EPIC accessMode=read
  // List Available Event Properties for a WidgetBlueprint (i.e. a Multicast Delegate)
  "MVVMToolset.MVVMToolset.ListWidgetViewEvents": "unknown", // EPIC accessMode=execute
  // List all view models used by a widget Blueprint
  "MVVMToolset.MVVMToolset.ListWidgetViewModels": "read", // EPIC accessMode=read
  // Exports a property value from the live design-time preview widget of an open WidgetBlueprint designer. Use aft
  "MVVMToolset.MVVMToolset.ReadPreviewWidgetProperty": "read", // EPIC accessMode=read
  // Removes a condition from the WidgetBlueprint's View. Not supported during PIE.
  "MVVMToolset.MVVMToolset.RemoveViewCondition": "mutate", // EPIC accessMode=write
  // Removes View Binding on the WidgetBlueprint
  "MVVMToolset.MVVMToolset.RemoveWidgetViewBinding": "mutate", // EPIC accessMode=write
  // Sets the binding mode (OneTimeToDestination/OneWayToDestination/TwoWay/OneWayToSource) for an existing view bi
  "MVVMToolset.MVVMToolset.SetBindingMode": "mutate", // EPIC accessMode=write
  // Sets a literal value on an input pin of a binding's conversion function, then recompiles the WidgetBlueprint. 
  "MVVMToolset.MVVMToolset.SetConversionFunctionArgumentLiteral": "mutate", // EPIC accessMode=write
  // Sets a literal value on an input pin of an event binding, then recompiles the WidgetBlueprint if the edit chan
  "MVVMToolset.MVVMToolset.SetEventBindingArgumentLiteral": "mutate", // EPIC accessMode=write
  // Sets the comparison of an existing condition, then compiles the widget blueprint; compile errors do not fail t
  "MVVMToolset.MVVMToolset.SetViewConditionOperation": "mutate", // EPIC accessMode=write
  // Turns the View Bindings design-time preview on or off for a WidgetBlueprint, exactly like the eye toggle on th
  "MVVMToolset.MVVMToolset.SetWidgetPreviewActive": "mutate", // EPIC accessMode=write
  // Changes the enabled and compile flag for an existing widget view binding.
  "MVVMToolset.MVVMToolset.SetWidgetViewBindingEnabled": "mutate", // EPIC accessMode=write
  // Writes the MVVM View configuration on the WidgetBlueprint's View.
  "MVVMToolset.MVVMToolset.SetWidgetViewConfig": "mutate", // EPIC accessMode=write

  /* ── NavMeshToolset.NavMeshToolset ─────────────────────────────── */
  // Dumps the full generation and query configuration of one navmesh.
  "NavMeshToolset.NavMeshToolset.DescribeNavMesh": "read", // EPIC accessMode=read
  // Describes the navmesh polygon nearest a location.
  "NavMeshToolset.NavMeshToolset.DescribePolyAtLocation": "read", // EPIC accessMode=read
  // Measures the distance from a point to the nearest navmesh wall (walkable-space boundary).
  "NavMeshToolset.NavMeshToolset.FindDistanceToWall": "read", // EPIC accessMode=read
  // Finds a path between two points. Area cost overrides and exclusions apply to this query only; no state is touc
  "NavMeshToolset.NavMeshToolset.FindPath": "read", // EPIC accessMode=read
  // Deep navigation report for one actor: everything navigation-relevant it carries and, when it follows paths, wh
  "NavMeshToolset.NavMeshToolset.GetActorNavInfo": "read", // EPIC accessMode=read
  // Reports the state of a world's navigation system.
  "NavMeshToolset.NavMeshToolset.GetNavigationSystemInfo": "read", // EPIC accessMode=read
  // Reports whether navmesh building is up to date and what rebuild work is pending or suspended. Addressing a nav
  "NavMeshToolset.NavMeshToolset.GetNavMeshBuildStatus": "read", // EPIC accessMode=read
  // Computes the navmesh region reachable from an origin within a path-cost budget, connectivity respected.
  "NavMeshToolset.NavMeshToolset.GetReachableArea": "read", // EPIC accessMode=read
  // Describes the navmesh tile containing a location.
  "NavMeshToolset.NavMeshToolset.GetTileInfo": "read", // EPIC accessMode=read
  // Lists pawns as navigation agents, each with the navmesh it resolves to.
  "NavMeshToolset.NavMeshToolset.ListNavAgentActors": "read", // EPIC accessMode=read
  // Lists the concrete nav area classes in the process (engine and game); abstract, deprecated, and superseded cla
  "NavMeshToolset.NavMeshToolset.ListNavAreas": "read", // EPIC accessMode=read
  // Lists every registered navigation bounds region and its supported agents. Navmesh only generates inside these 
  "NavMeshToolset.NavMeshToolset.ListNavBoundsVolumes": "read", // EPIC accessMode=read
  // Lists the navigation data actors registered with the world's navigation system. Non-Recast navdata are include
  "NavMeshToolset.NavMeshToolset.ListNavData": "read", // EPIC accessMode=read
  // Reports the navigation invokers driving tile generation. On invoker-driven maps navmesh only exists near a reg
  "NavMeshToolset.NavMeshToolset.ListNavInvokers": "read", // EPIC accessMode=read
  // Lists nav links from three sources: nav link proxy actors, custom link components, and links baked into the na
  "NavMeshToolset.NavMeshToolset.ListNavLinks": "read", // EPIC accessMode=read
  // Lists the nav modifier volumes and components (area carvers) shaping the navmesh.
  "NavMeshToolset.NavMeshToolset.ListNavModifiers": "read", // EPIC accessMode=read
  // Lists the agent configs a world supports and the live navigation data each one resolves to.
  "NavMeshToolset.NavMeshToolset.ListSupportedAgents": "read", // EPIC accessMode=read
  // Lists the worlds a navmesh tool call can target (editor, PIE server, PIE clients, game).
  "NavMeshToolset.NavMeshToolset.ListWorlds": "read", // EPIC accessMode=read
  // Projects a point onto navigation data: does walkable space exist here, and where exactly.
  "NavMeshToolset.NavMeshToolset.ProjectPointToNavMesh": "read", // EPIC accessMode=read
  // Queries the navigation octree for the elements contributing geometry or modifiers around a location: the autho
  "NavMeshToolset.NavMeshToolset.QueryNavOctree": "read", // EPIC accessMode=read
  // Casts a ray across the navmesh surface and reports where walkable space ends.
  "NavMeshToolset.NavMeshToolset.RaycastNavMesh": "read", // EPIC accessMode=read

  /* ── NiagaraToolsets.NiagaraToolset_Assets ─────────────────────── */
  // Searches for UNiagaraScript assets matching the given filters.
  "NiagaraToolsets.NiagaraToolset_Assets.FindNiagaraScripts": "read",
  // Returns the project's configured asset discovery groups.
  "NiagaraToolsets.NiagaraToolset_Assets.GetAssetDiscoveryInfo": "read",
  // Returns the decoded asset-registry tag metadata for a Niagara script asset.
  "NiagaraToolsets.NiagaraToolset_Assets.GetNiagaraScriptDigest": "read",
  // Sets the Description field on a Niagara script's FVersionedNiagaraScriptData. Does not checkout or save the as
  "NiagaraToolsets.NiagaraToolset_Assets.SetNiagaraScriptDescription": "mutate", // EPIC accessMode=write

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

  /* ── OptimusScripting.DeformerGraphScriptingSubsystem ──────────── */
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddComment": "mutate", // EPIC accessMode=write
  // Adds a binding to another component of the actor than the one the deformer is applied to. Link a "ComponentBin
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddComponentBinding": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddGraph": "mutate", // EPIC accessMode=write
  // Adds a binding to the signature of a sub-graph: a pin on its entry node (input) or return node (output), and o
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddGraphBinding": "mutate", // EPIC accessMode=write
  // Adds a custom compute kernel with no bindings and an empty source.
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddKernel": "mutate", // EPIC accessMode=write
  // Adds a binding and its pin. An input named "Position" gives the source a ReadPosition function, an output a Wr
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddKernelBinding": "mutate", // EPIC accessMode=write
  // Adds a secondary input group: inputs read from another component binding than the kernel's primary group, decl
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddKernelInputGroup": "mutate", // EPIC accessMode=write
  // Links an output pin to an input pin, replacing the input pin's existing link if any.
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddLink": "mutate", // EPIC accessMode=write
  // Adds a pin to a node that supports it (a kernel adds a binding) with the name, type and data domain of SourceP
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddLinkWithNewPin": "mutate", // EPIC accessMode=write
  // Adds a value carried through the loop: an input and an output pin on both loop nodes.
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddLoopBinding": "mutate", // EPIC accessMode=write
  // Adds a node. Data interface nodes read from the primary component binding unless a component binding node is l
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddNode": "mutate", // EPIC accessMode=write
  // Adds a resource: a GPU buffer kernels write and read, kept between frames, accessed with "Resource/<Name>", "R
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddResource": "mutate", // EPIC accessMode=write
  // Adds a variable, set per deformer instance at runtime and read with "Variable/<Name>" nodes.
  "OptimusScripting.DeformerGraphScriptingSubsystem.AddVariable": "mutate", // EPIC accessMode=write
  // Compiles the graphs and their shaders. Not undoable. Completes once shader compilation has finished. Raises if
  "OptimusScripting.DeformerGraphScriptingSubsystem.Compile": "mutate", // EPIC accessMode=write
  // Turns a sub-graph into a function graph of the deformer, and the sub-graph node into a node calling it. Functi
  "OptimusScripting.DeformerGraphScriptingSubsystem.ConvertSubGraphToFunction": "mutate", // EPIC accessMode=write
  // Creates a deformer asset with an empty UpdateGraph and the primary component binding.
  "OptimusScripting.DeformerGraphScriptingSubsystem.CreateDeformer": "mutate", // EPIC accessMode=write
  // Returns the compile status, graphs, variables, resources and component bindings of a deformer.
  "OptimusScripting.DeformerGraphScriptingSubsystem.GetDeformerInfo": "read", // EPIC accessMode=read
  // Graphs
  "OptimusScripting.DeformerGraphScriptingSubsystem.GetGraphInfo": "read", // EPIC accessMode=read
  // Kernels
  "OptimusScripting.DeformerGraphScriptingSubsystem.GetKernelInfo": "read", // EPIC accessMode=read
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.GetLoopInfo": "read", // EPIC accessMode=read
  // Nodes
  "OptimusScripting.DeformerGraphScriptingSubsystem.GetNodeInfo": "read", // EPIC accessMode=read
  // List Component Sources
  "OptimusScripting.DeformerGraphScriptingSubsystem.ListComponentSources": "read", // EPIC accessMode=read
  // List Data Types
  "OptimusScripting.DeformerGraphScriptingSubsystem.ListDataTypes": "read", // EPIC accessMode=read
  // Lists the node types that can be added to the deformer's graphs, including nodes reading its own variables, re
  "OptimusScripting.DeformerGraphScriptingSubsystem.ListNodeTypes": "read", // EPIC accessMode=read
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveAllLinks": "mutate", // EPIC accessMode=write
  // Removes a binding and every node reading it. The primary binding cannot be removed.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveComponentBinding": "mutate", // EPIC accessMode=write
  // Removes a graph and its nodes. The update graph cannot be removed.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveGraph": "mutate", // EPIC accessMode=write
  // Removes a binding from the signature of a sub-graph, with its pins and links.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveGraphBinding": "mutate", // EPIC accessMode=write
  // Removes a binding, with its pin and links.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveKernelBinding": "mutate", // EPIC accessMode=write
  // Removes a secondary input group, with its bindings, pins and links.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveKernelInputGroup": "mutate", // EPIC accessMode=write
  // Remove Link
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveLink": "mutate", // EPIC accessMode=write
  // Remove Loop Binding
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveLoopBinding": "mutate", // EPIC accessMode=write
  // Removes nodes and their links, as one undo step. Nothing is removed if any path is invalid.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveNodes": "mutate", // EPIC accessMode=write
  // Removes a resource and every node accessing it.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveResource": "mutate", // EPIC accessMode=write
  // Removes a variable and every node reading it.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveVariable": "mutate", // EPIC accessMode=write
  // Renames a binding and its pins. Links are kept.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RenameBinding": "mutate", // EPIC accessMode=write
  // The primary binding keeps its name.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RenameComponentBinding": "mutate", // EPIC accessMode=write
  // Renames a trigger graph, function graph or sub-graph. The setup and update graphs keep their names.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RenameGraph": "mutate", // EPIC accessMode=write
  // Renames a secondary input group, which is also the HLSL namespace of its bindings. Links are kept.
  "OptimusScripting.DeformerGraphScriptingSubsystem.RenameKernelInputGroup": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.RenameResource": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.RenameVariable": "mutate", // EPIC accessMode=write
  // Sets the data domain of a binding and its pins, which removes the links the new data domain is incompatible wi
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetBindingDataDomain": "mutate", // EPIC accessMode=write
  // Sets the data type of a binding and its pins, which removes their links.
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetBindingDataType": "mutate", // EPIC accessMode=write
  // Set Comment
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetComment": "mutate", // EPIC accessMode=write
  // Changes the kind of component a binding reads from.
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetComponentBindingSource": "mutate", // EPIC accessMode=write
  // Sets a setting of a data interface node, as listed in FDeformerGraphNodeInfo::Settings. The node's pins are re
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetDataInterfaceSetting": "mutate", // EPIC accessMode=write
  // Set Function Settings
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetFunctionSettings": "mutate", // EPIC accessMode=write
  // Sets the shader source assets the kernel includes before its own source, e.g. to share HLSL functions between 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelAdditionalSources": "mutate", // EPIC accessMode=write
  // Sets the options of an output binding.
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelBindingOptions": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelExecutionDomain": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelGroupSize": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelName": "mutate", // EPIC accessMode=write
  // Sets the HLSL source. Errors in it are only reported by Compile.
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelSource": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetLoopCount": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetNodePosition": "mutate", // EPIC accessMode=write
  // Sets the value of a pin whose FDeformerGraphPinInfo::bHasValue is true.
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetPinValue": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetPreviewMesh": "mutate", // EPIC accessMode=write
  // Sets the component binding that sizes a resource, which also resets its data domain to the binding's first exe
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetResourceComponentBinding": "mutate", // EPIC accessMode=write
  // Set Resource Data Domain
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetResourceDataDomain": "mutate", // EPIC accessMode=write
  // Set Resource Data Type
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetResourceDataType": "mutate", // EPIC accessMode=write
  // Changes the type, which resets the default value and removes links the new type cannot keep.
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetVariableDataType": "mutate", // EPIC accessMode=write
  // 
  "OptimusScripting.DeformerGraphScriptingSubsystem.SetVariableDefaultValue": "mutate", // EPIC accessMode=write

  /* ── PCGEditor.PCGEditorSubsystem ──────────────────────────────── */
  // Adds a comment box around the given nodes.
  "PCGEditor.PCGEditorSubsystem.AddCommentBox": "mutate", // EPIC accessMode=write
  // Adds a native node to the graph.
  "PCGEditor.PCGEditorSubsystem.AddNode": "mutate", // EPIC accessMode=write
  // Adds a subgraph node to the graph.
  "PCGEditor.PCGEditorSubsystem.AddSubgraphNode": "mutate", // EPIC accessMode=write
  // Collapses at least two nodes into a subgraph. Empty AssetPath creates an embedded graph; otherwise creates a n
  "PCGEditor.PCGEditorSubsystem.CollapseNodesToSubgraph": "mutate", // EPIC accessMode=write
  // Add an edge between two nodes connected to the specified pins.
  "PCGEditor.PCGEditorSubsystem.ConnectNodePins": "mutate", // EPIC accessMode=write
  // Creates a new unsaved PCG graph asset with a unique name.
  "PCGEditor.PCGEditorSubsystem.CreateGraph": "mutate", // EPIC accessMode=write
  // Deletes an embedded graph. Referenced graphs raise a script error unless bAllowReferenced is true, which clear
  "PCGEditor.PCGEditorSubsystem.DeleteEmbeddedSubgraph": "mutate", // EPIC accessMode=write
  // Removes the edge between two nodes connected to the specified pins.
  "PCGEditor.PCGEditorSubsystem.DisconnectNodePins": "mutate", // EPIC accessMode=write
  // Triggers the user to draw a spline in the viewport to be used later in the world building. Waits for the user 
  "PCGEditor.PCGEditorSubsystem.DrawSpline": "mutate", // EPIC accessMode=write
  // Schedules generation and returns immediately. Returns an error if the source is already generating. Allow edit
  "PCGEditor.PCGEditorSubsystem.ExecuteGraphInstance": "unknown", // EPIC accessMode=execute
  // Executes a transient instance of the PCG graph asynchronously with the specified parameters. Await the returne
  "PCGEditor.PCGEditorSubsystem.ExecuteTransientGraphAsync": "unknown", // EPIC accessMode=execute
  // Returns registered execution sources as JSON entries with executionSource, gridSize and gridCoordinates. Pass 
  "PCGEditor.PCGEditorSubsystem.GetExecutionSources": "read", // EPIC accessMode=read
  // Returns the description of a PCG graph.
  "PCGEditor.PCGEditorSubsystem.GetGraphDescription": "read", // EPIC accessMode=read
  // Gets the graph instance params of a specific actor, actor MUST have a graph instance
  "PCGEditor.PCGEditorSubsystem.GetGraphInstanceParams": "read", // EPIC accessMode=read
  // Returns the schema for a PCG Graph's graph parameters
  "PCGEditor.PCGEditorSubsystem.GetGraphSchema": "read", // EPIC accessMode=read
  // Returns the graph's name, description, nodes, and connections.
  "PCGEditor.PCGEditorSubsystem.GetGraphStructure": "read", // EPIC accessMode=read
  // Returns the schema for a PCG node type including input/output pins, parameters, and their types.
  "PCGEditor.PCGEditorSubsystem.GetNativeNodeSchema": "read", // EPIC accessMode=read
  // Returns a JSON Data View of a specific node's output data from the supplied execution source. Enables source a
  "PCGEditor.PCGEditorSubsystem.GetNodeDataView": "mutate", // EPIC accessMode=write
  // Returns node details including name, position, and all parameter values.
  "PCGEditor.PCGEditorSubsystem.GetNodeInfo": "read", // EPIC accessMode=read
  // Returns whether automatic regeneration is paused for the graph by the user or scripting. Excludes temporary no
  "PCGEditor.PCGEditorSubsystem.IsGraphAutomaticRegenerationPaused": "read", // EPIC accessMode=read
  // Lists the PCG graphs that can be used with the Subgraph native node. Only these graphs should be used with the
  "PCGEditor.PCGEditorSubsystem.ListAvailableSubgraphs": "read", // EPIC accessMode=read
  // Lists the embedded graphs owned by Graph, including unused ones. An embedded Graph lists its owner's graphs.
  "PCGEditor.PCGEditorSubsystem.ListEmbeddedSubgraphs": "read", // EPIC accessMode=read
  // Gets all actors with a PCG graph instance in the scene.
  "PCGEditor.PCGEditorSubsystem.ListGraphInstances": "read", // EPIC accessMode=read
  // Returns a list of available native PCG node type names.
  "PCGEditor.PCGEditorSubsystem.ListNativeNodes": "read", // EPIC accessMode=read
  // Removes a comment box from the graph. Does not affect the nodes it contains.
  "PCGEditor.PCGEditorSubsystem.RemoveCommentBox": "mutate", // EPIC accessMode=write
  // Removes graph parameters to a specific PCG graph, such that they are not overridable anymore.
  "PCGEditor.PCGEditorSubsystem.RemoveGraphParams": "mutate", // EPIC accessMode=write
  // Removes the node from the graph, will also remove edges connected to the node.
  "PCGEditor.PCGEditorSubsystem.RemoveNode": "mutate", // EPIC accessMode=write
  // Changes an embedded graph's display name. Name must be nonempty and unique among its siblings, ignoring case.
  "PCGEditor.PCGEditorSubsystem.RenameEmbeddedSubgraph": "mutate", // EPIC accessMode=write
  // Change the position of node.
  "PCGEditor.PCGEditorSubsystem.RepositionNode": "mutate", // EPIC accessMode=write
  // Resets the given graph instance params back to the graph's default values. Actor MUST have a graph instance.
  "PCGEditor.PCGEditorSubsystem.ResetGraphInstanceParams": "mutate", // EPIC accessMode=write
  // Exports a data collection to a PCG Data Asset using the attribute inspector's save operation. Game thread only
  "PCGEditor.PCGEditorSubsystem.SaveDataAsAsset": "mutate", // EPIC accessMode=write
  // Controls automatic graph regeneration without requiring an open graph editor. Shares the toolbar's transient p
  "PCGEditor.PCGEditorSubsystem.SetGraphAutomaticRegenerationPaused": "mutate", // EPIC accessMode=write
  // Set the description of a PCGGraph
  "PCGEditor.PCGEditorSubsystem.SetGraphDescription": "mutate", // EPIC accessMode=write
  // Sets the graph instance params of a specific actor, actor MUST have a graph instance
  "PCGEditor.PCGEditorSubsystem.SetGraphInstanceParams": "mutate", // EPIC accessMode=write
  // Adds one or more graph user parameters to a specific PCG graph, such that they will be overridable in per grap
  "PCGEditor.PCGEditorSubsystem.SetGraphParams": "mutate", // EPIC accessMode=write
  // Change the comment on the specified node.
  "PCGEditor.PCGEditorSubsystem.SetNodeComment": "mutate", // EPIC accessMode=write
  // Spawns a PCG Volume with associated Graph Instance into the scene, optionally with Graph Param overrides.
  "PCGEditor.PCGEditorSubsystem.SpawnGraphInstance": "mutate", // EPIC accessMode=write
  // Adds a direct edge and returns true, including for an existing connection. Rejection raises a script error and
  "PCGEditor.PCGEditorSubsystem.TryAddEdge": "mutate", // EPIC accessMode=write
  // Updates an existing comment box with new nodes and value.
  "PCGEditor.PCGEditorSubsystem.UpdateCommentBox": "mutate", // EPIC accessMode=write
  // Updates a node by changing its params and/or title.
  "PCGEditor.PCGEditorSubsystem.UpdateNode": "mutate", // EPIC accessMode=write
  // Returns whether the pins can connect directly without modifying the graph. Invalid nodes or pin labels raise a
  "PCGEditor.PCGEditorSubsystem.ValidateEdge": "read", // EPIC accessMode=read
  // Returns an async result for the source's current generation, without starting or restarting it. Returns an err
  "PCGEditor.PCGEditorSubsystem.WaitForGraphInstance": "read", // EPIC accessMode=read

  /* ── PCGToolset.PCGSpatialToolset ──────────────────────────────── */
  // Runs an instant PCG graph with the specified parameters. Returns an asynchronous result containing execution m
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
  // Schedules generation. Allow editor ticks until the returned result is complete, then inspect Error and Value (
  "PCGToolset.PCGToolset.ExecuteGraphInstance": "mutate",
  // Returns registered execution sources as JSON entries with executionSource, gridSize and gridCoordinates. Pass 
  "PCGToolset.PCGToolset.GetExecutionSources": "read", // EPIC accessMode=read
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
  // Returns a JSON Data View of a specific node's output data from the supplied execution source. Enables source a
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
  // Returns an async result for the source's current generation, without starting or restarting it. Returns an err
  "PCGToolset.PCGToolset.WaitForGraphInstance": "read", // EPIC accessMode=read

  /* ── PerfToolset.CsvProfilerToolset ────────────────────────────── */
  // Surveys a capture without returning per-frame values. StatGroups (the group histogram) is always populated; in
  "PerfToolset.CsvProfilerToolset.GetCsvInfo": "unknown", // EPIC accessMode=execute
  // Returns the capture's events with their frame indices. Repeats of the same event name on one frame collapse in
  "PerfToolset.CsvProfilerToolset.ListEvents": "unknown", // EPIC accessMode=execute
  // Returns up to Count per-frame values of one stat, starting at FrameStart. An over-large Count clamps to the st
  "PerfToolset.CsvProfilerToolset.ReadStatValues": "unknown", // EPIC accessMode=execute
  // Reduces each requested stat to average, min, max, total, and the P50/P90/P95/P99 percentiles over a frame-inde
  "PerfToolset.CsvProfilerToolset.ReduceStat": "unknown", // EPIC accessMode=execute
  // Ranks the capture's stats by a reduced aggregate (average, max, total, or a P50/P90/P95/P99 percentile) over a
  "PerfToolset.CsvProfilerToolset.TopStats": "unknown", // EPIC accessMode=execute

  /* ── PerfToolset.GpuProfilerToolset ────────────────────────────── */
  // Captures the next rendered frame's GPU work as a nested tree (2-3 frame latency). Top-level entries are GPU qu
  "PerfToolset.GpuProfilerToolset.TriggerGPUProfile": "read", // EPIC accessMode=read

  /* ── PerfToolset.StatsToolset ──────────────────────────────────── */
  // Reads the engine's per-frame timing globals once (no smoothing - usually the previous frame).
  "PerfToolset.StatsToolset.GetFrameTimings": "read", // EPIC accessMode=read
  // Captures the latest stat frame as a nested tree. Siblings sorted most-expensive first. Top-level entries are t
  "PerfToolset.StatsToolset.GetFrameTree": "read", // EPIC accessMode=read
  // Samples every non-cycle counter in a stat group for a single frame: integer/float counters and memory pool siz
  "PerfToolset.StatsToolset.GetStatCounters": "read", // EPIC accessMode=read
  // Enumerates registered stat groups. Names are STATGROUP_-prefixed and match the argument accepted by GetStatCou
  "PerfToolset.StatsToolset.ListStatGroups": "read", // EPIC accessMode=read

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
  // Adds a directory the project searches for plugins, saves the .uproject file, and rescans so plugins in the dir
  "PluginToolset.PluginToolset.AddAdditionalPluginDirectory": "mutate", // EPIC accessMode=write
  // Adds an entry to a plugin's DisallowedPlugins array in its .uplugin file, or updates its comment if the entry 
  "PluginToolset.PluginToolset.AddDisallowedPlugin": "mutate", // EPIC accessMode=write
  // Adds a dependency entry to a plugin's Plugins array in its .uplugin file. No-ops if a dependency with that nam
  "PluginToolset.PluginToolset.AddPluginDependency": "mutate",
  // Creates a new plugin from a template and loads it into the editor. Use GetPluginTemplateDescriptions to obtain
  "PluginToolset.PluginToolset.CreatePlugin": "mutate",
  // Lists the extra directories the project searches for plugins (the .uproject's AdditionalPluginDirectories).
  "PluginToolset.PluginToolset.GetAdditionalPluginDirectories": "read", // EPIC accessMode=read
  // Returns the entries from a plugin's DisallowedPlugins array in its .uplugin file.
  "PluginToolset.PluginToolset.GetDisallowedPlugins": "read", // EPIC accessMode=read
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
  // Removes a directory the project searches for plugins, saves the .uproject file, and rescans. Plugins from the 
  "PluginToolset.PluginToolset.RemoveAdditionalPluginDirectory": "mutate", // EPIC accessMode=write
  // Removes an entry from a plugin's DisallowedPlugins array in its .uplugin file.
  "PluginToolset.PluginToolset.RemoveDisallowedPlugin": "mutate", // EPIC accessMode=write
  // Removes a dependency entry from a plugin's Plugins array in its .uplugin file.
  "PluginToolset.PluginToolset.RemovePluginDependency": "mutate",
  // Enables or disables a plugin in the project and saves the .uproject file, checking it out via source control i
  "PluginToolset.PluginToolset.SetPluginEnabled": "mutate",
  // Changes the settings of an existing dependency entry in a plugin's Plugins array in its .uplugin file. Anythin
  "PluginToolset.PluginToolset.UpdatePluginDependency": "mutate", // EPIC accessMode=write
  // Updates a plugin's descriptor fields and writes them to its .uplugin file. Checks out the file via source cont
  "PluginToolset.PluginToolset.UpdatePluginDescriptor": "mutate",
  // Validates that PluginName and RelativePluginLocation are acceptable for a new plugin.
  "PluginToolset.PluginToolset.ValidateNewPluginNameAndLocation": "read",

  /* ── RewindDebuggerToolset.RewindDebuggerToolset ───────────────── */
  // Re-centres the Rewind Debugger visible view range on Time, preserving the current view width. Independent of t
  "RewindDebuggerToolset.RewindDebuggerToolset.CenterView": "mutate", // EPIC accessMode=write
  // Returns all events that fired on a recorded object within a time range. Events represent discrete occurrences 
  "RewindDebuggerToolset.RewindDebuggerToolset.GetObjectEvents": "read", // EPIC accessMode=read
  // Returns the recorded property state of an object at or before a given time. Uses closest-before semantics: ret
  "RewindDebuggerToolset.RewindDebuggerToolset.GetObjectProperties": "read", // EPIC accessMode=read
  // Returns the object currently selected in the Rewind Debugger outliner, if any.
  "RewindDebuggerToolset.RewindDebuggerToolset.GetSelectedObject": "read", // EPIC accessMode=read
  // Returns the current Rewind Debugger session state. Call this first to confirm a session is active before query
  "RewindDebuggerToolset.RewindDebuggerToolset.GetSessionInfo": "read", // EPIC accessMode=read
  // Returns the full decoded JSON for a single trace event from a named source.
  "RewindDebuggerToolset.RewindDebuggerToolset.GetTraceEventDetails": "read", // EPIC accessMode=read
  // Lists all objects recorded in the current session, optionally filtered by class name. Each entry includes an O
  "RewindDebuggerToolset.RewindDebuggerToolset.ListObjects": "read", // EPIC accessMode=read
  // Lists trace events attributed to ObjectId within [StartTime, EndTime], aggregated across every registered IRew
  "RewindDebuggerToolset.RewindDebuggerToolset.ListObjectTraceEvents": "read", // EPIC accessMode=read
  // Selects a recorded object in the Rewind Debugger panel and opens the details panel so its state and timelines 
  "RewindDebuggerToolset.RewindDebuggerToolset.SelectObject": "mutate", // EPIC accessMode=write
  // Moves the Rewind Debugger scrub cursor to a specific recording time. The engine clamps to the recording's rang
  "RewindDebuggerToolset.RewindDebuggerToolset.SetScrubTime": "mutate", // EPIC accessMode=write

  /* ── SceneCaptureToolset.SceneCaptureToolset ───────────────────── */
  // Renders offscreen from the given world and camera using the specified mode. Use ListModes for names available 
  "SceneCaptureToolset.SceneCaptureToolset.Capture": "read", // EPIC accessMode=read
  // Captures the active editor camera offscreen using the requested mode, without changing the viewport. Repeated 
  "SceneCaptureToolset.SceneCaptureToolset.CaptureActiveEditorView": "read", // EPIC accessMode=read
  // Redraws and captures an editor viewport with its view and post-process state: a level viewport or an asset edi
  "SceneCaptureToolset.SceneCaptureToolset.CaptureEditorViewport": "read", // EPIC accessMode=read
  // Captures the current Game or PIE viewport as Slate draws it, with UMG and any other UI over the viewport, such
  "SceneCaptureToolset.SceneCaptureToolset.CaptureGameViewport": "read", // EPIC accessMode=read
  // Describes a mode's purpose, interpretation, limits, registered sub-modes and default. Accepts the same names a
  "SceneCaptureToolset.SceneCaptureToolset.DescribeMode": "read", // EPIC accessMode=read
  // Snapshots a level editor viewport's world, camera and clipping planes: ViewportId's, or the active one. Asset 
  "SceneCaptureToolset.SceneCaptureToolset.GetActiveEditorView": "read", // EPIC accessMode=read
  // Lists the open level and asset editor viewports, with ids to pass as Options.ViewportId or to CaptureEditorVie
  "SceneCaptureToolset.SceneCaptureToolset.ListEditorViewports": "read", // EPIC accessMode=read
  // Lists canonical names of modes that can capture in this build and session. Aliases are not listed separately. 
  "SceneCaptureToolset.SceneCaptureToolset.ListModes": "read", // EPIC accessMode=read
  // Lists this session's viewport recordings, including the active one.
  "SceneCaptureToolset.SceneCaptureToolset.ListViewportRecordings": "read", // EPIC accessMode=read
  // Snapshots actors with active camera components and player camera managers in the given world. Preserves custom
  "SceneCaptureToolset.SceneCaptureToolset.ListViews": "read", // EPIC accessMode=read
  // Lists Editor, PIE and Game worlds in the engine's world contexts, including non-renderable worlds.
  "SceneCaptureToolset.SceneCaptureToolset.ListWorlds": "read", // EPIC accessMode=read
  // Records a viewport for DurationSeconds, then completes with the finalized file. Canceling the call stops the r
  "SceneCaptureToolset.SceneCaptureToolset.RecordViewport": "unknown", // EPIC accessMode=execute
  // Starts recording a viewport to an MP4 as the user sees it and returns at once; StopViewportRecording finishes 
  "SceneCaptureToolset.SceneCaptureToolset.StartViewportRecording": "unknown", // EPIC accessMode=execute
  // Stops a recording and completes once its file is finalized. A recording that already ended returns its result.
  "SceneCaptureToolset.SceneCaptureToolset.StopViewportRecording": "unknown", // EPIC accessMode=execute

  /* ── SemanticSearchToolset.SemanticSearchToolset ───────────────── */
  // Find assets whose embeddings are semantically similar to the given asset's embedding. Vector-only (no BM25). T
  "SemanticSearchToolset.SemanticSearchToolset.FindSimilar": "read",
  // Run a semantic search over the Content Browser assets indexed by the SemanticSearch plugin.
  "SemanticSearchToolset.SemanticSearchToolset.Search": "read",

  /* ── SequencerAnimTools.SequencerAnimToolsEditorLibrary ────────── */
  // 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.AreMotionTrailsVisible": "read", // EPIC accessMode=read
  // Remove a pinned trail's offset. Raises when the trail is not pinned.
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.ClearMotionTrailOffset": "mutate", // EPIC accessMode=write
  // Draw a pinned trail in world space again. Raises when the trail is not pinned.
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.ClearMotionTrailSpace": "mutate", // EPIC accessMode=write
  // Fit the full-weight band of the Time Posing window to the selected keys, keeping the falloff widths, one undoa
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.FrameTimePosingRangeOnSelectedKeys": "mutate", // EPIC accessMode=write
  // 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetMotionTrailSettings": "read", // EPIC accessMode=read
  // 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetPinnedMotionTrails": "read", // EPIC accessMode=read
  // Get the Time Posing window. Raises when Time Posing is off for the sequence.
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetTimePosingRange": "read", // EPIC accessMode=read
  // 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetTimePosingSettings": "read", // EPIC accessMode=read
  // Get the share of a Time Posing offset applied at a frame: 1 in the full-weight band, the falloff curve between
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetTimePosingWeight": "read", // EPIC accessMode=read
  // 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.IsEditPivotToolActive": "read", // EPIC accessMode=read
  // 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.IsTimePosingActive": "read", // EPIC accessMode=read
  // Pin an actor's motion trail without changing the actor or control selection; when the actor's trail is not dra
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinActorMotionTrail": "mutate", // EPIC accessMode=write
  // Pin a Control Rig control's motion trail without selecting the control. Only Transform, TransformNoScale and E
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinControlRigMotionTrail": "mutate", // EPIC accessMode=write
  // Pin every trail currently shown for the selection, as Shift-clicking Motion Paths does. Raises when trails are
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinSelectedMotionTrails": "mutate", // EPIC accessMode=write
  // Pin the motion trail of a socket or bone, as Motion Paths > Pin Socket does. The component or its actor needs 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinSocketMotionTrail": "mutate", // EPIC accessMode=write
  // Turn the Edit Pivot tool on or off in the level editor, as the Animation Mode "Temporary Pivot" button does. I
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetEditPivotToolActive": "mutate", // EPIC accessMode=write
  // Draw a pinned trail moved by an offset, in the space of the object at each frame, without changing any keys. R
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailOffset": "mutate", // EPIC accessMode=write
  // Set the motion trail display settings and save them to the editor preferences (KeyColor lasts the session only
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailSettings": "mutate", // EPIC accessMode=write
  // Draw a pinned trail relative to another actor, or to one of its components, sockets or bones, instead of world
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailSpace": "mutate", // EPIC accessMode=write
  // Show or hide actor and Control Rig motion trails, as the Animation Mode "Motion Paths" button does. Showing tr
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailsVisible": "mutate", // EPIC accessMode=write
  // Turn Time Posing on or off for a sequence. Turning it on places the window by LinkMode: around the current tim
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetTimePosingActive": "mutate", // EPIC accessMode=write
  // Set the Time Posing window, one undoable step; anchors snap to display frames. With LinkMode CurrentFrame the 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetTimePosingRange": "mutate", // EPIC accessMode=write
  // Set the Time Posing preferences and save them to the editor preferences. The struct holds every field, so read
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetTimePosingSettings": "mutate", // EPIC accessMode=write
  // Key an actor's world transform at a frame and spread the change across the Time Posing window by weight, as mo
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.TimePoseActorWorldTransform": "mutate", // EPIC accessMode=write
  // Key a Control Rig control's world transform at a frame and spread the change across the Time Posing window by 
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.TimePoseControlRigWorldTransform": "mutate", // EPIC accessMode=write
  // Key actors transformed about a pivot at each frame, as the Edit Pivot tool does in Pose mode: the new world tr
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.TransformActorsAboutPivot": "mutate", // EPIC accessMode=write
  // Unpin every actor, control and socket trail.
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.UnpinAllMotionTrails": "mutate", // EPIC accessMode=write
  // Unpin a motion trail; it stays drawn while its actor or control is selected. Raises when the trail is not pinn
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.UnpinMotionTrail": "mutate", // EPIC accessMode=write
  // Unpin the pinned trails of the selected actors and controls, as Alt-clicking Motion Paths does.
  "SequencerAnimTools.SequencerAnimToolsEditorLibrary.UnpinSelectedMotionTrails": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneBindingExtensions ────────────── */
  // Add a new track to the specified binding
  "SequencerScripting.MovieSceneBindingExtensions.AddTrack": "mutate", // EPIC accessMode=write
  // Find all tracks within a given binding of the specified type
  "SequencerScripting.MovieSceneBindingExtensions.FindTracksByType": "read", // EPIC accessMode=read
  // Get all the children of this binding. Actor bindings can own child possessable bindings for their components (
  "SequencerScripting.MovieSceneBindingExtensions.GetChildPossessables": "read", // EPIC accessMode=read
  // Get this binding's name
  "SequencerScripting.MovieSceneBindingExtensions.GetDisplayName": "read", // EPIC accessMode=read
  // Get all the tracks stored within this binding
  "SequencerScripting.MovieSceneBindingExtensions.GetTracks": "read", // EPIC accessMode=read
  // Check whether the specified binding is valid
  "SequencerScripting.MovieSceneBindingExtensions.IsValid": "read", // EPIC accessMode=read
  // Remove the specified binding
  "SequencerScripting.MovieSceneBindingExtensions.Remove": "mutate", // EPIC accessMode=write
  // Remove the specified track from this binding
  "SequencerScripting.MovieSceneBindingExtensions.RemoveTrack": "mutate", // EPIC accessMode=write
  // Set this binding's name
  "SequencerScripting.MovieSceneBindingExtensions.SetDisplayName": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneBindingTagExtensions ─────────── */
  // Get every tag name registered on the sequence's MovieScene.
  "SequencerScripting.MovieSceneBindingTagExtensions.GetAllBindingTags": "read", // EPIC accessMode=read
  // Get every tag currently attached to this specific binding.
  "SequencerScripting.MovieSceneBindingTagExtensions.GetBindingTags": "read", // EPIC accessMode=read
  // Remove a tag entirely from the sequence. Clears the tag from every binding that had it, and removes the regist
  "SequencerScripting.MovieSceneBindingTagExtensions.RemoveBindingTag": "mutate", // EPIC accessMode=write
  // Attach a tag to the given binding. If the tag name has not been seen before, it is registered in the MovieScen
  "SequencerScripting.MovieSceneBindingTagExtensions.TagBinding": "mutate", // EPIC accessMode=write
  // Remove a tag from the given binding. Removing its last association also unregisters the tag. No-op if the bind
  "SequencerScripting.MovieSceneBindingTagExtensions.UntagBinding": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneConditionExtensions ──────────── */
  // Remove the condition from this section
  "SequencerScripting.MovieSceneConditionExtensions.ClearSectionCondition": "mutate", // EPIC accessMode=write
  // Remove the track-level condition
  "SequencerScripting.MovieSceneConditionExtensions.ClearTrackCondition": "mutate", // EPIC accessMode=write
  // Remove the condition from a specific track row. A row without metadata is left unchanged.
  "SequencerScripting.MovieSceneConditionExtensions.ClearTrackRowCondition": "mutate", // EPIC accessMode=write
  // Get the condition on this section, or nullptr if none is set
  "SequencerScripting.MovieSceneConditionExtensions.GetSectionCondition": "read", // EPIC accessMode=read
  // Get the track-level condition, or nullptr if none is set
  "SequencerScripting.MovieSceneConditionExtensions.GetTrackCondition": "read", // EPIC accessMode=read
  // Get the condition on a specific track row, or nullptr if none is set or the row has no metadata.
  "SequencerScripting.MovieSceneConditionExtensions.GetTrackRowCondition": "read", // EPIC accessMode=read
  // Set a condition on this section by creating a new instance of the given class. Passing a null class clears the
  "SequencerScripting.MovieSceneConditionExtensions.SetSectionCondition": "mutate", // EPIC accessMode=write
  // Set a track-level condition by creating a new instance of the given class. Passing a null class clears the con
  "SequencerScripting.MovieSceneConditionExtensions.SetTrackCondition": "mutate", // EPIC accessMode=write
  // Set a condition on a specific track row by creating a new instance of the given class. Passing a null class cl
  "SequencerScripting.MovieSceneConditionExtensions.SetTrackRowCondition": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneDoubleVectorTrackExtensions ──── */
  // Get the number of channels used for this track
  "SequencerScripting.MovieSceneDoubleVectorTrackExtensions.GetNumChannelsUsed": "read", // EPIC accessMode=read
  // Set the number of channels used for this track
  "SequencerScripting.MovieSceneDoubleVectorTrackExtensions.SetNumChannelsUsed": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneEventTrackExtensions ─────────── */
  // Create a new event repeater section for the given track
  "SequencerScripting.MovieSceneEventTrackExtensions.AddEventRepeaterSection": "mutate", // EPIC accessMode=write
  // Create a new event trigger section for the given track
  "SequencerScripting.MovieSceneEventTrackExtensions.AddEventTriggerSection": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneFloatVectorTrackExtensions ───── */
  // Get the number of channels used for this track
  "SequencerScripting.MovieSceneFloatVectorTrackExtensions.GetNumChannelsUsed": "read", // EPIC accessMode=read
  // Set the number of channels used for this track
  "SequencerScripting.MovieSceneFloatVectorTrackExtensions.SetNumChannelsUsed": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneFolderExtensions ─────────────── */
  // Add a child folder to the target folder. The child must belong to the target folder's movie scene.
  "SequencerScripting.MovieSceneFolderExtensions.AddChildFolder": "mutate", // EPIC accessMode=write
  // Add a guid for an object binding to this folder
  "SequencerScripting.MovieSceneFolderExtensions.AddChildObjectBinding": "mutate", // EPIC accessMode=write
  // Add a track to this folder. The track must belong to the folder's movie scene.
  "SequencerScripting.MovieSceneFolderExtensions.AddChildTrack": "mutate", // EPIC accessMode=write
  // Get the child folders of a given folder
  "SequencerScripting.MovieSceneFolderExtensions.GetChildFolders": "read", // EPIC accessMode=read
  // Get the object bindings contained by this folder
  "SequencerScripting.MovieSceneFolderExtensions.GetChildObjectBindings": "read", // EPIC accessMode=read
  // Get the tracks contained by this folder
  "SequencerScripting.MovieSceneFolderExtensions.GetChildTracks": "read", // EPIC accessMode=read
  // Get the display color of the given folder
  "SequencerScripting.MovieSceneFolderExtensions.GetFolderColor": "read", // EPIC accessMode=read
  // Get the given folder's display name
  "SequencerScripting.MovieSceneFolderExtensions.GetFolderName": "read", // EPIC accessMode=read
  // Get the sorting order of the given folder. A negative value means the folder has no custom order and compares 
  "SequencerScripting.MovieSceneFolderExtensions.GetSortingOrder": "read", // EPIC accessMode=read
  // Remove a child folder from the given folder
  "SequencerScripting.MovieSceneFolderExtensions.RemoveChildFolder": "mutate", // EPIC accessMode=write
  // Remove an object binding from the given folder
  "SequencerScripting.MovieSceneFolderExtensions.RemoveChildObjectBinding": "mutate", // EPIC accessMode=write
  // Remove a track from the given folder
  "SequencerScripting.MovieSceneFolderExtensions.RemoveChildTrack": "mutate", // EPIC accessMode=write
  // Set the display color of the given folder
  "SequencerScripting.MovieSceneFolderExtensions.SetFolderColor": "mutate", // EPIC accessMode=write
  // Set the name of the given folder
  "SequencerScripting.MovieSceneFolderExtensions.SetFolderName": "mutate", // EPIC accessMode=write
  // Set the sorting order of the given folder. Siblings that each have a custom order sort by it, with higher valu
  "SequencerScripting.MovieSceneFolderExtensions.SetSortingOrder": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieScenePropertyTrackExtensions ──────── */
  // Get the enum whose values an enum track animates, or None when it has not been set. Raises if the enum is hidd
  "SequencerScripting.MovieScenePropertyTrackExtensions.GetEnumTrackEnum": "read", // EPIC accessMode=read
  // Set this byte track's enum
  "SequencerScripting.MovieScenePropertyTrackExtensions.SetByteTrackEnum": "mutate", // EPIC accessMode=write
  // Set the enum whose values an enum track animates, including on its existing sections. Keys are entry values, s
  "SequencerScripting.MovieScenePropertyTrackExtensions.SetEnumTrackEnum": "mutate", // EPIC accessMode=write
  // Set this track's property name and path
  "SequencerScripting.MovieScenePropertyTrackExtensions.SetPropertyNameAndPath": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneSectionEasingExtensions ──────── */
  // Get the effective ease-in duration in tick-resolution frames (not display-rate frames), considering manual ove
  "SequencerScripting.MovieSceneSectionEasingExtensions.GetEaseInDuration": "read", // EPIC accessMode=read
  // Get the effective ease-out duration in tick-resolution frames (not display-rate frames), considering manual ov
  "SequencerScripting.MovieSceneSectionEasingExtensions.GetEaseOutDuration": "read", // EPIC accessMode=read
  // Set manual ease-in duration in tick-resolution frames (not display-rate frames), enabling the manual override
  "SequencerScripting.MovieSceneSectionEasingExtensions.SetEaseInDuration": "mutate", // EPIC accessMode=write
  // Set manual ease-out duration in tick-resolution frames (not display-rate frames), enabling the manual override
  "SequencerScripting.MovieSceneSectionEasingExtensions.SetEaseOutDuration": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneSectionExtensions ────────────── */
  // Add a bool key to the unique matching bool channel, including an unnamed one. Replaces a key already at Frame.
  "SequencerScripting.MovieSceneSectionExtensions.AddBoolKey": "mutate", // EPIC accessMode=write
  // Override the named channel with a new override of OverrideClass, replacing any existing override. The channel'
  "SequencerScripting.MovieSceneSectionExtensions.AddChannelOverride": "mutate", // EPIC accessMode=write
  // Add a double key and return its identity. InTime is in display-rate frames unless TimeUnit is TickResolution. 
  "SequencerScripting.MovieSceneSectionExtensions.AddDoubleKey": "mutate", // EPIC accessMode=write
  // Add a float key and return its identity. InTime is in display-rate frames unless TimeUnit is TickResolution. S
  "SequencerScripting.MovieSceneSectionExtensions.AddFloatKey": "mutate", // EPIC accessMode=write
  // Add an integer key to the unique matching integer channel, including an unnamed one. Replaces a key already at
  "SequencerScripting.MovieSceneSectionExtensions.AddIntegerKey": "mutate", // EPIC accessMode=write
  // Add a string key to the unique matching string channel, including an unnamed one. Replaces a key already at Fr
  "SequencerScripting.MovieSceneSectionExtensions.AddStringKey": "mutate", // EPIC accessMode=write
  // Remove the section's match offsets and reset its match bone.
  "SequencerScripting.MovieSceneSectionExtensions.ClearSkeletalAnimationMatch": "mutate", // EPIC accessMode=write
  // Sample once per owning-sequence display frame in [StartFrame, EndFrame). Supports float, double, bool (0/1), i
  "SequencerScripting.MovieSceneSectionExtensions.EvaluateChannelKeys": "read", // EPIC accessMode=read
  // Return the section's optional blend type, including whether it is set.
  "SequencerScripting.MovieSceneSectionExtensions.GetBlendType": "read", // EPIC accessMode=read
  // Snapshot keys as JSON with display frame, subframe, value, index and numeric interpolation. Binding IDs use ex
  "SequencerScripting.MovieSceneSectionExtensions.GetChannelKeys": "read", // EPIC accessMode=read
  // Snapshot requested key indices in order. Missing indices have null frame and value.
  "SequencerScripting.MovieSceneSectionExtensions.GetChannelKeysByIndex": "read", // EPIC accessMode=read
  // Get all scripting channel names, including NAME_None for unnamed channels.
  "SequencerScripting.MovieSceneSectionExtensions.GetChannelNames": "read", // EPIC accessMode=read
  // Get the class of the named channel's override, or None when the channel is not overridden.
  "SequencerScripting.MovieSceneSectionExtensions.GetChannelOverrideClass": "read", // EPIC accessMode=read
  // List the override classes that can replace the named channel, such as the float or double Perlin noise class. 
  "SequencerScripting.MovieSceneSectionExtensions.GetChannelOverrideClasses": "read", // EPIC accessMode=read
  // Return how the section's animated state is handled after evaluation.
  "SequencerScripting.MovieSceneSectionExtensions.GetCompletionMode": "read", // EPIC accessMode=read
  // Get the double default; use HasDefault to distinguish an absent default.
  "SequencerScripting.MovieSceneSectionExtensions.GetDoubleDefault": "read", // EPIC accessMode=read
  // Read a live double key at double precision.
  "SequencerScripting.MovieSceneSectionExtensions.GetDoubleKeyValue": "read", // EPIC accessMode=read
  // Get the exclusive end in display-rate frames, rounded down. Raises a script error and returns -1 when unbounde
  "SequencerScripting.MovieSceneSectionExtensions.GetEndFrame": "read", // EPIC accessMode=read
  // Get the float default; use HasDefault to distinguish an absent default.
  "SequencerScripting.MovieSceneSectionExtensions.GetFloatDefault": "read", // EPIC accessMode=read
  // Read a live float key's value.
  "SequencerScripting.MovieSceneSectionExtensions.GetFloatKeyValue": "read", // EPIC accessMode=read
  // Return all keys, including keys outside the section range.
  "SequencerScripting.MovieSceneSectionExtensions.GetKeys": "read", // EPIC accessMode=read
  // Return a live key's time in display-rate frames by default, or in tick-resolution frames.
  "SequencerScripting.MovieSceneSectionExtensions.GetKeyTime": "read", // EPIC accessMode=read
  // Get the section's float channels followed by its double channels. Raises and returns an empty array when any c
  "SequencerScripting.MovieSceneSectionExtensions.GetNumericChannels": "read", // EPIC accessMode=read
  // Get the transform channels a shot or subsequence section's origin override animates. Scale and weight are alwa
  "SequencerScripting.MovieSceneSectionExtensions.GetOriginOverrideMask": "read", // EPIC accessMode=read
  // List the names of the section's channels that are replaced by a procedural override, such as Perlin noise. Ove
  "SequencerScripting.MovieSceneSectionExtensions.GetOverriddenChannelNames": "read", // EPIC accessMode=read
  // Get the noise parameters of a channel overridden with Perlin noise. Frequency is in peaks per second and Offse
  "SequencerScripting.MovieSceneSectionExtensions.GetPerlinNoiseParams": "read", // EPIC accessMode=read
  // Return the section's post-roll in tick-resolution frames.
  "SequencerScripting.MovieSceneSectionExtensions.GetPostRollFrames": "read", // EPIC accessMode=read
  // Return the section's pre-roll in tick-resolution frames.
  "SequencerScripting.MovieSceneSectionExtensions.GetPreRollFrames": "read", // EPIC accessMode=read
  // Get the zero-based track row.
  "SequencerScripting.MovieSceneSectionExtensions.GetSectionRowIndex": "read", // EPIC accessMode=read
  // Whether the channel's curve is drawn in the Sequencer track area, the Show Curve entry under Display in the se
  "SequencerScripting.MovieSceneSectionExtensions.GetShowCurve": "read", // EPIC accessMode=read
  // Get the settings the section was last matched with. BoneName is None when the section is not matched.
  "SequencerScripting.MovieSceneSectionExtensions.GetSkeletalAnimationMatchSettings": "read", // EPIC accessMode=read
  // Get the inclusive start in display-rate frames, rounded down. Raises a script error and returns -1 when unboun
  "SequencerScripting.MovieSceneSectionExtensions.GetStartFrame": "read", // EPIC accessMode=read
  // Get the time of a camera cut or cinematic shot section's thumbnail, in seconds from the section start. Editor 
  "SequencerScripting.MovieSceneSectionExtensions.GetThumbnailReferenceOffset": "read", // EPIC accessMode=read
  // Get the channels a 3D transform section animates.
  "SequencerScripting.MovieSceneSectionExtensions.GetTransformMask": "read", // EPIC accessMode=read
  // Whether this channel has a default value.
  "SequencerScripting.MovieSceneSectionExtensions.HasDefault": "read", // EPIC accessMode=read
  // Has end frame
  "SequencerScripting.MovieSceneSectionExtensions.HasEndFrame": "read", // EPIC accessMode=read
  // Has start frame
  "SequencerScripting.MovieSceneSectionExtensions.HasStartFrame": "read", // EPIC accessMode=read
  // Get whether this section is skipped during evaluation because it is inactive, or because its track row or trac
  "SequencerScripting.MovieSceneSectionExtensions.IsDeactivated": "read", // EPIC accessMode=read
  // Offset the section's root motion so Settings.BoneName lines up with the previous or next section on the track 
  "SequencerScripting.MovieSceneSectionExtensions.MatchSkeletalAnimationSection": "mutate", // EPIC accessMode=write
  // Remove the first key in a display-rate frame, including its subframes. Returns false if no key matches.
  "SequencerScripting.MovieSceneSectionExtensions.RemoveChannelKeyAtFrame": "mutate", // EPIC accessMode=write
  // Remove the named channel's override so its keys animate it again. Raises when the channel is not overridden.
  "SequencerScripting.MovieSceneSectionExtensions.RemoveChannelOverride": "mutate", // EPIC accessMode=write
  // Clear the channel's default value.
  "SequencerScripting.MovieSceneSectionExtensions.RemoveDefault": "mutate", // EPIC accessMode=write
  // Remove a key only if its complete channel identity and live key handle match.
  "SequencerScripting.MovieSceneSectionExtensions.RemoveKey": "mutate", // EPIC accessMode=write
  // Set a blend type supported by the section's track.
  "SequencerScripting.MovieSceneSectionExtensions.SetBlendType": "mutate", // EPIC accessMode=write
  // Assign a camera binding. Supply RootSequence for a binding in another sequence; instances must be unique in th
  "SequencerScripting.MovieSceneSectionExtensions.SetCameraCutBinding": "mutate", // EPIC accessMode=write
  // Set how the section's animated state is handled after evaluation.
  "SequencerScripting.MovieSceneSectionExtensions.SetCompletionMode": "mutate", // EPIC accessMode=write
  // Set the double channel default value.
  "SequencerScripting.MovieSceneSectionExtensions.SetDoubleDefault": "mutate", // EPIC accessMode=write
  // Set a live double key's value at double precision.
  "SequencerScripting.MovieSceneSectionExtensions.SetDoubleKeyValue": "mutate", // EPIC accessMode=write
  // Set end frame bounded
  "SequencerScripting.MovieSceneSectionExtensions.SetEndFrameBounded": "mutate", // EPIC accessMode=write
  // Set the float channel default value.
  "SequencerScripting.MovieSceneSectionExtensions.SetFloatDefault": "mutate", // EPIC accessMode=write
  // Set a live float key's value.
  "SequencerScripting.MovieSceneSectionExtensions.SetFloatKeyValue": "mutate", // EPIC accessMode=write
  // Move a live key to NewFrameNumber. SubFrame is clamped to [0, 1) and ignored for tick-resolution frames.
  "SequencerScripting.MovieSceneSectionExtensions.SetKeyTime": "mutate", // EPIC accessMode=write
  // Set the transform channels a shot or subsequence section's origin override animates. Only translation and rota
  "SequencerScripting.MovieSceneSectionExtensions.SetOriginOverrideMask": "mutate", // EPIC accessMode=write
  // Set the noise parameters of a channel overridden with Perlin noise. All values must be finite and Frequency no
  "SequencerScripting.MovieSceneSectionExtensions.SetPerlinNoiseParams": "mutate", // EPIC accessMode=write
  // Set non-negative post-roll in tick-resolution frames.
  "SequencerScripting.MovieSceneSectionExtensions.SetPostRollFrames": "mutate", // EPIC accessMode=write
  // Set non-negative pre-roll in tick-resolution frames.
  "SequencerScripting.MovieSceneSectionExtensions.SetPreRollFrames": "mutate", // EPIC accessMode=write
  // Set range
  "SequencerScripting.MovieSceneSectionExtensions.SetRange": "mutate", // EPIC accessMode=write
  // Set the zero-based track row. A negative row is invalid, and so is any row but 0 on a track that does not supp
  "SequencerScripting.MovieSceneSectionExtensions.SetSectionRowIndex": "mutate", // EPIC accessMode=write
  // Set whether the channel's curve is drawn in the Sequencer track area. The Curve Editor's own curve visibility 
  "SequencerScripting.MovieSceneSectionExtensions.SetShowCurve": "mutate", // EPIC accessMode=write
  // Assign an animation asset by content path to a skeletal animation section.
  "SequencerScripting.MovieSceneSectionExtensions.SetSkeletalAnimation": "mutate", // EPIC accessMode=write
  // Set start frame bounded
  "SequencerScripting.MovieSceneSectionExtensions.SetStartFrameBounded": "mutate", // EPIC accessMode=write
  // Set the time of a camera cut or cinematic shot section's thumbnail, in seconds from the section start. Editor 
  "SequencerScripting.MovieSceneSectionExtensions.SetThumbnailReferenceOffset": "mutate", // EPIC accessMode=write
  // Set the channels a 3D transform section animates. Disabled channels keep their keys.
  "SequencerScripting.MovieSceneSectionExtensions.SetTransformMask": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneSequenceExtensions ───────────── */
  // Add a binding that resolves to nothing, to be assigned an object later, as Sequencer's New Empty Binding does.
  "SequencerScripting.MovieSceneSequenceExtensions.AddEmptyBinding": "mutate", // EPIC accessMode=write
  // Adds a mark or mark range at the given start and end frames with default settings (StartFrame == EndFrame is a
  "SequencerScripting.MovieSceneSequenceExtensions.AddMarkedFrameRangeToSequence": "mutate", // EPIC accessMode=write
  // Add a root folder to the given sequence
  "SequencerScripting.MovieSceneSequenceExtensions.AddRootFolderToSequence": "mutate", // EPIC accessMode=write
  // Add a sequence-level track of the specified concrete type. Camera-cut tracks occupy the sequence's dedicated c
  "SequencerScripting.MovieSceneSequenceExtensions.AddTrack": "mutate", // EPIC accessMode=write
  // Whether the sequence's marks and mark ranges are also drawn in Sequencer while another sequence in the hierarc
  "SequencerScripting.MovieSceneSequenceExtensions.AreMarkedFramesShownGlobally": "read", // EPIC accessMode=read
  // Hide the marks and mark ranges of this sequence and of every sub-sequence beneath it, as Sequencer's Clear Glo
  "SequencerScripting.MovieSceneSequenceExtensions.ClearMarkedFramesShownGlobally": "mutate", // EPIC accessMode=write
  // Delete the user mark or mark range by index @DeleteIndex The index to the user mark or mark range to delete
  "SequencerScripting.MovieSceneSequenceExtensions.DeleteMarkedFrame": "mutate", // EPIC accessMode=write
  // Delete all user marks and mark ranges
  "SequencerScripting.MovieSceneSequenceExtensions.DeleteMarkedFrames": "mutate", // EPIC accessMode=write
  // Find a binding by its exact internal possessable or spawnable name, which can differ from the binding's displa
  "SequencerScripting.MovieSceneSequenceExtensions.FindBindingByName": "read", // EPIC accessMode=read
  // Find the first binding ID stored under a tag, or an invalid ID when absent.
  "SequencerScripting.MovieSceneSequenceExtensions.FindBindingByTag": "read", // EPIC accessMode=read
  // Copy all binding IDs stored under a tag.
  "SequencerScripting.MovieSceneSequenceExtensions.FindBindingsByTag": "read", // EPIC accessMode=read
  // Find the user mark or mark range by label @InLabel The label to the user mark or mark range to find
  "SequencerScripting.MovieSceneSequenceExtensions.FindMarkedFrameByLabel": "read", // EPIC accessMode=read
  // Get the binding ID for a binding within a sequence.
  "SequencerScripting.MovieSceneSequenceExtensions.GetBindingID": "read", // EPIC accessMode=read
  // Get all the bindings in this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.GetBindings": "read", // EPIC accessMode=read
  // Get the clock source for this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.GetClockSource": "read", // EPIC accessMode=read
  // Get the actor or asset driving the sequence's custom clock, or None when the sequence does not use one or it i
  "SequencerScripting.MovieSceneSequenceExtensions.GetCustomClockSource": "read", // EPIC accessMode=read
  // Gets this sequence's display rate
  "SequencerScripting.MovieSceneSequenceExtensions.GetDisplayRate": "read", // EPIC accessMode=read
  // Get the evaluation type for this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.GetEvaluationType": "read", // EPIC accessMode=read
  // Get the marks and mark ranges for this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.GetMarkedFramesFromSequence": "read", // EPIC accessMode=read
  // Get playback end of this sequence in display rate resolution
  "SequencerScripting.MovieSceneSequenceExtensions.GetPlaybackEnd": "read", // EPIC accessMode=read
  // Get playback start of this sequence in display rate resolution
  "SequencerScripting.MovieSceneSequenceExtensions.GetPlaybackStart": "read", // EPIC accessMode=read
  // Get a portable binding ID for a binding that resides in a different sequence to the one where this binding wil
  "SequencerScripting.MovieSceneSequenceExtensions.GetPortableBindingID": "read", // EPIC accessMode=read
  // Get the root folders in the provided sequence
  "SequencerScripting.MovieSceneSequenceExtensions.GetRootFoldersInSequence": "read", // EPIC accessMode=read
  // Return every section grouped with Section, including Section itself, or an empty array when it is not grouped.
  "SequencerScripting.MovieSceneSequenceExtensions.GetSectionGroup": "read", // EPIC accessMode=read
  // List one section from each section group, the first that a track of the sequence holds. Pass it to GetSectionG
  "SequencerScripting.MovieSceneSequenceExtensions.GetSectionGroups": "read", // EPIC accessMode=read
  // List the evaluation flags set on the sequence, one entry per flag.
  "SequencerScripting.MovieSceneSequenceExtensions.GetSequenceFlags": "read", // EPIC accessMode=read
  // Gets this sequence's tick resolution
  "SequencerScripting.MovieSceneSequenceExtensions.GetTickResolution": "read", // EPIC accessMode=read
  // Get the sequence's root tracks, including its camera-cut track. Tracks attached to object bindings are not inc
  "SequencerScripting.MovieSceneSequenceExtensions.GetTracks": "read", // EPIC accessMode=read
  // Get the sequence view range end in seconds
  "SequencerScripting.MovieSceneSequenceExtensions.GetViewRangeEnd": "read", // EPIC accessMode=read
  // Get the sequence view range start in seconds
  "SequencerScripting.MovieSceneSequenceExtensions.GetViewRangeStart": "read", // EPIC accessMode=read
  // Get the sequence work range end in seconds
  "SequencerScripting.MovieSceneSequenceExtensions.GetWorkRangeEnd": "read", // EPIC accessMode=read
  // Get the sequence work range start in seconds
  "SequencerScripting.MovieSceneSequenceExtensions.GetWorkRangeStart": "read", // EPIC accessMode=read
  // Group sections so that moving one moves them all, merging any groups they already belong to. Requires at least
  "SequencerScripting.MovieSceneSequenceExtensions.GroupSections": "mutate", // EPIC accessMode=write
  // Is playback ranged locked
  "SequencerScripting.MovieSceneSequenceExtensions.IsPlaybackRangeLocked": "read", // EPIC accessMode=read
  // Remove a root folder from the given sequence. Will throw an exception if the specified folder is not valid or 
  "SequencerScripting.MovieSceneSequenceExtensions.RemoveRootFolderFromSequence": "mutate", // EPIC accessMode=write
  // Remove a root or binding track owned by this sequence. Returns false for a track this sequence does not contai
  "SequencerScripting.MovieSceneSequenceExtensions.RemoveTrack": "mutate", // EPIC accessMode=write
  // Set the clock source for this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.SetClockSource": "mutate", // EPIC accessMode=write
  // Drive the sequence's playback from ClockSource, as the Custom clock source picker does: an asset or actor that
  "SequencerScripting.MovieSceneSequenceExtensions.SetCustomClockSource": "mutate", // EPIC accessMode=write
  // Sets this sequence's display rate
  "SequencerScripting.MovieSceneSequenceExtensions.SetDisplayRate": "mutate", // EPIC accessMode=write
  // Set the evaluation type for this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.SetEvaluationType": "mutate", // EPIC accessMode=write
  // Show or hide the sequence's marks and mark ranges in Sequencer while another sequence in the hierarchy is focu
  "SequencerScripting.MovieSceneSequenceExtensions.SetMarkedFramesShownGlobally": "mutate", // EPIC accessMode=write
  // Set playback end of this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackEnd": "mutate", // EPIC accessMode=write
  // Set both playback bounds atomically in display-rate frames. The start is inclusive and the end is exclusive; a
  "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackRange": "mutate", // EPIC accessMode=write
  // Set playback range locked @bInLocked Whether the movie scene playback range should be locked
  "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackRangeLocked": "mutate", // EPIC accessMode=write
  // Set playback start of this sequence
  "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackStart": "mutate", // EPIC accessMode=write
  // Set or clear one evaluation flag. Set Volatile on any sequence that will be changed procedurally at runtime.
  "SequencerScripting.MovieSceneSequenceExtensions.SetSequenceFlag": "mutate", // EPIC accessMode=write
  // Sets this sequence's tick resolution and migrates frame times
  "SequencerScripting.MovieSceneSequenceExtensions.SetTickResolution": "mutate", // EPIC accessMode=write
  // Set both view range bounds atomically in seconds. Requires finite bounds with start before end.
  "SequencerScripting.MovieSceneSequenceExtensions.SetViewRange": "mutate", // EPIC accessMode=write
  // Set both work range bounds atomically in seconds. Requires finite bounds with start before end.
  "SequencerScripting.MovieSceneSequenceExtensions.SetWorkRange": "mutate", // EPIC accessMode=write
  // Remove a section from its group. Raises when the section is not grouped. A group left with one section is diss
  "SequencerScripting.MovieSceneSequenceExtensions.UngroupSection": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneTimeWarpExtensions ───────────── */
  // Get the class of a section's custom time warp, or None when the section does not use one.
  "SequencerScripting.MovieSceneTimeWarpExtensions.GetCustomTimeWarpClass": "read", // EPIC accessMode=read
  // Get a section's fixed play rate. Returns 0 for a section held on a fixed time. Raises when the section uses an
  "SequencerScripting.MovieSceneTimeWarpExtensions.GetSectionPlayRate": "read", // EPIC accessMode=read
  // List the custom time warp classes that SetCustomTimeWarp accepts, such as the play rate curve and the time war
  "SequencerScripting.MovieSceneTimeWarpExtensions.GetTimeWarpClasses": "read", // EPIC accessMode=read
  // Get the kind of time warp a section uses. Raises for a section without a time warp.
  "SequencerScripting.MovieSceneTimeWarpExtensions.GetTimeWarpType": "read", // EPIC accessMode=read
  // Replace a section's time warp with a new custom time warp of TimeWarpClass, initialized the way the editor doe
  "SequencerScripting.MovieSceneTimeWarpExtensions.SetCustomTimeWarp": "mutate", // EPIC accessMode=write
  // Give a section a fixed play rate, replacing any custom time warp. A rate of 0 holds the section on its first f
  "SequencerScripting.MovieSceneTimeWarpExtensions.SetSectionPlayRate": "mutate", // EPIC accessMode=write

  /* ── SequencerScripting.MovieSceneTrackExtensions ──────────────── */
  // Add a new section to this track
  "SequencerScripting.MovieSceneTrackExtensions.AddSection": "mutate", // EPIC accessMode=write
  // Add a new section of SectionClass to this track and return it, as the track's add-section menu does for tracks
  "SequencerScripting.MovieSceneTrackExtensions.AddSectionOfClass": "mutate", // EPIC accessMode=write
  // Get the system that blends the track's overlapping sections. Raises for a track without a choice of blender.
  "SequencerScripting.MovieSceneTrackExtensions.GetBlenderSystem": "read", // EPIC accessMode=read
  // Whether camera cut sections auto-arrange to fill gaps. Stays true on a track with more than one row, where aut
  "SequencerScripting.MovieSceneTrackExtensions.GetCameraCutAutoArrangeSections": "read", // EPIC accessMode=read
  // Whether overlapping camera cut sections blend between their cameras.
  "SequencerScripting.MovieSceneTrackExtensions.GetCameraCutCanBlend": "read", // EPIC accessMode=read
  // Get this track's display name
  "SequencerScripting.MovieSceneTrackExtensions.GetDisplayName": "read", // EPIC accessMode=read
  // Access all this track's sections
  "SequencerScripting.MovieSceneTrackExtensions.GetSections": "read", // EPIC accessMode=read
  // Get the section to key for this track
  "SequencerScripting.MovieSceneTrackExtensions.GetSectionToKey": "read", // EPIC accessMode=read
  // List the blender systems the track can use. Raises for a track without a choice of blender.
  "SequencerScripting.MovieSceneTrackExtensions.GetSupportedBlenderSystems": "read", // EPIC accessMode=read
  // Get whether this track has been deactivated, which is the state saved with the asset. Local mute and solo are 
  "SequencerScripting.MovieSceneTrackExtensions.IsDeactivated": "read", // EPIC accessMode=read
  // Get whether a row of this track has been deactivated, which is the state saved with the asset. Local mute and 
  "SequencerScripting.MovieSceneTrackExtensions.IsRowDeactivated": "read", // EPIC accessMode=read
  // Remove the specified section
  "SequencerScripting.MovieSceneTrackExtensions.RemoveSection": "mutate", // EPIC accessMode=write
  // Set the system that blends the track's overlapping sections. Must be one of GetSupportedBlenderSystems.
  "SequencerScripting.MovieSceneTrackExtensions.SetBlenderSystem": "mutate", // EPIC accessMode=write
  // Set whether camera cut sections auto-arrange. Enabling it on a single-row track closes gaps between sections, 
  "SequencerScripting.MovieSceneTrackExtensions.SetCameraCutAutoArrangeSections": "mutate", // EPIC accessMode=write
  // Set whether overlapping camera cut sections blend. Disabling it removes section overlaps and easing, as the ed
  "SequencerScripting.MovieSceneTrackExtensions.SetCameraCutCanBlend": "mutate", // EPIC accessMode=write
  // Set this track's display name
  "SequencerScripting.MovieSceneTrackExtensions.SetDisplayName": "mutate", // EPIC accessMode=write

  /* ── SequencerScriptingEditor.SequencerToolsFunctionLibrary ────── */
  // Export Passed in Binding as an Anim Sequence. Note use this when you want to export without an open Level Sequ
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportAnimSequence": "mutate", // EPIC accessMode=write
  // Export a level sequence's cinematic shot track to an Edit Decision List (EDL) file. Audio tracks are not expor
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportLevelSequenceEDL": "mutate", // EPIC accessMode=write
  // Export bindings and tracks to an FBX file.
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportLevelSequenceFBX": "mutate", // EPIC accessMode=write
  // Export a level sequence's cinematic shot and audio tracks to a Final Cut Pro 7 XML file.
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportLevelSequenceFCPXML": "mutate", // EPIC accessMode=write
  // Get the links to the anim sequences if they exist on this level sequence
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.GetAnimSequenceLinkFromLevelSequence": "read", // EPIC accessMode=read
  // Get the link to the level sequence if it exists on this anim sequence
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.GetLevelSequenceLinkFromAnimSequence": "read", // EPIC accessMode=read
  // Import an Edit Decision List (EDL) file onto a level sequence's cinematic shot track, as one undoable transact
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ImportLevelSequenceEDL": "mutate", // EPIC accessMode=write
  // Import animation and cameras from an FBX file onto bindings of a level sequence, as one undoable transaction. 
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ImportLevelSequenceFBX": "mutate", // EPIC accessMode=write
  // Import a Final Cut Pro 7 XML file onto a level sequence's cinematic shot and audio tracks, as one undoable tra
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ImportLevelSequenceFCPXML": "mutate", // EPIC accessMode=write
  // Links a LevelSequence's SkeletalMesh binding to an existing anim sequence. Both assets must be writable, and t
  "SequencerScriptingEditor.SequencerToolsFunctionLibrary.LinkAnimSequence": "mutate", // EPIC accessMode=write

  /* ── SequenceValidator.SequenceValidatorSubsystem ──────────────── */
  // List the validation rules registered with the Sequence Validator.
  "SequenceValidator.SequenceValidatorSubsystem.GetValidationRules": "read", // EPIC accessMode=read
  // Run validation rules on Sequence and its subsequences, as the Sequence Validator window does. The sequence is 
  "SequenceValidator.SequenceValidatorSubsystem.ValidateSequence": "read", // EPIC accessMode=read

  /* ── SlateDebugger.SlateDebuggerSubsystem ──────────────────────── */
  // Clears all recorded events.
  "SlateDebugger.SlateDebuggerSubsystem.ClearFocusLog": "mutate", // EPIC accessMode=write
  // What has focus RIGHT NOW for a user, queried live from Slate, with its full widget path.
  "SlateDebugger.SlateDebuggerSubsystem.GetCurrentFocus": "read", // EPIC accessMode=read
  // The full causal chain for ONE event, located precisely by its sequence number (from any GetFocusEvents/GetFocu
  "SlateDebugger.SlateDebuggerSubsystem.GetFocusEventDetail": "read", // EPIC accessMode=read
  // Recent events (newest first), filtered.
  "SlateDebugger.SlateDebuggerSubsystem.GetFocusEvents": "read", // EPIC accessMode=read
  // Recording status: on/off, record count, dropped count, time span.
  "SlateDebugger.SlateDebuggerSubsystem.GetFocusLogStatus": "read", // EPIC accessMode=read
  // Lightweight chronological digest of recorded events (oldest->newest). No paths/callstack.
  "SlateDebugger.SlateDebuggerSubsystem.GetFocusTimeline": "read", // EPIC accessMode=read
  // Reconstructs the causal chain (input -> navigation -> focus + context + callstack) that gave a widget focus.
  "SlateDebugger.SlateDebuggerSubsystem.ReconstructFocusPath": "read", // EPIC accessMode=read
  // Starts recording Slate focus and navigation events into an in-memory buffer, discarding events already capture
  "SlateDebugger.SlateDebuggerSubsystem.StartFocusRecording": "mutate", // EPIC accessMode=write
  // Stops recording. The buffer is retained for querying.
  "SlateDebugger.SlateDebuggerSubsystem.StopFocusRecording": "mutate", // EPIC accessMode=write

  /* ── SlateInspectorToolset.SlateInspectorToolset ───────────────── */
  // Send an analog axis value along the target user's focus path; 1D axes only, send 0 afterwards to release.
  "SlateInspectorToolset.SlateInspectorToolset.AnalogInput": "unknown", // EPIC accessMode=execute
  // Click a Slate widget identified by its ref. Returns false without clicking when the target point would not rec
  "SlateInspectorToolset.SlateInspectorToolset.Click": "mutate",
  // Drag from one Slate widget to another (mouse down, stepped move, release). Both ends are hit-tested first; ret
  "SlateInspectorToolset.SlateInspectorToolset.Drag": "mutate",
  // Fill multiple Slate form fields at once.
  "SlateInspectorToolset.SlateInspectorToolset.FillForm": "mutate",
  // Set Slate keyboard/navigation focus to a widget, e.g. before gamepad navigation in PIE.
  "SlateInspectorToolset.SlateInspectorToolset.Focus": "mutate", // EPIC accessMode=write
  // Resolve a Slate widget ref back to the UMG UWidget that created it (for UMG and PIE UIs). Returns the UWidget 
  "SlateInspectorToolset.SlateInspectorToolset.GetUMGInfo": "read", // EPIC accessMode=read
  // Report whether a widget is reachable by pointer input, without changing anything. Returns JSON {reached, sampl
  "SlateInspectorToolset.SlateInspectorToolset.HitTest": "read", // EPIC accessMode=read
  // Hover over a Slate widget, triggering any hover state or tooltip. Returns false without hovering when the targ
  "SlateInspectorToolset.SlateInspectorToolset.Hover": "mutate",
  // Press a key and leave it held; pair with KeyUp. Same key specs and gamepad routing as PressKey. A held modifie
  "SlateInspectorToolset.SlateInspectorToolset.KeyDown": "unknown", // EPIC accessMode=execute
  // Release a key held by an earlier KeyDown for the same Slate user. Same key specs and gamepad routing as PressK
  "SlateInspectorToolset.SlateInspectorToolset.KeyUp": "unknown", // EPIC accessMode=execute
  // Why the last action tool refused, as JSON {seq, tool, target, reason}; seq is 0 when nothing has refused this 
  "SlateInspectorToolset.SlateInspectorToolset.LastRefusal": "read", // EPIC accessMode=read
  // List all active observers as a JSON array for debugging. Each entry includes the observer identifier, whether 
  "SlateInspectorToolset.SlateInspectorToolset.ListObservers": "read",
  // Press a mouse button at a widget's center and leave it held; idempotent for a button this widget already holds
  "SlateInspectorToolset.SlateInspectorToolset.MouseDown": "unknown", // EPIC accessMode=execute
  // Move the pointer to an absolute screen position; a button held by MouseDown stays held, so MouseDown/MouseMove
  "SlateInspectorToolset.SlateInspectorToolset.MouseMove": "mutate", // EPIC accessMode=write
  // Move the pointer to a widget's center plus an offset; a held button stays held. Use MouseMove for an absolute 
  "SlateInspectorToolset.SlateInspectorToolset.MouseMoveTo": "mutate", // EPIC accessMode=write
  // Release a mouse button at the pointer, to its captor or whatever lies under it; safe without a prior MouseDown
  "SlateInspectorToolset.SlateInspectorToolset.MouseUp": "unknown", // EPIC accessMode=execute
  // Register an observer on a widget subtree so its refs are continuously kept up to date (~100ms tick). Call this
  "SlateInspectorToolset.SlateInspectorToolset.Observe": "mutate",
  // Press and release a key on the focused Slate widget; gamepad keys route as controller input for the target use
  "SlateInspectorToolset.SlateInspectorToolset.PressKey": "mutate",
  // Release every button MouseDown still holds, lift every finger TouchStart still holds and release every key Key
  "SlateInspectorToolset.SlateInspectorToolset.ReleaseHeldInput": "unknown", // EPIC accessMode=execute
  // Screenshot a Slate widget or an editor window. Prefer this over SceneTools.take_screenshot for Editor UI; use 
  "SlateInspectorToolset.SlateInspectorToolset.Screenshot": "mutate",
  // Scroll the mouse wheel over a Slate widget. Hit-tested like Click; returns false without scrolling if the poin
  "SlateInspectorToolset.SlateInspectorToolset.Scroll": "unknown", // EPIC accessMode=execute
  // Select an option in a Slate combobox by its text label. Opens the dropdown, finds the matching text, and click
  "SlateInspectorToolset.SlateInspectorToolset.SelectOption": "mutate",
  // Capture a Slate UI accessibility snapshot. Use this to read the current widget tree and discover refs for acti
  "SlateInspectorToolset.SlateInspectorToolset.Snapshot": "read",
  // Lift a finger held by an earlier TouchStart at an absolute screen position.
  "SlateInspectorToolset.SlateInspectorToolset.TouchEnd": "unknown", // EPIC accessMode=execute
  // Move a finger held by an earlier TouchStart to an absolute screen position.
  "SlateInspectorToolset.SlateInspectorToolset.TouchMove": "unknown", // EPIC accessMode=execute
  // Put a finger down and leave it there; it shares the left mouse button with the cursor, so do not mix it with a
  "SlateInspectorToolset.SlateInspectorToolset.TouchStart": "unknown", // EPIC accessMode=execute
  // Type text into a Slate text input widget, focusing it first; use PressKey for keys that are not characters.
  "SlateInspectorToolset.SlateInspectorToolset.Type": "mutate",
  // Remove an observer by its identifier.
  "SlateInspectorToolset.SlateInspectorToolset.Unobserve": "mutate",
  // Check once whether text is present or absent in the Slate widget tree (substring match); poll to wait.
  "SlateInspectorToolset.SlateInspectorToolset.WaitFor": "read",
  // List, select, close or resize top-level Slate editor windows. "list" returns a JSON array of {index, ref, titl
  "SlateInspectorToolset.SlateInspectorToolset.Windows": "unknown",

  /* ── SourceControl.SourceControlHelpers ────────────────────────── */
  // Adds files to source control; they are added on the next submit. Async: await the result. The provider decides
  "SourceControl.SourceControlHelpers.Add": "mutate", // EPIC accessMode=write
  // Checks files out, so they can be edited and submitted. Async: await the result. The provider decides which fil
  "SourceControl.SourceControlHelpers.CheckOut": "mutate", // EPIC accessMode=write
  // Creates a branch off the current branch. Async: await the result. Does not change local files. Providers may r
  "SourceControl.SourceControlHelpers.CreateBranch": "mutate", // EPIC accessMode=write
  // Creates a new pending changelist and moves the specified files into it. Async: await the result.
  "SourceControl.SourceControlHelpers.CreateChangelist": "mutate", // EPIC accessMode=write
  // Marks files for delete and removes them from disk, discarding any unsaved changes in their loaded packages. Un
  "SourceControl.SourceControlHelpers.Delete": "unknown", // EPIC accessMode=execute
  // Deletes a branch for every user. Providers that keep history may archive it instead. Async: await the result. 
  "SourceControl.SourceControlHelpers.DeleteBranch": "unknown", // EPIC accessMode=execute
  // Downloads a revision of a file to a local path, without changing the workspace. Async: await the result. Refre
  "SourceControl.SourceControlHelpers.Download": "mutate", // EPIC accessMode=write
  // Replaces the description of a pending changelist. Async: await the result. The default changelist's descriptio
  "SourceControl.SourceControlHelpers.EditChangelistDescription": "mutate", // EPIC accessMode=write
  // Returns the source-control state for an asset. Blocks until the provider answers. Does not write to the messag
  "SourceControl.SourceControlHelpers.GetAssetSourceControlState": "read", // EPIC accessMode=read
  // Lists the files in a pending changelist, including files that are not content. Raises when source control is u
  "SourceControl.SourceControlHelpers.GetChangelistFiles": "read", // EPIC accessMode=read
  // Returns the content paths of all assets you currently have open in source control (checked out, marked for add
  "SourceControl.SourceControlHelpers.GetCheckedOutAssets": "read", // EPIC accessMode=read
  // Returns the source-control revision history for an asset, newest first. Async: await the result.
  "SourceControl.SourceControlHelpers.ListAssetRevisions": "read", // EPIC accessMode=read
  // Lists the branches of a repository. Async: await the result.
  "SourceControl.SourceControlHelpers.ListBranches": "read", // EPIC accessMode=read
  // Lists the pending changelists, including the default changelist. Raises when source control is unavailable, th
  "SourceControl.SourceControlHelpers.ListChangelists": "read", // EPIC accessMode=read
  // Lists submitted changelists, one page at a time, in the order the provider reports them, typically newest firs
  "SourceControl.SourceControlHelpers.ListSubmittedChangelists": "read", // EPIC accessMode=read
  // Loads a historical revision of an asset alongside the currently loaded one. Async: await the result. Treat the
  "SourceControl.SourceControlHelpers.LoadAssetRevision": "read", // EPIC accessMode=read
  // Merges the head of another branch into the current branch and reloads the loaded packages the merge changes, i
  "SourceControl.SourceControlHelpers.MergeBranch": "unknown", // EPIC accessMode=execute
  // Moves files into an existing pending changelist. Async: await the result.
  "SourceControl.SourceControlHelpers.MoveFilesToChangelist": "mutate", // EPIC accessMode=write
  // Pushes a branch to the server. Async: await the result. Does not change local files.
  "SourceControl.SourceControlHelpers.PushBranch": "mutate", // EPIC accessMode=write
  // Gets the details of a branch. Async: await the result.
  "SourceControl.SourceControlHelpers.QueryBranch": "read", // EPIC accessMode=read
  // Gets the details of a submitted changelist, such as its description, author and date. Async: await the result.
  "SourceControl.SourceControlHelpers.QueryChangelistDetails": "read", // EPIC accessMode=read
  // Gets the revision history of any file, such as a Verse file, a project file or an asset, newest first. Async: 
  "SourceControl.SourceControlHelpers.QueryHistory": "read", // EPIC accessMode=read
  // Gets the status the provider reports for a repository, for example the current branch, user and workspace. Asy
  "SourceControl.SourceControlHelpers.QueryProvider": "read", // EPIC accessMode=read
  // Gets the current source control states of files, refreshing them from the provider first. Async: await the res
  "SourceControl.SourceControlHelpers.QueryStates": "read", // EPIC accessMode=read
  // Marks conflicted mergeable files, such as text or Verse files, resolved once their conflict markers have been 
  "SourceControl.SourceControlHelpers.Resolve": "mutate", // EPIC accessMode=write
  // Completes the resolution of conflicts, once every conflicted file has been resolved with Resolve, ResolveMine 
  "SourceControl.SourceControlHelpers.ResolveComplete": "mutate", // EPIC accessMode=write
  // Resolves conflicted files by keeping the local version, and reloads their loaded packages, including the open 
  "SourceControl.SourceControlHelpers.ResolveMine": "mutate", // EPIC accessMode=write
  // Resolves conflicted files by taking the incoming version, discarding their local changes, and reloads their lo
  "SourceControl.SourceControlHelpers.ResolveTheirs": "unknown", // EPIC accessMode=execute
  // Makes the synced revision the latest revision of the current branch again, by submitting a new revision with i
  "SourceControl.SourceControlHelpers.RestoreAsLatest": "unknown", // EPIC accessMode=execute
  // Restores a branch removed by DeleteBranch. Async: await the result. Does not change local files. The provider 
  "SourceControl.SourceControlHelpers.RestoreBranch": "mutate", // EPIC accessMode=write
  // Reverts files, discarding their local changes and any unsaved changes in their loaded packages, and reloads th
  "SourceControl.SourceControlHelpers.Revert": "unknown", // EPIC accessMode=execute
  // Releases the checkout of the given files that are checked out but not modified, so other users can check them 
  "SourceControl.SourceControlHelpers.RevertUnchanged": "mutate", // EPIC accessMode=write
  // Submits a pending changelist after validating it, and fails with the validation errors if it does not pass. Wa
  "SourceControl.SourceControlHelpers.SubmitChangelist": "unknown", // EPIC accessMode=execute
  // Submits every pending change under the source control locations, without a changelist. Blocks until the submit
  "SourceControl.SourceControlHelpers.SubmitChanges": "unknown", // EPIC accessMode=execute
  // Switches to the head of another branch and reloads the loaded packages the switch changes, including the open 
  "SourceControl.SourceControlHelpers.SwitchBranch": "unknown", // EPIC accessMode=execute
  // Syncs to a revision and reloads the loaded packages the sync changes, including the open level when its map or
  "SourceControl.SourceControlHelpers.Sync": "mutate", // EPIC accessMode=write
  // Validates a pending changelist the way the submit check does: the registered validators run on the changelist 
  "SourceControl.SourceControlHelpers.ValidateChangelist": "read", // EPIC accessMode=read
  // Validates every pending change under the source control locations, without a changelist, the way the check-in 
  "SourceControl.SourceControlHelpers.ValidateChanges": "read", // EPIC accessMode=read

  /* ── state_tree_toolset.state_tree.StateTreeToolset ────────────── */
  // Adds a Blueprint-based utility consideration to a state. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_consideration": "mutate", // EPIC accessMode=write
  // Adds a Blueprint-based enter condition to a state. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_enter_condition": "mutate", // EPIC accessMode=write
  // Adds a Blueprint-based evaluator to the asset. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_evaluator": "mutate", // EPIC accessMode=write
  // Adds a Blueprint-based global task to the asset. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_global_task": "mutate", // EPIC accessMode=write
  // Adds a Blueprint-based task to a state. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_blueprint_task": "mutate", // EPIC accessMode=write
  // Adds a new child state under the given parent state.
  "state_tree_toolset.state_tree.StateTreeToolset.add_child_state": "mutate", // EPIC accessMode=write
  // Adds a struct-based utility consideration to a state. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_consideration": "mutate", // EPIC accessMode=write
  // Adds a struct-based enter condition to a state. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_enter_condition": "mutate", // EPIC accessMode=write
  // Adds a struct-based evaluator to the asset. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_evaluator": "mutate", // EPIC accessMode=write
  // Adds a struct-based global task to the asset. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_global_task": "mutate", // EPIC accessMode=write
  // Adds a new top-level subtree to the asset.
  "state_tree_toolset.state_tree.StateTreeToolset.add_sub_tree": "mutate", // EPIC accessMode=write
  // Adds a struct-based task to a state. Returns the new node's ID.
  "state_tree_toolset.state_tree.StateTreeToolset.add_task": "mutate", // EPIC accessMode=write
  // Compiles a StateTree. Returns whether it succeeded and the compiler's messages.
  "state_tree_toolset.state_tree.StateTreeToolset.compile": "mutate", // EPIC accessMode=write
  // Returns every state in the tree (root + descendants), in hierarchy order.
  "state_tree_toolset.state_tree.StateTreeToolset.get_all_states": "read", // EPIC accessMode=read
  // Returns the immediate child states of a state.
  "state_tree_toolset.state_tree.StateTreeToolset.get_children": "read", // EPIC accessMode=read
  // Returns the utility considerations on a state.
  "state_tree_toolset.state_tree.StateTreeToolset.get_considerations": "read", // EPIC accessMode=read
  // Returns the editor data for the asset.
  "state_tree_toolset.state_tree.StateTreeToolset.get_editor_data": "read", // EPIC accessMode=read
  // Returns the enter conditions on a state.
  "state_tree_toolset.state_tree.StateTreeToolset.get_enter_conditions": "read", // EPIC accessMode=read
  // Returns the global evaluators.
  "state_tree_toolset.state_tree.StateTreeToolset.get_evaluators": "read", // EPIC accessMode=read
  // Returns the first editor-data extension of the requested type, or None.
  "state_tree_toolset.state_tree.StateTreeToolset.get_extension": "read", // EPIC accessMode=read
  // Returns the global tasks (running across all states).
  "state_tree_toolset.state_tree.StateTreeToolset.get_global_tasks": "read", // EPIC accessMode=read
  // Returns a human-readable description of an editor node.
  "state_tree_toolset.state_tree.StateTreeToolset.get_node_description": "read", // EPIC accessMode=read
  // Returns the node's Execution Runtime struct (per-execution scratch data).
  "state_tree_toolset.state_tree.StateTreeToolset.get_node_execution_runtime_data": "read", // EPIC accessMode=read
  // Returns the node's Execution Runtime UObject.
  "state_tree_toolset.state_tree.StateTreeToolset.get_node_execution_runtime_object": "read", // EPIC accessMode=read
  // Returns the node's Instance struct (per-state-tree-instance data, struct-typed).
  "state_tree_toolset.state_tree.StateTreeToolset.get_node_instance_data": "read", // EPIC accessMode=read
  // Returns the node's Instance UObject (per-state-tree-instance data, UObject-typed).
  "state_tree_toolset.state_tree.StateTreeToolset.get_node_instance_object": "read", // EPIC accessMode=read
  // Returns the node's Template struct (the type-defining, shared data).
  "state_tree_toolset.state_tree.StateTreeToolset.get_node_template_data": "read", // EPIC accessMode=read
  // Returns the top-level (root) states of the asset.
  "state_tree_toolset.state_tree.StateTreeToolset.get_root_states": "read", // EPIC accessMode=read
  // Returns the tasks on a state.
  "state_tree_toolset.state_tree.StateTreeToolset.get_tasks": "read", // EPIC accessMode=read
  // Returns the outgoing transitions on a state.
  "state_tree_toolset.state_tree.StateTreeToolset.get_transitions": "read", // EPIC accessMode=read
  // Marks the asset as dirty so a recompile and UI refresh happen on the next tick. Call this when you've mutated 
  "state_tree_toolset.state_tree.StateTreeToolset.mark_as_modified": "mutate", // EPIC accessMode=write
  // Moves a child state to a specific index in its parent's children list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_child_state": "mutate", // EPIC accessMode=write
  // Moves a consideration to a specific index in the state's consideration list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_consideration": "mutate", // EPIC accessMode=write
  // Moves an enter condition to a specific index in the state's enter-condition list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_enter_condition": "mutate", // EPIC accessMode=write
  // Moves an evaluator to a specific index in the asset's evaluator list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_evaluator": "mutate", // EPIC accessMode=write
  // Moves a global task to a specific index in the asset's global-task list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_global_task": "mutate", // EPIC accessMode=write
  // Moves an existing subtree to a specific index in the subtree list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_sub_tree": "mutate", // EPIC accessMode=write
  // Moves a task to a specific index in the state's task list.
  "state_tree_toolset.state_tree.StateTreeToolset.move_task": "mutate", // EPIC accessMode=write
  // Removes a child state from its parent.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_child_state": "mutate", // EPIC accessMode=write
  // Removes a consideration from a state by ID.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_consideration": "mutate", // EPIC accessMode=write
  // Removes an enter condition from a state by ID.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_enter_condition": "mutate", // EPIC accessMode=write
  // Removes an evaluator by ID.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_evaluator": "mutate", // EPIC accessMode=write
  // Removes a global task by ID.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_global_task": "mutate", // EPIC accessMode=write
  // Removes a subtree from the asset.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_sub_tree": "mutate", // EPIC accessMode=write
  // Removes a task from a state by ID.
  "state_tree_toolset.state_tree.StateTreeToolset.remove_task": "mutate", // EPIC accessMode=write
  // Requests a full refresh of the open StateTree editor (no-op if no editor is open).
  "state_tree_toolset.state_tree.StateTreeToolset.request_ui_refresh": "mutate", // EPIC accessMode=write
  // Sets the node's Execution Runtime struct. The struct types must match.
  "state_tree_toolset.state_tree.StateTreeToolset.set_node_execution_runtime_data": "mutate", // EPIC accessMode=write
  // Sets the node's Instance struct. The struct types must match.
  "state_tree_toolset.state_tree.StateTreeToolset.set_node_instance_data": "mutate", // EPIC accessMode=write
  // Sets the node's Template struct. The struct types must match.
  "state_tree_toolset.state_tree.StateTreeToolset.set_node_template_data": "mutate", // EPIC accessMode=write

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

  /* ── TemplateSequenceEditor.TemplateSequenceEditorSubsystem ────── */
  // The actor class the sequence binds to, or None when the sequence has no root binding.
  "TemplateSequenceEditor.TemplateSequenceEditorSubsystem.GetBoundActorClass": "read", // EPIC accessMode=read
  // Rebind the sequence to ActorClass by replacing its root spawnable, moving the tracks and component bindings un
  "TemplateSequenceEditor.TemplateSequenceEditorSubsystem.SetBoundActorClass": "mutate", // EPIC accessMode=write

  /* ── TextureGraphToolset.TextureGraphTools ─────────────────────── */
  // Adds an input-parameter node (a graph-level parameter such as Scalar, Color, or Vector) and returns it. The pa
  "TextureGraphToolset.TextureGraphTools.AddInputParam": "unknown", // EPIC accessMode=execute
  // Adds a node of the given expression type to the graph and returns the new node. Input-parameter and Output exp
  "TextureGraphToolset.TextureGraphTools.AddNode": "unknown", // EPIC accessMode=execute
  // Adds an Output node and returns it. The output is exposed on the graph automatically. If OutputName is set it 
  "TextureGraphToolset.TextureGraphTools.AddOutput": "unknown", // EPIC accessMode=execute
  // Connects an output pin on FromNode to an input pin on ToNode.
  "TextureGraphToolset.TextureGraphTools.ConnectPins": "unknown", // EPIC accessMode=execute
  // Creates a new empty TextureGraph asset. This should ONLY be called after getting explicit direction or permiss
  "TextureGraphToolset.TextureGraphTools.CreateTextureGraph": "unknown", // EPIC accessMode=execute
  // Removes the edge between the specified pins.
  "TextureGraphToolset.TextureGraphTools.DisconnectPins": "unknown", // EPIC accessMode=execute
  // Exports every texture output node to a Texture2D asset in the given content folder. Triggers a full graph eval
  "TextureGraphToolset.TextureGraphTools.ExportTextureGraph": "unknown", // EPIC accessMode=execute
  // Returns every node in the graph, each with its input and output pins and, for a connected input pin, the node 
  "TextureGraphToolset.TextureGraphTools.GetGraph": "unknown", // EPIC accessMode=execute
  // Validates the graph and returns every issue it reports: the errors and warnings the engine's validation surfac
  "TextureGraphToolset.TextureGraphTools.GetGraphIssues": "unknown", // EPIC accessMode=execute
  // Returns the graph's input-parameter nodes: the graph-level parameters such as Scalar, Color, or Vector.
  "TextureGraphToolset.TextureGraphTools.ListInputParams": "unknown", // EPIC accessMode=execute
  // Returns every available TextureGraph expression node type, sorted. A returned class identifies the node type t
  "TextureGraphToolset.TextureGraphTools.ListNodeTypes": "unknown", // EPIC accessMode=execute
  // Returns the graph's Output nodes.
  "TextureGraphToolset.TextureGraphTools.ListOutputs": "unknown", // EPIC accessMode=execute
  // Removes the input-parameter node exposing the named parameter.
  "TextureGraphToolset.TextureGraphTools.RemoveInputParam": "unknown", // EPIC accessMode=execute
  // Removes a node and all its connected edges from the graph.
  "TextureGraphToolset.TextureGraphTools.RemoveNode": "unknown", // EPIC accessMode=execute
  // Removes the Output node exposing the named output.
  "TextureGraphToolset.TextureGraphTools.RemoveOutput": "unknown", // EPIC accessMode=execute
  // Renames a node by setting its primary pin's alias name. For Output nodes this additionally syncs OutputSetting
  "TextureGraphToolset.TextureGraphTools.SetNodeAlias": "unknown", // EPIC accessMode=execute
  // Typed shortcut for the most common Output-node configuration. Sets BaseName, FolderPath, and TexturePresetType
  "TextureGraphToolset.TextureGraphTools.SetOutputSettings": "unknown", // EPIC accessMode=execute
  // Sets one input pin's value on a node, resolving the pin by argument name or alias. For a polymorphic Variant p
  "TextureGraphToolset.TextureGraphTools.SetPinValue": "unknown", // EPIC accessMode=execute

  /* ── ToolsetRegistry.AgentSkillToolset ─────────────────────────── */
  // Creates a new AgentSkill. This should ONLY be called after getting explicit direction or permission from the u
  "ToolsetRegistry.AgentSkillToolset.CreateSkill": "mutate",
  // Returns detailed information about a specific set of AgentSkills.
  "ToolsetRegistry.AgentSkillToolset.GetSkills": "read",
  // Gets a summary of all AgentSkills in the project.
  "ToolsetRegistry.AgentSkillToolset.ListSkills": "read",
  // Updates an existing AgentSkill. This should ONLY be called after getting explicit direction or permission from
  "ToolsetRegistry.AgentSkillToolset.UpdateSkill": "mutate",

  /* ── UMGEditor.UMGDebuggerSubsystem ────────────────────────────── */
  // Discards all captured events.
  "UMGEditor.UMGDebuggerSubsystem.ClearWidgetLifetimeLog": "mutate", // EPIC accessMode=write
  // Returns captured widget lifetime events, newest first.
  "UMGEditor.UMGDebuggerSubsystem.GetWidgetLifetimeEvents": "read", // EPIC accessMode=read
  // Returns whether recording is running plus how many events are held and were dropped.
  "UMGEditor.UMGDebuggerSubsystem.GetWidgetLifetimeLogStatus": "read", // EPIC accessMode=read
  // Opens or focuses the Widget Lifetime Log editor tab, which shows the same recording. Returns false without a S
  "UMGEditor.UMGDebuggerSubsystem.ShowWidgetLifetimeLogWindow": "mutate", // EPIC accessMode=write
  // Starts recording Constructed, Destructed and Rebuilt events for every user widget, discarding events already c
  "UMGEditor.UMGDebuggerSubsystem.StartWidgetLifetimeLogging": "mutate", // EPIC accessMode=write
  // Stops recording. Captured events stay queryable until ClearWidgetLifetimeLog or the next start.
  "UMGEditor.UMGDebuggerSubsystem.StopWidgetLifetimeLogging": "mutate", // EPIC accessMode=write

  /* ── UMGEditor.WidgetBlueprintEditorSubsystem ──────────────────── */
  // Adds a UI component of the given class to the named widget.
  "UMGEditor.WidgetBlueprintEditorSubsystem.AddUIComponent": "mutate", // EPIC accessMode=write
  // Adds a Blueprint event handler graph node bound to a widget's multicast delegate event. Typical events: UButto
  "UMGEditor.WidgetBlueprintEditorSubsystem.BindToEventProperty": "mutate", // EPIC accessMode=write
  // Compiles a widget blueprint. Returns false with error details if compilation fails. Errors include missing Bin
  "UMGEditor.WidgetBlueprintEditorSubsystem.CompileWidgetBlueprint": "mutate", // EPIC accessMode=write
  // Adds a widget to the tree at the specified position. When ParentWidget is null and no root exists, the new wid
  "UMGEditor.WidgetBlueprintEditorSubsystem.CreateAndAddWidget": "mutate", // EPIC accessMode=write
  // Creates a new Widget Blueprint asset. Returns the blueprint or nullptr on failure.
  "UMGEditor.WidgetBlueprintEditorSubsystem.CreateWidgetBlueprint": "mutate", // EPIC accessMode=write
  // Returns navigation/focus documentation for a container widget class - both layout panels (UScrollBox, UVertica
  "UMGEditor.WidgetBlueprintEditorSubsystem.GetContainerWidgetNavigationDoc": "read", // EPIC accessMode=read
  // Returns available named slots and their content, if any (separate from tree hierarchy).
  "UMGEditor.WidgetBlueprintEditorSubsystem.GetNamedSlots": "read", // EPIC accessMode=read
  // Returns the Category, Description, and panel flag for a single widget class. Returns an empty entry if WidgetC
  "UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgetClassInfo": "read", // EPIC accessMode=read
  // Returns blueprint info and all widgets in depth-first order. Children within each parent are in their panel sl
  "UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgets": "read", // EPIC accessMode=read
  // Lists widget blueprints in a content folder.
  "UMGEditor.WidgetBlueprintEditorSubsystem.ListWidgetBlueprints": "read", // EPIC accessMode=read
  // Lists available widget classes, optionally filtered by name substring.
  "UMGEditor.WidgetBlueprintEditorSubsystem.ListWidgetClasses": "read", // EPIC accessMode=read
  // Moves a UI component before or after another component on the same widget.
  "UMGEditor.WidgetBlueprintEditorSubsystem.MoveUIComponent": "mutate", // EPIC accessMode=write
  // Moves a widget to a new parent panel at the specified position, within one widget blueprint. Returns updated w
  "UMGEditor.WidgetBlueprintEditorSubsystem.MoveWidget": "mutate", // EPIC accessMode=write
  // Removes a UI component of the given class from the named widget.
  "UMGEditor.WidgetBlueprintEditorSubsystem.RemoveUIComponent": "mutate", // EPIC accessMode=write
  // Removes a widget and its children from the tree.
  "UMGEditor.WidgetBlueprintEditorSubsystem.RemoveWidget": "mutate", // EPIC accessMode=write
  // Renames a widget and updates the references to it. Returns updated widget info, or an empty info (with a scrip
  "UMGEditor.WidgetBlueprintEditorSubsystem.RenameWidget": "mutate", // EPIC accessMode=write
  // Replaces a panel widget with its first child, removing the panel from the tree. The widget to replace must be 
  "UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithChild": "mutate", // EPIC accessMode=write
  // Replaces a host widget with the content of one of its named slots. The host must implement INamedSlotInterface
  "UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithNamedSlot": "mutate", // EPIC accessMode=write
  // Replaces a widget instance in the blueprint's widget tree with a new instance created from a different templat
  "UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithTemplate": "mutate", // EPIC accessMode=write
  // Moves or adds content for a named slot. Use this to set new widget based on WidgetClass or attempt to find a w
  "UMGEditor.WidgetBlueprintEditorSubsystem.SetNamedSlotContent": "mutate", // EPIC accessMode=write
  // Sets the bIsVariable flag.
  "UMGEditor.WidgetBlueprintEditorSubsystem.ToggleWidgetAsVariable": "mutate", // EPIC accessMode=write
  // Wraps one or more widgets in a new panel widget of the specified class. Only the root-most widgets in the sele
  "UMGEditor.WidgetBlueprintEditorSubsystem.WrapWidgets": "mutate", // EPIC accessMode=write

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

  /* ── UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem  */
  // Annotate a scene actor, so its meshes are marked in the matte. Annotating it again replaces the prompt.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.AnnotateActor": "mutate", // EPIC accessMode=write
  // Whether annotated scene actors draw through occluders, which also makes the matte ignore occlusion.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.AreAnnotatedActorsAlwaysOnTop": "read", // EPIC accessMode=read
  // Whether the viewport draws the prompt labels. Each annotation can also hide its own (bShowLabel).
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.AreLabelsVisible": "read", // EPIC accessMode=read
  // Find an annotation by the id an MCP tool or GetAnnotations reported.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.FindAnnotation": "read", // EPIC accessMode=read
  // The group, 1-8, that new annotations join when no group is given.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetActiveGroup": "read", // EPIC accessMode=read
  // List every annotation in the editor level, proxies and annotated actors alike.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetAnnotations": "read", // EPIC accessMode=read
  // Where in the frame the viewport capture reads colour.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetCaptureStage": "read", // EPIC accessMode=read
  // The colour groups shown in the viewport overlay and the matte, in ascending order.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetVisibleGroups": "read", // EPIC accessMode=read
  // Show only this group, 1-8, and make it the active group.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.IsolateGroup": "mutate", // EPIC accessMode=write
  // Whether the viewport draws the coloured annotation overlay. The matte and passes do not depend on it.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.IsOverlayVisible": "read", // EPIC accessMode=read
  // Save every scratch annotation with the level from now on.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.PersistAnnotations": "mutate", // EPIC accessMode=write
  // Place a proxy region carrying a prompt.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.PlaceAnnotation": "mutate", // EPIC accessMode=write
  // 
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.RemoveAllAnnotations": "mutate", // EPIC accessMode=write
  // Remove an annotation. A proxy region is deleted; a scene actor stays and loses its matte marking.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.RemoveAnnotation": "mutate", // EPIC accessMode=write
  // Remove every annotation the actors carry: the inverse of annotating them.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.RemoveAnnotationsFromActors": "mutate", // EPIC accessMode=write
  // Set the group, 1-8, that new annotations join. It must be visible, or they would not show.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetActiveGroup": "mutate", // EPIC accessMode=write
  // For this editor session; Project Settings saves it. Proxies use their own DepthMode instead.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotatedActorsAlwaysOnTop": "mutate", // EPIC accessMode=write
  // Recolour an annotation into another group, 1-8.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotationGroup": "mutate", // EPIC accessMode=write
  // Set Annotation Prompt
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotationPrompt": "mutate", // EPIC accessMode=write
  // Change a proxy region's shape. An annotation on a scene actor takes the actor's own shape and is refused.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotationShape": "mutate", // EPIC accessMode=write
  // For this editor session; Project Settings saves it.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetCaptureStage": "mutate", // EPIC accessMode=write
  // Set Labels Visible
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetLabelsVisible": "mutate", // EPIC accessMode=write
  // Show or hide the viewport overlay. It turns on again when a level that had no annotations gets one.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetOverlayVisible": "mutate", // EPIC accessMode=write
  // For this editor session; Project Settings > Plugins > Unreal Canvas Spatial Annotations saves it.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetPersistNewAnnotations": "mutate", // EPIC accessMode=write
  // Show only these colour groups. If the active group ends up hidden it moves to the lowest visible one.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetVisibleGroups": "mutate", // EPIC accessMode=write
  // Whether new annotations are saved with the level (the default) or are scratch work.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.ShouldPersistNewAnnotations": "read", // EPIC accessMode=read
  // Show All Groups
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.ShowAllGroups": "mutate", // EPIC accessMode=write

  /* ── UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset  */
  // Returns the annotations with their on-screen rects, as the JSON the image pipeline consumes. Each entry carrie
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.GatherVisibleAnnotations": "read", // EPIC accessMode=read
  // Shows a single colour group and hides the rest, so the annotation guide marks one target. Use this to work thr
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.IsolateAnnotationGroup": "mutate", // EPIC accessMode=write
  // Lists every 3D spatial annotation in the level, with its id, objectId, prompt, colour, colour group (1-8), wor
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.ListAnnotations": "read", // EPIC accessMode=read
  // Places a new 3D annotation proxy box carrying a spatial prompt.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.PlaceAnnotation": "mutate", // EPIC accessMode=write
  // Deletes one annotation.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.RemoveAnnotation": "mutate", // EPIC accessMode=write
  // Deletes every annotation on the currently selected actors - the inverse of annotating a selection. A custom-pl
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.RemoveAnnotationsFromSelection": "mutate", // EPIC accessMode=write
  // Shows a specific set of colour groups and hides every other one. If the active group ends up hidden it moves t
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.SetVisibleAnnotationGroups": "mutate", // EPIC accessMode=write
  // Makes every annotation colour group visible again, undoing an isolate or set-visible call. Nothing is hidden a
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.ShowAllAnnotationGroups": "mutate", // EPIC accessMode=write
  // Changes an existing annotation's prompt and/or colour group.
  "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.UpdateAnnotation": "mutate", // EPIC accessMode=write

  /* ── WidgetAnimationToolset.WidgetAnimationToolset ─────────────── */
  // Add a UWidget (or a UPanelSlot with non-null content) as a possessable binding on a widget animation. Returns 
  "WidgetAnimationToolset.WidgetAnimationToolset.AddWidgetToAnimation": "mutate", // EPIC accessMode=write
  // Create a new UWidgetAnimation on a Widget Blueprint and return its summary. The empty FWidgetAnimationInfo ret
  "WidgetAnimationToolset.WidgetAnimationToolset.CreateWidgetAnimation": "mutate", // EPIC accessMode=write
  // Find an animation by internal name. Returns empty info (Animation == nullptr) when not found.
  "WidgetAnimationToolset.WidgetAnimationToolset.FindWidgetAnimation": "read", // EPIC accessMode=read
  // Get all bindings on a widget animation as FMovieSceneBindingProxy entries.
  "WidgetAnimationToolset.WidgetAnimationToolset.GetWidgetAnimationBindings": "read", // EPIC accessMode=read
  // List all animations on a Widget Blueprint.
  "WidgetAnimationToolset.WidgetAnimationToolset.ListWidgetAnimations": "read", // EPIC accessMode=read
  // Remove an animation from a Widget Blueprint.
  "WidgetAnimationToolset.WidgetAnimationToolset.RemoveWidgetAnimation": "mutate", // EPIC accessMode=write
  // Remove a binding (by GUID) from a widget animation. Removes both the MovieScene possessable and the UMG bindin
  "WidgetAnimationToolset.WidgetAnimationToolset.RemoveWidgetBinding": "mutate", // EPIC accessMode=write
  // Rename an animation. NewName must not collide with an existing animation on the blueprint.
  "WidgetAnimationToolset.WidgetAnimationToolset.RenameWidgetAnimation": "mutate", // EPIC accessMode=write
  // Set the user-facing display label shown in the Animations panel.
  "WidgetAnimationToolset.WidgetAnimationToolset.SetWidgetAnimationDisplayLabel": "mutate", // EPIC accessMode=write
  // Set the animation playback length in seconds. Updates the MovieScene playback range to [0, LengthSeconds] in t
  "WidgetAnimationToolset.WidgetAnimationToolset.SetWidgetAnimationLength": "mutate", // EPIC accessMode=write

  /* ── WidgetFinder.WidgetFinderSubsystem ────────────────────────── */
  // Remove any highlight overlay drawn by Highlight.
  "WidgetFinder.WidgetFinderSubsystem.ClearHighlight": "mutate", // EPIC accessMode=write
  // Retrieve the complete query grammar, so a Query can be built without guessing. Works with the service stopped.
  "WidgetFinder.WidgetFinderSubsystem.GetQuerySyntax": "read", // EPIC accessMode=read
  // Read the service state without changing it, to check it is up or that a start stored something.
  "WidgetFinder.WidgetFinderSubsystem.GetServiceStatus": "read", // EPIC accessMode=read
  // Outline every match in the live UI with the Widget Reflector's debug border, to see what a query selects. Need
  "WidgetFinder.WidgetFinderSubsystem.Highlight": "mutate", // EPIC accessMode=write
  // Locate live Slate widgets with a query, whose grammar GetQuerySyntax returns. Needs a running service. Check b
  "WidgetFinder.WidgetFinderSubsystem.Query": "read", // EPIC accessMode=read
  // Begin mirroring the live Slate widget tree into the store, so Query/Highlight/WaitForQuery can work. The servi
  "WidgetFinder.WidgetFinderSubsystem.StartService": "mutate", // EPIC accessMode=write
  // Stop mirroring and empty the store, ending the scanning cost. Pending WaitForQuery calls fail at once, and the
  "WidgetFinder.WidgetFinderSubsystem.StopService": "mutate", // EPIC accessMode=write
  // Wait, without polling, until a query's raw match count meets Comparison/Threshold, then return the matches. Pr
  "WidgetFinder.WidgetFinderSubsystem.WaitForQuery": "read", // EPIC accessMode=read

  /* ── WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset ───── */
  // Full structured description of every widget in the tree (or the subtree rooted at StartWidget). Values are the
  "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.DescribeWidget": "read", // EPIC accessMode=read
  // Reverse-searches every Widget Blueprint whose MVVM View references ViewModelClass, and returns each one's own 
  "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.GetViewModelConnectionMap": "read", // EPIC accessMode=read
  // Maps every connection on WidgetBlueprint (MVVM field bindings, event bindings, delegates, and legacy property 
  "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.GetWidgetConnectionMap": "read", // EPIC accessMode=read
  // Returns the maximum depth of the widget tree. Depth: root with no children = 0; root + children = 1; etc.
  "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.GetWidgetTreeDepth": "read", // EPIC accessMode=read
  // Traces every statically-discoverable write to a widget property across the whole tree, merging Blueprint-graph
  "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.TraceWidgetPropertyOverrides": "read", // EPIC accessMode=read

  /* ── WorldConditionsToolset.WorldConditionTools ────────────────── */
  // Returns a human-readable description of a single world condition. The condition must be passed as an FInstance
  "WorldConditionsToolset.WorldConditionTools.GetConditionDescription": "read",
  // Returns a human-readable description of a world condition query.
  "WorldConditionsToolset.WorldConditionTools.GetQueryDescription": "read",
};
