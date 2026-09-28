/**
 * Whole-plan validation: every problem a run would hit, found before step 1
 * and refused as one usage error.
 */
import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../../src/tools.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import { validatePlan } from "../../../src/flow/validate-plan.js";
import { expressionProblem } from "../../../src/flow/condition.js";
import { McpError, ErrorCode } from "../../../src/core/errors.js";
import { envelopeOfError } from "../../../src/dispatch/error-envelope.js";
import type { ToolContext } from "../../../src/core/types.js";

const registry = buildFlowRegistry(ALL_TOOLS);
const defaults = buildDefaults(ALL_TOOLS) as { tasks: Record<string, unknown>; flows: Record<string, unknown> };

function configWith(flows: Record<string, unknown>) {
  return FlowConfigSchema.parse({ ...defaults, flows: { ...defaults.flows, ...flows } });
}

function ctx(calls: string[] = []): ToolContext {
  return {
    bridge: {
      isConnected: true,
      capabilities: undefined,
      call: async (method: string) => { calls.push(method); return { success: true, path: "/Game/X" }; },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default" }),
    },
    project: { ensureLoaded: () => {} },
    getToolGraph: () => ALL_TOOLS,
  } as unknown as ToolContext;
}

const BROKEN = {
  steps: {
    1: { task: "blueprint.complie", options: { assetPath: "/Game/BP" } },
    2: { task: "blueprint.compile" },
    3: { task: "asset.save", options: { assetPath: "${steps.9.path}" } },
    4: { task: "asset.save", when: "probe.wrold.mode == 'play'", options: { assetPath: "/Game/A" } },
    5: { task: "asset.save", when: "editor.connected = true", options: { assetPath: "/Game/A" } },
    6: { flow: "nope" },
    7: { task: "asset.save", options: { assetPath: "${params.target}" } },
    8: { task: "asset.save", options: { assetPath: "/Game/A", assetPth: "/Game/B" } },
    9: { task: "asset.save", options: { assetPath: "/Game/A" } },
  },
};

describe("validatePlan", () => {
  it("lists every problem, not the first", async () => {
    const v = await validatePlan({ config: configWith({ broken: BROKEN }), registry, graph: ALL_TOOLS, flowName: "broken" });
    expect(v.ok).toBe(false);
    const at = (path: string) => v.problems.filter((p) => p.path === path);
    expect(at("1")[0]).toMatchObject({ kind: "task", didYouMean: expect.arrayContaining(["blueprint.compile"]) });
    expect(at("2")[0]).toMatchObject({ kind: "option", message: expect.stringContaining("'assetPath'") });
    expect(at("3")[0]).toMatchObject({ kind: "reference" });
    expect(at("4")[0]).toMatchObject({ kind: "probe", didYouMean: ["world"] });
    expect(at("5")[0]).toMatchObject({ kind: "expression" });
    expect(at("6")[0]).toMatchObject({ kind: "flow" });
    expect(at("7")[0]).toMatchObject({ kind: "reference", message: expect.stringContaining("params.target") });
    expect(at("9")).toEqual([]);
    expect(v.warnings?.find((w) => w.path === "8")).toMatchObject({ kind: "option", didYouMean: expect.arrayContaining(["assetPath"]) });
  });

  it("counts the run's params as supplied options and references", async () => {
    const config = configWith({
      commit: { steps: { 1: { task: "blueprint.compile" }, 2: { task: "asset.save", options: { assetPath: "${params.assetPath}" } } } },
    });
    const without = await validatePlan({ config, registry, graph: ALL_TOOLS, flowName: "commit" });
    expect(without.problems.map((p) => p.path)).toEqual(["1", "2"]);
    const withParams = await validatePlan({ config, registry, graph: ALL_TOOLS, flowName: "commit", params: { assetPath: "/Game/BP" } });
    expect(withParams).toEqual({ ok: true, problems: [] });
  });

  it("follows nested flows and hooks, with their paths", async () => {
    const config = configWith({
      outer: { steps: { 1: { flow: "inner" } }, finally: [{ task: "asset.nope" }] },
      inner: { steps: { 1: { task: "blueprint.compile" } } },
    });
    const v = await validatePlan({ config, registry, graph: ALL_TOOLS, flowName: "outer" });
    expect(v.problems.map((p) => [p.path, p.kind])).toEqual([["1/1", "option"], ["finally/1", "task"]]);
  });

  it("refuses a flow that runs itself", async () => {
    const config = configWith({ loop: { steps: { 1: { flow: "loop" } } } });
    const v = await validatePlan({ config, registry, graph: ALL_TOOLS, flowName: "loop" });
    expect(v.problems[0].message).toMatch(/runs itself/);
  });

  it("names the closest flow for one that does not exist", async () => {
    const v = await validatePlan({ config: configWith({}), registry, graph: ALL_TOOLS, flowName: "niagra_fire" });
    expect(v.problems[0]).toMatchObject({ kind: "flow", didYouMean: expect.arrayContaining(["niagara_fire"]) });
  });
});

