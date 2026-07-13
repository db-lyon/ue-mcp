import { describe, it, expect, vi } from "vitest";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import type { IBridge } from "../../src/bridge.js";

function fakeInner(): IBridge & { calls: Array<{ method: string; params?: Record<string, unknown> }> } {
  const calls: Array<{ method: string; params?: Record<string, unknown> }> = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    call: async (method, params) => {
      calls.push({ method, params });
      return { ok: true };
    },
  };
}

// Resolver that treats every /Game/* path as an existing file, mapping to a
// pseudo absolute path. Returns null for anything else (simulating "not on disk").
const resolveExisting = (cp: string) => (cp.startsWith("/Game/") ? `C:/proj/Content/${cp.slice(6)}.uasset` : null);

describe("GuardedBridge", () => {
  it("is a pass-through when no guard is installed", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, resolveExisting);
    await gb.call("save_asset", { assetPath: "/Game/Foo" });
    expect(inner.calls).toHaveLength(1);
  });

  it("invokes the guard before a write, with resolved existing files", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, resolveExisting);
    const guard = vi.fn(async () => {});
    gb.setGuard(guard);

    await gb.call("save_asset", { assetPath: "/Game/Foo" });

    expect(guard).toHaveBeenCalledTimes(1);
    const info = guard.mock.calls[0][0];
    expect(info.method).toBe("save_asset");
    expect(info.paths).toEqual(["C:/proj/Content/Foo.uasset"]);
    // The inner write still happened, after the guard.
    expect(inner.calls).toHaveLength(1);
  });

  it("does not call the guard for read methods", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, resolveExisting);
    const guard = vi.fn(async () => {});
    gb.setGuard(guard);

    await gb.call("read_asset", { assetPath: "/Game/Foo" });
    expect(guard).not.toHaveBeenCalled();
    expect(inner.calls).toHaveLength(1);
  });

  it("skips the guard when no target file exists yet (create/import)", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, resolveExisting);
    const guard = vi.fn(async () => {});
    gb.setGuard(guard);

    // destinationPath resolves to a non-/Game path -> resolver returns null.
    await gb.call("duplicate_asset", { destinationPath: "/Other/New" });
    expect(guard).not.toHaveBeenCalled();
    expect(inner.calls).toHaveLength(1);
  });

  it("propagates a guard denial and does NOT perform the write", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, resolveExisting);
    gb.setGuard(async () => {
      throw new Error("locked by another user");
    });

    await expect(gb.call("save_asset", { assetPath: "/Game/Foo" })).rejects.toThrow("locked by another user");
    expect(inner.calls).toHaveLength(0);
  });

  it("delegates connection lifecycle to the inner bridge", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, resolveExisting);
    expect(gb.isConnected).toBe(true);
    await expect(gb.connect()).resolves.toBeUndefined();
  });
});
