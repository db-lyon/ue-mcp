/**
 * `when:` expressions and the `${project.*}` / `${editor.*}` / `${session.*}`
 * namespaces the flow runner is given.
 */
import { describe, expect, it } from "vitest";
import type { FlowStepResult } from "@db-lyon/flowkit";
import { hostNamespaces, makeConditionEvaluator } from "../../src/flow/condition.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../src/flow/schema.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import type { ToolContext } from "../../src/core/types.js";
import type { IBridge } from "../../src/bridge/bridge.js";

function ctx(connected = true): ToolContext {
  return {
    project: {
      projectName: "Shrine",
      projectPath: "C:/p/Shrine.uproject",
      projectDir: "C:/p",
      contentDir: "C:/p/Content",
      engineAssociation: "5.6",
    },
    bridge: { isConnected: connected, call: async () => ({ success: true }) } as unknown as IBridge,
    session: { name: "alpha" },
    sessions: { size: 2 },
  } as unknown as ToolContext;
}

const steps = [
  { stepNumber: 1, name: "asset.list", type: "task", skipped: false, duration: 0, result: { success: true, data: { count: 3, ok: true, name: "x" } } },
  { stepNumber: 2, name: "asset.read", type: "task", skipped: false, duration: 0, result: { success: true, data: { ok: false } } },
] as FlowStepResult[];

const evaluate = (expression: string, params?: Record<string, unknown>, connected = true) =>
  makeConditionEvaluator(hostNamespaces(ctx(connected)))(expression, { steps, params, context: {} });

describe("when: expressions", () => {
  it.each([
    ["${steps.1.count} == 3", true],
    ["${steps.1.count} > 2 && ${steps.2.ok} == false", true],
    ["steps.1.count >= 4 or steps.asset.list.ok", true],
    ["not steps.2.ok", true],
    ["!(${steps.1.ok} && ${steps.2.ok})", true],
    ["project.name == 'Shrine'", true],
    ["project.engine != \"5.5\"", true],
    ["editor.connected", true],
    ["session.count > 1 and session.name == 'alpha'", true],
    ["params.mode == 'fast'", true],
    ["${steps.1.count} < 3", false],
  ])("%s is %s", async (expression, expected) => {
    expect(await evaluate(expression, { mode: "fast" })).toBe(expected);
  });

  it("reads the editor as it is when the condition runs", async () => {
    expect(await evaluate("editor.connected", undefined, false)).toBe(false);
  });

  it.each([
    ["${steps.1.ok}", true],
    ["${steps.2.ok}", false],
    ["${steps.1.name} is ready", true],
    ["yes", true],
    ["false", false],
    ["0", false],
    ["", false],
  ])("keeps plain `${}` truthiness for %j", async (expression, expected) => {
    expect(await evaluate(expression)).toBe(expected);
  });

  it("names the grammar when an expression does not parse", async () => {
    expect(() => evaluate("${steps.1.count} = 3")).toThrow(/not a valid expression/);
  });

  it("fails on a step that has not run, as an option reference does", async () => {
    expect(() => evaluate("${steps.9.ok} == true")).toThrow(/no completed step/);
  });

  it("never evaluates JavaScript", async () => {
    expect(() => evaluate("process.exit(1) == 1")).toThrow(/not a valid expression/);
  });
});

describe("host namespaces in a flow", () => {
  const tool = categoryTool("asset", "Assets.", { read: bp("read", "read_asset") });

  it("interpolate project, editor and session references and gate steps with when", async () => {
    const calls: Array<Record<string, unknown>> = [];
    const c = ctx();
    c.bridge = { isConnected: true, call: async (_m: string, p: Record<string, unknown>) => { calls.push(p); return { success: true }; } } as unknown as IBridge;
    const config = FlowConfigSchema.parse({
      ...buildDefaults([tool]),
      flows: {
        go: {
          steps: {
            1: { task: "asset.read", options: { assetPath: "/Game/${project.name}/Thing", note: "${session.name}" } },
            2: { task: "asset.read", when: "editor.connected == false", options: { assetPath: "/Game/Skipped" } },
            3: { task: "asset.read", when: "session.count > 1", options: { assetPath: "/Game/Ran" } },
          },
        },
      },
    });
    const flow = createFlowTool(buildFlowRegistry([tool]), () => config);
    const result = await flow.handler(c, { action: "run", flowName: "go" }) as { success: boolean; steps: Array<{ skipped: boolean }> };
    expect(result.success).toBe(true);
    expect(result.steps.map((s) => s.skipped)).toEqual([false, true, false]);
    expect(calls).toEqual([
      { assetPath: "/Game/Shrine/Thing", note: "alpha" },
      { assetPath: "/Game/Ran" },
    ]);
  });
});
