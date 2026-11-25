/**
 * Composing a track or a scene into what one run plays (#2113).
 *
 * The instances of every track are laid on one timeline. Clip instances are
 * merged into inline clips the clip player plays (pie clip_run with `clip`),
 * each instance's lanes, events, check notes and regions offset to its start,
 * cut to its trim and given ids prefixed with the instance's key. A step-list
 * instance splits the timeline: the clip segment before it ends at its start,
 * the script plays, and the next segment begins at its end. Every segment
 * plays in the same PIE session.
 */
import {
  instanceLength,
  type Arrangement,
  type ArrangementFinding,
  type ClipFacts,
  type InstanceKind,
} from "./hub-arrangement.js";

/** A clip as pie(clip_read) answers it: the fields composition reads. */
export interface ClipJson {
  rate?: number;
  length?: number;
  effective_length?: number;
  map?: string;
  profile?: string;
  description?: string;
  fixed_timestep?: boolean;
  seed?: number;
  hold_timeout?: number;
  start?: unknown;
  frame_times?: number[];
  regions?: Array<{ id: string; name?: string; start: number; end: number }>;
  tracks?: Array<{ action: string; type: string; client?: number; notes?: ClipNote[]; keys?: ClipKey[] }>;
  events?: Array<{ id: string; at: number; do: Record<string, unknown> }>;
  checks?: ClipCheck[];
}
interface ClipNote { id: string; start: number; length: number; value?: unknown }
interface ClipKey { id: string; t: number; v: unknown; interp?: string }
interface ClipCheck {
  id: string;
  start: number;
  length: number;
  mode: string;
  manual?: boolean;
  recorded?: { frame: number; game_ms?: number; value?: number };
  [k: string]: unknown;
}

/** A clip's facts for layout and validation, from its JSON. */
export function clipFacts(target: string, name: string, c: ClipJson): ClipFacts {
  return {
    kind: "clip",
    target,
    name,
    length: c.length && c.length > 0 ? c.length : c.effective_length ?? 0,
    rate: c.rate || 60,
    map: c.map ?? null,
    profile: c.profile ?? null,
    manual: (c.checks ?? []).some((k) => k.manual === true),
  };
}

export interface ComposeTrack {
  goalId: string;
  title?: string;
  arrangement: Arrangement;
  /** The player the track drives: 0 the run's own, N remote client N. */
  player: number;
}

export interface ComposedInstance {
  /** Unique within the run: the instance id in a track run, "<track number>-<id>" in a scene run. Prefixes its ids in the composed clip. */
  key: string;
  goalId: string;
  id: string;
  kind: InstanceKind;
  target: string;
  name: string;
  player: number;
  /** Frames on the run's timeline. */
  start: number;
  end: number;
  segment: number;
  /** Clip instances: their checks' ids in the composed clip. */
  checks: string[];
  manual: boolean;
  /** Clip instances: the trim, in the clip's own frames. */
  in?: number;
  out?: number;
}

export type ArrangementSegment =
  | { kind: "clip"; offset: number; length: number; clip: Record<string, unknown>; instances: string[] }
  | { kind: "script"; offset: number; length: number; target: string; instance: string };

export interface ArrangementComposition {
  rate: number;
  length: number;
  segments: ArrangementSegment[];
  instances: ComposedInstance[];
  findings: ArrangementFinding[];
  /** Some instance waits for a person's verdict. */
  manual: boolean;
}

export interface ComposeOptions {
  clip: (target: string) => ClipJson | undefined;
  facts: (target: string, kind: InstanceKind) => ClipFacts | undefined;
  /** A scene: instance keys carry the track number. */
  scene?: boolean;
}

const EPS = 1e-6;
/** How far before an instance's end its curve's last value is held, so the zero key after it lands on another frame. */
const EDGE = 0.01;
const round = (n: number): number => Math.round(n * 1000) / 1000;
/** Act verbs a remote client takes; the others run on the server or the editor's own player. */
const REMOTE_VERBS = ["press", "hold", "tape", "inject", "console"];

const zeroOf = (v: unknown): unknown => (Array.isArray(v) ? v.map(() => 0) : 0);
const isZero = (v: unknown): boolean => (Array.isArray(v) ? v.every((x) => x === 0) : v === 0);
const lerp = (a: unknown, b: unknown, t: number): unknown =>
  Array.isArray(a) && Array.isArray(b) ? a.map((x, n) => (x as number) + ((b[n] as number) - (x as number)) * t) : (a as number) + ((b as number) - (a as number)) * t;

