import { describe, expect, it, vi } from "vitest";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import { injectEditorTarget } from "../../src/surface/target-params.js";
import { niagaraTool } from "../../src/tools/niagara.js";
import { recordingBridge } from "../fake-bridge.js";

function activeContext(bridge: IBridge, tool: ToolDef): ToolContext {
  return {
    bridge,
    project: {} as ToolContext["project"],
    getToolGraph: () => [tool],
  } as ToolContext;
}

function rebuilt(actions = { ...niagaraTool.actions }): ToolDef {
  return niagaraTool.rebuild!(actions);
}

describe("niagara batch dispatch (#1081)", () => {
  it("uses the same timeout, path repair, and projection pipeline as a direct call", async () => {
    const tool = rebuilt();
    const directBridge = recordingBridge({ kept: 1, dropped: 2 });
    const batchBridge = recordingBridge({ kept: 1, dropped: 2 });
    const op = {
      action: "get_info",
      assetPath: "\\Game\\VFX\\NS_Test",
      timeoutMs: 45_000,
      select: ["kept"],
    };
    const { action: _action, ...opParams } = op;

    const direct = await tool.handler(activeContext(directBridge, tool), op);
    const batch = await tool.handler(activeContext(batchBridge, tool), {
      action: "batch",
      ops: [{ action: "get_info", params: opParams }],
    }) as { results: Array<{ action: string; result: unknown }>; stoppedAt: number | null };

    expect(batchBridge.calls).toEqual(directBridge.calls);
    expect(batchBridge.calls).toEqual([{
      method: "get_niagara_info",
      params: { assetPath: "/Game/VFX/NS_Test" },
      timeoutMs: 45_000,
    }]);
    expect(batch.results).toEqual([{ action: "get_info", result: direct }]);
    expect(batch.stoppedAt).toBeNull();
    expect(direct).toMatchObject({ kept: 1, pathsRepaired: expect.any(Object) });
  });

  it("hands the batch's timeoutMs to every bridge op that names none of its own", async () => {
    const tool = rebuilt();
    const bridge = recordingBridge({ kept: 1, dropped: 2 });

    await tool.handler(activeContext(bridge, tool), {
      action: "batch",
      timeoutMs: 120_000,
      ops: [
        { action: "compile", params: { systemPath: "/Game/VFX/NS_Test" } },
        { action: "get_info", params: { assetPath: "/Game/VFX/NS_Test", timeoutMs: 30_000 } },
      ],
    });

    expect(bridge.calls.map((c) => [c.method, c.timeoutMs])).toEqual([
      ["compile_niagara_system", 120_000],
      ["get_niagara_info", 30_000],
    ]);
  });

  it("dispatches actions injected into the active session graph", async () => {
    const bridge = recordingBridge({ session: true });
    const tool = rebuilt({
      ...niagaraTool.actions,
      session_probe: bp("read", "Session probe. Params: assetPath", "session_probe"),
    });

    const out = await tool.handler(activeContext(bridge, tool), {
      action: "batch",
      ops: [{ action: "session_probe", params: { assetPath: "/Game/VFX/NS_Test" } }],
    }) as { results: Array<{ result: unknown }>; stoppedAt: number | null };

    expect(bridge.calls[0]).toMatchObject({ method: "session_probe", params: { assetPath: "/Game/VFX/NS_Test" } });
    expect(out).toEqual({ results: [{ action: "session_probe", result: { session: true } }], stoppedAt: null });
  });

  it("does not fall back when the active graph omits the action or category", async () => {
    const bridge = recordingBridge({ kept: 1, dropped: 2 });
    const { get_info: _removed, ...actions } = niagaraTool.actions;
    const tool = rebuilt(actions);

    const out = await tool.handler(activeContext(bridge, tool), {
      action: "batch",
      ops: [{ action: "get_info", params: { assetPath: "/Game/VFX/NS_Test" } }],
    });

    expect(out).toEqual({
      results: [{ action: "get_info", error: "Unknown niagara action 'get_info'" }],
      stoppedAt: 0,
    });
    expect(bridge.calls).toEqual([]);

    const noCategory = {
      ...activeContext(bridge, tool),
      getToolGraph: () => [],
    } as ToolContext;
    await expect(tool.handler(noCategory, {
      action: "batch",
      ops: [{ action: "get_info" }],
    })).resolves.toEqual({
      results: [{ action: "get_info", error: "Niagara is not available in the active tool graph" }],
      stoppedAt: 0,
    });
    expect(bridge.calls).toEqual([]);
  });

  it("refuses an op that targets a different editor than the batch runs on", async () => {
    const bridge = recordingBridge({ ok: true });
    const tool = rebuilt();
    injectEditorTarget(tool, ["A", "B"]);
    // Disconnected with no status file, so the per-call modal check has
    // nothing to probe and lets the batch run.
    const offline = { isConnected: false, getTarget: () => ({ projectPath: null }) };
    const sessionA = { name: "A", bridge: offline, project: { projectPath: null } } as unknown as ToolContext["session"];
    const sessionB = { name: "B", bridge: offline, project: { projectPath: null } } as unknown as ToolContext["session"];
    const ctx = {
      ...activeContext(bridge, tool),
      session: sessionA,
      sessions: {
        resolve: (t: unknown) => {
          if (t === "A") return sessionA;
          if (t === "B") return sessionB;
          throw new Error(`No editor session named '${String(t)}'`);
        },
      },
    } as unknown as ToolContext;

    const out = await tool.handler(ctx, {
      action: "batch",
      ops: [
        { action: "get_info", params: { assetPath: "/Game/VFX/One", editor: "A" } },
        { action: "get_info", params: { assetPath: "/Game/VFX/Two", editor: "B" } },
        { action: "get_info", params: { assetPath: "/Game/VFX/Three" } },
      ],
    });

    expect(out).toEqual({
      results: [
        { action: "get_info", result: { ok: true } },
        {
          action: "get_info",
          error: "ops[1] targets editor 'B', but a batch runs on one editor ('A'). Put 'editor' on the batch call, or split the batch per editor.",
        },
      ],
      stoppedAt: 1,
    });
    expect(bridge.calls).toEqual([
      { method: "get_niagara_info", params: { assetPath: "/Game/VFX/One" }, timeoutMs: undefined },
    ]);
  });

  it("reports a session with no tool surface per op instead of rejecting the batch", async () => {
    const bridge = recordingBridge({ kept: 1, dropped: 2 });
    const tool = rebuilt();
    const noSurface = {
      ...activeContext(bridge, tool),
      getToolGraph: () => { throw new Error("Editor 'B' has no tool surface built"); },
    } as ToolContext;

    await expect(tool.handler(noSurface, {
      action: "batch",
      ops: [{ action: "get_info", params: { assetPath: "/Game/VFX/NS_Test" } }],
    })).resolves.toEqual({
      results: [{
        action: "get_info",
        error: "Niagara is not available in the active tool graph: Editor 'B' has no tool surface built",
      }],
      stoppedAt: 0,
    });
    expect(bridge.calls).toEqual([]);
  });

  it("runs the active category's parameter folding for each operation", async () => {
    const bridge = recordingBridge({ kept: 1, dropped: 2 });
    const tool = categoryTool(
      "niagara",
      "Test Niagara graph.",
      {
        ...niagaraTool.actions,
        fold_probe: bp("read", "Fold probe. Params: legacyPath", "fold_probe"),
      },
      undefined,
      {
        normalizeParams: (params) => {
          const { legacyPath, ...rest } = params;
          return { ...rest, assetPath: legacyPath ?? rest.assetPath };
        },
      },
    );

    await tool.handler(activeContext(bridge, tool), {
      action: "batch",
      ops: [{ action: "fold_probe", params: { legacyPath: "/Game/VFX/NS_Test" } }],
    });

    expect(bridge.calls).toEqual([{
      method: "fold_probe",
      params: { assetPath: "/Game/VFX/NS_Test" },
      timeoutMs: undefined,
    }]);
  });

  it("keeps fail-fast results and rejects nesting before another operation runs", async () => {
    const bridge = recordingBridge({ ok: true });
    const tool = rebuilt();
    const unusedCall = vi.spyOn(bridge, "call");

    const out = await tool.handler(activeContext(bridge, tool), {
      action: "batch",
      ops: [
        { action: "get_info", params: { assetPath: "/Game/VFX/One" } },
        { action: "batch", params: { ops: [] } },
        { action: "get_info", params: { assetPath: "/Game/VFX/Two" } },
      ],
    });

    expect(out).toEqual({
      results: [
        { action: "get_info", result: { ok: true } },
        { action: "batch", error: "nested batch not allowed" },
      ],
      stoppedAt: 1,
    });
    expect(unusedCall).toHaveBeenCalledTimes(1);
  });

  it("stops at an op whose handler answered success:false, keeping its body", async () => {
    const failed = { success: false, error: "System not found: /Game/VFX/Missing" };
    const bridge = recordingBridge({ kept: 1, dropped: 2 });
    let n = 0;
    bridge.call = async (method, params, timeoutMs) => {
      bridge.calls.push({ method, params: params ?? {}, timeoutMs });
      return ++n === 1 ? failed : { ok: true };
    };
    const tool = rebuilt();

    const out = await tool.handler(activeContext(bridge, tool), {
      action: "batch",
      ops: [
        { action: "compile", params: { systemPath: "/Game/VFX/Missing" } },
        { action: "get_info", params: { assetPath: "/Game/VFX/Two" } },
      ],
    });

    expect(out).toEqual({
      results: [{ action: "compile", result: failed, error: "System not found: /Game/VFX/Missing" }],
      stoppedAt: 0,
    });
    expect(bridge.calls).toHaveLength(1);
  });
});
