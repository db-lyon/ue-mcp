// @live-editor render
/**
 * Unreal Canvas (UE 5.9) through the bridge and the shipped server (#1824).
 * tests/ue_mcp enables UnrealCanvasBridge as an optional plugin, so an engine
 * without it (before 5.9) skips this file. The Bridge registers its tools with
 * Epic's Unreal MCP plugin; epic(call_mcp_tool) runs them raw, and the canvas
 * category runs them typed and shapes their text results into paths.
 *
 * Renders use ExecutionMode EditorWorld (no PIE) and a small custom size, and
 * the Bridge's own shipped preset (/UnrealCanvasBridge/MRG/MrgNodePreset).
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { liveTarget } from "./harness.js";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";

type Json = Record<string, any>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

const DIR = "/Game/MCPTest/Canvas";
const SEQ = `${DIR}/LS_CanvasRender`;
const SMALL = { size: "custom", width: 256, height: 144, matchAspect: false };

let bridge: EditorBridge;
let server: LiveServer | null = null;
let canvasLoaded = false;
// The cutout tool writes its textures under /Game/UnrealCanvas; removed afterwards when the run created it.
const CANVAS_CONTENT = path.join(TEST_PROJECT_DIR, "Content", "UnrealCanvas");
const canvasContentExisted = fs.existsSync(CANVAS_CONTENT);

async function raw(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function canvas(action: string, args: Json = {}): Promise<Json> {
  const r: LiveCallResult = await server!.call("canvas", { action, args });
  expect(r.isError, `canvas.${action}: ${r.text}`).toBe(false);
  // A tool that fails answers success:false in the body; isError is for calls the server refused.
  const answer = resultJson<Json>(r);
  expect(answer.success, `canvas.${action}: ${r.text}`).not.toBe(false);
  return answer;
}

beforeAll(async () => {
  bridge = await getBridge();
  // The sequence render needs a saved level open; a fresh editor opens on an untitled map.
  await callBridge(bridge, "demo_go_home", { discardUnsaved: true });
  const listed = await raw("epic_list_mcp_tools", { nameFilter: "MRG" });
  canvasLoaded = (listed.tools as Json[] | undefined)?.some((t) => t.name === "TriggerMRGRender") ?? false;
  server = await LiveServer.start({ projects: [(await liveTarget()).uproject] });
  if (canvasLoaded) await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 300_000);

afterAll(async () => {
  if (canvasLoaded) {
    await callBridge(bridge, "epic_call_mcp_tool", { tool: "StopViewportStream", input: {} });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  }
  if (canvasLoaded && !canvasContentExisted) {
    // The cutout textures can still be saving when the last test ends, so delete again once they have landed.
    await callBridge(bridge, "delete_folder", { path: "/Game/UnrealCanvas", force: true });
    await new Promise((resolve) => setTimeout(resolve, 3000));
    await callBridge(bridge, "delete_folder", { path: "/Game/UnrealCanvas", force: true });
  }
  await server?.close();
  if (bridge) disconnectBridge();
  if (!canvasContentExisted) fs.rmSync(CANVAS_CONTENT, { recursive: true, force: true });
});

/**
 * Where the editor has no Canvas Bridge (any engine before 5.9, or the plugin off), every
 * case asserts the refusal instead of skipping: the server withholds the canvas actions and
 * names the plugin, and the bridge refuses the tool itself.
 */
async function expectCanvasAbsent(): Promise<void> {
  const r = await server!.call("canvas", { action: "status" });
  expect(r.text).toContain("withheld from this server's surface");
  expect(r.text).toContain("UnrealCanvasBridge");
  const direct = await callBridge(bridge, "epic_call_mcp_tool", { tool: "TriggerMRGRender", input: {} });
  expect(direct.ok && body(direct).success !== false).toBe(false);
}

