// @session independent: The hub contract's attention rule: a pure function over the hub's answers. Imports only sibling contract files.
/**
 * The Playtest Hub's Needs attention list: what waits for a person and does
 * not depend on a game playing now, worked out from what the hub already
 * knows, never stored. Blocked loops come from the playtest runs;
 * regressions from the checks' history; sign-offs from the goals' status;
 * failed plans from the queue. A playtest missing on this checkout is not
 * here: another branch may have it. Ids stay the same while the thing
 * waits, so a client can keep its place. The agent serves it, and the hub
 * demo answers with the same rule.
 */
import {
  ATTENTION_ORDER,
  checkId,
  needsSignOff,
  type AttentionItem,
  type CheckState,
  type GoalState,
  type PlanRun,
} from "./hub.js";
import { qaScriptKey, type QaRun } from "./hub-qa.js";

/** How long a failed plan stays on the list. */
export const PLAN_FAILED_DAYS = 3;

export interface AttentionInput {
  runs: QaRun[];
  goals: GoalState[];
  checks: CheckState[];
  plans: PlanRun[];
  now: number;
}

export function buildAttention(i: AttentionInput): AttentionItem[] {
  const goalsOf = new Map(i.checks.map((c) => [c.id, c.goalIds]));
  const scriptGoals = (script: string) => goalsOf.get(checkId({ kind: "script", target: qaScriptKey(script) })) ?? [];
  const out: AttentionItem[] = [];

  // A loop is carried by every card its attempts took: the newest carrier speaks for it.
  const loops = new Map<string, QaRun>();
  for (const run of i.runs) {
    const loop = run.assignment?.loop;
    if (!loop || loop.state !== "needs_you" || run.deleted) continue;
    const had = loops.get(loop.id);
    if (!had || had.seenAt <= run.seenAt) loops.set(loop.id, run);
  }
  for (const [loopId, run] of loops) {
    const loop = run.assignment!.loop!;
    out.push({
      id: `loop_blocked:${loopId}`,
      kind: "loop_blocked",
      at: loop.endedAt ?? loop.startedAt,
      title: run.scriptName,
      detail: loop.blocker?.reason ?? "The fix loop stopped and needs you.",
      goalIds: scriptGoals(run.script),
      runId: run.runId,
      ...(loop.blocker?.step !== undefined ? { step: loop.blocker.step } : {}),
      checkId: checkId({ kind: "script", target: qaScriptKey(run.script) }),
      ...(run.assignment?.sessionId ? { sessionId: run.assignment.sessionId } : {}),
    });
  }

  for (const g of i.goals) {
    const goal = g.goal;
    if (goal.archived || !needsSignOff(goal) || g.status !== "ready_to_sign_off") continue;
    // Waiting since its newest result came in.
    const at = g.checks.map((c) => c.last?.endedAt ?? c.last?.startedAt ?? "").sort().pop() || g.signOff?.record.at || new Date(i.now).toISOString();
    const detail = g.signOff ? "Signed off on an earlier revision. Every playtest passes, so it can be signed off again." : "Every playtest passes. The goal waits for its sign-off.";
    out.push({ id: `sign_off:${goal.id}`, kind: "sign_off", at, title: goal.title, detail, goalIds: [goal.id] });
  }

  for (const c of i.checks) {
    // A regression is a fail after a pass on something else; on the same provenance it is the check's flakiness, shown as Failing.
    if (c.change?.kind === "regressed" && c.last?.runId === c.change.runId) {
      out.push({
        id: `regression:${c.id}:${c.change.runId}`,
        kind: "regression",
        at: c.change.at,
        title: c.name,
        detail: "Failed after passing",
        goalIds: c.goalIds,
        runId: c.change.runId,
        checkId: c.id,
      });
    }
  }

  for (const p of i.plans) {
    if (p.state !== "failed" || !p.endedAt || i.now - Date.parse(p.endedAt) > PLAN_FAILED_DAYS * 86_400_000) continue;
    const bad = p.items.find((x) => x.state === "failed" || x.state === "error");
    out.push({
      id: `plan_failed:${p.id}`,
      kind: "plan_failed",
      at: p.endedAt,
      title: p.title,
      detail: bad ? `${bad.name}: ${bad.reason ?? bad.state}` : "Every playtest in it failed",
      goalIds: p.scope.kind === "goal" ? [p.scope.goalId] : "goalId" in p.scope && p.scope.goalId ? [p.scope.goalId] : [],
      planRunId: p.id,
      ...(bad?.runId ? { runId: bad.runId } : {}),
    });
  }

  const rank = (k: AttentionItem["kind"]) => ATTENTION_ORDER.indexOf(k);
  return out.sort((a, b) => rank(a.kind) - rank(b.kind) || (a.at < b.at ? -1 : a.at > b.at ? 1 : a.id < b.id ? -1 : 1));
}
