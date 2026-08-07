/**
 * The bridge's binding of flowkit's task-guard discovery: the `write` scope,
 * the option shape a guard task is handed, the per-session context, and the
 * mapping from a denial to an McpError the tool layer renders.
 */
import { describe, it, expect } from "vitest";
import { BaseTask, TaskRegistry, type TaskResult, type TaskConstructor } from "@db-lyon/flowkit";
import { discoverTaskGuards } from "../../src/flow/task-guards.js";
import { GuardRegistry, makeCallContext, type CallContext } from "../../src/flow/guard.js";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { McpError, ErrorCode } from "../../src/errors.js";
import type { IBridge } from "../../src/bridge.js";
import type { ToolContext } from "../../src/types.js";

const target = { projectPath: null, port: 0, portSource: "default" as const, verified: true };

function fakeBridge(result: unknown = { ok: true }) {
  const calls: string[] = [];
  const bridge: IBridge & { calls: string[] } = {
    calls,
    isConnected: true,
    connect: async () => {},
    retargetProject: () => target,
    getTarget: () => target,
    call: async (method: string) => {
      calls.push(method);
      return result;
    },
  };
  return bridge;
}

/** Every /Game/* path exists on disk; anything else is a not-yet-created asset. */
const resolveExisting = (cp: string) =>
  cp.startsWith("/Game/") ? `C:/proj/Content/${cp.slice(6)}.uasset` : null;

const ctx = { bridge: fakeBridge(), project: {} } as unknown as ToolContext;

/** Records what each guard task was handed, so a test can assert the option shape. */
const seen: Array<Record<string, unknown>> = [];

function guardTask(outcome: TaskResult): TaskConstructor {
  class Stub extends BaseTask {
    get taskName() {
      return "guard-stub";
    }
    async execute(): Promise<TaskResult> {
      seen.push(this.options as Record<string, unknown>);
      return outcome;
    }
  }
  return Stub as unknown as TaskConstructor;
}

const ALLOW: TaskResult = { success: true };
const DENY: TaskResult = { success: false, error: new Error("checked out by alice") };

function registryWith(entries: Record<string, TaskResult>): TaskRegistry {
  const reg = new TaskRegistry();
  for (const [name, outcome] of Object.entries(entries)) reg.register(name, guardTask(outcome));
  return reg;
}

function callCtx(method: string, params: Record<string, unknown> = {}): CallContext {
  return makeCallContext(method, params, undefined, fakeBridge(), resolveExisting);
}

