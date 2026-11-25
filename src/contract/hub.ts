// @session independent: The hub contract: types, constants and pure rules over their arguments. Imports only sibling contract files.
/**
 * The Playtest Hub: goals a game must meet, the playtests that prove them,
 * the runs that are their evidence, and the queue that runs them on the
 * editor.
 *
 * Goals and milestones are tracked like issues: in one place, not versioned
 * per branch. They live in the project's goal store, outside the game repo
 * (HubRecord below; on this computer under the extension's data directory),
 * with their graph, owner, priority and due date, what each playtest says
 * about itself, sign-offs and the health rule. The repo holds only the
 * playtest scripts, which goals name by asset path; a script missing on the
 * current checkout shows as missing and the goal never forks. Run history
 * and the queue are per machine too. Status is always computed, never
 * stored: these rules are the one place it is worked out, for the agent and
 * both clients.
 *
 * In code a playtest is a "check", the general word, kept for when a second
 * kind runs; people only ever read "playtest".
 *
 * Routes are under /v1/hub on the daemon (see HUB_ROUTES below); every change
 * publishes `hub.changed` with a HubChanged body.
 */
import { qaEnded, type QaRun, type QaStep, type QaStepStatus, type QaSummary } from "./hub-qa.js";
import type { Arrangement, ArrangementFinding, InstanceLayout } from "./hub-arrangement.js";
import type { ArrangementRun } from "./hub-compose.js";
import type { SetColor, Slot } from "./hub-set.js";

export const HUB_SCHEMA = 1 as const;

// ---------- Goals and checks ----------

/**
 * What a check is: a ue-mcp PIE test script, a playtest. The only kind the
 * schema has; a kind returns with its runner, as a schema version. Goals
 * imported with another kind keep those entries apart (Goal.unsupported).
 *
 * "track", "scene", "slot" and "arrangement" name a run of a track's
 * timeline, a scene's row, one slot or every timeline (#2113): a plan item
 * and a run record carry them, a goal's own checks never do. Their target is
 * the goal's id, "<scene id>/<track id>" for a slot, "arrangement" for all.
 */
export type CheckKind = "script" | "track" | "scene" | "slot" | "arrangement";

// ---------- Tracker links ----------

/**
 * A link from a goal, a check or a run to an item elsewhere: an issue, a
 * ticket, a card, a document. Any tracker: the hub keeps the URL and never
 * contacts it, so there is no provider list.
 */
export interface TrackerLink {
  url: string;
  title?: string;
  /** How the item relates, free text. TRACKER_LINK_RELS are the usual ones, offered by clients and never enforced. */
  rel?: string;
}

/** Relationships clients suggest for a link. Any other text is as valid. */
export const TRACKER_LINK_RELS = ["counterpart", "related", "filed-from-run", "blocks"] as const;
/** The most links one goal, check or run keeps. */
export const TRACKER_LINKS_MAX = 20;
export const TRACKER_URL_MAX = 2048;
export const TRACKER_TITLE_MAX = 200;
export const TRACKER_REL_MAX = 40;
/** Schemes a link may never use: they run code or read local data where it opens. */
export const TRACKER_BLOCKED_SCHEMES: readonly string[] = ["javascript", "data", "vbscript", "file", "blob", "about"];
const URL_SCHEME = /^([a-z][a-z0-9+.-]*):/i;

