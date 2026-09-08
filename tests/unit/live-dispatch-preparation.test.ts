/**
 * The per-call preparation, asserted on the route a real MCP call takes.
 *
 * `index.ts` dispatches every tool call through `sessionRegistry.create(...)`
 * and `task.run()`. It does NOT call `categoryTool.handler`. Three separate
 * pieces of behaviour were nevertheless written inside that handler, passed
 * every unit test because every unit test called it, and did nothing at all on
 * a live call:
 *
 *   D1  `CategoryOptions.normalizeParams`, so the `widget` category advertised
 *       `widgetBlueprintPath` / `widgetDisplayName` / `parentWidget` in its
 *       description AND its schema, and then handed the editor neither the
 *       alias nor the canonical name it folds to.
 *   D2  `timeoutMs`, which neither reached `bridge.call` as its timeout nor
 *       left the parameter bag, so the advertised budget did nothing and the
 *       key travelled to a C++ handler as a stray argument. The three actions
 *       that author their own 120s budget got 30s.
 *   D3  The path repair and the field projection inside the micro gateway's
 *       `args`, where every real parameter of a gateway call lives.
 *
 * Every case below therefore goes through `buildFlowRegistry` and `task.run()`
 * exactly as `index.ts` does, `action` destructured out of the parameters the
 * same way. A test that calls `tool.handler!` proves nothing about any of
 * this, which is the whole reason all three shipped.
 *
 * The last block is a parity guard: the same call down both routes has to
 * produce the same bridge call. That is what stops the two from drifting
 * apart again rather than catching the drift a release later.
 */
import { describe, expect, it } from "vitest";

import { buildFlowRegistry } from "../../src/flow/registry.js";
import { categoryTool, bp, type ToolDef, type ToolContext } from "../../src/types.js";
import { buildMicroGateway } from "../../src/lean-context.js";
import { widgetTool } from "../../src/tools/widget.js";
import { ProjectContext } from "../../src/project.js";
import type { IBridge } from "../../src/bridge.js";
import type { FlowContext } from "../../src/flow/context.js";

interface Recorded {
  method: string;
  params: Record<string, unknown>;
  timeoutMs?: number;
}

interface RecordingBridge extends IBridge {
  calls: Recorded[];
}

function recordingBridge(answer: unknown = { success: true }): RecordingBridge {
  const calls: Recorded[] = [];
  return {
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    call: async (method: string, params?: Record<string, unknown>, timeoutMs?: number) => {
      calls.push({ method, params: params ?? {}, timeoutMs });
      return answer;
    },
    calls,
  } as unknown as RecordingBridge;
}

function flowContext(bridge: IBridge): FlowContext {
  return { bridge, project: new ProjectContext() };
}

/**
 * One call down the LIVE route, assembled the way `index.ts` assembles it:
 * the registry built from the tool graph, `action` taken off the parameters
 * because it named the task rather than being one of its arguments, then
 * `create` + `run`.
 */
async function callLive(
  tools: ToolDef[],
  taskName: string,
  bridge: IBridge,
  params: Record<string, unknown> = {},
): Promise<{ success: boolean; data?: Record<string, unknown> }> {
  const registry = buildFlowRegistry(tools);
  const { action: _named, ...taskParams } = params;
  const task = await registry.create(taskName, flowContext(bridge), taskParams);
  const result = await task.run();
  return { success: result.success, data: result.data };
}

// ── D1: the category's parameter folding ────────────────────────────────────