/** A curve's value at frame f, or undefined before its first key (it feeds nothing there). Cubic keys are read as linear. */
function curveAt(keys: readonly ClipKey[], f: number): unknown {
  let k0: ClipKey | undefined;
  let k1: ClipKey | undefined;
  for (const k of keys) {
    if (k.t <= f + EPS) k0 = k;
    else {
      k1 = k;
      break;
    }
  }
  if (!k0) return undefined;
  if (!k1 || k0.interp === "constant" || Math.abs(k1.t - k0.t) < EPS) return k0.v;
  return lerp(k0.v, k1.v, (f - k0.t) / (k1.t - k0.t));
}

/** Lay every track's instances on one timeline and compose the segments a run plays. */
export function composeArrangement(tracks: readonly ComposeTrack[], o: ComposeOptions): ArrangementComposition {
  const rate = tracks[0]?.arrangement.rate || 60;
  const findings: ArrangementFinding[] = [];
  const instances: ComposedInstance[] = [];
  tracks.forEach((t, tn) => {
    const scale = rate / (t.arrangement.rate || rate);
    for (const i of t.arrangement.instances) {
      const f = o.facts(i.target, i.kind);
      const len = instanceLength(i, f, t.arrangement.rate) * scale;
      const key = o.scene ? `${tn + 1}-${i.id}` : i.id;
      const clip = i.kind === "clip" ? o.clip(i.target) : undefined;
      if (i.kind === "clip" && !clip) findings.push({ id: key, field: "target", message: `${key}: the timeline clip ${i.target} could not be read, so the run cannot play it`, blocks: true });
      instances.push({
        key,
        goalId: t.goalId,
        id: i.id,
        kind: i.kind,
        target: i.target,
        name: i.name ?? f?.name ?? i.target.slice(i.target.lastIndexOf("/") + 1),
        player: t.player,
        start: round(i.start * scale),
        end: round((i.start + len / scale) * scale),
        segment: -1,
        checks: [],
        manual: i.kind === "clip" ? (clip?.checks ?? []).some((c) => c.manual === true) : f?.manual === true,
        ...(i.in !== undefined ? { in: i.in } : {}),
        ...(i.out !== undefined ? { out: i.out } : {}),
      });
    }
  });
  instances.sort((a, b) => a.start - b.start || a.key.localeCompare(b.key));
  const length = instances.reduce((m, i) => Math.max(m, i.end), 0);

  // A scene: the same action on one player at once, and anything beside a step-list instance.
  if (o.scene) findings.push(...sceneFindings(tracks, instances, o));

  // Split at the step-list instances.
  const scripts = instances.filter((i) => i.kind === "script");
  const segments: ArrangementSegment[] = [];
  let cursor = 0;
  const clipSegment = (from: number, to: number) => {
    if (to - from <= EPS) return;
    const inSeg = instances.filter((i) => i.kind === "clip" && i.start >= from - EPS && i.start < to - EPS);
    for (const i of inSeg) {
      if (i.end > to + EPS) findings.push({ id: i.key, field: "start", message: `${i.key} runs past ${to}, where a step-list instance starts`, blocks: true });
    }
    const n = segments.length;
    for (const i of inSeg) i.segment = n;
    segments.push({ kind: "clip", offset: from, length: round(to - from), clip: composeClip(inSeg, from, to, rate, o, n === 0), instances: inSeg.map((i) => i.key) });
  };
  for (const s of scripts) {
    clipSegment(cursor, s.start);
    s.segment = segments.length;
    segments.push({ kind: "script", offset: s.start, length: round(s.end - s.start), target: s.target, instance: s.key });
    cursor = Math.max(cursor, s.end);
  }
  clipSegment(cursor, length);
  // Each clip instance's checks, by their composed ids.
  for (const seg of segments) {
    if (seg.kind !== "clip") continue;
    const checks = (seg.clip.checks as ClipCheck[]) ?? [];
    for (const key of seg.instances) {
      const inst = instances.find((i) => i.key === key)!;
      inst.checks = checks.filter((c) => c.id.startsWith(`${key}.`)).map((c) => c.id);
    }
  }
  return { rate, length: round(length), segments, instances, findings, manual: instances.some((i) => i.manual) };
}

