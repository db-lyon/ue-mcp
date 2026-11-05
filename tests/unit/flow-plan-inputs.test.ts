/**
 * flow(plan) reports `inputs`: the parameters a run may pass, derived from the
 * step tasks' schemas and their flowkit option specs (#1508).
 */
import * as path from "node:path";
import { z } from "zod";
import { describe, expect, it } from "vitest";
import { deepMerge, loadRawYaml } from "@db-lyon/flowkit";
import { FlowConfigSchema, type FlowConfig } from "../../src/flow/schema.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { deriveFlowInputs, type FlowInput } from "../../src/flow/flow-inputs.js";
import { ProjectContext } from "../../src/config/project.js";
import { categoryTool } from "../../src/surface/category-tool.js";
import { ALL_TOOLS } from "../../src/tools.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolDef } from "../../src/core/types.js";

const bridge: IBridge = {
  isConnected: false,
  connect: async () => {},
  retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  call: async () => ({}),
};

const noop = async () => ({});
const assetTool = categoryTool("asset", "Test-only category.", {
  create: { kind: "handler", effect: "mutate", description: "Create. Params: name, packagePath?, mode?", handler: noop },
  rename: { kind: "handler", effect: "mutate", description: "Rename. Params: path, newName, mode?", handler: noop },
  tag: { kind: "handler", effect: "mutate", description: "Tag. Params: path, count?", handler: noop },
  remove: { kind: "handler", effect: "mutate", description: "Remove. Params: path OR packagePath, mode?", handler: noop },
}, {
  name: z.string().optional().describe("Asset name"),
  packagePath: z.string().optional().describe("Content folder"),
  path: z.string().optional().describe("Asset path"),
  newName: z.string().optional().describe("New name"),
  mode: z.enum(["fast", "safe", "slow"]).optional().describe("How hard to try"),
  count: z.number().optional().describe("How many"),
});
const levelTool = categoryTool("level", "Test-only category.", {
  tag: { kind: "handler", effect: "mutate", description: "Tag. Params: count?", handler: noop },
}, {
  count: z.string().optional().describe("Tag text"),
});
const TOOLS: ToolDef[] = [assetTool, levelTool];

function config(raw: Record<string, unknown>): FlowConfig {
  return FlowConfigSchema.parse({ "ue-mcp": { version: 1 }, ...raw });
}

async function inputsOf(cfg: FlowConfig, flowName: string, tools: ToolDef[] = TOOLS): Promise<FlowInput[]> {
  return deriveFlowInputs(flowName, {
    flows: cfg.flows as never,
    tasks: cfg.tasks as never,
    registry: buildFlowRegistry(tools),
    tools,
  });
}

const byName = (inputs: FlowInput[]) => Object.fromEntries(inputs.map((i) => [i.name, i]));

