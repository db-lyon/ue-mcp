// @session independent: The arrangement model: types and pure functions over their arguments. No state.
/**
 * Tracks and scenes as arrangements of clips (#2113).
 *
 * A track places instances of clips on one timeline: each instance refers to a
 * timeline clip (pie clip_*) or a step-list playtest (pie script_*) and copies
 * nothing out of it, so editing the clip changes every instance. A scene plays
 * several tracks together, each for its own player.
 *
 * Everything here is plain data and pure functions shared by the daemon and its
 * clients: the model, its text form, validation with candidates, the edit ops,
 * and the composition of a track or scene into the segments one run plays.
 */

/** Frames per second of game time a track is laid out in when it says nothing else. */
export const ARRANGEMENT_RATE = 60;
/** Frames a step-list instance takes up on its track before it has run: ten seconds. */
export const SCRIPT_RESERVE_FRAMES = 600;
/** The most instances one track holds. */
export const ARRANGEMENT_MAX_INSTANCES = 500;

export type InstanceKind = "clip" | "script";

/** A clip placed on a track. Times are frames at the track's rate. */
export interface Instance {
  /** "i1", "i2" and so on, stable and never reused on the track. */
  id: string;
  kind: InstanceKind;
  /** The asset path without the object name, such as /Game/PIEStudio/Clips/CL_Door. */
  target: string;
  start: number;
  /** Clip instances: frames trimmed off the clip's start, in the clip's own frames. */
  in?: number;
  /** Clip instances: the clip frame the instance ends at, in the clip's own frames; the clip's length when absent. */
  out?: number;
  /** Step-list instances: frames the instance takes up on the track. */
  length?: number;
  /** A display name; the clip's own otherwise. */
  name?: string;
  /** One of SET_COLORS (hub-set.ts). */
  color?: string;
}

export interface Arrangement {
  rate: number;
  instances: Instance[];
  /** The number the next instance id takes, so an id is never used twice on a track. */
  next?: number;
}

/** One finding about an arrangement or a scene, in the shape clip_edit uses. */
export interface ArrangementFinding {
  /** The instance at fault, or a track's goal id for a scene finding, or "arrangement". */
  id: string;
  field: string;
  message: string;
  candidates?: string[];
  /** It refuses a save (structure) or a run (a scene's overlaps). A warning otherwise. */
  blocks: boolean;
}

/** What validation and layout need to know about a clip. */
export interface ClipFacts {
  kind: InstanceKind;
  target: string;
  name: string;
  /** Clips: frames at the clip's rate; scripts: the newest run's duration in frames at 60, when known. */
  length: number | null;
  rate: number;
  map?: string | null;
  profile?: string | null;
  /** It has a manual check or step, so an unattended run skips it. */
  manual?: boolean;
}

/** An instance as a client draws it: where it starts and ends on the track, resolved against its clip. */
export interface InstanceLayout {
  id: string;
  kind: InstanceKind;
  target: string;
  name: string;
  start: number;
  end: number;
  length: number;
  /** False when the clip could not be read, so its length is a guess. */
  known: boolean;
}

export const emptyArrangement = (): Arrangement => ({ rate: ARRANGEMENT_RATE, instances: [] });

/** The asset path a target names, without an object name. */
export function targetKey(path: string): string {
  const dot = path.lastIndexOf(".");
  return dot > path.lastIndexOf("/") ? path.slice(0, dot) : path;
}

const leaf = (path: string): string => {
  const p = targetKey(path);
  return p.slice(p.lastIndexOf("/") + 1) || p;
};

const round = (n: number): number => Math.round(n * 1000) / 1000;

/** The instance's length on its track, in the track's frames. */
export function instanceLength(inst: Instance, facts: ClipFacts | undefined, rate: number): number {
  if (inst.kind === "script") return inst.length ?? (facts?.length ? Math.round((facts.length * rate) / ARRANGEMENT_RATE) : Math.round((SCRIPT_RESERVE_FRAMES * rate) / ARRANGEMENT_RATE));
  const clipRate = facts?.rate || rate;
  const clipLen = facts?.length ?? 0;
  const out = inst.out ?? clipLen;
  return round(Math.max(0, out - (inst.in ?? 0)) * (rate / clipRate));
}

