import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("editor — read / query", () => {
  it("get_viewport_info", async () => {
    const r = await callBridge(bridge, "get_viewport_info");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_editor_performance_stats", async () => {
    const r = await callBridge(bridge, "get_editor_performance_stats");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_output_log", async () => {
    const r = await callBridge(bridge, "get_output_log", { maxLines: 20 });
    expect(r.ok, r.error).toBe(true);
  });

  it("search_log", async () => {
    const r = await callBridge(bridge, "search_log", { query: "MCP" });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_message_log", async () => {
    const r = await callBridge(bridge, "get_message_log");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_build_status", async () => {
    const r = await callBridge(bridge, "get_build_status");
    expect(r.ok, r.error).toBe(true);
  });

  it("pie_control (status)", async () => {
    const r = await callBridge(bridge, "pie_control", { action: "status" });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("editor — safe commands", () => {
  it("execute_python (simple)", async () => {
    const r = await callBridge(bridge, "execute_python", { code: "result = 1 + 1" });
    expect(r.ok, r.error).toBe(true);
  });

  it("execute_command (stat fps)", async () => {
    const r = await callBridge(bridge, "execute_command", { command: "stat fps" });
    expect(r.ok, r.error).toBe(true);
  });

  it("capture_screenshot", async () => {
    const r = await callBridge(bridge, "capture_screenshot", { filename: "mcp_smoke_test" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.target).toBe("editor");
    expect(result.sizeBytes).toBeGreaterThan(0);
  });

  it("capture_screenshot target=pie captures a file", async () => {
    const before = await callBridge(bridge, "pie_control", { action: "status" });
    expect(before.ok, before.error).toBe(true);
    const wasPlaying = Boolean((before.result as Record<string, unknown>).isPlaying);

    if (!wasPlaying) {
      const start = await callBridge(bridge, "pie_control", { action: "start" });
      expect(start.ok, start.error).toBe(true);
      for (let i = 0; i < 20; i++) {
        const status = await callBridge(bridge, "pie_control", { action: "status" });
        if ((status.result as Record<string, unknown>).isPlaying) break;
        await new Promise((resolve) => setTimeout(resolve, 500));
      }
    }

    try {
      const r = await callBridge(bridge, "capture_screenshot", {
        filename: "mcp_pie_view_smoke.png",
        target: "pie",
      });
      expect(r.ok, r.error).toBe(true);
      const result = r.result as Record<string, unknown>;
      expect(result.target).toBe("pie");
      expect(result.sizeBytes).toBeGreaterThan(0);
    } finally {
      if (!wasPlaying) {
        await callBridge(bridge, "pie_control", { action: "stop" });
      }
    }
  }, 60000);

  it("capture_scene_png", async () => {
    const r = await callBridge(bridge, "capture_scene_png", {
      outputPath: "Saved/Screenshots/mcp_scene_smoke.png",
      location: { x: 0, y: -300, z: 200 },
      rotation: { pitch: -20, yaw: 90, roll: 0 },
      width: 320,
      height: 180,
      world: "editor",
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).sizeBytes).toBeGreaterThan(0);
  });

  it("set_viewport_camera", async () => {
    const r = await callBridge(bridge, "set_viewport_camera", {
      location: { x: 0, y: 0, z: 300 },
      rotation: { pitch: -30, yaw: 0, roll: 0 },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("undo", async () => {
    const r = await callBridge(bridge, "undo");
    expect(r.ok, r.error).toBe(true);
  });

  it("redo", async () => {
    const r = await callBridge(bridge, "redo");
    expect(r.ok, r.error).toBe(true);
  });

  it("reload_handlers", async () => {
    const r = await callBridge(bridge, "reload_handlers");
    expect(r.ok, r.error).toBe(true);
  });
});

describe("editor — open_asset safety (#17)", () => {
  it("open_asset returns success:false instead of crashing for missing asset", async () => {
    const r = await callBridge(bridge, "open_asset", { assetPath: "/Game/DoesNotExist/SM_Nope" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(false);
    expect(result.error).toBeDefined();
  });

  it("open_asset does not crash on StaticMesh", async () => {
    // Create a simple static mesh import target or use engine content
    // Just verify the call returns without crashing the bridge
    const r = await callBridge(bridge, "open_asset", { assetPath: "/Engine/BasicShapes/Cube" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    // Should either succeed or fail gracefully — not crash
    expect(typeof result.success).toBe("boolean");
  });
});
