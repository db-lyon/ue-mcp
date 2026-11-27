// @session independent: Performers and lane capabilities: types and pure functions over their arguments. No state.
/**
 * What a track drives (#2137). A track holds a performer reference, a tagged
 * union on `kind`: the editor's player or a remote client (`player`), or any
 * actor in the level (`actor`). A clip's lanes are typed by capability, and a
 * performer kind declares the capabilities it takes, so validation checks a
 * clip's lanes against the track that plays it.
 *
 * The union grows by adding a case. A group performer (one track driving
 * several actors, like an instrument rack) will be `{ kind: "group", members }`
 * taking the capabilities all its members take; several tracks driving one
 * performer (routing) needs nothing new, since a performer is a reference by
 * value and two tracks may name the same one. Neither changes the stored
 * shapes below.
 */

/** The kinds of performer a track may name. */
export const PERFORMER_KINDS = ["player", "actor"] as const;
export type PerformerKind = (typeof PERFORMER_KINDS)[number];

/** The editor's own player (client 0) or remote client N. */
export interface PlayerPerformer {
  kind: "player";
  client: number;
}

/** An actor in the level, named as the level names it: its label, its object name, or its path. */
export interface ActorPerformer {
  kind: "actor";
  actor: string;
}

export type Performer = PlayerPerformer | ActorPerformer;

/** What a lane drives: Input Actions, moves through a controller, function calls, or a property or blackboard value over time. */
export const LANE_CAPABILITIES = ["input", "move", "action", "value"] as const;
export type LaneCapability = (typeof LANE_CAPABILITIES)[number];

/** The lanes each performer kind takes. A player's action and value lanes act on the pawn it possesses. */
export const PERFORMER_CAPABILITIES: { readonly [K in PerformerKind]: readonly LaneCapability[] } = {
  player: ["input", "action", "value"],
  actor: ["move", "action", "value"],
};

/** The most remote clients a player performer may name. */
export const PERFORMER_CLIENT_MAX = 15;

/** The performer a track drives when it names none: the editor's own player. */
export const DEFAULT_PERFORMER: PlayerPerformer = Object.freeze({ kind: "player", client: 0 }) as PlayerPerformer;

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const validClient = (c: unknown): c is number => typeof c === "number" && Number.isInteger(c) && c >= 0 && c <= PERFORMER_CLIENT_MAX;

/** A performer from JSON, or null when it is not one. Actor names are trimmed and kept to 200 characters. */
export function performerOf(v: unknown): Performer | null {
  if (!isObj(v)) return null;
  if (v.kind === "player") return validClient(v.client ?? 0) ? { kind: "player", client: (v.client as number | undefined) ?? 0 } : null;
  if (v.kind === "actor") {
    const actor = typeof v.actor === "string" ? v.actor.trim().slice(0, 200) : "";
    return actor ? { kind: "actor", actor } : null;
  }
  return null;
}

/** Why a value is not a performer, for a finding. */
export function performerProblem(v: unknown): string {
  if (!isObj(v)) return "a performer is { kind: player, client } or { kind: actor, actor }";
  if (v.kind === "player") return `a player performer's client is 0 (the editor's own player) or a remote client from 1 to ${PERFORMER_CLIENT_MAX}`;
  if (v.kind === "actor") return "an actor performer names the actor: its label, object name or path";
  return `performer kind '${String(v.kind)}' is not one of ${PERFORMER_KINDS.join(", ")}`;
}

/**
 * A performer from its text form: `player:N`, `actor:<name>`, or a bare
 * number for a player. Null when it is none of these.
 */
export function performerFromText(s: string): Performer | null {
  const t = s.trim();
  if (/^\d+$/.test(t)) return performerOf({ kind: "player", client: Number(t) });
  const at = t.indexOf(":");
  if (at < 0) return null;
  const kind = t.slice(0, at).toLowerCase();
  const rest = t.slice(at + 1);
  if (kind === "player") return /^\d+$/.test(rest) ? performerOf({ kind: "player", client: Number(rest) }) : null;
  if (kind === "actor") return performerOf({ kind: "actor", actor: rest });
  return null;
}