export function arrangementLayout(a: Arrangement, factsOf: (target: string, kind: InstanceKind) => ClipFacts | undefined): InstanceLayout[] {
  return a.instances.map((i) => {
    const f = factsOf(i.target, i.kind);
    const length = instanceLength(i, f, a.rate);
    return { id: i.id, kind: i.kind, target: i.target, name: i.name ?? f?.name ?? leaf(i.target), start: i.start, end: round(i.start + length), length, known: !!f && (i.kind === "script" || f.length !== null) };
  });
}

/**
 * The arrangement as it plays from frame `from` (at ARRANGEMENT_RATE), shifted
 * so `from` is its frame 0. Instances that end by then are left out. A clip
 * instance the frame falls inside plays from that point (its `in` moves on);
 * a step-list instance cannot start part-way, so it plays whole at frame 0,
 * keeping the space it had left.
 */
export function arrangementFrom(a: Arrangement, from: number, factsOf: (target: string, kind: InstanceKind) => ClipFacts | undefined): Arrangement {
  if (!(from > 0)) return a;
  const at = (from * a.rate) / ARRANGEMENT_RATE;
  const instances: Instance[] = [];
  for (const i of a.instances) {
    const f = factsOf(i.target, i.kind);
    const end = i.start + instanceLength(i, f, a.rate);
    if (end <= at + 1e-6) continue;
    if (i.start >= at) {
      instances.push({ ...i, start: round(i.start - at) });
      continue;
    }
    const cut = at - i.start;
    if (i.kind === "clip") instances.push({ ...i, start: 0, in: round((i.in ?? 0) + cut * ((f?.rate || a.rate) / a.rate)) });
    else instances.push({ ...i, start: 0, length: round(end - at) });
  }
  return { ...a, instances };
}

/** The end of the track: the latest instance end. */
export const arrangementEnd = (layout: readonly InstanceLayout[]): number => layout.reduce((m, l) => Math.max(m, l.end), 0);

// ---------- Reading what a client sent ----------

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const num = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);

/** An arrangement from JSON, with every field that does not read named. Unknown fields are dropped. */
export function arrangementOf(v: unknown): { arrangement: Arrangement; findings: ArrangementFinding[] } {
  const findings: ArrangementFinding[] = [];
  const out = emptyArrangement();
  if (!isObj(v)) return { arrangement: out, findings: [{ id: "arrangement", field: "arrangement", message: "an arrangement is { rate?, instances: [] }", blocks: true }] };
  if (v.rate !== undefined) {
    if (num(v.rate) && v.rate >= 1 && v.rate <= 1000) out.rate = v.rate;
    else findings.push({ id: "arrangement", field: "rate", message: "rate is frames per second, 1 to 1000", blocks: true });
  }
  if (num(v.next) && Number.isInteger(v.next) && v.next > 0) out.next = v.next;
  const list = v.instances ?? [];
  if (!Array.isArray(list)) return { arrangement: out, findings: [...findings, { id: "arrangement", field: "instances", message: "instances must be a list", blocks: true }] };
  list.forEach((raw, n) => {
    const where = isObj(raw) && typeof raw.id === "string" && raw.id ? raw.id : `instances[${n}]`;
    if (!isObj(raw)) return findings.push({ id: where, field: "instance", message: `${where} must be an object`, blocks: true });
    const kind = raw.kind;
    if (kind !== "clip" && kind !== "script") return findings.push({ id: where, field: "kind", message: `${where}.kind must be clip or script`, candidates: ["clip", "script"], blocks: true });
    if (typeof raw.target !== "string" || !raw.target.startsWith("/")) return findings.push({ id: where, field: "target", message: `${where}.target must be an asset path such as /Game/PIEStudio/Clips/CL_Door`, blocks: true });
    if (!num(raw.start)) return findings.push({ id: where, field: "start", message: `${where}.start must be a frame number`, blocks: true });
    const inst: Instance = { id: typeof raw.id === "string" ? raw.id : "", kind, target: targetKey(raw.target), start: raw.start };
    for (const k of ["in", "out", "length"] as const) {
      if (raw[k] === undefined || raw[k] === null) continue;
      if (num(raw[k])) inst[k] = raw[k] as number;
      else findings.push({ id: where, field: k, message: `${where}.${k} must be a frame number`, blocks: true });
    }
    if (typeof raw.name === "string" && raw.name.trim()) inst.name = raw.name.trim().slice(0, 200);
    if (typeof raw.color === "string" && /^[a-z]{3,10}$/.test(raw.color)) inst.color = raw.color;
    out.instances.push(inst);
  });
  fillIds(out);
  sortInstances(out);
  return { arrangement: out, findings };
}

