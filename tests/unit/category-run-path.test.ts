import { describe, it, expect } from "vitest";
import { z } from "zod";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";
import { bp, categoryTool } from "../../src/category-tool.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "../../src/extensions/injection.js";
import { buildProvidedTool } from "../../src/extensions/provision.js";
import { ErrorCode } from "../../src/core/errors.js";

function bridgeAnswering(answer: unknown): IBridge & { calls: Array<{ method: string; params?: Record<string, unknown> }> } {
  const calls: Array<{ method: string; params?: Record<string, unknown> }> = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    call: async (method, params) => {
      calls.push({ method, params });
      return answer;
    },
  };
}

const ctxFor = (bridge: IBridge): ToolContext => ({ bridge, project: {} } as unknown as ToolContext);

function fakePcg(): ToolDef {
  return categoryTool(
    "pcg",
    "Fake PCG.",
    { list_graphs: bp("read", "List graphs.", "pcg_list_graphs") },
    { graphPath: z.string().optional() },
  );
}

describe("a category tool's own handler runs the task path", () => {
  it("wraps a scalar answer the way a live call's task does", async () => {
    const tool = categoryTool("probe", "Probe.", { go: bp("read", "Go.", "probe_go") });
    expect(await tool.handler(ctxFor(bridgeAnswering(7)), { action: "go" })).toEqual({ result: 7 });
  });

  it("hands back a failing body intact outside a flow", async () => {
    const tool = categoryTool("probe", "Probe.", { go: bp("mutate", "Go.", "probe_go") });
    const failed = { success: false, error: "no such asset", rollback: { method: "undo", payload: {} } };
    expect(await tool.handler(ctxFor(bridgeAnswering(failed)), { action: "go" })).toEqual(failed);
  });
});

describe("tools built from plugins", () => {
  const plan: InjectionPlan = {
    category: "pcg",
    prefix: "vpp",
    pluginName: "ue-mcp-voxel-pro",
    actions: { scatter: { task: "vpp.scatter", description: "Scatter" } },
  };

  it("an injected action reaches the merged tool's dispatcher instead of being unknown to it", async () => {
    const { tool } = mergeInjectionsIntoTool(fakePcg(), [plan]);
    await expect(tool.handler(ctxFor(bridgeAnswering({})), { action: "vpp_scatter" }))
      .rejects.toMatchObject({ code: ErrorCode.NO_HANDLER });
    const bridge = bridgeAnswering({ graphs: [] });
    await tool.handler(ctxFor(bridge), { action: "list_graphs" });
    expect(bridge.calls.map((c) => c.method)).toEqual(["pcg_list_graphs"]);
    expect(tool.rebuild).toBeDefined();
  });

  it("a provided category takes the routing parameters every category takes", () => {
    const tool = buildProvidedTool({
      category: "terrain_sculpt",
      pluginName: "ue-mcp-terrain",
      spec: { actions: { raise: { description: "Raise terrain", schema: {} } } } as never,
    });
    for (const key of ["timeoutMs", "select", "omit"]) expect(tool.schema[key], key).toBeDefined();
    expect(tool.description).toContain("- raise: Raise terrain");
    expect(tool.rebuild).toBeDefined();
  });
});