/** Compose the clip instances that start in [from, to) into one inline clip whose frame 0 is `from`. */
function composeClip(list: readonly ComposedInstance[], from: number, to: number, rate: number, o: ComposeOptions, first: boolean): Record<string, unknown> {
  const lanes = new Map<string, { action: string; type: string; client?: number; notes: ClipNote[]; keys: ClipKey[] }>();
  const events: Array<{ id: string; at: number; do: Record<string, unknown> }> = [];
  const checks: ClipCheck[] = [];
  const regions: Array<{ id: string; name?: string; start: number; end: number }> = [];
  const times: number[] = [];
  let lead: ClipJson | undefined;
  for (const inst of list) {
    const c = o.clip(inst.target);
    if (!c) continue;
    lead ??= c;
    const def = inst;
    const cr = c.rate || 60;
    const s = rate / cr;
    const cin = def.in ?? 0;
    const cout = def.out ?? (c.length && c.length > 0 ? c.length : c.effective_length ?? 0);
    const at = (f: number): number => round(inst.start - from + (f - cin) * s);
    const end = at(cout);
    const id = (x: string) => `${inst.key}.${x}`;
    for (const tr of c.tracks ?? []) {
      const client = inst.player > 0 ? inst.player : tr.client ?? 0;
      const laneKey = `${tr.action}@${client}`;
      let lane = lanes.get(laneKey);
      if (!lane) {
        lane = { action: tr.action, type: tr.type, ...(client > 0 ? { client } : {}), notes: [], keys: [] };
        lanes.set(laneKey, lane);
      }
      for (const n of tr.notes ?? []) {
        const ns = Math.max(n.start, cin);
        const ne = Math.min(n.start + n.length, cout);
        if (ne - ns <= EPS) continue;
        lane.notes.push({ ...n, id: id(n.id), start: at(ns), length: round((ne - ns) * s) });
      }
      const keys = [...(tr.keys ?? [])].sort((a, b) => a.t - b.t);
      if (keys.length) {
        const into: ClipKey[] = [];
        const vIn = curveAt(keys, cin);
        const exactIn = keys.some((k) => Math.abs(k.t - cin) < EPS);
        if (vIn !== undefined && !exactIn) into.push({ id: id(`in-${tr.action.slice(tr.action.lastIndexOf(".") + 1)}`), t: at(cin), v: vIn, interp: keys.filter((k) => k.t <= cin).pop()?.interp ?? "linear" });
        for (const k of keys) if (k.t >= cin - EPS && k.t < cout - EPS) into.push({ ...k, id: id(k.id), t: at(k.t) });
        const vOut = curveAt(keys, cout);
        if (vOut !== undefined && !isZero(vOut)) {
          const last = into[into.length - 1];
          if (!last || end - EDGE > last.t + EPS) into.push({ id: id(`hold-${tr.action.slice(tr.action.lastIndexOf(".") + 1)}`), t: round(end - EDGE), v: vOut, interp: "constant" });
          else last.interp = "constant";
          into.push({ id: id(`end-${tr.action.slice(tr.action.lastIndexOf(".") + 1)}`), t: end, v: zeroOf(vOut), interp: "constant" });
        } else if (vOut !== undefined) {
          into.push({ id: id(`end-${tr.action.slice(tr.action.lastIndexOf(".") + 1)}`), t: end, v: zeroOf(vOut), interp: "constant" });
        }
        // A later instance's key on the frame this one's zero key holds wins it.
        for (const k of into) {
          const clash = lane.keys.findIndex((x) => Math.abs(x.t - k.t) < EPS);
          if (clash >= 0) lane.keys.splice(clash, 1);
          lane.keys.push(k);
        }
      }
    }
    for (const e of c.events ?? []) {
      if (e.at < cin - EPS || e.at >= cout - EPS) continue;
      const verb = Object.keys(e.do ?? {}).find((k) => REMOTE_VERBS.includes(k));
      const d = inst.player > 0 && verb && !("kind" in e.do) && e.do.client === undefined ? { ...e.do, client: inst.player } : e.do;
      events.push({ id: id(e.id), at: at(e.at), do: d });
    }
    for (const k of c.checks ?? []) {
      if (k.start < cin - EPS || k.start >= cout - EPS) continue;
      const hold = k.mode === "hold";
      let len = k.length > 0 ? k.length : hold ? c.hold_timeout ?? 600 : 0;
      if (!hold && k.start + len > cout) len = cout - k.start;
      const next: ClipCheck = { ...k, id: id(k.id), start: at(k.start), length: round(len * s) };
      if (k.recorded && typeof k.recorded.frame === "number") next.recorded = { ...k.recorded, frame: at(k.recorded.frame) };
      checks.push(next);
    }
    for (const r of c.regions ?? []) {
      const rs = Math.max(r.start, cin);
      const re = Math.min(r.end, cout);
      if (re - rs <= EPS) continue;
      regions.push({ id: id(r.id), name: r.name, start: at(rs), end: at(re) });
    }
    if (c.frame_times?.length) {
      const own = c.frame_times.filter((f) => f >= cin - EPS && f <= cout + EPS).map(at);
      // The space between two instances' recorded frames plays at the track's own frame rate.
      const prev = times[times.length - 1];
      if (prev !== undefined && own.length && own[0] - prev > 1 + EPS) for (let f = prev + 1; f < own[0] - EPS; f++) times.push(round(f));
      times.push(...own);
    }
  }
  for (const lane of lanes.values()) {
    lane.notes.sort((a, b) => a.start - b.start);
    lane.keys.sort((a, b) => a.t - b.t);
  }
  const clip: Record<string, unknown> = {
    rate,
    length: round(to - from),
    grid: "free",
    fixed_timestep: lead?.fixed_timestep ?? true,
    tracks: [...lanes.values()].map((l) => (l.type === "bool" ? { action: l.action, type: l.type, ...(l.client ? { client: l.client } : {}), notes: l.notes } : l)),
    events: events.sort((a, b) => a.at - b.at),
    checks: checks.sort((a, b) => a.start - b.start),
    regions: regions.sort((a, b) => a.start - b.start),
  };
  if (times.length) clip.frame_times = times;
  if (lead?.profile) clip.profile = lead.profile;
  // The first segment sets the stage: the level, the start pose and the seed. Later ones play on from where the game is.
  if (first && lead) {
    if (lead.map) clip.map = lead.map;
    if (lead.start && list[0] && list[0].start - from < EPS && (list[0].in ?? 0) === 0) clip.start = lead.start;
    if (lead.seed) clip.seed = lead.seed;
  }
  return clip;
}

