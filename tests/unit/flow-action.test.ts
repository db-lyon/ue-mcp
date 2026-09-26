/**
 * kind "flow": an action backed by a flow or a composite. Its children run
 * through flowkit's runner via ctx.step, on a live call and inside a flow,
 * so they are recorded, locked for the whole call and reduced into the
 * action's own response.
 */
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { FlowRunner } from "@db-lyon/flowkit";
import type { TaskDefinition, FlowDefinition } from "@db-lyon/flowkit";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../src/flow/schema.js";
import { createLiveTask } from "../../src/flow/live-task.js";
import { lockScopeOpener, resolveLockingConfig } from "../../src/dispatch/locking.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ChildOutcome, FlowActionSpec, ToolContext, ToolDef } from "../../src/core/types.js";

/** Delete then rename one asset, best-effort, reduced to a verdict per child. */
const moveAction: FlowActionSpec<ChildOutcome[]> = {
  kind: "flow",
  effect: "mutate",
  description: "Delete then rename, as children.",
  inputs: { assetPath: z.string().optional(), note: z.string().optional().describe("move: a note") },
  expand: (p) => [
    { task: "asset.delete", options: { assetPath: p.assetPath } },
    { task: "asset.rename", options: { assetPath: p.assetPath, newName: "B" } },
  ],
  compose: async (run, p) => [
    await run("asset.delete", { assetPath: p.assetPath }),
    await run("asset.rename", { assetPath: p.assetPath, newName: "B" }),
  ],
  result: (children, p) => ({ assetPath: p.assetPath, ok: children.map((c) => c.success) }),
};

/** Runs the `go` flow and reports what each of its steps answered. */
const replayAction: FlowActionSpec<ChildOutcome> = {
  kind: "flow",
  effect: "mutate",
  description: "Run the go flow.",
  inputs: {},
  expand: () => [{ flow: "go" }],
  compose: async (run) => run({ flow: "go" }),
  result: (child) => ({
    ran: child.success,
    steps: (child.steps ?? []).map((s) => [s.name, s.success, s.data?.echo ?? null]),
  }),
};

const assetTool: ToolDef = categoryTool(
  "asset",
  "Assets.",
  {
    list: bp("read", "list_assets"),
    delete: bp("mutate", "delete_asset"),
    rename: bp("mutate", "rename_asset"),
    move: moveAction,
    replay: replayAction,
  },
  { assetPath: z.string().optional().describe("the category's own assetPath") },
);

const goFlow = {
  go: {
    steps: {
      1: { task: "asset.list", options: { directory: "/Game" } },
      2: { task: "asset.delete", options: { assetPath: "/Game/Z" } },
    },
  },
};

function context(answer: (method: string, params: Record<string, unknown>) => unknown = () => undefined) {
  const calls: string[] = [];
  const bridge = {
    isConnected: true,
    call: async (method: string, params: Record<string, unknown>) => {
      calls.push(`${method}:${String(params.path ?? params.assetPath ?? params.directory ?? "")}`);
      const answered = answer(method, params);
      if (answered !== undefined) return answered;
      if (method === "acquire_lock") return { acquired: true };
      return { success: true, echo: method };
    },
  } as unknown as IBridge;
  const ctx = {
    project: {},
    bridge,
    openAssetLocks: lockScopeOpener(resolveLockingConfig({ enabled: true }), () => [assetTool]),
  } as unknown as FlowContext;
  return { ctx, calls };
}

function flowTool(flows: Record<string, unknown>) {
  const registry = buildFlowRegistry([assetTool]);
  const config = FlowConfigSchema.parse({ ...buildDefaults([assetTool]), flows });
  return createFlowTool(registry, () => config);
}

describe("a flow action's declaration", () => {
  it("renders inputs the category does not declare, and leaves the ones it does", () => {
    expect(Object.keys(assetTool.schema)).toContain("note");
    expect(assetTool.schema.assetPath.description).toBe("the category's own assetPath");
  });

  it("registers a composite whose expand lists the children for a plan", async () => {
    const registry = buildFlowRegistry([assetTool]);
    const runner = new FlowRunner({
      tasks: {} as Record<string, TaskDefinition>,
      flows: goFlow as unknown as Record<string, FlowDefinition>,
      registry,
      context: context().ctx,
    });
    expect(await runner.expandTask("asset.move", { assetPath: "/Game/A" })).toEqual([
      { task: "asset.delete", options: { assetPath: "/Game/A" } },
      { task: "asset.rename", options: { assetPath: "/Game/A", newName: "B" } },
    ]);
    expect(await runner.expandTask("asset.replay")).toEqual([{ flow: "go" }]);
  });
});

