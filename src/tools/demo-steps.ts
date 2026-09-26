/**
 * The Neon Shrine demo steps as flows (`demo_step_1` .. `demo_step_19`), and
 * the reduction of each flow's results into the demo(step) response.
 *
 * A step moved out of C++ when it is a fixed sequence of existing primitives
 * with no intermediate-state hazard (plans/task-flow-architecture.md 3.7).
 * The rest stay one C++ primitive, `demo_step` with the step's index, because
 * each needs behaviour no primitive exposes: spawning under a label that is
 * already taken (the documented replay behaviour; place_actor and spawn_light
 * skip or refuse a taken label), the demo's own material graphs, an editor
 * utility widget parented to EditorUtilityWidget, a sequence binding.
 */
import type { ChildOutcome } from "../core/types.js";
import { handlerFailure } from "../flow/handler-outcome.js";

export const DEMO_LEVEL = "/Game/Demo/DemoLevel";

type StepResults = NonNullable<ChildOutcome["steps"]>;

interface DemoStep {
  index: number;
  id: string;
  description: string;
  /** The flow's steps. */
  steps: Record<string, unknown>;
  /** The step's own answer, before the demo-wide fields are added. */
  reduce: (steps: StepResults) => Record<string, unknown>;
}

/** A step still served by the C++ primitive, which answers the step's result itself. */
function inCpp(index: number, id: string, description: string): DemoStep {
  return {
    index,
    id,
    description,
    steps: { 1: { task: "ue-mcp.bridge", options: { method: "demo_step", step: index } } },
    reduce: (steps) => {
      const answered = [...steps].reverse().find((s) => s.data !== undefined);
      if (!answered?.data) throw steps.find((s) => s.error)?.error ?? new Error(`demo step ${index} did not answer`);
      return { ...answered.data };
    },
  };
}

/** The C++ step's failure shape: error first, then success. */
function failed(step: StepResults[number] | undefined, message?: string): Record<string, unknown> {
  return {
    error: message ?? handlerFailure(step?.data) ?? step?.error?.message ?? "the step did not run",
    success: false,
  };
}

const createLevel: DemoStep = {
  index: 1,
  id: "create_level",
  description: "Create new level at /Game/Demo/DemoLevel",
  // An existing demo level is opened rather than recreated, which on an
  // existing path would land the editor on an Untitled map.
  steps: {
    1: { task: "level.create", options: { levelPath: DEMO_LEVEL } },
    2: { task: "level.load", options: { levelPath: DEMO_LEVEL }, when: "${steps.1.existed}" },
    3: { task: "level.save", options: {}, when: "${steps.1.created}" },
  },
  reduce: (steps) => {
    const [create, load, save] = steps;
    if (!create?.success) return failed(create);
    const created = create.data?.created === true;
    if (created && save && !save.skipped && !save.success) {
      return failed(save, `${DEMO_LEVEL} was created but could not be saved`);
    }
    if (!created && load && !load.skipped && !load.success) return failed(load);
    return { levelPath: DEMO_LEVEL, created, success: true };
  },
};

const save: DemoStep = {
  index: 19,
  id: "save",
  description: "Save current level",
  steps: { 1: { task: "level.save", options: {} } },
  reduce: (steps) => {
    const answered = steps[0]?.data ?? {};
    const saved = steps[0]?.success === true;
    return {
      ...(typeof answered.levelName === "string" ? { levelName: answered.levelName } : {}),
      ...(typeof answered.levelPath === "string" ? { levelPath: answered.levelPath } : {}),
      saved,
      success: saved,
    };
  },
};

