import * as fs from "node:fs";
import { z } from "zod";
import type { ActionSpec, ToolDef } from "../core/types.js";
import { bp, categoryTool } from "../surface/category-tool.js";
import { shapeCanvasAnswer, type CanvasResultShape } from "./canvas-results.js";

// Unreal Canvas (UE 5.9): the UnrealCanvasBridge plugin registers about sixty
// tools with Epic's Unreal MCP plugin, outside the toolset registry. These
// actions are typed fronts for the ones agents use most: Movie Render Graph
// renders, scene snapshots, the viewport stream, stencil and annotation
// capture settings, and Gaussian splat and cutout actors. Each runs its tool
// through epic_call_mcp_tool, so there is no new C++, and is withheld while
// the editor's MCP tool list lacks it, which is what an editor without the
// Bridge plugin reports (#1824).
//
// A category of its own rather than actions on editor and level: every one
// of them drives the one plugin's pipeline, reads its preset and settings
// (Editor Preferences > Plugins > Unreal Canvas) or acts on actors only it
// spawns, and none of them affects the native capture, render or actor
// actions those categories hold. Withheld together, the category disappears
// as a unit from an editor that does not load the plugin.
//
// Each action is written `{ ...bp(effect, description, "epic_call_mcp_tool"),
// ...mcp(tool, toInput, options) }` so the source audits read its description
// as they read every other action's.

export const CANVAS_PLUGIN = "UnrealCanvasBridge";

type Params = Record<string, unknown>;

/** Drop undefined keys, so the tool sees only what the caller set. */
function defined(o: Params): Params {
  const out: Params = {};
  for (const [k, v] of Object.entries(o)) if (v !== undefined) out[k] = v;
  return out;
}

/** [x, y, z] from an array, or from {x, y, z} / {pitch, yaw, roll}, as the Bridge's spawners take it. */
export function vec3(v: unknown): number[] | undefined {
  if (v === undefined || v === null) return undefined;
  if (Array.isArray(v)) return v.map(Number);
  if (typeof v === "object") {
    const o = v as Record<string, unknown>;
    if ("pitch" in o || "yaw" in o || "roll" in o) return [Number(o.pitch ?? 0), Number(o.yaw ?? 0), Number(o.roll ?? 0)];
    return [Number(o.x ?? 0), Number(o.y ?? 0), Number(o.z ?? 0)];
  }
  throw new Error("location and rotation take [x, y, z] or {x, y, z} ({pitch, yaw, roll} for rotation)");
}

/** The PNG bytes a cutout tool takes, from pngBase64 or a file at pngPath on this machine. */
export function pngBytes(p: Params): string {
  if (typeof p.pngBase64 === "string" && p.pngBase64 !== "") return p.pngBase64;
  if (typeof p.pngPath === "string" && p.pngPath !== "") {
    try {
      return fs.readFileSync(p.pngPath).toString("base64");
    } catch (e) {
      throw new Error("pngPath could not be read on the machine this server runs on: " + (e as Error).message);
    }
  }
  throw new Error("Pass pngPath (a PNG file) or pngBase64 (its bytes, base64).");
}

/** The fields the render tools share, under the Bridge's own names. */
function renderCommon(p: Params): Params {
  return { PresetPath: p.presetPath, Variables: p.variables, Resolution: p.resolution };
}

interface CanvasOptions {
  shape?: CanvasResultShape;
  timeoutMs?: number;
  destructive?: boolean;
}

/**
 * The half of a canvas action that says which Bridge tool it runs: the
 * envelope epic_call_mcp_tool takes, the tool for withholding, and the
 * result shaping. Spread over a bp() that names epic_call_mcp_tool.
 */
function mcp(tool: string, toInput: (p: Params) => Params = () => ({}), options: CanvasOptions = {}): Partial<ActionSpec> {
  const shape = options.shape ?? "json";
  return defined({
    mapParams: (p: Params) => ({ tool, input: defined(toInput(p)) }),
    mcpTool: { name: tool, plugin: CANVAS_PLUGIN },
    followUp: async (_ctx: unknown, _params: Params, answered: unknown) => shapeCanvasAnswer(shape, answered),
    timeoutMs: options.timeoutMs,
    destructive: options.destructive,
  }) as Partial<ActionSpec>;
}

