/**
 * demo(step) runs the step's demo_step_N flow and answers with the fields the
 * C++ dispatcher used to add, in the order it added them. Every step is built
 * from public actions and the internal primitives, never a demo bridge method.
 */
import { describe, expect, it } from "vitest";
import fs from "node:fs";
import { ALL_TOOLS } from "../../../src/tools.js";
import { demoTool } from "../../../src/tools/demo.js";
import { DEMO_STEPS } from "../../../src/tools/demo-steps.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { createLiveTask } from "../../../src/flow/live-task.js";
import type { FlowContext } from "../../../src/flow/context.js";
import type { IBridge } from "../../../src/bridge/bridge.js";
import type { ToolContext } from "../../../src/core/types.js";
import { repoPath } from "../../helpers/repo-root.js";

const CPP = repoPath("plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers", "Demo", "DemoHandlers.cpp");

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

/** A bridge that answers every method the demo flows call, as the editor does on a fresh scene. */
function editor(overrides: Record<string, (params: Record<string, unknown>) => unknown> = {}): Answer {
  return (method, params) => {
    const override = overrides[method];
    if (override) return override(params);
    const name = String(params.name);
    switch (method) {
      case "internal_spawn_actor":
        return {
          success: true, created: true, existed: false, actorLabel: params.label,
          actorPath: `/Game/Demo/DemoLevel.DemoLevel:PersistentLevel.${String(params.label)}_1`,
          actorName: `${String(params.label)}_1`, actorClass: params.actorClass,
        };
      case "internal_create_constant_material":
        return { success: true, saved: true, created: true, existed: false,
          assetPath: `${String(params.packagePath)}/${name}.${name}`, name };
      case "delete_asset":
        return { success: true, path: params.assetPath, alreadyDeleted: true };
      case "create_niagara_system_from_spec":
        return { success: true, created: true, saved: true, path: "/Game/Demo/NS_Demo_Aura.NS_Demo_Aura", emittersAdded: 1 };
      case "create_pcg_graph":
        return { success: true, created: true, path: "/Game/Demo/PCG_Demo_Scatter.PCG_Demo_Scatter", name };
      case "create_level_sequence":
        return { success: true, created: true, path: "/Game/Demo/SEQ_Demo_Showcase.SEQ_Demo_Showcase" };
      case "add_sequence_track":
        return { success: true, created: true, actorLabel: params.actorLabel, bindingGuid: "0A1B2C3D", trackType: "Transform" };
      case "create_editor_utility_widget":
        return { success: true, created: true, saved: true, path: "/Game/Demo/EUW_DemoTuning.EUW_DemoTuning", name: "EUW_DemoTuning" };
      default:
        return { success: true };
    }
  };
}

const TAGS = (index: number, id: string) => ({
  step: index, stepId: id, existed: false, created: true, replayNote: REPLAY, rollbackPossible: false, rollbackNote: NO_ROLLBACK,
});

