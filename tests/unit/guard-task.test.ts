/**
 * `UeMcpGuard`, the base class a guard is written against.
 *
 * A guard was authored by extending `UeMcpTask` and implementing `execute()`,
 * which left three things wrong, and these cover all three plus the promise
 * that fixing them broke nothing:
 *
 *   the options bag was untyped, and the guard's own configuration and the
 *   call being guarded were flattened together with the call winning any
 *   collision, so an option named `method` was silently lost;
 *   one `execute()` served both hooks, distinguishable only by whether a
 *   `result` key happened to be present;
 *   and a guard still written the old way has to keep working exactly.
 */
import { describe, it, expect } from "vitest";
import { BaseTask, TaskRegistry, type TaskConstructor, type TaskResult } from "@db-lyon/flowkit";
import { buildGuards } from "../../src/flow/guards.js";
import { GuardsSchema } from "../../src/flow/guard-schema.js";
import { makeCallContext, type CallContext } from "../../src/flow/guard.js";
import { UeMcpGuard, type GuardedCall } from "../../src/guard-task.js";
import type { IBridge } from "../../src/bridge.js";
import type { ToolContext } from "../../src/types.js";

const target = { projectPath: null, port: 0, portSource: "default" as const, verified: true };
const fakeBridge = (): IBridge => ({
  isConnected: true,
  connect: async () => {},
  retargetProject: () => target,
  getTarget: () => target,
  call: async () => ({ ok: true }),
});
const ctx = { bridge: fakeBridge(), project: {} } as unknown as ToolContext;
const resolveExisting = (cp: string) =>
  cp.startsWith("/Game/") ? `C:/proj/Content/${cp.slice(6)}.uasset` : null;
const callCtx = (method: string, params: Record<string, unknown> = {}): CallContext =>
  makeCallContext(method, params, undefined, fakeBridge(), resolveExisting);

function registryWith(classes: Record<string, TaskConstructor>): TaskRegistry {
  const registry = new TaskRegistry();
  for (const [name, ctor] of Object.entries(classes)) registry.register(name, ctor);
  return registry;
}
const deps = (registry: TaskRegistry) => ({ registry, ctx, rawBridge: fakeBridge() });
const declare = (raw: unknown) => GuardsSchema.parse(raw);
const SOURCE = { label: "ue-mcp.yml" };

/* ── the subjects ─────────────────────────────────────────────────────── */

let sawBefore: GuardedCall[] = [];
let sawAfter: Array<{ call: GuardedCall; result: unknown }> = [];

class Freeze extends UeMcpGuard<{ enabled?: boolean; method?: string }> {
  get taskName() { return "freeze"; }
  async before(call: GuardedCall) {
    sawBefore.push(call);
    if (this.config.enabled === false) return this.allow();
    return this.deny(`Blocked '${call.method}'.`);
  }
}

class Audit extends UeMcpGuard {
  get taskName() { return "audit"; }
  async after(call: GuardedCall, result: unknown) {
    sawAfter.push({ call, result });
    return { audited: true };
  }
}

class BothHooks extends UeMcpGuard {
  get taskName() { return "both"; }
  async before(call: GuardedCall) {
    sawBefore.push(call);
    return this.allow();
  }
  async after(call: GuardedCall) {
    sawAfter.push({ call, result: undefined });
  }
}

class WrongHook extends UeMcpGuard {
  get taskName() { return "wrong-hook"; }
  async after() { return undefined; }
}

/** A guard written the old way, which must keep working untouched. */
class LegacyTaskGuard extends BaseTask {
  get taskName() { return "legacy"; }
  async execute(): Promise<TaskResult> {
    const o = this.options as Record<string, unknown>;
    return o.method === "save_asset"
      ? { success: false, error: new Error(`legacy blocked ${String(o.method)}`) }
      : { success: true };
  }
}