describe("normalizeParams on the live dispatch route", () => {
  it("folds the widget aliases the tool advertises, instead of dropping them", async () => {
    // widget(read_tree) reaches a C++ handler doing
    // RequireStringAlt(Params, "assetPath", "path"). Sending neither is a
    // missing-parameter refusal for a spelling the tool itself accepts.
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.read_tree", bridge, {
      action: "read_tree",
      widgetBlueprintPath: "/Game/UI/WBP_Menu",
    });
    expect(bridge.calls[0].method).toBe("read_widget_tree");
    expect(bridge.calls[0].params.assetPath).toBe("/Game/UI/WBP_Menu");
    expect(bridge.calls[0].params.path).toBe("/Game/UI/WBP_Menu");
  });

  it("normalizes the object-suffix spelling of an asset path", async () => {
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.read_tree", bridge, {
      action: "read_tree",
      assetPath: "/Game/UI/WBP_Menu.WBP_Menu",
    });
    expect(bridge.calls[0].params.assetPath).toBe("/Game/UI/WBP_Menu");
  });

  it("composes name + packagePath into assetPath, which needs the action name", async () => {
    // `create` is the only widget action whose normalizer branches: it splits
    // assetPath back into name + packagePath. The branch reads `action` out of
    // the bag, and dispatch strips `action` before a task ever sees it, so the
    // action name has to be handed to the preparation separately. Without
    // that, this call reaches the editor as bare name + packagePath.
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.create", bridge, {
      action: "create",
      name: "WBP_X",
      packagePath: "/Game/UI",
    });
    expect(bridge.calls[0].method).toBe("create_widget_blueprint");
    expect(bridge.calls[0].params).toEqual({
      name: "WBP_X",
      packagePath: "/Game/UI",
      assetPath: "/Game/UI/WBP_X",
      path: "/Game/UI/WBP_X",
    });
  });

  it("splits a canonical assetPath back apart for the create handler", async () => {
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.create", bridge, {
      action: "create",
      assetPath: "/Game/UI/WBP_Y",
    });
    expect(bridge.calls[0].params.name).toBe("WBP_Y");
    expect(bridge.calls[0].params.packagePath).toBe("/Game/UI");
  });

  it("never leaks the action name it injected for the normalizer", async () => {
    // GUARD, not a reproduction: this one passes against the unfixed code too,
    // because the unfixed code never injects an action name in the first
    // place. It exists so the mechanism that makes the folding work cannot
    // start leaking a key of its own.
    //
    // The action name is put into the bag so the normalizer can branch on it,
    // and taken back off. An `action` key arriving at a C++ handler is exactly
    // the stray-parameter defect the routing parameters are stripped to avoid.
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.create", bridge, {
      action: "create",
      assetPath: "/Game/UI/WBP_Z",
    });
    expect(bridge.calls[0].params).not.toHaveProperty("action");
  });

  it("refuses a contradictory pair with the normalizer's own message", async () => {
    // Throwing from normalizeParams is how a category rejects a malformed
    // combination. On the unfixed live route the normalizer never ran, so a
    // contradiction was forwarded to the editor instead of refused.
    const bridge = recordingBridge();
    const registry = buildFlowRegistry([widgetTool]);
    const task = await registry.create("widget.create", flowContext(bridge), {
      assetPath: "/Game/UI/WBP_A",
      name: "WBP_B",
    });
    const result = await task.run();
    expect(result.success).toBe(false);
    expect(result.error?.message).toContain("Conflicting parameters");
    expect(bridge.calls).toHaveLength(0);
  });

  it("runs the folding for a direct-handler action too", async () => {
    let seen: Record<string, unknown> | undefined;
    const tool = categoryTool(
      "probe",
      "Test-only category.",
      { look: { kind: "handler", effect: "read", description: "A direct handler.", handler: async (_ctx, p) => { seen = p; return { ok: true }; } } },
      undefined,
      undefined,
      { normalizeParams: (p) => ({ ...p, canonical: p.legacy ?? p.canonical }) },
    );
    await callLive([tool], "probe.look", recordingBridge(), { action: "look", legacy: "v" });
    expect(seen?.canonical).toBe("v");
  });

  it("folds AFTER the path repair, so the normalizer reads a repaired path", async () => {
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.read_tree", bridge, {
      action: "read_tree",
      widgetBlueprintPath: "\\Game\\UI\\WBP_Menu",
    });
    expect(bridge.calls[0].params.assetPath).toBe("/Game/UI/WBP_Menu");
  });
});

