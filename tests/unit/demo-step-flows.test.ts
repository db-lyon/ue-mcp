/**
 * demo(step) runs the step's demo_step_N flow and answers with the fields the
 * C++ dispatcher used to add, in the order it added them. Steps 1 and 19 are
 * level actions; the rest call the demo_step primitive.
 */
import { describe, expect, it } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS } from "../../src/tools.js";
import { demoTool } from "../../src/tools/demo.js";
import { DEMO_STEPS } from "../../src/tools/demo-steps.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../src/flow/schema.js";
import { createLiveTask } from "../../src/flow/live-task.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const CPP = path.join(here, "..", "..", "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers", "Demo", "DemoHandlers.cpp");

const REPLAY = "Demo steps are not idempotent: running this step again spawns a second set of Demo_ actors. Run demo(cleanup) before replaying.";
const NO_ROLLBACK = "No inverse is emitted. Demo steps all write into the same /Game/Demo folder and the same Demo_ actors and none records what it alone created, so nothing can undo one step. demo(cleanup) removes the whole demo scene and, on the way, creates /Game/MCP_Home if it is missing and switches the editor to it, then deletes by label prefix in whatever level is then open - run it deliberately when you mean to discard the entire demo, not as a rollback for one step.";

type Answer = (method: string, params: Record<string, unknown>) => unknown;

function context(answer: Answer) {
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  const bridge = {
    isConnected: true,
    call: async (method: string, params: Record<string, unknown>) => {
      calls.push({ method, params });
      return answer(method, params);
    },
  } as unknown as IBridge;
  return { ctx: { project: {}, bridge } as unknown as FlowContext, calls };
}

const registry = buildFlowRegistry(ALL_TOOLS);

async function step(answer: Answer, params: Record<string, unknown>) {
  const { ctx, calls } = context(answer);
  const result = await (await createLiveTask({ registry }, ctx, "demo.step", params)).run();
  return { result, calls, json: JSON.stringify(result.data) };
}

