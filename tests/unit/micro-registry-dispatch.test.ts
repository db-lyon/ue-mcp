import { describe, expect, it } from "vitest";
import { FlowDefinitionSchema, FlowRunner } from "@db-lyon/flowkit";
import type { TaskConstructor, TaskResult } from "@db-lyon/flowkit";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import type { FlowContext } from "../../src/flow/context.js";
import { applyLeanContext, buildMicroGateway } from "../../src/lean-context.js";
import { nativeHandlerSurface } from "../../src/plugin/loader.js";
import { PluginManifestSchema } from "../../src/plugin/manifest.js";
import { buildProvidedTool } from "../../src/plugin/provision.js";
import { categoryTool } from "../../src/types.js";
import { UeMcpTask } from "../../src/task.js";
import type { IBridge } from "../../src/bridge.js";

// The same manifest -> registry-kind action -> native task registration path
// as PIE Studio, without installing a plugin or contacting an editor.
function fixture(mode: "full" | "lean" | "micro" = "micro") {
  const manifest = PluginManifestSchema.parse({
    actionPrefix: "pie",
    nativeModule: {
      uePluginName: "PIE_Studio", minBridgeApi: 1, source: "ue/Plugins/PIE_Studio",
      category: "pie",
      handlers: {
        inject_input: {
          description: "Inject input. Params: assetPath, value?",
          timeoutSeconds: 120,
          schema: { assetPath: { type: "string" }, value: { type: "number" } },
        },
      },
    },
  });
  const surface = nativeHandlerSurface(manifest, "pie-studio", new Set());
  if (surface?.kind !== "provide") throw new Error("expected a provided native category");
  const pie = buildProvidedTool(surface.plan);
  const tools = mode === "micro" ? [buildMicroGateway([pie]), pie]
    : mode === "lean" ? applyLeanContext([pie]) : [pie];
  const registry = buildFlowRegistry(tools);
  // Plugin tasks register after the category graph, exactly as at startup.
  for (const { name, ctor } of surface.taskRegistrations) registry.register(name, ctor);
  return { pie, registry };
}

function context(answer: unknown = { success: true }) {
  const calls: Array<{ method: string; params: Record<string, unknown>; timeoutMs?: number }> = [];
  const ctx = {
    project: {},
    bridge: {
      call: async (method: string, params: Record<string, unknown>, timeoutMs?: number) => {
        calls.push({ method, params, timeoutMs });
        return answer;
      },
    } as unknown as IBridge,
  } as FlowContext;
  return { ctx, calls };
}

const withoutDuration = ({ duration: _duration, ...result }: TaskResult) => result;

