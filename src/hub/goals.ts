// @session per-session: The rules over one project's goal store, built over that project's directory.
/**
 * The rules over the goal store (goal-store.ts): what a goal, a check's own
 * record, a sign-off and the health rule may hold, the goal graph (a goal
 * requires goals, held by the goal that requires; a change that would make a
 * loop is refused), and moving the store as a file (export and import). The
 * store only keeps records; everything that decides what goes in it is here.
 *
 * A project that kept goals in QA/Goals under an older Studio has them
 * imported once, into an empty store, the first time the hub starts there.
 * The files are read and never changed or read again, and the import is
 * recorded (GoalImport). A file's "parent" becomes an entry in the parent's
 * requires; a check's title and links become that check's own record; a
 * check of a kind this Studio cannot run is kept as unsupported. The
 * sign-offs that Studio kept in <dataDir>/hub/sign-offs.json are taken in
 * the same way, once.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  DEFAULT_ACCEPTANCE,
  DEFAULT_HEALTH_RULE,
  GOAL_DUE,
  GOAL_ID,
  GOAL_PRIORITIES,
  HUB_EXPORT_FORMAT,
  HUB_SCHEMA,
  MAIN_ARRANGEMENT_ID,
  MAIN_ARRANGEMENT_TITLE,
  SET_PLAYER_MAX,
  performerOf,
  performerPlayer,
  trackPerformer,
  setColorOf,
  slotOf,
  SCRIPT_RESERVE_FRAMES,
  arrangementFromChecks,
  arrangementOf,
  repairMigratedArrangement,
  blockingFindings,
  checkId,
  goalLoops,
  goalSlug,
  healthRuleProblem,
  leafName,
  loopIfRequired,
  requiresOf,
  uniqueLinks,
  type AcceptanceRule,
  type AnyHubRecord,
  type Arrangement,
  type ArrangementInfo,
  type ArrangementRecord,
  type Performer,
  type SetModel,
  type Slot,
  type CheckMeta,
  type CheckMetaRecord,
  type CheckRef,
  type Goal,
  type GoalDraft,
  type GoalImport,
  type GoalPriority,
  type GoalRequirement,
  type GoalUpdate,
  type HealthRule,
  type HubExport,
  type HubImportReport,
  type HubRecordType,
  type SignOff,
  type TrackerLink,
  type UnsupportedCheck,
} from "./contract.js";
import type { GoalStore, GoalStoreChange } from "./goal-store.js";
import { bad, canonical, checkRefOf, conflict, isObject, linksOf, notFound, readLinks } from "./util.js";

/** Where an older Studio kept goals in the project, one JSON file each. */
export const LEGACY_GOALS_DIR = "QA/Goals";
/** The import record's id, and the health rule's. */
export const LEGACY_IMPORT_ID = "qa-goals";
/** The record of taking in an older Studio's sign-offs.json. */
export const LEGACY_SIGNOFFS_ID = "sign-offs";
export const HEALTH_RULE_ID = "health";
/** The longest title, description and note kept. */
const MAX_TITLE = 200;
const MAX_TEXT = 20_000;
/** A new id's random ending: four base-36 characters after a dash. */
const SUFFIX = 4;
/** The types an export carries; the import record stays with the store it describes. */
const EXPORTED: readonly HubRecordType[] = ["goal", "check", "signoff", "rules", "arrangement"];
/** A loop in words: "a > b > a". */
const loopText = (p: string[]): string => p.join(" > ");

const str = (v: unknown, max = MAX_TEXT): string | undefined => (typeof v === "string" && v.trim() ? v.trim().slice(0, max) : undefined);

function acceptanceOf(v: unknown, fallback: AcceptanceRule = DEFAULT_ACCEPTANCE): AcceptanceRule {
  const o = isObject(v) ? v : {};
  const runs = Number(o.stableRuns);
  return {
    stableRuns: Number.isInteger(runs) && runs >= 1 && runs <= 10 ? runs : fallback.stableRuns,
    signOff: typeof o.signOff === "boolean" ? o.signOff : fallback.signOff,
  };
}

/** Checks without duplicates, the first of each id kept. */
function uniqueChecks(checks: CheckRef[]): CheckRef[] {
  const seen = new Set<string>();
  return checks.filter((c) => {
    const id = checkId(c);
    if (seen.has(id)) return false;
    seen.add(id);
    return true;
  });
}

/** What reading a goal gave: the goal, what was wrong (each left out), an older file's "parent", and checks' titles and links found on the goal. */
export interface ReadGoal {
  goal: Goal;
  problems: string[];
  parent?: string;
  metas: CheckMetaRecord[];
}

/**
 * A goal from JSON: a record's data, or a file an older Studio wrote. Fields
 * of the wrong type are left out and named in problems. Null-ish input, or
 * one without a title, is no goal at all.
 */
export function readGoal(raw: unknown, id: string): ReadGoal | { problem: string } {
  if (!isObject(raw)) return { problem: "it is not a JSON object" };
  if (typeof raw.schema === "number" && raw.schema > HUB_SCHEMA) return { problem: `it was written by a newer ue-mcp (schema ${raw.schema})` };
  if (raw.id !== undefined && raw.id !== id) return { problem: `its id "${String(raw.id)}" is not ${id}` };
  const title = str(raw.title, MAX_TITLE);
  if (!title) return { problem: "it has no title" };
  const problems: string[] = [];
  const checks: CheckRef[] = [];
  const unsupported: UnsupportedCheck[] = [];
  const metas: CheckMetaRecord[] = [];
  if (raw.checks !== undefined && !Array.isArray(raw.checks)) problems.push("checks is not a list");
  for (const [i, c] of (Array.isArray(raw.checks) ? raw.checks : []).entries()) {
    const ref = checkRefOf(c);
    if (ref) {
      checks.push(ref);
      // An older Studio kept a check's title and links on the goal: they move to the check's own record.
      const o = c as Record<string, unknown>;
      const links = readLinks(o.links);
      for (const p of links.problems) problems.push(`check ${i + 1} ${p}`);
      const t = str(o.title, MAX_TITLE);
      if (t || links.links.length) metas.push({ check: { kind: ref.kind, target: ref.target }, ...(t ? { title: t } : {}), ...(links.links.length ? { links: links.links } : {}) });
    } else if (isObject(c) && typeof c.kind === "string" && c.kind !== "script" && typeof c.target === "string" && c.target.trim()) {
      unsupported.push({ kind: c.kind, target: c.target.trim(), ...(c.required === false ? { required: false } : {}) });
    } else {
      problems.push(`check ${i + 1} is not a clip: { kind: "script", target: "/Game/..." }`);
    }
  }
  for (const u of Array.isArray(raw.unsupported) ? raw.unsupported : []) {
    if (isObject(u) && typeof u.kind === "string" && typeof u.target === "string") unsupported.push({ kind: u.kind, target: u.target, ...(u.required === false ? { required: false } : {}) });
  }
  const goalLinks = readLinks(raw.links);
  problems.push(...goalLinks.problems);
  const requires: GoalRequirement[] = [];
  if (raw.requires !== undefined && !Array.isArray(raw.requires)) problems.push("requires is not a list");
  for (const [i, r] of (Array.isArray(raw.requires) ? raw.requires : []).entries()) {
    const rid = isObject(r) ? r.goal : undefined;
    if (typeof rid !== "string" || !GOAL_ID.test(rid) || rid === id) {
      problems.push(`required track ${i + 1} is not another track's { goal: id }`);
      continue;
    }
    if (!requires.some((x) => x.goal === rid)) requires.push({ goal: rid, ...((r as Record<string, unknown>).required === false ? { required: false } : {}) });
  }
  let arrangement: Arrangement | undefined;
  if (raw.arrangement !== undefined) {
    const a = arrangementOf(raw.arrangement);
    for (const f of blockingFindings(a.findings)) problems.push(`arrangement: ${f.message}`);
    arrangement = a.arrangement;
  }
  if (raw.priority !== undefined && !GOAL_PRIORITIES.includes(raw.priority as GoalPriority)) problems.push(`priority is one of ${GOAL_PRIORITIES.join(", ")}`);
  if (raw.due !== undefined && raw.due !== null && !(typeof raw.due === "string" && GOAL_DUE.test(raw.due))) problems.push("due is a date, YYYY-MM-DD");
  const parent = typeof raw.parent === "string" && GOAL_ID.test(raw.parent) && raw.parent !== id ? raw.parent : undefined;
  const goal: Goal = {
    schema: HUB_SCHEMA,
    id,
    kind: raw.kind === "milestone" ? "milestone" : "goal",
    title,
    ...(str(raw.description) ? { description: str(raw.description) } : {}),
    ...(str(raw.owner, MAX_TITLE) ? { owner: str(raw.owner, MAX_TITLE) } : {}),
    ...(GOAL_PRIORITIES.includes(raw.priority as GoalPriority) ? { priority: raw.priority as GoalPriority } : {}),
    ...(typeof raw.due === "string" && GOAL_DUE.test(raw.due) ? { due: raw.due } : {}),
    ...(goalLinks.links.length ? { links: goalLinks.links } : {}),
    acceptance: acceptanceOf(raw.acceptance),
    ...(requires.length ? { requires } : {}),
    checks: uniqueChecks(checks),
    ...(unsupported.length ? { unsupported } : {}),
    ...(raw.archived === true ? { archived: true } : {}),
    ...(arrangement ? { arrangement } : {}),
    ...setFieldsOf(raw, raw.kind === "milestone" ? "milestone" : "goal"),
  };
  return { goal, problems, ...(parent ? { parent } : {}), metas };
}