/** Ids for instances without one, after the highest already used. */
function fillIds(a: Arrangement): void {
  let next = nextIdNumber(a);
  for (const i of a.instances) if (!i.id || i.id === "-") i.id = `i${next++}`;
}

function nextIdNumber(a: Arrangement): number {
  let max = (a.next ?? 1) - 1;
  for (const i of a.instances) {
    const m = /^i(\d+)$/.exec(i.id);
    if (m) max = Math.max(max, Number(m[1]));
  }
  return max + 1;
}

const sortInstances = (a: Arrangement): void => void a.instances.sort((x, y) => x.start - y.start || x.id.localeCompare(y.id));

// ---------- Validation ----------

/** Closest strings to `word`, best first: shared tail (asset name) first, then edit distance. */
export function rankClipCandidates(word: string, pool: readonly string[], n = 3): string[] {
  const w = word.toLowerCase();
  const wl = leaf(w);
  const score = (c: string): number => {
    const l = c.toLowerCase();
    if (l === w) return -1;
    return Math.min(distance(wl, leaf(l)), distance(w, l));
  };
  return [...new Set(pool)].map((c) => [c, score(c)] as const).sort((a, b) => a[1] - b[1]).slice(0, n).map(([c]) => c);
}

function distance(a: string, b: string): number {
  const d = Array.from({ length: b.length + 1 }, (_, j) => j);
  for (let i = 1; i <= a.length; i++) {
    let prev = d[0];
    d[0] = i;
    for (let j = 1; j <= b.length; j++) {
      const t = d[j];
      d[j] = Math.min(d[j] + 1, d[j - 1] + 1, prev + (a[i - 1] === b[j - 1] ? 0 : 1));
      prev = t;
    }
  }
  return d[b.length];
}

export interface ArrangementValidateOptions {
  /** Facts for a clip, or undefined when it could not be read. */
  factsOf: (target: string, kind: InstanceKind) => ClipFacts | undefined;
  /** Every clip of that kind on this checkout, for candidates; null when the list could not be read (offline). */
  library?: (kind: InstanceKind) => string[] | null;
}

/**
 * Everything wrong with an arrangement. Structure refuses a save (blocks); a
 * clip this checkout does not have, a second level and a second observation
 * profile are warnings, since another checkout may have the clip and a run
 * settles the rest.
 */
export function validateArrangement(a: Arrangement, o: ArrangementValidateOptions): ArrangementFinding[] {
  const out: ArrangementFinding[] = [];
  const ids = new Set<string>();
  if (a.instances.length > ARRANGEMENT_MAX_INSTANCES) out.push({ id: "arrangement", field: "instances", message: `a track holds at most ${ARRANGEMENT_MAX_INSTANCES} instances`, blocks: true });
  for (const i of a.instances) {
    if (ids.has(i.id)) out.push({ id: i.id, field: "id", message: `two instances are ${i.id}`, blocks: true });
    ids.add(i.id);
    if (i.start < 0) out.push({ id: i.id, field: "start", message: `${i.id} starts before frame 0`, candidates: ["0"], blocks: true });
    const f = o.factsOf(i.target, i.kind);
    if (i.kind === "script") {
      if (i.in !== undefined || i.out !== undefined) out.push({ id: i.id, field: "in", message: `${i.id} is a step-list playtest, which settles by condition and cannot be trimmed; change its length instead`, blocks: true });
      if (i.length !== undefined && i.length <= 0) out.push({ id: i.id, field: "length", message: `${i.id}.length must be more than 0`, blocks: true });
    } else {
      if (i.length !== undefined) out.push({ id: i.id, field: "length", message: `${i.id} is a timeline clip, whose length is its clip's; trim it with in and out`, blocks: true });
      if (i.in !== undefined && i.in < 0) out.push({ id: i.id, field: "in", message: `${i.id}.in is before the clip's frame 0`, candidates: ["0"], blocks: true });
      const len = f?.length ?? null;
      const out_ = i.out ?? len;
      if (out_ !== null && (i.in ?? 0) >= out_) out.push({ id: i.id, field: "out", message: `${i.id} is empty: in (${i.in ?? 0}) must be before out (${out_})`, blocks: true });
      if (len !== null && i.out !== undefined && i.out > len + 1e-6) out.push({ id: i.id, field: "out", message: `${i.id}.out (${i.out}) is past the clip's end (${len})`, candidates: [String(len)], blocks: true });
    }
    if (!f) {
      const pool = o.library?.(i.kind);
      if (pool) out.push({ id: i.id, field: "target", message: `no ${i.kind === "clip" ? "timeline clip" : "step-list playtest"} at ${i.target} on this checkout`, candidates: rankClipCandidates(i.target, pool), blocks: false });
    }
  }
  // Overlaps, in start order.
  const layout = arrangementLayout(a, o.factsOf);
  const sorted = [...layout].sort((x, y) => x.start - y.start);
  for (let n = 1; n < sorted.length; n++) {
    const prev = sorted[n - 1];
    const cur = sorted[n];
    if (cur.start < prev.end - 1e-6) out.push({ id: cur.id, field: "start", message: `${cur.id} starts at ${cur.start}, before ${prev.id} ends at ${prev.end}; instances on a track may not overlap`, candidates: [String(prev.end), String(freeSlot(sorted, cur.length, cur.id))], blocks: true });
  }
  // One level and one observation profile per run: the first instance's.
  const first = sorted.map((l) => ({ l, f: o.factsOf(l.target, l.kind) })).filter((x) => x.f);
  const map = first.find((x) => x.f!.map)?.f!.map;
  const profile = first.find((x) => x.f!.profile)?.f!.profile;
  for (const { l, f } of first) {
    if (map && f!.map && f!.map !== map) out.push({ id: l.id, field: "map", message: `${l.id} plays in ${f!.map}, but the track plays in ${map}, its first instance's level`, blocks: false });
    if (profile && f!.profile && f!.profile !== profile) out.push({ id: l.id, field: "profile", message: `${l.id} arms ${f!.profile}, but a track run arms ${profile}, its first instance's observation profile`, blocks: false });
  }
  return out;
}

