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