describe("the Canvas Bridge's tools through epic(call_mcp_tool)", () => {
  it("lists the render, stream, stencil, splat and cutout tools as plugin tools", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    const { tools } = await raw("epic_list_mcp_tools");
    const names = new Set((tools as Json[]).filter((x) => x.source === "plugin").map((x) => x.name));
    for (const tool of ["GetStatus", "GetMRGPasses", "TriggerMRGRender", "TriggerSequenceRender", "BuildAndRenderRepositionSequence",
      "SaveSceneSnapshot", "StartViewportStream", "StopViewportStream", "AssignStencilID", "SetStencilColorMap",
      "SpawnGsplatFromPLY", "SpawnCutoutAt", "ListCutouts"]) {
      expect(names.has(tool), tool).toBe(true);
    }
  }, 60_000);

  it("TriggerMRGRender returns one PNG path per capture, each on disk", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    const r = await raw("epic_call_mcp_tool", { tool: "TriggerMRGRender", input: { ExecutionMode: "EditorWorld", Resolution: SMALL } });
    const pngs = (r.content as Json[]).filter((b) => b.type === "text" && /\.png$/i.test(b.text));
    expect(pngs.length).toBeGreaterThan(0);
    for (const b of pngs) {
      expect(fs.existsSync(b.text), b.text).toBe(true);
      expect(typeof b.annotations?.passName).toBe("string");
    }
  }, 300_000);
});

