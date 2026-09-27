#!/usr/bin/env tsx
/**
 * Generate universal/ue-mcp.universal.yml, the universal config layer.
 *
 * It holds what is derived rather than written: one task per action of every
 * shipped category, the beacon flow (its pillars and lights are placed with
 * trigonometry), and the Neon Shrine demo flows from src/tools/demo-steps.ts.
 * The hand-authored flows in universal/flows.yml are folded in unchanged.
 * tests/unit/flow/universal-layer.test.ts fails when the committed file is not
 * what this produces.
 *
 * Run: npm run generate:universal
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import yaml from "js-yaml";

import { ALL_TOOLS } from "../src/tools.js";
import { dumpYaml } from "../src/core/yaml-dump.js";
import { actionTaskEntry } from "../src/flow/universal.js";
import { DEMO_STEPS, demoFlowName, demoStepFlows } from "../src/tools/demo-steps.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
export const AUTHORED = path.join(ROOT, "universal", "flows.yml");
export const OUTPUT = path.join(ROOT, "universal", "ue-mcp.universal.yml");

const HEADER = [
  "# Auto-generated - do not edit by hand.",
  "# Source: scripts/generate-universal.ts (tasks, beacon, Neon Shrine) and",
  "# universal/flows.yml (hand-authored flows). Run: npm run generate:universal",
  "",
].join("\n");

/** The order flows are listed in; anything unlisted follows in authored order. */
const FLOW_ORDER = ["niagara_fire", "beacon", "texture_bomb", "neon_shrine", "neon_shrine_cleanup"];

const PKG = "/Game/Flows/Beacon";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SPHERE = "/Engine/BasicShapes/Sphere.Sphere";
const CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder";
const M_FLOOR = `${PKG}/M_Floor`;
const M_PILLAR = `${PKG}/M_Pillar`;
const M_GLOW = `${PKG}/M_Glow`;
const M_PEDESTAL = `${PKG}/M_Pedestal`;

/** A shrine scene: floor, pedestal, orb, five pillars on a pentagon, four materials, lights. */
function beacon(): Record<string, unknown> {
  let s = 0;
  const steps: Record<string, unknown> = {};
  const step = (task: string, options: Record<string, unknown>): number => {
    steps[String(++s)] = { task, options };
    return s;
  };
  /** The engine name a step's new node came back with. */
  const node = (n: number) => `\${steps.${n}.expressionName}`;

  step("level.create", { levelPath: `${PKG}/BeaconLevel` });
  step("level.place_actor", { actorClass: "SkyAtmosphere", label: "Sky" });
  step("level.place_actor", { actorClass: "ExponentialHeightFog", label: "Fog" });
  step("level.place_actor", { actorClass: "SkyLight", label: "Ambient" });

  step("material.create", { name: "M_Floor", packagePath: PKG });
  step("material.set_base_color", { assetPath: M_FLOOR, color: { r: 15, g: 15, b: 18 } });
  step("material.recompile", { materialPath: M_FLOOR });

  // Constants take their value at creation and are wired by engine name,
  // since a Constant's description is its value.
  step("material.create", { name: "M_Pillar", packagePath: PKG });
  step("material.set_base_color", { assetPath: M_PILLAR, color: { r: 60, g: 65, b: 80 } });
  const metallic = step("material.add_expression", {
    materialPath: M_PILLAR, expressionType: "Constant", name: "Metallic", value: 1.0,
  });
  step("material.connect_to_property", { materialPath: M_PILLAR, expressionName: node(metallic), property: "Metallic" });
  const roughness = step("material.add_expression", {
    materialPath: M_PILLAR, expressionType: "Constant", name: "Roughness", value: 0.3,
  });
  step("material.connect_to_property", { materialPath: M_PILLAR, expressionName: node(roughness), property: "Roughness" });
  step("material.recompile", { materialPath: M_PILLAR });

  step("material.create", { name: "M_Pedestal", packagePath: PKG });
  step("material.set_base_color", { assetPath: M_PEDESTAL, color: { r: 90, g: 80, b: 65 } });
  step("material.recompile", { materialPath: M_PEDESTAL });

  // VectorParam x Strength into EmissiveColor.
  step("material.create", { name: "M_Glow", packagePath: PKG });
  step("material.add_expression", {
    materialPath: M_GLOW, expressionType: "VectorParameter", name: "GlowColor", parameterName: "GlowColor",
  });
  const glowStrength = step("material.add_expression", {
    materialPath: M_GLOW, expressionType: "Constant", name: "GlowStrength", value: 50,
  });
  step("material.add_expression", { materialPath: M_GLOW, expressionType: "Multiply", name: "Multiply" });
  step("material.connect_expressions", {
    materialPath: M_GLOW, sourceExpression: "GlowColor", targetExpression: "Multiply", targetInput: "A",
  });
  step("material.connect_expressions", {
    materialPath: M_GLOW, sourceExpression: node(glowStrength), targetExpression: "Multiply", targetInput: "B",
  });
  step("material.connect_to_property", { materialPath: M_GLOW, expressionName: "Multiply", property: "EmissiveColor" });
  step("material.recompile", { materialPath: M_GLOW });

  step("level.place_actor", {
    actorClass: "StaticMeshActor", label: "Floor", staticMesh: CUBE, material: M_FLOOR,
    location: { x: 0, y: 0, z: -5 }, scale: { x: 25, y: 25, z: 0.1 },
  });
  step("level.place_actor", {
    actorClass: "StaticMeshActor", label: "Pedestal", staticMesh: CYLINDER, material: M_PEDESTAL,
    location: { x: 0, y: 0, z: 0 }, scale: { x: 1.5, y: 1.5, z: 3 },
  });
  step("level.place_actor", {
    actorClass: "StaticMeshActor", label: "Orb", staticMesh: SPHERE, material: M_GLOW,
    location: { x: 0, y: 0, z: 350 }, scale: { x: 1.5, y: 1.5, z: 1.5 },
  });

  // Five pillars on a pentagon of radius 600.
  const pillarAngles = [0, 72, 144, 216, 288];
  const onPentagon = (i: number) => {
    const rad = (pillarAngles[i] * Math.PI) / 180;
    return { x: Math.round(600 * Math.cos(rad)), y: Math.round(600 * Math.sin(rad)) };
  };
  for (let i = 0; i < pillarAngles.length; i++) {
    step("level.place_actor", {
      actorClass: "StaticMeshActor", label: `Pillar_${i + 1}`, staticMesh: CUBE, material: M_PILLAR,
      location: { ...onPentagon(i), z: 0 }, scale: { x: 0.4, y: 0.4, z: 5 },
    });
  }

  step("level.spawn_light", { lightType: "directional", label: "Sun", intensity: 10 });
  step("level.set_light_properties", { actorLabel: "Sun", color: { r: 255, g: 160, b: 80 } });
  step("level.move_actor", { actorLabel: "Sun", rotation: { pitch: -25, yaw: -135 } });

  const pillarColors = [
    { r: 0, g: 200, b: 255 },
    { r: 255, g: 0, b: 200 },
    { r: 255, g: 200, b: 0 },
    { r: 100, g: 255, b: 50 },
    { r: 120, g: 80, b: 255 },
  ];
  for (let i = 0; i < pillarAngles.length; i++) {
    step("level.spawn_light", {
      lightType: "point", label: `PillarLight_${i + 1}`, location: { ...onPentagon(i), z: 550 }, intensity: 80000,
    });
    step("level.set_light_properties", { actorLabel: `PillarLight_${i + 1}`, color: pillarColors[i] });
  }

  step("level.spawn_light", { lightType: "spot", label: "OrbSpot", location: { x: 0, y: 0, z: 700 }, intensity: 300000 });
  step("level.move_actor", { actorLabel: "OrbSpot", rotation: { pitch: -90, yaw: 0 } });
  step("level.spawn_light", { lightType: "point", label: "FillWarm", location: { x: -400, y: -300, z: 100 }, intensity: 20000 });
  step("level.set_light_properties", { actorLabel: "FillWarm", color: { r: 255, g: 200, b: 150 } });
  step("level.spawn_light", { lightType: "point", label: "FillCool", location: { x: 400, y: 300, z: 100 }, intensity: 15000 });
  step("level.set_light_properties", { actorLabel: "FillCool", color: { r: 150, g: 200, b: 255 } });

  step("editor.set_viewport", { location: { x: -900, y: -500, z: 400 }, rotation: { pitch: -15, yaw: 30 } });

  return {
    description:
      "Demo - build a shrine scene from scratch: floor, pedestal, orb, five pillars, " +
      "four materials (dark stone, brushed metal, warm stone, parameterized emissive), " +
      "colored lights, and atmosphere",
    steps,
  };
}

