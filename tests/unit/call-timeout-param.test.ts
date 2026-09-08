/**
 * The per-call timeout budget, end to end (#989).
 *
 * A 190-item batch on a machine that was also compiling shaders could not
 * finish inside the flat 30 second client budget. The editor completed the
 * operation every time; the client reported a failure, and a naive retry
 * applied the mutation twice. The budget is now the caller's to set.
 *
 * Two properties matter and neither is obvious from reading the dispatcher:
 * the value has to reach bridge.call as its timeout argument, and it must not
 * reach the bridge as a handler parameter, where a C++ handler reading an
 * unexpected key would be the next defect.
 */
import { describe, expect, it } from "vitest";
import { categoryTool, bp, takeTimeout, type ToolContext, type ToolDef } from "../../src/types.js";
import { MAX_BRIDGE_TIMEOUT_MS } from "../../src/bridge-timeouts.js";
import { buildMicroGateway } from "../../src/lean-context.js";
import type { IBridge } from "../../src/bridge.js";

interface Recorded {
  method: string;
  params?: Record<string, unknown>;
  timeoutMs?: number;
}

function recordingBridge(): IBridge & { calls: Recorded[] } {
  const calls: Recorded[] = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    call: async (method: string, params?: Record<string, unknown>, timeoutMs?: number) => {
      calls.push({ method, params, timeoutMs });
      return { success: true };
    },
  } as unknown as IBridge & { calls: Recorded[] };
}

function fixture(): ToolDef {
  return categoryTool("demo", "Demo", {
    quick: bp("read", "A normal action", "quick_method"),
    slow: { kind: "bridge", effect: "read", description: "An action that already asks for longer", bridge: "slow_method", timeoutMs: 120_000 },
  });
}

const ctxFor = (bridge: IBridge): ToolContext =>
  ({ bridge, project: {} as ToolContext["project"] }) as ToolContext;

describe("timeoutMs on a category call (#989)", () => {
  it("is advertised by every category tool", () => {
    expect(fixture().schema.timeoutMs).toBeDefined();
    expect(fixture().schema.timeoutMs.safeParse(600_000).success).toBe(true);
    expect(fixture().schema.timeoutMs.safeParse(0).success).toBe(false);
    expect(fixture().schema.timeoutMs.safeParse(-1).success).toBe(false);
    expect(fixture().schema.timeoutMs.safeParse(MAX_BRIDGE_TIMEOUT_MS + 1).success).toBe(false);
  });

  it("reaches bridge.call as the timeout, not as a parameter", async () => {
    const bridge = recordingBridge();
    await fixture().handler!(ctxFor(bridge), { action: "quick", path: "/Game/A", timeoutMs: 600_000 });
    expect(bridge.calls[0].timeoutMs).toBe(600_000);
    expect(bridge.calls[0].params).toEqual({ path: "/Game/A" });
    expect(bridge.calls[0].params).not.toHaveProperty("timeoutMs");
  });

  it("leaves the action's own budget in place when the caller says nothing", async () => {
    const bridge = recordingBridge();
    await fixture().handler!(ctxFor(bridge), { action: "slow" });
    expect(bridge.calls[0].timeoutMs).toBe(120_000);
  });

  it("lets the caller raise an action that already declares a longer budget", async () => {
    // An authored timeoutMs is a floor the action needs, not a ceiling on the
    // caller: a batch is as slow as the batch is.
    const bridge = recordingBridge();
    await fixture().handler!(ctxFor(bridge), { action: "slow", timeoutMs: 900_000 });
    expect(bridge.calls[0].timeoutMs).toBe(900_000);
  });

  it("never forwards the budget to a custom handler's parameters", async () => {
    let seen: Record<string, unknown> | undefined;
    const tool = categoryTool("demo", "Demo", {
      local: { kind: "handler", effect: "read", description: "Local handler", handler: async (_ctx, p) => { seen = p; return { ok: true }; } },
    });
    await tool.handler!(ctxFor(recordingBridge()), { action: "local", name: "x", timeoutMs: 600_000 });
    expect(seen).toEqual({ action: "local", name: "x" });
  });

  it("is honoured through the micro-context gateway, beside args or inside them", async () => {
    const gateway = buildMicroGateway([fixture()]);
    const bridge = recordingBridge();

    await gateway.handler!(ctxFor(bridge), {
      action: "call", category: "demo", method: "quick", args: { path: "/Game/A" }, timeoutMs: 600_000,
    });
    expect(bridge.calls[0].timeoutMs).toBe(600_000);
    expect(bridge.calls[0].params).toEqual({ path: "/Game/A" });

    await gateway.handler!(ctxFor(bridge), {
      action: "call", category: "demo", method: "quick", args: { path: "/Game/B", timeoutMs: 450_000 },
    });
    expect(bridge.calls[1].timeoutMs).toBe(450_000);
    expect(bridge.calls[1].params).toEqual({ path: "/Game/B" });
  });
});

describe("takeTimeout", () => {
  it("separates a usable budget from the parameters", () => {
    expect(takeTimeout({ a: 1, timeoutMs: 5_000 })).toEqual({ timeoutMs: 5_000, rest: { a: 1 } });
  });

  it("discards an unusable one rather than refusing the call", () => {
    for (const bad of [0, -1, "600000", null, undefined, Number.NaN]) {
      expect(takeTimeout({ a: 1, timeoutMs: bad }).timeoutMs, String(bad)).toBeUndefined();
    }
  });

  it("caps at the ceiling", () => {
    expect(takeTimeout({ timeoutMs: 99_999_999 }).timeoutMs).toBe(MAX_BRIDGE_TIMEOUT_MS);
  });
});
