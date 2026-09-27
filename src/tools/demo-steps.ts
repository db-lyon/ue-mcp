/**
 * The Neon Shrine demo steps as flows (`demo_step_1` .. `demo_step_19`), and
 * the reduction of each flow's results into the demo(step) response.
 *
 * Every step is a fixed sequence of public actions plus two internal
 * primitives (src/flow/internal-tasks.ts): `internal.spawn_actor`, because the
 * documented replay spawns a second set under labels already taken, which
 * place_actor and spawn_light skip; and `internal.create_constant_material`,
 * for the demo's material graphs in one save. Each spawn answers with the new
 * actor's path, and the configuring actions address that path, so a replay
 * configures the actor it just made rather than refusing an ambiguous label.
 */
import type { ChildOutcome } from "../core/types.js";
import { handlerFailure } from "../flow/handler-outcome.js";

export const DEMO_LEVEL = "/Game/Demo/DemoLevel";

const DEMO_DIR = "/Game/Demo";
const FOLDER = "Demo_Scene";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SPHERE = "/Engine/BasicShapes/Sphere.Sphere";
const CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder";
const FOUNTAIN = "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain";

type StepResults = NonNullable<ChildOutcome["steps"]>;
type StepResult = StepResults[number];

interface DemoStep {
  index: number;
  id: string;
  description: string;
  /** The flow's steps. */
  steps: Record<string, unknown>;
  /** The step's own answer, before the demo-wide fields are added. */
  reduce: (steps: StepResults) => Record<string, unknown>;
}

type Vec3 = { x: number; y: number; z: number };
const vec = (x: number, y: number, z: number): Vec3 => ({ x, y, z });
const uniform = (s: number): Vec3 => vec(s, s, s);

/** A demo material's object path. */
const material = (name: string): string => `${DEMO_DIR}/${name}.${name}`;

/**
 * An sRGB byte as set_light_properties reads a channel (value / 255, linear),
 * so the light gets the colour FLinearColor(FColor) gave it.
 */
function linearByte(byte: number): number {
  const c = byte / 255;
  const linear = c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  return Math.round(linear * 255 * 1000) / 1000;
}
const lightColor = (r: number, g: number, b: number) => ({ r: linearByte(r), g: linearByte(g), b: linearByte(b) });

/** Builds a flow's numbered steps; each `add` returns the step's position in the results. */
class FlowSteps {
  readonly steps: Record<string, unknown> = {};
  private count = 0;

  add(task: string, options: Record<string, unknown>, extra: Record<string, unknown> = {}): number {
    this.count += 1;
    this.steps[String(this.count)] = { task, options, ...extra };
    return this.count - 1;
  }

  /** A reference to a field of the step at `position`. */
  ref(position: number, field: string): string {
    return `\${steps.${position + 1}.${field}}`;
  }

  /** Spawn under `label` whether or not it is taken, into the demo folder. */
  spawn(actorClass: string, label: string, options: Record<string, unknown> = {}): number {
    return this.add("internal.spawn_actor", { actorClass, label, folderPath: FOLDER, ...options });
  }

  /** A StaticMeshActor with its mesh and material. */
  mesh(label: string, mesh: string, location: Vec3, scale: Vec3, mat: string): number {
    return this.spawn("StaticMeshActor", label, { location, scale, staticMesh: mesh, material: material(mat) });
  }
}

const ran = (s: StepResult | undefined): s is StepResult => s !== undefined && !s.skipped;
const ok = (s: StepResult | undefined): boolean => ran(s) && s.success;

/** The first step that failed without declaring it would, if any. */
function firstFailure(steps: StepResults, tolerated: ReadonlySet<number> = new Set()): StepResult | undefined {
  return steps.find((s, i) => ran(s) && !s.success && !tolerated.has(i));
}

/** The failure shape every step answered with: error first, then success. */
function failed(step: StepResult | undefined, message?: string): Record<string, unknown> {
  return {
    error: message ?? handlerFailure(step?.data) ?? step?.error?.message ?? "the step did not run",
    success: false,
  };
}

const str = (s: StepResult | undefined, field: string): string | undefined => {
  const v = s?.data?.[field];
  return typeof v === "string" ? v : undefined;
};

/** One actor spawned, then configured: its label, or the first failure. */
function oneActor(steps: StepResults): Record<string, unknown> {
  const failure = firstFailure(steps);
  if (failure || !ok(steps[0])) return failed(failure ?? steps[0]);
  return { actorLabel: str(steps[0], "actorLabel"), success: true };
}

