/**
 * The canvas category (#1824): typed fronts for the Unreal Canvas Bridge's MCP
 * tools, each dispatched through epic_call_mcp_tool with the tool's own input
 * names, results shaped into paths, and withheld while the editor's MCP tool
 * list lacks the tool.
 */
import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { canvasTool, vec3 } from "../../src/tools/canvas.js";
import { frameFile, shapeCanvasAnswer } from "../../src/tools/canvas-results.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { epicMcpToolNames } from "../../src/bridge/bridge.js";
import {
  absentMcpTool,
  registeredMethodsOf,
  registeredUnion,
  runsEpicMcpTool,
  withheldTarget,
  withholdFromTool,
} from "../../src/surface/registered-surface.js";
import type { BridgeActionSpec } from "../../src/core/types.js";

const spec = (action: string) => canvasTool.actions[action] as BridgeActionSpec;
const envelope = (action: string, p: Record<string, unknown>) => spec(action).mapParams!(p) as { tool: string; input: Record<string, unknown> };

/** The input property names each wrapped tool declares in the UE 5.9 Bridge source. */
const TOOL_INPUTS: Record<string, string[]> = {
  GetStatus: [],
  GetMRGPasses: ["PresetPath", "Variables"],
  GetMRGVariables: ["PresetPath"],
  TriggerMRGRender: ["PresetPath", "Variables", "Resolution", "CameraSource", "ViewportLookFlags", "ExecutionMode", "CaptureSceneState"],
  TriggerSequenceRender: ["SequencePath", "PresetPath", "Variables", "Resolution", "CameraSource", "ViewportLookFlags"],
  BuildAndRenderRepositionSequence: ["sceneStateBlobPath", "cameraPose", "durationSeconds", "fps", "saveSequence", "PresetPath", "Variables", "Resolution"],
  SaveSceneSnapshot: ["sceneStateBlobPath", "DestinationPath", "AssetName", "SkipDialog"],
  StartViewportStream: ["port", "width", "height", "fps", "enable_color", "enable_depth", "enable_normal", "enable_stencil", "shared_memory", "exact_size"],
  SetViewportStreamFps: ["fps"],
  StopViewportStream: [],
  AssignStencilID: ["actorId", "actorName", "stencilId"],
  ClearStencilID: ["actorId", "actorName"],
  ClearAllStencils: ["actorIds", "actorNames"],
  SetStencilColorMap: ["groups"],
  SetDepthRange: ["nearCm", "farCm", "encoding"],
  SetCaptureStage: ["stage"],
  SetCaptureAnnotations: ["enabled"],
  SetShowAnnotationsInViewport: ["enabled"],
  SetAnnotationRenderMode: ["fill", "edgeThickness"],
  SpawnGsplatFromPLY: ["ply_path", "location", "rotation", "actor_tag", "label", "replace_existing"],
  ListGsplats: [],
  RemoveGsplat: ["gsplat_id"],
  SpawnCutoutAt: ["texture_paths", "labels", "location", "rotation", "intensity"],
  SpawnCutoutFromPNG: ["png_bytes_base64", "location", "rotation", "label", "intensity", "provenance_path"],
  ListCutouts: [],
  RemoveCutout: ["cutout_id"],
  AddCutoutVariant: ["cutout_id", "png_bytes_base64", "label"],
  SetCutoutActiveVariant: ["cutout_id", "variant_index"],
};

/** A value for every declared category parameter, so each mapper emits every key it can. */
function everyParam(): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const key of Object.keys(canvasTool.schema)) out[key] = key === "location" || key === "rotation" ? [1, 2, 3] : `v_${key}`;
  out.pngBase64 = "AAAA";
  return out;
}