// ── D2: the per-call budget ─────────────────────────────────────────────────

function budgetTool(): ToolDef {
  return categoryTool("demo", "Demo", {
    quick: bp("read", "A normal action", "quick_method"),
    slow: { kind: "bridge", effect: "read", description: "An action with its own authored floor", bridge: "slow_method", timeoutMs: 120_000 },
    local: { kind: "handler", effect: "read", description: "A direct handler", handler: async () => ({ ok: true }) },
  });
}

describe("timeoutMs on the live dispatch route", () => {
  it("reaches bridge.call as the timeout, not as a parameter", async () => {
    const bridge = recordingBridge();
    await callLive([budgetTool()], "demo.quick", bridge, {
      action: "quick",
      path: "/Game/A",
      timeoutMs: 600_000,
    });
    expect(bridge.calls[0].timeoutMs).toBe(600_000);
    expect(bridge.calls[0].params).toEqual({ path: "/Game/A" });
    expect(bridge.calls[0].params).not.toHaveProperty("timeoutMs");
  });

  it("keeps the action's own authored budget when the caller says nothing", async () => {
    // blueprint(flush_component_templates), widget(add_widget) and
    // widget(remove_widget) are the three real actions in this position: their
    // bridge method has no entry in the editor's timeout table, so the 120s
    // they declare is the only thing between them and a 30s default.
    const bridge = recordingBridge();
    await callLive([budgetTool()], "demo.slow", bridge, { action: "slow" });
    expect(bridge.calls[0].timeoutMs).toBe(120_000);
  });

  it("lets the caller raise an action that already declares a longer budget", async () => {
    const bridge = recordingBridge();
    await callLive([budgetTool()], "demo.slow", bridge, { action: "slow", timeoutMs: 900_000 });
    expect(bridge.calls[0].timeoutMs).toBe(900_000);
  });

  it("gives the same 120s floor to the real widget actions that author one", async () => {
    const bridge = recordingBridge();
    await callLive([widgetTool], "widget.add_widget", bridge, {
      action: "add_widget",
      assetPath: "/Game/UI/WBP_Menu",
      widgetClass: "Button",
    });
    expect(bridge.calls[0].timeoutMs).toBe(120_000);
  });

  it("hands a direct handler the budget on the context, never in its parameters", async () => {
    let seenCtx: ToolContext | undefined;
    let seenParams: Record<string, unknown> | undefined;
    const tool = categoryTool("demo", "Demo", {
      local: {
        description: "A direct handler",
        handler: async (ctx, p) => { seenCtx = ctx; seenParams = p; return { ok: true }; },
      },
    });
    await callLive([tool], "demo.local", recordingBridge(), {
      action: "local",
      name: "x",
      timeoutMs: 600_000,
    });
    expect(seenCtx?.callTimeoutMs).toBe(600_000);
    expect(seenParams).toEqual({ name: "x" });
  });

  it("discards an unusable budget rather than refusing the call", async () => {
    const bridge = recordingBridge();
    await callLive([budgetTool()], "demo.quick", bridge, { action: "quick", timeoutMs: 0 });
    expect(bridge.calls[0].timeoutMs).toBeUndefined();
    expect(bridge.calls[0].params).not.toHaveProperty("timeoutMs");
  });
});

// ── D3: the micro gateway's nested parameters ───────────────────────────────

