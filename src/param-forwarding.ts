/**
 * Which of a call's parameters an action's `mapParams` actually sends (#1057).
 *
 * A category's zod shape is shared by every action in it, so a parameter one
 * action declares passes validation on all of them. When the called action's
 * mapper never reads it, the value is dropped before the bridge and the call
 * still reports success. This runs the mapper over a read-tracking view of the
 * bag, so the answer comes from the mapper itself rather than from a second
 * list of names that could drift from it.
 */

/** A key counts as read on get, `in`, or an own-property check. Spreads read everything. */
function trackReads(bag: Record<string, unknown>): { view: Record<string, unknown>; read: Set<string> } {
  const read = new Set<string>();
  const note = (key: string | symbol): void => {
    if (typeof key === "string") read.add(key);
  };
  const view = new Proxy(bag, {
    get(target, key, receiver) { note(key); return Reflect.get(target, key, receiver); },
    has(target, key) { note(key); return Reflect.has(target, key); },
    getOwnPropertyDescriptor(target, key) { note(key); return Reflect.getOwnPropertyDescriptor(target, key); },
  });
  return { view, read };
}

/** The keys a mapper reads from `bag`, including those read before it throws. */
export function keysRead(
  mapParams: (p: Record<string, unknown>) => Record<string, unknown>,
  bag: Record<string, unknown>,
): Set<string> {
  const { view, read } = trackReads(bag);
  try {
    const out = mapParams(view);
    if ((out as unknown) === view) return new Set(Object.keys(bag));
  } catch {
    // A refusal still read what it validated.
  }
  return read;
}

export interface ForwardedCall {
  /** What the bridge should receive. Never the tracking view itself. */
  params: Record<string, unknown>;
  /** Supplied keys the mapper never looked at, sorted. */
  unforwarded: string[];
}

/**
 * Run `mapParams` and report the supplied keys it ignored.
 *
 * `supplied` limits the report to what the caller sent, so keys a category
 * normalizer added are never blamed on the caller. A key whose value reached
 * the mapper under another name (a normalizer mirroring `assetPath` into
 * `path`) counts as forwarded.
 */
export function mapTracked(
  mapParams: (p: Record<string, unknown>) => Record<string, unknown>,
  bag: Record<string, unknown>,
  supplied: Iterable<string> = Object.keys(bag),
): ForwardedCall {
  const { view, read } = trackReads(bag);
  let params = mapParams(view);
  if ((params as unknown) === view) {
    return { params: bag, unforwarded: [] };
  }
  // A mapper that nests its whole bag hands the view on; unwrap it.
  if (params && typeof params === "object" && Object.values(params).includes(view)) {
    params = Object.fromEntries(Object.entries(params).map(([k, v]) => [k, v === view ? bag : v]));
    return { params, unforwarded: [] };
  }

  // Only strings and objects identify an alias; two `true`s are a coincidence.
  const aliasable = (v: unknown): boolean => (typeof v === "string" && v !== "") || (typeof v === "object" && v !== null);
  const readValues = new Set([...read].filter((k) => k in bag && aliasable(bag[k])).map((k) => bag[k]));
  const unforwarded = [...new Set(supplied)]
    .filter((k) => k in bag && bag[k] !== undefined && !read.has(k) && !readValues.has(bag[k]))
    .sort();
  return { params, unforwarded };
}