describe("canvas category", () => {
  it("status and list_passes read the configured preset", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    const status = await canvas("status");
    expect(status.ready).toBe(true);
    expect(status.presetConfigured).toBe(true);
    const passes = await canvas("list_passes");
    expect(Array.isArray(passes.passes) && passes.passes.length).toBeGreaterThan(0);
    const vars = await canvas("list_variables");
    expect(Array.isArray(vars.variables)).toBe(true);
  }, 120_000);

  it("render returns image paths matching the preset's branches, written as PNGs", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    const branches = new Set((await canvas("list_passes")).passes as string[]);
    const r = await canvas("render", { executionMode: "EditorWorld", resolution: SMALL });
    expect(r.images.length).toBeGreaterThan(0);
    for (const img of r.images as Json[]) {
      const head = fs.readFileSync(img.path).subarray(0, 8);
      expect(head.toString("hex"), img.path).toBe("89504e470d0a1a0a");
      expect(branches.has(img.branchName ?? img.passName), JSON.stringify(img)).toBe(true);
      expect(img.camera?.intrinsics).toBeDefined();
    }
    expect(r.content).toBeUndefined();
  }, 300_000);

  it("render with captureSceneState hands back a blob save_scene_snapshot turns into an asset", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    const r = await canvas("render", { executionMode: "EditorWorld", resolution: SMALL, captureSceneState: true });
    expect(typeof r.sceneStateBlobPath).toBe("string");
    expect(fs.existsSync(r.sceneStateBlobPath)).toBe(true);
    const saved = await canvas("save_scene_snapshot", { sceneStateBlobPath: r.sceneStateBlobPath, destinationPath: DIR, assetName: "SceneState_Live" });
    expect(saved.assetPath).toContain(`${DIR}/SceneState_Live`);
    const listed = await raw("list_assets", { directory: DIR });
    expect((listed.assets as Json[]).map((a) => a.name)).toContain("SceneState_Live");
  }, 300_000);

  it("render_sequence returns one frame directory per capture, with its frames on disk", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    await raw("create_level_sequence", { name: "LS_CanvasRender", packagePath: DIR });
    await raw("set_sequence_playback_range", { sequencePath: SEQ, startSeconds: 0, endSeconds: 0.2 });
    const r = await canvas("render_sequence", { sequencePath: SEQ, resolution: SMALL, cameraSource: { mode: "viewport" } });
    expect(r.passes.length).toBeGreaterThan(0);
    for (const p of r.passes as Json[]) {
      expect(fs.statSync(p.directory).isDirectory(), p.directory).toBe(true);
      expect(p.frameCount).toBeGreaterThan(0);
      expect(fs.existsSync(p.firstFrame), p.firstFrame).toBe(true);
      expect(fs.existsSync(p.lastFrame), p.lastFrame).toBe(true);
    }
  }, 900_000);

  it("a cancelled render leaves the render queue free for the next one", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    // The Bridge's tools do not implement CancelAsync, so a cancel cannot stop a
    // render; what it must not do is wedge the serial queue behind it.
    // A 50 ms budget gives up on the call, and the bridge forwards the cancel to the tool.
    await expect(bridge.call("epic_call_mcp_tool", { tool: "TriggerMRGRender", input: { ExecutionMode: "EditorWorld", Resolution: SMALL } }, 50)).rejects.toThrow();
    const next = await canvas("render", { executionMode: "EditorWorld", resolution: SMALL });
    expect(next.images.length).toBeGreaterThan(0);
  }, 600_000);

  it("starts, retimes and stops the viewport stream", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    const started = await canvas("start_viewport_stream", { width: 128, height: 128, fps: 10, depth: false, normal: false });
    expect(started.streaming).toBe(true);
    expect(typeof started.port).toBe("number");
    expect([9877, bridge.port]).not.toContain(started.port);
    expect((await canvas("set_viewport_stream_fps", { fps: 5 })).fps).toBe(5);
    expect(await canvas("stop_viewport_stream")).toMatchObject({ streaming: false, wasStopped: true });
    const again = await server!.call("canvas", { action: "set_viewport_stream_fps", args: { fps: 5 } });
    expect(resultJson<Json>(again).success).toBe(false);
    expect(again.text).toContain("No viewport stream is running");
  }, 120_000);

  it("assigns and clears a stencil on a placed actor, and applies the capture setters", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    await raw("place_actor", { actorClass: "StaticMeshActor", label: "CanvasStencilCube", location: { x: 0, y: 0, z: 0 } });
    const actorPath = String((await raw("get_actor_details", { actorLabel: "CanvasStencilCube" })).actorPath);
    // The stencil lives on the mesh component, not the actor.
    const stencil = async (): Promise<Json> =>
      (await raw("get_object_properties", { objectPath: `${actorPath}.StaticMeshComponent0`, propertyNames: ["CustomDepthStencilValue", "bRenderCustomDepth"] })).properties as Json;
    try {
      await canvas("assign_stencil", { actorPath, stencilId: 7 });
      expect(await stencil()).toEqual({ CustomDepthStencilValue: "7", bRenderCustomDepth: "True" });
      // Clearing turns custom depth off; the stored value is left as it was.
      await canvas("clear_stencil", { actorPath });
      expect((await stencil()).bRenderCustomDepth).toBe("False");
    } finally {
      await callBridge(bridge, "delete_actor", { actorLabel: "CanvasStencilCube" });
    }
    expect((await canvas("set_stencil_colors", { groups: [{ stencilId: 7, r: 255, g: 0, b: 0 }] })).success).not.toBe(false);
    expect((await canvas("set_capture_annotations", { enabled: true })).enabled).toBe(true);
    expect((await canvas("set_capture_annotations", { enabled: false })).enabled).toBe(false);
    expect((await canvas("set_capture_stage", { stage: "post_tonemap" })).success).not.toBe(false);
  }, 120_000);

  it("spawns a cutout from a PNG, adds and switches a variant, lists and removes it", async () => {
    if (!canvasLoaded) return expectCanvasAbsent();
    // A 1x1 opaque PNG.
    const png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==";
    const spawned = await canvas("spawn_cutout_from_png", { pngBase64: png, location: { x: 0, y: 0, z: 100 }, label: "first" });
    expect(typeof spawned.cutout_id).toBe("string");
    try {
      expect((await canvas("add_cutout_variant", { cutoutId: spawned.cutout_id, pngBase64: png, label: "second" })).variant_index).toBe(1);
      await canvas("set_cutout_variant", { cutoutId: spawned.cutout_id, variantIndex: 1 });
      const listed = (await canvas("list_cutouts")).cutouts as Json[];
      expect(listed.find((c) => c.cutout_id === spawned.cutout_id)).toMatchObject({ variant_count: 2, active_variant_index: 1 });
    } finally {
      await canvas("remove_cutout", { cutoutId: spawned.cutout_id });
    }
    expect(((await canvas("list_cutouts")).cutouts as Json[]).some((c) => c.cutout_id === spawned.cutout_id)).toBe(false);
  }, 120_000);
});
