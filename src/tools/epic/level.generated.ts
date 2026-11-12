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

const S_epic_add_actors_to_data_layer = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"assetPath":{"type":"string"}},"required":["actors","assetPath"]} as const;
const S_epic_add_component = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"component_type":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["owner","component_type","name"]} as const;
const S_epic_add_component__1 = {"properties":{"owner":{"type":"object","properties":{"refPath":{}}},"componentType":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["owner","componentType","name"]} as const;
const S_epic_add_cone = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"height":{"type":"number"},"local_transform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_cone__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"height":{"type":"number"},"localTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_cube = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"dimensions":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"local_transform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_cube__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"dimensions":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"localTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_cylinder = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"height":{"type":"number"},"local_transform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_cylinder__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"height":{"type":"number"},"localTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_sphere = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"local_transform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_sphere__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"radius":{"type":"number"},"localTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["actor","name"]} as const;
const S_epic_add_tag = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_add_tag__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_add_to_scene_from_asset = {"properties":{"asset_path":{"type":"string"},"name":{"type":"string"},"xform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"parent":{"type":"object","properties":{"refPath":{}}},"snap_to_ground":{"type":"boolean"}},"required":["asset_path","name","xform"]} as const;
const S_epic_add_to_scene_from_asset__1 = {"properties":{"assetPath":{"type":"string"},"name":{"type":"string"},"xform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"parent":{"type":"object","properties":{"refPath":{}}},"bSnapToGround":{"type":"boolean"},"bAllowSingletonDuplicate":{"type":"boolean"}},"required":["assetPath","name","xform"]} as const;
const S_epic_add_to_scene_from_class = {"properties":{"actor_type":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"xform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"parent":{"type":"object","properties":{"refPath":{}}},"snap_to_ground":{"type":"boolean"}},"required":["actor_type","name","xform"]} as const;
const S_epic_add_to_scene_from_class__1 = {"properties":{"actorType":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"xform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"parent":{"type":"object","properties":{"refPath":{}}},"bSnapToGround":{"type":"boolean"},"bAllowSingletonDuplicate":{"type":"boolean"}},"required":["actorType","name","xform"]} as const;
const S_epic_annotate_actor = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"prompt":{"type":"string"},"group":{"type":"integer"},"bShowLabel":{"type":"boolean"}},"required":["actor","prompt"]} as const;
const S_epic_are_annotated_actors_always_on_top = {"properties":{}} as const;
const S_epic_are_labels_visible = {"properties":{}} as const;
const S_epic_can_edit = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_commit_level_instance = {"properties":{"level_instance":{"type":"object","properties":{"refPath":{}}},"discard":{"type":"boolean"}},"required":["level_instance"]} as const;
const S_epic_commit_level_instance__1 = {"properties":{"levelInstance":{"type":"object","properties":{"refPath":{}}},"bDiscard":{"type":"boolean"}},"required":["levelInstance"]} as const;
const S_epic_create_data_layer_asset = {"properties":{"assetPath":{"type":"string"},"bIsRuntime":{"type":"boolean"}},"required":["assetPath"]} as const;
const S_epic_create_level = {"properties":{"levelPath":{"type":"string"},"bWorldPartition":{"type":"boolean"}},"required":["levelPath"]} as const;
const S_epic_create_level_instance = {"properties":{"level_path":{"type":"string"},"name":{"type":"string"},"xform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["level_path","name","xform"]} as const;
const S_epic_create_level_instance__1 = {"properties":{"levelPath":{"type":"string"},"name":{"type":"string"},"xform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["levelPath","name","xform"]} as const;
const S_epic_delete_folder = {"properties":{"folder_path":{"type":"string"}},"required":["folder_path"]} as const;
const S_epic_delete_folder__1 = {"properties":{"folderPath":{"type":"string"}},"required":["folderPath"]} as const;
const S_epic_diff = {"properties":{"oldActor":{"type":"object","properties":{"refPath":{}}},"newActor":{"type":"object","properties":{"refPath":{}}}},"required":["oldActor","newActor"]} as const;
const S_epic_diff__level_toolset = {"properties":{"oldScenePath":{"type":"string"},"newScenePath":{"type":"string"}},"required":["oldScenePath","newScenePath"]} as const;
const S_epic_edit_level_instance = {"properties":{"level_instance":{"type":"object","properties":{"refPath":{}}}},"required":["level_instance"]} as const;
const S_epic_edit_level_instance__1 = {"properties":{"levelInstance":{"type":"object","properties":{"refPath":{}}}},"required":["levelInstance"]} as const;
const S_epic_find_actors = {"properties":{"root":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"actor_type":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"},"bounds":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"isValid":{"type":"boolean"}}},"collision_channels":{"type":"array","items":{"type":"string","enum":["ObjectTypeQuery1","ObjectTypeQuery2","ObjectTypeQuery3","ObjectTypeQuery4","ObjectTypeQuery5","ObjectTypeQuery6","ObjectTypeQuery7","ObjectTypeQuery8","ObjectTypeQuery9","ObjectTypeQuery10","ObjectTypeQuery11","ObjectTypeQuery12","ObjectTypeQuery13","ObjectTypeQuery14","ObjectTypeQuery15","ObjectTypeQuery16","ObjectTypeQuery17","ObjectTypeQuery18","ObjectTypeQuery19","ObjectTypeQuery20","ObjectTypeQuery21","ObjectTypeQuery22","ObjectTypeQuery23","ObjectTypeQuery24","ObjectTypeQuery25","ObjectTypeQuery26","ObjectTypeQuery27","ObjectTypeQuery28","ObjectTypeQuery29","ObjectTypeQuery30","ObjectTypeQuery31","ObjectTypeQuery32","ObjectTypeQuery33","ObjectTypeQuery34","ObjectTypeQuery35","ObjectTypeQuery36","ObjectTypeQuery37","ObjectTypeQuery38","ObjectTypeQuery39","ObjectTypeQuery40","ObjectTypeQuery41","ObjectTypeQuery42","ObjectTypeQuery43","ObjectTypeQuery44","ObjectTypeQuery45","ObjectTypeQuery46","ObjectTypeQuery47","ObjectTypeQuery48","ObjectTypeQuery49","ObjectTypeQuery50","ObjectTypeQuery51","ObjectTypeQuery52","ObjectTypeQuery53","ObjectTypeQuery54","ObjectTypeQuery55","ObjectTypeQuery56","ObjectTypeQuery57","ObjectTypeQuery58","ObjectTypeQuery59","ObjectTypeQuery60","ObjectTypeQuery61","ObjectTypeQuery62","ObjectTypeQuery63","ObjectTypeQuery64"]}}},"required":["name","tag","collision_channels"]} as const;
const S_epic_find_actors__1 = {"properties":{"filter":{"type":"object","properties":{"root":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"actorType":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"},"bounds":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"isValid":{"type":"boolean"}}},"collisionChannels":{"type":"array","items":{"type":"string","enum":["ObjectTypeQuery1","ObjectTypeQuery2","ObjectTypeQuery3","ObjectTypeQuery4","ObjectTypeQuery5","ObjectTypeQuery6"]}},"dataLayer":{"type":"string"}}}}} as const;
const S_epic_find_annotation = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_gather_visible_annotations = {"properties":{}} as const;
const S_epic_get_active_group = {"properties":{}} as const;
const S_epic_get_actor_asset_path = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actor_bounds = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actor_bounds__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actor_transform = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actor_transform__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_actors_in_data_layer = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_actors_in_folder = {"properties":{"folder_path":{"type":"string"},"recursive":{"type":"boolean"}},"required":["folder_path"]} as const;
const S_epic_get_actors_in_folder__1 = {"properties":{"folderPath":{"type":"string"},"bRecursive":{"type":"boolean"},"levelInstance":{"type":"object","properties":{"guid":{"type":"string"},"instanceGuid":{"type":"string"},"parentInstanceGuid":{"type":"string"},"nativeClass":{"type":"object","properties":{"refPath":{}}},"class":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"label":{"type":"string"},"bounds":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"isValid":{"type":"boolean"}}},"runtimeGrid":{"type":"string"},"bIsSpatiallyLoaded":{"type":"boolean"},"bActorIsEditorOnly":{"type":"boolean"},"actorPackage":{"type":"string"},"actorPath":{"type":"string"},"folderPath":{"type":"string"},"dataLayerAssets":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}}}},"required":["folderPath"]} as const;
const S_epic_get_annotations = {"properties":{}} as const;
const S_epic_get_canonical_actor_state = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_capture_stage = {"properties":{}} as const;
const S_epic_get_collision_channels = {"properties":{}} as const;
const S_epic_get_collision_channels__1 = {"properties":{}} as const;
const S_epic_get_component_actor = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_component_actor__1 = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_component_transform = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_components = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component_type":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_components__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"componentType":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_current_level = {"properties":{}} as const;
const S_epic_get_current_level__1 = {"properties":{}} as const;
const S_epic_get_data_layers = {"properties":{}} as const;
const S_epic_get_folders = {"properties":{}} as const;
const S_epic_get_folders__1 = {"properties":{"name":{"type":"string"}}} as const;
const S_epic_get_label = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_label__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_parent_component = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_parent_component__1 = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_parent_level_instance = {"properties":{"actor":{"type":"object","properties":{"guid":{"type":"string"},"instanceGuid":{"type":"string"},"parentInstanceGuid":{"type":"string"},"nativeClass":{"type":"object","properties":{"refPath":{}}},"class":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"label":{"type":"string"},"bounds":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"isValid":{"type":"boolean"}}},"runtimeGrid":{"type":"string"},"bIsSpatiallyLoaded":{"type":"boolean"},"bActorIsEditorOnly":{"type":"boolean"},"actorPackage":{"type":"string"},"actorPath":{"type":"string"},"folderPath":{"type":"string"},"dataLayerAssets":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}}}},"required":["actor"]} as const;
const S_epic_get_root_component = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_root_component__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_tags = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_tags__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_visible_groups = {"properties":{}} as const;
const S_epic_has_tag = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_has_tag__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_is_actor_hidden = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_is_checked_out = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_is_level_instance_loaded = {"properties":{"levelInstance":{"type":"object","properties":{"refPath":{}}}},"required":["levelInstance"]} as const;
const S_epic_is_overlay_visible = {"properties":{}} as const;
const S_epic_isolate_annotation_group = {"properties":{"group":{"type":"integer"}},"required":["group"]} as const;
const S_epic_isolate_group = {"properties":{"group":{"type":"integer"}},"required":["group"]} as const;
const S_epic_list_annotations = {"properties":{}} as const;
const S_epic_load_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"guid":{"type":"string"},"instanceGuid":{"type":"string"},"parentInstanceGuid":{"type":"string"},"nativeClass":{"type":"object","properties":{"refPath":{}}},"class":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"label":{"type":"string"},"bounds":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"isValid":{"type":"boolean"}}},"runtimeGrid":{"type":"string"},"bIsSpatiallyLoaded":{"type":"boolean"},"bActorIsEditorOnly":{"type":"boolean"},"actorPackage":{"type":"string"},"actorPath":{"type":"string"},"folderPath":{"type":"string"},"dataLayerAssets":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}}}}},"required":["actors"]} as const;
const S_epic_load_level = {"properties":{"level_path":{"type":"string"}},"required":["level_path"]} as const;
const S_epic_load_level__1 = {"properties":{"levelPath":{"type":"string"},"unsavedChangesPolicy":{"type":"string","enum":["Fail","Save","Discard"]}},"required":["levelPath"]} as const;
const S_epic_load_level_instance = {"properties":{"levelInstance":{"type":"object","properties":{"refPath":{}}}},"required":["levelInstance"]} as const;
const S_epic_look_at = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"target":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["actor","target"]} as const;
const S_epic_look_at__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"target":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["actor","target"]} as const;
const S_epic_merge_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"output_path":{"type":"string"},"name":{"type":"string"},"destroy_source_actors":{"type":"boolean"}},"required":["actors","output_path","name"]} as const;
const S_epic_merge_actors__1 = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"outputPath":{"type":"string"},"name":{"type":"string"},"bDestroySourceActors":{"type":"boolean"}},"required":["actors","outputPath"]} as const;
const S_epic_persist_annotations = {"properties":{}} as const;
const S_epic_place_annotation = {"properties":{"prompt":{"type":"string"},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"group":{"type":"integer"},"shape":{"type":"string","enum":["Cube","Sphere","Cylinder","Cone","Plane"]}},"required":["prompt","transform"]} as const;
const S_epic_place_annotation__unreal_canvas_spatial_annotation_toolset = {"properties":{"prompt":{"type":"string"},"group":{"type":"integer"},"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"number"}},"required":["prompt"]} as const;
const S_epic_remove_actors_from_data_layer = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"assetPath":{"type":"string"}},"required":["actors","assetPath"]} as const;
const S_epic_remove_all_annotations = {"properties":{}} as const;
const S_epic_remove_annotation = {"properties":{"annotation":{"type":"object","properties":{"refPath":{}}}},"required":["annotation"]} as const;
const S_epic_remove_annotation__unreal_canvas_spatial_annotation_toolset = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_remove_annotations_from_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["actors"]} as const;
const S_epic_remove_annotations_from_selection = {"properties":{}} as const;
const S_epic_remove_component = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_remove_component__1 = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_remove_from_scene = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_remove_from_scene__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_remove_tag = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_remove_tag__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"tag":{"type":"string"}},"required":["actor","tag"]} as const;
const S_epic_rename_folder = {"properties":{"old_path":{"type":"string"},"new_path":{"type":"string"}},"required":["old_path","new_path"]} as const;
const S_epic_rename_folder__1 = {"properties":{"oldPath":{"type":"string"},"newPath":{"type":"string"}},"required":["oldPath","newPath"]} as const;
const S_epic_replace_actors_with_asset = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"assetPath":{"type":"string"},"bCopySourceProperties":{"type":"boolean"}},"required":["actors","assetPath"]} as const;
const S_epic_replace_actors_with_class = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"actorType":{"type":"object","properties":{"refPath":{}}}},"required":["actors","actorType"]} as const;
const S_epic_save_actor = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_save_actor__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_set_active_group = {"properties":{"group":{"type":"integer"}},"required":["group"]} as const;
const S_epic_set_actor_folder = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"folder_path":{"type":"string"}},"required":["actor","folder_path"]} as const;
const S_epic_set_actor_folder__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"folderPath":{"type":"string"}},"required":["actor","folderPath"]} as const;
const S_epic_set_actor_hidden = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"bHidden":{"type":"boolean"}},"required":["actor","bHidden"]} as const;
const S_epic_set_actor_transform = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"xform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"worldspace":{"type":"boolean"}},"required":["actor","xform"]} as const;
const S_epic_set_actor_transform__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"bWorldSpace":{"type":"boolean"}},"required":["actor"]} as const;
const S_epic_set_annotated_actors_always_on_top = {"properties":{"bAlwaysOnTop":{"type":"boolean"}},"required":["bAlwaysOnTop"]} as const;
const S_epic_set_annotation_group = {"properties":{"annotation":{"type":"object","properties":{"refPath":{}}},"group":{"type":"integer"}},"required":["annotation","group"]} as const;
const S_epic_set_annotation_prompt = {"properties":{"annotation":{"type":"object","properties":{"refPath":{}}},"prompt":{"type":"string"}},"required":["annotation","prompt"]} as const;
const S_epic_set_annotation_shape = {"properties":{"annotation":{"type":"object","properties":{"refPath":{}}},"shape":{"type":"string","enum":["Cube","Sphere","Cylinder","Cone","Plane"]}},"required":["annotation","shape"]} as const;
const S_epic_set_capture_stage = {"properties":{"stage":{"type":"string","enum":["PrePostProcess","PostTonemap","PostFinal"]}},"required":["stage"]} as const;
const S_epic_set_component_transform = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"bWorldSpace":{"type":"boolean"}},"required":["component"]} as const;
const S_epic_set_label = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"label":{"type":"string"}},"required":["actor","label"]} as const;
const S_epic_set_label__1 = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"label":{"type":"string"}},"required":["actor","label"]} as const;
const S_epic_set_labels_visible = {"properties":{"bVisible":{"type":"boolean"}},"required":["bVisible"]} as const;
const S_epic_set_overlay_visible = {"properties":{"bVisible":{"type":"boolean"}},"required":["bVisible"]} as const;
const S_epic_set_parent_component = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_set_parent_component__1 = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_set_persist_new_annotations = {"properties":{"bPersist":{"type":"boolean"}},"required":["bPersist"]} as const;
const S_epic_set_visible_annotation_groups = {"properties":{"groups":{"type":"array","items":{"type":"integer"}}},"required":["groups"]} as const;
const S_epic_set_visible_groups = {"properties":{"groups":{"type":"array","items":{"type":"integer"}}},"required":["groups"]} as const;
const S_epic_should_persist_new_annotations = {"properties":{}} as const;
const S_epic_show_all_annotation_groups = {"properties":{}} as const;
const S_epic_show_all_groups = {"properties":{}} as const;
const S_epic_trace_world = {"properties":{"start":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"end":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["start","end"]} as const;
const S_epic_trace_world__1 = {"properties":{"start":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"end":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["start","end"]} as const;
const S_epic_unload_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"guid":{"type":"string"},"instanceGuid":{"type":"string"},"parentInstanceGuid":{"type":"string"},"nativeClass":{"type":"object","properties":{"refPath":{}}},"class":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"label":{"type":"string"},"bounds":{"type":"object","properties":{"min":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"max":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"isValid":{"type":"boolean"}}},"runtimeGrid":{"type":"string"},"bIsSpatiallyLoaded":{"type":"boolean"},"bActorIsEditorOnly":{"type":"boolean"},"actorPackage":{"type":"string"},"actorPath":{"type":"string"},"folderPath":{"type":"string"},"dataLayerAssets":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}}}}},"required":["actors"]} as const;
const S_epic_unload_level_instance = {"properties":{"levelInstance":{"type":"object","properties":{"refPath":{}}}},"required":["levelInstance"]} as const;
const S_epic_update_annotation = {"properties":{"id":{"type":"string"},"prompt":{"type":"string"},"group":{"type":"integer"}},"required":["id"]} as const;