describe("the demo step table", () => {
  it("matches the steps the editor lists", () => {
    const src = fs.readFileSync(CPP, "utf8");
    const cpp = [...src.matchAll(/Steps\.Add\(\{\s*(\d+),\s*TEXT\("([a-z_]+)"\),\s*TEXT\("([^"]*)"\)\s*\}\)/g)]
      .map((m) => [Number(m[1]), m[2], m[3]]);
    expect(cpp).toHaveLength(19);
    expect(DEMO_STEPS.map((s) => [s.index, s.id, s.description])).toEqual(cpp);
  });

  it("keeps in C++ only the steps the editor still builds", () => {
    const src = fs.readFileSync(CPP, "utf8");
    const built = [...src.matchAll(/case (\d+):\s+StepResult = /g)].map((m) => Number(m[1]));
    const viaPrimitive = DEMO_STEPS.filter((s) => JSON.stringify(s.steps).includes("\"demo_step\"")).map((s) => s.index);
    expect(built).toEqual(viaPrimitive);
  });
});

describe("demo(step) on a step the editor builds", () => {
  it("sends the index to the primitive and answers as the C++ dispatcher did", async () => {
    const { calls, json, result } = await step(() => ({ success: true, actorLabel: "Demo_Floor" }), { stepIndex: 3 });
    expect(calls).toEqual([{ method: "demo_step", params: { step: 3 } }]);
    expect(result.success).toBe(true);
    expect(json).toBe(JSON.stringify({
      success: true, actorLabel: "Demo_Floor", step: 3, stepId: "floor", existed: false, created: true,
      replayNote: REPLAY, rollbackPossible: false, rollbackNote: NO_ROLLBACK,
    }));
    expect(result.children?.[0]).toMatchObject({ type: "flow", name: "demo_step_3" });
  });

  it("marks a step that failed as neither created nor existed, keeping its body", async () => {
    const { json } = await step(() => ({ error: "Failed to spawn floor", success: false }), { step: 3 });
    expect(json).toBe(JSON.stringify({
      error: "Failed to spawn floor", success: false, step: 3, stepId: "floor", created: false, existed: false,
      rollbackPossible: false, rollbackNote: NO_ROLLBACK,
    }));
  });

  it("fails the call when the primitive does not answer", async () => {
    const { result } = await step(() => { throw new Error("socket closed"); }, { step: 5 });
    expect(result.success).toBe(false);
    expect(result.error?.message).toBe("socket closed");
  });
});

describe("demo(step) on a step built from level actions", () => {
  const created = (method: string) => (method === "create_new_level"
    ? { success: true, existed: false, created: true, levelPath: "/Game/Demo/DemoLevel" }
    : { success: true });

  it("creates and saves a new demo level", async () => {
    const { calls, json } = await step(created, { stepIndex: 1 });
    expect(calls.map((c) => c.method)).toEqual(["create_new_level", "save_level"]);
    expect(calls[0].params).toEqual({ levelPath: "/Game/Demo/DemoLevel" });
    expect(json).toBe(JSON.stringify({
      levelPath: "/Game/Demo/DemoLevel", created: true, success: true, step: 1, stepId: "create_level", existed: false,
      replayNote: REPLAY, rollbackPossible: false, rollbackNote: NO_ROLLBACK,
    }));
  });

  it("opens an existing demo level instead of recreating it", async () => {
    const { calls } = await step((m) => (m === "create_new_level"
      ? { success: true, existed: true, created: false, levelPath: "/Game/Demo/DemoLevel" }
      : { success: true }), { stepIndex: 1 });
    expect(calls.map((c) => c.method)).toEqual(["create_new_level", "load_level"]);
  });

  it("reports a new level that could not be saved", async () => {
    const { json } = await step((m) => (m === "save_level" ? { success: false, error: "read-only" } : created(m)), { stepIndex: 1 });
    expect(json).toBe(JSON.stringify({
      error: "/Game/Demo/DemoLevel was created but could not be saved", success: false,
      step: 1, stepId: "create_level", created: false, existed: false, rollbackPossible: false, rollbackNote: NO_ROLLBACK,
    }));
  });

  it("saves the level and names it", async () => {
    const saved = { success: true, levelName: "DemoLevel", levelPath: "/Game/Demo/DemoLevel.DemoLevel", savedCount: 1 };
    const { calls, json } = await step(() => saved, { stepIndex: 19 });
    expect(calls.map((c) => c.method)).toEqual(["save_level"]);
    expect(json).toBe(JSON.stringify({
      levelName: "DemoLevel", levelPath: "/Game/Demo/DemoLevel.DemoLevel", saved: true, success: true,
      step: 19, stepId: "save", existed: false, created: true,
      replayNote: REPLAY, rollbackPossible: false, rollbackNote: NO_ROLLBACK,
    }));
    const failed = await step(() => ({ ...saved, success: false, error: "locked" }), { stepIndex: 19 });
    expect(failed.result.data).toMatchObject({ saved: false, success: false, created: false });
  });
});

describe("demo(step) without a step to run", () => {
  it("answers the step list when no step is named", async () => {
    const list = { success: true, steps: [{ index: 1, id: "create_level", description: "x" }], count: 1 };
    const { calls, json } = await step(() => list, {});
    expect(calls).toEqual([{ method: "demo_get_steps", params: {} }]);
    expect(json).toBe(JSON.stringify(list));
  });

  it("refuses an index outside 1 to 19 without calling the editor", async () => {
    const { calls, result } = await step(() => ({ success: true }), { step: 20 });
    expect(calls).toEqual([]);
    expect(result.data).toEqual({ success: false, error: "Invalid step index 20. Valid range: 1-19" });
  });

  it("reports parameters no step reads, as the editor did", async () => {
    const { json } = await step(() => ({ success: true, actorLabel: "Demo_Floor" }), { step: 3, stepIndex: 4, foo: 1 });
    expect(JSON.parse(json).paramsNotRead).toEqual({
      params: ["stepIndex", "foo"],
      note: "The editor received these parameters and this action never read them, so they had no effect. "
        + "project(action=\"describe_action\") lists what it takes.",
    });
  });
});

describe("neon_shrine", () => {
  it("is a flow of the nineteen demo step flows", async () => {
    const config = FlowConfigSchema.parse(buildDefaults(ALL_TOOLS));
    const steps = (config.flows.neon_shrine as { steps: Record<string, unknown> }).steps;
    expect(Object.values(steps)).toEqual(DEMO_STEPS.map((s) => ({ flow: `demo_step_${s.index}` })));
    const { ctx, calls } = context((m) => (m === "create_new_level" ? { success: true, created: true } : { success: true }));
    const out = await createFlowTool(registry, () => config)
      .handler(ctx as ToolContext, { action: "run", flowName: "neon_shrine" }) as Record<string, unknown>;
    expect(out.success).toBe(true);
    expect(calls.map((c) => c.method)).toEqual([
      "create_new_level", "save_level",
      ...Array.from({ length: 17 }, () => "demo_step"),
      "save_level",
    ]);
  });

  it("keeps demo(step) on its recorded parameter spec", () => {
    const spec = demoTool.actions.step;
    expect(spec.kind).toBe("flow");
    expect(spec.kind === "flow" && spec.paramSpec?.map((p) => p.name)).toEqual(["step"]);
  });
});
