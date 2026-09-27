/**
 * niagara.batch answers exactly as the handler it replaced: that handler, as
 * it stood before the move to a composite, is kept below.
 */
import { describe, expect, it } from "vitest";
import { niagaraTool } from "../../../src/tools/niagara.js";
import { handlerFailure } from "../../../src/flow/handler-outcome.js";
import type { ToolDef } from "../../../src/core/types.js";
import { expectParity, type LegacyHandler } from "../../helpers/composite-parity.js";

const legacyBatch: LegacyHandler = async (ctx, params) => {
  const opsUnknown = params.ops;
  if (!Array.isArray(opsUnknown)) throw new Error("'ops' must be an array of {action, params}");
  let unavailable = "Niagara is not available in the active tool graph";
  let dispatchTool: ToolDef | undefined;
  try {
    const graph = ctx.getToolGraph ? ctx.getToolGraph() : undefined;
    dispatchTool = graph === undefined ? niagaraTool : graph.find((tool) => tool.name === "niagara");
  } catch (e) {
    unavailable = `${unavailable}: ${(e as Error).message}`;
  }
  const results: Array<{ action: string; result?: unknown; error?: string }> = [];
  for (let i = 0; i < opsUnknown.length; i++) {
    const op = opsUnknown[i] as { action?: string; params?: Record<string, unknown> } | undefined;
    const action = op?.action;
    if (!action) { results.push({ action: "(missing)", error: `ops[${i}] missing 'action'` }); return { results, stoppedAt: i }; }
    if (!dispatchTool) { results.push({ action, error: unavailable }); return { results, stoppedAt: i }; }
    const spec = dispatchTool.actions[action];
    if (!spec) { results.push({ action, error: `Unknown niagara action '${action}'` }); return { results, stoppedAt: i }; }
    if (action === "batch") { results.push({ action, error: "nested batch not allowed" }); return { results, stoppedAt: i }; }
    try {
      const subParams = { ...(op.params ?? {}), action } as Record<string, unknown>;
      if (subParams.timeoutMs === undefined && ctx.callTimeoutMs !== undefined) subParams.timeoutMs = ctx.callTimeoutMs;
      const result = await dispatchTool.handler(ctx, subParams);
      const failure = handlerFailure(result);
      if (failure !== null) {
        results.push({ action, result, error: failure });
        return { results, stoppedAt: i };
      }
      results.push({ action, result });
    } catch (e) {
      results.push({ action, error: (e as Error).message });
      return { results, stoppedAt: i };
    }
  }
  return { results, stoppedAt: null };
};

const ops = [
  { action: "get_info", params: { assetPath: "/Game/VFX/NS_A" } },
  { action: "compile", params: { systemPath: "/Game/VFX/NS_A", timeoutMs: 5_000 } },
  { action: "get_info", params: { assetPath: "\\Game\\VFX\\NS_B", select: ["kept"] } },
];

const parity = (answer: Parameters<typeof expectParity>[3], params: Record<string, unknown>) =>
  expectParity(niagaraTool, "batch", legacyBatch, answer, params);

describe("niagara.batch answers as the handler did", () => {
  it("on a clean run, budget and projection included", async () => {
    const out = await parity(() => ({ kept: 1, dropped: 2 }), { ops, timeoutMs: 60_000 });
    expect(out.data).toMatchObject({ stoppedAt: null });
  });

  it("stopping at a child that answers success:false, with its body", async () => {
    const out = await parity((m) => (m === "compile_niagara_system" ? { success: false, error: "System not found" } : { ok: true }), { ops });
    expect(out.data).toMatchObject({ stoppedAt: 1 });
  });

  it("stopping at a child that throws", async () => {
    await parity((m) => {
      if (m === "compile_niagara_system") throw new Error("socket closed");
      return { ok: true };
    }, { ops });
  });

  it("refusing a malformed op before anything after it runs", async () => {
    for (const bad of [{ params: {} }, { action: "nope" }, { action: "batch", params: { ops: [] } }]) {
      await parity(() => ({ ok: true }), { ops: [ops[0], bad, ops[2]] });
    }
  });

  it("refusing ops that are not a list", async () => {
    await parity(() => ({ ok: true }), { ops: "get_info" });
  });
});
