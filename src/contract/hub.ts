// @session independent: The Playtest Hub's contract: types and pure functions over their arguments. No state.
/**
 * The Playtest Hub's core (#2146). Composing and testing are separate:
 *
 * - A clip is a reusable performance for one performer kind: input lanes for
 *   a player; move, action and value lanes for an actor. A clip never asserts.
 * - A track is one performer and a timeline of clip instances.
 * - An arrangement is the scenario: a level, its tracks, one Checks track and
 *   named regions. It plays in one PIE session.
 * - A test is an arrangement or one of its regions. It passes when all of its
 *   checks pass.
 * - A suite is a named list of tests and suites. It owns health, pass-rate
 *   history, flaky marking and quarantine.
 *
 * Clips, arrangements and suites are text files in the project (HUB_DIR),
 * written by fileText so the same value always gives the same bytes. Runs,
 * the queue and schedules stay on the machine that ran them.
 *
 * Imports only its siblings, so `ue-mcp/hub` loads in a browser.
 */
import {
  PERFORMER_CAPABILITIES,
  PERFORMER_KINDS,
  clipLaneOf,
  performerLabel,
  performerOf,
  type ClipLane,
  type Performer,
  type PerformerKind,
} from "./hub-performers.js";

/** The version of the files under HUB_DIR. */
export const HUB_SCHEMA = 2 as const;
/** The project folder that holds the hub's files: clips/, arrangements/ and suites/ under it. */
export const HUB_DIR = "PlaytestHub";
export const HUB_KINDS = ["clip", "arrangement", "suite"] as const;
export type HubKind = (typeof HUB_KINDS)[number];
/** The folder of each kind under HUB_DIR. */
export const HUB_FOLDERS: { readonly [K in HubKind]: string } = { clip: "clips", arrangement: "arrangements", suite: "suites" };

/** The frame rate an arrangement counts in unless it says otherwise. */
export const DEFAULT_RATE = 60;
/** A clip, arrangement or suite id: also its file name. */
export const HUB_ID = /^[a-z0-9][a-z0-9-]{0,63}$/;
/** A track, instance, check or region id inside a file. */
export const PART_ID = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;
export const TITLE_MAX = 200;
export const MAX_INSTANCES = 500;
export const MAX_CHECKS = 200;
export const MAX_TRACKS = 32;

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const r3 = (n: number): number => Math.round(n * 1000) / 1000;
const num = (v: unknown): number | null => (typeof v === "number" && Number.isFinite(v) ? v : null);
const str = (v: unknown, max = TITLE_MAX): string => (typeof v === "string" ? v.trim().slice(0, max) : "");

/** An id made from a title: lowercase words joined by dashes. */
export function slugOf(title: string, max = 48): string {
  const s = title.toLowerCase().normalize("NFKD").replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, max).replace(/-+$/, "");
  return s || "untitled";
}

/** Something in a body or a file that is wrong, at a path such as `tracks[0].instances[2]`. A blocking one refuses the save or the run. */
export interface HubFinding {
  path: string;
  message: string;
  blocks: boolean;
  /** Ids that would fit, for an unknown one. */
  candidates?: string[];
}

// ---------- Clips ----------

export const INPUT_TYPES = ["bool", "axis1d", "axis2d", "axis3d"] as const;
export type InputType = (typeof INPUT_TYPES)[number];
export const INTERPS = ["linear", "constant", "cubic"] as const;

export interface InputNote {
  id: string;
  start: number;
  length: number;
  /** An axis lane's value held over the note. */
  value?: number | number[];
}
export interface InputKey {
  id: string;
  t: number;
  v: number | number[];
  interp?: (typeof INTERPS)[number];
}
/** One Input Action's lane: the raw value that reaches the action. A player's capability `input`. */
export interface InputLane {
  action: string;
  type: InputType;
  notes: InputNote[];
  keys: InputKey[];
}

/** A clip: a performance for one performer kind, in frames at its rate. It has no checks. */
export interface Clip {
  id: string;
  title: string;
  performer: PerformerKind;
  rate: number;
  /** Frames. */
  length: number;
  description?: string;
  /** Input Action lanes; a player clip only. */
  input: InputLane[];
  /** Move, action and value lanes. The track's performer drives them, so a lane names none. */
  lanes: ClipLane[];
}

export type ClipDraft = Partial<Omit<Clip, "id">> & { id?: string };

const vec = (v: unknown): number | number[] | null => (num(v) !== null ? r3(v as number) : Array.isArray(v) && v.length >= 1 && v.length <= 3 && v.every((x) => num(x) !== null) ? (v as number[]).map(r3) : null);