/** Neon Shrine: one demo_step_N flow per demo step. */
function neonShrine(): Record<string, unknown> {
  return {
    description:
      "Build the full Neon Shrine demo scene end-to-end (19 steps), one demo_step_N " +
      "flow per step. Leaves the editor on " +
      "/Game/Demo/DemoLevel; run neon_shrine_cleanup to wipe it.",
    steps: Object.fromEntries(DEMO_STEPS.map((s) => [String(s.index), { flow: demoFlowName(s.index) }])),
  };
}

function orderFlows(flows: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const name of FLOW_ORDER) if (name in flows) out[name] = flows[name];
  for (const [name, def] of Object.entries(flows)) if (!(name in out)) out[name] = def;
  return out;
}

/** The universal layer as YAML text. */
export function generateUniversal(): string {
  const tasks: Record<string, unknown> = {};
  for (const tool of ALL_TOOLS) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      tasks[`${tool.name}.${action}`] = actionTaskEntry(tool.name, action, spec);
    }
  }
  tasks.shell = { class_path: "shell", group: "util", description: "Run a shell command. Params: command, cwd?, timeout?" };

  const authored = (yaml.load(fs.readFileSync(AUTHORED, "utf-8")) as { flows?: Record<string, unknown> } | null)?.flows ?? {};
  const generated: Record<string, unknown> = { beacon: beacon(), neon_shrine: neonShrine(), ...demoStepFlows() };
  const clash = Object.keys(authored).filter((n) => n in generated);
  if (clash.length > 0) throw new Error(`universal/flows.yml redeclares generated flows: ${clash.join(", ")}`);

  return HEADER + dumpYaml({ "ue-mcp": { version: 1 }, tasks, flows: orderFlows({ ...authored, ...generated }) });
}

const isMain = process.argv[1] !== undefined
  && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);

if (isMain) {
  const text = generateUniversal();
  const check = process.argv.includes("--check");
  if (check) {
    const current = fs.existsSync(OUTPUT) ? fs.readFileSync(OUTPUT, "utf-8") : "";
    if (current !== text) {
      console.error(`[generate] ${OUTPUT} is stale. Run: npm run generate:universal`);
      process.exit(1);
    }
    console.log(`[generate] ${OUTPUT} is current`);
  } else {
    fs.writeFileSync(OUTPUT, text, "utf-8");
    console.log(`[generate] ${OUTPUT}`);
  }
}
