/**
 * The engine range ue-mcp supports, oldest to newest. This is the one place it
 * is written down.
 *
 * Everything that states the range reads it from here: `dist/tool-counts.json`
 * (`engineRange`, which the landing site and the other repos read from the npm
 * package), the `<!-- count:engineRange -->` markers in README and docs, the
 * README overview image, and the prose lint's range rule. Moving to a new
 * engine is a change to this file followed by `npm run generate:metadata`.
 *
 * `tests/unit/engine-range.test.ts` holds the places that cannot read it (the
 * engine probe list, the release binaries table, the recorded Epic catalogs)
 * to agreement with it.
 */

/** @type {{ readonly min: string; readonly max: string }} */
export const SUPPORTED_ENGINE_RANGE = Object.freeze({ min: "5.4", max: "5.9" });

/** "5.4 to 5.9": the wording public copy uses for the range. */
export function engineRangeText(range = SUPPORTED_ENGINE_RANGE) {
  return `${range.min} to ${range.max}`;
}

/** Negative, zero or positive as `a` is older than, equal to or newer than `b`. */
export function compareEngines(a, b) {
  const [am, an] = String(a).split(".").map(Number);
  const [bm, bn] = String(b).split(".").map(Number);
  return am - bm || an - bn;
}