/** 99 wrapped engine tools routed to the `level` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_actors_to_data_layer: {
    epicSchema: S_epic_add_actors_to_data_layer,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.AddActorsToDataLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Adds actors to a data layer, creating its instance in the current world if it has none yet. Actors already in the layer are left unchanged. Available on UE 5.9; absent from UE 5.8. Params: actors, assetPath", "epic_call_tool"),
  },
  epic_add_component: {
    epicSchema: S_epic_add_component,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.add_component", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.AddComponent", engines: ["5.9"], schema: S_epic_add_component__1, rename: {"component_type":"componentType"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Adds a component to an actor instance or blueprint. Params: owner, component_type, name. On UE 5.9 it calls EditorToolset.ActorToolset.AddComponent (Params: owner, componentType, name).", "epic_call_tool"),
  },
  epic_add_cone: {
    epicSchema: S_epic_add_cone,
    epicTool: { toolset: "editor_toolset.toolsets.primitive.PrimitiveTools", name: "editor_toolset.toolsets.primitive.PrimitiveTools.add_cone", engines: ["5.8"], successors: [{ toolset: "EditorToolset.PrimitiveToolset", name: "EditorToolset.PrimitiveToolset.AddCone", engines: ["5.9"], schema: S_epic_add_cone__1, rename: {"local_transform":"localTransform"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a cone-shaped StaticMeshComponent to an actor. Params: actor, name, radius?, height?, local_transform?. On UE 5.9 it calls EditorToolset.PrimitiveToolset.AddCone (Params: actor, name, radius?, height?, localTransform?).", "epic_call_tool"),
  },
  epic_add_cube: {
    epicSchema: S_epic_add_cube,
    epicTool: { toolset: "editor_toolset.toolsets.primitive.PrimitiveTools", name: "editor_toolset.toolsets.primitive.PrimitiveTools.add_cube", engines: ["5.8"], successors: [{ toolset: "EditorToolset.PrimitiveToolset", name: "EditorToolset.PrimitiveToolset.AddCube", engines: ["5.9"], schema: S_epic_add_cube__1, rename: {"local_transform":"localTransform"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a cube-shaped StaticMeshComponent to an actor. Params: actor, name, dimensions?, local_transform?. On UE 5.9 it calls EditorToolset.PrimitiveToolset.AddCube (Params: actor, name, dimensions?, localTransform?).", "epic_call_tool"),
  },
  epic_add_cylinder: {
    epicSchema: S_epic_add_cylinder,
    epicTool: { toolset: "editor_toolset.toolsets.primitive.PrimitiveTools", name: "editor_toolset.toolsets.primitive.PrimitiveTools.add_cylinder", engines: ["5.8"], successors: [{ toolset: "EditorToolset.PrimitiveToolset", name: "EditorToolset.PrimitiveToolset.AddCylinder", engines: ["5.9"], schema: S_epic_add_cylinder__1, rename: {"local_transform":"localTransform"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a cylinder-shaped StaticMeshComponent to an actor. Params: actor, name, radius?, height?, local_transform?. On UE 5.9 it calls EditorToolset.PrimitiveToolset.AddCylinder (Params: actor, name, radius?, height?, localTransform?).", "epic_call_tool"),
  },
  epic_add_sphere: {
    epicSchema: S_epic_add_sphere,
    epicTool: { toolset: "editor_toolset.toolsets.primitive.PrimitiveTools", name: "editor_toolset.toolsets.primitive.PrimitiveTools.add_sphere", engines: ["5.8"], successors: [{ toolset: "EditorToolset.PrimitiveToolset", name: "EditorToolset.PrimitiveToolset.AddSphere", engines: ["5.9"], schema: S_epic_add_sphere__1, rename: {"local_transform":"localTransform"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.primitive.PrimitiveTools] Adds a sphere-shaped StaticMeshComponent to an actor. Params: actor, name, radius?, local_transform?. On UE 5.9 it calls EditorToolset.PrimitiveToolset.AddSphere (Params: actor, name, radius?, localTransform?).", "epic_call_tool"),
  },
  epic_add_tag: {
    epicSchema: S_epic_add_tag,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.add_tag", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.AddTag", engines: ["5.9"], schema: S_epic_add_tag__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Adds a tag to an actor. Params: actor, tag. On UE 5.9 it calls EditorToolset.ActorToolset.AddTag (Params: actor, tag).", "epic_call_tool"),
  },
  epic_add_to_scene_from_asset: {
    epicSchema: S_epic_add_to_scene_from_asset,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.AddToSceneFromAsset", engines: ["5.9"], schema: S_epic_add_to_scene_from_asset__1, rename: {"asset_path":"assetPath","snap_to_ground":"bSnapToGround"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Creates a new actor in the scene from an asset. Params: asset_path, name, xform, parent?, snap_to_ground?. On UE 5.9 it calls EditorToolset.LevelToolset.AddToSceneFromAsset (Params: assetPath, name, xform, parent?, bSnapToGround?, bAllowSingletonDuplicate?).", "epic_call_tool"),
  },
  epic_add_to_scene_from_class: {
    epicSchema: S_epic_add_to_scene_from_class,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.AddToSceneFromClass", engines: ["5.9"], schema: S_epic_add_to_scene_from_class__1, rename: {"actor_type":"actorType","snap_to_ground":"bSnapToGround"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Creates a new instance of the specified object at the specified transform. Params: actor_type, name, xform, parent?, snap_to_ground?. On UE 5.9 it calls EditorToolset.LevelToolset.AddToSceneFromClass (Params: actorType, name, xform, parent?, bSnapToGround?, bAllowSingletonDuplicate?).", "epic_call_tool"),
  },
  epic_annotate_actor: {
    epicSchema: S_epic_annotate_actor,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.AnnotateActor", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Annotate a scene actor, so its meshes are marked in the matte. Annotating it again replaces the prompt. Available on UE 5.9; absent from UE 5.8. Params: actor, prompt, group?, bShowLabel?", "epic_call_tool"),
  },
  epic_are_annotated_actors_always_on_top: {
    epicSchema: S_epic_are_annotated_actors_always_on_top,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.AreAnnotatedActorsAlwaysOnTop", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Whether annotated scene actors draw through occluders, which also makes the matte ignore occlusion. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_are_labels_visible: {
    epicSchema: S_epic_are_labels_visible,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.AreLabelsVisible", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Whether the viewport draws the prompt labels. Each annotation can also hide its own (bShowLabel). Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_can_edit: {
    epicSchema: S_epic_can_edit,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.can_edit", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Checks whether an actor can be edited. Available on UE 5.8; absent from UE 5.9. Params: actor", "epic_call_tool"),
  },
  epic_commit_level_instance: {
    epicSchema: S_epic_commit_level_instance,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.commit_level_instance", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.CommitLevelInstance", engines: ["5.9"], schema: S_epic_commit_level_instance__1, rename: {"level_instance":"levelInstance","discard":"bDiscard"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Saves or discards edits to a level instance and exits edit mode. Params: level_instance, discard?. On UE 5.9 it calls EditorToolset.LevelToolset.CommitLevelInstance (Params: levelInstance, bDiscard?).", "epic_call_tool"),
  },
  epic_create_data_layer_asset: {
    epicSchema: S_epic_create_data_layer_asset,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.CreateDataLayerAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Creates a new Data Layer asset in the content browser. A Data Layer asset is reusable across levels; assign it to actors in the current world by adding them to the data layer. Available on UE 5.9; absent from UE 5.8. Params: assetPath, bIsRuntime?", "epic_call_tool"),
  },
  epic_create_level: {
    epicSchema: S_epic_create_level,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.CreateLevel", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Creates a new empty level asset and opens it in the editor. Available on UE 5.9; absent from UE 5.8. Params: levelPath, bWorldPartition?", "epic_call_tool"),
  },
  epic_create_level_instance: {
    epicSchema: S_epic_create_level_instance,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.create_level_instance", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.CreateLevelInstance", engines: ["5.9"], schema: S_epic_create_level_instance__1, rename: {"level_path":"levelPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Creates a Level Instance actor in the scene referencing an existing level asset. Params: level_path, name, xform, parent?. On UE 5.9 it calls EditorToolset.LevelToolset.CreateLevelInstance (Params: levelPath, name, xform, parent?).", "epic_call_tool"),
  },
  epic_delete_folder: {
    epicSchema: S_epic_delete_folder,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.delete_folder", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.DeleteFolder", engines: ["5.9"], schema: S_epic_delete_folder__1, rename: {"folder_path":"folderPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Deletes a folder from the outliner. Actors directly in the folder are moved to the parent folder. Sub-folders and their actors are preserved by re-rooting them under the parent. For example, deleting 'Lighting' with a sub-folder 'Lighting/Spotlights' leaves 'Spotlights' intact under the parent. Params: folder_path. On UE 5.9 it calls EditorToolset.LevelToolset.DeleteFolder (Params: folderPath).", "epic_call_tool"),
  },
  epic_diff: {
    epicSchema: S_epic_diff,
    epicTool: { toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.ActorToolset] Returns a human-readable text diff between two actors. Compares the actors' classes, top-level properties, and per-component class and top-level properties. Available on UE 5.9; absent from UE 5.8. Params: oldActor, newActor", "epic_call_tool"),
  },
  epic_diff__level_toolset: {
    epicSchema: S_epic_diff__level_toolset,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns a human-readable text diff between two scene asset packages, each expected to contain a single external actor. Available on UE 5.9; absent from UE 5.8. Params: oldScenePath, newScenePath", "epic_call_tool"),
  },
  epic_edit_level_instance: {
    epicSchema: S_epic_edit_level_instance,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.edit_level_instance", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.EditLevelInstance", engines: ["5.9"], schema: S_epic_edit_level_instance__1, rename: {"level_instance":"levelInstance"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Opens a level instance for editing. While in edit mode, scene tools such as add_to_scene_from_class and remove_from_scene operate within the level instance's sub-level. Only one level instance can be in edit mode at a time. Call commit_level_instance when done to save or discard changes. Params: level_instance. On UE 5.9 it calls EditorToolset.LevelToolset.EditLevelInstance (Params: levelInstance).", "epic_call_tool"),
  },
  epic_find_actors: {
    epicSchema: S_epic_find_actors,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.find_actors", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.FindActors", engines: ["5.9"], schema: S_epic_find_actors__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Searches the scene for actors that match specific criteria. Params: root?, name, actor_type?, tag, bounds?, collision_channels. On UE 5.9 it calls EditorToolset.LevelToolset.FindActors (Params: filter?).", "epic_call_tool"),
  },
  epic_find_annotation: {
    epicSchema: S_epic_find_annotation,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.FindAnnotation", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Find an annotation by the id an MCP tool or GetAnnotations reported. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_gather_visible_annotations: {
    epicSchema: S_epic_gather_visible_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.GatherVisibleAnnotations", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Returns the annotations with their on-screen rects, as the JSON the image pipeline consumes. Each entry carries a `visible` flag that is false when the annotation is out of frustum OR its colour group is hidden - so an isolate/show call shows up there, not by dropping entries. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_active_group: {
    epicSchema: S_epic_get_active_group,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetActiveGroup", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] The group, 1-8, that new annotations join when no group is given. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_actor_asset_path: {
    epicSchema: S_epic_get_actor_asset_path,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetActorAssetPath", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns the content path that identifies this actor on disk. Available on UE 5.9; absent from UE 5.8. Params: actor", "epic_call_tool"),
  },
  epic_get_actor_bounds: {
    epicSchema: S_epic_get_actor_bounds,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_actor_bounds", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetActorBounds", engines: ["5.9"], schema: S_epic_get_actor_bounds__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the bounding box of an actor. Params: actor. On UE 5.9 it calls EditorToolset.ActorToolset.GetActorBounds (Params: actor).", "epic_call_tool"),
  },
  epic_get_actor_transform: {
    epicSchema: S_epic_get_actor_transform,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_actor_transform", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetActorTransform", engines: ["5.9"], schema: S_epic_get_actor_transform__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the position, rotation, and scale of an actor. Params: actor. On UE 5.9 it calls EditorToolset.ActorToolset.GetActorTransform (Params: actor).", "epic_call_tool"),
  },
  epic_get_actors_in_data_layer: {
    epicSchema: S_epic_get_actors_in_data_layer,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetActorsInDataLayer", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns descriptors for the loaded actors assigned to a data layer. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_get_actors_in_folder: {
    epicSchema: S_epic_get_actors_in_folder,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.get_actors_in_folder", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetActorsInFolder", engines: ["5.9"], schema: S_epic_get_actors_in_folder__1, rename: {"folder_path":"folderPath","recursive":"bRecursive"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Returns the actors in the specified outliner folder. Params: folder_path, recursive?. On UE 5.9 it calls EditorToolset.LevelToolset.GetActorsInFolder (Params: folderPath, bRecursive?, levelInstance?).", "epic_call_tool"),
  },
  epic_get_annotations: {
    epicSchema: S_epic_get_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetAnnotations", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] List every annotation in the editor level, proxies and annotated actors alike. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_canonical_actor_state: {
    epicSchema: S_epic_get_canonical_actor_state,
    epicTool: { toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetCanonicalActorState", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.ActorToolset] Returns a JSON-safe canonical form of an actor (or a Blueprint's CDO) for diffing. Captures the actor's class, its top-level properties, and each component's class and top-level properties. References to the actor itself (path or label) are normalized to \"<actor>\", and for Blueprint CDOs the generated class path is normalized to \"<class>\" and the reported class becomes the parent native class, so two CDOs from structurally identical Blueprints produce identical output despite their unique generated-class paths. Available on UE 5.9; absent from UE 5.8. Params: actor", "epic_call_tool"),
  },
  epic_get_capture_stage: {
    epicSchema: S_epic_get_capture_stage,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetCaptureStage", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Where in the frame the viewport capture reads colour. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_collision_channels: {
    epicSchema: S_epic_get_collision_channels,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.get_collision_channels", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetCollisionChannels", engines: ["5.9"], schema: S_epic_get_collision_channels__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Returns all available collision channels for use with find_actors. Params: none. On UE 5.9 it calls EditorToolset.LevelToolset.GetCollisionChannels (Params: none).", "epic_call_tool"),
  },
  epic_get_component_actor: {
    epicSchema: S_epic_get_component_actor,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_component_actor", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetComponentActor", engines: ["5.9"], schema: S_epic_get_component_actor__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the actor that owns the specified component. Params: component. On UE 5.9 it calls EditorToolset.ActorToolset.GetComponentActor (Params: component).", "epic_call_tool"),
  },
  epic_get_component_transform: {
    epicSchema: S_epic_get_component_transform,
    epicTool: { toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetComponentTransform", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.ActorToolset] Returns the position, rotation, and scale of a scene component. Available on UE 5.9; absent from UE 5.8. Params: component", "epic_call_tool"),
  },
  epic_get_components: {
    epicSchema: S_epic_get_components,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_components", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetComponents", engines: ["5.9"], schema: S_epic_get_components__1, rename: {"component_type":"componentType"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the components that an actor contains. Params: actor, component_type?. On UE 5.9 it calls EditorToolset.ActorToolset.GetComponents (Params: actor, componentType?).", "epic_call_tool"),
  },
  epic_get_current_level: {
    epicSchema: S_epic_get_current_level,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.get_current_level", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetCurrentLevel", engines: ["5.9"], schema: S_epic_get_current_level__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Returns the path to the current level asset. Params: none. On UE 5.9 it calls EditorToolset.LevelToolset.GetCurrentLevel (Params: none).", "epic_call_tool"),
  },
  epic_get_data_layers: {
    epicSchema: S_epic_get_data_layers,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetDataLayers", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns the data layer instances in the current world. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_folders: {
    epicSchema: S_epic_get_folders,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.get_folders", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetFolders", engines: ["5.9"], schema: S_epic_get_folders__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Returns all folder paths currently in use in the outliner. Includes all intermediate parent paths. For example, if an actor is assigned to 'Lighting/Spotlights', both 'Lighting' and 'Lighting/Spotlights' are returned. Params: none. On UE 5.9 it calls EditorToolset.LevelToolset.GetFolders (Params: name?).", "epic_call_tool"),
  },
  epic_get_label: {
    epicSchema: S_epic_get_label,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_label", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetLabel", engines: ["5.9"], schema: S_epic_get_label__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the actor's human friendly name as it appears in the editor. Params: actor. On UE 5.9 it calls EditorToolset.ActorToolset.GetLabel (Params: actor).", "epic_call_tool"),
  },
  epic_get_parent_component: {
    epicSchema: S_epic_get_parent_component,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_parent_component", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetParentComponent", engines: ["5.9"], schema: S_epic_get_parent_component__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the parent component that this component is attached to, if any. Params: component. On UE 5.9 it calls EditorToolset.ActorToolset.GetParentComponent (Params: component).", "epic_call_tool"),
  },
  epic_get_parent_level_instance: {
    epicSchema: S_epic_get_parent_level_instance,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.GetParentLevelInstance", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns the level instance that directly contains an actor, loaded or not. Available on UE 5.9; absent from UE 5.8. Params: actor", "epic_call_tool"),
  },
  epic_get_root_component: {
    epicSchema: S_epic_get_root_component,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_root_component", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetRootComponent", engines: ["5.9"], schema: S_epic_get_root_component__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the root component of an actor, if any. Params: actor. On UE 5.9 it calls EditorToolset.ActorToolset.GetRootComponent (Params: actor).", "epic_call_tool"),
  },
  epic_get_tags: {
    epicSchema: S_epic_get_tags,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.get_tags", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.GetTags", engines: ["5.9"], schema: S_epic_get_tags__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns the list of tags on an actor. Params: actor. On UE 5.9 it calls EditorToolset.ActorToolset.GetTags (Params: actor).", "epic_call_tool"),
  },
  epic_get_visible_groups: {
    epicSchema: S_epic_get_visible_groups,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.GetVisibleGroups", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] The colour groups shown in the viewport overlay and the matte, in ascending order. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_has_tag: {
    epicSchema: S_epic_has_tag,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.has_tag", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.HasTag", engines: ["5.9"], schema: S_epic_has_tag__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.actor.ActorTools] Returns whether an actor has a specific tag. Params: actor, tag. On UE 5.9 it calls EditorToolset.ActorToolset.HasTag (Params: actor, tag).", "epic_call_tool"),
  },
  epic_is_actor_hidden: {
    epicSchema: S_epic_is_actor_hidden,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.IsActorHidden", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns whether the actor is hidden in the editor viewport and outliner. Available on UE 5.9; absent from UE 5.8. Params: actor", "epic_call_tool"),
  },
  epic_is_checked_out: {
    epicSchema: S_epic_is_checked_out,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.is_checked_out", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.scene.SceneTools] Checks whether an actor is checked out by the current user. Available on UE 5.8; absent from UE 5.9. Params: actor", "epic_call_tool"),
  },
  epic_is_level_instance_loaded: {
    epicSchema: S_epic_is_level_instance_loaded,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.IsLevelInstanceLoaded", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.LevelToolset] Returns whether a level instance's level is loaded. Level instances created by other tools, or that came with a loaded level, finish loading some time after those tools return. Available on UE 5.9; absent from UE 5.8. Params: levelInstance", "epic_call_tool"),
  },
  epic_is_overlay_visible: {
    epicSchema: S_epic_is_overlay_visible,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.IsOverlayVisible", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Whether the viewport draws the coloured annotation overlay. The matte and passes do not depend on it. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_isolate_annotation_group: {
    epicSchema: S_epic_isolate_annotation_group,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.IsolateAnnotationGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Shows a single colour group and hides the rest, so the annotation guide marks one target. Use this to work through annotations one group at a time; ShowAllAnnotationGroups restores. Also makes Group the active group, so a later PlaceAnnotation without an explicit Group lands in the group you just soloed rather than in a hidden one. Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_isolate_group: {
    epicSchema: S_epic_isolate_group,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.IsolateGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Show only this group, 1-8, and make it the active group. Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_list_annotations: {
    epicSchema: S_epic_list_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.ListAnnotations", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Lists every 3D spatial annotation in the level, with its id, objectId, prompt, colour, colour group (1-8), world location and the actor it is bound to. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_load_actors: {
    epicSchema: S_epic_load_actors,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.LoadActors", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Loads the actors described by the given descriptors into the editor and returns them. Available on UE 5.9; absent from UE 5.8. Params: actors", "epic_call_tool"),
  },
  epic_load_level: {
    epicSchema: S_epic_load_level,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.load_level", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.LoadLevel", engines: ["5.9"], schema: S_epic_load_level__1, rename: {"level_path":"levelPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Loads a level in the editor. Params: level_path. On UE 5.9 it calls EditorToolset.LevelToolset.LoadLevel (Params: levelPath, unsavedChangesPolicy?).", "epic_call_tool"),
  },
  epic_load_level_instance: {
    epicSchema: S_epic_load_level_instance,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.LoadLevelInstance", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Loads a level instance's level, making its actors available as loaded actors. Available on UE 5.9; absent from UE 5.8. Params: levelInstance", "epic_call_tool"),
  },
  epic_look_at: {
    epicSchema: S_epic_look_at,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.look_at", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.LookAt", engines: ["5.9"], schema: S_epic_look_at__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Rotates an actor so its forward vector points at a world-space position. Params: actor, target. On UE 5.9 it calls EditorToolset.ActorToolset.LookAt (Params: actor, target).", "epic_call_tool"),
  },
  epic_merge_actors: {
    epicSchema: S_epic_merge_actors,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.merge_actors", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.MergeActors", engines: ["5.9"], schema: S_epic_merge_actors__1, rename: {"output_path":"outputPath","destroy_source_actors":"bDestroySourceActors"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Merges multiple StaticMesh actors into a single mesh asset and actor. Params: actors, output_path, name, destroy_source_actors?. On UE 5.9 it calls EditorToolset.LevelToolset.MergeActors (Params: actors, outputPath, name?, bDestroySourceActors?).", "epic_call_tool"),
  },
  epic_persist_annotations: {
    epicSchema: S_epic_persist_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.PersistAnnotations", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Save every scratch annotation with the level from now on. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_place_annotation: {
    epicSchema: S_epic_place_annotation,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.PlaceAnnotation", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Place a proxy region carrying a prompt. Available on UE 5.9; absent from UE 5.8. Params: prompt, transform, group?, shape?", "epic_call_tool"),
  },
  epic_place_annotation__unreal_canvas_spatial_annotation_toolset: {
    epicSchema: S_epic_place_annotation__unreal_canvas_spatial_annotation_toolset,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.PlaceAnnotation", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Places a new 3D annotation proxy box carrying a spatial prompt. Available on UE 5.9; absent from UE 5.8. Params: prompt, group?, location?, rotation?, scale?", "epic_call_tool"),
  },
  epic_remove_actors_from_data_layer: {
    epicSchema: S_epic_remove_actors_from_data_layer,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.RemoveActorsFromDataLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Removes actors from a data layer. Actors not in the layer are left unchanged. Available on UE 5.9; absent from UE 5.8. Params: actors, assetPath", "epic_call_tool"),
  },
  epic_remove_all_annotations: {
    epicSchema: S_epic_remove_all_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.RemoveAllAnnotations", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] RemoveAllAnnotations Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_remove_annotation: {
    epicSchema: S_epic_remove_annotation,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.RemoveAnnotation", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Remove an annotation. A proxy region is deleted; a scene actor stays and loses its matte marking. Available on UE 5.9; absent from UE 5.8. Params: annotation", "epic_call_tool"),
  },
  epic_remove_annotation__unreal_canvas_spatial_annotation_toolset: {
    epicSchema: S_epic_remove_annotation__unreal_canvas_spatial_annotation_toolset,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.RemoveAnnotation", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Deletes one annotation. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_remove_annotations_from_actors: {
    epicSchema: S_epic_remove_annotations_from_actors,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.RemoveAnnotationsFromActors", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Remove every annotation the actors carry: the inverse of annotating them. Available on UE 5.9; absent from UE 5.8. Params: actors", "epic_call_tool"),
  },
  epic_remove_annotations_from_selection: {
    epicSchema: S_epic_remove_annotations_from_selection,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.RemoveAnnotationsFromSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Deletes every annotation on the currently selected actors - the inverse of annotating a selection. A custom-placed one takes its proxy actor with it. Use RemoveAnnotation for an id. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_remove_component: {
    epicSchema: S_epic_remove_component,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.remove_component", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.RemoveComponent", engines: ["5.9"], schema: S_epic_remove_component__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Removes a component from an actor instance or blueprint. Params: component. On UE 5.9 it calls EditorToolset.ActorToolset.RemoveComponent (Params: component).", "epic_call_tool"),
  },
  epic_remove_from_scene: {
    epicSchema: S_epic_remove_from_scene,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.remove_from_scene", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.RemoveFromScene", engines: ["5.9"], schema: S_epic_remove_from_scene__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Deletes an actor from the scene. Params: actor. On UE 5.9 it calls EditorToolset.LevelToolset.RemoveFromScene (Params: actor).", "epic_call_tool"),
  },
  epic_remove_tag: {
    epicSchema: S_epic_remove_tag,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.remove_tag", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.RemoveTag", engines: ["5.9"], schema: S_epic_remove_tag__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Removes a tag from an actor. Params: actor, tag. On UE 5.9 it calls EditorToolset.ActorToolset.RemoveTag (Params: actor, tag).", "epic_call_tool"),
  },
  epic_rename_folder: {
    epicSchema: S_epic_rename_folder,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.rename_folder", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.RenameFolder", engines: ["5.9"], schema: S_epic_rename_folder__1, rename: {"old_path":"oldPath","new_path":"newPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Renames a folder in the outliner. Updates the folder path for all actors in the folder and any sub-folders. For example, renaming 'Lighting' to 'Lights' also updates actors in 'Lighting/Spotlights' to 'Lights/Spotlights'. If the new path already exists then the affected actors will be merged into it. Params: old_path, new_path. On UE 5.9 it calls EditorToolset.LevelToolset.RenameFolder (Params: oldPath, newPath).", "epic_call_tool"),
  },
  epic_replace_actors_with_asset: {
    epicSchema: S_epic_replace_actors_with_asset,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.ReplaceActorsWithAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Replaces actors in the scene with new actors created from an asset (e.g. a static mesh, Blueprint, or particle system), matching the editor's \"Replace Selected Actors With...\" command. Preserves each actor's world transform, World Partition identity (actor guid and external package), and layers, and reattaches its former parent and children. Does not preserve the outliner folder path, Actor tags, or data layer assignments. Available on UE 5.9; absent from UE 5.8. Params: actors, assetPath, bCopySourceProperties?", "epic_call_tool"),
  },
  epic_replace_actors_with_class: {
    epicSchema: S_epic_replace_actors_with_class,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.ReplaceActorsWithClass", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Replaces actors in the scene with new instances of a native Actor class, matching the editor's \"Replace Selected Actors With...\" command. Preserves each actor's world transform, World Partition identity (actor guid and external package), and layers, and reattaches its former parent and children. Does not preserve the outliner folder path, Actor tags, or data layer assignments. Available on UE 5.9; absent from UE 5.8. Params: actors, actorType", "epic_call_tool"),
  },
  epic_save_actor: {
    epicSchema: S_epic_save_actor,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.save_actor", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.SaveActor", engines: ["5.9"], schema: S_epic_save_actor__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Saves the actor to disk. Params: actor. On UE 5.9 it calls EditorToolset.LevelToolset.SaveActor (Params: actor).", "epic_call_tool"),
  },
  epic_set_active_group: {
    epicSchema: S_epic_set_active_group,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetActiveGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Set the group, 1-8, that new annotations join. It must be visible, or they would not show. Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_set_actor_folder: {
    epicSchema: S_epic_set_actor_folder,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.set_actor_folder", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.SetActorFolder", engines: ["5.9"], schema: S_epic_set_actor_folder__1, rename: {"folder_path":"folderPath"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Assigns an actor to the specified folder in the outliner. Creates the folder implicitly if it does not already exist. Pass an empty string to move the actor to the root of the outliner. Params: actor, folder_path. On UE 5.9 it calls EditorToolset.LevelToolset.SetActorFolder (Params: actor, folderPath).", "epic_call_tool"),
  },
  epic_set_actor_hidden: {
    epicSchema: S_epic_set_actor_hidden,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.SetActorHidden", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Shows or hides the actor in the editor viewport and outliner. This is the same transient, editor-only visibility toggled by the outliner's eye icon; it does not affect gameplay visibility and does not dirty the level. Available on UE 5.9; absent from UE 5.8. Params: actor, bHidden", "epic_call_tool"),
  },
  epic_set_actor_transform: {
    epicSchema: S_epic_set_actor_transform,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.set_actor_transform", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.SetActorTransform", engines: ["5.9"], schema: S_epic_set_actor_transform__1, rename: {"worldspace":"bWorldSpace"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Updates the position, rotation, and/or scale of an actor. Params: actor, xform, worldspace?. On UE 5.9 it calls EditorToolset.ActorToolset.SetActorTransform (Params: actor, translation?, rotation?, scale?, bWorldSpace?).", "epic_call_tool"),
  },
  epic_set_annotated_actors_always_on_top: {
    epicSchema: S_epic_set_annotated_actors_always_on_top,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotatedActorsAlwaysOnTop", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] For this editor session; Project Settings saves it. Proxies use their own DepthMode instead. Available on UE 5.9; absent from UE 5.8. Params: bAlwaysOnTop", "epic_call_tool"),
  },
  epic_set_annotation_group: {
    epicSchema: S_epic_set_annotation_group,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotationGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Recolour an annotation into another group, 1-8. Available on UE 5.9; absent from UE 5.8. Params: annotation, group", "epic_call_tool"),
  },
  epic_set_annotation_prompt: {
    epicSchema: S_epic_set_annotation_prompt,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotationPrompt", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Set Annotation Prompt Available on UE 5.9; absent from UE 5.8. Params: annotation, prompt", "epic_call_tool"),
  },
  epic_set_annotation_shape: {
    epicSchema: S_epic_set_annotation_shape,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetAnnotationShape", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Change a proxy region's shape. An annotation on a scene actor takes the actor's own shape and is refused. Available on UE 5.9; absent from UE 5.8. Params: annotation, shape", "epic_call_tool"),
  },
  epic_set_capture_stage: {
    epicSchema: S_epic_set_capture_stage,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetCaptureStage", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] For this editor session; Project Settings saves it. Available on UE 5.9; absent from UE 5.8. Params: stage", "epic_call_tool"),
  },
  epic_set_component_transform: {
    epicSchema: S_epic_set_component_transform,
    epicTool: { toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.SetComponentTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.ActorToolset] Updates the position, rotation, and/or scale of a scene component. Anything not provided keeps its current value. Available on UE 5.9; absent from UE 5.8. Params: component, translation?, rotation?, scale?, bWorldSpace?", "epic_call_tool"),
  },
  epic_set_label: {
    epicSchema: S_epic_set_label,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.set_label", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.SetLabel", engines: ["5.9"], schema: S_epic_set_label__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Sets the human-friendly name of the actor. Params: actor, label. On UE 5.9 it calls EditorToolset.ActorToolset.SetLabel (Params: actor, label).", "epic_call_tool"),
  },
  epic_set_labels_visible: {
    epicSchema: S_epic_set_labels_visible,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetLabelsVisible", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Set Labels Visible Available on UE 5.9; absent from UE 5.8. Params: bVisible", "epic_call_tool"),
  },
  epic_set_overlay_visible: {
    epicSchema: S_epic_set_overlay_visible,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetOverlayVisible", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Show or hide the viewport overlay. It turns on again when a level that had no annotations gets one. Available on UE 5.9; absent from UE 5.8. Params: bVisible", "epic_call_tool"),
  },
  epic_set_parent_component: {
    epicSchema: S_epic_set_parent_component,
    epicTool: { toolset: "editor_toolset.toolsets.actor.ActorTools", name: "editor_toolset.toolsets.actor.ActorTools.set_parent_component", engines: ["5.8"], successors: [{ toolset: "EditorToolset.ActorToolset", name: "EditorToolset.ActorToolset.SetParentComponent", engines: ["5.9"], schema: S_epic_set_parent_component__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.actor.ActorTools] Sets the parent for the specified scene component. For blueprint actors, passing a component as the parent of the root promotes it to the scene root, making the current root a child of it. If the current root is a DefaultSceneRoot, Unreal will automatically remove it. Params: component, parent?. On UE 5.9 it calls EditorToolset.ActorToolset.SetParentComponent (Params: component, parent?).", "epic_call_tool"),
  },
  epic_set_persist_new_annotations: {
    epicSchema: S_epic_set_persist_new_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetPersistNewAnnotations", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] For this editor session; Project Settings > Plugins > Unreal Canvas Spatial Annotations saves it. Available on UE 5.9; absent from UE 5.8. Params: bPersist", "epic_call_tool"),
  },
  epic_set_visible_annotation_groups: {
    epicSchema: S_epic_set_visible_annotation_groups,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.SetVisibleAnnotationGroups", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Shows a specific set of colour groups and hides every other one. If the active group ends up hidden it moves to the lowest group still visible, so a later PlaceAnnotation without an explicit Group stays visible. Available on UE 5.9; absent from UE 5.8. Params: groups", "epic_call_tool"),
  },
  epic_set_visible_groups: {
    epicSchema: S_epic_set_visible_groups,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.SetVisibleGroups", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Show only these colour groups. If the active group ends up hidden it moves to the lowest visible one. Available on UE 5.9; absent from UE 5.8. Params: groups", "epic_call_tool"),
  },
  epic_should_persist_new_annotations: {
    epicSchema: S_epic_should_persist_new_annotations,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.ShouldPersistNewAnnotations", engines: ["5.9"] },
    ...bp("read", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Whether new annotations are saved with the level (the default) or are scratch work. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_show_all_annotation_groups: {
    epicSchema: S_epic_show_all_annotation_groups,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.ShowAllAnnotationGroups", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Makes every annotation colour group visible again, undoing an isolate or set-visible call. Nothing is hidden afterwards, so the active group keeps whatever value it had. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_show_all_groups: {
    epicSchema: S_epic_show_all_groups,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem.ShowAllGroups", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem] Show All Groups Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_trace_world: {
    epicSchema: S_epic_trace_world,
    epicTool: { toolset: "editor_toolset.toolsets.scene.SceneTools", name: "editor_toolset.toolsets.scene.SceneTools.trace_world", engines: ["5.8"], successors: [{ toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.TraceWorld", engines: ["5.9"], schema: S_epic_trace_world__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.scene.SceneTools] Traces a line through the world and returns the distance to the first hit. Params: start, end. On UE 5.9 it calls EditorToolset.LevelToolset.TraceWorld (Params: start, end).", "epic_call_tool"),
  },
  epic_unload_actors: {
    epicSchema: S_epic_unload_actors,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.UnloadActors", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Unloads previously loaded World Partition actors from the editor. Available on UE 5.9; absent from UE 5.8. Params: actors", "epic_call_tool"),
  },
  epic_unload_level_instance: {
    epicSchema: S_epic_unload_level_instance,
    epicTool: { toolset: "EditorToolset.LevelToolset", name: "EditorToolset.LevelToolset.UnloadLevelInstance", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.LevelToolset] Unloads a level instance's level. Available on UE 5.9; absent from UE 5.8. Params: levelInstance", "epic_call_tool"),
  },
  epic_update_annotation: {
    epicSchema: S_epic_update_annotation,
    epicTool: { toolset: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset", name: "UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset.UpdateAnnotation", engines: ["5.9"] },
    ...bp("mutate", "[Epic UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset] Changes an existing annotation's prompt and/or colour group. Available on UE 5.9; absent from UE 5.8. Params: id, prompt?, group?", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actor: z.unknown().optional().describe("Represents a reference to a UObject or UClass."),
  actor_type: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  actors: z.array(z.unknown()).optional().describe("The actors to add."),
  actorType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Actor class to instantiate."),
  annotation: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  asset_path: z.string().optional(),
  assetPath: z.string().optional().describe("The content path of the data layer asset (e.g. \"/Game/DataLayers/Audio\")."),
  bAllowSingletonDuplicate: z.boolean().optional().describe("Some actors (e.g. DirectionalLight) are singleton-by-convention. Set this to true to allow creating multiple instances of these actors."),
  bAlwaysOnTop: z.boolean().optional(),
  bCopySourceProperties: z.boolean().optional().describe("If true and the asset is a Blueprint that shares a native parent with a replaced actor's class, copies over that actor's property values."),
  bDestroySourceActors: z.boolean().optional().describe("If true, removes the source actors after merging."),
  bDiscard: z.boolean().optional().describe("If true, discards all changes without saving."),
  bHidden: z.boolean().optional().describe("True to hide the actor, false to unhide it."),
  bIsRuntime: z.boolean().optional().describe("If true, the layer affects runtime streaming (its actors can be loaded and unloaded during play). If false, it is editor-only."),
  bounds: z.record(z.string(), z.unknown()).optional(),
  bPersist: z.boolean().optional(),
  bRecursive: z.boolean().optional().describe("If true, also includes actors in sub-folders."),
  bShowLabel: z.boolean().optional().describe("Whether the viewport shows the prompt label."),
  bSnapToGround: z.boolean().optional().describe("If true, adjusts the actor's Z position so the bottom of its bounding box rests on the ground."),
  bVisible: z.boolean().optional(),
  bWorldPartition: z.boolean().optional().describe("If true, the new level is a partitioned (World Partition) world. If false, a regular non-partitioned level."),
  bWorldSpace: z.boolean().optional().describe("True means the values are in world space. False means relative to parent. Has no effect on actors in blueprints, which only have a default relative transform."),
  collision_channels: z.array(z.unknown()).optional(),
  component: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  component_type: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  componentType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The type of component to add."),
  destroy_source_actors: z.boolean().optional(),
  dimensions: z.record(z.string(), z.unknown()).optional().describe("The x, y, and z size of the cube. Defaults to (100, 100, 100)."),
  discard: z.boolean().optional(),
  end: z.record(z.string(), z.unknown()).optional().describe("The end point of the trace in world space."),
  filter: z.record(z.string(), z.unknown()).optional().describe("The search criteria. Defaults to matching every actor in the level."),
  folder_path: z.string().optional(),
  folderPath: z.string().optional().describe("The folder path to delete (e.g. \"Lighting/Spotlights\")."),
  group: z.number().optional().describe("Colour group 1-8, or 0 for the active group."),
  groups: z.array(z.unknown()).optional().describe("At least one group, each 1-8. ShowAllGroups restores every group."),
  height: z.number().optional().describe("The height of the cone. Defaults to 100."),
  id: z.string().optional().describe("The annotation's id, as returned by ListAnnotations."),
  label: z.string().optional().describe("The new name for the actor."),
  level_instance: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  level_path: z.string().optional(),
  levelInstance: z.unknown().optional().describe("The level instance actor to commit."),
  levelPath: z.string().optional().describe("The content path for the new level asset (e.g. \"/Game/Maps/MyLevel\")."),
  local_transform: z.record(z.string(), z.unknown()).optional().describe("Represents a 3D transformation with optional location, rotation, and scale. Unset fields mean \"identity\" when creating objects and \"don't change\" when modifyin…"),
  localTransform: z.record(z.string(), z.unknown()).optional().describe("The transform of the component relative to the actor."),
  location: z.record(z.string(), z.unknown()).optional().describe("World location in cm; omit to spawn in front of the camera."),
  name: z.string().optional().describe("The name of the new component."),
  new_path: z.string().optional(),
  newActor: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The actor whose state is the new side of the diff."),
  newPath: z.string().optional().describe("The new folder path (e.g. \"Lights\")."),
  newScenePath: z.string().optional().describe("Content path to the new scene asset."),
  old_path: z.string().optional(),
  oldActor: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The actor whose state is the old side of the diff."),
  oldPath: z.string().optional().describe("The current folder path (e.g. \"Lighting\")."),
  oldScenePath: z.string().optional().describe("Content path to the old scene asset."),
  output_path: z.string().optional(),
  outputPath: z.string().optional().describe("Content path for the merged mesh asset (e.g. \"/Game/Meshes/Merged\")."),
  owner: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  parent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  prompt: z.string().optional().describe("The edit instruction; empty tags the actor without one."),
  radius: z.number().optional().describe("The radius of the cone base. Defaults to 50."),
  recursive: z.boolean().optional(),
  root: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  rotation: z.record(z.string(), z.unknown()).optional().describe("The new rotation, or unset to keep the current one."),
  scale: z.unknown().optional().describe("The new scale, or unset to keep the current one."),
  shape: z.string().optional().describe("The proxy's shape."),
  snap_to_ground: z.boolean().optional(),
  stage: z.string().optional().describe("Value display names: PrePostProcess = \"Pre Post-Process (linear HDR)\", PostFinal = \"Post Final (after FXAA/SMAA)\"."),
  start: z.record(z.string(), z.unknown()).optional().describe("The start point of the trace in world space."),
  tag: z.string().optional().describe("The Actor tag to add."),
  target: z.record(z.string(), z.unknown()).optional().describe("The world-space position to face."),
  transform: z.record(z.string(), z.unknown()).optional().describe("World transform. Scale 1 is a 100 cm shape."),
  translation: z.record(z.string(), z.unknown()).optional().describe("The new position, or unset to keep the current one."),
  unsavedChangesPolicy: z.string().optional().describe("What to do if there are unsaved level changes: Fail raises an error naming them, Save saves them first, Discard loses them."),
  worldspace: z.boolean().optional(),
  xform: z.record(z.string(), z.unknown()).optional().describe("Represents a 3D transformation with optional location, rotation, and scale. Unset fields mean \"identity\" when creating objects and \"don't change\" when modifyin…"),
};