/** Every step, in order. The ids and descriptions match demo_get_steps in C++. */
export const DEMO_STEPS: readonly DemoStep[] = [
  createLevel,
  inCpp(2, "materials", "Create 3 materials: floor, glow, pillar"),
  inCpp(3, "floor", "60m dark reflective floor"),
  inCpp(4, "pedestal", "Central pedestal cylinder"),
  inCpp(5, "hero_sphere", "Emissive gold hero sphere"),
  inCpp(6, "pillars", "4 corner pillar cylinders"),
  inCpp(7, "orbs", "4 glowing orbs at pillar bases"),
  inCpp(8, "neon_lights", "4 coloured point lights"),
  inCpp(9, "hero_light", "Warm point light above hero"),
  inCpp(10, "moonlight", "Directional moon light"),
  inCpp(11, "sky_light", "SkyLight ambient fill"),
  inCpp(12, "fog", "ExponentialHeightFog atmosphere"),
  inCpp(13, "post_process", "PostProcessVolume bloom/vignette"),
  inCpp(14, "niagara_vfx", "Niagara particle system above hero"),
  inCpp(15, "pcg_scatter", "PCG scatter volume on floor"),
  inCpp(16, "orbit_rings", "8 orbiting emissive spheres + rotation"),
  inCpp(17, "level_sequence", "LevelSequence with hero binding"),
  inCpp(18, "tuning_panel", "EditorUtilityWidget tuning panel"),
  save,
];

export function demoFlowName(index: number): string {
  return `demo_step_${index}`;
}

/** The universal `demo_step_N` flows. */
export function demoStepFlows(): Record<string, unknown> {
  return Object.fromEntries(DEMO_STEPS.map((s) => [
    demoFlowName(s.index),
    { description: `Neon Shrine demo step ${s.index} (${s.id}): ${s.description}.`, steps: s.steps },
  ]));
}

const REPLAY_NOTE = "Demo steps are not idempotent: running this step again spawns a second set of Demo_ actors. "
  + "Run demo(cleanup) before replaying.";

const ROLLBACK_NOTE = "No inverse is emitted. Demo steps all write into the same /Game/Demo folder and the same Demo_ "
  + "actors and none records what it alone created, so nothing can undo one step. demo(cleanup) removes "
  + "the whole demo scene and, on the way, creates /Game/MCP_Home if it is missing and switches the "
  + "editor to it, then deletes by label prefix in whatever level is then open - run it deliberately "
  + "when you mean to discard the entire demo, not as a rollback for one step.";

/**
 * The fields every step's answer carries, in the order the C++ dispatcher
 * wrote them. A key the step already set keeps its place, as it did there.
 */
export function tagDemoStep(raw: Record<string, unknown>, step: DemoStep): Record<string, unknown> {
  const out: Record<string, unknown> = { ...raw };
  out.step = step.index;
  out.stepId = step.id;
  // Demo steps always CREATE, so a replayed step leaves two of everything;
  // created rather than a bare success is what makes that visible.
  if (typeof out.success === "boolean" ? out.success : true) {
    out.existed = false;
    out.created = true;
    out.replayNote = REPLAY_NOTE;
  } else {
    out.created = false;
    out.existed = false;
  }
  // No executable inverse: demo(cleanup) removes the whole scene, not one step.
  out.rollbackPossible = false;
  out.rollbackNote = ROLLBACK_NOTE;
  return out;
}

/** The step index a demo(step) call names, read as the editor read it; undefined for the step list. */
export function demoStepIndex(input: Record<string, unknown>): number | undefined {
  const raw = input.step ?? input.stepIndex;
  if (typeof raw === "number" && Number.isFinite(raw)) return Math.trunc(raw);
  if (typeof raw === "boolean") return raw ? 1 : 0;
  if (typeof raw === "string" && raw.trim() !== "" && Number.isFinite(Number(raw))) return Math.trunc(Number(raw));
  return undefined;
}

/** Keys a demo(step) call sent that the step never read, as the editor reported them. */
export function demoUnreadParams(input: Record<string, unknown>): string[] {
  const read = new Set(["step"]);
  if (input.step === undefined) read.add("stepIndex");
  return Object.keys(input).filter((k) => !read.has(k) && input[k] !== undefined);
}