describe("canvas category", () => {
  it("is registered, and every action runs a Canvas Bridge tool through epic_call_mcp_tool", () => {
    expect(ALL_TOOLS.map((t) => t.name)).toContain("canvas");
    for (const [action, s] of Object.entries(canvasTool.actions)) {
      expect(s.kind, action).toBe("bridge");
      const b = s as BridgeActionSpec;
      expect(b.bridge, action).toBe("epic_call_mcp_tool");
      expect(b.mcpTool?.plugin, action).toBe("UnrealCanvasBridge");
      expect(Object.keys(TOOL_INPUTS), action).toContain(b.mcpTool!.name);
      expect(b.description, action).toMatch(/Params: /);
      expect(runsEpicMcpTool(ALL_TOOLS, `canvas.${action}`)).toBe(true);
    }
  });

  it("sends only input names the wrapped tool declares", () => {
    const p = everyParam();
    for (const action of Object.keys(canvasTool.actions)) {
      const { tool, input } = envelope(action, p);
      expect(tool).toBe(spec(action).mcpTool!.name);
      for (const key of Object.keys(input)) expect(TOOL_INPUTS[tool], `${action} sends ${key}`).toContain(key);
    }
  });

  it("maps typed params onto the tool's names and drops what was not set", () => {
    expect(envelope("render", { presetPath: "/Game/MRG", executionMode: "EditorWorld", captureSceneState: true })).toEqual({
      tool: "TriggerMRGRender",
      input: { PresetPath: "/Game/MRG", ExecutionMode: "EditorWorld", CaptureSceneState: true },
    });
    expect(envelope("assign_stencil", { actorPath: "/Game/M.M:PersistentLevel.Cube", stencilId: 3 }).input)
      .toEqual({ actorId: "/Game/M.M:PersistentLevel.Cube", stencilId: 3 });
    expect(envelope("spawn_splat", { plyPath: "C:/s.ply", location: { x: 1, y: 2, z: 3 }, rotation: { pitch: 0, yaw: 90, roll: 0 } }).input)
      .toEqual({ ply_path: "C:/s.ply", location: [1, 2, 3], rotation: [0, 90, 0] });
    expect(envelope("render_reposition", { sceneStateBlobPath: "C:/b.bin", cameraPose: { transform: {} } }).input)
      .toEqual({ sceneStateBlobPath: "C:/b.bin", cameraPose: "{\"transform\":{}}" });
    expect(envelope("status", {})).toEqual({ tool: "GetStatus", input: {} });
  });

  it("saves a snapshot without the modal dialog unless asked", () => {
    expect(envelope("save_scene_snapshot", { sceneStateBlobPath: "b" }).input.SkipDialog).toBe(true);
    expect(envelope("save_scene_snapshot", { sceneStateBlobPath: "b", showDialog: true }).input.SkipDialog).toBe(false);
  });

  it("reads a PNG from pngPath, and says so when it cannot", () => {
    const file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-canvas-")), "c.png");
    fs.writeFileSync(file, Buffer.from([0x89, 0x50, 0x4e, 0x47]));
    expect(envelope("add_cutout_variant", { cutoutId: "id", pngPath: file }).input.png_bytes_base64).toBe("iVBORw==");
    expect(() => envelope("add_cutout_variant", { cutoutId: "id", pngPath: file + ".missing" })).toThrow(/pngPath could not be read/);
    expect(() => envelope("add_cutout_variant", { cutoutId: "id" })).toThrow(/pngPath .* or pngBase64/);
  });

  it("gives renders long timeouts and marks removals destructive", () => {
    expect(spec("render").timeoutMs).toBe(600_000);
    expect(spec("render_sequence").timeoutMs).toBe(900_000);
    expect(spec("remove_splat").destructive).toBe(true);
    expect(spec("list_splats").effect).toBe("read");
    expect(vec3(undefined)).toBeUndefined();
    expect(() => vec3("x")).toThrow();
  });
});

