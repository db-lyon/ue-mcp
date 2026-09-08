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

const S_epic_add_component = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"component_type":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["owner","component_type","name"]} as const;
const S_epic_add_cone = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"height":{"type":"number"},"local_transform":{"type":"object"}},"required":["actor","name"]} as const;
const S_epic_add_cube = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"dimensions":{"type":"object"},"local_transform":{"type":"object"}},"required":["actor","name"]} as const;
const S_epic_add_cylinder = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"height":{"type":"number"},"local_transform":{"type":"object"}},"required":["actor","name"]} as const;
const S_epic_add_sphere = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"local_transform":{"type":"object"}},"required":["actor","name"]} as const;
const S_epic_add_tag = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_add_to_scene_from_asset = {"properties":{"asset_path":{"type":"string"},"name":{"type":"string"},"xform":{"type":"object"},"parent":{"type":"object","properties":{"refPath":{}}},"snap_to_ground":{"type":"boolean"}},"required":["asset_path","name","xform"]} as const;
const S_epic_add_to_scene_from_class = {"properties":{"actor_type":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"xform":{"type":"object"},"parent":{"type":"object","properties":{"refPath":{}}},"snap_to_ground":{"type":"boolean"}},"required":["actor_type","name","xform"]} as const;
const S_epic_can_edit = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_commit_level_instance = {"properties":{"level_instance":{"type":"object","properties":{"refPath":{}}},"discard":{"type":"boolean"}},"required":["level_instance"]} as const;
const S_epic_create_level_instance = {"properties":{"level_path":{"type":"string"},"name":{"type":"string"},"xform":{"type":"object"},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["level_path","name","xform"]} as const;
const S_epic_delete_folder = {"properties":{"folder_path":{"type":"string"}},"required":["folder_path"]} as const;
const S_epic_edit_level_instance = {"properties":{"level_instance":{"type":"object","properties":{"refPath":{}}}},"required":["level_instance"]} as const;
const S_epic_find_actors = {"properties":{"root":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"actor_type":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"},"bounds":{"type":"object"},"collision_channels":{"type":"array"}},"required":["name","tag","collision_channels"]} as const;
const S_epic_get_actor_bounds = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actor_transform = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actors_in_folder = {"properties":{"folder_path":{"type":"string"},"recursive":{"type":"boolean"}},"required":["folder_path"]} as const;
const S_epic_get_collision_channels = {"properties":{}} as const;
const S_epic_get_component_actor = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_components = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component_type":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_current_level = {"properties":{}} as const;
const S_epic_get_folders = {"properties":{}} as const;
const S_epic_get_label = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_parent_component = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_root_component = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_tags = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_has_tag = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_is_checked_out = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_load_level = {"properties":{"level_path":{"type":"string"}},"required":["level_path"]} as const;
const S_epic_look_at = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"target":{"type":"object"}},"required":["actor","target"]} as const;
const S_epic_merge_actors = {"properties":{"actors":{"type":"array"},"output_path":{"type":"string"},"name":{"type":"string"},"destroy_source_actors":{"type":"boolean"}},"required":["actors","output_path","name"]} as const;
const S_epic_remove_component = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_remove_from_scene = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_remove_tag = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_rename_folder = {"properties":{"old_path":{"type":"string"},"new_path":{"type":"string"}},"required":["old_path","new_path"]} as const;
const S_epic_save_actor = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_set_actor_folder = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"folder_path":{"type":"string"}},"required":["actor","folder_path"]} as const;
const S_epic_set_actor_transform = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"xform":{"type":"object"},"worldspace":{"type":"boolean"}},"required":["actor","xform"]} as const;
const S_epic_set_label = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"label":{"type":"string"}},"required":["actor","label"]} as const;
const S_epic_set_parent_component = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_trace_world = {"properties":{"start":{"type":"object"},"end":{"type":"object"}},"required":["start","end"]} as const;

