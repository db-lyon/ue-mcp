/**
 * The universal flows as a release gate (plans/task-flow-architecture.md 3.9).
 *
 * Every flow the default layer ships is planned and run through the shipped
 * server's `flow` tool against tests/ue_mcp. A run passes when no step fails
 * without having declared `ignore_failure`, and its plan must have been clean
 * first: preflight ok, nothing refused, the same top-level steps.
 *
 * The scene is prepared and restored with the repo's own flows in
 * tests/ue_mcp/ue-mcp.yml (smoke_scene_prepare, smoke_scene_teardown), and
 * everything the flows write is deleted afterwards.
 *
 * Two flows are planned and never run: this suite attaches to an editor
 * somebody else started and never stops or rebuilds it (PLAN_ONLY).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { builtinFlows } from "../../src/flow/loader.js";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();

const LONG = 20 * 60_000;
const STAMP = `${Date.now().toString(36)}`;
const GATE_PKG = `/Game/UniversalFlowGate_${STAMP}`;
/** niagara_fire takes its packagePath at run time, so it lands somewhere unique. */
const FIRE_PKG = `${GATE_PKG}/Fire`;
/** Assets the commit flows compile and save, created by the gate itself. */
const COMMIT_PKG = `${GATE_PKG}/Commit`;

/** Run order, and the runtime params each run takes. */
const RUNS: Array<{ flow: string; params?: Record<string, unknown> }> = [
  { flow: "niagara_fire", params: { packagePath: FIRE_PKG } },
  { flow: "texture_bomb" },
  { flow: "beacon" },
  { flow: "neon_shrine" },
  { flow: "neon_shrine_cleanup" },
  { flow: "commit_niagara", params: { assetPath: `${FIRE_PKG}/NS_Fire` } },
  { flow: "commit_blueprint", params: { assetPath: `${COMMIT_PKG}/BP_Commit` } },
  { flow: "commit_widget", params: { assetPath: `${COMMIT_PKG}/WBP_Commit` } },
  { flow: "commit_statetree", params: { assetPath: `${COMMIT_PKG}/ST_Commit` } },
  { flow: "commit_material", params: { assetPath: `${COMMIT_PKG}/M_Commit` } },
  { flow: "save_and_verify_clean" },
  { flow: "qa_gate", params: { directory: GATE_PKG } },
  { flow: "playtest_smoke" },
  { flow: "sandbox", params: { folder: `${GATE_PKG}/Sandbox` } },
  // The editor is up, so this only reports status: the start step is skipped.
  { flow: "editor_up" },
];

/** Planned, never run: each stops, restarts or rebuilds the editor the suite is attached to. */
const PLAN_ONLY: Record<string, string> = {
  clean_restart: "stops and starts the editor",
  cpp_iterate: "compiles the project's C++ into the running editor, or stops it for a full build",
};

/** Content the fixed-path flows write, removed before and after the gate. */
const OWNED_FOLDERS = ["/Game/Flows/Beacon", "/Game/Demo", GATE_PKG];
const OWNED_ASSETS = ["/Game/Materials/Functions/MF_TextureBomb"];
/** Parents a flow may have created; removed only when empty. */
const PARENT_FOLDERS = ["/Game/Flows", "/Game/Materials/Functions", "/Game/Materials"];

const UNIVERSAL = builtinFlows() as Record<string, { steps: Record<string, { flow?: string }> }>;

interface StepReport {
  stepNumber: number;
  name: string;
  type: string;
  skipped?: boolean;
  success: boolean;
  ignoredFailure?: boolean;
  error?: { message: string };
  nestedSteps?: StepReport[];
}

interface RunBody {
  success: boolean;
  failedStep?: string;
  summary: string;
  steps: StepReport[];
}

interface PlanBody {
  steps: Array<{ name: string; stepNumber: number }>;
  preflight: { ok: boolean; refused: Array<{ path?: string; name: string; message: string }> };
  validation: { ok: boolean; problems: unknown[] };
  frozen: { definition?: Record<string, unknown>; refused?: string[] };
}

/** A plan lists hooks too, numbered below 1 (on_start) or from 10000 (the rest); a run's steps are the main ones. */
const isMainStep = (s: { stepNumber: number }) => s.stepNumber > 0 && s.stepNumber < 10_000;

let server: LiveServer;
let restored = false;

async function runFlow(flowName: string, params?: Record<string, unknown>): Promise<LiveCallResult> {
  return server.call("flow", { action: "run", flowName, ...(params ? { params } : {}) }, LONG);
}

/** Every step that failed without declaring it would, at any depth. */
function unexpectedFailures(steps: StepReport[], prefix = ""): string[] {
  const out: string[] = [];
  for (const s of steps) {
    const at = `${prefix}${s.stepNumber}`;
    if (!s.skipped && !s.success && !s.ignoredFailure) {
      out.push(`${at}. ${s.name}: ${s.error?.message ?? "failed"}`);
    }
    out.push(...unexpectedFailures(s.nestedSteps ?? [], `${at}/`));
  }
  return out;
}

/** Names of every nested flow a run went through. */
function flowsRun(steps: StepReport[]): string[] {
  return steps.flatMap((s) => [...(s.type === "flow" ? [s.name] : []), ...flowsRun(s.nestedSteps ?? [])]);
}

