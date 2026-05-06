import * as fs from "node:fs";
import * as path from "node:path";
import { loadConfig, type LoadedConfig } from "@db-lyon/flowkit";
import { FlowConfigSchema, type FlowConfig } from "./schema.js";
import type { ToolDef } from "../types.js";

/**
 * Build the defaults object from tool definitions.
 * This is the runtime equivalent of scripts/generate-default-config.ts.
 */
export function buildDefaults(tools: ToolDef[]): Record<string, unknown> {
  const tasks: Record<string, unknown> = {};

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;

      // class_path always matches the task name so the per-action factory
      // class (registered in registry.ts with mapParams baked in) is the one
      // that runs. The previous default of class_path: "ue-mcp.bridge" routed
      // every bridge action through the generic BridgeTask which silently
      // dropped mapParams — so YAML callers had to know each handler's exact
      // C++-side param names instead of the documented TS-side ones.
      const taskDef: Record<string, unknown> = {
        class_path: taskName,
        group: tool.name,
      };
      if (spec.description) taskDef.description = spec.description;

      tasks[taskName] = taskDef;
    }
  }

  // Built-in shell task
  tasks["shell"] = {
    class_path: "shell",
    group: "util",
    description: "Run a shell command. Params: command, cwd?, timeout?",
  };

  return { tasks, flows: defaultFlows() };
}

// Shorthand constants
const PKG = "/Game/Flows/Beacon";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SPHERE = "/Engine/BasicShapes/Sphere.Sphere";
const CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder";

const M_FLOOR = `${PKG}/M_Floor`;
const M_PILLAR = `${PKG}/M_Pillar`;
const M_GLOW = `${PKG}/M_Glow`;
const M_PEDESTAL = `${PKG}/M_Pedestal`;

