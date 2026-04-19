/**
 * Ontology types.
 *
 * Keep this file tight. ue-mcp is the emission side of the kantext
 * relationship: we produce .kant-shaped fragments and write them to
 * disk. Parsing, composition, and HQL evaluation live in kantext.
 *
 * Discipline (from kantext's kernel):
 * - No native lists. Multiplicity = named children in a KantSpace.
 * - No booleans. Boolean-ish state = Signal with a numeric value in
 *   a marker neighborhood defined by a Mapping.
 * - Every Point has a path. Paths are / separated, rooted at /UE.
 * - Every Meaning carries `meaning` and `purpose` strings.
 */

export type KantPath = string;

/** A leaf value allowed inside a .kant point. No arrays, no null, no bool. */
export type KantScalar = string | number;

/** A signal value - a numeric position in a marker neighborhood. */
export interface KantSignal {
  readonly kind: "signal";
  readonly value: number;
  readonly marker?: string;
}

/** Named children space. The kantext answer to arrays. */
export interface KantSpace {
  readonly kind: "space";
  readonly children: Readonly<Record<string, KantPoint>>;
}

/** A single point in the address space. */
export interface KantPoint {
  readonly meaning?: string;
  readonly purpose?: string;
  readonly category?: KantPath;
  readonly fields?: Readonly<Record<string, KantScalar | KantSignal>>;
  readonly children?: Readonly<Record<string, KantPoint>>;
}

/** A fragment produced by a projector: a set of points anchored at basePath. */
export interface KantFragment {
  readonly basePath: KantPath;
  readonly producer: string;
  readonly producedAt: string; // ISO 8601
  readonly points: Readonly<Record<string, KantPoint>>;
}

/** Events a projector can listen for. */
export type ProjectorEvent =
  | "startup"
  | "handler-registry-changed"
  | "module-loaded"
  | "hot-reload"
  | "asset-registry-changed"
  | "gameplay-tag-changed"
  | "flow-completed"
  | "manual";

export interface Projector<TInput = void> {
  readonly name: string;
  readonly basePath: KantPath;
  readonly triggerEvents: readonly ProjectorEvent[];
  project(input: TInput): KantFragment;
}