function inputLaneOf(v: unknown, path: string, out: HubFinding[]): InputLane | null {
  if (!isObj(v) || typeof v.action !== "string" || !v.action.startsWith("/")) {
    out.push({ path, message: `${path} needs action, an Input Action's asset path`, blocks: true });
    return null;
  }
  const type = (INPUT_TYPES as readonly string[]).includes(String(v.type)) ? (v.type as InputType) : null;
  if (!type) {
    out.push({ path: `${path}.type`, message: `${path}.type is one of ${INPUT_TYPES.join(", ")}`, blocks: true });
    return null;
  }
  const notes: InputNote[] = [];
  for (const [i, n] of (Array.isArray(v.notes) ? v.notes : []).entries()) {
    const start = isObj(n) ? num(n.start) : null;
    const length = isObj(n) ? num(n.length) : null;
    if (!isObj(n) || typeof n.id !== "string" || start === null || length === null || length < 0) {
      out.push({ path: `${path}.notes[${i}]`, message: `${path}.notes[${i}] needs id, start and a length of 0 or more`, blocks: true });
      continue;
    }
    const value = n.value === undefined ? null : vec(n.value);
    notes.push({ id: n.id, start: r3(start), length: r3(length), ...(value !== null ? { value } : {}) });
  }
  const keys: InputKey[] = [];
  for (const [i, k] of (Array.isArray(v.keys) ? v.keys : []).entries()) {
    const t = isObj(k) ? num(k.t) : null;
    const val = isObj(k) ? vec(k.v) : null;
    if (!isObj(k) || typeof k.id !== "string" || t === null || val === null) {
      out.push({ path: `${path}.keys[${i}]`, message: `${path}.keys[${i}] needs id, t and a numeric v`, blocks: true });
      continue;
    }
    const interp = (INTERPS as readonly string[]).includes(String(k.interp)) ? (k.interp as InputKey["interp"]) : undefined;
    keys.push({ id: k.id, t: r3(t), v: val, ...(interp && interp !== "linear" ? { interp } : {}) });
  }
  notes.sort((a, b) => a.start - b.start || a.id.localeCompare(b.id));
  keys.sort((a, b) => a.t - b.t || a.id.localeCompare(b.id));
  for (let i = 1; i < notes.length; i++) {
    if (notes[i]!.start < notes[i - 1]!.start + notes[i - 1]!.length) out.push({ path: `${path}.notes`, message: `${path}: notes ${notes[i - 1]!.id} and ${notes[i]!.id} overlap`, blocks: true });
  }
  return { action: v.action, type, notes, keys };
}

/** A lane's own times rounded and sorted, its performer dropped: the track names the performer. */
function laneNormal(l: ClipLane): ClipLane {
  if (l.kind === "move") return { id: l.id, kind: "move", points: l.points.map((p) => ({ ...p, t: r3(p.t), at: p.at.map(r3) as [number, number, number] })).sort((a, b) => a.t - b.t || a.id.localeCompare(b.id)) };
  if (l.kind === "action") return { id: l.id, kind: "action", calls: l.calls.map((c) => ({ ...c, t: r3(c.t) })).sort((a, b) => a.t - b.t || a.id.localeCompare(b.id)), ...(l.watch?.length ? { watch: l.watch } : {}) };
  return { id: l.id, kind: "value", target: l.target, type: l.type, keys: l.keys.map((k) => ({ ...k, t: r3(k.t) })).sort((a, b) => a.t - b.t || a.id.localeCompare(b.id)) };
}

/**
 * A clip from a body or a file, normalized, with what is wrong with it. A
 * `checks` field is refused: checks live on an arrangement's Checks track.
 * A lane its performer kind does not take refuses the clip.
 */
export function clipOf(v: unknown, id: string): { clip: Clip; findings: HubFinding[] } {
  const out: HubFinding[] = [];
  const o = isObj(v) ? v : {};
  if (!isObj(v)) out.push({ path: "", message: "a clip is a JSON object", blocks: true });
  if (o.checks !== undefined) out.push({ path: "checks", message: "a clip has no checks: put them on the arrangement's Checks track", blocks: true });
  const performer = (PERFORMER_KINDS as readonly string[]).includes(String(o.performer)) ? (o.performer as PerformerKind) : null;
  if (!performer) out.push({ path: "performer", message: `performer is the kind the clip is for: ${PERFORMER_KINDS.join(" or ")}`, blocks: true });
  const kind = performer ?? "player";
  const rate = num(o.rate) ?? DEFAULT_RATE;
  if (rate <= 0 || rate > 1000) out.push({ path: "rate", message: "rate is frames per second, more than 0 and at most 1000", blocks: true });
  const input = (Array.isArray(o.input) ? o.input : []).map((l, i) => inputLaneOf(l, `input[${i}]`, out)).filter((l): l is InputLane => !!l);
  const lanes: ClipLane[] = [];
  for (const [i, raw] of (Array.isArray(o.lanes) ? o.lanes : []).entries()) {
    const lane = clipLaneOf(raw);
    if (!lane) {
      out.push({ path: `lanes[${i}]`, message: `lanes[${i}] is a move, action or value lane with an id`, blocks: true });
      continue;
    }
    lanes.push(laneNormal(lane));
  }
  const takes = PERFORMER_CAPABILITIES[kind];
  if (input.length && !takes.includes("input")) out.push({ path: "input", message: `an ${kind} clip takes ${takes.join(", ")} lanes, not Input Action lanes`, blocks: true });
  for (const [i, l] of lanes.entries()) if (!takes.includes(l.kind)) out.push({ path: `lanes[${i}]`, message: `a ${kind} clip takes ${takes.join(", ")} lanes, and lane ${l.id} is a ${l.kind} lane`, blocks: true });
  const ids = new Set<string>();
  for (const l of lanes) {
    if (ids.has(l.id)) out.push({ path: "lanes", message: `two lanes have the id ${l.id}`, blocks: true });
    ids.add(l.id);
  }
  const ends = [
    ...input.flatMap((l) => [...l.notes.map((n) => n.start + n.length), ...l.keys.map((k) => k.t)]),
    ...lanes.flatMap((l) => (l.kind === "move" ? l.points.map((p) => p.t) : l.kind === "action" ? l.calls.map((c) => c.t) : l.keys.map((k) => k.t))),
  ];
  const content = ends.length ? Math.max(...ends) : 0;
  const given = num(o.length);
  const length = r3(given !== null && given > 0 ? Math.max(given, content) : content);
  const description = str(o.description, 4000);
  return {
    clip: { id, title: str(o.title) || id, performer: kind, rate: r3(rate), length, ...(description ? { description } : {}), input, lanes },
    findings: out,
  };
}

