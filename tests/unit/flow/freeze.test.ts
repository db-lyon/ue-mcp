/**
 * Freeze: the resolved plan as a flow definition that is itself valid input,
 * and that runs the same steps.
 */
import { describe, expect, it } from "vitest";
import { FlowDefinitionSchema } from "@db-lyon/flowkit";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { freezeFlow, type FrozenPlan } from "../../../src/flow/freeze.js";
import { bp, categoryTool } from "../../../src/surface/category-tool.js";
import type { ToolContext } from "../../../src/core/types.js";
import type { IBridge } from "../../../src/bridge/bridge.js";

const tool = categoryTool("asset", "Assets.", {
  read: bp("read", "read_asset"),
  save: bp("mutate", "save_asset"),
});
const registry = buildFlowRegistry([tool]);

const FLOWS = {
  outer: {
    description: "Outer.",
    steps: {
      1: { task: "asset.read", options: { assetPath: "/Game/${project.name}/A" } },
      2: { flow: "inner", options: { "asset.save": { force: true } } },
      3: { task: "asset.read", options: { assetPath: "${steps.1.path}" } },
      4: { task: "asset.read", when: "${steps.1.ok} == true", options: { assetPath: "${params.target}" } },
      5: { task: "asset.read", when: false, options: { assetPath: "/Game/Never" } },
    },
    finally: [{ task: "asset.read", options: { assetPath: "${steps.3.path}" } }],
  },
  inner: {
    steps: {
      1: { task: "asset.read", options: { assetPath: "/Game/Inner" } },
      2: { task: "asset.save", options: { assetPath: "${steps.1.path}" } },
    },
  },
};

function harness(extra: Record<string, unknown> = {}) {
  const calls: Array<[string, Record<string, unknown>]> = [];
  let n = 0;
  const ctx = {
    project: { projectName: "Shrine" },
    bridge: {
      isConnected: true,
      call: async (method: string, p: Record<string, unknown>) => {
        calls.push([method, p]);
        return { success: true, ok: true, path: `/Game/P${++n}` };
      },
    } as unknown as IBridge,
    getToolGraph: () => [tool],
  } as unknown as ToolContext;
  const config = FlowConfigSchema.parse({ ...buildDefaults([tool]), flows: { ...FLOWS, ...extra } });
  return { ctx, calls, config, flow: createFlowTool(registry, () => config) };
}

describe("freezeFlow", () => {
  it("expands nested flows, resolves options and renumbers references", () => {
    const { config } = harness();
    const frozen = freezeFlow({
      config, flowName: "outer", params: { target: "/Game/T" }, namespaces: { project: { name: "Shrine" } },
    }) as FrozenPlan;
    expect(FlowDefinitionSchema.safeParse(frozen.definition).success).toBe(true);
    const steps = frozen.definition.steps as Record<string, Record<string, unknown>>;
    expect(Object.keys(steps)).toEqual(["1", "2", "3", "4", "5"]);
    expect(steps[1]).toEqual({ task: "asset.read", options: { assetPath: "/Game/Shrine/A", target: "/Game/T" } });
    expect(steps[3]).toEqual({ task: "asset.save", options: { assetPath: "${steps.2.path}", force: true, target: "/Game/T" } });
    expect(steps[4].options).toMatchObject({ assetPath: "${steps.1.path}" });
    expect(steps[5]).toMatchObject({ when: "${steps.1.ok} == true", options: { assetPath: "/Game/T" } });
    expect((frozen.definition.finally as Array<Record<string, unknown>>)[0].options).toMatchObject({ assetPath: "${steps.4.path}" });
  });

  it("refuses what a flat plan cannot say", () => {
    const { config } = harness({
      hooked: { steps: { 1: { task: "asset.read" } }, finally: [{ task: "asset.read" }] },
      parent: { steps: { 1: { flow: "hooked" }, 2: { task: "asset.read", options: { x: "${steps.1.stepCount}" } } } },
    });
    const out = freezeFlow({ config, flowName: "parent", namespaces: {} }) as { refused: string[] };
    expect(out.refused.join("\n")).toMatch(/hooks or checks/);
    expect(out.refused.join("\n")).toMatch(/nested flow step 1/);
  });
});

describe("flow(plan) freezes, and the frozen plan runs the same steps", () => {
  it("returns it on the plan", async () => {
    const { ctx, flow } = harness();
    const plan = await flow.handler(ctx, { action: "plan", flowName: "outer", params: { target: "/Game/T" } }) as { frozen: FrozenPlan };
    expect(plan.frozen.from).toBe("outer");
    expect(Object.keys(plan.frozen.definition.steps as object)).toHaveLength(5);
  });

  it("runs what the original ran, call for call", async () => {
    const original = harness();
    const plan = await original.flow.handler(original.ctx, { action: "plan", flowName: "outer", params: { target: "/Game/T" } }) as { frozen: FrozenPlan };
    await original.flow.handler(original.ctx, { action: "run", flowName: "outer", params: { target: "/Game/T" } });

    const replay = harness({ outer_frozen: plan.frozen.definition });
    const body = await replay.flow.handler(replay.ctx, { action: "run", flowName: "outer_frozen" }) as { success: boolean };
    expect(body.success).toBe(true);
    expect(replay.calls).toEqual(original.calls);

    // Freezing the frozen plan changes nothing.
    const again = await replay.flow.handler(replay.ctx, { action: "plan", flowName: "outer_frozen" }) as { frozen: FrozenPlan };
    expect(again.frozen.definition.steps).toEqual(plan.frozen.definition.steps);
  });
});