/** How a track's performer is stored: a player as `player` (left out for 0), anything else as `performer`. */
function storedPerformer(t: { player?: number; performer?: Performer }): Partial<Goal> {
  const p = t.performer ?? trackPerformer(t);
  if (p.kind !== "player") return { performer: p };
  return p.client ? { player: p.client } : {};
}

/** A track's player from JSON: a whole number from 1 to SET_PLAYER_MAX, or 0 (left out) otherwise. */
function playerOf(v: unknown): number {
  return typeof v === "number" && Number.isInteger(v) && v > 0 && v <= SET_PLAYER_MAX ? v : 0;
}

/** The set fields a goal record holds (order, color, player, mute, solo, slots), read leniently. */
function setFieldsOf(raw: Record<string, unknown>, kind: Goal["kind"]): Partial<Goal> {
  const out: Partial<Goal> = {};
  if (typeof raw.order === "number" && Number.isFinite(raw.order)) out.order = raw.order;
  if (typeof raw.arrangementId === "string" && GOAL_ID.test(raw.arrangementId) && raw.arrangementId !== MAIN_ARRANGEMENT_ID) out.arrangementId = raw.arrangementId;
  const color = setColorOf(raw.color);
  if (color) out.color = color;
  if (kind === "goal") {
    const player = playerOf(raw.player);
    if (player) out.player = player;
    // A performer that is not a player is stored as one; a player stays in `player`, as every track before #2137 was.
    const performer = performerOf(raw.performer);
    if (performer && performer.kind !== "player") {
      out.performer = performer;
      delete out.player;
    } else if (performer?.kind === "player" && performer.client > 0) out.player = performer.client;
    if (raw.mute === true) out.mute = true;
    if (raw.solo === true) out.solo = true;
  } else if (Array.isArray(raw.slots)) {
    const slots: Slot[] = [];
    for (const s of raw.slots) {
      const slot = slotOf(s);
      if (slot && !slots.some((x) => x.track === slot.track)) slots.push(slot);
    }
    if (slots.length) out.slots = slots;
  }
  return out;
}

/** A goal's checks from its arrangement: the step-list playtests it uses, in order, keeping what each said of required. */
function checksFromArrangement(a: Arrangement, had: readonly CheckRef[]): CheckRef[] {
  const out: CheckRef[] = [];
  for (const i of [...a.instances].sort((x, y) => x.start - y.start)) {
    if (i.kind !== "script" || out.some((c) => c.target === i.target)) continue;
    const before = had.find((c) => c.target === i.target);
    out.push({ kind: "script", target: i.target, ...(before?.required === false ? { required: false } : {}) });
  }
  return out;
}

/** A check's own record from JSON, or why it is not one. */
function checkMetaOf(raw: unknown, id: string): CheckMetaRecord | string {
  if (!isObject(raw)) return "it is not a JSON object";
  const ref = checkRefOf(raw.check);
  if (!ref || checkId(ref) !== id) return `it does not name the clip ${id}`;
  const links = readLinks(raw.links);
  if (links.problems.length) return links.problems.join("; ");
  if (raw.title !== undefined && typeof raw.title !== "string") return "its title is not text";
  const title = str(raw.title, MAX_TITLE);
  return { check: { kind: ref.kind, target: ref.target }, ...(title ? { title } : {}), ...(links.links.length ? { links: links.links } : {}) };
}

function isSignOff(v: unknown): v is SignOff {
  const s = v as Partial<SignOff> | null;
  return !!s && typeof s.goalId === "string" && typeof s.by === "string" && typeof s.at === "string" && Array.isArray(s.runIds) && s.runIds.every((r) => typeof r === "string") && typeof s.requirements === "string" && (typeof s.revision === "string" || s.revision === null);
}

/** A record's data read by its type: the normalized record, or why it does not read. */
function recordDataOf(r: AnyHubRecord): AnyHubRecord | string {
  switch (r.type) {
    case "goal": {
      const g = readGoal(r.data, r.id);
      if ("problem" in g) return g.problem;
      return g.problems.length ? g.problems.join("; ") : { ...r, data: g.goal };
    }
    case "check": {
      const m = checkMetaOf(r.data, r.id);
      return typeof m === "string" ? m : { ...r, data: m };
    }
    case "signoff":
      return isSignOff(r.data) ? r : "it is not a sign-off";
    case "rules": {
      if (r.id !== HEALTH_RULE_ID) return `the health rule's id is ${HEALTH_RULE_ID}`;
      const d = r.data as Partial<HealthRule> | undefined;
      const problem = healthRuleProblem(d);
      if (problem) return problem;
      return d!.flakyWindow === undefined || d!.staleAfterDays === undefined ? "the rule needs staleAfterDays and flakyWindow" : r;
    }
    case "import":
      return r;
    case "arrangement": {
      const a = arrangementRecordOf(r.data);
      return typeof a === "string" ? a : { ...r, data: a };
    }
  }
}

/** An arrangement's record from JSON, or why it does not read. */
function arrangementRecordOf(raw: unknown): ArrangementRecord | string {
  if (!isObject(raw)) return "it is not a JSON object";
  const title = str(raw.title, MAX_TITLE);
  if (!title) return "it has no title";
  const level = levelOf(raw.level);
  return { title, order: typeof raw.order === "number" && Number.isFinite(raw.order) ? raw.order : 0, ...(level ? { level } : {}) };
}

/** A level as a map's package path, "/Game/Maps/Arena" from "/Game/Maps/Arena.Arena" too; undefined when it is not one. */
export function levelOf(v: unknown): string | undefined {
  if (typeof v !== "string") return undefined;
  const s = v.trim();
  if (!s.startsWith("/") || /\s/.test(s) || s.length > 400) return undefined;
  const dot = s.lastIndexOf(".");
  return dot > s.lastIndexOf("/") ? s.slice(0, dot) : s;
}