describe("UeMcpGuard", () => {
  it("hands before() a typed call rather than an untyped bag", async () => {
    sawBefore = [];
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "all", before: { class_path: "freeze" } } }),
      deps(registryWith({ freeze: Freeze as unknown as TaskConstructor })),
      SOURCE,
    );
    await expect(guard.before!(callCtx("save_asset", { assetPath: "/Game/Foo" })))
      .rejects.toThrow(/Blocked 'save_asset'/);

    expect(sawBefore).toHaveLength(1);
    expect(sawBefore[0].phase).toBe("before");
    expect(sawBefore[0].method).toBe("save_asset");
    expect(sawBefore[0].params).toEqual({ assetPath: "/Game/Foo" });
    expect(sawBefore[0].paths).toEqual(["C:/proj/Content/Foo.uasset"]);
  });

  it("keeps the guard's own configuration out of reach of the call", async () => {
    // The hazard this closes. `method` is a real thing to want to configure,
    // and under the flattened bag the call being guarded overwrote it, so the
    // guard silently read the wrong value with no way to notice.
    sawBefore = [];
    const [guard] = await buildGuards(
      declare({
        freeze: {
          scope: "all",
          before: { class_path: "freeze", options: { enabled: false, method: "MY OWN SETTING" } },
        },
      }),
      deps(registryWith({ freeze: Freeze as unknown as TaskConstructor })),
      SOURCE,
    );

    // enabled:false is read from config, so the call is allowed through.
    await expect(guard.before!(callCtx("save_asset", { assetPath: "/Game/Foo" }))).resolves.toBeUndefined();
    // The guard's `method` option survived; the call's method did not mask it.
    expect(sawBefore[0].method, "the CALL's method reaches the call object").toBe("save_asset");
  });

  it("routes each phase to its own method", async () => {
    sawBefore = [];
    sawAfter = [];
    const [guard] = await buildGuards(
      declare({
        both: { scope: "all", before: { class_path: "both" }, after: { class_path: "both" } },
      }),
      deps(registryWith({ both: BothHooks as unknown as TaskConstructor })),
      SOURCE,
    );

    await guard.before!(callCtx("save_asset"));
    await guard.after!(callCtx("save_asset"), { ok: true });

    expect(sawBefore.map((c) => c.phase)).toEqual(["before"]);
    expect(sawAfter.map((c) => c.call.phase)).toEqual(["after"]);
  });

  it("gives after() the call's result, and replaces it when one is returned", async () => {
    sawAfter = [];
    const [guard] = await buildGuards(
      declare({ trail: { scope: "all", after: { class_path: "audit" } } }),
      deps(registryWith({ audit: Audit as unknown as TaskConstructor })),
      SOURCE,
    );

    const replaced = await guard.after!(callCtx("save_asset"), { original: true });
    expect(sawAfter[0].result).toEqual({ original: true });
    expect(replaced).toEqual({ audited: true });
  });

  it("says so when it is declared for a hook it does not implement", async () => {
    const [guard] = await buildGuards(
      declare({ oops: { scope: "all", before: { class_path: "wrong" } } }),
      deps(registryWith({ wrong: WrongHook as unknown as TaskConstructor })),
      SOURCE,
    );
    // A guard that cannot answer must not read as one that approved.
    await expect(guard.before!(callCtx("save_asset"))).rejects.toThrow(/implements no before\(\)/);
  });

  it("refuses to be run as an ordinary task", async () => {
    const registry = registryWith({ freeze: Freeze as unknown as TaskConstructor });
    const task = await registry.create("freeze", {} as never, { enabled: true });
    const answered = (await task.execute()) as TaskResult;
    expect(answered.success).toBe(false);
    expect(String(answered.error?.message)).toMatch(/is a guard and was run as a task/);
  });

  it("leaves a guard written against UeMcpTask working exactly as before", async () => {
    const [guard] = await buildGuards(
      declare({ legacy: { scope: "all", before: { class_path: "legacy" } } }),
      deps(registryWith({ legacy: LegacyTaskGuard as unknown as TaskConstructor })),
      SOURCE,
    );
    await expect(guard.before!(callCtx("save_asset"))).rejects.toThrow(/legacy blocked save_asset/);
    await expect(guard.before!(callCtx("read_asset"))).resolves.toBeUndefined();
  });
});