describe("flow inputs", () => {
  it("reports a parameter some step leaves unset, with the steps that read it", async () => {
    const cfg = config({
      flows: {
        f: {
          steps: {
            "1": { task: "asset.create", options: { name: "A" } },
            "2": { task: "asset.create", options: { name: "B", packagePath: "/Game/B" } },
          },
        },
      },
    });
    const inputs = byName(await inputsOf(cfg, "f"));
    // name is set by every step that reads it, so a run has nothing to pass.
    expect(inputs.name).toBeUndefined();
    expect(inputs.packagePath).toEqual({
      name: "packagePath",
      type: "string",
      description: "Content folder",
      required: false,
      steps: [{ path: "1", task: "asset.create", flow: "f" }],
    });
    expect(inputs.mode).toMatchObject({ type: "string", enum: ["fast", "safe", "slow"], required: false });
    expect(inputs.mode.steps.map((s) => s.path)).toEqual(["1", "2"]);
    // Routing parameters are the dispatcher's, never a run's.
    for (const routing of ["action", "timeoutMs", "select", "omit", "editor"]) expect(inputs[routing]).toBeUndefined();
  });

  it("puts required inputs first and marks them required", async () => {
    const cfg = config({ flows: { f: { steps: { "1": { task: "asset.rename", options: { path: "/Game/A" } } } } } });
    const inputs = await inputsOf(cfg, "f");
    expect(inputs.map((i) => [i.name, i.required])).toEqual([["newName", true], ["mode", false]]);
  });

  it("drops the other side of a choice the step already made", async () => {
    const cfg = config({ flows: { f: { steps: { "1": { task: "asset.remove", options: { path: "/Game/A" } } } } } });
    expect((await inputsOf(cfg, "f")).map((i) => i.name)).toEqual(["mode"]);
  });

  it("reports a parameter two steps type differently once, with both types", async () => {
    const cfg = config({
      flows: { f: { steps: { "1": { task: "asset.tag", options: { path: "/Game/A" } }, "2": { task: "level.tag" } } } },
    });
    const count = byName(await inputsOf(cfg, "f")).count;
    expect(count).toMatchObject({ type: ["number", "string"], conflict: true });
    expect(count.steps.map((s) => s.task)).toEqual(["asset.tag", "level.tag"]);
  });

  it("overlays a task's options_schema on the action schema", async () => {
    const cfg = config({
      tasks: {
        make: {
          class_path: "asset.create",
          options: { name: "Fixed" },
          options_schema: {
            packagePath: { required: true, description: "Where it lands" },
            mode: { enum: ["safe"], default: "safe" },
            extra: { type: "integer", description: "Declared only by the task" },
          },
        },
      },
      flows: { f: { steps: { "1": { task: "make" } } } },
    });
    const inputs = byName(await inputsOf(cfg, "f"));
    // The task definition's own options set name.
    expect(inputs.name).toBeUndefined();
    expect(inputs.packagePath).toMatchObject({ type: "string", required: true, description: "Where it lands" });
    expect(inputs.mode).toMatchObject({ type: "string", enum: ["safe"], default: "safe", required: false });
    expect(inputs.extra).toMatchObject({ type: "integer", description: "Declared only by the task" });
    expect(inputs.extra.steps).toEqual([{ path: "1", task: "make", flow: "f" }]);
  });

  it("collects a nested flow's inputs under the nesting step's path, after its overrides", async () => {
    const cfg = config({
      flows: {
        child: { steps: { "1": { task: "asset.rename", options: { path: "/Game/A" } } } },
        parent: {
          steps: {
            "1": { task: "asset.create", options: { name: "A", packagePath: "/Game", mode: "fast" } },
            // The flow step's options are a per-task override map for the child's steps.
            "2": { flow: "child", options: { "asset.rename": { mode: "safe" } } },
          },
        },
      },
    });
    const inputs = byName(await inputsOf(cfg, "parent"));
    expect(inputs.newName).toMatchObject({ required: true, steps: [{ path: "2/1", task: "asset.rename", flow: "child" }] });
    expect(inputs.mode).toBeUndefined();
  });

  it("reads hook steps and stops at a flow that nests itself", async () => {
    const cfg = config({
      flows: {
        loop: {
          steps: { "1": { flow: "loop" } },
          on_start: [{ task: "asset.tag", options: { path: "/Game/A" } }],
        },
      },
    });
    const inputs = byName(await inputsOf(cfg, "loop"));
    expect(inputs.count.steps).toEqual([{ path: "on_start/1", task: "asset.tag", flow: "loop" }]);
  });

  it("falls back to a task description's Params clause when nothing else declares it", async () => {
    const cfg = config({
      tasks: { run_it: { class_path: "shell", description: "Run a shell command. Params: command, cwd?, timeout?" } },
      flows: { f: { steps: { "1": { task: "run_it", options: { command: "echo" } } } } },
    });
    const inputs = await inputsOf(cfg, "f");
    expect(inputs.map((i) => [i.name, i.type, i.required])).toEqual([["cwd", "any", false], ["timeout", "any", false]]);
  });

  it("is on the flow(plan) response", async () => {
    const cfg = config({ flows: { f: { steps: { "1": { task: "asset.rename", options: { path: "/Game/A" } } } } } });
    const tool = createFlowTool(buildFlowRegistry(TOOLS), () => cfg);
    const out = (await tool.handler(
      { bridge, project: new ProjectContext(), getToolGraph: () => TOOLS },
      { action: "plan", flowName: "f" },
    )) as { inputs: FlowInput[] };
    expect(out.inputs.map((i) => i.name)).toEqual(["newName", "mode"]);
  });

  it("carries options_scope on the plan's definition, so a form can nest a step-scoped run's params", async () => {
    const cfg = config({ flows: { f: { options_scope: "step", steps: { "1": { task: "asset.rename", options: { path: "/Game/A" } } } } } });
    const tool = createFlowTool(buildFlowRegistry(TOOLS), () => cfg);
    const plan = (flowName: string) => tool.handler({ bridge, project: new ProjectContext(), getToolGraph: () => TOOLS }, { action: "plan", flowName }) as Promise<{ definition: { options_scope?: string } }>;
    expect((await plan("f")).definition.options_scope).toBe("step");
  });
});

describe("flow inputs of the shipped flows", () => {
  const defaults = buildDefaults(ALL_TOOLS);

  it("playtest takes a target and the optional context, profile, name and package_path", async () => {
    const cfg = FlowConfigSchema.parse(defaults);
    const inputs = await inputsOf(cfg, "playtest", ALL_TOOLS);
    expect(inputs.map((i) => [i.name, i.type, i.required, i.steps.map((s) => s.path)])).toEqual([
      ["target", "string", true, ["1"]],
      ["context", "string", false, ["1"]],
      ["profile", "string", false, ["1"]],
      ["name", "string", false, ["1"]],
      ["package_path", "string", false, ["8"]],
    ]);
  });

  it("every built-in flow derives without a conflict or a routing parameter", async () => {
    const cfg = FlowConfigSchema.parse(defaults);
    for (const flowName of Object.keys(cfg.flows)) {
      const inputs = await inputsOf(cfg, flowName, ALL_TOOLS);
      for (const input of inputs) {
        expect(input.conflict, `${flowName}.${input.name}`).toBeUndefined();
        expect(["action", "timeoutMs", "select", "omit", "editor"]).not.toContain(input.name);
        expect(input.steps.length).toBeGreaterThan(0);
      }
    }
  });

  it("neon_shrine takes nothing: each step sets demo(step) by its alias stepIndex", async () => {
    const cfg = FlowConfigSchema.parse(defaults);
    expect(await inputsOf(cfg, "neon_shrine", ALL_TOOLS)).toEqual([]);
  });

  it("the tests/ue_mcp leak probe leaves only the listing's paging open", async () => {
    const yml = loadRawYaml(path.resolve("tests/ue_mcp/ue-mcp.yml")) as Record<string, unknown>;
    const cfg = FlowConfigSchema.parse(deepMerge(defaults, yml));
    const inputs = await inputsOf(cfg, "live_tier_leak_probe", ALL_TOOLS);
    // directory and editor are set; the generic bridge task declares nothing.
    expect(inputs.map((i) => [i.name, i.required, i.steps.map((s) => `${s.path} ${s.task}`)])).toEqual([
      ["cursor", false, ["1 asset.list_textures"]],
      ["limit", false, ["1 asset.list_textures"]],
    ]);
  });
});
