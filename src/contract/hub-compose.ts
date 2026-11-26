// @session independent: Pure composition of timelines into the segments a run plays, and its results. No state.
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
import {
  DEFAULT_PERFORMER,
  PERFORMER_CAPABILITIES,
  clipLaneOf,
  laneSpan,
  laneSubject,
  performerAccepts,
  performerKey,
  performerLabel,
  performerOf,
  performerPlayer,
  trackPerformer,
  type ClipLane,
  type Performer,
} from "./hub-performers.js";

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
  /** Input Action lanes (capability input), each for player `client`. */
  tracks?: Array<{ action: string; type: string; client?: number; notes?: ClipNote[]; keys?: ClipKey[] }>;
  /** The performer the clip's lanes drive when neither the lane nor the track names one (#2137). */
  performer?: unknown;
  /** Move, action and value lanes (#2137). */
  lanes?: unknown[];
  events?: Array<{ id: string; at: number; do: Record<string, unknown> }>;
  checks?: ClipCheck[];
}

/** A clip's move, action and value lanes, read leniently. */
export function clipLanes(c: ClipJson): ClipLane[] {
  return (c.lanes ?? []).map(clipLaneOf).filter((l): l is ClipLane => !!l);
}

/**
 * The performer a lane drives when its clip plays on a track: the track's
 * performer when it names anyone but the editor's own player (as a track's
 * player has always overridden its clips' client), else the lane's own, else
 * the clip's, else the editor's player.
 */
export function lanePerformer(track: Performer, lane: ClipLane | undefined, clip: ClipJson): Performer {
  if (track.kind !== "player" || track.client > 0) return track;
  return lane?.performer ?? performerOf(clip.performer) ?? DEFAULT_PERFORMER;
}

/**
 * A clip's lanes checked against the performer of the track it is on: each
 * lane whose capability that performer does not take is a finding that
 * refuses the run, with the capabilities it does take as candidates.
 */
