// @session independent: Pure functions that turn the hub category's inputs into calls on a hub handed to them.
/**
 * The hub category's actions over a Hub: list goals with their status, read a
 * goal or a playtest, create and update goals, put goals under milestones,
 * link goals, playtests and runs to tracker items, and run a plan. Every
 * change goes to the project's goal store (goals.ts), never to a project
 * file. src/tools/hub.ts declares them as the `hub` MCP category.
 *
 * Each function returns the answer as plain data and throws a HubError (or
 * Error) with the reason when it cannot, so a category handler lets the
 * dispatcher turn it into an error result.
 */
import { checkId, type PlanRun, type PlanScope, type QueueState, type SetView, type TrackerLink, TRACKER_LINKS_MAX } from "./contract.js";
import { FindingError } from "./set-service.js";
import type { Hub } from "./hub.js";
import { HubError, bad, checkRefOf, linkOf, notFound } from "./util.js";

export const HUB_ACTIONS = [
  "list_goals",
  "read_goal",
  "read_check",
  "create_goal",
  "update_goal",
  "add_link",
  "remove_link",
  "require_goal",
  "unrequire_goal",
  "run_plan",
  "read_queue",
  "read_set",
  "edit_set",
  "run",
  "read_run",
] as const;
export type HubAction = (typeof HUB_ACTIONS)[number];

/** What the actions need of the hub. */
export type HubToolTarget = Pick<Hub, "goals" | "overview" | "goal" | "check" | "addRunLink" | "removeRunLink" | "updateCheckMeta"> & Partial<Pick<Hub, "plan" | "queueView" | "set" | "runSet" | "run">>;

const str = (v: unknown): v is string => typeof v === "string" && v.trim() !== "";

/** Check a call's input; null when it is fine. */
export function validateHub(action: string, i: Record<string, unknown>): string | null {
  switch (action) {
    case "list_goals":
      return i.include_archived === undefined || typeof i.include_archived === "boolean" ? null : "include_archived must be a boolean";
    case "read_goal":
    case "update_goal":
      return str(i.id) ? null : `${action} needs id`;
    case "read_check":
      if (!str(i.check_id)) return "read_check needs check_id";
      return i.limit === undefined || Number.isInteger(i.limit) ? null : "limit must be a whole number";
    case "create_goal":
      return str(i.title) ? null : "create_goal needs title";
    case "require_goal":
    case "unrequire_goal":
      if (!str(i.goal_id) || !str(i.requires)) return `${action} needs goal_id and requires`;
      return i.required === undefined || (action === "require_goal" && typeof i.required === "boolean") ? null : "required must be a boolean, given only to require_goal";
    case "add_link":
    case "remove_link": {
      if (!str(i.url)) return `${action} needs url`;
      const given = ["goal_id", "check_id", "run_id"].filter((k) => i[k] !== undefined);
      for (const k of given) if (!str(i[k])) return `${k} must be text`;
      return given.length === 1 ? null : "say what to link: exactly one of goal_id and check_id and run_id";
    }
    case "run_plan": {
      const goal = i.goal_id !== undefined;
      const tests = i.playtests !== undefined;
      if (goal === tests) return "say what to run: exactly one of goal_id and playtests";
      if (goal && !str(i.goal_id)) return "goal_id must be text";
      if (tests && (!Array.isArray(i.playtests) || i.playtests.length === 0 || !i.playtests.every(str))) return "playtests is a list of clip asset paths, such as /Game/Tests/DoorOpens";
      for (const k of ["attended", "replay"]) if (i[k] !== undefined && typeof i[k] !== "boolean") return `${k} must be a boolean`;
      return null;
    }
    case "read_queue":
    case "read_set":
      return null;
    case "edit_set":
      return str(i.ops) || (Array.isArray(i.ops) && i.ops.length > 0) ? null : "edit_set needs ops: op lines as text (one per line), or a list of op objects";
    case "run":
      return str(i.scope) ? null : "run needs scope: track <track>, scene <scene>, or arrangement";
    case "read_run":
      return str(i.run_id) ? null : "read_run needs run_id";
  }
  return `unknown action ${action}`;
}

/** Links with one added (replacing a link to the same URL) or removed. */
function changedLinks(links: readonly TrackerLink[] | undefined, action: string, i: Record<string, unknown>): TrackerLink[] {
  const have = links ?? [];
  if (action === "remove_link") {
    if (!have.some((l) => l.url === i.url)) throw notFound(`there is no link to ${String(i.url)}`);
    return have.filter((l) => l.url !== i.url);
  }
  const link = linkOf({ url: i.url, title: i.title, rel: i.rel });
  const rest = have.filter((l) => l.url !== link.url);
  if (rest.length >= TRACKER_LINKS_MAX) throw bad(`at most ${TRACKER_LINKS_MAX} links`);
  return [...rest, link];
}

/** A plan's scope from a run_plan input: a goal, or playtests by asset path. */
function scopeOfInput(i: Record<string, unknown>): PlanScope {
  if (str(i.goal_id)) return { kind: "goal", goalId: i.goal_id };
  const checks = (i.playtests as string[]).map((p) => {
    const ref = checkRefOf({ kind: "script", target: p });
    if (!ref) throw bad(`${p} is not a clip asset path: it starts with /, such as /Game/Tests/DoorOpens`);
    return ref;
  });
  return { kind: "checks", checks };
}

