/**
 * How a handler reaches another action (through its task) and its own bridge
 * method (directly, with the caller's budget).
 */
import { describe, expect, it } from "vitest";
import { callAction, callOwnBridgeMethod } from "../../src/flow/action-call.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import type { ToolContext } from "../../src/core/types.js";
import type { IBridge } from "../../src/bridge/bridge.js";

const tool = categoryTool("asset", "Assets.", { delete: bp("mutate", "delete_asset") });

function context(answer: unknown, extra: Partial<ToolContext> = {}) {
  const calls: unknown[][] = [];
  const ctx = {
    project: {},
    bridge: {
      isConnected: true,
      call: async (...args: unknown[]) => {
        calls.push(args);
        if (answer instanceof Error) throw answer;
        return answer;
      },
    } as unknown as IBridge,
    ...extra,
  } as ToolContext;
  return { ctx, calls };
}

describe("callOwnBridgeMethod", () => {
  it("sends the caller's budget when it has one, and nothing extra when it does not", async () => {
    const a = context({ ok: true });
    await callOwnBridgeMethod(a.ctx, "search_assets", { query: "x" });
    expect(a.calls).toEqual([["search_assets", { query: "x" }]]);
    const b = context({ ok: true }, { callTimeoutMs: 90_000 });
    await callOwnBridgeMethod(b.ctx, "search_assets", { query: "x" });
    expect(b.calls).toEqual([["search_assets", { query: "x" }, 90_000]]);
  });
});

describe("callAction", () => {
  it("reads a success:false body as a failed result", async () => {
    const { ctx } = context({ success: false, error: "in use" });
    const r = await callAction(ctx, "asset.delete", { assetPath: "/Game/A" }, tool);
    expect(r.success).toBe(false);
    expect(r.error?.message).toBe("in use");
    expect(r.data).toEqual({ success: false, error: "in use" });
  });

  it("finds the action in the context's tool graph when no home is given", async () => {
    const { ctx, calls } = context({ success: true }, { getToolGraph: () => [tool] });
    expect((await callAction(ctx, "asset.delete", { assetPath: "/Game/A" })).success).toBe(true);
    expect(calls[0][0]).toBe("delete_asset");
  });

  it("reports a throw and an unknown action as failed results rather than throwing", async () => {
    const { ctx } = context(new Error("socket closed"));
    expect((await callAction(ctx, "asset.delete", {}, tool)).error?.message).toBe("socket closed");
    expect((await callAction(ctx, "asset.nope", {}, tool)).error?.message).toMatch(/not available/);
  });
});
