/**
 * Asset locks are taken on the params a handler's C++ spec declares with
 * `.Role(EMCPParamRole::EditTarget)`. A writing handler that names an asset
 * path and declares no edit target runs unlocked, so each one is either
 * annotated or listed here as reviewed.
 */
import { describe, expect, it } from "vitest";
import { bridgeMethodEffect } from "../../../src/surface/action-effects.js";
import { RECORDED_HANDLER_SPECS } from "../../../src/tools/specs/index.js";

/** Params that name a file, a folder a new asset is created under, or an actor, never an asset written. */
const NOT_AN_ASSET = /^(filePath|csvPath|jsonPath|folderPath|packagePath|subPath|outputBasePath|scriptPath|.*[aA]ctorPaths?)$/;

/** Writing handlers whose path params are reviewed as not naming the asset they write. */
const REVIEWED_WITHOUT_TARGET: Record<string, string[]> = {
  "it reads the asset it names and writes a new one under packagePath and name": [
    "create_anim_blueprint", "create_anim_composite", "create_anim_montage", "create_behavior_tree", "create_blendspace",
    "create_blendspace_1d", "create_control_rig", "create_foliage_type", "create_ik_rig", "create_material_instance",
    "create_mirror_data_table", "create_niagara_emitter", "create_pose_search_database", "create_pose_search_schema",
    "create_sequence", "create_sound_class", "create_submix", "import_animation", "import_skeletal_mesh",
    "batch_retarget_animations", "convert_brushes_to_static_mesh", "apply_mesh_fracture",
  ],
  "it writes the level, an actor or a component, and only references the asset": [
    "add_foliage_instances", "add_foliage_type_to_level", "remove_foliage_instances", "remove_foliage_type_from_level",
    "add_pcg_volume", "spawn_volume", "toggle_pcg_graph", "add_post_process_blendable", "add_rvt_volume", "set_rvt_volume_bounds",
    "assign_rvt_to_landscape", "add_streaming_sublevel", "attach_actor", "attach_component", "detach_actor", "detach_component",
    "clear_procedural_foliage", "simulate_procedural_foliage", "set_actor_material", "set_landscape_material",
    "set_live_post_process_anim_blueprint", "set_world_partition_settings", "spawn_ambient_sound", "spawn_niagara_actor",
    "spawn_niagara_at_location", "spawn_skeletal_mesh_actor", "invoke_object_function", "set_object_property", "report_noise_event",
  ],
  "it writes a file, or nothing, from the asset it reads": [
    "analyze_animation", "capture_scene_png", "capture_screenshot", "export_actor_fbx", "export_asset", "export_blueprint_batch",
    "export_texture", "export_uv_layout", "render_material_preview", "render_sequence_frames", "migrate", "list_sockets",
  ],
  "it opens, runs or plays the asset without writing it": [
    "add_to_viewport", "load_level", "open_asset", "play_sequence", "scrub_sequence", "play_sound_at_location",
    "run_behavior_tree", "run_construction_script", "run_editor_utility_blueprint", "run_editor_utility_widget",
  ],
  "it addresses content folders, not assets": ["create_folder", "delete_folder", "move_folder"],
};

describe("edit target roles", () => {
  const reviewed = new Set(Object.values(REVIEWED_WITHOUT_TARGET).flat());

  it("are declared by every writing handler that names an asset path, or it is reviewed", () => {
    const unannotated: string[] = [];
    for (const [method, spec] of Object.entries(RECORDED_HANDLER_SPECS)) {
      if (bridgeMethodEffect(method).effect === "read") continue;
      if (spec.params.some((p) => p.role === "editTarget") || reviewed.has(method)) continue;
      const named = spec.params.filter((p) =>
        (p.type === "string" || (p.type === "array" && p.items === "string")) && /(^paths?$|Paths?$)/.test(p.name) && !NOT_AN_ASSET.test(p.name));
      if (named.length) unannotated.push(`${method} (${named.map((p) => p.name).join(", ")})`);
    }
    expect(unannotated, "annotate the written asset's param with .Role(EMCPParamRole::EditTarget), or review it here").toEqual([]);
  });

  it("are never declared by a handler that only reads", () => {
    const readers = Object.entries(RECORDED_HANDLER_SPECS)
      .filter(([method, spec]) => bridgeMethodEffect(method).effect === "read" && spec.params.some((p) => p.role === "editTarget"))
      .map(([method]) => method);
    expect(readers).toEqual([]);
  });

  it("are declared by every handler that declares a commit policy, which the edit scope needs to open it", () => {
    const untargeted = Object.entries(RECORDED_HANDLER_SPECS)
      .filter(([, spec]) => spec.commit !== undefined && !spec.params.some((p) => p.role === "editTarget"))
      .map(([method]) => method);
    expect(untargeted).toEqual([]);
  });

  it("come with a commit policy on every StateTree writer, since its edit scope takes compile and save from it", () => {
    const missing = Object.entries(RECORDED_HANDLER_SPECS)
      .filter(([, spec]) => spec.category === "statetree" && spec.params.some((p) => p.role === "editTarget") && spec.commit === undefined)
      .map(([method]) => method);
    expect(missing).toEqual([]);
  });

  it("list only handlers that exist and declare no edit target", () => {
    for (const method of reviewed) {
      expect(RECORDED_HANDLER_SPECS[method], method).toBeDefined();
      expect(RECORDED_HANDLER_SPECS[method].params.some((p) => p.role === "editTarget"), method).toBe(false);
    }
  });
});
