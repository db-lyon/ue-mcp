// @session independent: Pure schedule rules: reading a schedule from a body, its next run, and what a due one does.
/**
 * Schedules: a plan run at a local time on chosen days of the week, such as
 * "nightly at 02:00". A schedule is a person's standing instruction, so it
 * runs under the Manual playtest-runs setting too. It never takes the editor
 * from anyone: when it is due and the hub cannot start it (paused or busy
 * queue, the editor held by anything the occupancy answer names, the consent
 * ledger included, and every other session quiet), it waits and says why
 * (Schedule.waiting), then gives up with the reason once CATCH_UP_MS has
 * passed. Nobody is there to judge unless the schedule says so
 * (options.attended), so its plans skip checks that need a person.
 * Schedules are kept with the queue (queue.ts).
 */
import { planOptionsFor, type Schedule, type ScheduleDraft } from "./contract.js";
import { bad, isObject } from "./util.js";
import { planOptionsOf, scopeOf } from "./scope.js";

const HHMM = /^([01]\d|2[0-3]):([0-5]\d)$/;
/** How long a due schedule waits for the editor before it gives up; a due run found later than this (the daemon was not running) is skipped. */
export const CATCH_UP_MS = 60 * 60_000;

/** The next time after `after` that a schedule is due, in local time; null when it names no day. */
export function nextRunAt(s: Pick<Schedule, "at" | "days">, after: Date): Date | null {
  const m = HHMM.exec(s.at);
  if (!m || s.days.length === 0) return null;
  const [h, min] = [Number(m[1]), Number(m[2])];
  for (let d = 0; d <= 7; d++) {
    const t = new Date(after.getFullYear(), after.getMonth(), after.getDate() + d, h, min, 0, 0);
    if (t.getTime() > after.getTime() && s.days.includes(t.getDay())) return t;
  }
  return null;
}

function daysOf(v: unknown): number[] {
  if (!Array.isArray(v) || v.length === 0 || v.some((d) => !Number.isInteger(d) || d < 0 || d > 6)) throw bad("days must list the days it runs, 0 Sunday to 6 Saturday");
  return [...new Set(v as number[])].sort((a, b) => a - b);
}

/** A whole schedule draft from a body. Throws a 400 for anything wrong. */
export function scheduleDraftOf(body: unknown): ScheduleDraft {
  if (!isObject(body)) throw bad("body must be a schedule: { title, scope, at, days, options?, enabled? }");
  if (typeof body.title !== "string" || !body.title.trim()) throw bad("title must be a non-empty string");
  if (typeof body.at !== "string" || !HHMM.test(body.at)) throw bad("at must be a local time, HH:MM");
  if (body.enabled !== undefined && typeof body.enabled !== "boolean") throw bad("enabled must be a boolean");
  return {
    title: body.title.trim().slice(0, 200),
    scope: scopeOf(body.scope),
    options: planOptionsOf(body.options, planOptionsFor("schedule")),
    at: body.at,
    days: daysOf(body.days),
    enabled: body.enabled !== false,
  };
}

/** A schedule with the fields an update gives changed. Throws a 400 for anything wrong. */
export function scheduleUpdated(had: Schedule, body: unknown): Schedule {
  if (!isObject(body)) throw bad("body must be the fields to change");
  const next: Schedule = { ...had };
  if (body.title !== undefined) {
    if (typeof body.title !== "string" || !body.title.trim()) throw bad("title must be a non-empty string");
    next.title = body.title.trim().slice(0, 200);
  }
  if (body.at !== undefined) {
    if (typeof body.at !== "string" || !HHMM.test(body.at)) throw bad("at must be a local time, HH:MM");
    next.at = body.at;
  }
  if (body.days !== undefined) next.days = daysOf(body.days);
  if (body.scope !== undefined) next.scope = scopeOf(body.scope);
  if (body.options !== undefined) next.options = planOptionsOf(body.options, had.options);
  if (body.enabled !== undefined) {
    if (typeof body.enabled !== "boolean") throw bad("enabled must be a boolean");
    next.enabled = body.enabled;
  }
  return next;
}

/** The schedule's next run worked out again from now: none while it is off. A change no longer waits. */
export function withNext(s: Schedule, now: Date): Schedule {
  const next = s.enabled ? nextRunAt(s, now) : null;
  return { ...s, waiting: null, nextRunAt: next ? next.toISOString() : null };
}

/** What a due schedule does on a tick: run, wait with a reason, give up with a reason, or nothing yet. */
export type ScheduleTurn = { run: true } | { wait: string } | { skip: string } | null;

/** `why` answers why the hub cannot start the schedule's plan now, or null when it can. */
export function scheduleTurn(s: Schedule, now: Date, why: () => string | null): ScheduleTurn {
  if (!s.enabled || !s.nextRunAt) return null;
  const due = Date.parse(s.nextRunAt);
  if (!Number.isFinite(due) || due > now.getTime()) return null;
  const late = now.getTime() - due > CATCH_UP_MS;
  // Found late without having waited: the daemon was not running at the time.
  if (late && !s.waiting) return { skip: "the daemon was not running at the time" };
  const blocked = why();
  if (!blocked) return { run: true };
  return late ? { skip: `waited an hour while ${sentenceTail(blocked)}` } : { wait: blocked };
}

/** A reason read mid-sentence: "The editor is not connected" becomes "the editor is not connected"; a name keeps its capital. */
function sentenceTail(reason: string): string {
  return /^(The|A|An|Another|Someone|Other)\b/.test(reason) ? reason[0].toLowerCase() + reason.slice(1) : reason;
}