/** The set as an agent reads it: the text form, findings and each track's and scene's status. */
function setAnswer(v: SetView): Record<string, unknown> {
  return {
    text: v.text,
    status: Object.fromEntries([...v.tracks, ...v.scenes].map((x) => [x.id, x.status])),
    ...(v.findings.length ? { findings: v.findings.map((f) => `${f.blocks ? "error" : "warning"} ${f.id}: ${f.message}${f.candidates?.length ? ` (candidates: ${f.candidates.join(", ")})` : ""}`) } : {}),
  };
}

/** A run scope from text: "track <track>", "scene <scene>", "arrangement" or "slot <scene> <track>", by id or exact name. */
function setScopeOf(text: string, v: SetView): PlanScope {
  const [kind, ...rest] = text.trim().split(/\s+/);
  const name = rest.join(" ").replace(/^"|"$/g, "");
  const find = (list: Array<{ id: string; title: string }>, n: string) => list.find((x) => x.id === n) ?? list.find((x) => x.title.toLowerCase() === n.toLowerCase());
  if (kind === "arrangement") return { kind: "arrangement" };
  if (kind === "track" || kind === "scene") {
    const list = kind === "track" ? v.tracks : v.scenes;
    const x = find(list, name);
    if (!x) throw bad(`no ${kind} ${name} (${kind}s: ${list.map((y) => `${y.id} "${y.title}"`).join(", ") || "none"})`);
    return { kind, goalId: x.id };
  }
  if (kind === "slot") {
    const s = find(v.scenes, rest[0] ?? "");
    const t = find(v.tracks, rest[1] ?? "");
    if (!s || !t) throw bad("slot needs a scene and a track: slot <scene> <track>");
    return { kind: "slot", sceneId: s.id, trackId: t.id };
  }
  throw bad("scope is track <track>, scene <scene> or arrangement");
}

/** What a queued plan answers: its id and items, and the queue it joined. */
function planAnswer(plan: PlanRun, queue: QueueState | undefined): Record<string, unknown> {
  return {
    plan: {
      id: plan.id,
      title: plan.title,
      state: plan.state,
      trigger: plan.trigger,
      items: plan.items.map((x) => ({ playtest: checkId(x.check), name: x.name, state: x.state })),
    },
    ...(queue ? { queue: queueAnswer(queue) } : {}),
    note: "The plan waits for the editor lease: it starts when nothing else holds the editor. Read it again with read_queue.",
  };
}

function queueAnswer(q: QueueState): Record<string, unknown> {
  const plan = (p: PlanRun) => ({ id: p.id, title: p.title, state: p.state, items: p.items.map((x) => ({ playtest: checkId(x.check), name: x.name, state: x.state, ...(x.runId ? { runId: x.runId } : {}), ...(x.reason ? { reason: x.reason } : {}) })) });
  return {
    paused: q.paused,
    ...(q.waiting ? { waiting: q.waiting } : {}),
    ...(q.current ? { current: plan(q.current) } : {}),
    queued: q.queued.map(plan),
    recent: q.recent.slice(0, 5).map(plan),
  };
}