describe("canvas results", () => {
  const block = (text: string, annotations?: Record<string, unknown>) => ({ type: "text", text, annotations });

  it("turns a still render into image paths with their passes and camera", () => {
    const shaped = shapeCanvasAnswer("still", {
      success: true,
      tool: "TriggerMRGRender",
      content: [
        block("C:/P/Saved/Tmp/RawDepth.npy", { passName: "RawDepth", subResourceName: "", Camera: "{\"intrinsics\":{\"fov_h\":90}}" }),
        block("C:/P/Saved/Tmp/Beauty.png", { passName: "Beauty", branchName: "Beauty", subResourceName: "beauty", Camera: "{\"intrinsics\":{\"fov_h\":90}}", sceneStateBlobPath: "C:/P/blob.bin" }),
      ],
    }) as Record<string, unknown>;
    expect(shaped.images).toEqual([{
      path: "C:/P/Saved/Tmp/Beauty.png", passName: "Beauty", branchName: "Beauty", subResourceName: "beauty", camera: { intrinsics: { fov_h: 90 } },
    }]);
    expect(shaped.rawDepth).toEqual([{ path: "C:/P/Saved/Tmp/RawDepth.npy", passName: "RawDepth", camera: { intrinsics: { fov_h: 90 } } }]);
    expect(shaped.sceneStateBlobPath).toBe("C:/P/blob.bin");
    expect(shaped.content).toBeUndefined();
  });

  it("turns a sequence render into frame directories with first and last frame paths", () => {
    const shaped = shapeCanvasAnswer("sequence", {
      success: true,
      content: [block("C:\\P\\Saved\\Seq\\Beauty", { passName: "Beauty", frameCount: 3, fps: 24, filePattern: "frame.%04d.png", startFrame: 10, sequenceName: "Shot" })],
    }) as { passes: Array<Record<string, unknown>> };
    expect(shaped.passes[0]).toMatchObject({
      directory: "C:\\P\\Saved\\Seq\\Beauty",
      firstFrame: "C:\\P\\Saved\\Seq\\Beauty\\frame.0010.png",
      lastFrame: "C:\\P\\Saved\\Seq\\Beauty\\frame.0012.png",
      frameCount: 3,
    });
    expect(frameFile("f.%d.png", 7)).toBe("f.7.png");
  });

  it("merges a JSON answer, keeps an object path, and passes failures through", () => {
    expect(shapeCanvasAnswer("json", { success: true, tool: "GetMRGPasses", content: [block("{\"passes\":[\"Beauty\"]}")] }))
      .toEqual({ success: true, tool: "GetMRGPasses", passes: ["Beauty"] });
    expect(shapeCanvasAnswer("assetPath", { success: true, content: [block("/Game/Gen/SceneState/S.S")] }))
      .toEqual({ success: true, assetPath: "/Game/Gen/SceneState/S.S" });
    const failed = { success: false, error: "Tool 'TriggerMRGRender' failed: No MRG preset configured." };
    expect(shapeCanvasAnswer("still", failed)).toBe(failed);
  });
});

describe("withholding canvas actions when the Bridge is not loaded", () => {
  const methods = ["epic_call_mcp_tool", "epic_list_mcp_tools"];

  it("reads tool names off epic_list_mcp_tools, a plugin that is off as none", () => {
    expect(epicMcpToolNames({ success: true, available: true, tools: [{ name: "TriggerMRGRender" }, { name: "GetStatus" }] }))
      .toEqual(["GetStatus", "TriggerMRGRender"]);
    expect(epicMcpToolNames({ available: false, reason: "off", tools: [] })).toEqual([]);
    expect(epicMcpToolNames({ success: false })).toBeNull();
  });

  it("withholds nothing while the list is unknown, and the whole category when the Bridge is absent", () => {
    expect(withholdFromTool(canvasTool, registeredMethodsOf({ registeredActions: methods, epicMcpTools: null }))).toBe(canvasTool);
    expect(withholdFromTool(canvasTool, registeredMethodsOf({ registeredActions: methods, epicMcpTools: ["SomeOtherTool"] }))).toBeNull();
    const partial = withholdFromTool(canvasTool, registeredMethodsOf({ registeredActions: methods, epicMcpTools: ["GetStatus", "TriggerMRGRender"] }))!;
    expect(Object.keys(partial.actions)).toEqual(["status", "render"]);
  });

  it("refuses a withheld call naming the plugin to enable", () => {
    const message = withheldTarget(ALL_TOOLS, "canvas.render", { registeredActions: methods, epicMcpTools: [] });
    expect(message).toContain("'TriggerMRGRender'");
    expect(message).toContain("Enable UnrealCanvasBridge");
    expect(message).toContain("epic(list_mcp_tools)");
    expect(withheldTarget(ALL_TOOLS, "canvas.render", { registeredActions: methods, epicMcpTools: ["TriggerMRGRender"] })).toBeNull();
  });

  it("judges a tool absent across editors only when every editor's list is known", () => {
    const known = { registeredActions: methods, epicMcpTools: [] as string[] };
    const unknown = { registeredActions: methods, epicMcpTools: null };
    expect(absentMcpTool(spec("render"), registeredUnion([known, unknown]))).toBeUndefined();
    expect(absentMcpTool(spec("render"), registeredUnion([known, { ...known }]))).toBe("TriggerMRGRender");
  });
});
