import { describe, expect, it, vi } from "vitest";
import { classifyAction, resolveLockingConfig, withAssetLocks } from "../../src/locking.js";
import type { IBridge } from "../../src/bridge.js";
import { McpError, ErrorCode } from "../../src/errors.js";

describe("classifyAction", () => {
  it("treats read verbs as non-mutating", () => {
    for (const t of ["asset.list", "asset.read", "asset.get_properties", "blueprint.search_node_types", "asset.list_locks"]) {
      expect(classifyAction(t, { assetPath: "/Game/Foo" }).mutates).toBe(false);
    }
  });

  it("classifies mutating verbs and extracts the asset path", () => {
    const c = classifyAction("asset.set_property", { assetPath: "/Game/Foo", propertyName: "X" });
    expect(c.mutates).toBe(true);
    expect(c.paths).toEqual(["/Game/Foo"]);
  });

  it("extracts source + destination for rename/move", () => {
    const c = classifyAction("asset.move", { sourcePath: "/Game/A", destinationPath: "/Game/B" });
    expect(c.mutates).toBe(true);
    expect(new Set(c.paths)).toEqual(new Set(["/Game/A", "/Game/B"]));
  });

  it("extracts batch paths and rename lists", () => {
    expect(classifyAction("asset.delete_batch", { assetPaths: ["/Game/A", "/Game/B"] }).paths.sort()).toEqual(["/Game/A", "/Game/B"]);
    expect(classifyAction("asset.bulk_rename", { renames: [{ sourcePath: "/Game/A" }, { assetPath: "/Game/C" }] }).paths.sort()).toEqual(["/Game/A", "/Game/C"]);
  });

  it("extracts asset paths from bulk property descriptors", () => {
    const c = classifyAction("asset.bulk_set_properties", {
      items: [
        { assetPath: "/Game/A", properties: { Enabled: true } },
        { assetPath: "/Game/B", properties: { "Config.Weight": 2 } },
      ],
    });
    expect(c.mutates).toBe(true);
    expect(c.paths.sort()).toEqual(["/Game/A", "/Game/B"]);
  });

  it("does not treat a filesystem source (filePath) as an asset path", () => {
    // import uses filePath (disk) + packagePath; we only lock things that look
    // like content paths pulled from known asset-path keys.
    const c = classifyAction("asset.import_texture", { filePath: "C:/tmp/x.png", name: "X" });
    expect(c.paths).toEqual([]);
  });

  it("locks an action this server does not carry, rather than failing open on its verb", () => {
    // This used to answer `false`, because "frobnicate" was in neither verb
    // list. The verdict is the action's declared effect now, and a name no
    // action in this server declares gets the same `mutate` default every
    // other gate gives it: nothing vouches for it, so it is treated as a
    // change. The path is what keeps this narrow, not the verdict.
    expect(classifyAction("asset.frobnicate", { assetPath: "/Game/Foo" }).mutates).toBe(true);
    expect(classifyAction("asset.frobnicate", { assetPath: "/Game/Foo" }).paths).toEqual(["/Game/Foo"]);
    // With no asset path to lock it still runs unlocked, which is what stops a
    // conservative verdict from serialising the whole surface.
    expect(classifyAction("asset.frobnicate", { name: "X" }).paths).toEqual([]);
  });

  it("locks a declared mutation whose verb no list ever had", () => {
    // `unwrap_uvs` rewrites a mesh's UV layout in place and `fixup_redirectors`
    // loads, rewrites and saves every referencing package. Neither verb was in
    // MUTATE_PREFIXES, so neither ever took a lock.
    expect(classifyAction("asset.unwrap_uvs", { assetPath: "/Game/SM_Rock" }).mutates).toBe(true);
    expect(classifyAction("asset.fixup_redirectors", { assetPath: "/Game/Props" }).mutates).toBe(true);
  });

  it("does not lock a declared read whose name opens with a mutating verb", () => {
    // "bulk" is a mutate verb because most bulk_* actions write. This one only
    // reads, and says so.
    expect(classifyAction("asset.bulk_read_properties", { assetPath: "/Game/Foo" }).mutates).toBe(false);
  });

  it("does not lock the explicit lock/unlock actions themselves", () => {
    expect(classifyAction("asset.lock", { assetPath: "/Game/Foo" }).mutates).toBe(false);
    expect(classifyAction("asset.unlock", { assetPath: "/Game/Foo" }).mutates).toBe(false);
  });
});

