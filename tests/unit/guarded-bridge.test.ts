import { describe, it, expect, vi } from "vitest";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { GuardRegistry, type BridgeGuard, type CallContext } from "../../src/flow/guard.js";
import type { IBridge } from "../../src/bridge.js";

function fakeInner(result: unknown = { ok: true }): IBridge & { calls: Array<{ method: string; params?: Record<string, unknown> }> } {
  const calls: Array<{ method: string; params?: Record<string, unknown> }> = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    call: async (method, params) => {
      calls.push({ method, params });
      return result;
    },
  };
}

// Treats every /Game/* path as an existing file; null (not on disk) otherwise.
const resolveExisting = (cp: string) => (cp.startsWith("/Game/") ? `C:/proj/Content/${cp.slice(6)}.uasset` : null);

function bridgeWith(...guards: BridgeGuard[]) {
  const inner = fakeInner();
  const reg = new GuardRegistry();
  for (const g of guards) reg.register(g);
  return { inner, gb: new GuardedBridge(inner, reg, resolveExisting) };
}

/** A write-scoped before-guard, like source control. */
function writeGuard(name: string, before: (ctx: CallContext) => Promise<void>): BridgeGuard {
  return { name, appliesTo: (ctx) => ctx.writeFiles().length > 0, before };
}

describe("GuardedBridge pipeline", () => {
  it("is a pass-through when the registry is empty", async () => {
    const { inner, gb } = bridgeWith();
    await gb.call("save_asset", { assetPath: "/Game/Foo" });
    expect(inner.calls).toHaveLength(1);
  });

  it("runs a write-scoped guard's before with resolved existing files", async () => {
    const before = vi.fn(async () => {});
    const { inner, gb } = bridgeWith(writeGuard("sc", before));

    await gb.call("save_asset", { assetPath: "/Game/Foo" });

    expect(before).toHaveBeenCalledTimes(1);
    const ctx: CallContext = before.mock.calls[0][0];
    expect(ctx.method).toBe("save_asset");
    expect(ctx.writeFiles()).toEqual(["C:/proj/Content/Foo.uasset"]);
    expect(inner.calls).toHaveLength(1);
  });

  it("skips a write-scoped guard for reads and for not-yet-existing files", async () => {
    const before = vi.fn(async () => {});
    const { inner, gb } = bridgeWith(writeGuard("sc", before));

    await gb.call("read_asset", { assetPath: "/Game/Foo" });      // read verb -> no write
    await gb.call("duplicate_asset", { destinationPath: "/Other/New" }); // resolves to null (new)

    expect(before).not.toHaveBeenCalled();
    expect(inner.calls).toHaveLength(2);
  });

  it("an every-call guard runs on reads too", async () => {
    const before = vi.fn(async () => {});
    const audit: BridgeGuard = { name: "audit", before };
    const { gb } = bridgeWith(audit);
    await gb.call("read_asset", { assetPath: "/Game/Foo" });
    expect(before).toHaveBeenCalledTimes(1);
  });

  it("a before denial propagates and the inner call never happens", async () => {
    const { inner, gb } = bridgeWith(writeGuard("sc", async () => { throw new Error("locked by another user"); }));
    await expect(gb.call("save_asset", { assetPath: "/Game/Foo" })).rejects.toThrow("locked by another user");
    expect(inner.calls).toHaveLength(0);
  });

  it("runs before in order and after in reverse order", async () => {
    const seq: string[] = [];
    const mk = (name: string, order: number): BridgeGuard => ({
      name, order,
      before: async () => { seq.push(`before:${name}`); },
      after: async () => { seq.push(`after:${name}`); },
    });
    const { gb } = bridgeWith(mk("b", 2), mk("a", 1));
    await gb.call("read_asset", {});
    expect(seq).toEqual(["before:a", "before:b", "after:b", "after:a"]);
  });

  it("an after guard can replace the result", async () => {
    const inner = fakeInner({ original: true });
    const reg = new GuardRegistry();
    reg.register({ name: "wrap", after: async (_ctx, result) => ({ wrapped: result }) });
    const gb = new GuardedBridge(inner, reg, resolveExisting);
    const out = await gb.call("read_asset", {});
    expect(out).toEqual({ wrapped: { original: true } });
  });

  it("delegates connection lifecycle to the inner bridge", async () => {
    const { gb } = bridgeWith();
    expect(gb.isConnected).toBe(true);
    await expect(gb.connect()).resolves.toBeUndefined();
  });
});
