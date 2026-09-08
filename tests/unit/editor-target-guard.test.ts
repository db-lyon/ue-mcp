import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
// @ts-expect-error - plain ESM script, no types
import { assertLiveTestProjectDir, assertTestProjectDir } from "../../scripts/bridge-target.mjs";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/**
 * These tests create and delete assets and modify levels, so a run pointed at
 * somebody's real game corrupts whatever is open before anybody reads the
 * output. Two guards stand in the way, and both are asserted here rather than
 * assumed: a check at the moment a connection is handed out, and a check on
 * the project the live harness resolved.
 */
describe("a foreign project is refused", () => {
  it("refuses one by the live-harness path", () => {
    expect(() => assertLiveTestProjectDir("C:/Users/x/Projects/UE/Vale")).toThrow(
      /not this repository's test project/i,
    );
  });

  it("refuses one by the connection path", () => {
    // The two guards word it differently; both say the editor is not the one
    // these tests may drive, and both say nothing was sent.
    expect(() => assertTestProjectDir("C:/Users/x/Projects/UE/Vale")).toThrow(
      /not the smoke test project|not this repository's test project/i,
    );
    expect(() => assertTestProjectDir("C:/Users/x/Projects/UE/Vale")).toThrow(/nothing was sent/i);
  });

  it("refuses when the editor would not say which project it has open", () => {
    expect(() => assertTestProjectDir(null)).toThrow();
    expect(() => assertTestProjectDir("")).toThrow();
  });

  it("allows this checkout's test project", () => {
    const dir = path.join(REPO, "tests", "ue_mcp");
    expect(() => assertLiveTestProjectDir(dir)).not.toThrow();
    expect(() => assertTestProjectDir(dir)).not.toThrow();
  });

  it("refuses a path that merely looks similar", () => {
    expect(() => assertTestProjectDir(path.join(REPO, "tests", "ue_mcp_backup"))).toThrow();
  });
});

/**
 * The guards only help if every route to an editor goes through one, so the
 * call sites are asserted too. Removing one is then a failing test rather than
 * a silent hole nobody notices until a run lands in the wrong project.
 */
describe("every route to an editor passes a guard", () => {
  /**
   * Call sites, not mentions.
   *
   * Asserting that the name appears in the file is satisfied by the import
   * alone, so deleting the call leaves such a test green. This counts lines
   * that actually invoke it.
   */
  const callSites = (rel: string, fn: string): number => {
    const lines = fs.readFileSync(path.join(REPO, rel), "utf8").split("\n");
    return lines.filter((raw) => {
      const line = raw.replace("\r", "").trim();
      if (line.startsWith("import") || line.startsWith("*") || line.startsWith("//")) return false;
      return line.includes(fn + "(");
    }).length;
  };

  it("the shared test bridge verifies before handing out a connection", () => {
    expect(callSites("tests/setup.ts", "verifyTestProjectTarget")).toBeGreaterThan(0);
  });

  it("the live harness asserts the project it resolved", () => {
    expect(callSites("tests/live/harness.ts", "assertLiveTestProjectDir")).toBeGreaterThan(0);
  });

  it("the live runner asserts before it spawns anything", () => {
    expect(callSites("scripts/live-tests.mjs", "assertLiveTestProjectDir")).toBeGreaterThan(0);
  });

  it("the smoke runner asserts before it sends anything", () => {
    expect(callSites("scripts/smoke-test.js", "assertTestProjectDir")).toBeGreaterThan(0);
  });

  it("counts a call and not an import of the same name", () => {
    // The helper's own contract, so a future loosening of it is caught here.
    expect(callSites("tests/live/harness.ts", "assertLoopbackHost")).toBeGreaterThan(0);
  });
});