/** What each step answers on a fresh scene, key for key, as its C++ step did. */
const ANSWERS: Record<number, Record<string, unknown>> = {
  2: {
    materials: ["/Game/Demo/M_Demo_Floor.M_Demo_Floor", "/Game/Demo/M_Demo_Glow.M_Demo_Glow", "/Game/Demo/M_Demo_Pillar.M_Demo_Pillar"],
    count: 3, success: true,
  },
  3: { actorLabel: "Demo_Floor", success: true },
  4: { actorLabel: "Demo_Pedestal", success: true },
  5: { actorLabel: "Demo_HeroSphere", success: true },
  6: { pillars: ["Demo_Pillar_NE", "Demo_Pillar_NW", "Demo_Pillar_SE", "Demo_Pillar_SW"], count: 4, success: true },
  7: { orbs: ["Demo_Orb_NE", "Demo_Orb_NW", "Demo_Orb_SE", "Demo_Orb_SW"], count: 4, success: true },
  8: { lights: ["Demo_Neon_Cyan", "Demo_Neon_Magenta", "Demo_Neon_Amber", "Demo_Neon_Violet"], count: 4, success: true },
  9: { actorLabel: "Demo_HeroLight", success: true },
  10: { actorLabel: "Demo_Moonlight", success: true },
  11: { actorLabel: "Demo_SkyLight", success: true },
  12: { actorLabel: "Demo_Fog", success: true },
  13: { actorLabel: "Demo_PostProcess", success: true },
  14: { actorLabel: "Demo_NiagaraVFX", assetPath: "/Game/Demo/NS_Demo_Aura.NS_Demo_Aura", success: true },
  15: { graphAssigned: true, actorLabel: "Demo_PCGScatter", note: "PCG volume placed. Configure the graph for scatter behavior.", success: true },
  16: { rotationAdded: true, orbitOrbs: Array.from({ length: 8 }, (_, i) => `Demo_OrbitOrb_${i}`), count: 8, success: true },
  17: {
    boundActor: "Demo_HeroSphere", bindingGuid: "0A1B2C3D", sequenceActorLabel: "Demo_SequenceActor",
    sequencePath: "/Game/Demo/SEQ_Demo_Showcase.SEQ_Demo_Showcase", success: true,
  },
  18: { assetPath: "/Game/Demo/EUW_DemoTuning.EUW_DemoTuning", status: "created", success: true },
};