describe("a live call to a flow action", () => {
  it("runs its children through the runner, records them and reduces them", async () => {
    const { ctx } = context();
    const registry = buildFlowRegistry([assetTool]);
    const task = await createLiveTask({ registry }, ctx, "asset.move", { assetPath: "/Game/A" });
    const result = await task.run();
    expect(result.success).toBe(true);
    expect(result.data).toEqual({ assetPath: "/Game/A", ok: [true, true] });
    expect(result.children?.map((c) => [c.name, c.path, c.result?.success])).toEqual([
      ["asset.delete", "1", true],
      ["asset.rename", "2", true],
    ]);
  });

  it("holds its locks across every child and releases them when the call returns", async () => {
    const { ctx, calls } = context();
    const registry = buildFlowRegistry([assetTool]);
    const task = await createLiveTask({ registry }, ctx, "asset.move", { assetPath: "/Game/A" });
    await task.run();
    expect(calls).toEqual([
      "acquire_lock:/Game/A",
      "delete_asset:/Game/A",
      "rename_asset:/Game/A",
      "release_lock:/Game/A",
    ]);
  });

  it("keeps a failed child's verdict and data for the reducer", async () => {
    const { ctx } = context((method) => (method === "delete_asset" ? { success: false, error: "in use" } : undefined));
    const registry = buildFlowRegistry([assetTool]);
    const task = await createLiveTask({ registry }, ctx, "asset.move", { assetPath: "/Game/A" });
    const result = await task.run();
    expect(result.data).toEqual({ assetPath: "/Game/A", ok: [false, true] });
    expect(result.children?.[0].result).toMatchObject({ success: false, data: { success: false, error: "in use" } });
  });

  it("hands a flow child's step results to the reducer", async () => {
    const { ctx } = context();
    const registry = buildFlowRegistry([assetTool]);
    const task = await createLiveTask(
      { registry, flows: goFlow as unknown as Record<string, FlowDefinition> },
      ctx,
      "asset.replay",
      {},
    );
    const result = await task.run();
    expect(result.data).toEqual({
      ran: true,
      steps: [["asset.list", true, "list_assets"], ["asset.delete", true, "delete_asset"]],
    });
    expect(result.children?.[0]).toMatchObject({ type: "flow", name: "go" });
    expect(result.children?.[0].nestedSteps).toHaveLength(2);
  });

  it("answers the same through the category tool's own handler", async () => {
    const { ctx } = context();
    const out = await assetTool.handler(ctx as ToolContext, { action: "move", assetPath: "/Game/A" });
    expect(out).toEqual({ assetPath: "/Game/A", ok: [true, true] });
  });
});

describe("a flow action inside a flow", () => {
  it("records its children under its own step and holds the run's locks", async () => {
    const { ctx, calls } = context();
    const out = await flowTool({
      outer: { steps: { 1: { task: "asset.move", options: { assetPath: "/Game/A" } }, 2: { task: "asset.delete", options: { assetPath: "/Game/C" } } } },
    }).handler(ctx as ToolContext, { action: "run", flowName: "outer" }) as Record<string, unknown>;
    expect(out.success).toBe(true);
    const steps = out.steps as Array<{ name: string; data: unknown }>;
    expect(steps[0]).toMatchObject({ name: "asset.move", data: { assetPath: "/Game/A", ok: [true, true] } });
    expect(calls).toEqual([
      "acquire_lock:/Game/A",
      "delete_asset:/Game/A",
      "rename_asset:/Game/A",
      "acquire_lock:/Game/C",
      "delete_asset:/Game/C",
      "release_lock:/Game/A",
      "release_lock:/Game/C",
    ]);
  });

  it("runs a flow child from the run's own flows", async () => {
    const { ctx } = context();
    const out = await flowTool({ ...goFlow, outer: { steps: { 1: { task: "asset.replay" } } } })
      .handler(ctx as ToolContext, { action: "run", flowName: "outer" }) as Record<string, unknown>;
    const steps = out.steps as Array<{ data: unknown }>;
    expect(steps[0].data).toEqual({
      ran: true,
      steps: [["asset.list", true, "list_assets"], ["asset.delete", true, "delete_asset"]],
    });
  });
});

describe("a flow action's verdict and data", () => {
  it("keeps a live call's body when the reduced answer says success:false, and fails the step in a flow", async () => {
    const { ctx } = context((method) => (method === "delete_asset" ? { success: false, error: "in use" } : undefined));
    const refusing = categoryTool("asset", "Assets.", {
      delete: bp("mutate", "delete_asset"),
      try_delete: {
        kind: "flow",
        effect: "mutate",
        inputs: {},
        compose: async (run, p) => run("asset.delete", { assetPath: p.assetPath }),
        result: (child: ChildOutcome) => child.data,
      } satisfies FlowActionSpec<ChildOutcome>,
    });
    const registry = buildFlowRegistry([refusing]);
    const live = await (await createLiveTask({ registry }, ctx, "asset.try_delete", { assetPath: "/Game/A" })).run();
    expect(live).toMatchObject({ success: true, data: { success: false, error: "in use" } });

    const config = FlowConfigSchema.parse({
      ...buildDefaults([refusing]),
      flows: { f: { steps: { 1: { task: "asset.try_delete", options: { assetPath: "/Game/A" } } } } },
    });
    const out = await createFlowTool(registry, () => config)
      .handler(ctx as ToolContext, { action: "run", flowName: "f" }) as Record<string, unknown>;
    expect(out.success).toBe(false);
  });

  it("passes a caller's reference-shaped text to its children as data", async () => {
    const { ctx, calls } = context();
    const registry = buildFlowRegistry([assetTool]);
    const literal = "$" + "{steps.1.path}";
    await (await createLiveTask({ registry }, ctx, "asset.move", { assetPath: literal })).run();
    expect(calls).toContain(`delete_asset:${literal}`);
  });
});