/** The text form: `player:N` or `actor:<name>`, quoted when the name holds a space or a quote. */
export function performerToText(p: Performer): string {
  if (p.kind === "player") return `player:${p.client}`;
  return /[\s"\\]/.test(p.actor) ? `actor:"${p.actor.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"` : `actor:${p.actor}`;
}

/** One key per performer, equal for two references to the same one: what routing groups by. */
export function performerKey(p: Performer): string {
  return p.kind === "player" ? `player:${p.client}` : `actor:${p.actor.toLowerCase()}`;
}

export function samePerformer(a: Performer, b: Performer): boolean {
  return performerKey(a) === performerKey(b);
}

/** A short name for people: "Editor player", "Client 2", or the actor's name. */
export function performerLabel(p: Performer): string {
  if (p.kind === "player") return p.client === 0 ? "Editor player" : `Client ${p.client}`;
  return p.actor;
}

/** Whether a performer takes lanes of this capability. */
export function performerAccepts(p: Performer, capability: LaneCapability): boolean {
  return PERFORMER_CAPABILITIES[p.kind].includes(capability);
}

/** The performer a track drives from its stored fields: `performer` when it names a non-player, else player `player`. */
export function trackPerformer(t: { performer?: unknown; player?: unknown }): Performer {
  const p = performerOf(t.performer);
  if (p) return p;
  return validClient(t.player) ? { kind: "player", client: t.player } : DEFAULT_PERFORMER;
}

/** The player field older clients read: the client for a player performer, 0 for any other. */
export function performerPlayer(p: Performer): number {
  return p.kind === "player" ? p.client : 0;
}

/** One performer a client can pick for a track (GET /hub/performers). */
export interface PerformerCandidate {
  performer: Performer;
  label: string;
  /** Actors: the class, as the level reports it. */
  className?: string;
  /** Actors: whether it is a pawn, which move lanes need. */
  pawn?: boolean;
  capabilities: LaneCapability[];
}

/** The performers a track in an arrangement can drive (GET /hub/performers?arrangement=). */
export interface PerformersView {
  /** The level the actors were read from. */
  level: string | null;
  performers: PerformerCandidate[];
  /** Why no actor is listed, when none is: no editor attached, or the level could not be read. */
  reason?: string;
}

// ---------- A clip's lanes ----------

/** A move lane's point: walk to `at` from frame `t`, through the actor's controller. */
export interface ClipMovePoint {
  id: string;
  t: number;
  at: [number, number, number];
  /** How close counts as there, in centimetres; the controller's own when absent. */
  radius?: number;
}

/** An action lane's call: the function, on the actor or `Component.Function`, at frame `t`. */
export interface ClipActionCall {
  id: string;
  t: number;
  fn: string;
  args?: unknown[];
}

/** A value lane's key, as an input lane's curve keys are. */
export interface ClipValueKey {
  id: string;
  t: number;
  v: number | boolean;
  interp?: string;
}

/** A clip lane that is not an Input Action lane: a move, action or value lane, as a clip file and pie(clip_record_stop) hold it. */
export type ClipLane =
  | { id: string; kind: "move"; performer?: Performer; points: ClipMovePoint[] }
  | { id: string; kind: "action"; performer?: Performer; calls: ClipActionCall[]; watch?: string[] }
  | { id: string; kind: "value"; performer?: Performer; target: string; type: "axis1d" | "bool"; keys: ClipValueKey[] };

/** The capability of a lane kind. Input Action lanes are a clip's `tracks`. */
export const laneCapability = (lane: Pick<ClipLane, "kind">): LaneCapability => lane.kind;

/** The frames a lane spans: its first and last point, call or key. Null when it is empty. */
export function laneSpan(lane: ClipLane): { from: number; to: number } | null {
  const times = lane.kind === "move" ? lane.points.map((p) => p.t) : lane.kind === "action" ? lane.calls.map((c) => c.t) : lane.keys.map((k) => k.t);
  if (!times.length) return null;
  return { from: Math.min(...times), to: Math.max(...times) };
}

/** A lane from JSON, or null when it is not one. Lenient about extra fields. */
export function clipLaneOf(v: unknown): ClipLane | null {
  if (!isObj(v) || typeof v.id !== "string") return null;
  const performer = performerOf(v.performer) ?? undefined;
  const base = { id: v.id, ...(performer ? { performer } : {}) };
  const list = (k: string) => (Array.isArray(v[k]) ? (v[k] as unknown[]).filter(isObj) : []);
  if (v.kind === "move") {
    return {
      ...base,
      kind: "move",
      points: list("points").flatMap((p) => (typeof p.id === "string" && typeof p.t === "number" && Array.isArray(p.at) && p.at.length === 3 && p.at.every((n) => typeof n === "number")
        ? [{ id: p.id, t: p.t, at: p.at as [number, number, number], ...(typeof p.radius === "number" ? { radius: p.radius } : {}) }]
        : [])),
    };
  }
  if (v.kind === "action") {
    const watch = Array.isArray(v.watch) ? v.watch.filter((x): x is string => typeof x === "string") : [];
    return {
      ...base,
      kind: "action",
      calls: list("calls").flatMap((c) => (typeof c.id === "string" && typeof c.t === "number" && typeof c.fn === "string" ? [{ id: c.id, t: c.t, fn: c.fn, ...(Array.isArray(c.args) && c.args.length ? { args: c.args } : {}) }] : [])),
      ...(watch.length ? { watch } : {}),
    };
  }
  if (v.kind === "value" && typeof v.target === "string") {
    return {
      ...base,
      kind: "value",
      target: v.target,
      type: v.type === "bool" ? "bool" : "axis1d",
      keys: list("keys").flatMap((k) => (typeof k.id === "string" && typeof k.t === "number" && (typeof k.v === "number" || typeof k.v === "boolean") ? [{ id: k.id, t: k.t, v: k.v, ...(typeof k.interp === "string" ? { interp: k.interp } : {}) }] : [])),
    };
  }
  return null;
}

/** What a lane drives, for findings: an Input Action's short name, a value's target, or the lane's kind. */
export function laneSubject(lane: ClipLane): string {
  return lane.kind === "value" ? lane.target : `${lane.kind} lane ${lane.id}`;
}