describe("micro calls use the session task registry", () => {
  it.each(["full", "lean", "micro"] as const)("runs the native plugin task in %s mode", async (mode) => {
    const { pie, registry } = fixture(mode);
    const { ctx, calls } = context({ success: true, accepted: 1 });
    expect(pie.actions.inject_input.kind).toBe("registry");
    const args = { assetPath: "/Game/Take", value: 1 };
    const task = await registry.create(mode === "micro" ? "tools.call" : "pie.inject_input", ctx,
      mode === "micro" ? { category: "pie", method: "inject_input", args } : args);
    expect(task.taskName).toBe("pie.inject_input");
    expect(withoutDuration(await task.run())).toEqual({ success: true, data: { success: true, accepted: 1 } });
    expect(calls).toEqual([{ method: "inject_input", params: args, timeoutMs: 120_000 }]);
  });

  it("keeps path repair, timeout precedence and projection on the target's existing pipeline", async () => {
    const { registry } = fixture();
    const { ctx, calls } = context({ success: true, kept: { value: 1, hidden: 2 }, dropped: 3 });
    const task = await registry.create("tools.call", ctx, {
      category: "pie", method: "inject_input", timeoutMs: 600_000, select: ["kept"],
      args: { action: "not_a_parameter", editor: "already_routed", assetPath: "\\Game\\Take",
        timeoutMs: 200_000, select: ["dropped"], omit: ["kept.hidden"] },
    });
    const result = await task.run();
    expect(result.success).toBe(true);
    expect(calls).toEqual([{ method: "inject_input", params: { assetPath: "/Game/Take" }, timeoutMs: 600_000 }]);
    expect(result.data).toMatchObject({ kept: { value: 1 } });
    expect(result.data).not.toHaveProperty("kept.hidden");
    expect(result.data).not.toHaveProperty("dropped");
    // Compare with the direct registry path, including the path-repair report.
    const direct = await registry.create("pie.inject_input", ctx, {
      assetPath: "\\Game\\Take", timeoutMs: 600_000, select: ["kept"], omit: ["kept.hidden"],
    });
    expect(withoutDuration(result)).toEqual(withoutDuration(await direct.run()));
  });

  it.each([false, true])("preserves failure and rollback with flow context=%s", async (inFlow) => {
    const { registry } = fixture();
    const answer = { success: false, error: "partially applied", detail: "keep this",
      rollback: { method: "undo_input", payload: { assetPath: "/Game/Take" } } };
    const { ctx } = context(answer);
    if (inFlow) ctx.taskReferenceContext = { steps: [] };
    const args = { assetPath: "/Game/Take", select: ["detail"] };
    const micro = await registry.create("tools.call", ctx, { category: "pie", method: "inject_input", args });
    const direct = await registry.create("pie.inject_input", ctx, args);
    const actual = await micro.run();
    const expected = await direct.run();
    expect(withoutDuration(actual)).toEqual(withoutDuration(expected));
    expect(actual.success).toBe(!inFlow);
    expect(actual.data).toMatchObject({ detail: "keep this" });
    expect(actual.rollback).toEqual({ taskName: "ue-mcp.bridge", payload: { method: "undo_input", assetPath: "/Game/Take" } });
  });

  it("preserves a custom plugin TaskResult and invokes its registered class", async () => {
    const { registry } = fixture();
    const { ctx } = context();
    const result: TaskResult = { success: false, data: { detail: "plugin detail" }, error: new Error("plugin refused"),
      rollback: { taskName: "pie.undo", payload: { value: 1 } } };
    class PluginTask extends UeMcpTask {
      get taskName() { return "pie.inject_input"; }
      async execute() { return result; }
    }
    registry.register("pie.inject_input", PluginTask as unknown as TaskConstructor);
    const task = await registry.create("tools.call", ctx, { category: "pie", method: "inject_input", args: {} });
    expect(task).toBeInstanceOf(PluginTask);
    expect(await task.run()).toBe(result);
  });

  it("lets FlowRunner undo a partially failed native plugin call", async () => {
    const { registry } = fixture();
    const { ctx, calls } = context();
    ctx.bridge.call = async (method, params, timeoutMs) => {
      calls.push({ method, params: params ?? {}, timeoutMs });
      return method === "inject_input"
        ? { success: false, error: "partial input", rollback: { method: "undo_input", payload: { value: 7 } } }
        : { success: true };
    };
    const runner = new FlowRunner({
      registry, context: ctx, tasks: {},
      flows: { test: FlowDefinitionSchema.parse({ steps: {
        "1": { task: "tools.call", options: { category: "pie", method: "inject_input", args: { value: 7 } } },
      } }) },
    });
    const result = await runner.run({ flowName: "test", rollback_on_failure: true });
    expect(result.success).toBe(false);
    expect(result.rollback).toMatchObject({ attempted: 1, succeeded: 1 });
    expect(calls.map(({ method, params }) => ({ method, params }))).toEqual([
      { method: "inject_input", params: { value: 7 } },
      { method: "undo_input", params: { value: 7 } },
    ]);
  });

  it("uses the selected session's registry and refuses a category absent from its graph", async () => {
    const first = fixture();
    const second = fixture();
    const a = context({ editor: "Alpha" });
    const b = context({ editor: "Beta" });
    const options = { category: "pie", method: "inject_input", args: {} };
    const task = await second.registry.create("tools.call", { ...b.ctx, getToolGraph: () => [second.pie] }, options);
    expect((await task.run()).data).toEqual({ editor: "Beta" });
    expect(a.calls).toHaveLength(0);
    await expect(first.registry.create("tools.call", { ...a.ctx, getToolGraph: () => [] }, options))
      .rejects.toThrow('Unknown category "pie"');
    expect(a.calls).toHaveLength(0);
  });

  it("refuses unknown actions and missing plugin registrations before dynamic task loading", async () => {
    const { pie, registry } = fixture();
    const { ctx } = context();
    for (const method of ["missing", "constructor", "../../outside"]) {
      await expect(registry.create("tools.call", ctx, { category: "pie", method }))
        .rejects.toThrow("Unknown action");
    }
    const missing = buildFlowRegistry([buildMicroGateway([pie]), pie]);
    await expect(missing.create("tools.call", ctx, { category: "pie", method: "inject_input" }))
      .rejects.toThrow("has no registered task");
  });

  it("refuses a disabled category through tools.call when no session graph is on the context", async () => {
    // Startup builds the gateway from the enabled categories but keeps every
    // category in the registry, so flows can still name a disabled one directly.
    let ran = 0;
    const handler = async () => { ran++; return { ok: true }; };
    const open = categoryTool("open", "Open", { write: { kind: "handler", effect: "mutate", handler } });
    const locked = categoryTool("locked", "Locked", { write: { kind: "handler", effect: "mutate", handler } });
    const registry = buildFlowRegistry([buildMicroGateway([open]), open, locked]);
    const { ctx } = context();
    expect(ctx.getToolGraph).toBeUndefined();
    await expect(registry.create("tools.call", ctx, { category: "locked", method: "write", args: {} }))
      .rejects.toThrow('Unknown category "locked"');
    expect(ran).toBe(0);
    await (await registry.create("tools.call", ctx, { category: "open", method: "write", args: {} })).run();
    await (await registry.create("locked.write", ctx, {})).run();
    expect(ran).toBe(2);
  });

  it("prepares built-in handler parameters exactly once", async () => {
    let folds = 0;
    const probe = categoryTool("probe", "Probe", {
      read: { kind: "handler", effect: "read", handler: async (ctx, p) => ({ ...p, budget: ctx.callTimeoutMs }) },
    }, undefined, { normalizeParams: (p) => { folds++; return { ...p, canonical: p.alias }; } });
    const registry = buildFlowRegistry([buildMicroGateway([probe]), probe]);
    const { ctx } = context();
    const task = await registry.create("tools.call", ctx, {
      category: "probe", method: "read", args: { alias: "value", timeoutMs: 80_000 },
    });
    expect((await task.run()).data).toEqual({ alias: "value", canonical: "value", budget: 80_000 });
    expect(folds).toBe(1);
  });
});
