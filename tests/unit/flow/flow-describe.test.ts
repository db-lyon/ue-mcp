/**
 * describe_action, search_tools and list_available_actions cover flows: a flow
 * is described by its resolved plan, found by search beside the actions, and
 * counted by availability, without any action's answer changing.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { z } from "zod";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { FlowConfigCache } from "../../../src/flow/config-cache.js";
import { describeFlow, type ConfigLayerDoc, type FlowSource } from "../../../src/flow/flow-describe.js";
import { projectTool } from "../../../src/tools/project.js";
import { actionSchema } from "../../../src/surface/action-schema.js";
import { bp, categoryTool } from "../../../src/surface/category-tool.js";
import type { ToolContext, ToolDef } from "../../../src/core/types.js";

const shapes: ToolDef = categoryTool("shapes", "Shapes.", {
  make_cube: bp("mutate", "Make a cube. Params: size", "make_cube"),
  list_cubes: { kind: "handler", effect: "read", options: { params: [] }, description: "List cubes. Params: none", handler: async () => ({ cubes: [] }) },
}, { size: z.number().optional().describe("Edge length") });

const layers: ConfigLayerDoc[] = [
  { source: "built-in", doc: { flows: { cube_tower: { steps: { 1: {}, 2: {} } } } } },
  { source: "ue-mcp.yml", doc: { flows: { cube_tower: { steps: { 2: { when: "editor.connected" } } }, stack: {} } } },
];

function source(): FlowSource {
  const tools = [shapes];
  const config = FlowConfigSchema.parse({
    tasks: buildDefaults(tools).tasks,
    flows: {
      cube_tower: {
        description: "Stack cubes into a tower.",
        deprecated: "Use stack.",
        replaced_by: "stack",
        steps: {
          1: { task: "shapes.list_cubes" },
          2: { task: "shapes.make_cube", options: { size: 2 }, when: "editor.connected", ignore_failure: true },
          3: { flow: "stack" },
        },
      },
      stack: {
        description: "Put one cube on another.",
        checks: [{ when: "not editor.connected", action: "error", message: "Needs an editor." }],
        steps: { 1: { task: "shapes.make_cube", checks: [{ when: "session.count > 1", action: "warn" }] } },
      },
    },
  });
  return { config, registry: buildFlowRegistry(tools), layers: () => layers };
}

function ctx(connected = false): ToolContext {
  const src = source();
  return {
    bridge: { isConnected: connected, getTarget: () => ({ projectPath: null, port: 0, portSource: "default" }) },
    project: {},
    getToolGraph: () => [shapes],
    getFlows: () => Object.entries(src.config.flows).map(([name, def]) => ({ name, description: (def as { description?: string }).description })),
    getFlowSource: () => src,
  } as unknown as ToolContext;
}

const call = (c: ToolContext, action: string, p: Record<string, unknown>) =>
  projectTool.actions[action].kind === "handler"
    ? (projectTool.actions[action] as { handler: (c: ToolContext, p: Record<string, unknown>) => Promise<unknown> }).handler(c, p)
    : Promise.reject(new Error("not a handler"));

describe("describing a flow", () => {
  it("returns its resolved plan with nested flows flattened", async () => {
    const report = await describeFlow(source(), "cube_tower");
    expect(report).toMatchObject({
      kind: "flow",
      name: "cube_tower",
      description: "Stack cubes into a tower.",
      deprecated: "Use stack.",
      replaced_by: "stack",
      source: "ue-mcp.yml",
      run: 'flow(action="run", flowName="cube_tower")',
    });
    expect(report!.steps.map((s) => `${s.path}:${s.type}:${s.name}`)).toEqual([
      "1:task:shapes.list_cubes",
      "2:task:shapes.make_cube",
      "3:flow:stack",
      "3/1:task:shapes.make_cube",
    ]);
    expect(report!.steps[1]).toMatchObject({ when: "editor.connected", ignore_failure: true, options: { size: 2 }, source: "ue-mcp.yml" });
    expect(report!.steps[0].source).toBe("built-in");
    expect(report!.steps[3].checks).toEqual([{ when: "session.count > 1", action: "warn" }]);
  });

  it("answers describe_action for a flow name, bare or qualified", async () => {
    const c = ctx();
    expect(await call(c, "describe_action", { name: "stack" })).toMatchObject({ kind: "flow", name: "stack", checks: [{ action: "error" }] });
    expect(await call(c, "describe_action", { name: "flow.stack" })).toMatchObject({ kind: "flow", name: "stack" });
  });

  it("leaves an action's answer as it was", async () => {
    expect(await call(ctx(), "describe_action", { name: "shapes.make_cube" })).toEqual(actionSchema(shapes, "make_cube"));
  });

  it("still refuses a name that is neither", async () => {
    await expect(call(ctx(), "describe_action", { name: "no_such_thing" })).rejects.toThrow(/Unknown action 'no_such_thing'/);
  });
});

describe("searching and listing flows", () => {
  it("lists matching flows beside the action results", async () => {
    const found = await call(ctx(), "search_tools", { query: "tower" }) as { flows?: Array<{ flow: string }> };
    expect(found.flows?.map((f) => f.flow)).toEqual(["cube_tower"]);
    const none = await call(ctx(), "search_tools", { query: "list" }) as Record<string, unknown>;
    expect(none.flows).toBeUndefined();
    expect(none.resultCount).toBe(1);
  });

  it("counts which flows can run with no editor", async () => {
    const report = await call(ctx(false), "list_available_actions", { includeNames: true, state: "all" }) as {
      flows: { total: number; blocked: number; flows: Array<{ flow: string; availability: string; editorSteps?: string[] }> };
    };
    expect(report.flows.total).toBe(2);
    expect(report.flows.blocked).toBe(2);
    expect(report.flows.flows.find((f) => f.flow === "stack")).toMatchObject({ availability: "editor", editorSteps: ["shapes.make_cube"] });
  });

  it("says nothing about flows when narrowed to one category", async () => {
    const report = await call(ctx(), "list_available_actions", { category: "shapes" }) as Record<string, unknown>;
    expect(report.flows).toBeUndefined();
  });
});

describe("config layers", () => {
  const saved = process.env.UE_MCP_GLOBAL_CONFIG;
  afterEach(() => {
    if (saved === undefined) delete process.env.UE_MCP_GLOBAL_CONFIG;
    else process.env.UE_MCP_GLOBAL_CONFIG = saved;
  });

  it("names each layer a flow config is merged from", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-layers-"));
    process.env.UE_MCP_GLOBAL_CONFIG = path.join(dir, "absent-global.yml");
    fs.writeFileSync(path.join(dir, "ue-mcp.yml"), "flows:\n  mine:\n    steps:\n      1: { task: shapes.list_cubes }\n");
    const docs = new FlowConfigCache([shapes], dir).layerDocs();
    expect(docs.map((d) => d.source)).toEqual(["built-in", "ue-mcp.yml"]);
    expect((docs[1].doc as { flows: Record<string, unknown> }).flows.mine).toBeDefined();
  });
});
