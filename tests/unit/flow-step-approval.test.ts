/**
 * flows.stepApproval: a run started with `stepApproval` asks the named
 * extension before each task, with the task's resolved options. A denial
 * fails that step and stops the run before the task touches the editor; a
 * run naming an approver that is not registered fails closed. Driven through
 * the real flow tool and runner with a fake bridge.
 */
import { afterEach, describe, expect, it } from "vitest";
import { z } from "zod";
import { FlowConfigSchema } from "../../src/flow/schema.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { categoryTool } from "../../src/surface/category-tool.js";
import { ProjectContext } from "../../src/config/project.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";
import { parseStepApproval, registerFlowStepApprover, type FlowStepApprovalRequest, type FlowStepDecision } from "../../src/flow/step-approval.js";
import { DAEMON_FEATURES, EXTENSION_FEATURES } from "../../src/daemon/daemon.js";

function fakeBridge() {
  const calls: string[] = [];
  const bridge = {
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    call: async (method: string) => {
      calls.push(method);
      return { success: true };
    },
  } as unknown as IBridge;
  return { bridge, calls };
}

const probe = categoryTool(
  "probe",
  "Test-only category.",
  {
    touch: { kind: "bridge", effect: "mutate", description: "A benign bridge action.", bridge: "touch_the_thing" },
    run_code: { kind: "bridge", effect: "unknown", description: "Stands in for running code.", bridge: "run_the_code" },
  },
  { note: z.string().optional() } as Record<string, z.ZodType>,
);

const FLOWS = {
  mixed: {
    description: "a benign step, then code, then another benign step",
    steps: { "1": { task: "probe.touch", options: { note: "first" } }, "2": { task: "probe.run_code", options: { note: "import os" } }, "3": { task: "probe.touch" } },
  },
};

async function run(ctx: Partial<ToolContext>, bridge: IBridge, params: Record<string, unknown> = {}) {
  const flowTool = createFlowTool(buildFlowRegistry([probe]), () => FlowConfigSchema.parse({ "ue-mcp": { version: 1 }, flows: FLOWS }));
  return (await flowTool.handler({ bridge, project: new ProjectContext(), ...ctx }, { action: "run", flowName: "mixed", params })) as {
    success: boolean;
    failedStep?: string;
    steps: Array<{ name: string; success: boolean; error?: { message: string } }>;
    runId: string;
  };
}

const disposers: Array<() => void> = [];
afterEach(() => {
  for (const d of disposers.splice(0)) d();
});

function approver(decide: (r: FlowStepApprovalRequest) => FlowStepDecision) {
  const asked: FlowStepApprovalRequest[] = [];
  disposers.push(registerFlowStepApprover("studio", async (r) => {
    asked.push(r);
    return decide(r);
  }));
  return asked;
}

describe("flow step approval", () => {
  it("asks before every task with its resolved options and the caller's token; allowed, the run completes", async () => {
    const asked = approver(() => ({ allow: true }));
    const { bridge, calls } = fakeBridge();
    const r = await run({ flowStepApproval: { approver: "studio", token: "session-1" } }, bridge);
    expect(r.success).toBe(true);
    expect(asked.map((a) => [a.task, a.options.note, a.token, a.phase, a.flowName])).toEqual([
      ["probe.touch", "first", "session-1", "task", "mixed"],
      ["probe.run_code", "import os", "session-1", "task", "mixed"],
      ["probe.touch", undefined, "session-1", "task", "mixed"],
    ]);
    expect(asked.every((a) => a.runId === r.runId)).toBe(true);
    expect(calls).toEqual(["touch_the_thing", "run_the_code", "touch_the_thing"]);
  });

  it("a denied step fails with the reason, never reaches the editor, and stops the run", async () => {
    approver((req) => (req.task === "probe.run_code" ? { allow: false, reason: "Declined at the approval." } : { allow: true }));
    const { bridge, calls } = fakeBridge();
    const r = await run({ flowStepApproval: { approver: "studio", token: "t" } }, bridge);
    expect(r.success).toBe(false);
    expect(r.failedStep).toBe("probe.run_code");
    expect(r.steps[1].error?.message).toBe("Declined at the approval.");
    expect(calls).toEqual(["touch_the_thing"]);
  });

  it("an approver that throws denies the step", async () => {
    approver(() => {
      throw new Error("lost the session");
    });
    const { bridge, calls } = fakeBridge();
    const r = await run({ flowStepApproval: { approver: "studio", token: "t" } }, bridge);
    expect(r.success).toBe(false);
    expect(r.steps[0].error?.message).toMatch(/could not be approved.*lost the session/);
    expect(calls).toEqual([]);
  });

  it("a run naming an approver that is not registered fails closed", async () => {
    const { bridge, calls } = fakeBridge();
    const r = await run({ flowStepApproval: { approver: "nobody", token: "t" } }, bridge);
    expect(r.success).toBe(false);
    expect(r.steps[0].error?.message).toMatch(/needs approval from nobody, which is not loaded/);
    expect(calls).toEqual([]);
  });

  it("a run without stepApproval asks nobody, as before", async () => {
    const asked = approver(() => ({ allow: false }));
    const { bridge, calls } = fakeBridge();
    expect((await run({}, bridge)).success).toBe(true);
    expect(asked).toEqual([]);
    expect(calls).toHaveLength(3);
  });

  it("a newer registration replaces the older, and removing the older leaves the newer", async () => {
    const first = registerFlowStepApprover("studio", async () => ({ allow: false }));
    disposers.push(registerFlowStepApprover("studio", async () => ({ allow: true })));
    first();
    const { bridge } = fakeBridge();
    expect((await run({ flowStepApproval: { approver: "studio", token: "t" } }, bridge)).success).toBe(true);
  });

  it("reads the run body's spec, refusing a malformed one", () => {
    expect(parseStepApproval(undefined)).toBeNull();
    expect(parseStepApproval({ approver: "studio", token: "x" })).toEqual({ approver: "studio", token: "x" });
    expect(() => parseStepApproval({ approver: "studio" })).toThrow(/stepApproval/);
    expect(() => parseStepApproval("studio")).toThrow(/stepApproval/);
  });

  it("is advertised to extensions and on the HTTP API", () => {
    expect(EXTENSION_FEATURES).toContain("flows.stepApproval");
    expect(DAEMON_FEATURES).toContain("flows.stepApproval");
  });
});