function fakeBridge(calls: Array<[string, Record<string, unknown>]>, responder: (method: string, params: Record<string, unknown>) => unknown): IBridge {
  return {
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    call: async (method: string, params?: Record<string, unknown>) => {
      calls.push([method, params ?? {}]);
      return responder(method, params ?? {});
    },
  };
}

describe("withAssetLocks", () => {
  const cfg = resolveLockingConfig({ enabled: true });

  it("is a passthrough when disabled", async () => {
    const calls: Array<[string, Record<string, unknown>]> = [];
    const bridge = fakeBridge(calls, () => ({ acquired: true }));
    const run = vi.fn(async () => "done");
    const out = await withAssetLocks(bridge, resolveLockingConfig({ enabled: false }), "asset.set_property", { assetPath: "/Game/Foo" }, run);
    expect(out).toBe("done");
    expect(calls).toHaveLength(0);
  });

  it("acquires then releases around a mutating call", async () => {
    const calls: Array<[string, Record<string, unknown>]> = [];
    const bridge = fakeBridge(calls, () => ({ acquired: true }));
    const out = await withAssetLocks(bridge, cfg, "asset.set_property", { assetPath: "/Game/Foo" }, async () => "ok");
    expect(out).toBe("ok");
    expect(calls.map((c) => c[0])).toEqual(["acquire_lock", "release_lock"]);
  });

  it("throws a retryable ASSET_LOCKED error when busy, releasing nothing it holds", async () => {
    const calls: Array<[string, Record<string, unknown>]> = [];
    const bridge = fakeBridge(calls, (m) => (m === "acquire_lock" ? { acquired: false, holder: { sessionId: "other", ttlSecondsRemaining: 12 } } : {}));
    await expect(
      withAssetLocks(bridge, cfg, "asset.set_property", { assetPath: "/Game/Foo" }, async () => "ok"),
    ).rejects.toMatchObject({ code: ErrorCode.ASSET_LOCKED });
    // Only the failed acquire happened; nothing to release.
    expect(calls.map((c) => c[0])).toEqual(["acquire_lock"]);
  });

  it("fails open (runs unlocked) when the lock subsystem errors", async () => {
    const calls: Array<[string, Record<string, unknown>]> = [];
    const bridge = fakeBridge(calls, (m) => {
      if (m === "acquire_lock") throw new McpError(ErrorCode.BRIDGE_ERROR, "Unknown method: acquire_lock");
      return {};
    });
    const out = await withAssetLocks(bridge, cfg, "asset.set_property", { assetPath: "/Game/Foo" }, async () => "ran");
    expect(out).toBe("ran");
  });

  it("releases the first lock if a later acquire in a batch is busy", async () => {
    const calls: Array<[string, Record<string, unknown>]> = [];
    const bridge = fakeBridge(calls, (m, p) => {
      if (m === "acquire_lock") return { acquired: p.path === "/Game/A" };
      return {};
    });
    await expect(
      withAssetLocks(bridge, cfg, "asset.delete_batch", { assetPaths: ["/Game/A", "/Game/B"] }, async () => "ok"),
    ).rejects.toMatchObject({ code: ErrorCode.ASSET_LOCKED });
    // Acquired A, failed B, then released A.
    expect(calls.map((c) => `${c[0]}:${c[1].path}`)).toEqual([
      "acquire_lock:/Game/A",
      "acquire_lock:/Game/B",
      "release_lock:/Game/A",
    ]);
  });
});
