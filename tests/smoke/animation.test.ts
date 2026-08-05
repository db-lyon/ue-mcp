import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/PSDB_SmokeTest` });
  disconnectBridge();
});

describe("animation - read / list", () => {
  it("list_anim_assets", async () => {
    const r = await callBridge(bridge, "list_anim_assets", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_skeletal_meshes", async () => {
    const r = await callBridge(bridge, "list_skeletal_meshes", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("animation - read specific (dynamic)", () => {
  let skelMeshPath: string | undefined;

  beforeAll(async () => {
    const r = await callBridge(bridge, "list_skeletal_meshes", { recursive: true });
    if (r.ok) {
      const items = resultArray(r.result, "assets", "meshes");
      if (items && items.length > 0) {
        const first = items[0] as Record<string, unknown>;
        skelMeshPath = (first.path ?? first.asset_path ?? first.objectPath) as string | undefined;
      }
    }
  });

  it("get_skeleton_info", async ({ skip }) => {
    if (!skelMeshPath) skip();
    const r = await callBridge(bridge, "get_skeleton_info", { assetPath: skelMeshPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_sockets", async ({ skip }) => {
    if (!skelMeshPath) skip();
    const r = await callBridge(bridge, "list_sockets", { assetPath: skelMeshPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_physics_asset_info", async ({ skip }) => {
    if (!skelMeshPath) skip();
    const r = await callBridge(bridge, "get_physics_asset_info", { assetPath: skelMeshPath });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("animation - v0.7.15: PoseSearch (motion matching)", () => {
  const DB_PATH = `${TEST_PREFIX}/PSDB_SmokeTest`;

  it("create_pose_search_database", async () => {
    const r = await callBridge(bridge, "create_pose_search_database", {
      name: "PSDB_SmokeTest",
      packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_pose_search_database returns schema + counts", async () => {
    const r = await callBridge(bridge, "read_pose_search_database", { assetPath: DB_PATH });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(typeof res.animationAssetCount).toBe("number");
  });

  it("set_pose_search_schema reports missing schema cleanly", async () => {
    const r = await callBridge(bridge, "set_pose_search_schema", {
      assetPath: DB_PATH,
      schemaPath: "/Game/Nonexistent/Schema",
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.success).toBe(false);
    expect(typeof res.error).toBe("string");
  });

  it("add_pose_search_sequence reports missing asset cleanly", async () => {
    const r = await callBridge(bridge, "add_pose_search_sequence", {
      assetPath: DB_PATH,
      sequencePath: "/Game/Nonexistent/Sequence",
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.success).toBe(false);
    expect(typeof res.error).toBe("string");
  });

  it("build_pose_search_index reports missing schema cleanly", async () => {
    const r = await callBridge(bridge, "build_pose_search_index", { assetPath: DB_PATH });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    // Empty DB with no schema - handler must return success=false, not crash
    expect(res.success).toBe(false);
    expect(typeof res.error).toBe("string");
  });
});