/** A record from an import file: its envelope and data, normalized, or why it does not read. */
function importedRecordOf(v: unknown): AnyHubRecord | string {
  if (!isObject(v)) return "it is not a JSON object";
  if (!EXPORTED.includes(v.type as HubRecordType)) return `its type is one of ${EXPORTED.join(", ")}`;
  if (v.schema !== HUB_SCHEMA) return typeof v.schema === "number" && v.schema > HUB_SCHEMA ? "it was written by a newer ue-mcp" : `its schema is ${HUB_SCHEMA}`;
  if (typeof v.id !== "string" || !v.id) return "it has no id";
  if (!Number.isInteger(v.rev) || (v.rev as number) < 1 || typeof v.at !== "string" || typeof v.origin !== "string" || !v.origin) return "it has no rev, at and origin";
  if (v.deleted !== undefined) return "an export holds no removed records";
  return recordDataOf(v as unknown as AnyHubRecord);
}

export interface GoalsOptions {
  store: GoalStore;
  /** The project, for the one-time import of QA/Goals. */
  projectDir: string;
  /** Where an older Studio kept this computer's sign-offs (sign-offs.json), taken in once. */
  legacySignOffs?: string;
  /** Records changed here. */
  changed?: (goalId?: string) => void;
  now?: () => string;
  /** Where a track's arrangement ends, its clips' lengths known; a rough end otherwise. */
  arrangementEnd?: (a: Arrangement) => number;
}

export class Goals {
  private readonly store: GoalStore;
  private readonly now: () => string;
  /** Records in the store that did not read, each named once. */
  private readonly unreadable: string[] = [];
  /** Each stored version as read (recordDataOf); a new version is a new object, so it is read again. */
  private readonly checked = new WeakMap<AnyHubRecord, AnyHubRecord | string>();

  constructor(private readonly o: GoalsOptions) {
    this.store = o.store;
    this.now = o.now ?? (() => new Date().toISOString());
    this.importLegacy();
    this.importLegacySignOffs();
  }

  // ── Reading ───────────────────────────────────────────────────────

  /** The live records of a type whose data reads; the rest are named in warnings. */
  private live<T extends HubRecordType>(type: T): Array<Extract<AnyHubRecord, { type: T }>> {
    const out: Array<Extract<AnyHubRecord, { type: T }>> = [];
    for (const r of this.store.list(type) as unknown as AnyHubRecord[]) {
      // Every read goes through here (a goal by id, each status): a version is checked once, not on every read.
      let read = this.checked.get(r);
      if (read === undefined) this.checked.set(r, (read = recordDataOf(r)));
      if (typeof read !== "string") {
        out.push(read as Extract<AnyHubRecord, { type: T }>);
        continue;
      }
      const note = `The ${type} record ${r.id} was not read: ${read}`;
      if (!this.unreadable.includes(note)) this.unreadable.push(note);
    }
    return out;
  }

  list(): Goal[] {
    return this.live("goal").map((r) => r.data!);
  }

  get(id: string): Goal | undefined {
    return this.list().find((g) => g.id === id);
  }

  private must(id: string): Goal {
    const g = this.get(id);
    if (!g) throw notFound(`no track ${id}`);
    return g;
  }

  /** What the overview warns of: records that did not read, goals required but gone, loops. */
  warnings(): string[] {
    const goals = this.list();
    const out = [...this.store.warnings(), ...this.unreadable];
    const ids = new Set(goals.map((g) => g.id));
    for (const g of goals) for (const r of requiresOf(g)) if (!ids.has(r.goal)) out.push(`${g.title} requires the track ${r.goal}, which is not here`);
    // A loop counts once around for status and is reported here.
    for (const cycle of goalLoops(goals).cycles) out.push(`Tracks require each other in a loop (${loopText(cycle)}), so the last step is left out of their status`);
    return out;
  }

  /** The one-time import of QA/Goals, when it happened. */
  imported(): GoalImport | null {
    return this.store.get("import", LEGACY_IMPORT_ID)?.data ?? null;
  }

  // ── Writing goals ─────────────────────────────────────────────────

  private save(goals: Goal[], extra: GoalStoreChange[] = []): void {
    this.store.write([...goals.map((g): GoalStoreChange => ({ type: "goal", id: g.id, data: clean(g) })), ...extra]);
    this.o.changed?.(goals.length === 1 && extra.length === 0 ? goals[0].id : undefined);
  }

  /** A new id: the asked one when free, else the title's slug, a dash and SUFFIX random base-36 characters. */
  private freshId(asked: unknown, title: string): string {
    const taken = (id: string): boolean => this.store.held("goal", id);
    if (typeof asked === "string" && asked) {
      if (!GOAL_ID.test(asked)) throw bad("id must be lowercase letters, digits and dashes, at most 64 characters");
      if (taken(asked)) throw bad(`a track ${asked} exists, or did`);
      return asked;
    }
    const base = goalSlug(title, 64 - SUFFIX - 1);
    for (;;) {
      const id = `${base}-${(crypto.randomBytes(4).readUInt32BE(0) % 36 ** SUFFIX).toString(36).padStart(SUFFIX, "0")}`;
      if (!taken(id)) return id;
    }
  }

  /** Check the fields a draft or update gives, and return them normalized. Throws a 400 for any that is wrong. */
  private fields(u: Record<string, unknown>, id: string | null): Partial<Goal> {
    const out: Partial<Goal> = {};
    if (u.title !== undefined) {
      const t = str(u.title, MAX_TITLE);
      if (!t) throw bad("title must be a non-empty string");
      out.title = t;
    }
    if (u.description !== undefined) {
      if (u.description !== null && typeof u.description !== "string") throw bad("description must be a string");
      out.description = str(u.description);
    }
    if (u.owner !== undefined) {
      if (u.owner !== null && typeof u.owner !== "string") throw bad("owner must be a string or null");
      out.owner = str(u.owner, MAX_TITLE) ?? null;
    }
    if (u.priority !== undefined) {
      if (u.priority !== null && !GOAL_PRIORITIES.includes(u.priority as GoalPriority)) throw bad(`priority must be one of ${GOAL_PRIORITIES.join(", ")}, or null`);
      out.priority = (u.priority ?? undefined) as GoalPriority | undefined;
    }
    if (u.due !== undefined) {
      if (u.due !== null && (typeof u.due !== "string" || !GOAL_DUE.test(u.due))) throw bad("due must be a date, YYYY-MM-DD, or null");
      out.due = (u.due as string | null) ?? null;
    }
    if (u.acceptance !== undefined) {
      if (!isObject(u.acceptance)) throw bad("acceptance must be an object");
      const a = u.acceptance;
      if (a.stableRuns !== undefined && !(Number.isInteger(a.stableRuns) && (a.stableRuns as number) >= 1 && (a.stableRuns as number) <= 10)) throw bad("acceptance.stableRuns must be a whole number from 1 to 10");
      if (a.staleAfterDays !== undefined) throw bad("staleness is the project's health rule, not a track's: PUT /hub/rules");
      if (a.signOff !== undefined && typeof a.signOff !== "boolean") throw bad("acceptance.signOff must be a boolean");
      out.acceptance = acceptanceOf(a, id ? this.get(id)?.acceptance : DEFAULT_ACCEPTANCE);
    }
    if (u.checks !== undefined) out.checks = checksOf(u.checks);
    if (u.links !== undefined) out.links = u.links === null ? [] : linksOf(u.links);
    if (u.archived !== undefined) {
      if (typeof u.archived !== "boolean") throw bad("archived must be a boolean");
      out.archived = u.archived;
    }
    if (u.unsupported !== undefined) throw bad("unsupported checks come only from an import");
    if (u.signOff !== undefined) throw bad("a sign-off is not part of the track: give it with /sign-off");
    if (u.requires !== undefined) out.requires = this.requiresList(u.requires, id);
    return out;
  }

