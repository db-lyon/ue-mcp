import { describe, it, expect } from "vitest";
// @ts-expect-error - plain ESM script, no types
import { keysInYaml, personalKeysIn, PERSONAL_KEYS } from "../../scripts/check-tracked-config.mjs";

const keys = (text: string): string[] => keysInYaml(text) as string[];
const personal = (text: string): string[] =>
  (personalKeysIn(text) as Array<{ key: string }>).map((p) => p.key);

/**
 * ue-mcp.yml is checked in and shared. A setting that differs per person or
 * per machine in there guarantees merge noise and one person silently
 * clobbering another's setup.
 */
describe("personal settings are refused in a tracked config", () => {
  it("catches one nested under the ue-mcp block", () => {
    expect(personal("ue-mcp:\n  version: 1\n  dialog:\n    mode: auto\n")).toEqual(["dialog.mode"]);
  });

  it("catches one at the top level too", () => {
    expect(personal("contextStrategy: micro\n")).toEqual(["contextStrategy"]);
  });

  it("catches a pinned port, which is per machine", () => {
    expect(personal("ue-mcp:\n  bridge:\n    port: 9877\n")).toEqual(["bridge.port"]);
  });

  it("allows a config that is all project settings", () => {
    expect(personal("ue-mcp:\n  version: 1\n  plugins:\n    - ue-mcp-meshy\n")).toEqual([]);
  });

  it("names every setting that is per person, not per project", () => {
    // Deleting one from the list used to leave the check green: it still
    // fired, just on less.
    const keys = (PERSONAL_KEYS as Array<{ key: string }>).map((p) => p.key);
    for (const k of ["feedback.mode", "dialog.mode", "contextStrategy", "bridge.port"]) {
      expect(keys, k).toContain(k);
    }
  });

  it("says where each one belongs, so the refusal is actionable", () => {
    for (const p of PERSONAL_KEYS as Array<{ key: string; why: string }>) {
      expect(p.why.length, p.key).toBeGreaterThan(30);
    }
  });
});

describe("reading the keys out of a config", () => {
  it("gives a dotted path for every nesting level", () => {
    expect(keys("a:\n  b:\n    c: 1\n")).toEqual(["a", "a.b", "a.b.c"]);
  });

  it("ignores comments and list items", () => {
    expect(keys("# a: 1\nb: 2\nlist:\n  - x: 1\n")).toEqual(["b", "list"]);
  });

  it("closes a block when the indent goes back out", () => {
    expect(keys("a:\n  b: 1\nc:\n  d: 2\n")).toEqual(["a", "a.b", "c", "c.d"]);
  });
});