function expectCompleted(flowName: string, result: LiveCallResult): RunBody {
  expect(result.isError, `${flowName} errored: ${result.text}`).toBe(false);
  const body = resultJson<RunBody>(result);
  const failures = unexpectedFailures(body.steps);
  expect(failures, `${flowName} failed:\n${body.summary}`).toEqual([]);
  expect(body.success, `${flowName} did not complete:\n${body.summary}`).toBe(true);
  expect(body.failedStep).toBeUndefined();
  return body;
}

/** Remove what the gate owns. Each call reports per path, so nothing here throws. */
async function removeOwnedContent(): Promise<void> {
  await server.call("asset", { action: "delete_folder", paths: OWNED_FOLDERS, force: true });
  for (const assetPath of OWNED_ASSETS) {
    await server.call("asset", { action: "delete", assetPath, force: true });
  }
  await server.call("asset", { action: "delete_folder", paths: PARENT_FOLDERS });
}

/** The assets the commit flows compile and save. */
async function createCommitAssets(): Promise<void> {
  const created = [
    await server.call("blueprint", { action: "create", name: "BP_Commit", packagePath: COMMIT_PKG, parentClass: "Actor" }),
    await server.call("widget", { action: "create", name: "WBP_Commit", packagePath: COMMIT_PKG }),
    await server.call("gameplay", { action: "create_state_tree", name: "ST_Commit", packagePath: COMMIT_PKG }),
    await server.call("material", { action: "create", name: "M_Commit", packagePath: COMMIT_PKG }),
  ];
  for (const r of created) expect(r.isError, r.text).toBe(false);
}

/** The messages of a flow's own declared checks, which may refuse a plan on the editor's state. */
function declaredCheckMessages(name: string): string[] {
  const def = UNIVERSAL[name] as unknown as { checks?: Array<{ message?: string }> };
  return (def.checks ?? []).map((c) => (c.message ?? "").trim());
}

/** Park on the scratch level, remove the gate's content, leave a fresh MCP_Home. */
async function restoreScene(): Promise<void> {
  expectCompleted("smoke_scene_prepare", await runFlow("smoke_scene_prepare"));
  await removeOwnedContent();
  expectCompleted("smoke_scene_teardown", await runFlow("smoke_scene_teardown"));
}

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
  // A crashed earlier run may have left the fixed-path content behind.
  await server.call("demo", { action: "cleanup" }, LONG);
  expectCompleted("smoke_scene_prepare", await runFlow("smoke_scene_prepare"));
  await removeOwnedContent();
  await createCommitAssets();
}, LONG);

afterAll(async () => {
  try {
    if (server && !restored) await restoreScene().catch(() => undefined);
  } finally {
    await server?.close();
    closeLiveBridges();
  }
}, LONG);

describe("universal flows as release gate", () => {
  it("runs every universal flow, directly or nested in one that is run, or plans it and says why not", () => {
    const direct = RUNS.map((r) => r.flow);
    const nested = direct.flatMap((name) =>
      Object.values(UNIVERSAL[name]?.steps ?? {}).flatMap((s) => (s.flow ? [s.flow] : [])),
    );
    for (const name of Object.keys(PLAN_ONLY)) expect(UNIVERSAL[name], `${name} is not a universal flow`).toBeDefined();
    const covered = new Set([...direct, ...nested, ...Object.keys(PLAN_ONLY)]);
    const missing = Object.keys(UNIVERSAL).filter((name) => !covered.has(name));
    expect(missing, "universal flows with no place in the gate; add each with its cleanup").toEqual([]);
    for (const name of direct) expect(UNIVERSAL[name], `${name} is not a universal flow`).toBeDefined();
  });

  it("plans every universal flow with nothing refused", async () => {
    for (const name of Object.keys(UNIVERSAL)) {
      const params = RUNS.find((r) => r.flow === name)?.params;
      const result = await server.call("flow", { action: "plan", flowName: name, ...(params ? { params } : {}) });
      expect(result.isError, `${name}: ${result.text}`).toBe(false);
      const plan = resultJson<PlanBody>(result);
      expect(plan.validation.problems, `${name} validation`).toEqual([]);
      expect(plan.frozen.refused, `${name} did not freeze`).toBeUndefined();
      // A flow's own checks read the editor's state; only the server's gates must stay quiet.
      const own = declaredCheckMessages(name);
      const gated = plan.preflight.refused.filter((r) => !own.includes(r.message.trim()));
      expect(gated, `${name} would be refused`).toEqual([]);
    }
  }, LONG);

  for (const run of RUNS) {
    it(`runs ${run.flow} to completion, as planned`, async () => {
      const plan = resultJson<PlanBody>(
        await server.call("flow", { action: "plan", flowName: run.flow, ...(run.params ? { params: run.params } : {}) }),
      );
      expect(plan.preflight.ok).toBe(true);

      const body = expectCompleted(run.flow, await runFlow(run.flow, run.params));
      expect(body.steps.map((s) => s.name)).toEqual(plan.steps.filter(isMainStep).map((s) => s.name));

      const declared = Object.values(UNIVERSAL[run.flow]!.steps).flatMap((s) => (s.flow ? [s.flow] : []));
      expect(flowsRun(body.steps)).toEqual(expect.arrayContaining(declared));
    }, LONG);
  }

  it("restores the scene with the repo's own flows", async () => {
    await restoreScene();
    restored = true;
  }, LONG);
});