/** The first start at or after frame 0 where `length` frames fit between the others. */
export function freeSlot(layout: readonly InstanceLayout[], length: number, skipId?: string, from = 0): number {
  const others = layout.filter((l) => l.id !== skipId).sort((a, b) => a.start - b.start);
  let at = from;
  for (const l of others) {
    if (l.end <= at) continue;
    if (l.start >= at + length) break;
    at = l.end;
  }
  return round(at);
}

export const blockingFindings = (fs: readonly ArrangementFinding[]): ArrangementFinding[] => fs.filter((f) => f.blocks);

// ---------- Text form ----------

/** The arrangement as text: an `arrangement` line, then one line per instance. */
export function arrangementToText(a: Arrangement): string {
  const lines = [`arrangement rate=${a.rate}`];
  for (const i of a.instances) {
    let line = `${i.kind} ${i.id} ${i.target} ${round(i.start)}`;
    if (i.in !== undefined) line += ` in=${round(i.in)}`;
    if (i.out !== undefined) line += ` out=${round(i.out)}`;
    if (i.length !== undefined) line += ` length=${round(i.length)}`;
    if (i.name) line += ` name=${quote(i.name)}`;
    if (i.color) line += ` color=${i.color}`;
    lines.push(line);
  }
  return lines.join("\n");
}

