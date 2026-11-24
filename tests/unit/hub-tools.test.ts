/**
 * The hub category's actions: reading goals and playtests, creating and
 * updating goals, adding and removing links on goals, playtests and runs, all
 * in the goal store and never in a project file; running a plan; and the
 * category's declaration (every action has its params, none runs without the
 * daemon's hub).
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { CheckDetail, GoalState, HubOverview, PlanRun, QueueState, RunRecord, TrackerLink } from "../../src/hub/contract.js";
import { GoalFiles } from "../../src/hub/goal-files.js";
import { Goals } from "../../src/hub/goals.js";
import { notFound } from "../../src/hub/util.js";
import { HUB_ACTIONS, runHubAction, validateHub, type HubToolTarget } from "../../src/hub/hub-tools.js";
import { hubTool } from "../../src/tools/hub.js";

let root: string;
let goals: Goals;
let runs: Map<string, RunRecord>;
let planned: Array<{ body: unknown; trigger: string }>;
let hub: HubToolTarget;
const DOOR = "/Game/Tests/DoorOpens";
const ISSUE = "https://git.example.com/ember/hollow/issues/42";

const stateOf = (id: string): GoalState => {
  const goal = goals.get(id);
  if (!goal) throw notFound(`no goal ${id}`);
  const parents = goals.list().filter((g) => (g.requires ?? []).some((r) => r.goal === id)).map((g) => g.id);
  return { goal, status: "not_started", progress: { met: 0, total: goal.checks.length }, parents, requires: (goal.requires ?? []).map((r) => ({ goal: r.goal, required: r.required !== false, status: "not_started" as const })), checks: goal.checks.map((c) => ({ id: `${c.kind}:${c.target}`, check: c, name: c.target.split("/").pop()!, goalIds: [id], health: "never_run", last: null, recent: [], passRate: null })) };
};

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-tools-"));
  goals = new Goals({ store: new GoalFiles(path.join(root, "data")), projectDir: root });
  planned = [];
  runs = new Map([["run_1", { runId: "run_1", kind: "script", target: DOOR, name: "DoorOpens", outcome: "failed", startedAt: "2026-11-22T00:00:00.000Z", source: "person" }]]);
  const setLinks = (runId: string, f: (l: TrackerLink[]) => TrackerLink[]) => {
    const r = runs.get(runId);
    if (!r) throw notFound(`no run ${runId}`);
    const next = { ...r, links: f(r.links ?? []) };
    runs.set(runId, next);
    return next;
  };
  hub = {
    goals,
    overview: async () => ({ goals: goals.list().map((g) => stateOf(g.id)), warnings: [] }) as unknown as HubOverview,
    goal: async (id: string) => stateOf(id),
    check: async (id: string) => ({ state: { id, check: { kind: "script", target: DOOR }, name: "DoorOpens", goalIds: [], health: "failing", last: null, recent: [], passRate: 0, links: goals.meta(id)?.links }, runs: [...runs.values()], more: false }) as CheckDetail,
    addRunLink: (runId: string, body: unknown) => setLinks(runId, (l) => [...l, (body as { link: TrackerLink }).link]),
    removeRunLink: (runId: string, url: unknown) => setLinks(runId, (l) => l.filter((x) => x.url !== url)),
    updateCheckMeta: (id: string, body: unknown) => goals.updateMeta({ kind: "script", target: id.slice("script:".length) }, body),
    plan: async (body: unknown, trigger: string = "person") => {
      planned.push({ body, trigger });
      const scope = (body as { scope: { kind: string; goalId?: string; checks?: Array<{ target: string }> } }).scope;
      const targets = scope.kind === "goal" ? [DOOR] : scope.checks!.map((c) => c.target);
      return { id: "plan_1", title: "Run", scope, options: {}, state: "queued", trigger, createdAt: "2026-11-23T00:00:00.000Z", items: targets.map((t) => ({ check: { kind: "script", target: t }, name: t.split("/").pop()!, state: "queued" })) } as unknown as PlanRun;
    },
    queueView: () => ({ paused: false, current: null, queued: [], recent: [], schedules: [], editor: { connected: true }, waiting: "Play In Editor is in use" }) as QueueState,
  } as unknown as HubToolTarget;
});
afterEach(() => fs.rmSync(root, { recursive: true, force: true }));

const run = (name: string, input: Record<string, unknown>) => runHubAction(hub, name, input) as Promise<any>;

describe("hub actions", () => {
  it("are declared on the hub category, each with its params, and every one runs a function of its name", () => {
    expect(Object.keys(hubTool.actions)).toEqual([...HUB_ACTIONS]);
    for (const [name, spec] of Object.entries(hubTool.actions)) {
      expect(spec.kind, name).toBe("handler");
      expect(spec.description, name).toBeTruthy();
      expect(spec.params, name).toBeDefined();
      // A declared param is in the category's shared shape.
      for (const p of [...(spec.params?.required ?? []), ...(spec.params?.optional ?? [])]) expect(Object.keys(hubTool.schema), `${name}.${p}`).toContain(p);
    }
    expect(hubTool.actions.run_plan.effect).toBe("mutate");
    expect(hubTool.actions.read_queue.effect).toBe("read");
  });

  it("refuse without the daemon's hub, saying where it runs", async () => {
    const ctx = { project: { projectDir: path.join(root, "nowhere") } } as never;
    const spec = hubTool.actions.list_goals;
    if (spec.kind !== "handler") throw new Error("handler expected");
    await expect(spec.handler(ctx, {})).rejects.toThrow(/runs in the project's daemon/);
  });

  it("create, update, list and read a goal, in the goal store and not the project", async () => {
    const c = await run("create_goal", { title: "Door opens", priority: "p1", due: "2026-12-01", checks: [{ kind: "script", target: DOOR }], links: [{ url: ISSUE, rel: "counterpart" }] });
    const id = goals.list()[0].id;
    expect(id).toMatch(/^door-opens-[0-9a-z]{4}$/);
    expect(c.created.id).toBe(id);
    expect(fs.existsSync(path.join(root, "QA"))).toBe(false);
    await run("update_goal", { id, owner: "Ama" });
    expect(goals.get(id)).toMatchObject({ owner: "Ama", priority: "p1", due: "2026-12-01" });
    await expect(run("update_goal", { id: "nope", owner: "x" })).rejects.toThrow("no goal nope");
    const list = await run("list_goals", {});
    expect(list.goals[0]).toMatchObject({ id, status: "not_started", progress: "0/1", owner: "Ama", priority: "p1", links: [{ url: ISSUE, rel: "counterpart" }] });
    const read = await run("read_goal", { id });
    expect(read).toMatchObject({ goal: { owner: "Ama" }, checks: [{ id: `script:${DOOR}`, health: "never_run" }] });
    const check = await run("read_check", { check_id: `script:${DOOR}` });
    expect(check.runs[0]).toMatchObject({ runId: "run_1", outcome: "failed" });
  });

  it("add and remove links on a goal, a playtest and a run", async () => {
    goals.create({ title: "A", id: "a", checks: [{ kind: "script", target: DOOR }] });
    await run("add_link", { goal_id: "a", url: ISSUE, title: "Door", rel: "counterpart" });
    // The same URL again replaces it.
    await run("add_link", { goal_id: "a", url: ISSUE, title: "Door, renamed" });
    expect(goals.get("a")?.links).toEqual([{ url: ISSUE, title: "Door, renamed" }]);
    await expect(run("add_link", { goal_id: "a", url: "javascript:alert(1)" })).rejects.toThrow();
    // A playtest's links are its own, whichever goals list it.
    await run("add_link", { check_id: `script:${DOOR}`, url: "jira://HOL-3" });
    expect(goals.meta(`script:${DOOR}`)?.links).toEqual([{ url: "jira://HOL-3" }]);
    await run("remove_link", { check_id: `script:${DOOR}`, url: "jira://HOL-3" });
    expect(goals.meta(`script:${DOOR}`)).toBeUndefined();
    await expect(run("remove_link", { goal_id: "a", url: "https://nowhere" })).rejects.toThrow();
    const r = await run("add_link", { run_id: "run_1", url: ISSUE, rel: "filed-from-run" });
    expect(r).toEqual({ runId: "run_1", links: [{ url: ISSUE, rel: "filed-from-run" }] });
    await expect(run("add_link", { run_id: "run_1", goal_id: "a", url: ISSUE })).rejects.toThrow();
    await expect(run("add_link", { url: ISSUE })).rejects.toThrow("say what to link");
  });

  it("require and unrequire a goal, refusing a loop", async () => {
    goals.create({ title: "Alpha", id: "alpha", kind: "milestone" });
    goals.create({ title: "Beta", id: "beta", kind: "milestone" });
    goals.create({ title: "Door", id: "door" });
    await run("require_goal", { goal_id: "alpha", requires: "door" });
    await run("require_goal", { goal_id: "beta", requires: "door", required: false });
    expect(goals.get("beta")?.requires).toEqual([{ goal: "door", required: false }]);
    const read = await run("read_goal", { id: "door" });
    expect(read.partOf).toEqual(["alpha", "beta"]);
    await expect(run("require_goal", { goal_id: "door", requires: "alpha" })).rejects.toThrow("loop: door > alpha > door");
    await run("unrequire_goal", { goal_id: "alpha", requires: "door" });
    expect(goals.get("alpha")?.requires).toBeUndefined();
    expect(validateHub("require_goal", { goal_id: "a" })).toContain("needs goal_id and requires");
  });

  it("run a plan for a goal, or for named playtests, as an agent's plan, and read the queue", async () => {
    const goal = await run("run_plan", { goal_id: "doors" });
    expect(planned[0]).toEqual({ body: { scope: { kind: "goal", goalId: "doors" } }, trigger: "agent" });
    expect(goal.plan).toMatchObject({ id: "plan_1", state: "queued", trigger: "agent", items: [{ playtest: `script:${DOOR}`, state: "queued" }] });
    expect(goal.queue).toMatchObject({ waiting: "Play In Editor is in use" });
    await run("run_plan", { playtests: [DOOR, `${DOOR}.DoorOpens`, "/Game/Tests/JumpCheck"], attended: true, replay: false });
    expect(planned[1].body).toEqual({
      scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }, { kind: "script", target: DOOR }, { kind: "script", target: "/Game/Tests/JumpCheck" }] },
      options: { attended: true, replay: false },
    });
    expect(await run("read_queue", {})).toMatchObject({ paused: false, waiting: "Play In Editor is in use", queued: [] });
    await expect(run("run_plan", {})).rejects.toThrow("exactly one of goal_id and playtests");
    await expect(run("run_plan", { goal_id: "a", playtests: [DOOR] })).rejects.toThrow("exactly one");
    await expect(run("run_plan", { playtests: ["Game/NoSlash"] })).rejects.toThrow("not a playtest asset path");
    expect(planned).toHaveLength(2);
  });

  it("inputs are checked before they run", () => {
    expect(validateHub("add_link", { url: ISSUE, goal_id: "a" })).toBeNull();
    expect(validateHub("add_link", { goal_id: "a" })).toContain("needs url");
    expect(validateHub("add_link", { url: ISSUE, goal_id: "a", check_id: "script:/Game/X" })).toContain("exactly one");
    expect(validateHub("read_goal", {})).toContain("needs id");
    expect(validateHub("nope", {})).toContain("unknown action");
  });
});