// ---------- Arrangements ----------

export interface ClipInstance {
  id: string;
  clip: string;
  /** The frame it starts on, at the arrangement's rate. */
  at: number;
}

export interface Track {
  id: string;
  title: string;
  performer: Performer;
  mute: boolean;
  solo: boolean;
  instances: ClipInstance[];
}

/**
 * How a check judges, from its start to its end:
 * - by_end: passes once the condition holds on any frame before the end.
 * - throughout: passes when it holds on every frame.
 * - hold: playback waits at the start until it holds, failing after its
 *   length (or `timeout` frames when the length is 0).
 * - manual: a person or an agent gives the verdict; PIE pauses until then.
 */
export const CHECK_MODES = ["by_end", "throughout", "hold", "manual"] as const;
export type CheckMode = (typeof CHECK_MODES)[number];

/** What a check reads: a value (as a script step's expect names them), an op and a target; `subject` an actor, the run's player when absent. */
export interface CheckExpect {
  value: string;
  op: string;
  target?: unknown;
  subject?: string;
}

export interface Check {
  id: string;
  title: string;
  at: number;
  length: number;
  mode: CheckMode;
  expect?: CheckExpect;
  /** A manual check: what to look at. */
  instruction?: string;
  /** A manual check only a person may judge. */
  personOnly?: boolean;
  /** A hold of length 0: the most frames it waits. */
  timeout?: number;
}

export interface Region {
  id: string;
  name: string;
  start: number;
  end: number;
}

export interface Arrangement {
  id: string;
  title: string;
  /** The map to play in, a package path; null plays the open one. */
  level: string | null;
  rate: number;
  description?: string;
  tracks: Track[];
  /** The Checks track. */
  checks: Check[];
  regions: Region[];
}

export type ArrangementDraft = Partial<Omit<Arrangement, "id">> & { id?: string };

/** How many arrangement frames a clip instance lasts. */
export const instanceLength = (clip: Pick<Clip, "length" | "rate"> | undefined, rate: number): number => (clip ? r3((clip.length * rate) / clip.rate) : 0);

function partId(v: unknown, fallback: string): string {
  return typeof v === "string" && PART_ID.test(v) ? v : fallback;
}

function checkOf(v: unknown, i: number, out: HubFinding[]): Check | null {
  const path = `checks[${i}]`;
  if (!isObj(v)) {
    out.push({ path, message: `${path} is an object`, blocks: true });
    return null;
  }
  const mode = (CHECK_MODES as readonly string[]).includes(String(v.mode)) ? (v.mode as CheckMode) : null;
  if (!mode) out.push({ path: `${path}.mode`, message: `${path}.mode is one of ${CHECK_MODES.join(", ")}`, blocks: true });
  const at = num(v.at) ?? 0;
  const length = num(v.length) ?? 0;
  if (at < 0 || length < 0) out.push({ path, message: `${path}: at and length are 0 or more`, blocks: true });
  const id = partId(v.id, `c${i + 1}`);
  const c: Check = { id, title: str(v.title) || id, at: r3(at), length: r3(length), mode: mode ?? "by_end" };
  if (mode === "manual") {
    const instruction = str(v.instruction, 2000);
    if (!instruction) out.push({ path: `${path}.instruction`, message: `${path}: a manual check needs an instruction saying what to look at`, blocks: true });
    c.instruction = instruction;
    if (v.personOnly === true) c.personOnly = true;
  } else if (mode) {
    const e = isObj(v.expect) ? v.expect : null;
    if (!e || typeof e.value !== "string" || !e.value || typeof e.op !== "string" || !e.op) out.push({ path: `${path}.expect`, message: `${path}: a ${mode} check needs expect { value, op, target? }`, blocks: true });
    else c.expect = { value: e.value, op: e.op, ...(e.target !== undefined ? { target: e.target } : {}), ...(typeof e.subject === "string" && e.subject ? { subject: e.subject } : {}) };
    const timeout = num(v.timeout);
    if (mode === "hold" && timeout !== null && timeout > 0) c.timeout = r3(timeout);
  }
  return c;
}

/**
 * An arrangement from a body or a file, normalized, with what is wrong with
 * it. `clips` finds a clip by id; given, instances are checked against it
 * (an unknown clip, a clip for another performer kind, overlap on a track).
 */
