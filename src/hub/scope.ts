// @session independent: Pure reading of a plan's scope and options from a body.
/** Reading a plan's scope and options from a body. */
import { GOAL_ID, QA_VERDICT_WAIT_CHOICES, qaVerdictWaitValid, type PlanOptions, type PlanScope } from "./contract.js";
import { bad, checkRefOf, isObject } from "./util.js";

const SCOPE_HELP = "scope must be { kind: goal | track | scene, goalId, from? } | { kind: slot, sceneId, trackId } | { kind: arrangement, from?, arrangementId? } | { kind: checks, checks } | { kind: failing | stale, goalId? } | { kind: all }";

export function scopeOf(v: unknown): PlanScope {
  if (!isObject(v)) throw bad(SCOPE_HELP);
  const goalId = (required: boolean): string | undefined => {
    if (v.goalId === undefined && !required) return undefined;
    if (typeof v.goalId !== "string" || !GOAL_ID.test(v.goalId)) throw bad("goalId must be a track's id");
    return v.goalId;
  };
  const from = (): { from?: number } => {
    if (v.from === undefined || v.from === 0) return {};
    if (typeof v.from !== "number" || !Number.isFinite(v.from) || v.from < 0) throw bad("from must be a frame on the timeline, 0 or more");
    return { from: Math.round(v.from * 1000) / 1000 };
  };
  switch (v.kind) {
    case "goal":
      return { kind: "goal", goalId: goalId(true)! };
    case "track":
    case "scene":
      return { kind: v.kind, goalId: goalId(true)!, ...from() };
    case "slot":
      if (typeof v.sceneId !== "string" || !GOAL_ID.test(v.sceneId) || typeof v.trackId !== "string" || !GOAL_ID.test(v.trackId)) throw bad("a slot scope is { kind: slot, sceneId, trackId }");
      return { kind: "slot", sceneId: v.sceneId, trackId: v.trackId };
    case "arrangement": {
      if (v.arrangementId !== undefined && v.arrangementId !== null && (typeof v.arrangementId !== "string" || !GOAL_ID.test(v.arrangementId))) throw bad("arrangementId must be an arrangement's id");
      return { kind: "arrangement", ...from(), ...(typeof v.arrangementId === "string" ? { arrangementId: v.arrangementId } : {}) };
    }
    case "failing":
    case "stale": {
      const g = goalId(false);
      return g ? { kind: v.kind, goalId: g } : { kind: v.kind };
    }
    case "all":
      return { kind: "all" };
    case "checks": {
      if (!Array.isArray(v.checks) || v.checks.length === 0) throw bad("checks must list at least one { kind, target }");
      const checks = v.checks.map((c, i) => {
        const ref = checkRefOf(c);
        if (!ref) throw bad(`check ${i + 1} must be { kind, target }, a script's target an asset path`);
        return ref;
      });
      return { kind: "checks", checks };
    }
    default:
      throw bad(SCOPE_HELP);
  }
}

/** The frame a scope plays its timelines from: 0 unless a track, scene or arrangement scope names one. */
export const fromOf = (scope: PlanScope): number => ("from" in scope && typeof scope.from === "number" ? scope.from : 0);

export function planOptionsOf(v: unknown, base: PlanOptions): PlanOptions {
  if (v === undefined) return { ...base };
  if (!isObject(v)) throw bad("options must be { attended?, verdictWaitMinutes?, stopOnFailure?, replay? }");
  const out = { ...base };
  if (v.attended !== undefined) {
    if (typeof v.attended !== "boolean") throw bad("options.attended must be a boolean");
    out.attended = v.attended;
  }
  if (v.verdictWaitMinutes === null) delete out.verdictWaitMinutes;
  else if (v.verdictWaitMinutes !== undefined) {
    if (!qaVerdictWaitValid(v.verdictWaitMinutes)) throw bad(`options.verdictWaitMinutes must be one of ${QA_VERDICT_WAIT_CHOICES.join(", ")}`);
    out.verdictWaitMinutes = v.verdictWaitMinutes;
  }
  if (v.stopOnFailure !== undefined) {
    if (typeof v.stopOnFailure !== "boolean") throw bad("options.stopOnFailure must be a boolean");
    out.stopOnFailure = v.stopOnFailure;
  }
  if (v.replay !== undefined) {
    if (typeof v.replay !== "boolean") throw bad("options.replay must be a boolean");
    out.replay = v.replay;
  }
  return out;
}