describe("the flow tool", () => {
  it("refuses a run as one usage error before any step reaches the editor", async () => {
    const calls: string[] = [];
    const flow = createFlowTool(registry, () => configWith({ broken: BROKEN }));
    const err = await flow.handler(ctx(calls), { action: "run", flowName: "broken" }).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(McpError);
    expect((err as McpError).code).toBe(ErrorCode.INVALID_PARAMS);
    expect((err as McpError).message).toMatch(/7 problems/);
    expect(((err as McpError).details as { problems: unknown[] }).problems).toHaveLength(7);
    expect(envelopeOfError(err).class).toBe("usage");
    expect(calls).toEqual([]);
  });

  it("reports the same validation on a plan, without refusing it", async () => {
    const flow = createFlowTool(registry, () => configWith({ broken: BROKEN }));
    const plan = await flow.handler(ctx(), { action: "plan", flowName: "broken" }) as { validation: { ok: boolean; problems: unknown[] } };
    expect(plan.validation.ok).toBe(false);
    expect(plan.validation.problems).toHaveLength(7);
  });

  it("resolves \${params.x} in a step's options", async () => {
    const sent: Array<Record<string, unknown>> = [];
    const c = ctx();
    (c.bridge as unknown as { call: unknown }).call = async (_m: string, p: Record<string, unknown>) => { sent.push(p); return { success: true }; };
    const flow = createFlowTool(registry, () => configWith({
      save_one: { steps: { 1: { task: "asset.save", options: { assetPath: "${params.target}" } } } },
    }));
    const result = await flow.handler(c, { action: "run", flowName: "save_one", params: { target: "/Game/T" } }) as { success: boolean };
    expect(result.success).toBe(true);
    expect(sent[0].assetPath).toBe("/Game/T");
  });

  it("carries the envelope on a failed step and on the run", async () => {
    const c = ctx();
    (c.bridge as unknown as { call: unknown }).call = async () => ({ success: false, error: "Asset not found: /Game/A" });
    const flow = createFlowTool(registry, () => configWith({
      one: { steps: { 1: { task: "asset.save", options: { assetPath: "/Game/A" } } } },
    }));
    const body = await flow.handler(c, { action: "run", flowName: "one" }) as {
      error: Record<string, unknown>;
      steps: Array<{ error: Record<string, unknown> }>;
    };
    expect(body.steps[0].error).toMatchObject({ class: "usage", step: "1", retryable: false, message: "Asset not found: /Game/A" });
    expect(body.error).toMatchObject({ class: "usage", step: "1" });
  });
});

describe("expressionProblem", () => {
  it.each([
    ["probe.connected && probe.world.mode == 'play'", null],
    ["${steps.1.ok}", null],
    ["plain prose is truthiness", null],
    [true, null],
  ])("accepts %j", (expr, expected) => {
    expect(expressionProblem(expr as string, ["probe", "editor"])).toBe(expected);
  });

  it.each(["editor.connected = true", "unknownns.x == 1", "(probe.connected == true"])("rejects %j", (expr) => {
    expect(expressionProblem(expr, ["probe", "editor"])).toMatch(/not a valid expression/);
  });
});