export function arrangementOf(v: unknown, id: string, clips?: (id: string) => Clip | undefined): { arrangement: Arrangement; findings: HubFinding[] } {
  const out: HubFinding[] = [];
  const o = isObj(v) ? v : {};
  if (!isObj(v)) out.push({ path: "", message: "an arrangement is a JSON object", blocks: true });
  const rate = num(o.rate) ?? DEFAULT_RATE;
  if (rate <= 0 || rate > 1000) out.push({ path: "rate", message: "rate is frames per second, more than 0 and at most 1000", blocks: true });
  const level = typeof o.level === "string" && o.level.trim() ? o.level.trim() : null;
  if (level && !level.startsWith("/")) out.push({ path: "level", message: "level is a map's package path, such as /Game/Maps/Arena", blocks: true });
  const tracks: Track[] = [];
  const rawTracks = Array.isArray(o.tracks) ? o.tracks : [];
  if (rawTracks.length > MAX_TRACKS) out.push({ path: "tracks", message: `an arrangement holds at most ${MAX_TRACKS} tracks`, blocks: true });
  let instanceCount = 0;
  for (const [ti, t] of rawTracks.entries()) {
    const tp = `tracks[${ti}]`;
    if (!isObj(t)) {
      out.push({ path: tp, message: `${tp} is an object`, blocks: true });
      continue;
    }
    const performer = performerOf(t.performer);
    if (!performer) out.push({ path: `${tp}.performer`, message: `${tp}.performer is {kind: "player", client} or {kind: "actor", actor}`, blocks: true });
    const tid = partId(t.id, `t${ti + 1}`);
    const instances: ClipInstance[] = [];
    for (const [ii, inst] of (Array.isArray(t.instances) ? t.instances : []).entries()) {
      const ip = `${tp}.instances[${ii}]`;
      const at = isObj(inst) ? num(inst.at) : null;
      if (!isObj(inst) || typeof inst.clip !== "string" || at === null || at < 0) {
        out.push({ path: ip, message: `${ip} needs clip (an id) and at (a frame, 0 or more)`, blocks: true });
        continue;
      }
      instances.push({ id: partId(inst.id, `i${ii + 1}`), clip: inst.clip, at: r3(at) });
    }
    instanceCount += instances.length;
    instances.sort((a, b) => a.at - b.at || a.id.localeCompare(b.id));
    tracks.push({ id: tid, title: str(t.title) || tid, performer: performer ?? { kind: "player", client: 0 }, mute: t.mute === true, solo: t.solo === true, instances });
  }
  if (instanceCount > MAX_INSTANCES) out.push({ path: "tracks", message: `an arrangement holds at most ${MAX_INSTANCES} clip instances`, blocks: true });
  const rawChecks = Array.isArray(o.checks) ? o.checks : [];
  if (rawChecks.length > MAX_CHECKS) out.push({ path: "checks", message: `the Checks track holds at most ${MAX_CHECKS} checks`, blocks: true });
  const checks = rawChecks.map((c, i) => checkOf(c, i, out)).filter((c): c is Check => !!c).sort((a, b) => a.at - b.at || a.id.localeCompare(b.id));
  const regions: Region[] = [];
  for (const [ri, r] of (Array.isArray(o.regions) ? o.regions : []).entries()) {
    const rp = `regions[${ri}]`;
    const start = isObj(r) ? num(r.start) : null;
    const end = isObj(r) ? num(r.end) : null;
    if (!isObj(r) || start === null || end === null || start < 0 || end <= start) {
      out.push({ path: rp, message: `${rp} needs start and an end after it`, blocks: true });
      continue;
    }
    const rid = partId(r.id, `r${ri + 1}`);
    regions.push({ id: rid, name: str(r.name) || rid, start: r3(start), end: r3(end) });
  }
  regions.sort((a, b) => a.start - b.start || a.id.localeCompare(b.id));
  for (let i = 1; i < regions.length; i++) if (regions[i]!.start < regions[i - 1]!.end) out.push({ path: "regions", message: `regions ${regions[i - 1]!.id} and ${regions[i]!.id} overlap`, blocks: true });
  const dupes = (what: string, ids: string[]) => {
    const seen = new Set<string>();
    for (const x of ids) {
      if (seen.has(x)) out.push({ path: what, message: `two ${what} have the id ${x}`, blocks: true });
      seen.add(x);
    }
  };
  dupes("tracks", tracks.map((t) => t.id));
  dupes("instances", tracks.flatMap((t) => t.instances.map((i) => i.id)));
  dupes("checks", checks.map((c) => c.id));
  dupes("regions", regions.map((r) => r.id));
  const description = str(o.description, 4000);
  const arrangement: Arrangement = { id, title: str(o.title) || id, level, rate: r3(rate), ...(description ? { description } : {}), tracks, checks, regions };
  if (clips) out.push(...instanceFindings(arrangement, clips));
  return { arrangement, findings: out };
}

/** Instances checked against their clips: each must exist, suit its track's performer and not overlap the one before it. */
export function instanceFindings(a: Arrangement, clips: (id: string) => Clip | undefined, known?: readonly string[]): HubFinding[] {
  const out: HubFinding[] = [];
  for (const [ti, t] of a.tracks.entries()) {
    let end = -Infinity;
    let prev = "";
    for (const [ii, inst] of t.instances.entries()) {
      const p = `tracks[${ti}].instances[${ii}]`;
      const clip = clips(inst.clip);
      if (!clip) {
        out.push({ path: p, message: `${t.title}: no clip ${inst.clip}`, blocks: true, ...(known ? { candidates: rank(inst.clip, known) } : {}) });
        continue;
      }
      if (clip.performer !== t.performer.kind) out.push({ path: p, message: `${t.title}: clip ${clip.id} is for a ${clip.performer}, and the track drives ${performerLabel(t.performer)}`, blocks: true });
      if (inst.at < end) out.push({ path: p, message: `${t.title}: ${inst.id} starts at ${inst.at}, before ${prev} ends at ${end}`, blocks: true });
      end = inst.at + instanceLength(clip, a.rate);
      prev = inst.id;
    }
  }
  return out;
}