  /** A requires list from a body: each { goal, required? } naming a goal here, each once. */
  private requiresList(v: unknown, self: string | null): GoalRequirement[] {
    if (v === null) return [];
    if (!Array.isArray(v)) throw bad("requires must be a list of { goal, required? }");
    const out: GoalRequirement[] = [];
    for (const [i, r] of v.entries()) {
      const one = this.requirementOf(r, `required track ${i + 1}`);
      if (one.goal === self) throw bad("a track cannot require itself");
      if (!out.some((x) => x.goal === one.goal)) out.push(one);
    }
    return out;
  }

  /** One { goal, required? } from a body, naming a goal here. */
  private requirementOf(r: unknown, what = "body"): GoalRequirement {
    const o = typeof r === "string" ? { goal: r } : r;
    if (!isObject(o) || typeof o.goal !== "string" || !o.goal) throw bad(`${what} must be { goal, required? }`);
    if (o.required !== undefined && typeof o.required !== "boolean") throw bad(`${what}: required must be a boolean`);
    if (!this.get(o.goal)) throw bad(`no track ${o.goal}`);
    return { goal: o.goal, ...(o.required === false ? { required: false } : {}) };
  }

  /** The parents a body gives: goal ids here, each once, never the goal itself. */
  private parentsOf(v: unknown, self: string | null): string[] {
    if (!Array.isArray(v) || v.some((p) => typeof p !== "string")) throw bad("parents must be a list of track ids");
    const out = [...new Set(v as string[])];
    for (const p of out) {
      if (p === self) throw bad("a track cannot require itself");
      if (!this.get(p)) throw bad(`no track ${p}`);
    }
    return out;
  }

  /** Refuse goals as they would be when one requires another in a loop: 400 naming the loop. */
  private refuseLoops(next: Goal[]): void {
    const graph = new Map(this.list().map((g) => [g.id, g]));
    const before = new Map(graph);
    for (const g of next) graph.set(g.id, g);
    const all = [...graph.values()];
    for (const g of next) {
      const had = new Set(requiresOf(before.get(g.id) ?? {}).map((r) => r.goal));
      for (const r of requiresOf(g)) {
        if (had.has(r.goal)) continue;
        const loop = loopIfRequired(all, g.id, r.goal);
        if (loop) throw bad(`that would make tracks require each other in a loop: ${loopText(loop)}`);
      }
    }
  }

  /** The parents as they would be with `id` required by exactly `parents`: only the goals that change. */
  private parentChanges(id: string, parents: string[]): Goal[] {
    const out: Goal[] = [];
    for (const g of this.list()) {
      const reqs = requiresOf(g);
      const has = reqs.some((r) => r.goal === id);
      const wants = parents.includes(g.id);
      if (has !== wants) out.push({ ...g, requires: wants ? [...reqs, { goal: id }] : reqs.filter((r) => r.goal !== id) });
    }
    return out;
  }

  /** Create a goal; `parents` in the draft each gain it at the end of their requires. */
  create(draft: unknown): Goal {
    if (!isObject(draft)) throw bad("body must be a track: { title, ... }");
    const d = draft as Partial<GoalDraft> & Record<string, unknown>;
    if (!str(d.title, MAX_TITLE)) throw bad("title must be a non-empty string");
    if (d.kind !== undefined && d.kind !== "goal" && d.kind !== "milestone") throw bad("kind must be goal or milestone");
    const f = this.fields(d, null);
    const id = this.freshId(d.id, f.title!);
    if (f.requires?.some((r) => r.goal === id)) throw bad("a track cannot require itself");
    // A track made outside the set goes to the arrangement named, else the first one.
    const arrangementId = this.resolveArrangement(d.arrangementId);
    const goal: Goal = { ...f, schema: HUB_SCHEMA, id, kind: d.kind ?? "goal", title: f.title!, acceptance: f.acceptance ?? { ...DEFAULT_ACCEPTANCE }, checks: f.checks ?? [], ...(arrangementId !== MAIN_ARRANGEMENT_ID ? { arrangementId } : {}) };
    const parents = d.parents !== undefined ? this.parentsOf(d.parents, null) : [];
    const changes = this.parentChanges(id, parents);
    this.refuseLoops([goal, ...changes]);
    this.save([goal, ...changes]);
    return this.must(id);
  }

  /** Change a goal; `parents`, when given, makes exactly those goals require it. */
  update(id: string, u: unknown): Goal {
    const had = this.must(id);
    if (!isObject(u)) throw bad("body must be the fields to change");
    const upd = u as GoalUpdate & Record<string, unknown>;
    if (upd.kind !== undefined && upd.kind !== "goal" && upd.kind !== "milestone") throw bad("kind must be goal or milestone");
    if (upd.id !== undefined && upd.id !== id) throw bad("a track's id does not change");
    const next: Goal = { ...had, ...this.fields(upd, id), kind: upd.kind ?? had.kind, schema: HUB_SCHEMA, id };
    const changes = upd.parents !== undefined ? this.parentChanges(id, this.parentsOf(upd.parents, id)) : [];
    this.refuseLoops([next, ...changes]);
    this.save([next, ...changes]);
    return this.must(id);
  }

  /** Remove a goal and its sign-off. Goals that required it stop requiring it; the goals it required stay. */
  delete(id: string): void {
    this.must(id);
    const parents = this.parentChanges(id, []);
    const signOffs = this.live("signoff").filter((s) => s.data!.goalId === id).map((s): GoalStoreChange => ({ type: "signoff", id: s.id, data: null }));
    this.store.write([{ type: "goal", id, data: null }, ...parents.map((g): GoalStoreChange => ({ type: "goal", id: g.id, data: clean(g) })), ...signOffs]);
    this.o.changed?.(id);
  }

  /** A goal requiring one more goal, at the end of its list. One it requires already keeps its place and takes the required given. */
  addRequire(id: string, body: unknown): Goal {
    const had = this.must(id);
    const r = this.requirementOf(body);
    if (r.goal === id) throw bad("a track cannot require itself");
    const reqs = requiresOf(had);
    const at = reqs.findIndex((x) => x.goal === r.goal);
    const asked = isObject(body) ? body.required : undefined;
    if (at >= 0) {
      if (asked === undefined || (reqs[at].required !== false) === asked) return had;
      this.save([{ ...had, requires: reqs.map((x, i) => (i === at ? r : x)) }]);
      return this.must(id);
    }
    const next = { ...had, requires: [...reqs, r] };
    this.refuseLoops([next]);
    this.save([next]);
    return this.must(id);
  }

  removeRequire(id: string, goal: string): Goal {
    const had = this.must(id);
    const reqs = requiresOf(had);
    if (!reqs.some((r) => r.goal === goal)) throw notFound(`track ${id} does not require ${goal}`);
    this.save([{ ...had, requires: reqs.filter((r) => r.goal !== goal) }]);
    return this.must(id);
  }

  /** The goals a goal requires in a new order: body { order } names each of them once. */
  orderRequires(id: string, body: unknown): Goal {
    const had = this.must(id);
    const reqs = requiresOf(had);
    const order = orderOf(body, reqs.map((r) => r.goal), "required track");
    this.save([{ ...had, requires: order.map((g) => reqs.find((r) => r.goal === g)!) }]);
    return this.must(id);
  }

  /** A goal's checks in a new order: body { order } names each check id once. */
  orderChecks(id: string, body: unknown): Goal {
    const had = this.must(id);
    const order = orderOf(body, had.checks.map(checkId), "check");
    this.save([{ ...had, checks: order.map((c) => had.checks.find((x) => checkId(x) === c)!) }]);
    return this.must(id);
  }