export const canvasTool: ToolDef = categoryTool(
  "canvas",
  "Unreal Canvas (UE 5.9, the UnrealCanvasBridge plugin): Movie Render Graph renders of the viewport, a Level Sequence or an object reposition, the passes and variables of the render preset, scene snapshots, the raw RGBA viewport stream, stencil, depth and annotation capture settings, and Gaussian splat and cutout actors. Each action runs the Bridge's own MCP tool through epic(call_mcp_tool); they are withheld when the editor does not serve that tool, which is the case until UnrealCanvasBridge is enabled in the .uproject. Render paths are on the editor's machine.",
  {
    status: { ...bp("read", "Whether the Canvas Bridge is ready and an MRG preset is configured, with the preset's pass names. Returns ready, presetConfigured, passes? Params: none", "epic_call_mcp_tool"), ...mcp("GetStatus") } as ActionSpec,
    list_passes: { ...bp("read", "The render passes (output branches) of the MRG preset and the captures each is expected to produce. Returns passes [name] and captures [{branch, subResource}], which match passName, branchName and subResourceName on render results. Params: presetPath?, variables?", "epic_call_mcp_tool"), ...mcp("GetMRGPasses", (p) => ({ PresetPath: p.presetPath, Variables: p.variables })) } as ActionSpec,
    list_variables: { ...bp("read", "The variables the MRG preset defines, with type and default as serialized strings. Pass overrides to the render actions as variables. Returns variables [{name, type, value}]. Params: presetPath?", "epic_call_mcp_tool"), ...mcp("GetMRGVariables", (p) => ({ PresetPath: p.presetPath })) } as ActionSpec,
    render: { ...bp("mutate", "Render the current editor viewport through Movie Render Graph. executionMode PlayInEditor (default) starts a PIE session so physics and Blueprint logic run; EditorWorld renders the loaded editor world at once, as a draft. captureSceneState takes a whole-world Level Snapshot first, for render_reposition and save_scene_snapshot (slow on a large level). Takes 30 to 120 seconds; renders run one at a time. "
      + "Returns images [{path, passName, branchName, subResourceName, camera}], one PNG per capture under the project's Saved folder; rawDepth [{path, camera}] for a depth capture (float centimetres as .npy); and sceneStateBlobPath when captureSceneState was set. "
      + "presetPath defaults to the preset in Editor Preferences > Plugins > Unreal Canvas; variables is {name: serialized value} (see list_variables); resolution is {size: preset|1080p|1440p|2160p|custom, width?, height?, matchAspect?}; cameraSource is {mode: viewport|sequence|actor, actorId?, actorName?}; viewportLook is {visibility?, showFlags?, viewMode?}. "
      + "Params: presetPath?, variables?, resolution?, cameraSource?, viewportLook?, executionMode? (PlayInEditor|EditorWorld), captureSceneState?", "epic_call_mcp_tool"), ...mcp("TriggerMRGRender", (p) => ({
      ...renderCommon(p),
      CameraSource: p.cameraSource,
      ViewportLookFlags: p.viewportLook,
      ExecutionMode: p.executionMode,
      CaptureSceneState: p.captureSceneState,
    }), { shape: "still", timeoutMs: 600_000 }) } as ActionSpec,
    render_sequence: { ...bp("mutate", "Render a Level Sequence through Movie Render Graph to PNG frames. sequencePath omitted renders the sequence open in Sequencer (only the focused shot when Sequencer is drilled into one); an explicit path renders all of it. cameraSource defaults to the sequence's own Camera Cuts. May take minutes. "
      + "Returns passes [{directory, passName, branchName, subResourceName, frameCount, fps, filePattern, startFrame, firstFrame, lastFrame, sequenceName}], one directory of PNG frames per capture under Saved/UnrealCanvasSequenceRender. presetPath, variables, resolution, cameraSource and viewportLook are as for render. "
      + "Params: sequencePath?, presetPath?, variables?, resolution?, cameraSource?, viewportLook?", "epic_call_mcp_tool"), ...mcp("TriggerSequenceRender", (p) => ({
      SequencePath: p.sequencePath,
      ...renderCommon(p),
      CameraSource: p.cameraSource,
      ViewportLookFlags: p.viewportLook,
    }), { shape: "sequence", timeoutMs: 900_000 }) } as ActionSpec,
    render_reposition: { ...bp("mutate", "Render the move from a captured scene state to the current one: loads the Level Snapshot a render with captureSceneState wrote (sceneStateBlobPath), builds a transient sequence that moves each changed actor from its captured pose to its current one (added actors appear and removed ones disappear at the midpoint) while the camera moves from cameraPose to the current viewport, and renders it. A camera-only change is valid. "
      + "cameraPose is the camera from the original render's result (object or JSON string). durationSeconds defaults to 4 and fps to 24; saveSequence keeps the sequence for Sequencer. Returns passes as render_sequence does. "
      + "Params: sceneStateBlobPath, cameraPose?, durationSeconds?, fps?, saveSequence?, presetPath?, variables?, resolution?", "epic_call_mcp_tool"), ...mcp("BuildAndRenderRepositionSequence", (p) => ({
      sceneStateBlobPath: p.sceneStateBlobPath,
      cameraPose: p.cameraPose === undefined || typeof p.cameraPose === "string" ? p.cameraPose : JSON.stringify(p.cameraPose),
      durationSeconds: p.durationSeconds,
      fps: p.fps,
      saveSequence: p.saveSequence,
      ...renderCommon(p),
    }), { shape: "sequence", timeoutMs: 900_000 }) } as ActionSpec,
    save_scene_snapshot: { ...bp("mutate", "Save the Level Snapshot a render captured (its sceneStateBlobPath) as a Level Snapshot asset, which can be inspected or applied to put the captured actor poses back. Saves straight to destinationPath/assetName (default the Canvas generated-content folder's SceneState); showDialog=true opens the editor's save-as dialog instead, which waits for a person. Returns assetPath. Params: sceneStateBlobPath, destinationPath?, assetName?, showDialog?", "epic_call_mcp_tool"), ...mcp("SaveSceneSnapshot", (p) => ({
      sceneStateBlobPath: p.sceneStateBlobPath,
      DestinationPath: p.destinationPath,
      AssetName: p.assetName,
      SkipDialog: p.showDialog !== true,
    }), { shape: "assetPath" }) } as ActionSpec,
    start_viewport_stream: { ...bp("mutate", "Stream the editor viewport over TCP as raw 8-bit RGBA frames, with optional depth, world-normal and stencil matte passes and the annotation JSON per frame. port omitted uses this editor's assigned stream port, or 9880. width and height default to 512, fps to 30; color, depth and normal default on, stencil off. Returns streaming, port, width, height, fps, format, exact_size, shared_memory_name? Params: port?, width?, height?, fps?, color?, depth?, normal?, stencil?, sharedMemory?, exactSize?", "epic_call_mcp_tool"), ...mcp("StartViewportStream", (p) => ({
      port: p.port,
      width: p.width,
      height: p.height,
      fps: p.fps,
      enable_color: p.color,
      enable_depth: p.depth,
      enable_normal: p.normal,
      enable_stencil: p.stencil,
      shared_memory: p.sharedMemory,
      exact_size: p.exactSize,
    })) } as ActionSpec,
    set_viewport_stream_fps: { ...bp("mutate", "Change the running viewport stream's capture rate (1-120) without restarting it. Refused when no stream runs. Returns fps. Params: fps", "epic_call_mcp_tool"), ...mcp("SetViewportStreamFps", (p) => ({ fps: p.fps })) } as ActionSpec,
    stop_viewport_stream: { ...bp("mutate", "Stop the viewport stream and disconnect its clients. Returns streaming=false and wasStopped. Params: none", "epic_call_mcp_tool"), ...mcp("StopViewportStream") } as ActionSpec,
    assign_stencil: { ...bp("mutate", "Turn on custom depth for an actor's primitive components and give them a stencil ID (1-254), which the stencil matte pass and stencil color map key on. Params: actorPath (or actorLabel), stencilId", "epic_call_mcp_tool"), ...mcp("AssignStencilID", (p) => ({ actorId: p.actorPath, actorName: p.actorLabel, stencilId: p.stencilId })) } as ActionSpec,
    clear_stencil: { ...bp("mutate", "Turn off custom depth on an actor's primitive components and reset its stencil value. Params: actorPath (or actorLabel)", "epic_call_mcp_tool"), ...mcp("ClearStencilID", (p) => ({ actorId: p.actorPath, actorName: p.actorLabel })) } as ActionSpec,
    clear_all_stencils: { ...bp("mutate", "Turn off custom depth and reset the stencil on the given actors, or on every actor in the editor world that has custom depth on when none are given. Params: actorPaths?, actorLabels?", "epic_call_mcp_tool"), ...mcp("ClearAllStencils", (p) => ({ actorIds: p.actorPaths, actorNames: p.actorLabels })) } as ActionSpec,
    set_stencil_colors: { ...bp("mutate", "Set the stencil-to-color map the viewport stream's matte pass uses: each group maps a stencil ID to an RGB color, and visible=false leaves it out of the matte. At most 32 groups, each {stencilId, r, g, b, visible?}. Params: groups", "epic_call_mcp_tool"), ...mcp("SetStencilColorMap", (p) => ({ groups: p.groups })) } as ActionSpec,
    set_depth_range: { ...bp("mutate", "Set the depth pass's near and far cutoffs in centimetres (0 or omitted uses each frame's own min or max) and its encoding (ndc, metric or hilbert). Params: nearCm?, farCm?, encoding?", "epic_call_mcp_tool"), ...mcp("SetDepthRange", (p) => ({ nearCm: p.nearCm, farCm: p.farCm, encoding: p.encoding })) } as ActionSpec,
    set_capture_stage: { ...bp("mutate", "Choose the post-process stage the stream captures scene color and composites stencils at: pre_postprocess, post_tonemap or post_final. A stage whose pass the project does not enable cascades to the one before it. Params: stage", "epic_call_mcp_tool"), ...mcp("SetCaptureStage", (p) => ({ stage: p.stage })) } as ActionSpec,
    set_capture_annotations: { ...bp("mutate", "Turn the 3D annotations pass (the annotations-only image the stream and renders carry) on or off. Params: enabled", "epic_call_mcp_tool"), ...mcp("SetCaptureAnnotations", (p) => ({ enabled: p.enabled })) } as ActionSpec,
    set_viewport_annotations: { ...bp("mutate", "Show or hide the 3D annotation overlay in the editor viewport. Params: enabled", "epic_call_mcp_tool"), ...mcp("SetShowAnnotationsInViewport", (p) => ({ enabled: p.enabled })) } as ActionSpec,
    set_annotation_render_mode: { ...bp("mutate", "Set how 3D annotations rasterize: filled faces or not, and the edge half-width in cube-local units (0 draws no edges). Params: fill, edgeThickness", "epic_call_mcp_tool"), ...mcp("SetAnnotationRenderMode", (p) => ({ fill: p.fill, edgeThickness: p.edgeThickness })) } as ActionSpec,
    spawn_splat: { ...bp("mutate", "Import a Gaussian splat .ply (a path on the editor's machine) as a new GaussianSplat asset and place a GaussianSplatActor for it. An actor with the same actorTag (default UnrealCanvasSplat) is updated in place unless replaceExisting=false. location is [x,y,z] or {x,y,z}; rotation is [pitch,yaw,roll] or {pitch,yaw,roll}. Returns gsplat_id, actor_path, asset_path, actor_tag, label. Params: plyPath, location?, rotation?, actorTag?, label?, replaceExisting?", "epic_call_mcp_tool"), ...mcp("SpawnGsplatFromPLY", (p) => ({
      ply_path: p.plyPath,
      location: vec3(p.location),
      rotation: vec3(p.rotation),
      actor_tag: p.actorTag,
      label: p.label,
      replace_existing: p.replaceExisting,
    })) } as ActionSpec,
    list_splats: { ...bp("read", "List the Gaussian splat actors spawn_splat placed in the editor world. Returns gsplats [{gsplat_id, actor_path, label, location, rotation, asset_path}]. Params: none", "epic_call_mcp_tool"), ...mcp("ListGsplats") } as ActionSpec,
    remove_splat: { ...bp("mutate", "Delete a Gaussian splat actor spawn_splat placed, by its gsplat_id. The imported asset stays. Returns actor_path. Params: splatId", "epic_call_mcp_tool"), ...mcp("RemoveGsplat", (p) => ({ gsplat_id: p.splatId }), { destructive: true }) } as ActionSpec,
    spawn_cutout: { ...bp("mutate", "Place a cutout actor (a textured card) using existing Texture2D assets, one variant per texture, the first active. labels runs parallel to texturePaths; intensity is the emissive multiplier (default 0.3). Returns cutout_id and actor_path. Params: texturePaths, location, rotation?, labels?, intensity?", "epic_call_mcp_tool"), ...mcp("SpawnCutoutAt", (p) => ({
      texture_paths: p.texturePaths,
      labels: p.labels,
      location: vec3(p.location),
      rotation: vec3(p.rotation),
      intensity: p.intensity,
    })) } as ActionSpec,
    spawn_cutout_from_png: { ...bp("mutate", "Place a cutout actor from a PNG image, which becomes its first variant. pngPath is read by this server, so it is a path on the machine ue-mcp runs on. provenancePath is a detached C2PA manifest signed for the image. Returns cutout_id and actor_path. Params: pngPath (or pngBase64), location, rotation?, label?, intensity?, provenancePath?", "epic_call_mcp_tool"), ...mcp("SpawnCutoutFromPNG", (p) => ({
      png_bytes_base64: pngBytes(p),
      location: vec3(p.location),
      rotation: vec3(p.rotation),
      label: p.label,
      intensity: p.intensity,
      provenance_path: p.provenancePath,
    })) } as ActionSpec,
    list_cutouts: { ...bp("read", "List the cutout actors in the editor world. Returns cutouts [{cutout_id, actor_path, location, rotation, variant_count, active_variant_index, labels}]. Params: none", "epic_call_mcp_tool"), ...mcp("ListCutouts") } as ActionSpec,
    remove_cutout: { ...bp("mutate", "Delete a cutout actor by its cutout_id. Params: cutoutId", "epic_call_mcp_tool"), ...mcp("RemoveCutout", (p) => ({ cutout_id: p.cutoutId }), { destructive: true }) } as ActionSpec,
    add_cutout_variant: { ...bp("mutate", "Add an image variant to a cutout actor. Returns variant_index. Params: cutoutId, pngPath (or pngBase64), label?", "epic_call_mcp_tool"), ...mcp("AddCutoutVariant", (p) => ({ cutout_id: p.cutoutId, png_bytes_base64: pngBytes(p), label: p.label })) } as ActionSpec,
    set_cutout_variant: { ...bp("mutate", "Show another of a cutout actor's variants, by 0-based index. Params: cutoutId, variantIndex", "epic_call_mcp_tool"), ...mcp("SetCutoutActiveVariant", (p) => ({ cutout_id: p.cutoutId, variant_index: p.variantIndex })) } as ActionSpec,
  },
  {
    presetPath: z.string().optional().describe("MovieGraphConfig asset path; omitted uses the preset set in Editor Preferences > Plugins > Unreal Canvas"),
    variables: z.record(z.string(), z.string()).optional().describe("MRG variable overrides, {name: serialized value} (e.g. {\"MyFloat\": \"1.5\"}); list_variables names them"),
    resolution: z.object({
      size: z.enum(["preset", "1080p", "1440p", "2160p", "custom"]).optional(),
      width: z.number().int().optional(),
      height: z.number().int().optional(),
      matchAspect: z.boolean().optional(),
    }).optional().describe("Output size: preset, a 16:9 tier, or custom width/height. matchAspect (default true) follows the render camera's aspect"),
    cameraSource: z.object({
      mode: z.enum(["viewport", "sequence", "actor"]).optional(),
      actorId: z.string().optional(),
      actorName: z.string().optional(),
    }).optional().describe("Where the render camera comes from: the viewport, the sequence's Camera Cuts, or a placed camera actor (actorId is its object path)"),
    viewportLook: z.object({
      visibility: z.boolean().optional(),
      showFlags: z.boolean().optional(),
      viewMode: z.boolean().optional(),
    }).optional().describe("Apply the editor viewport's actor visibility, show flags or view mode to the render"),
    executionMode: z.enum(["PlayInEditor", "EditorWorld"]).optional().describe("render: PlayInEditor (default) runs PIE; EditorWorld renders the editor world as shown, at once"),
    captureSceneState: z.boolean().optional().describe("render: take a Level Snapshot first and return its sceneStateBlobPath"),
    sequencePath: z.string().optional().describe("render_sequence: Level Sequence asset path; omitted renders the one open in Sequencer"),
    sceneStateBlobPath: z.string().optional().describe("The sceneStateBlobPath a render with captureSceneState returned"),
    cameraPose: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("render_reposition: the camera from the original render's result"),
    durationSeconds: z.number().optional().describe("render_reposition: clip length in seconds (default 4)"),
    fps: z.number().optional().describe("Frames per second: render_reposition's clip (default 24), or the viewport stream's rate"),
    saveSequence: z.boolean().optional().describe("render_reposition: keep the authored sequence for Sequencer"),
    destinationPath: z.string().optional().describe("save_scene_snapshot: content folder to save into"),
    assetName: z.string().optional().describe("save_scene_snapshot: asset name"),
    showDialog: z.boolean().optional().describe("save_scene_snapshot: open the save-as dialog, which waits for a person (default false)"),
    port: z.number().int().optional().describe("start_viewport_stream: TCP port; omitted uses this editor's assigned stream port"),
    width: z.number().int().optional().describe("start_viewport_stream: frame width in pixels"),
    height: z.number().int().optional().describe("start_viewport_stream: frame height in pixels"),
    color: z.boolean().optional().describe("start_viewport_stream: color pass (default true)"),
    depth: z.boolean().optional().describe("start_viewport_stream: depth pass (default true)"),
    normal: z.boolean().optional().describe("start_viewport_stream: world-normal pass (default true)"),
    stencil: z.boolean().optional().describe("start_viewport_stream: stencil matte pass (default false)"),
    sharedMemory: z.boolean().optional().describe("start_viewport_stream: also publish color frames as SharedMemoryMedia textures"),
    exactSize: z.boolean().optional().describe("start_viewport_stream: resize an offscreen editor's window so frames need no scaling"),
    actorPath: z.string().optional().describe("Actor object path (preferred over actorLabel)"),
    actorLabel: z.string().optional().describe("Actor display label"),
    actorPaths: z.array(z.string()).optional().describe("clear_all_stencils: actor object paths"),
    actorLabels: z.array(z.string()).optional().describe("clear_all_stencils: actor labels"),
    stencilId: z.number().int().min(1).max(254).optional().describe("assign_stencil: stencil ID, 1-254"),
    groups: z.array(z.object({
      stencilId: z.number().int(),
      r: z.number().int(),
      g: z.number().int(),
      b: z.number().int(),
      visible: z.boolean().optional(),
    })).optional().describe("set_stencil_colors: stencil ID to RGB (0-255) mappings"),
    nearCm: z.number().optional().describe("set_depth_range: near cutoff in cm (0 = frame min)"),
    farCm: z.number().optional().describe("set_depth_range: far cutoff in cm (0 = frame max)"),
    encoding: z.enum(["ndc", "metric", "hilbert"]).optional().describe("set_depth_range: depth encoding"),
    stage: z.enum(["pre_postprocess", "post_tonemap", "post_final"]).optional().describe("set_capture_stage: post-process stage"),
    enabled: z.boolean().optional().describe("set_capture_annotations / set_viewport_annotations: on or off"),
    fill: z.boolean().optional().describe("set_annotation_render_mode: draw filled faces"),
    edgeThickness: z.number().optional().describe("set_annotation_render_mode: edge half-width, 0 for none"),
    plyPath: z.string().optional().describe("spawn_splat: .ply file path on the editor's machine"),
    location: z.union([z.array(z.number()).length(3), z.object({ x: z.number(), y: z.number(), z: z.number() })]).optional().describe("World location, [x, y, z] or {x, y, z}"),
    rotation: z.union([z.array(z.number()).length(3), z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() })]).optional().describe("Rotation in degrees, [pitch, yaw, roll] or {pitch, yaw, roll}"),
    actorTag: z.string().optional().describe("spawn_splat: tag that identifies the splat actor to update (default UnrealCanvasSplat)"),
    label: z.string().optional().describe("Actor or variant label"),
    replaceExisting: z.boolean().optional().describe("spawn_splat: update the actor with the same actorTag (default true)"),
    splatId: z.string().optional().describe("remove_splat: the gsplat_id spawn_splat returned"),
    texturePaths: z.array(z.string()).optional().describe("spawn_cutout: Texture2D asset paths, one variant each"),
    labels: z.array(z.string()).optional().describe("spawn_cutout: variant labels, parallel to texturePaths"),
    intensity: z.number().optional().describe("Cutout emissive multiplier (default 0.3)"),
    pngPath: z.string().optional().describe("PNG file on the machine ue-mcp runs on"),
    pngBase64: z.string().optional().describe("PNG bytes, base64"),
    provenancePath: z.string().optional().describe("spawn_cutout_from_png: a detached C2PA manifest (.c2pa) signed for the image, attached to the assets made from it"),
    cutoutId: z.string().optional().describe("The cutout_id a spawn returned"),
    variantIndex: z.number().int().min(0).optional().describe("set_cutout_variant: 0-based variant index"),
  },
);