/** The ids in `pool` closest to `word`, best first. */
export function rank(word: string, pool: readonly string[], n = 3): string[] {
  const w = word.toLowerCase();
  const score = (c: string) => {
    const s = c.toLowerCase();
    if (s === w) return 0;
    if (s.startsWith(w) || w.startsWith(s)) return 1;
    if (s.includes(w) || w.includes(s)) return 2;
    return 3 + Math.abs(s.length - w.length);
  };
  return [...pool].sort((a, b) => score(a) - score(b) || a.localeCompare(b)).slice(0, n);
}

/** The frame an arrangement ends on: its last instance, check or region. */
export function arrangementLength(a: Arrangement, clips: (id: string) => Clip | undefined): number {
  let end = 0;
  for (const t of a.tracks) for (const i of t.instances) end = Math.max(end, i.at + instanceLength(clips(i.clip), a.rate));
  for (const c of a.checks) end = Math.max(end, c.at + c.length);
  for (const r of a.regions) end = Math.max(end, r.end);
  return r3(end);
}

/** The tracks a run plays: none muted, and only the soloed ones when any is. */
export function audibleTracks<T extends Pick<Track, "mute" | "solo">>(tracks: readonly T[]): T[] {
  const solo = tracks.some((t) => t.solo);
  return tracks.filter((t) => (solo ? t.solo : !t.mute));
}

// ---------- Tests ----------

/** A test: an arrangement, or one region of it. */
export interface TestRef {
  arrangement: string;
  region?: string;
}

/** A test's id: the arrangement's id, or `<arrangement>:<region>`. */
export const testIdOf = (t: TestRef): string => (t.region ? `${t.arrangement}:${t.region}` : t.arrangement);

export function parseTestId(id: string): TestRef | null {
  const [arrangement, region, more] = id.split(":");
  if (more !== undefined || !arrangement || !HUB_ID.test(arrangement)) return null;
  if (region === undefined) return { arrangement };
  return PART_ID.test(region) ? { arrangement, region } : null;
}

/** The checks a test judges: all of an arrangement's, or those that start inside the region. */
export function testChecks(a: Arrangement, region?: string): Check[] {
  if (!region) return a.checks;
  const r = a.regions.find((x) => x.id === region);
  return r ? a.checks.filter((c) => c.at >= r.start && c.at < r.end) : [];
}

/** Every test an arrangement makes: itself, then each region. */
export const testsOf = (a: Pick<Arrangement, "id" | "regions">): TestRef[] => [{ arrangement: a.id }, ...a.regions.map((r) => ({ arrangement: a.id, region: r.id }))];

// ---------- Suites ----------

export type SuiteItem = { test: string } | { suite: string };

export interface Suite {
  id: string;
  title: string;
  description?: string;
  /** Tests (by test id) and suites (by id), in order. */
  items: SuiteItem[];
  /** Tests that run but do not count toward the suite's health. */
  quarantined: string[];
  /** Tests a person marked flaky, whatever their runs say. */
  flaky: string[];
}

export type SuiteDraft = Partial<Omit<Suite, "id">> & { id?: string };

/** A suite from a body or a file, normalized, with what is wrong with it. */
export function suiteOf(v: unknown, id: string): { suite: Suite; findings: HubFinding[] } {
  const out: HubFinding[] = [];
  const o = isObj(v) ? v : {};
  if (!isObj(v)) out.push({ path: "", message: "a suite is a JSON object", blocks: true });
  const items: SuiteItem[] = [];
  const seen = new Set<string>();
  for (const [i, it] of (Array.isArray(o.items) ? o.items : []).entries()) {
    const p = `items[${i}]`;
    if (isObj(it) && typeof it.test === "string" && parseTestId(it.test)) {
      if (!seen.has(`t:${it.test}`)) items.push({ test: it.test });
      seen.add(`t:${it.test}`);
    } else if (isObj(it) && typeof it.suite === "string" && HUB_ID.test(it.suite)) {
      if (it.suite === id) out.push({ path: p, message: "a suite cannot contain itself", blocks: true });
      else if (!seen.has(`s:${it.suite}`)) items.push({ suite: it.suite });
      seen.add(`s:${it.suite}`);
    } else out.push({ path: p, message: `${p} is { test: "<arrangement>[:<region>]" } or { suite: "<id>" }`, blocks: true });
  }
  const ids = (k: string) => [...new Set((Array.isArray(o[k]) ? o[k] : []).filter((x): x is string => typeof x === "string" && !!parseTestId(x)))].sort();
  const description = str(o.description, 4000);
  return { suite: { id, title: str(o.title) || id, ...(description ? { description } : {}), items, quarantined: ids("quarantined"), flaky: ids("flaky") }, findings: out };
}