describe("the micro gateway on the live dispatch route", () => {
  const target = (): ToolDef => categoryTool("asset", "Assets", {
    delete: bp("read", "Delete an asset", "delete_asset"),
    slow: { kind: "bridge", effect: "read", description: "Authored floor", bridge: "slow_asset", timeoutMs: 120_000 },
  });

  it("repairs a backslashed path inside args, and reports the repair", async () => {
    // `tools.call` arrives as {category, method, args}. Preparing the TOP
    // level touches nothing: no key there is a path. The repair has to descend
    // into `args` or the editor answers "asset not found" for an asset that is
    // right there.
    const bridge = recordingBridge();
    const gateway = buildMicroGateway([target()]);
    const out = await callLive([gateway, target()], "tools.call", bridge, {
      action: "call",
      category: "asset",
      method: "delete",
      args: { assetPath: "\\Game\\Temp\\X" },
    });
    expect(bridge.calls[0].method).toBe("delete_asset");
    expect(bridge.calls[0].params).toEqual({ assetPath: "/Game/Temp/X" });
    expect(out.data?.pathsRepaired).toBeDefined();
  });

  it("consumes args.select instead of forwarding it as a method parameter", async () => {
    const bridge = recordingBridge({ kept: 1, dropped: 2 });
    const gateway = buildMicroGateway([target()]);
    const out = await callLive([gateway, target()], "tools.call", bridge, {
      action: "call",
      category: "asset",
      method: "delete",
      args: { assetPath: "/Game/Temp/X", select: ["kept"] },
    });
    expect(bridge.calls[0].params).toEqual({ assetPath: "/Game/Temp/X" });
    expect(bridge.calls[0].params).not.toHaveProperty("select");
    expect(out.data).toEqual({ kept: 1 });
  });

  it("consumes args.omit the same way", async () => {
    const bridge = recordingBridge({ kept: 1, dropped: 2 });
    const gateway = buildMicroGateway([target()]);
    const out = await callLive([gateway, target()], "tools.call", bridge, {
      action: "call",
      category: "asset",
      method: "delete",
      args: { assetPath: "/Game/Temp/X", omit: ["dropped"] },
    });
    expect(bridge.calls[0].params).not.toHaveProperty("omit");
    expect(out.data).toEqual({ kept: 1 });
  });

  it("repairs a path exactly once, so the report names one repair", async () => {
    const bridge = recordingBridge();
    const gateway = buildMicroGateway([target()]);
    const out = await callLive([gateway, target()], "tools.call", bridge, {
      action: "call",
      category: "asset",
      method: "delete",
      args: { assetPath: "\\Game\\Temp\\X" },
    });
    const report = out.data?.pathsRepaired as { repairs: unknown[] };
    expect(report.repairs).toHaveLength(1);
  });

  it("honours the budget beside args and inside them, and strips it from both", async () => {
    const gateway = buildMicroGateway([target()]);
    const outer = recordingBridge();
    await callLive([gateway, target()], "tools.call", outer, {
      action: "call", category: "asset", method: "delete",
      args: { assetPath: "/Game/A" }, timeoutMs: 600_000,
    });
    expect(outer.calls[0].timeoutMs).toBe(600_000);
    expect(outer.calls[0].params).toEqual({ assetPath: "/Game/A" });

    const inner = recordingBridge();
    await callLive([gateway, target()], "tools.call", inner, {
      action: "call", category: "asset", method: "delete",
      args: { assetPath: "/Game/B", timeoutMs: 450_000 },
    });
    expect(inner.calls[0].timeoutMs).toBe(450_000);
    expect(inner.calls[0].params).toEqual({ assetPath: "/Game/B" });
  });

  it("keeps the target action's authored floor through the gateway", async () => {
    // GUARD: passes against the unfixed code, because the gateway's own
    // handler already resolved `requested ?? spec.timeoutMs`. Pinned so the
    // move of the budget onto the context cannot quietly lose it.
    const bridge = recordingBridge();
    const gateway = buildMicroGateway([target()]);
    await callLive([gateway, target()], "tools.call", bridge, {
      action: "call", category: "asset", method: "slow", args: {},
    });
    expect(bridge.calls[0].timeoutMs).toBe(120_000);
  });

  it("applies the target category's parameter folding", async () => {
    // The gateway resolves the target, so it is the only place that knows
    // whose normalizer to run. Without it a category's advertised spellings
    // work in every mode except micro.
    const bridge = recordingBridge();
    const gateway = buildMicroGateway([widgetTool]);
    await callLive([gateway, widgetTool], "tools.call", bridge, {
      action: "call",
      category: "widget",
      method: "read_tree",
      args: { widgetBlueprintPath: "\\Game\\UI\\WBP_Menu" },
    });
    expect(bridge.calls[0].params.assetPath).toBe("/Game/UI/WBP_Menu");
    expect(bridge.calls[0].params).not.toHaveProperty("action");
  });

  it("leaves a call that asked for nothing byte-identical", async () => {
    // GUARD: passes against the unfixed code. A call that requested no
    // projection and needed no repair must hand back the editor's own object,
    // not a copy of it, however much preparation now runs in front.
    const answer = { a: 1, b: 2 };
    const bridge = recordingBridge(answer);
    const gateway = buildMicroGateway([target()]);
    const out = await callLive([gateway, target()], "tools.call", bridge, {
      action: "call", category: "asset", method: "delete", args: { assetPath: "/Game/A" },
    });
    expect(out.data).toBe(answer);
  });
});