/** Several actors, each a spawn then `perActor - 1` configuring steps: the labels that finished. */
function actorList(key: string, expected: number, perActor: number, offset = 0) {
  return (steps: StepResults): Record<string, unknown> => {
    const labels: string[] = [];
    for (let i = 0; i < expected; i++) {
      const own = steps.slice(offset + i * perActor, offset + (i + 1) * perActor);
      if (own.length === perActor && own.every(ok)) labels.push(str(own[0], "actorLabel") ?? "");
    }
    return { [key]: labels, count: labels.length, success: labels.length === expected && !firstFailure(steps) };
  };
}

function levelStep(index: number, id: string, description: string, build: (f: FlowSteps) => void,
  reduce: DemoStep["reduce"]): DemoStep {
  const f = new FlowSteps();
  build(f);
  return { index, id, description, steps: f.steps, reduce };
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

/** Each demo material: deleted when it exists, so a replay rebuilds it, then created and saved. */
const MATERIALS: Array<{ name: string; options: Record<string, unknown> }> = [
  // Dark reflective floor.
  { name: "M_Demo_Floor", options: { baseColor: { r: 0.02, g: 0.02, b: 0.025, a: 1 }, metallic: 0.9, roughness: 0.15 } },
  // Emissive gold.
  { name: "M_Demo_Glow", options: {
    baseColor: { r: 0.8, g: 0.6, b: 0.1, a: 1 }, metallic: 0.5, roughness: 0.3,
    emissiveColor: { r: 1, g: 0.75, b: 0.1, a: 1 }, emissiveStrength: 10,
  } },
  // Dark matte pillar.
  { name: "M_Demo_Pillar", options: { baseColor: { r: 0.05, g: 0.05, b: 0.06, a: 1 }, metallic: 0, roughness: 0.85 } },
];

const materials = levelStep(2, "materials", "Create 3 materials: floor, glow, pillar", (f) => {
  for (const m of MATERIALS) {
    f.add("asset.delete", { assetPath: `${DEMO_DIR}/${m.name}`, force: true });
    f.add("internal.create_constant_material", { name: m.name, packagePath: DEMO_DIR, ...m.options });
  }
}, (steps) => {
  const creates = MATERIALS.map((_, i) => steps[i * 2 + 1]);
  const paths = creates.filter(ok).map((s) => str(s, "assetPath") ?? "");
  const out: Record<string, unknown> = { materials: paths, count: paths.length, success: paths.length === MATERIALS.length };
  const saveErrors = creates.map((s) => str(s, "saveError")).filter((e): e is string => e !== undefined);
  const failure = firstFailure(steps);
  if (saveErrors.length > 0) out.saveErrors = saveErrors;
  if (saveErrors.length > 0 || failure) {
    out.success = false;
    out.error = saveErrors.length > 0 ? saveErrors.join(" ") : failed(failure).error;
  }
  return out;
});

const floor = levelStep(3, "floor", "60m dark reflective floor",
  (f) => { f.mesh("Demo_Floor", CUBE, vec(0, 0, -5), vec(60, 60, 0.1), "M_Demo_Floor"); }, oneActor);

const pedestal = levelStep(4, "pedestal", "Central pedestal cylinder",
  (f) => { f.mesh("Demo_Pedestal", CYLINDER, vec(0, 0, 75), vec(2.5, 2.5, 1.5), "M_Demo_Pillar"); }, oneActor);

const heroSphere = levelStep(5, "hero_sphere", "Emissive gold hero sphere", (f) => {
  const hero = f.mesh("Demo_HeroSphere", SPHERE, vec(0, 0, 260), uniform(1.8), "M_Demo_Glow");
  // Movable, for the rotation the orbit_rings step adds.
  f.add("level.set_actor_mobility", { actorPath: f.ref(hero, "actorPath"), mobility: "movable" });
}, oneActor);

const CORNERS: Array<[string, number, number]> = [["NE", 600, 600], ["NW", -600, 600], ["SE", 600, -600], ["SW", -600, -600]];

const pillars = levelStep(6, "pillars", "4 corner pillar cylinders", (f) => {
  for (const [c, x, y] of CORNERS) f.mesh(`Demo_Pillar_${c}`, CYLINDER, vec(x, y, 200), vec(0.6, 0.6, 4), "M_Demo_Pillar");
}, actorList("pillars", 4, 1));

const orbs = levelStep(7, "orbs", "4 glowing orbs at pillar bases", (f) => {
  for (const [c, x, y] of CORNERS) f.mesh(`Demo_Orb_${c}`, SPHERE, vec(x, y, 30), uniform(0.4), "M_Demo_Glow");
}, actorList("orbs", 4, 1));

/** A movable point light: spawned, then given its intensity and colour. */
function pointLight(f: FlowSteps, label: string, location: Vec3, color: [number, number, number], intensity: number): void {
  const light = f.spawn("PointLight", label, { location });
  f.add("level.set_light_properties", {
    actorPath: f.ref(light, "actorPath"), mobility: "movable", intensity, color: lightColor(...color),
  });
}

const neonLights = levelStep(8, "neon_lights", "4 coloured point lights", (f) => {
  pointLight(f, "Demo_Neon_Cyan", vec(600, 600, 350), [0, 220, 255], 80000);
  pointLight(f, "Demo_Neon_Magenta", vec(-600, 600, 350), [255, 0, 180], 80000);
  pointLight(f, "Demo_Neon_Amber", vec(600, -600, 350), [255, 170, 0], 80000);
  pointLight(f, "Demo_Neon_Violet", vec(-600, -600, 350), [130, 0, 255], 80000);
}, actorList("lights", 4, 2));

const heroLight = levelStep(9, "hero_light", "Warm point light above hero",
  (f) => pointLight(f, "Demo_HeroLight", vec(80, -80, 500), [255, 225, 190], 120000), oneActor);

const moonlight = levelStep(10, "moonlight", "Directional moon light", (f) => {
  const moon = f.spawn("DirectionalLight", "Demo_Moonlight", { rotation: { pitch: -30, yaw: 210, roll: 0 } });
  f.add("level.set_light_properties", {
    actorPath: f.ref(moon, "actorPath"), mobility: "movable", intensity: 3, color: lightColor(100, 120, 200),
  });
}, oneActor);

const skyLight = levelStep(11, "sky_light", "SkyLight ambient fill", (f) => {
  const sky = f.spawn("SkyLight", "Demo_SkyLight", { location: vec(0, 0, 500) });
  f.add("level.set_light_properties", {
    actorPath: f.ref(sky, "actorPath"), mobility: "movable", intensity: 0.3, recaptureSky: true,
  });
}, oneActor);

const fog = levelStep(12, "fog", "ExponentialHeightFog atmosphere", (f) => {
  const fogActor = f.spawn("ExponentialHeightFog", "Demo_Fog");
  const actorPath = f.ref(fogActor, "actorPath");
  f.add("level.set_fog_properties", { actorPath, fogDensity: 0.035, fogHeightFalloff: 0.5 });
  f.add("level.set_component_property", {
    actorPath, componentName: "HeightFogComponent0", propertyName: "FogMaxOpacity", value: 0.85,
  });
}, oneActor);

const postProcess = levelStep(13, "post_process", "PostProcessVolume bloom/vignette", (f) => {
  const volume = f.spawn("PostProcessVolume", "Demo_PostProcess");
  const actorPath = f.ref(volume, "actorPath");
  f.add("level.set_actor_property", { actorPath, propertyName: "bUnbound", value: true });
  f.add("level.set_post_process_settings", {
    actorPath, settings: { BloomIntensity: 2, VignetteIntensity: 0.6, AutoExposureBias: -1 },
  });
}, oneActor);

const NIAGARA_SYSTEM = `${DEMO_DIR}/NS_Demo_Aura`;

const niagaraVfx = levelStep(14, "niagara_vfx", "Niagara particle system above hero", (f) => {
  // Replaced on a replay, as the scene it feeds is.
  f.add("asset.delete", { assetPath: NIAGARA_SYSTEM, force: true });
  const system = f.add("niagara.create_system_from_spec", {
    name: "NS_Demo_Aura", packagePath: DEMO_DIR, emitters: [{ path: FOUNTAIN }],
  });
  const actor = f.spawn("NiagaraActor", "Demo_NiagaraVFX", { location: vec(0, 0, 380) });
  const actorPath = f.ref(actor, "actorPath");
  f.add("level.set_component_property", {
    actorPath, componentName: "NiagaraComponent0", propertyName: "Asset", value: f.ref(system, "path"),
  });
  f.add("niagara.reactivate", { actorPath });
}, (steps) => {
  const create = steps[1];
  if (ok(create) && create.data?.emittersAdded === 0) {
    return failed(create, "Could not load engine Fountain emitter template");
  }
  const failure = firstFailure(steps);
  if (failure) return failed(failure);
  return { actorLabel: str(steps[2], "actorLabel"), assetPath: str(create, "path"), success: true };
});

const pcgScatter = levelStep(15, "pcg_scatter", "PCG scatter volume on floor", (f) => {
  // The spawned volume's cube has a 100 unit half extent, so this bounds the
  // 3000 x 3000 x 300 region the surface sampler scatters within.
  const volume = f.spawn("PCGVolume", "Demo_PCGScatter", { scale: vec(30, 30, 3) });
  const graph = f.add("pcg.create_graph", { name: "PCG_Demo_Scatter", packagePath: DEMO_DIR });
  f.add("pcg.toggle_graph", { actorPath: f.ref(volume, "actorPath"), graphPath: f.ref(graph, "path") });
}, (steps) => {
  const failure = firstFailure(steps);
  if (failure || !ok(steps[0])) return failed(failure ?? steps[0]);
  return {
    graphAssigned: true,
    actorLabel: str(steps[0], "actorLabel"),
    note: "PCG volume placed. Configure the graph for scatter behavior.",
    success: true,
  };
});

const ORBIT_ORBS = 8;
const ORBIT_RADIUS = 220;
const ORBIT_HEIGHT = 280;

const orbitRings = levelStep(16, "orbit_rings", "8 orbiting emissive spheres + rotation", (f) => {
  // An invisible pivot the orbs hang from, turning at 45 degrees a second.
  const pivot = f.spawn("Actor", "Demo_OrbitPivot");
  const parentPath = f.ref(pivot, "actorPath");
  f.add("level.set_actor_mobility", { actorPath: parentPath, mobility: "movable" });
  f.add("level.add_component", { actorPath: parentPath, componentClass: "RotatingMovementComponent", componentName: "DemoRotation" });
  f.add("level.set_component_property", {
    actorPath: parentPath, componentName: "DemoRotation", propertyName: "RotationRate", value: { Pitch: 0, Yaw: 45, Roll: 0 },
  });
  for (let i = 0; i < ORBIT_ORBS; i++) {
    const angle = (2 * Math.PI * i) / ORBIT_ORBS;
    const location = vec(ORBIT_RADIUS * Math.cos(angle), ORBIT_RADIUS * Math.sin(angle), ORBIT_HEIGHT);
    const orb = f.mesh(`Demo_OrbitOrb_${i}`, SPHERE, location, uniform(0.2), "M_Demo_Glow");
    const childPath = f.ref(orb, "actorPath");
    f.add("level.set_actor_mobility", { actorPath: childPath, mobility: "movable" });
    f.add("level.attach_actor", { childPath, parentPath, attachRule: "KeepWorld" });
  }
}, (steps) => {
  const list = actorList("orbitOrbs", ORBIT_ORBS, 3, 4)(steps);
  return ok(steps[2]) ? { rotationAdded: true, ...list } : list;
});

const SEQUENCE = `${DEMO_DIR}/SEQ_Demo_Showcase`;

const levelSequence = levelStep(17, "level_sequence", "LevelSequence with hero binding", (f) => {
  f.add("asset.delete", { assetPath: SEQUENCE, force: true });
  const seq = f.add("editor.create_sequence", { name: "SEQ_Demo_Showcase", packagePath: DEMO_DIR });
  const assetPath = f.ref(seq, "path");
  // Binds the hero only when exactly one carries the label, as before: a
  // replay's second sphere makes the label ambiguous, and the sequence is
  // still made without a binding.
  f.add("editor.add_sequence_track", { assetPath, trackType: "Transform", actorLabel: "Demo_HeroSphere" }, { ignore_failure: true });
  f.add("asset.save", { assetPath });
  const actor = f.spawn("LevelSequenceActor", "Demo_SequenceActor");
  f.add("level.set_actor_property", { actorPath: f.ref(actor, "actorPath"), propertyName: "LevelSequenceAsset", value: assetPath });
}, (steps) => {
  const failure = firstFailure(steps, new Set([2]));
  if (failure) return failed(failure);
  const bind = steps[2];
  return {
    ...(ok(bind) ? { boundActor: "Demo_HeroSphere", bindingGuid: str(bind, "bindingGuid") } : {}),
    sequenceActorLabel: str(steps[4], "actorLabel"),
    sequencePath: str(steps[1], "path"),
    success: true,
  };
});

const TUNING_PANEL = `${DEMO_DIR}/EUW_DemoTuning`;

const tuningPanel = levelStep(18, "tuning_panel", "EditorUtilityWidget tuning panel", (f) => {
  f.add("widget.create_utility_widget", { assetPath: TUNING_PANEL });
}, (steps) => {
  const create = steps[0];
  if (!ok(create)) return failed(create);
  // Kept rather than replaced on a replay.
  if (create.data?.existed === true) return { assetPath: TUNING_PANEL, success: true, status: "existed" };
  return { assetPath: str(create, "path"), status: "created", success: true };
});

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
  materials,
  floor,
  pedestal,
  heroSphere,
  pillars,
  orbs,
  neonLights,
  heroLight,
  moonlight,
  skyLight,
  fog,
  postProcess,
  niagaraVfx,
  pcgScatter,
  orbitRings,
  levelSequence,
  tuningPanel,
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