const quote = (s: string): string => (/[\s"\\]/.test(s) || s === "" ? `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"` : s);

/** Split a line into words, keeping "quoted strings" (with \" and \\) whole, and key="quoted" pairs whole. */
function words(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQ = false;
  let had = false;
  for (let n = 0; n < line.length; n++) {
    const c = line[n];
    if (inQ) {
      if (c === "\\" && n + 1 < line.length) cur += line[++n];
      else if (c === '"') inQ = false;
      else cur += c;
    } else if (c === '"') {
      inQ = true;
      had = true;
    } else if (/\s/.test(c)) {
      if (cur || had) out.push(cur);
      cur = "";
      had = false;
    } else cur += c;
  }
  if (cur || had) out.push(cur);
  return out;
}

const LINE_WORDS = ["arrangement", "clip", "script"];

/** An arrangement from its text form, with every line that does not read named. */
export function arrangementFromText(text: string): { arrangement: Arrangement; findings: ArrangementFinding[] } {
  const a = emptyArrangement();
  const findings: ArrangementFinding[] = [];
  text.split(/\r?\n/).forEach((raw, n) => {
    const line = raw.trim();
    if (!line || line.startsWith("#")) return;
    const w = words(line);
    const at = `line ${n + 1}`;
    if (w[0] === "arrangement") {
      for (const kv of w.slice(1)) {
        const [k, v] = kv.split("=");
        if (k === "rate" && Number(v) >= 1 && Number(v) <= 1000) a.rate = Number(v);
        else findings.push({ id: at, field: k, message: `${at}: '${kv}' is not rate=<frames per second>`, blocks: true });
      }
      return;
    }
    if (w[0] !== "clip" && w[0] !== "script") return findings.push({ id: at, field: "line", message: `${at}: '${w[0]}' is not an arrangement line`, candidates: rankClipCandidates(w[0], LINE_WORDS), blocks: true });
    if (w.length < 4 || !Number.isFinite(Number(w[3]))) return findings.push({ id: at, field: "line", message: `${at}: expected ${w[0]} <id> <asset path> <start> [in=N] [out=N] [length=N] [name="..."]`, blocks: true });
    const inst: Instance = { id: w[1] === "-" ? "" : w[1], kind: w[0], target: targetKey(w[2]), start: Number(w[3]) };
    for (const kv of w.slice(4)) {
      const eq = kv.indexOf("=");
      const k = eq > 0 ? kv.slice(0, eq) : kv;
      const v = eq > 0 ? kv.slice(eq + 1) : "";
      if ((k === "in" || k === "out" || k === "length") && v !== "" && Number.isFinite(Number(v))) inst[k] = Number(v);
      else if (k === "name") inst.name = v;
      else if (k === "color" && v) inst.color = v;
      else findings.push({ id: inst.id || at, field: k, message: `${at}: '${kv}' is not one of in=, out=, length=, name=, color=`, candidates: rankClipCandidates(k, ["in", "out", "length", "name", "color"]), blocks: true });
    }
    a.instances.push(inst);
  });
  fillIds(a);
  sortInstances(a);
  return { arrangement: a, findings };
}

// ---------- Edit ops ----------

export type ArrangementOp =
  | { op: "add"; kind: InstanceKind; target: string; at?: number; in?: number; out?: number; length?: number; name?: string }
  | { op: "duplicate"; id: string; at?: number }
  | { op: "move"; id: string; to?: number; by?: number }
  | { op: "trim"; id: string; in?: number | null; out?: number | null; length?: number | null }
  | { op: "rename"; id: string; name: string | null }
  | { op: "delete"; id: string };

const OP_NAMES = ["add", "duplicate", "move", "trim", "rename", "delete"];

/** Ops from their text form, one per line: `add clip <path> at <frame>`, `duplicate i1 at 300`, `move i2 to 900`, `move i2 by -60`, `trim i1 in=10 out=200`, `trim i2 length=900`, `rename i1 "Jump"`, `delete i3`. */
export function arrangementOpsFromText(text: string): { ops: ArrangementOp[]; findings: ArrangementFinding[] } {
  const ops: ArrangementOp[] = [];
  const findings: ArrangementFinding[] = [];
  text.split(/\r?\n|;/).forEach((raw, n) => {
    const line = raw.trim();
    if (!line || line.startsWith("#")) return;
    const w = words(line);
    const at = `op ${n + 1}`;
    const bad = (message: string, candidates?: string[]) => findings.push({ id: at, field: "op", message: `${at} (${line}): ${message}`, candidates, blocks: true });
    const frame = (s: string | undefined): number | undefined => (s !== undefined && Number.isFinite(Number(s)) ? Number(s) : undefined);
    const kvs = (list: string[]) => Object.fromEntries(list.filter((x) => x.includes("=")).map((x) => [x.slice(0, x.indexOf("=")), x.slice(x.indexOf("=") + 1)]));
    switch (w[0]) {
      case "add": {
        if ((w[1] !== "clip" && w[1] !== "script") || !w[2]) return bad("expected add <clip|script> <asset path> [at <frame>]");
        const kv = kvs(w.slice(3));
        const atI = w.indexOf("at");
        ops.push({ op: "add", kind: w[1], target: w[2], at: atI > 0 ? frame(w[atI + 1]) : undefined, in: frame(kv.in), out: frame(kv.out), length: frame(kv.length), name: kv.name });
        return;
      }
      case "duplicate": {
        if (!w[1]) return bad("expected duplicate <id> [at <frame>]");
        ops.push({ op: "duplicate", id: w[1], at: w[2] === "at" ? frame(w[3]) : undefined });
        return;
      }
      case "move": {
        const f = frame(w[3]);
        if (!w[1] || (w[2] !== "to" && w[2] !== "by") || f === undefined) return bad("expected move <id> to <frame>, or move <id> by <frames>");
        ops.push(w[2] === "to" ? { op: "move", id: w[1], to: f } : { op: "move", id: w[1], by: f });
        return;
      }
      case "trim": {
        const kv = kvs(w.slice(2));
        if (!w[1] || Object.keys(kv).length === 0) return bad("expected trim <id> in=<frame> out=<frame>, or trim <id> length=<frames>");
        const v = (k: string): number | null | undefined => (kv[k] === undefined ? undefined : kv[k] === "-" ? null : frame(kv[k]));
        ops.push({ op: "trim", id: w[1], in: v("in"), out: v("out"), length: v("length") });
        return;
      }
      case "rename":
        if (!w[1]) return bad('expected rename <id> "name"');
        ops.push({ op: "rename", id: w[1], name: w[2] ?? null });
        return;
      case "delete":
        if (!w[1]) return bad("expected delete <id>");
        ops.push({ op: "delete", id: w[1] });
        return;
      default:
        bad(`'${w[0]}' is not an op`, rankClipCandidates(w[0], OP_NAMES));
    }
  });
  return { ops, findings };
}

/** Ops from JSON: a list of op objects, or a string of op lines. */
export function arrangementOpsOf(v: unknown): { ops: ArrangementOp[]; findings: ArrangementFinding[] } {
  if (typeof v === "string") return arrangementOpsFromText(v);
  if (!Array.isArray(v) || v.length === 0) return { ops: [], findings: [{ id: "ops", field: "ops", message: "ops must be a non-empty list of op objects, or op lines as text", blocks: true }] };
  const findings: ArrangementFinding[] = [];
  const ops: ArrangementOp[] = [];
  v.forEach((o, n) => {
    if (!isObj(o) || typeof o.op !== "string" || !OP_NAMES.includes(o.op)) {
      findings.push({ id: `op ${n + 1}`, field: "op", message: `op ${n + 1} must be { op: ${OP_NAMES.join(" | ")}, ... }`, candidates: isObj(o) && typeof o.op === "string" ? rankClipCandidates(o.op, OP_NAMES) : OP_NAMES, blocks: true });
      return;
    }
    ops.push(o as unknown as ArrangementOp);
  });
  return { ops, findings };
}

export type ArrangementEditResult =
  | { ok: true; arrangement: Arrangement; changed: string[]; findings: ArrangementFinding[] }
  | { ok: false; finding: ArrangementFinding; findings: ArrangementFinding[] };

/**
 * Apply ops in order, all or none. Positions snap to whole frames. An op that
 * names nothing, or leaves the track unsaveable, changes nothing and answers
 * the finding with candidates: the closest ids, the closest clips, or the
 * nearest free position.
 */
export function applyArrangementOps(a: Arrangement, ops: readonly ArrangementOp[], o: ArrangementValidateOptions): ArrangementEditResult {
  const work: Arrangement = { rate: a.rate, instances: a.instances.map((i) => ({ ...i })), ...(a.next ? { next: a.next } : {}) };
  const changed: string[] = [];
  const snap = (n: number): number => Math.max(0, Math.round(n));
  const fail = (n: number, id: string, field: string, message: string, candidates?: string[]): ArrangementEditResult => {
    const finding = { id, field, message: `op ${n + 1}: ${message}`, candidates, blocks: true };
    return { ok: false, finding, findings: [finding] };
  };
  for (let n = 0; n < ops.length; n++) {
    const op = ops[n] as ArrangementOp & Record<string, unknown>;
    const find = (id: unknown) => (typeof id === "string" ? work.instances.find((i) => i.id === id) : undefined);
    const missing = (id: unknown) => fail(n, String(id), "id", `the track has no instance ${String(id)}`, rankClipCandidates(String(id), work.instances.map((i) => i.id)));
    const layout = () => arrangementLayout(work, o.factsOf);
    switch (op.op) {
      case "add": {
        if (op.kind !== "clip" && op.kind !== "script") return fail(n, "add", "kind", "add needs kind clip or script", ["clip", "script"]);
        if (typeof op.target !== "string" || !op.target.startsWith("/")) return fail(n, "add", "target", "add needs target, an asset path such as /Game/PIEStudio/Clips/CL_Door");
        const target = targetKey(op.target);
        if (!o.factsOf(target, op.kind)) {
          const pool = o.library?.(op.kind);
          if (pool) return fail(n, target, "target", `no ${op.kind === "clip" ? "timeline clip" : "step-list playtest"} at ${target}`, rankClipCandidates(target, pool));
        }
        const id = `i${nextIdNumber(work)}`;
        work.next = nextIdNumber(work) + 1;
        const inst: Instance = { id, kind: op.kind, target, start: 0 };
        if (num(op.in)) inst.in = op.in;
        if (num(op.out)) inst.out = op.out;
        if (num(op.length)) inst.length = snap(op.length);
        if (typeof op.name === "string" && op.name.trim()) inst.name = op.name.trim();
        inst.start = num(op.at) ? snap(op.at) : arrangementEnd(layout());
        work.instances.push(inst);
        changed.push(id);
        break;
      }
      case "duplicate": {
        const src = find(op.id);
        if (!src) return missing(op.id);
        const id = `i${nextIdNumber(work)}`;
        work.next = nextIdNumber(work) + 1;
        const l = layout();
        const len = l.find((x) => x.id === src.id)!.length;
        const copy: Instance = { ...src, id, start: num(op.at) ? snap(op.at) : freeSlot(l, len, undefined, l.find((x) => x.id === src.id)!.end) };
        work.instances.push(copy);
        changed.push(id);
        break;
      }
      case "move": {
        const inst = find(op.id);
        if (!inst) return missing(op.id);
        if (!num(op.to) && !num(op.by)) return fail(n, inst.id, "to", "move needs to (a frame) or by (frames)");
        inst.start = snap(num(op.to) ? op.to : inst.start + (op.by as number));
        changed.push(inst.id);
        break;
      }
      case "trim": {
        const inst = find(op.id);
        if (!inst) return missing(op.id);
        for (const k of ["in", "out", "length"] as const) {
          if (op[k] === null) delete inst[k];
          else if (num(op[k])) inst[k] = k === "length" ? snap(op[k] as number) : round(op[k] as number);
        }
        changed.push(inst.id);
        break;
      }
      case "rename": {
        const inst = find(op.id);
        if (!inst) return missing(op.id);
        if (typeof op.name === "string" && op.name.trim()) inst.name = op.name.trim().slice(0, 200);
        else delete inst.name;
        changed.push(inst.id);
        break;
      }
      case "delete": {
        const at = work.instances.findIndex((i) => i.id === op.id);
        if (at < 0) return missing(op.id);
        work.instances.splice(at, 1);
        changed.push(String(op.id));
        break;
      }
      default:
        return fail(n, "op", "op", `'${String((op as { op?: unknown }).op)}' is not an op`, rankClipCandidates(String((op as { op?: unknown }).op), OP_NAMES));
    }
  }
  sortInstances(work);
  const findings = validateArrangement(work, o);
  const stop = blockingFindings(findings)[0];
  if (stop) return { ok: false, finding: stop, findings };
  return { ok: true, arrangement: work, changed, findings };
}

/** The unique clips an arrangement refers to, in start order, as a goal lists checks. */
export function arrangementRefs(a: Arrangement): Array<{ kind: InstanceKind; target: string }> {
  const seen = new Set<string>();
  const out: Array<{ kind: InstanceKind; target: string }> = [];
  for (const i of [...a.instances].sort((x, y) => x.start - y.start)) {
    const k = `${i.kind}:${i.target}`;
    if (seen.has(k)) continue;
    seen.add(k);
    out.push({ kind: i.kind, target: i.target });
  }
  return out;
}

/** A goal's script checks laid end to end from frame 0, each reserving its newest run's duration: how goals become tracks. */
export function arrangementFromChecks(targets: readonly string[], durationMs: (target: string) => number | null | undefined, rate = ARRANGEMENT_RATE): Arrangement {
  const a: Arrangement = { rate, instances: [] };
  let at = 0;
  targets.forEach((target, n) => {
    const ms = durationMs(target);
    const length = ms && ms > 0 ? Math.max(1, Math.round((ms / 1000) * rate)) : Math.round((SCRIPT_RESERVE_FRAMES * rate) / ARRANGEMENT_RATE);
    a.instances.push({ id: `i${n + 1}`, kind: "script", target, start: at, length });
    at += length;
  });
  return a;
}
