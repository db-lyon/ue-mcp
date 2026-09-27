/**
 * flow(plan) reports what a run would be refused for, from the same gates that
 * refuse it, declared as flow checks. Running is gated exactly as before.
 */
import { afterAll, beforeEach, describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { FlowConfigSchema, type FlowConfig } from "../../src/flow/schema.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { gateCandidates, resetRulings } from "../../src/dispatch/python-gate.js";
import { dispatchFlowCall, type DispatchDeps } from "../../src/dispatch/server-dispatch.js";
import { SessionRegistry } from "../../src/sessions/session.js";
import { GuardRegistry } from "../../src/flow/guard.js";
import { DialogGuard, type GuardDeps } from "../../src/editor/dialog-guard.js";
import type { PlanPreflight } from "../../src/flow/preflight.js";
import type { ToolContext } from "../../src/core/types.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

const SUMMARY = "capture a screenshot of the viewport";

const registry = buildFlowRegistry(ALL_TOOLS);
const tasks = buildDefaults(ALL_TOOLS).tasks;

function config(flows: Record<string, unknown>): FlowConfig {
  return FlowConfigSchema.parse({ tasks, flows });
}

function ctx(connected = false): ToolContext {
  return {
    bridge: {
      isConnected: connected,
      capabilities: undefined,
      call: async () => { throw new Error("no editor"); },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default" }),
    },
    project: { ensureLoaded: () => {} },
    getToolGraph: () => ALL_TOOLS,
  } as unknown as ToolContext;
}

async function plan(cfg: FlowConfig, flowName: string, c = ctx()): Promise<{ steps: unknown[]; preflight: PlanPreflight }> {
  const flow = createFlowTool(registry, () => cfg);
  return await flow.handler(c, { action: "plan", flowName }) as { steps: unknown[]; preflight: PlanPreflight };
}

beforeEach(() => resetRulings());

describe("the Python gate in a plan", () => {
  const python = config({
    py: { steps: { 1: { task: "editor.execute_python", options: { taskSummary: SUMMARY, code: "print(1)" } } } },
  });

  // Connected, so the only gate in play is Python's own.
  it("reports a step the gate would refuse", async () => {
    const { preflight, steps } = await plan(python, "py", ctx(true));
    expect(steps).toHaveLength(1);
    expect(preflight.ok).toBe(false);
    expect(preflight.refused).toEqual([
      expect.objectContaining({ path: "1", name: "editor.execute_python", message: expect.stringMatching(/execute_python's gate/) }),
    ]);
  });

  it("still refuses the same way when the flow runs", async () => {
    const flow = createFlowTool(registry, () => python);
    const run = await flow.handler(ctx(true), { action: "run", flowName: "py" }) as { steps: Array<{ data?: Record<string, unknown> }> };
    expect(run.steps[0].data).toMatchObject({ blocked: true, reason: "candidates_not_ruled_out" });
  });

  it("clears a step whose rulings cover every candidate, and remembers none of them", async () => {
    const ruledOut = gateCandidates(ctx(), SUMMARY).map((c) => ({ action: `${c.tool}(${c.action})`, reason: "does not do this particular task" }));
    expect(ruledOut.length).toBeGreaterThan(0);
    const ruled = config({
      py: { steps: { 1: { task: "editor.execute_python", options: { taskSummary: SUMMARY, code: "print(1)", ruledOut } } } },
    });
    expect((await plan(ruled, "py", ctx(true))).preflight).toMatchObject({ ok: true, refused: [] });
    // The plan decided without recording: the unruled flow is still refused.
    expect((await plan(python, "py", ctx(true))).preflight.ok).toBe(false);
  });
});

describe("the editor-connected gate in a plan", () => {
  const mixed = config({
    mixed: { steps: { 1: { task: "project.read_config", options: { configName: "Engine" } }, 2: { task: "asset.list" } } },
  });

  it("reports the steps that need an editor, and only those", async () => {
    const { preflight } = await plan(mixed, "mixed");
    expect(preflight.refused.map((r) => r.path)).toEqual(["2"]);
    expect(preflight.steps.find((s) => s.path === "1")?.status).toBe("run");
  });

  it("clears them once an editor is connected", async () => {
    expect((await plan(mixed, "mixed", ctx(true))).preflight).toMatchObject({ ok: true, refused: [] });
  });
});

describe("the untargeted-write gate in a plan", () => {
  const fixture = new ProjectFixture("ue-mcp-preflight-");
  afterAll(() => fixture.cleanup());

  function twoEditors() {
    const sessions = new SessionRegistry(new GuardRegistry());
    sessions.register({ projectPath: fixture.makeProject("Alpha"), name: "alpha" });
    sessions.register({ projectPath: fixture.makeProject("Beta"), name: "beta" });
    const deps = {
      sessions,
      loads: { dispatchUnion: { tools: [] } },
      lockingCfg: { enabled: false, ttlSeconds: 0 },
      dialogGuardFor: () => ({ check: async () => ({ allow: true }) }),
      elicit: () => undefined,
      client: () => undefined,
    } as unknown as DispatchDeps;
    const cfg = config({ build: { steps: { 1: { task: "project.read_config", options: { configName: "Engine" } } } } });
    const flow = createFlowTool(registry, () => cfg);
    flow.injectedEditorParam = true;
    const base = { bridge: sessions.active.guarded, project: sessions.active.project, sessions } as unknown as ToolContext;
    return { deps, flow, base };
  }

  it("reports that an untargeted run would be refused, in the words the run is refused in", async () => {
    const { deps, flow, base } = twoEditors();
    const run = await dispatchFlowCall(deps, flow, base, { action: "run", flowName: "build" });
    expect(run.isError).toBe(true);
    const refusal = run.content.map((b) => b.text).join("\n");

    const planned = await dispatchFlowCall(deps, flow, base, { action: "plan", flowName: "build" });
    expect(planned.isError).toBeUndefined();
    const body = JSON.parse(planned.content[0].text as string) as { preflight: PlanPreflight };
    expect(body.preflight.ok).toBe(false);
    expect(body.preflight.refused).toHaveLength(1);
    expect(body.preflight.refused[0].path).toBeUndefined();
    expect(refusal).toContain(body.preflight.refused[0].message);
  });

  it("reports nothing once the plan names its editor", async () => {
    const { deps, flow, base } = twoEditors();
    const planned = await dispatchFlowCall(deps, flow, base, { action: "plan", flowName: "build", editor: "beta" });
    const body = JSON.parse(planned.content[0].text as string) as { preflight: PlanPreflight };
    expect(body.preflight.refused.filter((r) => r.path === undefined)).toEqual([]);
  });
});

describe("the dialog gate's plan-time question", () => {
  function guard(mode: "auto" | "interactive") {
    const asked: unknown[] = [];
    const deps: GuardDeps = {
      mode: () => mode,
      probe: async () => ({ dialogs: [{ title: "Save Content", message: "m", buttons: ["Save", "Cancel"], choices: [] }] }),
      press: async () => ({ success: true }),
      elicit: () => (async (p: unknown) => { asked.push(p); return { action: "cancel" }; }) as never,
    };
    return { guard: new DialogGuard(deps), asked };
  }

  it("says a blocked call would be refused without asking anyone or pressing anything", async () => {
    const { guard: g, asked } = guard("interactive");
    expect(await g.wouldRefuse("asset.list", "action")).toBe(true);
    expect(await g.wouldRefuse("list_assets", "bridge")).toBe(true);
    expect(asked).toEqual([]);
  });

  it("lets through what is allowed while a modal is up", async () => {
    const { guard: g } = guard("auto");
    expect(await g.wouldRefuse("editor.list_dialogs", "action")).toBe(false);
  });
});

describe("a project's own checks", () => {
  it("read the same gate namespace at run time", async () => {
    const cfg = config({
      guarded: { steps: { 1: { task: "asset.list", checks: [{ when: "gate.editor", action: "skip", message: "No editor." }] } } },
    });
    const run = await createFlowTool(registry, () => cfg).handler(ctx(false), { action: "run", flowName: "guarded" }) as {
      success: boolean;
      steps: Array<{ skipped: boolean }>;
    };
    expect(run.success).toBe(true);
    expect(run.steps[0].skipped).toBe(true);
  });
});