  addChecks(id: string, checks: unknown): Goal {
    const had = this.must(id);
    const add = checksOf(checks);
    if (add.length === 0) throw bad("body must be { checks: [{ kind, target }] } with at least one check");
    const known = new Set(had.checks.map(checkId));
    const fresh = add.filter((c) => !known.has(checkId(c)));
    if (fresh.length === 0) return had;
    // A track places each new playtest after its last instance, so a client that adds checks keeps working.
    const arrangement = had.arrangement ? appendScripts(had.arrangement, fresh.map((c) => c.target), this.o.arrangementEnd?.(had.arrangement) ?? roughEnd(had.arrangement)) : undefined;
    this.save([{ ...had, checks: [...had.checks, ...fresh], ...(arrangement ? { arrangement } : {}) }]);
    return this.must(id);
  }

  removeCheck(id: string, check: string): Goal {
    const had = this.must(id);
    const checks = had.checks.filter((c) => checkId(c) !== check);
    if (checks.length === had.checks.length) throw notFound(`track ${id} has no clip ${check}`);
    const gone = had.checks.find((c) => checkId(c) === check)!;
    const arrangement = had.arrangement ? { ...had.arrangement, instances: had.arrangement.instances.filter((i) => !(i.kind === "script" && i.target === gone.target)) } : undefined;
    this.save([{ ...had, checks, ...(arrangement ? { arrangement } : {}) }]);
    return this.must(id);
  }

  /** A track's arrangement replaced, already validated; its checks follow the step-list playtests it uses. */
  setArrangement(id: string, arrangement: Arrangement): Goal {
    const had = this.must(id);
    this.save([{ ...had, arrangement, checks: checksFromArrangement(arrangement, had.checks) }]);
    return this.must(id);
  }

  /**
   * Lay every goal that has no arrangement out from its checks, end to end,
   * each reserving its newest run's duration. Done once per goal: a goal with
   * an arrangement, even an empty one, is never laid out again, except a
   * track still exactly as an older migration left it with a reservation of
   * hours (#2127), which is laid out again with sane lengths. Scenes keep
   * their requires as their tracks and need nothing. Returns how many changed.
   */
  migrateArrangements(durationMs: (target: string) => number | null | undefined): number {
    const all = this.list();
    const tracks = all.filter((g) => g.kind === "goal");
    const scenes = all.filter((g) => g.kind === "milestone");
    const next: Goal[] = [];
    const layout = (g: Goal): Arrangement | undefined => {
      if (g.kind !== "goal") return undefined;
      const targets = g.checks.map((c) => c.target);
      if (!g.arrangement) return arrangementFromChecks(targets, durationMs);
      return repairMigratedArrangement(g.arrangement, targets, durationMs) ?? undefined;
    };
    // Tracks and scenes without a place take the next one, in the store's order.
    const place = (list: Goal[]) => {
      let at = list.reduce((m, g) => Math.max(m, (g.order ?? -1) + 1), 0);
      for (const g of list) {
        const n = g.order === undefined ? at++ : g.order;
        const a = layout(g);
        if (g.order === undefined || a) next.push({ ...g, order: n, ...(a ? { arrangement: a } : {}) });
      }
    };
    place(tracks);
    place(scenes);
    if (next.length) this.save(next);
    return next.length;
  }

  // ── Arrangements ──────────────────────────────────────────────────

  /**
   * Every arrangement, in order. The first one, "main", needs no record until
   * it is renamed or given a level; a project never has none, so with every
   * record removed "main" stands again.
   */
  arrangements(): ArrangementInfo[] {
    const out: ArrangementInfo[] = this.live("arrangement").map((r) => ({ id: r.id, title: r.data!.title, order: r.data!.order, level: r.data!.level ?? null }));
    if (!out.some((a) => a.id === MAIN_ARRANGEMENT_ID) && (!this.store.held("arrangement", MAIN_ARRANGEMENT_ID) || out.length === 0)) out.push({ id: MAIN_ARRANGEMENT_ID, title: MAIN_ARRANGEMENT_TITLE, order: 0, level: null });
    return out.sort((a, b) => a.order - b.order || a.title.localeCompare(b.title));
  }

  /** The arrangement a request that names none gets: the first one. */
  defaultArrangement(): string {
    return this.arrangements()[0].id;
  }

  /** An arrangement by id, else by exact title ignoring case; the first one when none is asked for. 404 when none matches. */
  resolveArrangement(asked: unknown): string {
    const list = this.arrangements();
    if (asked === undefined || asked === null || asked === "") return list[0].id;
    if (typeof asked !== "string") throw bad("arrangement must be an arrangement's id or name");
    const hit = list.find((a) => a.id === asked) ?? list.find((a) => a.title.toLowerCase() === asked.trim().toLowerCase());
    if (!hit) throw notFound(`no arrangement ${asked} (arrangements: ${list.map((a) => `${a.id} "${a.title}"`).join(", ")})`);
    return hit.id;
  }

  arrangementInfo(id: string): ArrangementInfo {
    const a = this.arrangements().find((x) => x.id === id);
    if (!a) throw notFound(`no arrangement ${id}`);
    return a;
  }

  /** The arrangement a track or scene plays in. One naming an arrangement that is gone shows in the first one, so nothing is lost. */
  arrangementOf(g: Pick<Goal, "arrangementId">, known: ReadonlySet<string> = new Set(this.arrangements().map((a) => a.id))): string {
    const id = g.arrangementId ?? MAIN_ARRANGEMENT_ID;
    return known.has(id) ? id : this.defaultArrangement();
  }

  /** The arrangement of the track or scene with this id, or null when there is none. */
  arrangementOfGoal(id: string): string | null {
    const g = this.get(id);
    return g ? this.arrangementOf(g) : null;
  }

  private freshArrangementId(title: string): string {
    const base = goalSlug(title, 64 - SUFFIX - 1) || "arrangement";
    for (;;) {
      const id = `${base}-${(crypto.randomBytes(4).readUInt32BE(0) % 36 ** SUFFIX).toString(36).padStart(SUFFIX, "0")}`;
      if (!this.store.held("arrangement", id) && id !== MAIN_ARRANGEMENT_ID) return id;
    }
  }

  private writeArrangement(a: ArrangementInfo, extra: GoalStoreChange[] = []): void {
    this.store.write([{ type: "arrangement", id: a.id, data: { title: a.title, order: a.order, ...(a.level ? { level: a.level } : {}) } }, ...extra]);
    this.o.changed?.();
  }

  private arrangementFields(body: Record<string, unknown>): { title?: string; level?: string | null } {
    const out: { title?: string; level?: string | null } = {};
    if (body.title !== undefined) {
      const t = str(body.title, MAX_TITLE);
      if (!t) throw bad("title must be a non-empty string");
      out.title = t;
    }
    if (body.level !== undefined) {
      if (body.level === null || body.level === "") out.level = null;
      else {
        const level = levelOf(body.level);
        if (!level) throw bad("level must be a map's asset path, such as /Game/Maps/Arena, or null to clear it");
        out.level = level;
      }
    }
    return out;
  }

  /** A new, empty arrangement at the end of the list. */
  createArrangement(body: unknown): ArrangementInfo {
    if (!isObject(body)) throw bad("body must be { title, level? }");
    const f = this.arrangementFields(body);
    if (!f.title) throw bad("title must be a non-empty string");
    const order = this.arrangements().reduce((m, a) => Math.max(m, a.order + 1), 0);
    const a: ArrangementInfo = { id: this.freshArrangementId(f.title), title: f.title, order, level: f.level ?? null };
    this.writeArrangement(a);
    return this.arrangementInfo(a.id);
  }

