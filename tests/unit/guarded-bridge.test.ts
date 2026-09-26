import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, it, expect, vi } from "vitest";
import { DialogGatedBridge, GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { GuardRegistry, type BridgeGuard, type CallContext } from "../../src/flow/guard.js";
import { CLIENT_PROTOCOL_VERSION, type BridgeCapabilities, type IBridge } from "../../src/bridge/bridge.js";
import { ProjectContext } from "../../src/config/project.js";
import { projectTool } from "../../src/tools/project.js";
import { ALL_TOOLS } from "../../src/tools.js";
import type { EditorSession } from "../../src/session.js";

function fakeInner(result: unknown = { ok: true }): IBridge & { calls: Array<{ method: string; params?: Record<string, unknown> }> } {
  const calls: Array<{ method: string; params?: Record<string, unknown> }> = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
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
    const before = vi.fn(async (_ctx: CallContext) => {});
    const { inner, gb } = bridgeWith(writeGuard("sc", before));

    await gb.call("save_asset", { assetPath: "/Game/Foo" });

    expect(before).toHaveBeenCalledTimes(1);
    const ctx: CallContext = before.mock.calls[0][0];
    expect(ctx.method).toBe("save_asset");
    expect(ctx.writeFiles()).toEqual(["C:/proj/Content/Foo.uasset"]);
    expect(inner.calls).toHaveLength(1);
  });

  it("skips a write-scoped guard for reads and for not-yet-existing files", async () => {
    const before = vi.fn(async (_ctx: CallContext) => {});
    const { inner, gb } = bridgeWith(writeGuard("sc", before));

    await gb.call("read_asset", { assetPath: "/Game/Foo" });      // read verb -> no write
    await gb.call("duplicate_asset", { destinationPath: "/Other/New" }); // resolves to null (new)

    expect(before).not.toHaveBeenCalled();
    expect(inner.calls).toHaveLength(2);
  });

  it("an every-call guard runs on reads too", async () => {
    const before = vi.fn(async (_ctx: CallContext) => {});
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

  it("keeps live capabilities visible through both wrappers", () => {
    let capabilities: BridgeCapabilities | null | undefined;
    const inner = {
      ...fakeInner(),
      get capabilities() { return capabilities; },
    };
    const guarded = new GuardedBridge(inner, new GuardRegistry(), resolveExisting);
    const dialogGated = new DialogGatedBridge(inner, {} as EditorSession);

    for (const current of [
      undefined,
      null,
      { protocolVersion: 2, legacy: false, builtAt: "first" },
      { protocolVersion: 2, legacy: false, builtAt: "replacement" },
      null,
    ] satisfies Array<BridgeCapabilities | null | undefined>) {
      capabilities = current;
      expect(guarded.capabilities).toBe(current);
      expect(dialogGated.capabilities).toBe(current);
    }
    expect(inner.calls).toHaveLength(0);
  });
});

describe("project(get_status) through the session's guarded bridge", () => {
  // ctx.bridge is session.guarded, so the handshake reaches get_status only if
  // the wrapper forwards capabilities.
  it("reports bridgeProtocol and deployedPlugin from the wrapped bridge", async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-guarded-status-"));
    const uproject = path.join(root, "Probe.uproject");
    fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8" }));
    const project = new ProjectContext();
    project.setProject(uproject);

    const capabilities: BridgeCapabilities = {
      protocolVersion: CLIENT_PROTOCOL_VERSION,
      legacy: false,
      builtAt: "2026-09-01T00:00:00Z",
      actionCount: 42,
    };
    const inner = { ...fakeInner(), capabilities };
    const guarded = new GuardedBridge(inner, new GuardRegistry(), resolveExisting);

    const status = (await projectTool.handler({ project, bridge: guarded, getToolGraph: () => ALL_TOOLS } as never, {
      action: "get_status",
    })) as Record<string, unknown>;

    expect(status.bridgeProtocol).toEqual({
      plugin: CLIENT_PROTOCOL_VERSION,
      client: CLIENT_PROTOCOL_VERSION,
      builtAt: "2026-09-01T00:00:00Z",
      actionCount: 42,
    });
    expect(status.deployedPlugin).toEqual({ builtAt: "2026-09-01T00:00:00Z" });
    expect(inner.calls).toHaveLength(0);
  });
});