describe("the demo step table", () => {
  it("matches the steps the editor lists", () => {
    const src = fs.readFileSync(CPP, "utf8");
    const cpp = [...src.matchAll(/Steps\.Add\(\{\s*(\d+),\s*TEXT\("([a-z_]+)"\),\s*TEXT\("([^"]*)"\)\s*\}\)/g)]
      .map((m) => [Number(m[1]), m[2], m[3]]);
    expect(cpp).toHaveLength(19);
    expect(DEMO_STEPS.map((s) => [s.index, s.id, s.description])).toEqual(cpp);
  });

  it("builds every step from registered tasks, with no demo primitive left", () => {
    expect(fs.readFileSync(CPP, "utf8")).not.toMatch(/TEXT\("demo_step"\)/);
    const tasks = new Set(registry.listRegistered());
    for (const s of DEMO_STEPS) {
      for (const child of Object.values(s.steps) as Array<{ task: string }>) {
        expect(tasks.has(child.task), `${s.index}: ${child.task}`).toBe(true);
      }
    }
  });
});

describe("demo(step) on a step built from actions and internal primitives", () => {
  it.each(Object.keys(ANSWERS).map(Number))("step %i answers as its C++ step did, key for key", async (index) => {
    const { json, result, calls } = await step(editor(), { stepIndex: index });
    expect(result.success).toBe(true);
    expect(json).toBe(JSON.stringify({ ...ANSWERS[index], ...TAGS(index, DEMO_STEPS[index - 1].id) }));
    expect(calls.some((c) => c.method === "demo_step")).toBe(false);
    expect(result.children?.[0]).toMatchObject({ type: "flow", name: `demo_step_${index}` });
  });

  it("spawns under a label whether or not it is taken, then configures the actor it made by path", async () => {
    const { calls } = await step(editor(), { stepIndex: 12 });
    expect(calls.map((c) => c.method)).toEqual(["internal_spawn_actor", "set_fog_properties", "set_component_property"]);
    expect(calls[0].params).toMatchObject({ actorClass: "ExponentialHeightFog", label: "Demo_Fog", folderPath: "Demo_Scene" });
    const actorPath = "/Game/Demo/DemoLevel.DemoLevel:PersistentLevel.Demo_Fog_1";
    expect(calls[1].params).toMatchObject({ actorPath, fogDensity: 0.035, fogHeightFalloff: 0.5 });
    expect(calls[2].params).toMatchObject({ actorPath, propertyName: "FogMaxOpacity", value: 0.85 });
  });

  it("gives a light the colour its sRGB byte colour had", async () => {
    const { calls } = await step(editor(), { stepIndex: 10 });
    expect(calls[1].params).toMatchObject({ mobility: "movable", intensity: 3, color: { r: 32.497, g: 47.894, b: 147.283 } });
  });

  it("rebuilds the demo materials on a replay", async () => {
    const { calls } = await step(editor(), { stepIndex: 2 });
    expect(calls.map((c) => c.method)).toEqual([
      "delete_asset", "internal_create_constant_material",
      "delete_asset", "internal_create_constant_material",
      "delete_asset", "internal_create_constant_material",
    ]);
    expect(calls[0].params).toEqual({ assetPath: "/Game/Demo/M_Demo_Floor", force: true });
    expect(calls[3].params).toMatchObject({ emissiveColor: { r: 1, g: 0.75, b: 0.1, a: 1 }, emissiveStrength: 10 });
  });

  it("keeps an existing tuning panel", async () => {
    const { json } = await step(editor({
      create_editor_utility_widget: () => ({ success: true, existed: true, created: false, path: "/Game/Demo/EUW_DemoTuning.EUW_DemoTuning" }),
    }), { stepIndex: 18 });
    expect(json).toBe(JSON.stringify({
      assetPath: "/Game/Demo/EUW_DemoTuning", success: true, status: "existed", ...TAGS(18, "tuning_panel"),
    }));
  });

  it("builds the sequence without a binding when the hero label is ambiguous", async () => {
    const { json, result } = await step(editor({
      add_sequence_track: () => ({ success: false, error: "Label 'Demo_HeroSphere' matches 2 actors", ambiguous: true }),
    }), { stepIndex: 17 });
    expect(result.success).toBe(true);
    expect(json).toBe(JSON.stringify({
      sequenceActorLabel: "Demo_SequenceActor", sequencePath: "/Game/Demo/SEQ_Demo_Showcase.SEQ_Demo_Showcase", success: true,
      ...TAGS(17, "level_sequence"),
    }));
  });

  it("marks a step that failed as neither created nor existed, keeping the reason", async () => {
    const { json } = await step(editor({ internal_spawn_actor: () => ({ success: false, error: "Failed to spawn actor" }) }), { step: 3 });
    expect(json).toBe(JSON.stringify({
      error: "Failed to spawn actor", success: false, step: 3, stepId: "floor", created: false, existed: false,
      rollbackPossible: false, rollbackNote: NO_ROLLBACK,
    }));
  });

  it("reports a material that did not save", async () => {
    const { json } = await step(editor({
      internal_create_constant_material: () => ({ success: false, saved: false, saveError: "read-only", error: "not written: read-only" }),
    }), { step: 2 });
    expect(JSON.parse(json)).toMatchObject({ materials: [], count: 0, success: false, saveErrors: ["read-only"], error: "read-only" });
  });

  it("answers a step the editor never answered as a failure, with the reason", async () => {
    const { result } = await step(() => { throw new Error("socket closed"); }, { step: 5 });
    expect(result.data).toMatchObject({ error: "socket closed", success: false, created: false });
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
    const { json } = await step(editor(), { step: 3, stepIndex: 4, foo: 1 });
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
    const { ctx, calls } = context(editor({ create_new_level: () => ({ success: true, created: true }) }));
    const out = await createFlowTool(registry, () => config)
      .handler(ctx as ToolContext, { action: "run", flowName: "neon_shrine" }) as Record<string, unknown>;
    expect(out.success).toBe(true);
    const methods = calls.map((c) => c.method);
    expect(methods.slice(0, 2)).toEqual(["create_new_level", "save_level"]);
    expect(methods.at(-1)).toBe("save_level");
    expect(methods).not.toContain("demo_step");
    // One bridge call per task step: every step of every flow ran.
    const taskSteps = DEMO_STEPS.flatMap((s) => Object.values(s.steps) as Array<{ when?: unknown }>)
      .filter((s) => s.when === undefined).length + 1;
    expect(methods).toHaveLength(taskSteps);
  });

  it("keeps demo(step) on its declared parameter spec", () => {
    const spec = demoTool.actions.step;
    expect(spec.kind).toBe("flow");
    expect(spec.kind === "flow" && spec.paramSpec?.map((p) => p.name)).toEqual(["step"]);
  });
});