  /** Rename an arrangement or set its level; null clears the level. */
  updateArrangement(id: string, body: unknown): ArrangementInfo {
    const had = this.arrangementInfo(id);
    if (!isObject(body)) throw bad("body must be { title?, level? }");
    const f = this.arrangementFields(body);
    this.writeArrangement({ ...had, ...(f.title ? { title: f.title } : {}), ...(f.level !== undefined ? { level: f.level } : {}) });
    return this.arrangementInfo(id);
  }

  /**
   * A copy of an arrangement just after it: its tracks and scenes with new
   * ids, each track's instances as they are, each scene playing the copies of
   * its tracks. Clips are shared, never copied; sign-offs and links stay with
   * the originals.
   */
  duplicateArrangement(id: string, body: unknown): ArrangementInfo {
    const src = this.arrangementInfo(id);
    if (body !== undefined && body !== null && !isObject(body)) throw bad("body must be { title? }");
    const f = isObject(body) ? this.arrangementFields({ title: body.title }) : {};
    const title = f.title ?? `${src.title} copy`.slice(0, MAX_TITLE);
    // It goes right after the original: the ones after move down one.
    const after = this.arrangements().filter((a) => a.order > src.order);
    const copy: ArrangementInfo = { id: this.freshArrangementId(title), title, order: src.order + 1, level: src.level };
    const m = this.setModel(id);
    const taken = new Set<string>();
    const fresh = (t: string): string => {
      for (;;) {
        const n = this.newId(t);
        if (!taken.has(n)) {
          taken.add(n);
          return n;
        }
      }
    };
    const ids = new Map<string, string>([...m.tracks, ...m.scenes].map((x) => [x.id, fresh(x.title)]));
    const goals: Goal[] = [];
    for (const t of m.tracks) {
      const base = this.get(t.id)!;
      const arrangement: Arrangement = { rate: t.arrangement.rate, instances: t.arrangement.instances.map((i) => ({ ...i })) };
      goals.push({ schema: HUB_SCHEMA, id: ids.get(t.id)!, kind: "goal", title: t.title, ...(base.description ? { description: base.description } : {}), acceptance: { ...base.acceptance }, checks: checksFromArrangement(arrangement, base.checks), arrangement, order: t.order, ...(t.color ? { color: t.color } : {}), ...storedPerformer(t), ...(t.mute ? { mute: true } : {}), ...(t.solo ? { solo: true } : {}), arrangementId: copy.id });
    }
    for (const s of m.scenes) {
      const base = this.get(s.id)!;
      const requires = requiresOf(base).filter((r) => ids.has(r.goal)).map((r) => ({ ...r, goal: ids.get(r.goal)! }));
      const slots = s.slots.filter((x) => ids.has(x.track)).map((x) => ({ ...x, track: ids.get(x.track)! }));
      goals.push({ schema: HUB_SCHEMA, id: ids.get(s.id)!, kind: "milestone", title: s.title, ...(base.description ? { description: base.description } : {}), acceptance: { ...base.acceptance }, checks: [], order: s.order, ...(s.color ? { color: s.color } : {}), ...(requires.length ? { requires } : {}), ...(slots.length ? { slots } : {}), arrangementId: copy.id });
    }
    const moved = after.map((a): GoalStoreChange => ({ type: "arrangement", id: a.id, data: { title: a.title, order: a.order + 1, ...(a.level ? { level: a.level } : {}) } }));
    this.writeArrangement(copy, [...moved, ...goals.map((g): GoalStoreChange => ({ type: "goal", id: g.id, data: clean(g) }))]);
    return this.arrangementInfo(copy.id);
  }

  /** The ids of the tracks and scenes an arrangement holds, archived ones included. */
  arrangementGoals(id: string): string[] {
    const known = new Set(this.arrangements().map((a) => a.id));
    return this.list().filter((g) => this.arrangementOf(g, known) === id).map((g) => g.id);
  }

  /**
   * Delete an arrangement with its tracks and scenes (each as delete() does).
   * Clips are never deleted. The last arrangement stays: a project always has
   * one to open.
   */
  deleteArrangement(id: string): void {
    const a = this.arrangementInfo(id);
    if (this.arrangements().length <= 1) throw conflict(`"${a.title}" is the project's only arrangement. Create another arrangement first, then delete this one.`);
    for (const g of this.arrangementGoals(id)) if (this.get(g)) this.delete(g);
    // Main may have no record yet: one is written first, so its removal is held and it does not stand again.
    if (!this.store.get("arrangement", id)) this.store.write([{ type: "arrangement", id, data: { title: a.title, order: a.order } }]);
    this.store.write([{ type: "arrangement", id, data: null }]);
    this.o.changed?.();
  }

  /** The set of one arrangement (the first one when none is named): its live tracks and scenes, in order. Archived ones are left out. */
  setModel(arrangementId?: string): SetModel {
    const aid = arrangementId ?? this.defaultArrangement();
    const known = new Set(this.arrangements().map((a) => a.id));
    const live = this.list().filter((g) => !g.archived && this.arrangementOf(g, known) === aid);
    const byOrder = (a: Goal, b: Goal) => (a.order ?? 0) - (b.order ?? 0);
    return {
      tracks: live.filter((g) => g.kind === "goal").sort(byOrder).map((g) => ({ id: g.id, title: g.title, order: g.order ?? 0, ...(g.color ? { color: g.color } : {}), player: performerPlayer(trackPerformer(g)), performer: trackPerformer(g), mute: g.mute === true, solo: g.solo === true, arrangement: g.arrangement ?? { rate: 60, instances: [] } })),
      scenes: live.filter((g) => g.kind === "milestone").sort(byOrder).map((g) => ({ id: g.id, title: g.title, order: g.order ?? 0, ...(g.color ? { color: g.color } : {}), tracks: requiresOf(g).map((r) => r.goal).filter((id) => live.some((x) => x.id === id && x.kind === "goal")), slots: g.slots ?? [] })),
    };
  }

  /** A fresh goal id for a new track or scene. */
  newId(title: string): string {
    return this.freshId(undefined, title);
  }

  /**
   * Write a set back, as applySetOps left it: new tracks and scenes created,
   * gone ones deleted (each as delete() does), the rest updated with their
   * set fields. Nothing else on a goal changes.
   */
  applySet(next: SetModel, arrangementId: string = this.defaultArrangement()): void {
    const had = new Map(this.list().map((g) => [g.id, g]));
    const before = this.setModel(arrangementId);
    // A new track or scene joins the arrangement being edited.
    const mine = arrangementId === MAIN_ARRANGEMENT_ID ? {} : { arrangementId };
    const keep = new Set([...next.tracks.map((t) => t.id), ...next.scenes.map((s) => s.id)]);
    for (const x of [...before.tracks, ...before.scenes]) if (!keep.has(x.id)) this.delete(x.id);
    const writes: Goal[] = [];
    const now = new Map(this.list().map((g) => [g.id, g]));
    const strip = (g: Goal): Goal => {
      const out = { ...g };
      for (const k of ["color", "player", "performer", "mute", "solo", "slots"] as const) delete out[k];
      return out;
    };
    for (const t of next.tracks) {
      const base = now.get(t.id) ?? had.get(t.id) ?? ({ schema: HUB_SCHEMA, id: t.id, kind: "goal", title: t.title, acceptance: { ...DEFAULT_ACCEPTANCE }, checks: [], ...mine } as Goal);
      writes.push({ ...strip(base), title: t.title, order: t.order, ...(t.color ? { color: t.color } : {}), ...storedPerformer(t), ...(t.mute ? { mute: true } : {}), ...(t.solo ? { solo: true } : {}), arrangement: t.arrangement, checks: checksFromArrangement(t.arrangement, base.checks) });
    }
    for (const s of next.scenes) {
      const base = now.get(s.id) ?? had.get(s.id) ?? ({ schema: HUB_SCHEMA, id: s.id, kind: "milestone", title: s.title, acceptance: { ...DEFAULT_ACCEPTANCE }, checks: [], ...mine } as Goal);
      // Its tracks are its requires; what else it required (another scene) stays after them.
      const kept = requiresOf(base).filter((r) => now.get(r.goal)?.kind !== "goal" && !s.tracks.includes(r.goal));
      const requires = [...s.tracks.map((t) => requiresOf(base).find((r) => r.goal === t) ?? { goal: t }), ...kept];
      writes.push({ ...strip(base), title: s.title, order: s.order, ...(s.color ? { color: s.color } : {}), ...(s.slots.length ? { slots: s.slots } : {}), requires });
    }
    const changed = writes.filter((g) => canonical(clean(g)) !== canonical(clean(now.get(g.id) ?? ({} as Goal))));
    if (changed.length) this.save(changed);
  }