/** 41 wrapped engine tools routed to the `level` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_component: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Adds a component to an actor instance or blueprint. Params: owner, component_type, name",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.add_component", S_epic_add_component, p),
  ),
  epic_add_cone: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a cone-shaped StaticMeshComponent to an actor. Params: actor, name, radius?, height?, local_transform?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.primitive.PrimitiveTools", "editor_toolset.toolsets.primitive.PrimitiveTools.add_cone", S_epic_add_cone, p),
  ),
  epic_add_cube: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a cube-shaped StaticMeshComponent to an actor. Params: actor, name, dimensions?, local_transform?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.primitive.PrimitiveTools", "editor_toolset.toolsets.primitive.PrimitiveTools.add_cube", S_epic_add_cube, p),
  ),
  epic_add_cylinder: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a cylinder-shaped StaticMeshComponent to an actor. Params: actor, name, radius?, height?, local_transform?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.primitive.PrimitiveTools", "editor_toolset.toolsets.primitive.PrimitiveTools.add_cylinder", S_epic_add_cylinder, p),
  ),
  epic_add_sphere: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a sphere-shaped StaticMeshComponent to an actor. Params: actor, name, radius?, local_transform?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.primitive.PrimitiveTools", "editor_toolset.toolsets.primitive.PrimitiveTools.add_sphere", S_epic_add_sphere, p),
  ),
  epic_add_tag: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Adds a tag to an actor. Params: actor, tag",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.add_tag", S_epic_add_tag, p),
  ),
  epic_add_to_scene_from_asset: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Creates a new actor in the scene from an asset. Params: asset_path, name, xform, parent?, snap_to_ground?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", S_epic_add_to_scene_from_asset, p),
  ),
  epic_add_to_scene_from_class: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Creates a new instance of the specified object at the specified transform. Params: actor_type, name, xform, parent?, snap_to_ground?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class", S_epic_add_to_scene_from_class, p),
  ),
  epic_can_edit: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Checks whether an actor can be edited. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.can_edit", S_epic_can_edit, p),
  ),
  epic_commit_level_instance: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Saves or discards edits to a level instance and exits edit mode. Params: level_instance, discard?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.commit_level_instance", S_epic_commit_level_instance, p),
  ),
  epic_create_level_instance: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Creates a Level Instance actor in the scene referencing an existing level asset. Params: level_path, name, xform, parent?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.create_level_instance", S_epic_create_level_instance, p),
  ),
  epic_delete_folder: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Deletes a folder from the outliner. Actors directly in the folder are moved to the parent folder. Sub-folders and their actors are preserved by re-rooting them under the parent. For example, deleting 'Lighting' with a sub-folder 'Lighting/Spotlights' leaves 'Spotlights' intact under the parent. Params: folder_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.delete_folder", S_epic_delete_folder, p),
  ),
  epic_edit_level_instance: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Opens a level instance for editing. While in edit mode, scene tools such as add_to_scene_from_class and remove_from_scene operate within the level instance's sub-level. Only one level instance can be in edit mode at a time. Call commit_level_instance when done to save or discard changes. Params: level_instance",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.edit_level_instance", S_epic_edit_level_instance, p),
  ),
  epic_find_actors: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Searches the scene for actors that match specific criteria. Params: root?, name, actor_type?, tag, bounds?, collision_channels",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.find_actors", S_epic_find_actors, p),
  ),
  epic_get_actor_bounds: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the bounding box of an actor. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_actor_bounds", S_epic_get_actor_bounds, p),
  ),
  epic_get_actor_transform: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the position, rotation, and scale of an actor. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_actor_transform", S_epic_get_actor_transform, p),
  ),
  epic_get_actors_in_folder: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Returns the actors in the specified outliner folder. Params: folder_path, recursive?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.get_actors_in_folder", S_epic_get_actors_in_folder, p),
  ),
  epic_get_collision_channels: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Returns all available collision channels for use with find_actors. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.get_collision_channels", S_epic_get_collision_channels, p),
  ),
  epic_get_component_actor: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the actor that owns the specified component. Params: component",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_component_actor", S_epic_get_component_actor, p),
  ),
  epic_get_components: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the components that an actor contains. Params: actor, component_type?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_components", S_epic_get_components, p),
  ),
  epic_get_current_level: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Returns the path to the current level asset. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.get_current_level", S_epic_get_current_level, p),
  ),
  epic_get_folders: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Returns all folder paths currently in use in the outliner. Includes all intermediate parent paths. For example, if an actor is assigned to 'Lighting/Spotlights', both 'Lighting' and 'Lighting/Spotlights' are returned. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.get_folders", S_epic_get_folders, p),
  ),
  epic_get_label: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the actor's human friendly name as it appears in the editor. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_label", S_epic_get_label, p),
  ),
  epic_get_parent_component: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the parent component that this component is attached to, if any. Params: component",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_parent_component", S_epic_get_parent_component, p),
  ),
  epic_get_root_component: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the root component of an actor, if any. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_root_component", S_epic_get_root_component, p),
  ),
  epic_get_tags: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the list of tags on an actor. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.get_tags", S_epic_get_tags, p),
  ),
  epic_has_tag: bp(
    "read",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Returns whether an actor has a specific tag. Params: actor, tag",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.has_tag", S_epic_has_tag, p),
  ),
  epic_is_checked_out: bp(
    "read",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Checks whether an actor is checked out by the current user. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.is_checked_out", S_epic_is_checked_out, p),
  ),
  epic_load_level: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Loads a level in the editor. Params: level_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.load_level", S_epic_load_level, p),
  ),
  epic_look_at: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Rotates an actor so its forward vector points at a world-space position. Params: actor, target",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.look_at", S_epic_look_at, p),
  ),
  epic_merge_actors: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Merges multiple StaticMesh actors into a single mesh asset and actor. Params: actors, output_path, name, destroy_source_actors?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.merge_actors", S_epic_merge_actors, p),
  ),
  epic_remove_component: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Removes a component from an actor instance or blueprint. Params: component",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.remove_component", S_epic_remove_component, p),
  ),
  epic_remove_from_scene: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Deletes an actor from the scene. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.remove_from_scene", S_epic_remove_from_scene, p),
  ),
  epic_remove_tag: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Removes a tag from an actor. Params: actor, tag",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.remove_tag", S_epic_remove_tag, p),
  ),
  epic_rename_folder: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Renames a folder in the outliner. Updates the folder path for all actors in the folder and any sub-folders. For example, renaming 'Lighting' to 'Lights' also updates actors in 'Lighting/Spotlights' to 'Lights/Spotlights'. If the new path already exists then the affected actors will be merged into it. Params: old_path, new_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.rename_folder", S_epic_rename_folder, p),
  ),
  epic_save_actor: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Saves the actor to disk. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.save_actor", S_epic_save_actor, p),
  ),
  epic_set_actor_folder: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Assigns an actor to the specified folder in the outliner. Creates the folder implicitly if it does not already exist. Pass an empty string to move the actor to the root of the outliner. Params: actor, folder_path",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.set_actor_folder", S_epic_set_actor_folder, p),
  ),
  epic_set_actor_transform: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Updates the position, rotation, and/or scale of an actor. Params: actor, xform, worldspace?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.set_actor_transform", S_epic_set_actor_transform, p),
  ),
  epic_set_label: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Sets the human-friendly name of the actor. Params: actor, label",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.set_label", S_epic_set_label, p),
  ),
  epic_set_parent_component: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.actor.ActorTools] Sets the parent for the specified scene component. For blueprint actors, passing a component as the parent of the root promotes it to the scene root, making the current root a child of it. If the current root is a DefaultSceneRoot, Unreal will automatically remove it. Params: component, parent?",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.actor.ActorTools", "editor_toolset.toolsets.actor.ActorTools.set_parent_component", S_epic_set_parent_component, p),
  ),
  epic_trace_world: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.scene.SceneTools] Traces a line through the world and returns the distance to the first hit. Params: start, end",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.scene.SceneTools.trace_world", S_epic_trace_world, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actor: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  actor_type: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  actors: z.array(z.unknown()).optional(),
  asset_path: z.string().optional(),
  bounds: z.record(z.unknown()).optional(),
  collision_channels: z.array(z.unknown()).optional(),
  component: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  component_type: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  destroy_source_actors: z.boolean().optional(),
  dimensions: z.record(z.unknown()).optional(),
  discard: z.boolean().optional(),
  end: z.record(z.unknown()).optional(),
  folder_path: z.string().optional(),
  height: z.number().optional(),
  label: z.string().optional(),
  level_instance: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  level_path: z.string().optional(),
  local_transform: z.record(z.unknown()).optional().describe("Represents a 3D transformation with optional location, rotation, and scale. Unset fields mean \"identity\" when creating objects and \"don't change\" when modifyin…"),
  name: z.string().optional(),
  new_path: z.string().optional(),
  old_path: z.string().optional(),
  output_path: z.string().optional(),
  owner: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  parent: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  radius: z.number().optional(),
  recursive: z.boolean().optional(),
  root: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  snap_to_ground: z.boolean().optional(),
  start: z.record(z.unknown()).optional(),
  tag: z.string().optional(),
  target: z.record(z.unknown()).optional(),
  worldspace: z.boolean().optional(),
  xform: z.record(z.unknown()).optional().describe("Represents a 3D transformation with optional location, rotation, and scale. Unset fields mean \"identity\" when creating objects and \"don't change\" when modifyin…"),
};