/** A loop of suites through `id`, as the ids along it, or null. */
export function suiteLoop(suites: ReadonlyMap<string, Pick<Suite, "items">>, id: string): string[] | null {
  const walk = (at: string, path: string[]): string[] | null => {
    for (const it of suites.get(at)?.items ?? []) {
      if (!("suite" in it)) continue;
      if (it.suite === id) return [...path, id];
      if (path.includes(it.suite)) continue;
      const found = walk(it.suite, [...path, it.suite]);
      if (found) return found;
    }
    return null;
  };
  return walk(id, [id]);
}

/** Every test a suite holds, its own and its suites', once each in first-seen order, and the suite ids it could not find. */
export function suiteTests(suites: ReadonlyMap<string, Pick<Suite, "items">>, id: string): { tests: string[]; missingSuites: string[] } {
  const tests: string[] = [];
  const missing: string[] = [];
  const seen = new Set<string>();
  const walk = (at: string) => {
    if (seen.has(at)) return;
    seen.add(at);
    const s = suites.get(at);
    if (!s) {
      missing.push(at);
      return;
    }
    for (const it of s.items) {
      if ("test" in it) {
        if (!tests.includes(it.test)) tests.push(it.test);
      } else walk(it.suite);
    }
  };
  walk(id);
  return { tests, missingSuites: missing };
}

// ---------- Files ----------

function clipFile(c: Clip): Record<string, unknown> {
  return {
    schema: HUB_SCHEMA,
    title: c.title,
    performer: c.performer,
    rate: c.rate,
    length: c.length,
    ...(c.description ? { description: c.description } : {}),
    input: c.input.map((l) => ({ action: l.action, type: l.type, notes: l.notes.map((n) => ({ id: n.id, start: n.start, length: n.length, ...(n.value !== undefined ? { value: n.value } : {}) })), keys: l.keys.map((k) => ({ id: k.id, t: k.t, v: k.v, ...(k.interp ? { interp: k.interp } : {}) })) })),
    lanes: c.lanes,
  };
}

function arrangementFile(a: Arrangement): Record<string, unknown> {
  return {
    schema: HUB_SCHEMA,
    title: a.title,
    level: a.level,
    rate: a.rate,
    ...(a.description ? { description: a.description } : {}),
    tracks: a.tracks.map((t) => ({ id: t.id, title: t.title, performer: t.performer.kind === "player" ? { kind: "player", client: t.performer.client } : { kind: "actor", actor: t.performer.actor }, mute: t.mute, solo: t.solo, instances: t.instances.map((i) => ({ id: i.id, clip: i.clip, at: i.at })) })),
    checks: a.checks.map((c) => ({ id: c.id, title: c.title, at: c.at, length: c.length, mode: c.mode, ...(c.expect ? { expect: c.expect } : {}), ...(c.instruction ? { instruction: c.instruction } : {}), ...(c.personOnly ? { personOnly: true } : {}), ...(c.timeout ? { timeout: c.timeout } : {}) })),
    regions: a.regions.map((r) => ({ id: r.id, name: r.name, start: r.start, end: r.end })),
  };
}

function suiteFile(s: Suite): Record<string, unknown> {
  return { schema: HUB_SCHEMA, title: s.title, ...(s.description ? { description: s.description } : {}), items: s.items.map((it) => ("test" in it ? { test: it.test } : { suite: it.suite })), quarantined: s.quarantined, flaky: s.flaky };
}

/**
 * A file's text: fixed key order, two-space indent, LF, a final newline. The
 * id is the file name and is not repeated inside. Unordered lists (notes,
 * keys, instances, checks, regions) are sorted, so two people's edits merge
 * line by line.
 */
export function fileText(kind: "clip", v: Clip): string;
export function fileText(kind: "arrangement", v: Arrangement): string;
export function fileText(kind: "suite", v: Suite): string;
export function fileText(kind: HubKind, v: Clip | Arrangement | Suite): string {
  const body = kind === "clip" ? clipFile(v as Clip) : kind === "arrangement" ? arrangementFile(v as Arrangement) : suiteFile(v as Suite);
  return `${JSON.stringify(body, null, 2)}\n`;
}

// ---------- Runs (per machine) ----------

export type RunnerKind = "editor" | "headless";
export type RunSource = "person" | "agent" | "schedule";
export type CheckStatus = "pending" | "running" | "passed" | "failed" | "awaiting_verdict" | "skipped";
export type RunState = "starting" | "running" | "awaiting_verdict" | "passed" | "failed" | "error" | "stopped";
export const ENDED_STATES: readonly RunState[] = ["passed", "failed", "error", "stopped"];
export const runEnded = (s: RunState): boolean => ENDED_STATES.includes(s);

export interface CheckResult {
  id: string;
  status: CheckStatus;
  /** The frame it settled on. */
  frame?: number;
  message?: string;
  verdict?: { by: "person" | "agent"; note?: string; at: string };
}

/** The source control a project is under; "none" when the hub sees none. */
export type RunVcs = "git" | "perforce" | "plastic" | "svn" | "none";

/** Where the workspace is now. */
export interface CurrentProvenance {
  vcs: RunVcs | null;
  revision: string | null;
  dirty: number | null;
}