/** Run one hub action. */
export async function runHubAction(hub: HubToolTarget, action: string, input: Record<string, unknown>): Promise<unknown> {
  const invalid = validateHub(action, input);
  if (invalid) throw bad(invalid);
  const g = hub.goals;
  switch (action) {
    case "list_goals": {
      const o = await hub.overview();
      const goals = o.goals.filter((s) => input.include_archived === true || !s.goal.archived).map((s) => ({
        id: s.goal.id,
        kind: s.goal.kind,
        title: s.goal.title,
        status: s.status,
        progress: `${s.progress.met}/${s.progress.total}`,
        ...(s.parents.length ? { partOf: s.parents } : {}),
        ...(s.requires.length ? { requires: s.requires.map((r) => r.goal) } : {}),
        ...(s.unused ? { unused: true } : {}),
        ...(s.goal.owner ? { owner: s.goal.owner } : {}),
        ...(s.goal.priority ? { priority: s.goal.priority } : {}),
        ...(s.goal.due ? { due: s.goal.due } : {}),
        checks: s.checks.length,
        ...(s.goal.links?.length ? { links: s.goal.links } : {}),
        ...(s.goal.archived ? { archived: true } : {}),
      }));
      return { goals, ...(o.warnings.length ? { warnings: o.warnings } : {}) };
    }
    case "read_goal": {
      const s = await hub.goal(String(input.id));
      return {
        status: s.status,
        progress: s.progress,
        goal: s.goal,
        checks: s.checks.map((c) => ({
          id: c.id,
          name: c.name,
          health: c.health,
          ...(c.check.required === false ? { required: false } : {}),
          ...(c.renamedFrom ? { movedFrom: c.renamedFrom } : {}),
          ...(c.last ? { last: { runId: c.last.runId, outcome: c.last.outcome, startedAt: c.last.startedAt } } : {}),
          ...(c.links?.length ? { links: c.links } : {}),
        })),
        ...(s.requires.length ? { requires: s.requires } : {}),
        ...(s.parents.length ? { partOf: s.parents } : {}),
        ...(s.unused ? { unused: "no scene or track requires it" } : {}),
      };
    }
    case "read_check": {
      const limit = Math.min(50, Math.max(1, typeof input.limit === "number" ? input.limit : 10));
      const d = await hub.check(String(input.check_id), { limit });
      return {
        id: d.state.id,
        name: d.state.name,
        health: d.state.health,
        goals: d.state.goalIds,
        ...(d.state.links?.length ? { links: d.state.links } : {}),
        runs: d.runs.map((r) => ({ runId: r.runId, outcome: r.outcome, startedAt: r.startedAt, ...(r.failedSteps?.length ? { failedSteps: r.failedSteps.map((s) => s + 1) } : {}), ...(r.links?.length ? { links: r.links } : {}) })),
        more: d.more,
      };
    }
    case "add_link":
    case "remove_link": {
      if (str(input.run_id)) {
        const r = action === "add_link" ? hub.addRunLink(input.run_id, { link: { url: input.url, title: input.title, rel: input.rel } }) : hub.removeRunLink(input.run_id, input.url);
        return { runId: r.runId, links: r.links ?? [] };
      }
      if (str(input.check_id)) {
        const id = input.check_id;
        const had = (await hub.check(id, { limit: 1 })).state.links;
        const meta = hub.updateCheckMeta(id, { links: changedLinks(had, action, input) });
        return { checkId: id, links: meta.links ?? [] };
      }
      const goalId = String(input.goal_id);
      const goal = g.get(goalId);
      if (!goal) throw notFound(`no track ${goalId}`);
      const saved = g.update(goalId, { links: changedLinks(goal.links, action, input) });
      return { goalId, links: saved.links ?? [] };
    }
    case "create_goal":
      return { created: g.create(input) };
    case "update_goal": {
      const { id, ...u } = input;
      return { saved: g.update(String(id), u) };
    }
    case "require_goal": {
      const body = { goal: input.requires, ...(input.required !== undefined ? { required: input.required } : {}) };
      return { saved: g.addRequire(String(input.goal_id), body) };
    }
    case "unrequire_goal":
      return { saved: g.removeRequire(String(input.goal_id), String(input.requires)) };
    case "run_plan": {
      if (!hub.plan) throw new HubError(409, "this hub cannot run plans");
      const options = { ...(input.attended !== undefined ? { attended: input.attended } : {}), ...(input.replay !== undefined ? { replay: input.replay } : {}) };
      const plan = await hub.plan({ scope: scopeOfInput(input), ...(Object.keys(options).length ? { options } : {}) }, "agent");
      return planAnswer(plan, hub.queueView?.());
    }
    case "read_queue":
      if (!hub.queueView) throw new HubError(409, "this hub cannot run plans");
      return queueAnswer(hub.queueView());
    case "read_set": {
      if (!hub.set) throw new HubError(409, "this hub has no set");
      const v = await hub.set.view();
      return setAnswer(v);
    }
    case "edit_set": {
      if (!hub.set) throw new HubError(409, "this hub has no set");
      try {
        const v = await hub.set.edit({ ops: input.ops });
        return { changed: v.changed, ...setAnswer(v) };
      } catch (e) {
        if (e instanceof FindingError) throw new HubError(400, `${e.finding.message}${e.finding.candidates?.length ? ` (did you mean: ${e.finding.candidates.join(", ")})` : ""}`);
        throw e;
      }
    }
    case "run": {
      if (!hub.runSet || !hub.set) throw new HubError(409, "this hub cannot run plans");
      const view = await hub.set.view();
      const scope = setScopeOf(String(input.scope), view);
      const { plan } = await hub.runSet({ scope, ...(input.attended !== undefined ? { options: { attended: input.attended } } : {}) }, "agent");
      return planAnswer(plan, hub.queueView?.());
    }
    case "read_run": {
      if (!hub.run) throw new HubError(409, "this hub has no runs");
      const d = hub.run(String(input.run_id));
      const a = d.arrangement;
      if (!a) return { runId: d.record.runId, outcome: d.record.outcome, steps: d.steps.map((s) => `${s.index + 1} ${s.status} ${s.instruction}`) };
      const sec = (f: number) => `${(f / a.rate).toFixed(1)}s`;
      return {
        runId: d.record.runId,
        outcome: d.record.outcome,
        ...(d.run?.stopReason ? { stopReason: d.run.stopReason } : {}),
        tracks: a.tracks?.map((t) => `${t.goalId} ${t.status} player=${t.player}`),
        instances: a.instances.map((i) => `${i.goalId}/${i.id} ${i.status} ${sec(i.start)}-${sec(i.end)} ${i.name} checks ${i.checks.passed}/${i.checks.total} passed${i.checks.failed ? `, ${i.checks.failed} failed` : ""}${i.reason ? ` (${i.reason})` : ""}`),
      };
    }
  }
  throw bad(`unknown action ${action}`);
}