describe("discoverTaskGuards", () => {
  it("discovers guard.* tasks and ignores everything else", () => {
    const reg = registryWith({ "guard.p4.beforeWrite": ALLOW, deploy: ALLOW });
    const guards = discoverTaskGuards(reg, ctx, fakeBridge());
    expect(guards.map((g) => g.name)).toEqual(["p4.beforeWrite"]);
  });

  it("binds the Write suffix to the bridge's own write classification", async () => {
    const reg = registryWith({ "guard.p4.beforeWrite": ALLOW });
    const [guard] = discoverTaskGuards(reg, ctx, fakeBridge());

    // A read verb is not a write.
    expect(await guard.appliesTo!(callCtx("get_asset", { assetPath: "/Game/Foo" }))).toBe(false);
    // A write to a path that does not exist yet is a create, not a modify.
    expect(await guard.appliesTo!(callCtx("save_asset", { assetPath: "/Other/New" }))).toBe(false);
    // A write to an existing asset is what the scope claims.
    expect(await guard.appliesTo!(callCtx("save_asset", { assetPath: "/Game/Foo" }))).toBe(true);
  });

  it("hands the guard task the method, params and resolved paths", async () => {
    seen.length = 0;
    const reg = registryWith({ "guard.p4.beforeWrite": ALLOW });
    const [guard] = discoverTaskGuards(reg, ctx, fakeBridge());

    await guard.before!(callCtx("save_asset", { assetPath: "/Game/Foo" }));

    expect(seen).toEqual([
      {
        method: "save_asset",
        params: { assetPath: "/Game/Foo" },
        paths: ["C:/proj/Content/Foo.uasset"],
      },
    ]);
  });

  it("adds the result for an after guard only", async () => {
    seen.length = 0;
    const reg = registryWith({ "guard.audit.after": ALLOW });
    const [guard] = discoverTaskGuards(reg, ctx, fakeBridge());

    await guard.after!(callCtx("save_asset", { assetPath: "/Game/Foo" }), { saved: true });

    expect(seen[0].result).toEqual({ saved: true });
  });

  it("rejects with a WRITE_BLOCKED McpError naming the method and the files", async () => {
    const reg = registryWith({ "guard.p4.beforeWrite": DENY });
    const [guard] = discoverTaskGuards(reg, ctx, fakeBridge());

    const err = await guard
      .before!(callCtx("save_asset", { assetPath: "/Game/Foo" }))
      .then(() => null)
      .catch((e: unknown) => e as McpError);

    expect(err).toBeInstanceOf(McpError);
    expect(err!.code).toBe(ErrorCode.WRITE_BLOCKED);
    expect(err!.message).toBe(
      "blocked (save_asset) on C:/proj/Content/Foo.uasset: checked out by alice",
    );
  });

  it("omits the file list when an unscoped guard denies a read", async () => {
    const reg = registryWith({ "guard.policy.before": DENY });
    const [guard] = discoverTaskGuards(reg, ctx, fakeBridge());

    await expect(guard.before!(callCtx("get_asset", { assetPath: "/Game/Foo" }))).rejects.toThrow(
      "blocked (get_asset): checked out by alice",
    );
  });

  it("does not fail a completed call when an after guard reports failure", async () => {
    const reg = registryWith({ "guard.audit.after": DENY });
    const [guard] = discoverTaskGuards(reg, ctx, fakeBridge());
    await expect(guard.after!(callCtx("save_asset"), 1)).resolves.toBeUndefined();
  });

  it("throws at startup when a task names a scope the bridge does not declare", () => {
    const reg = registryWith({ "guard.p4.beforeCheckout": ALLOW });
    expect(() => discoverTaskGuards(reg, ctx, fakeBridge())).toThrow(
      /scope 'checkout'.*Known scopes: write/s,
    );
  });
});

describe("task guards through GuardedBridge", () => {
  it("denies the write before it reaches the editor", async () => {
    const inner = fakeBridge();
    const guards = new GuardRegistry().registerAll(
      discoverTaskGuards(registryWith({ "guard.p4.beforeWrite": DENY }), ctx, inner),
    );
    const gb = new GuardedBridge(inner, guards, resolveExisting);

    await expect(gb.call("save_asset", { assetPath: "/Game/Foo" })).rejects.toThrow(
      /checked out by alice/,
    );
    expect(inner.calls).toEqual([]);
  });

  it("lets a read through a write-scoped guard untouched", async () => {
    seen.length = 0;
    const inner = fakeBridge();
    const guards = new GuardRegistry().registerAll(
      discoverTaskGuards(registryWith({ "guard.p4.beforeWrite": DENY }), ctx, inner),
    );
    const gb = new GuardedBridge(inner, guards, resolveExisting);

    await expect(gb.call("get_asset", { assetPath: "/Game/Foo" })).resolves.toEqual({ ok: true });
    expect(inner.calls).toEqual(["get_asset"]);
    expect(seen).toHaveLength(0);
  });

  it("allows the write and then audits the result", async () => {
    seen.length = 0;
    const inner = fakeBridge({ saved: true });
    const guards = new GuardRegistry().registerAll(
      discoverTaskGuards(
        registryWith({ "guard.p4.beforeWrite": ALLOW, "guard.audit.after": ALLOW }),
        ctx,
        inner,
      ),
    );
    const gb = new GuardedBridge(inner, guards, resolveExisting);

    const out = await gb.call("save_asset", { assetPath: "/Game/Foo" });

    expect(out).toEqual({ saved: true });
    expect(inner.calls).toEqual(["save_asset"]);
    // The after guard observes; it never replaces the editor's result.
    expect(seen[1].result).toEqual({ saved: true });
  });
});