// ---------- Results ----------

export type InstanceStatus = "pending" | "running" | "passed" | "failed" | "skipped" | "awaiting_verdict";

/** What one segment of a run came to so far. */
export interface SegmentState {
  /** pie(clip_run) or pie(script_run)'s run id once it started. */
  runId: string | null;
  state: "pending" | "running" | "finished" | "stopped" | "error";
  /** Clip segments: playback's position in the segment's own frames. */
  position?: number;
  /** Clip segments: each check's status by its composed id. */
  checks?: Record<string, string>;
  /** Script segments: the run's outcome once it has one ("passed", "failed", "needs_you", ...). */
  outcome?: string;
  reason?: string | null;
}

/** One instance's result in a track or scene run, at its place on the timeline. */
export interface InstanceResult {
  key: string;
  goalId: string;
  id: string;
  kind: InstanceKind;
  target: string;
  name: string;
  player: number;
  start: number;
  end: number;
  status: InstanceStatus;
  checks: { total: number; passed: number; failed: number; pending: number; awaiting: number };
  /** The clip or script run that played it. */
  segmentRunId: string | null;
  reason?: string | null;
}

/**
 * A track or scene run as clients draw it: each instance's result at its
 * place, each track's in a scene, and the segments, so a client can follow a
 * segment's clip.position events and draw the cursor at offset + position.
 */
export type ArrangementRunKind = "track" | "arrangement" | "scene" | "slot";

export interface ArrangementRun {
  /** track: one timeline; arrangement: every timeline; scene: a Session row; slot: one Session slot. */
  kind: ArrangementRunKind;
  /** The track or scene; "arrangement" for an arrangement run; "<scene>/<track>" for a slot. */
  goalId: string;
  rate: number;
  length: number;
  instances: InstanceResult[];
  /** A scene: each track's status, passed when every instance on it passed. */
  tracks?: Array<{ goalId: string; title: string; player: number; status: InstanceStatus }>;
  segments: Array<{ index: number; kind: "clip" | "script"; runId: string | null; offset: number; length: number; state: SegmentState["state"] }>;
}

/** A track's status from its instances: failed on any failure, passed when all passed or were skipped. */
export function rollUpInstances(list: readonly Pick<InstanceResult, "status">[]): InstanceStatus {
  if (list.some((i) => i.status === "failed")) return "failed";
  if (list.some((i) => i.status === "awaiting_verdict")) return "awaiting_verdict";
  if (list.length > 0 && list.every((i) => i.status === "passed" || i.status === "skipped")) return "passed";
  return list.some((i) => i.status !== "pending") ? "running" : "pending";
}