// ── The guard: the two routes must agree ────────────────────────────────────

describe("route parity", () => {
  /**
   * `categoryTool.handler` is not what a live MCP call runs, but it is still
   * reachable (tests, the HTTP flow surface, embedders). Two routes that
   * behave differently is the defect itself, not a symptom of it, so the same
   * call down both has to produce the same bridge call.
   */
  const cases: Array<{ name: string; tool: () => ToolDef; task: string; params: Record<string, unknown> }> = [
    {
      name: "a folded widget alias",
      tool: () => widgetTool,
      task: "widget.read_tree",
      params: { action: "read_tree", widgetBlueprintPath: "\\Game\\UI\\WBP_Menu" },
    },
    {
      name: "a composed widget create",
      tool: () => widgetTool,
      task: "widget.create",
      params: { action: "create", name: "WBP_X", packagePath: "/Game/UI" },
    },
    {
      name: "a caller-raised budget",
      tool: budgetTool,
      task: "demo.slow",
      params: { action: "slow", path: "/Game/A", timeoutMs: 900_000 },
    },
    {
      name: "an authored budget with no caller value",
      tool: budgetTool,
      task: "demo.slow",
      params: { action: "slow" },
    },
  ];

  for (const c of cases) {
    it(`sends the same bridge call on both routes: ${c.name}`, async () => {
      const tool = c.tool();

      const viaHandler = recordingBridge();
      await tool.handler(
        { bridge: viaHandler, project: new ProjectContext() } as ToolContext,
        { ...c.params },
      );

      const viaRegistry = recordingBridge();
      await callLive([tool], c.task, viaRegistry, { ...c.params });

      expect(viaRegistry.calls).toEqual(viaHandler.calls);
    });
  }

  it("sends the same gateway call on both routes", async () => {
    const target = categoryTool("asset", "Assets", { delete: bp("read", "Delete", "delete_asset") });
    const gateway = buildMicroGateway([target]);
    const params = {
      action: "call",
      category: "asset",
      method: "delete",
      args: { assetPath: "\\Game\\Temp\\X", select: ["kept"], timeoutMs: 450_000 },
    };

    const viaHandler = recordingBridge({ kept: 1, dropped: 2 });
    await gateway.handler(
      { bridge: viaHandler, project: new ProjectContext() } as ToolContext,
      { ...params, args: { ...params.args } },
    );

    const viaRegistry = recordingBridge({ kept: 1, dropped: 2 });
    await callLive([gateway, target], "tools.call", viaRegistry, {
      ...params,
      args: { ...params.args },
    });

    expect(viaRegistry.calls).toEqual(viaHandler.calls);
    expect(viaRegistry.calls[0].params).toEqual({ assetPath: "/Game/Temp/X" });
    expect(viaRegistry.calls[0].timeoutMs).toBe(450_000);
  });
});
