/**
 * The advertised surface counts.
 *
 * The defect these cover: the 830 wrapped Epic tools became ordinary declared
 * actions inside `ALL_TOOLS`, but every surface that quotes a number kept
 * describing them as an extra on top of the total, so the published copy
 * claimed 1930 actions PLUS 830 more. At the same time the generator was still
 * reading the wrapped-tool count out of `assets/epic-catalog.snapshot.json`,
 * which that same change deleted, inside a `try/catch` that returned zero. The
 * landing site fetches `dist/tool-counts.json` live, so it rendered the two
 * bugs together: "1930+ native actions ... and all 0 of Epic's native 5.8
 * tools wrapped in-process".
 */
import { describe, expect, it } from "vitest";
import { computeCounts } from "../../scripts/generate-tool-metadata.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { SERVER_INSTRUCTIONS } from "../../src/instructions.js";

const counts = computeCounts();

describe("tool counts", () => {
  it("splits the total into ue-mcp's own actions and the wrapped Epic tools", () => {
    expect(counts.ownActions + counts.nativeToolActions).toBe(counts.actions);
    expect(counts.ownActions).toBeGreaterThan(0);
  });

  it("counts the wrapped Epic tools off the graph the server advertises", () => {
    const declared = ALL_TOOLS.reduce(
      (n, t) => n + Object.keys(t.actions).filter((a) => a.startsWith("epic_")).length,
      0,
    );
    expect(counts.nativeToolActions).toBe(declared);
  });

  it("never publishes a zero for a surface that ships", () => {
    // The read that produced this used to be wrapped in a catch returning 0.
    expect(counts.nativeToolActions).toBeGreaterThan(0);
    expect(counts.nativeToolsets).toBeGreaterThan(0);
  });

  it("does not advertise the wrapped tools as an addition to the total", () => {
    // Whatever the sentence looks like, the largest number in it must be the
    // real total. `1930 ... plus 830` reads as 2760 and is what shipped.
    const numbers = [...SERVER_INSTRUCTIONS.matchAll(/\b(\d{3,})\b/g)].map((m) => Number(m[1]));
    expect(numbers).toContain(counts.actions);
    expect(numbers).toContain(counts.ownActions);
    expect(numbers).toContain(counts.nativeToolActions);
    expect(Math.max(...numbers)).toBe(counts.actions);
  });
});
