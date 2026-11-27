// @session independent: Performers: types and pure functions over their arguments. No state.
/**
 * What a track drives. A track holds a performer reference, a tagged union on
 * `kind` with one case for now: the editor's player or a remote client
 * (`player`). A clip holds Input Action lanes only, which a player takes.
 *
 * The union grows by adding a case and the lanes that case takes. Several
 * tracks driving one performer (routing) needs nothing new, since a
 * performer is a reference by value and two tracks may name the same one.
 */

/** The kinds of performer a track may name. */
export const PERFORMER_KINDS = ["player"] as const;
export type PerformerKind = (typeof PERFORMER_KINDS)[number];

/** The editor's own player (client 0) or remote client N. */
export interface PlayerPerformer {
  kind: "player";
  client: number;
}

export type Performer = PlayerPerformer;

/** The most remote clients a player performer may name. */
export const PERFORMER_CLIENT_MAX = 15;

/** The performer a track drives when it names none: the editor's own player. */
export const DEFAULT_PERFORMER: PlayerPerformer = Object.freeze({ kind: "player", client: 0 }) as PlayerPerformer;

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const validClient = (c: unknown): c is number => typeof c === "number" && Number.isInteger(c) && c >= 0 && c <= PERFORMER_CLIENT_MAX;

/** A performer from JSON, or null when it is not one. */
export function performerOf(v: unknown): Performer | null {
  if (!isObj(v)) return null;
  if (v.kind === "player") return validClient(v.client ?? 0) ? { kind: "player", client: (v.client as number | undefined) ?? 0 } : null;
  return null;
}

/** Why a value is not a performer, for a finding. */
export function performerProblem(v: unknown): string {
  if (!isObj(v)) return "a performer is { kind: player, client }";
  if (v.kind === "player") return `a player performer's client is 0 (the editor's own player) or a remote client from 1 to ${PERFORMER_CLIENT_MAX}`;
  return `performer kind '${String(v.kind)}' is not one of ${PERFORMER_KINDS.join(", ")}`;
}

/** A performer from its text form: `player:N`, or a bare number for a player. Null when it is neither. */
export function performerFromText(s: string): Performer | null {
  const t = s.trim();
  if (/^\d+$/.test(t)) return performerOf({ kind: "player", client: Number(t) });
  const m = /^player:(\d+)$/i.exec(t);
  return m ? performerOf({ kind: "player", client: Number(m[1]) }) : null;
}

/** The text form: `player:N`. */
export function performerToText(p: Performer): string {
  return `player:${p.client}`;
}

/** One key per performer, equal for two references to the same one: what routing groups by. */
export function performerKey(p: Performer): string {
  return `player:${p.client}`;
}

export function samePerformer(a: Performer, b: Performer): boolean {
  return performerKey(a) === performerKey(b);
}

/** A short name for people: "Editor player" or "Client 2". */
export function performerLabel(p: Performer): string {
  return p.client === 0 ? "Editor player" : `Client ${p.client}`;
}

/** The performer a track drives from its stored fields: `performer`, else player `player`. */
export function trackPerformer(t: { performer?: unknown; player?: unknown }): Performer {
  const p = performerOf(t.performer);
  if (p) return p;
  return validClient(t.player) ? { kind: "player", client: t.player } : DEFAULT_PERFORMER;
}

/** The player field older clients read: the performer's client. */
export function performerPlayer(p: Performer): number {
  return p.client;
}

/** One performer a client can pick for a track (GET /hub/performers). */
export interface PerformerCandidate {
  performer: Performer;
  label: string;
}

/** The performers a track can drive (GET /hub/performers). */
export interface PerformersView {
  performers: PerformerCandidate[];
}