/** Built-in flows that ship with ue-mcp. */
function defaultFlows(): Record<string, unknown> {
  let s = 0;
  const steps: Record<string, unknown> = {};
  const step = (task: string, options: Record<string, unknown>) => {
    steps[String(++s)] = { task, options };
  };

  // ── 1. Create level & atmosphere ──────────────────────────────────
  step("level.create", { levelPath: `${PKG}/BeaconLevel` });
  step("level.place_actor", { actorClass: "SkyAtmosphere", label: "Sky" });
  step("level.place_actor", { actorClass: "ExponentialHeightFog", label: "Fog" });
  step("level.place_actor", { actorClass: "SkyLight", label: "Ambient" });

  // ── 2. Materials ──────────────────────────────────────────────────

  // M_Floor — dark stone
  step("material.create", { name: "M_Floor", packagePath: PKG });
  step("material.set_base_color", { assetPath: M_FLOOR, color: { r: 15, g: 15, b: 18 } });
  step("material.recompile", { materialPath: M_FLOOR });

  // M_Pillar — brushed metallic blue-grey
  step("material.create", { name: "M_Pillar", packagePath: PKG });
  step("material.set_base_color", { assetPath: M_PILLAR, color: { r: 60, g: 65, b: 80 } });
  step("material.add_expression", {
    materialPath: M_PILLAR, expressionType: "Constant", name: "Metallic",
  });
  step("material.set_expression_value", {
    materialPath: M_PILLAR, expressionIndex: 0, value: 1.0,
  });
  step("material.connect_to_property", {
    materialPath: M_PILLAR, expressionName: "Metallic", property: "Metallic",
  });
  step("material.add_expression", {
    materialPath: M_PILLAR, expressionType: "Constant", name: "Roughness",
  });
  step("material.set_expression_value", {
    materialPath: M_PILLAR, expressionIndex: 1, value: 0.3,
  });
  step("material.connect_to_property", {
    materialPath: M_PILLAR, expressionName: "Roughness", property: "Roughness",
  });
  step("material.recompile", { materialPath: M_PILLAR });

  // M_Pedestal — warm stone
  step("material.create", { name: "M_Pedestal", packagePath: PKG });
  step("material.set_base_color", { assetPath: M_PEDESTAL, color: { r: 90, g: 80, b: 65 } });
  step("material.recompile", { materialPath: M_PEDESTAL });

  // M_Glow — parameterized emissive (VectorParam × Strength → EmissiveColor)
  step("material.create", { name: "M_Glow", packagePath: PKG });
  step("material.add_expression", {
    materialPath: M_GLOW, expressionType: "VectorParameter",
    name: "GlowColor", parameterName: "GlowColor",
  });
  step("material.add_expression", {
    materialPath: M_GLOW, expressionType: "Constant", name: "GlowStrength",
  });
  step("material.set_expression_value", {
    materialPath: M_GLOW, expressionIndex: 1, value: 50,
  });
  step("material.add_expression", {
    materialPath: M_GLOW, expressionType: "Multiply", name: "Multiply",
  });
  step("material.connect_expressions", {
    materialPath: M_GLOW, sourceExpression: "GlowColor",
    targetExpression: "Multiply", targetInput: "A",
  });
  step("material.connect_expressions", {
    materialPath: M_GLOW, sourceExpression: "GlowStrength",
    targetExpression: "Multiply", targetInput: "B",
  });
  step("material.connect_to_property", {
    materialPath: M_GLOW, expressionName: "Multiply", property: "EmissiveColor",
  });
  step("material.recompile", { materialPath: M_GLOW });

  // ── 3. Geometry ───────────────────────────────────────────────────

  // Floor — large dark slab
  step("level.place_actor", {
    actorClass: "StaticMeshActor", label: "Floor",
    staticMesh: CUBE, material: M_FLOOR,
    location: { x: 0, y: 0, z: -5 },
    scale: { x: 25, y: 25, z: 0.1 },
  });

  // Center pedestal — tall cylinder
  step("level.place_actor", {
    actorClass: "StaticMeshActor", label: "Pedestal",
    staticMesh: CYLINDER, material: M_PEDESTAL,
    location: { x: 0, y: 0, z: 0 },
    scale: { x: 1.5, y: 1.5, z: 3 },
  });

  // Glowing orb on top of pedestal
  step("level.place_actor", {
    actorClass: "StaticMeshActor", label: "Orb",
    staticMesh: SPHERE, material: M_GLOW,
    location: { x: 0, y: 0, z: 350 },
    scale: { x: 1.5, y: 1.5, z: 1.5 },
  });

  // 5 pillars in a pentagon (radius 600, z=0)
  const pillarAngles = [0, 72, 144, 216, 288];
  for (let i = 0; i < pillarAngles.length; i++) {
    const rad = (pillarAngles[i] * Math.PI) / 180;
    const x = Math.round(600 * Math.cos(rad));
    const y = Math.round(600 * Math.sin(rad));
    step("level.place_actor", {
      actorClass: "StaticMeshActor", label: `Pillar_${i + 1}`,
      staticMesh: CUBE, material: M_PILLAR,
      location: { x, y, z: 0 },
      scale: { x: 0.4, y: 0.4, z: 5 },
    });
  }

  // ── 4. Lighting ───────────────────────────────────────────────────

  // Sunset directional
  step("level.spawn_light", { lightType: "directional", label: "Sun", intensity: 10 });
  step("level.set_light_properties", {
    actorLabel: "Sun", color: { r: 255, g: 160, b: 80 },
  });
  step("level.move_actor", {
    actorLabel: "Sun", rotation: { pitch: -25, yaw: -135 },
  });

  // Colored point light at each pillar
  const pillarColors = [
    { r: 0, g: 200, b: 255 },   // cyan
    { r: 255, g: 0, b: 200 },   // magenta
    { r: 255, g: 200, b: 0 },   // gold
    { r: 100, g: 255, b: 50 },  // green
    { r: 120, g: 80, b: 255 },  // violet
  ];
  for (let i = 0; i < pillarAngles.length; i++) {
    const rad = (pillarAngles[i] * Math.PI) / 180;
    const x = Math.round(600 * Math.cos(rad));
    const y = Math.round(600 * Math.sin(rad));
    step("level.spawn_light", {
      lightType: "point", label: `PillarLight_${i + 1}`,
      location: { x, y, z: 550 }, intensity: 80000,
    });
    step("level.set_light_properties", {
      actorLabel: `PillarLight_${i + 1}`, color: pillarColors[i],
    });
  }

  // Center spotlight pointing down at the orb
  step("level.spawn_light", {
    lightType: "spot", label: "OrbSpot",
    location: { x: 0, y: 0, z: 700 }, intensity: 300000,
  });
  step("level.move_actor", {
    actorLabel: "OrbSpot", rotation: { pitch: -90, yaw: 0 },
  });

  // Warm fill light from below
  step("level.spawn_light", {
    lightType: "point", label: "FillWarm",
    location: { x: -400, y: -300, z: 100 }, intensity: 20000,
  });
  step("level.set_light_properties", {
    actorLabel: "FillWarm", color: { r: 255, g: 200, b: 150 },
  });

  // Cool fill light opposite side
  step("level.spawn_light", {
    lightType: "point", label: "FillCool",
    location: { x: 400, y: 300, z: 100 }, intensity: 15000,
  });
  step("level.set_light_properties", {
    actorLabel: "FillCool", color: { r: 150, g: 200, b: 255 },
  });

  // ── 5. Camera ─────────────────────────────────────────────────────
  step("editor.set_viewport", {
    location: { x: -900, y: -500, z: 400 },
    rotation: { pitch: -15, yaw: 30 },
  });

  return {
    beacon: {
      description:
        "Demo — build a shrine scene from scratch: floor, pedestal, orb, five pillars, " +
        "four materials (dark stone, brushed metal, warm stone, parameterized emissive), " +
        "colored lights, and atmosphere",
      steps,
    },
  };
}

/**
 * Load ue-mcp.yml from the given directory, layered on top of built-in defaults.
 * Returns the merged config even if no project ue-mcp.yml exists.
 */
export function loadFlowConfig(
  tools: ToolDef[],
  configDir?: string,
): LoadedConfig<FlowConfig> {
  const dir = configDir ?? process.cwd();
  const configPath = path.join(dir, "ue-mcp.yml");
  const defaults = buildDefaults(tools);

  if (!fs.existsSync(configPath)) {
    return { config: FlowConfigSchema.parse(defaults), configDir: dir };
  }

  return loadConfig({
    filename: "ue-mcp.yml",
    schema: FlowConfigSchema,
    defaults,
    envVar: "UE_MCP_ENV",
    configDir: dir,
  });
}