  /** Whether the goal requires one of its checks (GoalCheckUpdate). */
  updateGoalCheck(id: string, check: string, u: unknown): Goal {
    const had = this.must(id);
    const at = had.checks.findIndex((c) => checkId(c) === check);
    if (at < 0) throw notFound(`track ${id} has no clip ${check}`);
    if (!isObject(u) || typeof u.required !== "boolean") throw bad("body must be { required: boolean }");
    const checks = had.checks.slice();
    const { required: _r, ...ref } = checks[at];
    checks[at] = u.required ? ref : { ...ref, required: false };
    this.save([{ ...had, checks }]);
    return this.must(id);
  }

  // ── What a check says about itself ────────────────────────────────

  metas(): Map<string, CheckMeta> {
    return new Map(this.live("check").map((r) => [r.id, metaOf(r.data!)]));
  }

  meta(check: string): CheckMeta | undefined {
    const r = this.live("check").find((x) => x.id === check);
    return r ? metaOf(r.data!) : undefined;
  }

  /** Change what a check says about itself (CheckMetaUpdate); nothing left removes the record. */
  updateMeta(ref: CheckRef, u: unknown): CheckMeta {
    if (!isObject(u)) throw bad("body must be { title?, links? }");
    const id = checkId(ref);
    const next: CheckMeta = { ...this.meta(id) };
    if (u.title !== undefined) {
      if (u.title !== null && typeof u.title !== "string") throw bad("title must be a string, or null to clear it");
      const t = str(u.title, MAX_TITLE);
      if (t) next.title = t;
      else delete next.title;
    }
    if (u.links !== undefined) {
      const links = u.links === null ? [] : linksOf(u.links);
      if (links.length) next.links = links;
      else delete next.links;
    }
    const empty = !next.title && !next.links?.length;
    this.store.write([{ type: "check", id, data: empty ? null : { check: { kind: ref.kind, target: ref.target }, ...next } }]);
    this.o.changed?.();
    return next;
  }

  // ── Sign-offs ─────────────────────────────────────────────────────

  /** Each goal's sign-off, by goal id. */
  signOffs(): Map<string, SignOff> {
    const out = new Map<string, SignOff>();
    for (const r of this.live("signoff")) {
      const had = out.get(r.data!.goalId);
      if (!had || had.at < r.data!.at) out.set(r.data!.goalId, r.data!);
    }
    return out;
  }

  /** Record a sign-off, in place of the goal's earlier one. */
  signOff(s: SignOff): void {
    const earlier = this.live("signoff").filter((r) => r.data!.goalId === s.goalId).map((r): GoalStoreChange => ({ type: "signoff", id: r.id, data: null }));
    this.store.write([...earlier, { type: "signoff", id: `signoff-${crypto.randomUUID()}`, data: s }]);
    this.o.changed?.(s.goalId);
  }

  /** Remove a goal's sign-off. Returns whether it had one. */
  clearSignOff(goalId: string): boolean {
    const gone = this.live("signoff").filter((r) => r.data!.goalId === goalId).map((r): GoalStoreChange => ({ type: "signoff", id: r.id, data: null }));
    if (gone.length === 0) return false;
    this.store.write(gone);
    this.o.changed?.(goalId);
    return true;
  }

  // ── The health rule ───────────────────────────────────────────────

  rules(): HealthRule {
    return this.live("rules").find((r) => r.id === HEALTH_RULE_ID)?.data ?? { ...DEFAULT_HEALTH_RULE };
  }

  setRules(body: unknown): HealthRule {
    const problem = healthRuleProblem(body);
    if (problem) throw bad(problem);
    const next: HealthRule = { ...this.rules(), ...(body as Partial<HealthRule>) };
    this.store.write([{ type: "rules", id: HEALTH_RULE_ID, data: next }]);
    this.o.changed?.();
    return next;
  }

  // ── Export and import ─────────────────────────────────────────────

  /** The whole store as one file: every live record that reads, at its newest version. */
  exportAll(): HubExport {
    return {
      format: HUB_EXPORT_FORMAT,
      schema: HUB_SCHEMA,
      exportedAt: this.now(),
      origin: this.store.origin,
      records: EXPORTED.flatMap((t) => this.live(t) as AnyHubRecord[]),
    };
  }

  /**
   * Import an export, merged by id (HubImportReport): a record this store
   * never held is added as the file has it; the same content is left alone;
   * other content, or a record removed here, keeps this store's version and
   * is reported. A file with any record that does not read changes nothing.
   */
  importAll(body: unknown): HubImportReport {
    if (!isObject(body) || body.format !== HUB_EXPORT_FORMAT) throw bad(`this is not an exported hub file (format ${HUB_EXPORT_FORMAT})`);
    if (body.schema !== HUB_SCHEMA) throw bad(typeof body.schema === "number" && body.schema > HUB_SCHEMA ? "the file was exported by a newer ue-mcp" : `the file's schema is not ${HUB_SCHEMA}`);
    if (!Array.isArray(body.records)) throw bad("the file has no records list");
    const records: AnyHubRecord[] = [];
    const problems: string[] = [];
    const seen = new Set<string>();
    for (const [i, v] of body.records.entries()) {
      const r = importedRecordOf(v);
      const name = isObject(v) && typeof v.id === "string" ? `${String(v.type)} ${v.id}` : `record ${i + 1}`;
      if (typeof r === "string") problems.push(`${name}: ${r}`);
      else if (seen.has(`${r.type}:${r.id}`)) problems.push(`${name}: it is in the file twice`);
      else {
        seen.add(`${r.type}:${r.id}`);
        records.push(r);
      }
    }
    if (problems.length) throw bad(`nothing was imported: ${problems.slice(0, 20).join("; ")}${problems.length > 20 ? `; and ${problems.length - 20} more` : ""}`);
    const report: HubImportReport = { added: 0, unchanged: 0, conflicts: [] };
    const adopt: AnyHubRecord[] = [];
    for (const r of records) {
      const here = this.store.get(r.type, r.id) as AnyHubRecord | undefined;
      // Removed here is this store's version too: an older export never brings the record back.
      if (!here && this.store.held(r.type, r.id)) report.conflicts.push({ type: r.type, id: r.id, title: `${this.recordTitle(r)} (removed here)` });
      else if (!here) {
        adopt.push(r);
        report.added++;
      } else if (canonical(here.data) === canonical(r.data)) report.unchanged++;
      else report.conflicts.push({ type: r.type, id: r.id, title: this.recordTitle(here) });
    }
    if (adopt.length) {
      this.store.adopt(adopt);
      this.o.changed?.();
    }
    return report;
  }

  /** A record's name, as a person reads it in an import report. */
  private recordTitle(r: AnyHubRecord): string {
    switch (r.type) {
      case "goal": return r.data!.title;
      case "check": return r.data!.title ?? leafName(r.data!.check.target);
      case "signoff": return `Sign-off of ${this.get(r.data!.goalId)?.title ?? r.data!.goalId}`;
      case "rules": return "Health rule";
      case "import": return "Import of QA/Goals";
      case "arrangement": return `Arrangement ${r.data!.title}`;
    }
  }