/** One run of one test, as this machine keeps it. */
export interface RunRecord {
  runId: string;
  test: string;
  title: string;
  runner: RunnerKind;
  source: RunSource;
  /** The queueing it came from: one per test, or one for every test of a suite. */
  batch: string;
  /** The suite queued, when it came from one. */
  suite?: string;
  startedAt: string;
  endedAt: string | null;
  state: RunState;
  checks: CheckResult[];
  frame?: number;
  error?: string;
  revision: string | null;
  dirty: number | null;
}

/** A finished run's state from its checks: any failed fails it, all passed passes it. */
export function stateFromChecks(checks: readonly Pick<CheckResult, "status">[]): RunState {
  if (checks.some((c) => c.status === "failed")) return "failed";
  if (checks.length > 0 && checks.every((c) => c.status === "passed" || c.status === "skipped")) return "passed";
  return "error";
}

// ---------- Health ----------

export type TestHealth = "running" | "failing" | "flaky" | "passing" | "never_run" | "missing";
export type SuiteHealth = "running" | "failing" | "flaky" | "never_run" | "passing" | "empty";
export const TEST_HEALTH_ORDER: readonly TestHealth[] = ["running", "failing", "missing", "flaky", "never_run", "passing"];

/** Flaky: mixed results among this many latest results on one revision. */
export const FLAKY_WINDOW = 5;
/** Pass rate: over this many latest results. */
export const PASS_RATE_WINDOW = 20;

const isResult = (r: Pick<RunRecord, "state">): boolean => r.state === "passed" || r.state === "failed";

/** Whether a test's latest results flip, and which checks flip between them. `runs` newest first. */
export function flakiness(runs: readonly RunRecord[], window = FLAKY_WINDOW): { flaky: boolean; checks: string[] } {
  const results = runs.filter(isResult);
  const rev = results[0]?.revision ?? null;
  const same = results.filter((r) => rev === null || r.revision === rev).slice(0, window);
  const flaky = same.some((r) => r.state === "passed") && same.some((r) => r.state === "failed");
  const statuses = new Map<string, Set<string>>();
  for (const r of same) for (const c of r.checks) if (c.status === "passed" || c.status === "failed") (statuses.get(c.id) ?? statuses.set(c.id, new Set()).get(c.id)!).add(c.status);
  return { flaky, checks: flaky ? [...statuses].filter(([, s]) => s.size > 1).map(([id]) => id).sort() : [] };
}

export function passRate(runs: readonly RunRecord[], window = PASS_RATE_WINDOW): number | null {
  const results = runs.filter(isResult).slice(0, window);
  return results.length ? r3(results.filter((r) => r.state === "passed").length / results.length) : null;
}

/** A test's health from its runs, newest first. */
export function testHealth(runs: readonly RunRecord[], o: { missing?: boolean; window?: number } = {}): TestHealth {
  if (o.missing) return "missing";
  if (runs[0] && !runEnded(runs[0].state)) return "running";
  const last = runs.find(isResult);
  if (!last) return "never_run";
  if (last.state === "failed") return "failing";
  return flakiness(runs, o.window).flaky ? "flaky" : "passing";
}

export interface RunSummary {
  runId: string;
  state: RunState;
  startedAt: string;
  endedAt: string | null;
}

export interface TestState {
  id: string;
  arrangement: string;
  region?: string;
  title: string;
  health: TestHealth;
  checks: number;
  passRate: number | null;
  flaky: { detected: boolean; marked: boolean; checks: string[] };
  /** Quarantined in some suite that holds it. */
  quarantined: boolean;
  last?: RunSummary;
}

/** One queueing of tests the suite holds: how many passed. */
export interface PassRatePoint {
  batch: string;
  at: string;
  passed: number;
  total: number;
  rate: number;
}

export interface SuiteState {
  id: string;
  title: string;
  description?: string;
  items: SuiteItem[];
  health: SuiteHealth;
  /** Over the counted tests' latest results. */
  passRate: number | null;
  /** Oldest first. */
  history: PassRatePoint[];
  /** Every test it holds, nested suites' included, in order. */
  tests: TestState[];
  quarantined: string[];
  flaky: string[];
  /** Tests and suites it names that do not exist. */
  missing: string[];
}

/** A suite's health over the tests that count (not quarantined): running, then failing, flaky, never run, passing. */
export function suiteHealth(tests: readonly Pick<TestState, "health" | "quarantined" | "flaky">[]): SuiteHealth {
  const counted = tests.filter((t) => !t.quarantined);
  if (!counted.length) return "empty";
  if (counted.some((t) => t.health === "running")) return "running";
  if (counted.some((t) => t.health === "failing" || t.health === "missing")) return "failing";
  if (counted.some((t) => t.health === "flaky" || t.flaky.marked)) return "flaky";
  if (counted.some((t) => t.health === "never_run")) return "never_run";
  return "passing";
}

/** The pass-rate history of a set of tests: one point per batch that ran any of them, oldest first. `runs` any order. */
export function passRateHistory(tests: ReadonlySet<string>, runs: readonly RunRecord[], max = 50): PassRatePoint[] {
  const by = new Map<string, PassRatePoint>();
  for (const r of runs) {
    if (!tests.has(r.test) || !isResult(r)) continue;
    const p = by.get(r.batch) ?? { batch: r.batch, at: r.startedAt, passed: 0, total: 0, rate: 0 };
    p.total++;
    if (r.state === "passed") p.passed++;
    if (r.startedAt < p.at) p.at = r.startedAt;
    by.set(r.batch, p);
  }
  return [...by.values()].map((p) => ({ ...p, rate: r3(p.passed / p.total) })).sort((a, b) => a.at.localeCompare(b.at)).slice(-max);
}