export function laneFindings(id: string, clip: ClipJson, track: Performer): ArrangementFinding[] {
  const out: ArrangementFinding[] = [];
  const takes = (p: Performer) => PERFORMER_CAPABILITIES[p.kind];
  // An Input Action lane always drives a player, so only a track that drives something else refuses it.
  if (!performerAccepts(track, "input")) {
    for (const tr of clip.tracks ?? []) {
      out.push({ id, field: "performer", message: `${id}: ${performerLabel(track)} takes ${takes(track).join(", ")} lanes, and the clip has an Input Action lane for ${tr.action.slice(tr.action.lastIndexOf(".") + 1)}`, candidates: [...takes(track)], blocks: true });
    }
  }
  for (const lane of clipLanes(clip)) {
    const p = lanePerformer(track, lane, clip);
    if (performerAccepts(p, lane.kind)) continue;
    out.push({ id, field: "performer", message: `${id}: ${performerLabel(p)} takes ${takes(p).join(", ")} lanes, and ${laneSubject(lane)} is a ${lane.kind} lane`, candidates: [...takes(p)], blocks: true });
  }
  return out;
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
  /** The player the track drives: 0 the run's own, N remote client N. Read when `performer` is absent. */
  player: number;
  /** What the track drives (#2137); player `player` when absent. */
  performer?: Performer;
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
  /** What its track drives. */
  performer: Performer;
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
  const trackOf = new Map<ComposedInstance, number>();
  tracks.forEach((t, tn) => {
    const scale = rate / (t.arrangement.rate || rate);
    const performer = t.performer ?? trackPerformer(t);
    for (const i of t.arrangement.instances) {
      const f = o.facts(i.target, i.kind);
      const len = instanceLength(i, f, t.arrangement.rate) * scale;
      const key = o.scene ? `${tn + 1}-${i.id}` : i.id;
      const clip = i.kind === "clip" ? o.clip(i.target) : undefined;
      if (i.kind === "clip" && !clip) findings.push({ id: key, field: "target", message: `${key}: the timeline clip ${i.target} could not be read, so the run cannot play it`, blocks: true });
      if (clip) findings.push(...laneFindings(key, clip, performer));
      // A step list drives the run's own player whatever its track drives: said, not refused.
      if (i.kind === "script" && performer.kind !== "player") findings.push({ id: key, field: "performer", message: `${key}: a step-list clip plays as the editor's player, not as ${performerLabel(performer)}`, blocks: false });
      const inst: ComposedInstance = {
        key,
        goalId: t.goalId,
        id: i.id,
        kind: i.kind,
        target: i.target,
        name: i.name ?? f?.name ?? i.target.slice(i.target.lastIndexOf("/") + 1),
        player: performerPlayer(performer),
        performer,
        start: round(i.start * scale),
        end: round((i.start + len / scale) * scale),
        segment: -1,
        checks: [],
        manual: i.kind === "clip" ? (clip?.checks ?? []).some((c) => c.manual === true) : f?.manual === true,
        ...(i.in !== undefined ? { in: i.in } : {}),
        ...(i.out !== undefined ? { out: i.out } : {}),
      };
      instances.push(inst);
      trackOf.set(inst, tn);
    }
  });
  // A scene: a step list plays alone, so what would play beside one waits for it.
  if (o.scene) findings.push(...playStepListsAlone(instances, trackOf, tracks.length));
  instances.sort((a, b) => a.start - b.start || a.key.localeCompare(b.key));
  const length = instances.reduce((m, i) => Math.max(m, i.end), 0);

  // A scene: the same action on one player at once.
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
  const others: Array<Record<string, unknown>> = [];
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
    // Move, action and value lanes: each instance's own, offset and cut to its trim, naming the performer it drives.
    for (const lane of clipLanes(c)) {
      const performer = lanePerformer(inst.performer, lane, c);
      const inside = (t: number) => t >= cin - EPS && t < cout - EPS;
      const head = { id: id(lane.id), kind: lane.kind, performer };
      if (lane.kind === "move") {
        const points = lane.points.filter((p) => inside(p.t)).map((p) => ({ ...p, id: id(p.id), t: at(p.t) }));
        if (points.length) others.push({ ...head, points });
      } else if (lane.kind === "action") {
        const calls = lane.calls.filter((k) => inside(k.t)).map((k) => ({ ...k, id: id(k.id), t: at(k.t) }));
        if (calls.length) others.push({ ...head, calls, ...(lane.watch ? { watch: lane.watch } : {}) });
      } else {
        const keys = [...lane.keys].sort((a, b) => a.t - b.t);
        const into = keys.filter((k) => inside(k.t)).map((k) => ({ ...k, id: id(k.id), t: at(k.t) }));
        // A trim that starts inside the curve begins on its value there.
        const before = keys.filter((k) => k.t < cin - EPS).pop();
        const after = keys.find((k) => k.t >= cin - EPS);
        if (before && (!after || after.t > cin + EPS) && after) {
          const v = typeof before.v === "number" && typeof after.v === "number" && before.interp !== "constant" ? before.v + (after.v - before.v) * ((cin - before.t) / (after.t - before.t)) : before.v;
          into.unshift({ id: id(`in-${lane.id}`), t: at(cin), v, interp: before.interp ?? "linear" });
        }
        if (into.length) others.push({ ...head, target: lane.target, type: lane.type, keys: into });
      }
    }
    for (const tr of c.tracks ?? []) {
      const client = inst.performer.kind === "player" && inst.performer.client > 0 ? inst.performer.client : tr.client ?? 0;
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
    ...(others.length ? { lanes: others } : {}),
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
  /** Script segments: the steps waiting for a verdict, by index, with what each asks. */
  awaiting?: InstanceAwaiting[];
  reason?: string | null;
}

/** A manual check waiting for a verdict: `check` is what POST /hub/runs/:id/verdict takes, `instruction` what to judge. */
export interface InstanceAwaiting {
  check: string;
  instruction: string;
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
  /** What its track drives (#2137). */
  performer?: Performer;
  start: number;
  end: number;
  status: InstanceStatus;
  checks: { total: number; passed: number; failed: number; pending: number; awaiting: number };
  /** The manual checks waiting for a verdict now: a clip's checks by their composed id, a step list's steps by index. Empty when none waits. */
  awaiting: InstanceAwaiting[];
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
  /** Where the run ends on the timeline, in frames. */
  length: number;
  /** A run played from the playhead: the frame it started at. Instance and segment frames are on the timeline, so they count from 0, not from here. */
  from?: number;
  /** A hash of what played: editing a clip or the timeline changes it, so runs compare only on the same version. */
  version?: string;
  instances: InstanceResult[];
  /** A scene: each track's status, passed when every instance on it passed. */
  tracks?: Array<{ goalId: string; title: string; player: number; performer?: Performer; status: InstanceStatus }>;
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
    let awaiting: InstanceAwaiting[] = [];
    if (seg?.kind === "script") {
      const o = st.outcome;
      status = o === "passed" ? "passed" : o === "failed" ? "failed" : o === "needs_you" ? "awaiting_verdict" : st.state === "running" ? "running" : st.state === "error" ? "failed" : "pending";
      if (st.state === "running") awaiting = st.awaiting ?? [];
    } else if (seg) {
      const said = new Map(((seg.clip.checks ?? []) as ClipCheck[]).map((c) => [c.id, c]));
      for (const id of inst.checks) {
        const s = st.checks?.[id] ?? "pending";
        if (s === "passed") counts.passed++;
        else if (s === "failed") counts.failed++;
        else if (s === "awaiting_verdict") {
          counts.awaiting++;
          const c = said.get(id);
          awaiting.push({ check: id, instruction: typeof c?.instruction === "string" && c.instruction ? c.instruction : typeof c?.name === "string" && c.name ? c.name : id });
        } else if (s !== "skipped") counts.pending++;
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
    return { key: inst.key, goalId: inst.goalId, id: inst.id, kind: inst.kind, target: inst.target, name: inst.name, player: inst.player, performer: inst.performer, start: inst.start, end: inst.end, status, checks: counts, awaiting, segmentRunId: st.runId, ...(reason ? { reason } : {}) };
  });
}

/**
 * Lay a scene's tracks out so a step-list instance plays alone (#2127). A step
 * list takes the whole PIE session and settles by condition, so its length is
 * a reservation, not a promise; two tracks' step lists, or a step list and
 * another track's timeline clip, cannot play at once whichever players the
 * tracks drive. Taking instances in the order they would start, one that
 * would start while another track's step list plays (or a step list while
 * another track's clip plays) moves to where that ends, and everything after
 * it on its track moves with it. Each move is a note, not a stop.
 */
function playStepListsAlone(instances: readonly ComposedInstance[], trackOf: ReadonlyMap<ComposedInstance, number>, trackCount: number): ArrangementFinding[] {
  const out: ArrangementFinding[] = [];
  const queues: ComposedInstance[][] = Array.from({ length: trackCount }, () => []);
  for (const i of instances) queues[trackOf.get(i)!].push(i);
  for (const q of queues) q.sort((a, b) => a.start - b.start);
  const shift = new Array<number>(trackCount).fill(0);
  const head = new Array<number>(trackCount).fill(0);
  let script: { until: number; key: string } = { until: 0, key: "" };
  let clip: { until: number; key: string } = { until: 0, key: "" };
  for (;;) {
    let t = -1;
    for (let n = 0; n < trackCount; n++) {
      const i = queues[n][head[n]];
      if (!i) continue;
      const best = t < 0 ? undefined : queues[t][head[t]];
      if (!best || i.start + shift[n] < best.start + shift[t] - EPS) t = n;
    }
    if (t < 0) break;
    const i = queues[t][head[t]++];
    const at = i.start + shift[t];
    const wait = i.kind === "script" && clip.until > script.until ? clip : script;
    const need = i.kind === "script" ? Math.max(at, script.until, clip.until) : Math.max(at, script.until);
    const len = i.end - i.start;
    shift[t] += need - at;
    const was = round(i.start);
    i.start = round(need);
    i.end = round(need + len);
    if (need - at > EPS) {
      const why = wait === script ? `it waits for the step list ${wait.key}, which plays alone` : `a step list plays alone, so it waits for ${wait.key} to end`;
      out.push({ id: i.key, field: "start", message: `${i.key} plays from ${i.start}, not ${was}: ${why}. The rest of its track moves with it`, blocks: false });
    }
    if (i.kind === "script") script = i.end > script.until ? { until: i.end, key: i.key } : script;
    else clip = i.end > clip.until ? { until: i.end, key: i.key } : clip;
  }
  return out;
}

/**
 * A scene's overlaps: one performer's input, move or value driven by two
 * tracks at once. Action lanes never clash: two calls are two calls.
 */
function sceneFindings(tracks: readonly ComposeTrack[], instances: readonly ComposedInstance[], o: ComposeOptions): ArrangementFinding[] {
  const out: ArrangementFinding[] = [];
  // What one instance drives over its span: an Input Action, a move, or a value target, on a performer.
  const uses: Array<{ inst: ComposedInstance; what: string; label: string; performer: string; who: string; from: number; to: number }> = [];
  for (const inst of instances) {
    if (inst.kind !== "clip") continue;
    const c = o.clip(inst.target);
    if (!c) continue;
    for (const tr of c.tracks ?? []) {
      const client = inst.performer.kind === "player" && inst.performer.client > 0 ? inst.performer.client : tr.client ?? 0;
      uses.push({ inst, what: `input ${tr.action}`, label: tr.action.slice(tr.action.lastIndexOf("/") + 1), performer: `player:${client}`, who: `player ${client}`, from: inst.start, to: inst.end });
    }
    for (const lane of clipLanes(c)) {
      if (lane.kind === "action" || !laneSpan(lane)) continue;
      const p = lanePerformer(inst.performer, lane, c);
      uses.push({ inst, what: lane.kind === "move" ? "move" : `value ${lane.target}`, label: lane.kind === "move" ? "its moves" : lane.target, performer: performerKey(p), who: performerLabel(p), from: inst.start, to: inst.end });
    }
  }
  for (let a = 0; a < uses.length; a++) {
    for (let b = a + 1; b < uses.length; b++) {
      const x = uses[a];
      const y = uses[b];
      if (x.inst.goalId === y.inst.goalId || x.what !== y.what || x.performer !== y.performer) continue;
      if (x.from < y.to - EPS && y.from < x.to - EPS) {
        out.push({ id: y.inst.key, field: "player", message: `${x.inst.key} and ${y.inst.key} both drive ${x.label} for ${x.who} between ${round(Math.max(x.from, y.from))} and ${round(Math.min(x.to, y.to))}; give one track another performer, or move one instance`, blocks: true });
      }
    }
  }
  if (tracks.length === 0) out.push({ id: "scene", field: "requires", message: "the scene has no tracks to play", blocks: true });
  return out;
}