  // ── The one-time import of QA/Goals ───────────────────────────────

  /**
   * An empty store in a project with QA/Goals files takes their goals, once.
   * The files are only read. Nothing happens without files, so a store that
   * is empty today still takes them the day a checkout brings them.
   */
  private importLegacy(): void {
    if (this.store.list("goal").length || this.store.held("import", LEGACY_IMPORT_ID)) return;
    const dir = path.join(this.o.projectDir, LEGACY_GOALS_DIR);
    let names: string[];
    try {
      names = fs.readdirSync(dir).filter((n) => n.toLowerCase().endsWith(".json") && n.toLowerCase() !== "goal.schema.json").sort();
    } catch {
      return;
    }
    if (names.length === 0) return;
    const problems: string[] = [];
    const read: ReadGoal[] = [];
    for (const name of names) {
      const id = name.slice(0, -".json".length);
      const where = `${LEGACY_GOALS_DIR}/${name}`;
      if (!GOAL_ID.test(id)) {
        problems.push(`${where}: a goal's file name is its id, lowercase letters, digits and dashes`);
        continue;
      }
      let raw: unknown;
      try {
        raw = JSON.parse(fs.readFileSync(path.join(dir, name), "utf-8").replace(/^﻿/, ""));
      } catch (e) {
        problems.push(`${where} is not valid JSON: ${e instanceof Error ? e.message : String(e)}`);
        continue;
      }
      const g = readGoal(raw, id);
      if ("problem" in g) {
        problems.push(`${where} was not imported: ${g.problem}`);
        continue;
      }
      for (const p of g.problems) problems.push(`${where}: ${p}, so it was left out`);
      if (isObject(raw) && Array.isArray(raw.tags) && raw.tags.length) problems.push(`${where}: its tags were not imported`);
      read.push(g);
    }
    // An older file's "parent" is an entry at the end of the parent's requires, in file order.
    const byId = new Map(read.map((g) => [g.goal.id, g.goal]));
    for (const g of read) {
      if (!g.parent) continue;
      const parent = byId.get(g.parent);
      if (!parent) {
        problems.push(`${LEGACY_GOALS_DIR}/${g.goal.id}.json: its parent "${g.parent}" is not there`);
        continue;
      }
      if (!requiresOf(parent).some((r) => r.goal === g.goal.id)) byId.set(parent.id, { ...parent, requires: [...requiresOf(parent), { goal: g.goal.id }] });
    }
    // A check's title and links, kept on each goal that listed it, become its one record: the first title, every link.
    const metas = new Map<string, CheckMetaRecord>();
    for (const m of read.flatMap((g) => g.metas)) {
      const id = checkId(m.check);
      const had = metas.get(id);
      metas.set(id, { check: m.check, ...((had?.title ?? m.title) ? { title: had?.title ?? m.title } : {}), ...(had?.links || m.links ? { links: uniqueLinks([...(had?.links ?? []), ...(m.links ?? [])]) } : {}) });
    }
    const goals = [...byId.values()];
    const marker: GoalImport = { from: LEGACY_GOALS_DIR, at: this.now(), goals: goals.map((g) => g.id), problems };
    this.store.write([
      ...goals.map((g): GoalStoreChange => ({ type: "goal", id: g.id, data: clean(g) })),
      ...[...metas].map(([id, data]): GoalStoreChange => ({ type: "check", id, data })),
      { type: "import", id: LEGACY_IMPORT_ID, data: marker },
    ]);
  }

  /**
   * Sign-offs an older Studio kept in one file on this computer become
   * sign-off records, once: the same shape, for goals the store has. The file
   * is only read.
   */
  private importLegacySignOffs(): void {
    const file = this.o.legacySignOffs;
    if (!file || this.store.held("import", LEGACY_SIGNOFFS_ID)) return;
    let saved: unknown;
    try {
      saved = JSON.parse(fs.readFileSync(file, "utf-8"));
    } catch {
      return;
    }
    const problems: string[] = [];
    const taken: SignOff[] = [];
    for (const s of Array.isArray(saved) ? saved : []) {
      if (!isSignOff(s)) problems.push("an entry is not a sign-off");
      else if (!this.get(s.goalId)) problems.push(`the sign-off of ${s.goalId} names no track here`);
      else if (!taken.some((t) => t.goalId === s.goalId)) taken.push(s);
    }
    const marker: GoalImport = { from: path.basename(file), at: this.now(), goals: taken.map((s) => s.goalId), problems };
    this.store.write([
      ...taken.map((data): GoalStoreChange => ({ type: "signoff", id: `signoff-${crypto.randomUUID()}`, data })),
      { type: "import", id: LEGACY_SIGNOFFS_ID, data: marker },
    ]);
  }
}

/** Checks from a body: each a playtest, the first of each kept. */
function checksOf(v: unknown): CheckRef[] {
  if (!Array.isArray(v)) throw bad("checks must be a list of { kind: script, target }");
  return uniqueChecks(v.map((c, i) => {
    const ref = checkRefOf(c);
    if (!ref) throw bad(`check ${i + 1} must be { kind: script, target }, the target a script's asset path`);
    return ref;
  }));
}

/** An arrangement's end without its clips' lengths: a clip instance with no out counts as ten seconds. */
function roughEnd(a: Arrangement): number {
  const reserve = Math.round((SCRIPT_RESERVE_FRAMES * a.rate) / 60);
  return a.instances.reduce((m, i) => Math.max(m, i.start + (i.kind === "script" ? i.length ?? reserve : i.out !== undefined ? i.out - (i.in ?? 0) : reserve)), 0);
}

/** Step-list instances for scripts, laid end to end after the arrangement's last instance. */
function appendScripts(a: Arrangement, targets: readonly string[], end: number): Arrangement {
  let next = a.instances.reduce((m, i) => Math.max(m, /^i(\d+)$/.test(i.id) ? Number(i.id.slice(1)) : 0), 0) + 1;
  let at = end;
  const length = Math.round((SCRIPT_RESERVE_FRAMES * a.rate) / 60);
  const added = targets.map((target) => {
    const inst = { id: `i${next++}`, kind: "script" as const, target, start: at, length };
    at += length;
    return inst;
  });
  return { ...a, instances: [...a.instances, ...added] };
}

/** A new order from body { order }: every id in `have` once, nothing else. */
function orderOf(body: unknown, have: string[], what: string): string[] {
  const order = isObject(body) ? body.order : undefined;
  if (!Array.isArray(order) || order.some((x) => typeof x !== "string")) throw bad(`body must be { order: [${what} ids] }`);
  const ids = order as string[];
  if (ids.length !== have.length || new Set(ids).size !== ids.length || ids.some((x) => !have.includes(x))) throw bad(`order must name each ${what} once: ${have.join(", ") || "there are none"}`);
  return ids;
}

const metaOf = (r: CheckMetaRecord): CheckMeta => ({ ...(r.title ? { title: r.title } : {}), ...(r.links?.length ? { links: r.links as TrackerLink[] } : {}) });

/** A goal without empty optional fields, so the record stays as short as the goal is. */
function clean(g: Goal): Goal {
  const out = { ...g } as Record<string, unknown>;
  for (const k of ["description", "owner", "due", "priority"]) if (out[k] === null || out[k] === undefined || out[k] === "") delete out[k];
  for (const k of ["requires", "links", "unsupported", "slots"]) if (Array.isArray(out[k]) && (out[k] as unknown[]).length === 0) delete out[k];
  for (const k of ["archived", "mute", "solo"]) if (out[k] === false) delete out[k];
  if (out.player === 0) delete out.player;
  return out as unknown as Goal;
}