// ---------- Queue and schedules (per machine) ----------

export interface QueueItem {
  id: string;
  test: string;
  title: string;
  runner: RunnerKind;
  source: RunSource;
  batch: string;
  suite?: string;
  queuedAt: string;
  state: "queued" | "running";
  runId?: string;
}

/** Queue one test or every test of a suite. */
export interface QueueRequest {
  test?: string;
  suite?: string;
  runner?: RunnerKind;
  /** Put it at the front. */
  next?: boolean;
}

export interface QueueState {
  paused: boolean;
  /** Why the next editor item waits; empty when it does not. */
  waiting: OccupancyReason[];
  running: QueueItem[];
  queued: QueueItem[];
}

/** Run a suite or a test on chosen weekdays (0 Sunday) at a local time. */
export interface Schedule {
  id: string;
  title: string;
  suite?: string;
  test?: string;
  runner: RunnerKind;
  days: number[];
  /** HH:MM, local. */
  time: string;
  enabled: boolean;
  lastRunAt: string | null;
}

export type ScheduleDraft = Partial<Omit<Schedule, "id" | "lastRunAt">>;
export const SCHEDULE_TIME = /^([01]\d|2[0-3]):[0-5]\d$/;

// ---------- What else holds the editor ----------

export type OccupancyKind = "disconnected" | "operation" | "dialog" | "pie" | "agent" | "client" | "input" | "session" | "consent";
export interface OccupancyReason {
  kind: OccupancyKind;
  text: string;
}

/** The hub's hold on one editor while a queue item plays on it. Only the hub acts on what it holds: its run, and PIE when that run started it. */
export interface EditorLease {
  editor: string;
  purpose: string;
  item: string;
  runId?: string | null;
  pie?: boolean;
  since: string;
}

export interface HubLeaseEvent {
  lease: EditorLease | null;
}
export const HUB_LEASE = "hub.lease";

/** An editor call in flight, for the occupancy answer. */
export interface BusyCall {
  id: number;
  category: string;
  action: string;
  /** Daemon clock, epoch ms. */
  startedAt: number;
  /** agent: a client's own agent made it (HubClientOptions.agentCalls); client: an MCP client did. */
  source: "agent" | "client";
  sessionId?: string;
  client?: string;
  stopped: boolean;
}

/** An editor restart, rebuild, start or stop the daemon is running. */
export interface EditorOperation {
  op: "start" | "stop" | "restart" | "rebuild";
  step: string;
  startedAt: number;
  stepStartedAt: number;
}

export interface EditorOperationFailure {
  op: EditorOperation["op"];
  step: string;
  reason: string;
  at: number;
}

/** What is using the editor now. `now` is the daemon's clock. */
export interface BusyState {
  now: number;
  calls: BusyCall[];
  operation?: EditorOperation;
  failure?: EditorOperationFailure;
}

// ---------- Views ----------

export interface ClipSummary {
  id: string;
  title: string;
  performer: PerformerKind;
  rate: number;
  length: number;
  lanes: number;
  /** Arrangements that place it. */
  usedBy: string[];
}

export interface ArrangementSummary {
  id: string;
  title: string;
  level: string | null;
  tracks: number;
  checks: number;
  regions: Array<Pick<Region, "id" | "name">>;
  length: number;
  findings: HubFinding[];
}

/** A file under HUB_DIR that does not read. */
export interface FileProblem {
  kind: HubKind;
  id: string;
  file: string;
  findings: HubFinding[];
}

export interface HubOverview {
  clips: ClipSummary[];
  arrangements: ArrangementSummary[];
  suites: SuiteState[];
  /** Every test: each arrangement and each of its regions. */
  tests: TestState[];
  queue: QueueState;
  lease: EditorLease | null;
  recent: RunRecord[];
  schedules: Schedule[];
  problems: FileProblem[];
}

export interface ArrangementView {
  arrangement: Arrangement;
  length: number;
  findings: HubFinding[];
  /** The clips it places, by id. */
  clips: Record<string, Clip>;
  tests: TestState[];
}

export interface TestDetail {
  test: TestState;
  checks: Check[];
  runs: RunRecord[];
}

/**
 * What the editor plugin plays (the pie action arrangement_play): the arrangement with
 * the audible tracks only and every clip they place, so the plugin reads no
 * hub file. `region` plays one region and judges the checks inside it.
 */
export interface PlayRequest {
  runId: string;
  arrangement: Arrangement;
  clips: Record<string, Clip>;
  region?: string;
}

/** What the pie action arrangement_status answers. */
export interface PlayStatus {
  runId: string;
  state: "starting" | "running" | "awaiting_verdict" | "finished" | "stopped" | "error";
  frame?: number;
  checks: CheckResult[];
  error?: string;
  /** The run started Play In Editor. */
  pieStarted?: boolean;
}

export type HubChangedWhat = "clips" | "arrangements" | "suites" | "runs" | "queue" | "schedules";
export interface HubChanged {
  what: HubChangedWhat[];
  id?: string;
  runId?: string;
}
export const HUB_CHANGED = "hub.changed";