const SETTLED_OK = ["passed", "skipped"];

/** Every instance's result from its segment's state. */
export function instanceResults(c: ArrangementComposition, segs: readonly SegmentState[]): InstanceResult[] {
  return c.instances.map((inst) => {
    const seg = c.segments[inst.segment];
    const st = segs[inst.segment] ?? { runId: null, state: "pending" as const };
    const counts = { total: inst.checks.length, passed: 0, failed: 0, pending: 0, awaiting: 0 };
    let status: InstanceStatus = "pending";
    let reason: string | null | undefined = st.reason;
    if (seg?.kind === "script") {
      const o = st.outcome;
      status = o === "passed" ? "passed" : o === "failed" ? "failed" : o === "needs_you" ? "awaiting_verdict" : st.state === "running" ? "running" : st.state === "error" ? "failed" : "pending";
    } else if (seg) {
      for (const id of inst.checks) {
        const s = st.checks?.[id] ?? "pending";
        if (s === "passed") counts.passed++;
        else if (s === "failed") counts.failed++;
        else if (s === "awaiting_verdict") counts.awaiting++;
        else if (s !== "skipped") counts.pending++;
      }
      const pos = (st.position ?? 0) + seg.offset;
      const reached = st.state === "finished" || pos >= inst.end - EPS;
      const settled = inst.checks.every((id) => SETTLED_OK.includes(st.checks?.[id] ?? "pending"));
      if (counts.failed > 0) status = "failed";
      else if (counts.awaiting > 0) status = "awaiting_verdict";
      else if (st.state === "error") {
        status = "failed";
      } else if (reached && settled && st.state !== "pending") status = inst.checks.length > 0 && inst.checks.every((id) => st.checks?.[id] === "skipped") ? "skipped" : "passed";
      else if (st.state === "running" && pos >= inst.start - EPS) status = "running";
      if (status === "pending" && (st.state === "stopped" || st.state === "finished")) reason ??= "the run ended before this instance played through";
    }
    return { key: inst.key, goalId: inst.goalId, id: inst.id, kind: inst.kind, target: inst.target, name: inst.name, player: inst.player, start: inst.start, end: inst.end, status, checks: counts, segmentRunId: st.runId, ...(reason ? { reason } : {}) };
  });
}

/** A scene's overlaps: one player's action driven by two tracks at once, and anything beside a step-list instance. */
function sceneFindings(tracks: readonly ComposeTrack[], instances: readonly ComposedInstance[], o: ComposeOptions): ArrangementFinding[] {
  const out: ArrangementFinding[] = [];
  const uses: Array<{ inst: ComposedInstance; action: string; from: number; to: number }> = [];
  for (const inst of instances) {
    if (inst.kind !== "clip") continue;
    const c = o.clip(inst.target);
    if (!c) continue;
    for (const tr of c.tracks ?? []) uses.push({ inst, action: tr.action, from: inst.start, to: inst.end });
  }
  for (let a = 0; a < uses.length; a++) {
    for (let b = a + 1; b < uses.length; b++) {
      const x = uses[a];
      const y = uses[b];
      if (x.inst.goalId === y.inst.goalId || x.action !== y.action || x.inst.player !== y.inst.player) continue;
      if (x.from < y.to - EPS && y.from < x.to - EPS) {
        out.push({ id: y.inst.key, field: "player", message: `${x.inst.key} and ${y.inst.key} both drive ${x.action.slice(x.action.lastIndexOf("/") + 1)} for player ${x.inst.player} between ${round(Math.max(x.from, y.from))} and ${round(Math.min(x.to, y.to))}; give one track another player, or move one instance`, blocks: true });
      }
    }
  }
  for (const s of instances.filter((i) => i.kind === "script")) {
    for (const i of instances) {
      if (i === s || i.goalId === s.goalId) continue;
      if (i.start < s.end - EPS && s.start < i.end - EPS) out.push({ id: i.key, field: "start", message: `${i.key} overlaps the step-list instance ${s.key} (${s.start} to ${s.end}), and the scene stops for it; move one of them`, blocks: true });
    }
  }
  if (tracks.length === 0) out.push({ id: "scene", field: "requires", message: "the scene has no tracks to play", blocks: true });
  return out;
}
