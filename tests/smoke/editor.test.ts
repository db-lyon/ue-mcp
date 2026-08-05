import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("editor - read / query", () => {
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

describe("editor - safe commands", () => {
  it("invoke_object_functions runs an ordered UObject call sequence", async () => {
    const subsystem = {
      target: "subsystem",
      subsystemClass: "/Script/UnrealEd.EditorAssetSubsystem",
    };
    const r = await callBridge(bridge, "invoke_object_functions", {
      world: "editor",
      calls: [
        { ...subsystem, functionName: "DoesAssetExist", args: { AssetPath: "/Engine/BasicShapes/Cube.Cube" } },
        { ...subsystem, functionName: "DoesDirectoryExist", args: { DirectoryPath: "/Engine/BasicShapes" } },
      ],
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(result.completedCalls).toBe(2);
    expect(result.requestedCalls).toBe(2);
    expect(result.results).toHaveLength(2);
  });

  it("invoke_object_functions stops at the first failure and says where", async () => {
    const subsystem = {
      target: "subsystem",
      subsystemClass: "/Script/UnrealEd.EditorAssetSubsystem",
    };
    const r = await callBridge(bridge, "invoke_object_functions", {
      world: "editor",
      calls: [
        { ...subsystem, functionName: "DoesAssetExist", args: { AssetPath: "/Engine/BasicShapes/Cube.Cube" } },
        { ...subsystem, functionName: "NoSuchFunctionOnThisSubsystem" },
        { ...subsystem, functionName: "DoesDirectoryExist", args: { DirectoryPath: "/Engine/BasicShapes" } },
      ],
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(false);
    expect(result.failedIndex).toBe(1);
    // The failing call is reported but not counted as completed, and the third
    // call never runs.
    expect(result.completedCalls).toBe(1);
    expect(result.requestedCalls).toBe(3);
    expect(result.results).toHaveLength(2);
  });

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

describe("editor - open_asset safety (#17)", () => {
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
    // Should either succeed or fail gracefully - not crash
    expect(typeof result.success).toBe("boolean");
  });
});

describe("editor - live object access (#802)", () => {
  it("find_object resolves an exact path", async () => {
    const r = await callBridge(bridge, "find_object", { objectPath: "/Engine/BasicShapes/Cube.Cube" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(result.found).toBe(true);
    expect((result.object as Record<string, unknown>).class).toBe("StaticMesh");
  });

  it("find_object reports a missing path instead of failing the call", async () => {
    const r = await callBridge(bridge, "find_object", { objectPath: "/Game/NoSuchPackage.NoSuchObject" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(result.found).toBe(false);
  });

  it("find_object searches live instances by class", async () => {
    const r = await callBridge(bridge, "find_object", { className: "WorldSettings", world: "editor", limit: 5 });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(result.totalMatches as number).toBeGreaterThan(0);
  });

  it("find_object needs a filter", async () => {
    const r = await callBridge(bridge, "find_object", {});
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(false);
  });
});

describe("editor - live instance writes (#802)", () => {
  it("find_object then set_object_property writes the instance it resolved", async () => {
    const found = await callBridge(bridge, "find_object", { className: "WorldSettings", world: "editor", limit: 1 });
    expect(found.ok, found.error).toBe(true);
    const matches = (found.result as Record<string, unknown>).matches as Array<Record<string, unknown>>;
    expect(matches.length).toBeGreaterThan(0);
    const objectPath = matches[0].objectPath as string;

    const r = await callBridge(bridge, "set_object_property", {
      objectPath,
      propertyName: "TimeDilation",
      value: 1.0,
      world: "editor",
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(result.previousValue).toBeDefined();
    expect(result.persisted).toBe(false);
  });

  it("set_object_property lists the available properties when the name is wrong", async () => {
    const found = await callBridge(bridge, "find_object", { className: "WorldSettings", world: "editor", limit: 1 });
    const matches = (found.result as Record<string, unknown>).matches as Array<Record<string, unknown>>;
    const r = await callBridge(bridge, "set_object_property", {
      objectPath: matches[0].objectPath as string,
      propertyName: "NoSuchVariableOnThisClass",
      value: 1,
      world: "editor",
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(false);
    expect(String(result.error)).toContain("Available:");
  });
});