/** Why a URL cannot be a link, or null when it can: http, https, mailto, or an app's own scheme such as jira://. */
export function trackerUrlProblem(url: unknown): string | null {
  if (typeof url !== "string" || !url.trim()) return "a link needs a url";
  const u = url.trim();
  if (u.length > TRACKER_URL_MAX) return `a link's url is at most ${TRACKER_URL_MAX} characters`;
  if (/\s/.test(u)) return "a link's url has no spaces";
  const scheme = URL_SCHEME.exec(u)?.[1]?.toLowerCase();
  if (!scheme) return "a link's url starts with its scheme, such as https://";
  if (TRACKER_BLOCKED_SCHEMES.includes(scheme)) return `${scheme}: links are not allowed`;
  if ((scheme === "http" || scheme === "https") && !/^https?:\/\/[^/?#\s]+/i.test(u)) return "a web link needs a host, such as https://example.com/issue/1";
  if (u.length <= scheme.length + 1) return "a link's url has nothing after its scheme";
  return null;
}

/** A link from a body or a file, normalized; or why it is not one. */
export function trackerLinkOf(v: unknown): { link: TrackerLink } | { problem: string } {
  if (!v || typeof v !== "object" || Array.isArray(v)) return { problem: "a link is { url, title?, rel? }" };
  const o = v as Record<string, unknown>;
  const problem = trackerUrlProblem(o.url);
  if (problem) return { problem };
  if (o.title !== undefined && o.title !== null && typeof o.title !== "string") return { problem: "a link's title is text" };
  if (o.rel !== undefined && o.rel !== null && typeof o.rel !== "string") return { problem: "a link's rel is text" };
  const title = typeof o.title === "string" ? o.title.trim().slice(0, TRACKER_TITLE_MAX) : "";
  const rel = typeof o.rel === "string" ? o.rel.trim().slice(0, TRACKER_REL_MAX) : "";
  return { link: { url: (o.url as string).trim(), ...(title ? { title } : {}), ...(rel ? { rel } : {}) } };
}

/** Links without a second one to the same URL, the first kept, at most TRACKER_LINKS_MAX. */
export function uniqueLinks(links: readonly TrackerLink[]): TrackerLink[] {
  const seen = new Set<string>();
  return links.filter((l) => {
    if (seen.has(l.url)) return false;
    seen.add(l.url);
    return true;
  }).slice(0, TRACKER_LINKS_MAX);
}

/**
 * What a link is shown as when it has no title: the host of a web link
 * (without www.), the address of a mail link, the scheme of an app link
 * ("jira"). Empty when the URL does not parse.
 */
export function linkHost(url: string): string {
  const u = url.trim();
  const scheme = URL_SCHEME.exec(u)?.[1]?.toLowerCase();
  if (!scheme) return "";
  if (scheme === "mailto") {
    const to = u.slice("mailto:".length).split("?")[0] ?? "";
    try {
      return decodeURIComponent(to);
    } catch {
      return to;
    }
  }
  if (scheme === "http" || scheme === "https") {
    const m = /^https?:\/\/(?:[^@/?#]*@)?(\[[^\]]+\]|[^:/?#]+)/i.exec(u);
    return m ? m[1].toLowerCase().replace(/^www\./, "") : "";
  }
  return scheme;
}

/** A check as a goal lists it: which one, and whether the goal needs it. Nothing about the check itself (that is its CheckMeta). */
export interface CheckRef {
  kind: CheckKind;
  /** The script's asset path without the object name, such as /Game/Tests/PT_Jump. */
  target: string;
  /** A goal is met only when its required checks pass. Default true. */
  required?: boolean;
}

/** PUT /hub/goals/:id/checks/:checkId: what the goal says of the check. */
export interface GoalCheckUpdate {
  required?: boolean;
}

/**
 * A check a goal was imported with whose kind this ue-mcp cannot run (a UAT
 * scenario, an automation filter, a manual checklist item). Kept as it came,
 * shown as unsupported, and never counted toward the goal.
 */
export interface UnsupportedCheck {
  kind: string;
  target: string;
  required?: boolean;
}

/** What a check says about itself, wherever it is listed: its one home, one record in the goal store. */
export interface CheckMeta {
  /** A display name; the script's own name otherwise. */
  title?: string;
  /** Tracker items the check refers to. */
  links?: TrackerLink[];
}

/** PUT /hub/checks/:checkId. Given fields change; null clears the title, an empty list the links. */
export interface CheckMetaUpdate {
  title?: string | null;
  links?: TrackerLink[];
}

/** A check's CheckMeta as the goal store keeps it: it names its check. */
export interface CheckMetaRecord extends CheckMeta {
  check: Pick<CheckRef, "kind" | "target">;
}

/** A check's identity across goals and runs: "<kind>:<target>". */
export const checkId = (c: Pick<CheckRef, "kind" | "target">): string => `${c.kind}:${c.target}`;
export function parseCheckId(id: string): Pick<CheckRef, "kind" | "target"> | null {
  const i = id.indexOf(":");
  if (i <= 0 || id.slice(0, i) !== "script") return null;
  const target = id.slice(i + 1);
  return target ? { kind: "script", target } : null;
}

export interface AcceptanceRule {
  /** How many consecutive passes of each required check count when the goal rolls up. 1 to 10, default 1. */
  stableRuns: number;
  /** A person signs the goal off before it is met. */
  signOff: boolean;
}
export const DEFAULT_ACCEPTANCE: AcceptanceRule = { stableRuns: 1, signOff: false };

/**
 * The project's one rule for a check's health, whichever goal lists it: when
 * a passing check is flaky or stale. Kept in the goal store, a project
 * setting.
 */
export interface HealthRule {
  /** A passing check not run for this many days is stale, where no dependency fingerprints say more. Null: never stale by age. */
  staleAfterDays: number | null;
  /** Flaky: mixed results among this many latest results on the same provenance. 2 to 20. */
  flakyWindow: number;
}
export const DEFAULT_HEALTH_RULE: HealthRule = { staleAfterDays: null, flakyWindow: 3 };
export const FLAKY_WINDOW_MIN = 2;
export const FLAKY_WINDOW_MAX = 20;
export const STALE_DAYS_MAX = 365;

/** Why a body cannot be a health rule, or null when it can. */
export function healthRuleProblem(v: unknown): string | null {
  if (!v || typeof v !== "object" || Array.isArray(v)) return "the rule is { staleAfterDays, flakyWindow }";
  const o = v as Record<string, unknown>;
  const d = o.staleAfterDays;
  if (d !== undefined && d !== null && !(typeof d === "number" && Number.isInteger(d) && d >= 1 && d <= STALE_DAYS_MAX)) return `staleAfterDays is a whole number of days from 1 to ${STALE_DAYS_MAX}, or null for never`;
  const w = o.flakyWindow;
  if (w !== undefined && !(typeof w === "number" && Number.isInteger(w) && w >= FLAKY_WINDOW_MIN && w <= FLAKY_WINDOW_MAX)) return `flakyWindow is a whole number from ${FLAKY_WINDOW_MIN} to ${FLAKY_WINDOW_MAX}`;
  return null;
}

export type GoalKind = "milestone" | "goal";
export type GoalPriority = "p0" | "p1" | "p2" | "p3";
export const GOAL_PRIORITIES: readonly GoalPriority[] = ["p0", "p1", "p2", "p3"];
/** A due date: YYYY-MM-DD. */
export const GOAL_DUE = /^\d{4}-\d{2}-\d{2}$/;

/**
 * A person's sign-off of a goal, a record in the goal store beside the goal.
 * It records what was signed so it can lapse (signOffLapse) when that no
 * longer describes the game.
 */
export interface SignOff {
  goalId: string;
  /** The signed-in ue-mcp account that gave it. */
  by: string;
  /** The paired browser or phone it came from, by the name it paired with; absent when given at this computer. */
  via?: string;
  at: string;
  note?: string;
  /** The revision the workspace was at (RunProvenance.revision's kind of value); null when the project has no source control the hub can read. */
  revision: string | null;
  /** The runs it was given against: each required check's results the acceptance rule looked at. */
  runIds: string[];
  /** The goal's requirements when it was given (requirementsKey). */
  requirements: string;
}

/** Why a sign-off no longer counts: a required check failed after it, the goal's requirements changed, or the workspace moved to another revision. */
export type SignOffLapse = "failed" | "requirements" | "revision";

/** A goal's sign-off as status reads it. A lapsed one is history: the goal waits for a new one. */
export interface SignOffState {
  record: SignOff;
  lapsed: SignOffLapse | null;
}

/**
 * One goal another goal requires, held by the goal that requires. A goal may
 * be required by any number of goals and milestones; the edge lives only on
 * the goal that requires, so adding a goal to a milestone changes only the
 * milestone.
 */
export interface GoalRequirement {
  goal: string;
  /** False: the requiring goal is met without this one. True when absent. */
  required?: boolean;
}

/** One goal, a record in the goal store. Ids are the title's slug and a random ending, so ids made on two computers never meet. */
export interface Goal {
  schema: typeof HUB_SCHEMA;
  id: string;
  /** A milestone is a goal with a label: any goal may require goals, milestones usually do. */
  kind: GoalKind;
  title: string;
  /** What "done" means, in the team's words. Markdown. */
  description?: string;
  /** Who owns the goal, as the team names people. */
  owner?: string | null;
  priority?: GoalPriority;
  /** YYYY-MM-DD. */
  due?: string | null;
  /** Tracker items this goal refers to: its issue, a design document. Optional, and never synced. */
  links?: TrackerLink[];
  acceptance: AcceptanceRule;
  /** The goals this one requires, in its own order. A goal is met only when its required goals are. */
  requires?: GoalRequirement[];
  checks: CheckRef[];
  /** Checks it was imported with that this ue-mcp cannot run. */
  unsupported?: UnsupportedCheck[];
  archived?: boolean;
  /**
   * A track's timeline (#2113): instances of clips placed on it. A goal that
   * has none is laid out from its checks once, when the store loads. `checks`
   * keeps the step-list playtests the instances use, for clients that read it.
   */
  arrangement?: Arrangement;
  /** The track's column, or the scene's row, in the set. */
  order?: number;
  color?: SetColor;
  /** A track: the player it drives, 0 (the editor's own) when absent, N for remote client N. */
  player?: number;
  /** A track: left out of every run. */
  mute?: boolean;
  /** A track: while any track is soloed, only soloed tracks play. */
  solo?: boolean;
  /** A scene: its clip slots, at most one per track. */
  slots?: Slot[];
}

/**
 * The goals that require this one, as a draft or update gives them: each
 * named goal gains the requirement, and on an update each goal that required
 * it and is not named loses it. Never stored on this goal.
 */
export interface GoalParents {
  parents?: string[];
}

/** POST /hub/goals. The agent fills id (the title's slug and a random suffix, unless one is given), schema and defaults. */
export type GoalDraft = Pick<Goal, "title"> & Partial<Omit<Goal, "schema">> & GoalParents;
/** PUT /hub/goals/:id. Given fields change; checks and requires, when given, replace the list; null clears owner, priority and due. */
export type GoalUpdate = Partial<Omit<Goal, "schema" | "id" | "priority">> & { priority?: GoalPriority | null } & GoalParents;

/** A goal id's readable part from a title: lowercase ASCII words joined by dashes, at most `max` characters. */
export function goalSlug(title: string, max = 64): string {
  const s = title
    .toLowerCase()
    .normalize("NFKD")
    .replace(/[̀-ͯ]/g, "")
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, max)
    .replace(/-+$/g, "");
  return s || "goal";
}
export const GOAL_ID = /^[a-z0-9][a-z0-9-]{0,63}$/;

// ---------- Runs (per machine) ----------

export type RunOutcome = "passed" | "failed" | "needs_you" | "running" | "incomplete" | "unjudged" | "stopped" | "error";
export type RunSource = "person" | "agent" | "plan" | "outside";

/**
 * Why a run happened, beside who started it (RunRecord.source): a run of
 * the check, a replay of an earlier run's recorded input, or an attempt in a
 * fix loop, which runs a project in the middle of being changed. Only "run"
 * counts toward health, acceptance and pass rate; the others stay in history.
 */
export type RunTrigger = "run" | "replay" | "loop";

/** The source control a project is under; "none" when the hub sees none. */
export type RunVcs = "git" | "perforce" | "plastic" | "svn" | "none";

/**
 * What a run tested, read when the hub first saw it running, with the fields
 * ue-mcp records itself taken from results.json once it ends, where present.
 * Null is always "not known", never a value: unknown matches nothing.
 */
export interface RunProvenance {
  /** The project's source control, null when it could not be read. */
  vcs: RunVcs | null;
  /** The commit, changelist, revision or changeset the workspace was at. */
  revision: string | null;
  /** Files changed or opened in source control when the run started: 0 is a clean workspace. */
  dirty: number | null;
  /** The script's version: "sha256:<hex>" of its definition as pie(script_read) gave it, or "ue-mcp:<hash>" when results.json carries ue-mcp's own. */
  scriptHash: string | null;
  /** The engine version: major.minor from the project file, or ue-mcp's full version when results.json has it. */
  engine: string | null;
  /** The players the client set PIE to before the run, or ue-mcp's record of them. Null: PIE ran as it was left. */
  players: { count: number; netMode: "standalone" | "listen" | "client" } | null;
  /** Fingerprints of the assets the script reaches, by package path, when results.json has them. */
  dependencies?: Record<string, string>;
}

/** Where the workspace is now, for the rules that compare runs with it. */
export interface CurrentProvenance {
  vcs: RunVcs | null;
  revision: string | null;
  dirty: number | null;
  /** Current fingerprints of what a check depends on, when ue-mcp can tell without a run. */
  dependencies?: Record<string, string>;
}

/** One run of one check, as the hub's run index keeps it. */
export interface RunRecord {
  runId: string;
  kind: CheckKind;
  target: string;
  name: string;
  outcome: RunOutcome;
  startedAt: string;
  endedAt?: string | null;
  durationMs?: number | null;
  summary?: QaSummary | null;
  /** Step indexes that failed, for comparing runs. */
  failedSteps?: number[];
  source: RunSource;
  /** Absent on records kept before triggers were: a replay then says so by replayOf. */
  trigger?: RunTrigger;
  sessionId?: string | null;
  planRunId?: string | null;
  replayOf?: string | null;
  recordingId?: string | null;
  map?: string | null;
  /** Absent for a run the hub did not see start (backfilled, or kept before provenance was): it tested an unknown revision. */
  provenance?: RunProvenance;
  /** Project files changed while the run played (anyone's saves): it tested a project that changed under it. */
  tainted?: boolean;
  /** Tracker items this run refers to, such as the issue filed from it. Per machine, as the run is. */
  links?: TrackerLink[];
  /** A track or scene run: each instance's result at its place on the timeline. */
  arrangement?: ArrangementRun;
}

/** A run's trigger, for records kept before triggers were too. */
export const triggerOf = (r: Pick<RunRecord, "trigger" | "replayOf">): RunTrigger => r.trigger ?? (r.replayOf ? "replay" : "run");

/**
 * A run's outcome from its QaRun, judged by its steps. A live run waiting on
 * a verdict is "needs_you". Once ended, a failed step makes it "failed"; a
 * step the run reached but nobody judged (the wait ran out, or the run was
 * stopped while it waited) makes it "unjudged", no result, since a person's
 * absence says nothing about the game. A run that ended on its own with
 * steps never reached (PIE or the observation ended) is "incomplete"; a
 * stopped one is "stopped".
 */
export function outcomeOf(run: Pick<QaRun, "state" | "steps" | "summary">): RunOutcome {
  const steps = run.steps ?? [];
  const failed = run.summary?.failed ?? steps.filter((s) => s.status === "failed").length;
  const ended = qaEnded(run.state);
  if (!ended && (run.state === "awaiting_verdict" || steps.some((s) => s.status === "awaiting_verdict"))) return "needs_you";
  if (run.state === "armed" || run.state === "running") return failed > 0 ? "failed" : "running";
  if (failed > 0) return "failed";
  if (steps.some((s) => s.status === "awaiting_verdict")) return "unjudged";
  const open: QaStepStatus[] = ["pending", "running"];
  if (steps.length === 0 || steps.some((s) => open.includes(s.status))) return run.state === "stopped" ? "stopped" : "incomplete";
  return "passed";
}

/** Timeline of a run for the run screen: one bar per step on the run's clock. */
export interface RunStepSpan {
  index: number;
  instruction: string;
  status: QaStepStatus;
  judge: "auto" | "manual";
  startMs: number | null;
  endMs: number | null;
}

/** GET /hub/runs/:runId. Everything the run screen draws. */
export interface RunDetail {
  record: RunRecord;
  /** The live or last-known QaRun, when the hub has it. */
  run: QaRun | null;
  steps: RunStepSpan[];
  /** Values the observer tracked, sampled for charts: series name to [ms, value] points. */
  series?: Record<string, Array<[number, number]>>;
  /** Warnings and errors the PIE session logged during the run. */
  log?: Array<{ atMs: number; level: "warning" | "error"; text: string }>;
  /** The newest earlier passing run of the same check, to compare with. */
  lastPass?: RunRecord | null;
  /** Steps whose result differs from lastPass. */
  changedSteps?: number[];
  resultsUrl?: string | null;
  /** A track or scene run: the live or recorded result of each instance, and the segments it played as. */
  arrangement?: ArrangementRun;
}

// ---------- Health and status (computed) ----------

/**
 * A check's health. A run holding for a live verdict is "running": the
 * verdict itself is asked for in the app bar and on the Run screen, never
 * as a status. "missing": the script is not on this checkout, which says
 * nothing about the goal (another branch may have it), so it never makes a
 * goal fail.
 */
export type CheckHealth = "failing" | "running" | "flaky" | "stale" | "passing" | "never_run" | "missing";
/**
 * A goal's status. "ready_to_sign_off": everything passes and only a
 * person's sign-off is missing. "at_risk": met, but a required check is
 * flaky or stale. "in_progress": some evidence, not all of it yet, which
 * the goal's progress counts.
 */
export type GoalStatus = "failing" | "ready_to_sign_off" | "at_risk" | "running" | "in_progress" | "not_started" | "met";

/** Worst first: what every list sorts by, and the order the status ladder is drawn in. */
export const CHECK_HEALTH_ORDER: readonly CheckHealth[] = ["failing", "running", "flaky", "stale", "never_run", "missing", "passing"];
export const GOAL_STATUS_ORDER: readonly GoalStatus[] = ["failing", "ready_to_sign_off", "at_risk", "running", "in_progress", "not_started", "met"];

export interface CheckState {
  id: string;
  check: CheckRef;
  /** The check's title when its CheckMeta has one, else the script's name. */
  name: string;
  /** Tracker items the check refers to (CheckMeta.links). */
  links?: TrackerLink[];
  /** Goals that list this check. Empty for a script no goal names yet. */
  goalIds: string[];
  health: CheckHealth;
  last: RunRecord | null;
  /** Outcomes of the latest results (isResult), newest first, at most 20. */
  recent: RunOutcome[];
  /** Passed share of `recent`, or null with no results. */
  passRate: number | null;
  /** The script's map and step count, when known. */
  map?: string | null;
  steps?: number | null;
  /** The script is not on this checkout. */
  missing?: boolean;
  /** The path goals name, when the script moved and is followed to `check.target` (a redirector ue-mcp reported). */
  renamedFrom?: string;
  /** No goal lists this check, so nothing it proves counts anywhere. */
  unused?: boolean;
  /** The newest result differs from the one before it, and they tested different things (checkChange). */
  change?: CheckChange;
}

/**
 * A result that turned between two runs that tested different things: a
 * pass after a fail is "fixed", a fail after a pass "regressed". The same
 * turn on the same provenance is flakiness, not a change.
 */
export interface CheckChange {
  kind: "fixed" | "regressed";
  /** The newest result, the run that turned. */
  runId: string;
  at: string;
}

/** One goal a goal requires, with that goal's status. */
export interface RequiredGoalState {
  goal: string;
  required: boolean;
  /** Null for a goal that is not in the project or is archived: it counts for nothing. */
  status: GoalStatus | null;
}

export interface GoalState {
  goal: Goal;
  status: GoalStatus;
  checks: CheckState[];
  /** Required checks currently satisfying the acceptance rule, out of all required checks. */
  progress: { met: number; total: number };
  /** The goals and milestones that require this one, archived ones left out, in file order. */
  parents: string[];
  /** The goals this one requires, in its order. */
  requires: RequiredGoalState[];
  /** Not a milestone, and nothing requires it: it is part of no milestone and no goal. */
  unused?: boolean;
  /** The goal's newest sign-off, current or lapsed. */
  signOff?: SignOffState;
}

/**
 * Outcomes that say something about the game: what health, acceptance, pass
 * rates and regressions count. "stopped" and "unjudged" are not: the run was
 * ended before its steps were reached, or nobody gave a manual step its
 * verdict, which proves nothing about the game either way, so the run before
 * it speaks for the check. "incomplete" and "error" are: the game could not
 * get through the run's steps on its own.
 */
export const RESULT_OUTCOMES: readonly RunOutcome[] = ["passed", "failed", "incomplete", "error"];
const DAY = 86_400_000;

/**
 * Whether a run is a result: an outcome that says something about the game
 * (RESULT_OUTCOMES) from a plain run of the check. Replays and fix-loop
 * attempts never are, whatever they show; they stay in history, marked.
 */
export const isResult = (r: RunRecord): boolean => RESULT_OUTCOMES.includes(r.outcome) && triggerOf(r) === "run";

/** The run tested uncommitted changes. */
export const ranDirty = (r: Pick<RunRecord, "provenance">): boolean => (r.provenance?.dirty ?? 0) > 0;

/** Whether a run tested this workspace's revision, or both are under no source control. Unknown never did. */
export function onRevision(p: RunProvenance | undefined, current: Pick<CurrentProvenance, "vcs" | "revision">): boolean {
  if (!p?.vcs || !current.vcs || p.vcs !== current.vcs) return false;
  return p.vcs === "none" || (p.revision !== null && p.revision === current.revision);
}

/**
 * Whether two runs tested the same thing: the same revision, the same script
 * version, and neither over uncommitted changes, which may differ between
 * them. Unknown never matches.
 */
export function sameProvenance(a: Pick<RunRecord, "provenance">, b: Pick<RunRecord, "provenance">): boolean {
  const p = a.provenance;
  const q = b.provenance;
  const clean = (x: RunProvenance): boolean => x.vcs === "none" || x.dirty === 0;
  return !!p && !!q && clean(p) && clean(q) && !!p.scriptHash && p.scriptHash === q.scriptHash && onRevision(p, q);
}

export interface HealthInput {
  check: CheckRef;
  /** Runs of this check, newest first. */
  runs: RunRecord[];
  missing?: boolean;
  /** The project's health rule; DEFAULT_HEALTH_RULE without one. */
  rule?: HealthRule;
  /** The workspace now, for staleness by dependency fingerprints. */
  current?: CurrentProvenance;
  now?: number;
}

/**
 * Stale since a check's last pass: an asset it depends on changed, where the
 * pass and the workspace both have fingerprints for it; otherwise older than
 * the rule's age.
 */
function staleSince(pass: RunRecord, rule: HealthRule, current: CurrentProvenance | undefined, now: number): boolean {
  const then = pass.provenance?.dependencies;
  const deps = current?.dependencies;
  const known = then && deps ? Object.keys(then).filter((k) => k in deps) : [];
  if (known.length) return known.some((k) => deps![k] !== then![k]);
  if (rule.staleAfterDays === null) return false;
  const at = Date.parse(pass.endedAt ?? pass.startedAt);
  return Number.isFinite(at) && now - at > rule.staleAfterDays * DAY;
}

/**
 * A check's health from its results (isResult), the same wherever it is
 * listed: stopped, unjudged, replayed and fix-loop runs leave the health the
 * result before them gave.
 *
 * - Running: a run of it plays now, or holds for a verdict.
 * - Failing: the newest result did not pass.
 * - Flaky: it passed, and the rule's flakyWindow latest results on the same
 *   provenance are mixed. Mixed results across revisions or script versions
 *   are a fix or a regression (checkChange), not flakiness.
 * - Stale: staleSince the pass.
 *
 * A run over uncommitted changes is the newest word on the check, so a person
 * changing the game sees what their last run said, but it never shows
 * flakiness (sameProvenance) and never counts toward acceptance.
 */
export function checkHealth(input: HealthInput): CheckHealth {
  const { runs, rule = DEFAULT_HEALTH_RULE, now = Date.now() } = input;
  if (input.missing) return "missing";
  const live = runs[0];
  if (live?.outcome === "running" || live?.outcome === "needs_you") return "running";
  const done = runs.filter(isResult);
  if (done.length === 0) return "never_run";
  const newest = done[0];
  if (newest.outcome !== "passed") return "failing";
  const same = done.filter((r) => sameProvenance(r, newest)).slice(0, rule.flakyWindow);
  if (same.some((r) => r.outcome !== "passed")) return "flaky";
  if (staleSince(newest, rule, input.current, now)) return "stale";
  return "passing";
}

/**
 * Whether a check satisfies the acceptance rule: its latest stableRuns
 * results on the current revision and the script's newest version all
 * passed. Runs over uncommitted changes and runs of an unknown revision
 * never count.
 */
export function checkSatisfied(runs: RunRecord[], acceptance: AcceptanceRule, current?: CurrentProvenance): boolean {
  const need = Math.max(1, acceptance.stableRuns);
  const counted = runs.filter((r) => isResult(r) && !!r.provenance && !ranDirty(r) && (!current || onRevision(r.provenance, current)));
  const version = counted[0]?.provenance?.scriptHash;
  if (!version) return false;
  const done = counted.filter((r) => r.provenance!.scriptHash === version).slice(0, need);
  return done.length >= need && done.every((r) => r.outcome === "passed");
}

/** The newest result's turn from the one before it, when they tested different things. */
export function checkChange(runs: RunRecord[]): CheckChange | undefined {
  const [n, p] = runs.filter(isResult);
  if (!n || !p) return undefined;
  const passed = (r: RunRecord): boolean => r.outcome === "passed";
  if (passed(n) === passed(p) || sameProvenance(n, p)) return undefined;
  return { kind: passed(n) ? "fixed" : "regressed", runId: n.runId, at: n.endedAt ?? n.startedAt };
}

export function checkStateOf(input: HealthInput & { goalIds: string[]; meta?: CheckMeta; name?: string; map?: string | null; steps?: number | null; renamedFrom?: string }): CheckState {
  const done = input.runs.filter(isResult).slice(0, 20);
  const change = checkChange(input.runs);
  const links = input.meta?.links ?? [];
  return {
    id: checkId(input.check),
    check: input.check,
    name: input.meta?.title ?? input.name ?? leafName(input.check.target),
    ...(links.length ? { links } : {}),
    goalIds: input.goalIds,
    health: checkHealth(input),
    last: input.runs[0] ?? null,
    recent: done.map((r) => r.outcome),
    passRate: done.length ? done.filter((r) => r.outcome === "passed").length / done.length : null,
    map: input.map ?? null,
    steps: input.steps ?? null,
    ...(input.missing ? { missing: true } : {}),
    ...(input.renamedFrom ? { renamedFrom: input.renamedFrom } : {}),
    ...(input.goalIds.length === 0 ? { unused: true } : {}),
    ...(change ? { change } : {}),
  };
}

/** Whether a goal is met only once a person signs it off. */
export const needsSignOff = (goal: Pick<Goal, "acceptance">): boolean => goal.acceptance.signOff;

/**
 * A goal's status from its checks' health and acceptance (stableRuns counted
 * here, at the roll-up, never in a check's own health) and the statuses of
 * the goals it requires. An optional goal (required: false) never blocks:
 * only a run in it shows, as an optional check's would. A required check
 * missing on this checkout is not met, and is no failure either: the goal
 * waits for it. `current` is the workspace acceptance compares revisions
 * with (checkSatisfied). `signedOff`: the goal has a sign-off that has not
 * lapsed.
 */
export function goalStatus(goal: Goal, checks: Array<{ state: CheckState; runs: RunRecord[] }>, childStatuses: GoalStatus[] = [], optionalStatuses: GoalStatus[] = [], current?: CurrentProvenance, signedOff = false): { status: GoalStatus; progress: { met: number; total: number } } {
  const required = checks.filter((c) => c.state.check.required !== false);
  const met = required.filter((c) => !c.state.missing && checkSatisfied(c.runs, goal.acceptance, current)).length;
  // At risk is met, with a flaky or stale check under it.
  const childMet = (s: GoalStatus): boolean => s === "met" || s === "at_risk";
  // Required goals count as one each beside the checks, so a milestone made of goals shows how many are met.
  const progress = { met: met + childStatuses.filter(childMet).length, total: required.length + childStatuses.length };
  const hs = checks.map((c) => c.state.health);
  const reqHs = required.map((c) => c.state.health);
  if (reqHs.includes("failing") || childStatuses.includes("failing")) return { status: "failing", progress };
  if (hs.includes("running") || childStatuses.includes("running") || optionalStatuses.includes("running")) return { status: "running", progress };
  if (required.length === 0 && childStatuses.length === 0) return { status: "not_started", progress };
  const everyChildMet = childStatuses.every(childMet);
  if (met === required.length && everyChildMet) {
    // Everything is in and only a person's sign-off is missing.
    if (goal.acceptance.signOff && !signedOff) return { status: "ready_to_sign_off", progress };
    if (reqHs.some((h) => h === "flaky" || h === "stale") || childStatuses.includes("at_risk")) return { status: "at_risk", progress };
    return { status: "met", progress };
  }
  if (reqHs.every((h) => h === "never_run" || h === "missing") && childStatuses.every((s) => s === "not_started")) return { status: "not_started", progress };
  // Some evidence, not all of it yet (a check never run, too few passes in a row, a required goal not met): the progress says how far.
  return { status: "in_progress", progress };
}

// ---------- Sign-off ----------

/** What a sign-off accepts of a goal: its acceptance rule, required checks and required goals, in a form that ignores their order. */
export function requirementsKey(goal: Pick<Goal, "acceptance" | "checks" | "requires">): string {
  const { stableRuns, signOff } = goal.acceptance;
  return JSON.stringify({
    acceptance: { stableRuns, signOff },
    checks: goal.checks.filter((c) => c.required !== false).map(checkId).sort(),
    requires: requiresOf(goal).filter((r) => r.required !== false).map((r) => r.goal).sort(),
  });
}

/**
 * Why a sign-off no longer describes the goal, or null while it does:
 *
 * - "requirements": the goal's requirements changed since (requirementsKey).
 * - "failed": a required check has a failing result since.
 * - "revision": the workspace is at another revision, and a required check's
 *   newest result there tested another script version or map than the run
 *   the sign-off was given against, or there is no result there yet to say.
 *
 * Without `current` the revision is not compared, as in checkSatisfied.
 */
export function signOffLapse(s: SignOff, goal: Goal, checks: Array<{ state: CheckState; runs: RunRecord[] }>, current?: CurrentProvenance): SignOffLapse | null {
  if (requirementsKey(goal) !== s.requirements) return "requirements";
  const required = checks.filter((c) => c.state.check.required !== false);
  const failedSince = (r: RunRecord): boolean => isResult(r) && r.outcome !== "passed" && (r.endedAt ?? r.startedAt) > s.at;
  if (required.some((c) => c.runs.some(failedSince))) return "failed";
  if (!current || current.revision === s.revision) return null;
  const sameTest = (c: { runs: RunRecord[] }): boolean => {
    const signed = c.runs.find((r) => s.runIds.includes(r.runId));
    const now = c.runs.find((r) => isResult(r) && onRevision(r.provenance, current));
    return !!signed?.provenance?.scriptHash && !!now?.provenance && now.provenance.scriptHash === signed.provenance.scriptHash && (now.map ?? null) === (signed.map ?? null);
  };
  return required.every(sameTest) ? null : "revision";
}

// ---------- The relationship graph ----------

type GraphGoal = Pick<Goal, "id" | "requires">;

/** The goals a goal requires, in its order. */
export const requiresOf = (g: Pick<Goal, "requires">): GoalRequirement[] => g.requires ?? [];

/** Each goal's parents: the goals that require it, archived ones left out, in the order the goals are given. */
export function goalParents(goals: readonly Pick<Goal, "id" | "requires" | "archived">[]): Map<string, string[]> {
  const out = new Map<string, string[]>();
  for (const g of goals) {
    if (g.archived) continue;
    for (const r of requiresOf(g)) {
      const list = out.get(r.goal) ?? [];
      if (!list.includes(g.id)) list.push(g.id);
      out.set(r.goal, list);
    }
  }
  return out;
}

/**
 * Loops of goals that require each other, each as a path that starts and
 * ends on the same goal (["a", "b", "a"]), and the edges ("from>to") that
 * close them. Found depth first from the goals in id order, so the same goals
 * always give the same answer; without those edges the goals form no loop.
 */
export function goalLoops(goals: readonly GraphGoal[]): { cycles: string[][]; edges: Set<string> } {
  const byId = new Map(goals.map((g) => [g.id, g]));
  const state = new Map<string, 1 | 2>();
  const stack: string[] = [];
  const cycles: string[][] = [];
  const edges = new Set<string>();
  const visit = (id: string): void => {
    state.set(id, 1);
    stack.push(id);
    for (const r of requiresOf(byId.get(id)!)) {
      if (!byId.has(r.goal)) continue;
      const s = state.get(r.goal);
      if (s === 1) {
        cycles.push([...stack.slice(stack.indexOf(r.goal)), r.goal]);
        edges.add(`${id}>${r.goal}`);
      } else if (s === undefined) {
        visit(r.goal);
      }
    }
    stack.pop();
    state.set(id, 2);
  };
  for (const id of [...byId.keys()].sort()) if (!state.has(id)) visit(id);
  return { cycles, edges };
}

/** A path of requirements from one goal to another ([from, ..., to]), or null when there is none. */
export function requirePath(goals: readonly GraphGoal[], from: string, to: string): string[] | null {
  const byId = new Map(goals.map((g) => [g.id, g]));
  const seen = new Set<string>();
  const walk = (id: string, path: string[]): string[] | null => {
    if (id === to) return path;
    if (seen.has(id)) return null;
    seen.add(id);
    const g = byId.get(id);
    for (const r of g ? requiresOf(g) : []) {
      const found = walk(r.goal, [...path, r.goal]);
      if (found) return found;
    }
    return null;
  };
  return walk(from, [from]);
}

/** The loop `parent` requiring `child` would close ([parent, child, ..., parent]), or null when it closes none. */
export function loopIfRequired(goals: readonly GraphGoal[], parent: string, child: string): string[] | null {
  if (parent === child) return [parent, parent];
  const back = requirePath(goals, child, parent);
  return back ? [parent, ...back] : null;
}

/**
 * The goals a goal stands for, itself last: every goal reachable through
 * its requirements, depth first in each goal's order, each once. Archived
 * and unknown goals are left out, and a loop is followed only once.
 */
export function goalTree(goals: readonly Pick<Goal, "id" | "requires" | "archived">[], id: string): string[] {
  const byId = new Map(goals.map((g) => [g.id, g]));
  const out: string[] = [];
  const seen = new Set<string>();
  const visit = (gid: string): void => {
    const g = byId.get(gid);
    if (!g || seen.has(gid) || (g.archived && gid !== id)) return;
    seen.add(gid);
    for (const r of requiresOf(g)) visit(r.goal);
    out.push(gid);
  };
  visit(id);
  return out;
}

/**
 * The checks running a goal runs: those of every goal it requires, depth
 * first in order, then its own, each check once, in that order.
 */
export function goalChecksDeep(goals: readonly Pick<Goal, "id" | "requires" | "archived" | "checks">[], id: string): CheckRef[] {
  const byId = new Map(goals.map((g) => [g.id, g]));
  const seen = new Set<string>();
  const out: CheckRef[] = [];
  for (const gid of goalTree(goals, id)) {
    for (const c of byId.get(gid)!.checks) {
      const cid = checkId(c);
      if (seen.has(cid)) continue;
      seen.add(cid);
      out.push(c);
    }
  }
  return out;
}

/**
 * Every goal's state: its own checks' status rolled up through the goals it
 * requires, each worked out once. An edge that closes a loop is left out of
 * the roll-up (goalLoops says which), so status never goes round forever;
 * archived and unknown goals count for nothing. `current` as for goalStatus.
 * `signOffs`: the goal store's current sign-off of each goal, by goal id,
 * each counted unless it lapsed (signOffLapse).
 */
export function rollUpGoals(goals: readonly Goal[], checksOf: (g: Goal) => Array<{ state: CheckState; runs: RunRecord[] }>, current?: CurrentProvenance, signOffs?: ReadonlyMap<string, SignOff>): GoalState[] {
  const byId = new Map(goals.map((g) => [g.id, g]));
  const { edges } = goalLoops(goals);
  const parents = goalParents(goals);
  const done = new Map<string, GoalState>();
  const one = (g: Goal): GoalState => {
    const had = done.get(g.id);
    if (had) return had;
    const kids = requiresOf(g)
      .map((r) => ({ r, child: byId.get(r.goal) }))
      .filter((x): x is { r: GoalRequirement; child: Goal } => !!x.child && !x.child.archived && !edges.has(`${g.id}>${x.r.goal}`))
      .map((x) => ({ required: x.r.required !== false, status: one(x.child).status }));
    const checks = checksOf(g);
    const record = signOffs?.get(g.id);
    const signOff: SignOffState | undefined = record ? { record, lapsed: signOffLapse(record, g, checks, current) } : undefined;
    const { status, progress } = goalStatus(g, checks, kids.filter((k) => k.required).map((k) => k.status), kids.filter((k) => !k.required).map((k) => k.status), current, !!signOff && !signOff.lapsed);
    const ps = parents.get(g.id) ?? [];
    const state: GoalState = { goal: g, status, checks: checks.map((c) => c.state), progress, parents: ps, requires: [], ...(g.kind !== "milestone" && !g.archived && ps.length === 0 ? { unused: true } : {}), ...(signOff ? { signOff } : {}) };
    done.set(g.id, state);
    return state;
  };
  const all = goals.map(one);
  for (const s of all) {
    s.requires = requiresOf(s.goal).map((r) => {
      const c = done.get(r.goal);
      return { goal: r.goal, required: r.required !== false, status: c && !c.goal.archived ? c.status : null };
    });
  }
  return all;
}

export const leafName = (path: string): string => {
  const last = path.split("/").filter(Boolean).pop() ?? path;
  return last.replace(/\.[^.]*$/, "");
};

// ---------- The goal store ----------

/**
 * What the goal store keeps, one kind of record each: goals (milestones
 * included), what a check says about itself (id: the check id), sign-offs,
 * the project's health rule (id "health"), and the one-time import of an
 * older Studio's QA/Goals files (id "qa-goals").
 */
export type HubRecordType = "goal" | "check" | "signoff" | "rules" | "import";

/**
 * The import of a project's QA/Goals files, where an older Studio kept
 * goals, into an empty store. It happens once: the files are read, never
 * changed, and never read again.
 */
export interface GoalImport {
  /** The folder read, relative to the project. */
  from: string;
  at: string;
  /** The goals it brought. */
  goals: string[];
  /** Files and fields it could not read, each named. */
  problems: string[];
}

export interface HubRecordData {
  goal: Goal;
  check: CheckMetaRecord;
  signoff: SignOff;
  rules: HealthRule;
  import: GoalImport;
}

/**
 * One version of one record. A store never rewrites a version: a change is a
 * new version (rev one more), and a removal is a version with `deleted` and
 * no data. The newest version of each (type, id) is the record. The same
 * records move to a hosted store unchanged: ids are unique across computers
 * (goal ids end in a random part, sign-off ids are random) and each version
 * names the store it was written in.
 */
export interface HubRecord<T extends HubRecordType = HubRecordType> {
  schema: typeof HUB_SCHEMA;
  type: T;
  id: string;
  /** This version's number: 1 for the first. */
  rev: number;
  at: string;
  /** The store this version was written in: a random id each project's store takes on each computer. */
  origin: string;
  deleted?: true;
  data?: HubRecordData[T];
}

/** Any record, its data typed by its type. */
export type AnyHubRecord = { [T in HubRecordType]: HubRecord<T> }[HubRecordType];

/** The format a goals file names, so an import knows it for one. */
export const HUB_EXPORT_FORMAT = "ue-mcp.goals";

/**
 * GET /hub/store: the whole goal store as one file, to move it to another
 * computer. Every live record at its newest version, the import marker
 * left out.
 */
export interface HubExport {
  format: typeof HUB_EXPORT_FORMAT;
  schema: typeof HUB_SCHEMA;
  exportedAt: string;
  /** The store it came from. */
  origin: string;
  records: AnyHubRecord[];
}

/**
 * POST /hub/store: what importing a HubExport did. The rule is merge by id,
 * and nothing here changes: a record this store lacks is added as it is in
 * the file; one it holds with the same content is unchanged; one it holds
 * with other content keeps this store's version and is listed in conflicts.
 * A file with any record that does not read is refused whole.
 */
export interface HubImportReport {
  added: number;
  unchanged: number;
  conflicts: Array<{ type: HubRecordType; id: string; title: string }>;
}

// ---------- Plans and the queue (per machine) ----------

export type PlanScope =
  | { kind: "goal"; goalId: string }
  | { kind: "track"; goalId: string }
  | { kind: "scene"; goalId: string }
  | { kind: "slot"; sceneId: string; trackId: string }
  | { kind: "arrangement" }
  | { kind: "checks"; checks: CheckRef[] }
  | { kind: "failing"; goalId?: string }
  | { kind: "stale"; goalId?: string }
  | { kind: "all" };

export interface PlanOptions {
  /**
   * A person is there to judge manual steps while they play. Without one, a
   * check whose script has a manual step is skipped (NEEDS_PERSON) rather
   * than started.
   */
  attended: boolean;
  /** How long each run waits for a verdict, one of QA_VERDICT_WAIT_CHOICES. Absent: the "Wait for a verdict" setting. */
  verdictWaitMinutes?: number;
  stopOnFailure: boolean;
  /** Replay each check's last recording instead of a live run, where it has one. */
  replay: boolean;
}
/** The options a plan starts from when its request gives none: someone is there to judge. */
export const DEFAULT_PLAN_OPTIONS: PlanOptions = { attended: true, stopOnFailure: false, replay: false };
/** The options a plan starts from by what queued it: a schedule's runs are unattended. */
export const planOptionsFor = (trigger: PlanTrigger): PlanOptions => ({ ...DEFAULT_PLAN_OPTIONS, attended: trigger !== "schedule" });

/** Why an unattended plan skips a check. */
export const NEEDS_PERSON = "needs a person";

/**
 * Whether a script needs someone at the editor: it has a manual step. A step
 * an agent may judge counts too, since a plan run has no agent assigned to
 * judge it.
 */
export const needsPerson = (steps: readonly Pick<QaStep, "judge">[]): boolean => steps.some((s) => s.judge === "manual");

export type PlanItemState = "queued" | "running" | "passed" | "failed" | "needs_you" | "unjudged" | "skipped" | "error" | "crashed" | "cancelled";
export interface PlanItem {
  check: CheckRef;
  name: string;
  /**
   * needs_you: its run holds at a manual step, PIE paused, until a verdict.
   * unjudged: the run ended without that verdict, no result (reason "no verdict").
   * error: the run could not get through its steps (reason says why).
   * crashed: the editor crashed during it. Recorded once and never retried; it counts as failing.
   */
  state: PlanItemState;
  runId?: string | null;
  /** A fix item's chat session, where its loop's agents work. */
  sessionId?: string | null;
  reason?: string | null;
  startedAt?: string | null;
  endedAt?: string | null;
}

/** Plan item states that count as failing. */
export const FAILING_ITEM_STATES: readonly PlanItemState[] = ["failed", "error", "crashed"];

/** A plan item's state once its run ended: a stopped run is a cancelled item; one that ended before its steps settled is an error. */
export function itemStateOf(outcome: RunOutcome): PlanItemState {
  return outcome === "passed" || outcome === "failed" || outcome === "unjudged" ? outcome : outcome === "stopped" ? "cancelled" : "error";
}

export type PlanState = "queued" | "running" | "paused" | "done" | "cancelled" | "failed";
export type PlanTrigger = "person" | "schedule" | "agent";

export interface PlanRun {
  id: string;
  title: string;
  /** What each item does: "run" runs its check (the default), "fix" hands it to a fix loop in a chat session of its own. */
  work?: "run" | "fix";
  scope: PlanScope;
  options: PlanOptions;
  state: PlanState;
  items: PlanItem[];
  trigger: PlanTrigger;
  /** The schedule that queued it. */
  scheduleId?: string | null;
  createdAt: string;
  startedAt?: string | null;
  endedAt?: string | null;
}

/** POST /hub/plans. */
export interface PlanRequest {
  title?: string;
  scope: PlanScope;
  options?: Partial<PlanOptions>;
  /** Run next: ahead of every other queued plan, behind only the one on the editor. */
  next?: boolean;
}

/**
 * Where a person's Run put its plan (POST /hub/plans, POST
 * /hub/checks/:checkId/run), so the client can say so where they clicked.
 *
 * A person's Run is a person resuming: when the queue was paused (by a
 * person, Release the editor, or a reconnect) it resumes, and with nothing
 * on the editor the plan starts ahead of whatever waited behind the pause.
 * An agent's plan never resumes a paused queue.
 *
 * - started: its first item is on the editor (or starting).
 * - waiting: it is next, but the editor is not free (`reason`).
 * - queued: other work is ahead of it (`ahead`, `position`), or the queue is paused (`reason`).
 */
export interface PlanPlacement {
  state: "started" | "waiting" | "queued";
  /** Its place among the queued plans, 1 for next; 0 once started. */
  position: number;
  /** Plans ahead of it, the one on the editor included. */
  ahead: number;
  /** Why it does not start now. */
  reason?: string | null;
  /** The queue was paused and this Run resumed it. */
  resumed?: boolean;
}

/**
 * A plan run at a local time on chosen days of the week, such as "nightly
 * at 02:00". A schedule is a person's standing instruction, so it runs under
 * the Manual playtest-runs setting too, but it never takes the editor from
 * anyone: when it is due it waits while the queue is paused or busy, or the
 * editor is held (the occupancy answer, the consent ledger included, and
 * every other session quiet), and gives up with the reason in `lastSkipped`
 * once it has waited an hour. Kept with the queue.
 */
export interface Schedule {
  id: string;
  title: string;
  scope: PlanScope;
  options: PlanOptions;
  /** Local time HH:MM. */
  at: string;
  /** Days of the week it runs, 0 Sunday to 6 Saturday. */
  days: number[];
  enabled: boolean;
  lastRunAt?: string | null;
  /** Why the last due run did not start. */
  lastSkipped?: string | null;
  /** Due now, and why it has not started yet. */
  waiting?: string | null;
  nextRunAt?: string | null;
}
export type ScheduleDraft = Omit<Schedule, "id" | "lastRunAt" | "lastSkipped" | "waiting" | "nextRunAt">;

/** POST /hub/fix: a fix plan for every failing check of a goal (or of the project), or for one check. */
export interface FixRequest {
  goalId?: string;
  checkId?: string;
}

export interface QueueState {
  paused: boolean;
  /** Why the queue is paused: a person paused it, Release the editor paused it, or the editor reconnected. A person resumes it either way. */
  pausedBy?: QueuePause | null;
  current: PlanRun | null;
  queued: PlanRun[];
  /** Finished plans, newest first, at most 20. */
  recent: PlanRun[];
  schedules: Schedule[];
  editor: { connected: boolean };
  /** The hub's hold on the editor while it starts or runs an item. */
  lease?: EditorLease | null;
  /** Why the next item waits although the queue is not paused, such as "Play In Editor is in use". */
  waiting?: string | null;
}

export type QueuePause = "person" | "release" | "reconnect";

// ---------- The editor lease (per machine) ----------

/**
 * What else holds the editor, one signal each, from what the daemon already
 * knows: the editor not connected, an editor operation (restart, rebuild),
 * an open modal dialog, Play In Editor the hub did not start, a playtest the
 * hub did not start, a chat or fix loop with editor calls in flight, another
 * MCP client's calls, and someone using this computer. From the consent
 * ledger (editor(hold), disruption requests): another session holding the
 * editor or with an editor transaction open ("session"), and a request to
 * stop, restart or rebuild the editor still open ("consent").
 */
export type OccupancyKind = "disconnected" | "operation" | "dialog" | "pie" | "run" | "agent" | "client" | "input" | "session" | "consent";
export interface OccupancyReason {
  kind: OccupancyKind;
  text: string;
}

/**
 * The hub's explicit hold on one editor: taken before it starts a plan item
 * or a fix loop, released when that ends or when a person releases the
 * editor. Only the hub acts on what it holds: the run it started, and Play In
 * Editor when that run started it. `editor` names which editor; today the
 * developer's own ("workspace") is the only one.
 */
export interface EditorLease {
  editor: string;
  /** What the hub is doing, such as "Run Tutorial: Door opens". */
  purpose: string;
  planRunId: string;
  /** The item's position in its plan. */
  item: number;
  work: "run" | "fix";
  /** The run the item started, once it has one. */
  runId?: string | null;
  /** The fix loop's chat session. */
  sessionId?: string | null;
  /** The item's run started Play In Editor, so ending that session is the hub's to do. */
  pie?: boolean;
  since: string;
}

/** The body of `hub.lease` (HUB_LEASE): the lease as it is now, null when the hub holds nothing. */
export interface HubLeaseEvent {
  lease: EditorLease | null;
}
export const HUB_LEASE = "hub.lease";

// ---------- Needs attention (computed) ----------

/**
 * What waits for a person and does not depend on a game playing now. A
 * verdict is not one: it is given live, while its step plays (the run's
 * held verdict, QaHeld).
 */
export type AttentionKind = "loop_blocked" | "regression" | "sign_off" | "plan_failed";

export interface AttentionItem {
  /** Stable while the thing it points at waits: "<kind>:<id>" and the like. */
  id: string;
  kind: AttentionKind;
  at: string;
  title: string;
  detail?: string;
  goalIds: string[];
  runId?: string;
  step?: number;
  checkId?: string;
  planRunId?: string;
  /** A blocked fix loop's chat session: where the agent work, and its blocker, are. */
  sessionId?: string;
}

export const ATTENTION_ORDER: readonly AttentionKind[] = ["loop_blocked", "sign_off", "regression", "plan_failed"];

// ---------- Overview ----------

/**
 * Where the runs behind every status come from. "local": this computer's
 * own runs only, so two people can see two statuses for the same goal; a
 * shared store would answer for more than one computer.
 */
export interface EvidenceScope {
  store: "local";
  current: CurrentProvenance;
}

/** GET /hub. Everything the overview and library draw, in one answer. */
export interface HubOverview {
  goals: GoalState[];
  /** Every known check: those goals name and scripts in the project no goal names yet. */
  checks: CheckState[];
  attention: AttentionItem[];
  queue: QueueState;
  /** Newest runs across the project, at most 30. */
  recent: RunRecord[];
  /** Whose evidence every status here is worked out from, and the workspace it is compared with. */
  evidence: EvidenceScope;
  /** The project's rule for flaky and stale. */
  rules: HealthRule;
  /** The goals imported from an older Studio's QA/Goals files, when that happened. */
  imported: GoalImport | null;
  editor: { connected: boolean };
  /** Goals that require each other in a loop, goals required but gone, and what went wrong in the background. */
  warnings: string[];
}

/** GET /hub/checks/:checkId. */
export interface CheckDetail {
  state: CheckState;
  /** Runs newest first, a page. */
  runs: RunRecord[];
  more: boolean;
  /** The script's steps as ue-mcp has them, when it is a script. */
  script?: QaRun | null;
}

export type HubChangedWhat = "goals" | "runs" | "queue" | "attention" | "checks";
/** The body of `studio.hub.changed`. */
export interface HubChanged {
  what: HubChangedWhat[];
  goalId?: string;
  runId?: string;
  planRunId?: string;
}
export const HUB_CHANGED = "hub.changed";

/**
 * The hub's routes, each relative to /v1 (so `GET /hub` is `GET /v1/hub`).
 * Ids in paths are encodeURIComponent'd. DAEMON_ROUTES names each one as
 * `hub.<name>`. Verdicts and stop go through pie(verdict) and pie(script_stop).
 */
export const HUB_ROUTES = {
  overview: "GET /hub",
  goal: "GET /hub/goals/:id -> GoalState",
  createGoal: "POST /hub/goals GoalDraft -> { goal: Goal }",
  updateGoal: "PUT /hub/goals/:id GoalUpdate -> { goal: Goal }",
  deleteGoal: "DELETE /hub/goals/:id -> 204",
  addChecks: "POST /hub/goals/:id/checks { checks: CheckRef[] } -> { goal }",
  removeCheck: "DELETE /hub/goals/:id/checks/:checkId -> { goal }",
  updateGoalCheck: "PUT /hub/goals/:id/checks/:checkId GoalCheckUpdate -> { goal }",
  orderChecks: "PUT /hub/goals/:id/checks/order { order: checkIds } -> { goal }",
  addRequire: "POST /hub/goals/:id/requires { goal, required? } -> { goal } (an existing entry takes the new required)",
  removeRequire: "DELETE /hub/goals/:id/requires/:goalId -> { goal }",
  orderRequires: "PUT /hub/goals/:id/requires { order: goalIds } -> { goal }",
  signOff: "POST /hub/goals/:id/sign-off { note? } -> { signOff: SignOff } (409 without a signed-in account)",
  clearSignOff: "DELETE /hub/goals/:id/sign-off -> 204",
  writePlaytests: "POST /hub/goals/:id/write-playtests { text? } -> 202 { sessionId }",
  check: "GET /hub/checks/:checkId?limit=&before= -> CheckDetail",
  updateCheck: "PUT /hub/checks/:checkId CheckMetaUpdate -> { meta: CheckMeta }",
  rules: "PUT /hub/rules Partial<HealthRule> -> { rules: HealthRule }",
  exportStore: "GET /hub/store -> HubExport",
  importStore: "POST /hub/store HubExport -> HubImportReport (merge by id; 400 naming every record that does not read)",
  runs: "GET /hub/runs?check=&limit=&before= -> { runs: RunRecord[], more: boolean }",
  run: "GET /hub/runs/:runId -> RunDetail",
  addRunLink: "POST /hub/runs/:runId/links { link: TrackerLink } -> { record: RunRecord }",
  removeRunLink: "DELETE /hub/runs/:runId/links?url= -> { record: RunRecord }",
  queue: "GET /hub/queue -> QueueState",
  plan: "POST /hub/plans PlanRequest -> { plan: PlanRun, placement: PlanPlacement } (a person's Run resumes a paused queue)",
  cancelPlan: "POST /hub/plans/:id/cancel -> { plan }",
  movePlan: "POST /hub/plans/:id/move { to: number } -> QueueState (0 is next)",
  createSchedule: "POST /hub/schedules ScheduleDraft -> { schedule }",
  updateSchedule: "PUT /hub/schedules/:id Partial<ScheduleDraft> -> { schedule }",
  deleteSchedule: "DELETE /hub/schedules/:id -> 204",
  pause: "PUT /hub/queue/paused { paused: boolean } -> QueueState",
  lease: "GET /hub/lease -> HubLeaseEvent",
  release: "POST /hub/release -> QueueState (stops what the hub holds on the editor and pauses the queue)",
  fix: "POST /hub/fix FixRequest -> { queued: number, plan: PlanRun | null, placement?: PlanPlacement } (a fix plan: one chat session per check)",
  runCheck: "POST /hub/checks/:checkId/run { replay?, next? } -> { plan: PlanRun, placement: PlanPlacement }",
  arrangement: "GET /hub/goals/:id/arrangement -> ArrangementView",
  putArrangement: "PUT /hub/goals/:id/arrangement { arrangement } | { text } -> ArrangementView (400 { error, findings })",
  editArrangement: "POST /hub/goals/:id/arrangement/edit { ops } -> ArrangementView & { changed } (400 { error, finding, findings })",
  set: "GET /hub/set -> SetView",
  editSet: "POST /hub/set/edit { ops } -> SetView & { changed } (400 { error, finding })",
  clips: "GET /hub/clips?q=&kind= -> { clips: ClipEntry[] }",
  runSet: "POST /hub/run { scope: track | scene | slot | arrangement, next? } -> { plan: PlanRun, placement: PlanPlacement, runId: string | null } (runId once a plan that started at once has its run)",
  runVerdict: "POST /hub/runs/:runId/verdict { check, verdict, note? } -> { ok: true } (a manual check in a track or scene run)",
} as const;

/** A track's arrangement as a client edits it (GET /hub/goals/:id/arrangement). */
export interface ArrangementView {
  arrangement: Arrangement;
  /** The compact text form agents read and write. */
  text: string;
  layout: InstanceLayout[];
  findings: ArrangementFinding[];
}

/** The set as both views draw it (GET /hub/set): tracks and scenes in order, with their status. */
export interface SetView {
  tracks: Array<{ id: string; title: string; order: number; color?: SetColor; player: number; mute: boolean; solo: boolean; arrangement: Arrangement; layout: InstanceLayout[]; status: GoalStatus }>;
  scenes: Array<{ id: string; title: string; order: number; color?: SetColor; slots: Array<Slot & { length: number | null }>; status: GoalStatus }>;
  /** The compact text form agents read. */
  text: string;
  findings: ArrangementFinding[];
}

/** One clip a browser lists (GET /hub/clips): a timeline clip or a step-list playtest. */
export interface ClipEntry {
  kind: "clip" | "script";
  target: string;
  name: string;
  map: string | null;
  /** Frames at `rate`: a timeline clip's length, or a step-list playtest's newest run at 60. Null when not known. */
  length: number | null;
  rate: number;
  lastRun: { runId: string; outcome: string; at: string } | null;
}

// ---------- What else holds the editor ----------

/** Whether an agent's plan may start without a person: a client's setting. */
export type PlaytestRunsMode = "manual" | "automatic";

/** An editor call in flight, for the occupancy answer. */
export interface BusyCall {
  id: number;
  category: string;
  action: string;
  /** Daemon clock, epoch ms. */
  startedAt: number;
  /** agent: a client's own agent made it, named by sessionId (HubClientOptions.agentCalls); client: an MCP client did. */
  source: "agent" | "client";
  sessionId?: string;
  client?: string;
  /** The turn waiting on it was stopped; the editor finishes the call regardless. */
  stopped: boolean;
}

/** An editor restart, rebuild, start or stop the daemon is running. */
export interface EditorOperation {
  op: "start" | "stop" | "restart" | "rebuild";
  step: string;
  /** Daemon clock, epoch ms. */
  startedAt: number;
  stepStartedAt: number;
}

/** How the last editor operation ended when it did not bring the editor back. */
export interface EditorOperationFailure {
  op: EditorOperation["op"];
  step: string;
  /** What went wrong, in one or two sentences. */
  reason: string;
  /** Daemon clock, epoch ms. */
  at: number;
}

/** What is using the editor now. `now` is the daemon's clock. */
export interface BusyState {
  now: number;
  calls: BusyCall[];
  /** The editor operation in progress, when there is one. */
  operation?: EditorOperation;
  /** The last operation's failure, until the editor connects or another operation starts. */
  failure?: EditorOperationFailure;
}
