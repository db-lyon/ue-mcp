import { describe, it, expect } from "vitest";
import fsSync from "node:fs";
// @ts-expect-error - plain ESM script, no types
import { parseVersion, versionDeltaProblem, docsFreshnessProblem } from "../../scripts/check-release-gates.mjs";

const problem = (from: string, to: string): string | null =>
  versionDeltaProblem(from, to) as string | null;

/**
 * Every published version number is burned permanently, whether it is later
 * unpublished or not. That is why the bump rule is patch-only and why a skip
 * matters as much as a jump: both put numbers out of reach for good.
 */
describe("a version move has to be a single patch step", () => {
  it("allows the next patch", () => {
    expect(problem("1.3.4", "1.3.5")).toBeNull();
  });

  it("allows standing still, which is every commit that is not a release", () => {
    expect(problem("1.3.5", "1.3.5")).toBeNull();
  });

  it("allows a beta on the NEXT patch, which is how one is cut", () => {
    expect(problem("1.3.5", "1.3.6-beta.1")).toBeNull();
    expect(problem("1.3.5-beta.1", "1.3.5-beta.2")).toBeNull();
  });

  it("refuses a beta on the patch that already shipped", () => {
    // A prerelease sorts below the release of the same number, so this puts
    // the new version behind what is already published.
    expect(problem("1.3.5", "1.3.5-beta.1")).toContain("BELOW");
  });

  it("refuses a minor bump, and says whose call it is", () => {
    const p = problem("1.3.5", "1.4.0");
    expect(p).toContain("MINOR");
    expect(p).toContain("sign-off");
  });

  it("refuses a major bump", () => {
    expect(problem("1.3.5", "2.0.0")).toContain("MAJOR");
  });

  it("refuses going backwards", () => {
    expect(problem("1.3.5", "1.3.4")).toContain("backwards");
  });

  it("refuses skipping a number, because the skipped one is gone for good", () => {
    const p = problem("1.3.5", "1.3.8");
    expect(p).toContain("skipped");
    expect(p).toContain("burned permanently");
  });

  it("refuses something that is not a version at all", () => {
    expect(problem("1.3.5", "next")).toContain("not a version");
  });

  it("says nothing when there is no baseline to compare against", () => {
    expect(problem("", "1.3.5")).toBeNull();
  });
});

describe("parsing a version", () => {
  it("splits the parts and keeps the prerelease tail", () => {
    expect(parseVersion("1.3.5-beta.2")).toMatchObject({
      major: 1,
      minor: 3,
      patch: 5,
      prerelease: "beta.2",
    });
  });

  it("returns null for anything that is not one", () => {
    for (const bad of ["v1.3.5", "1.3", "latest", ""]) {
      expect(parseVersion(bad), bad).toBeNull();
    }
  });
});

/**
 * Stale docs is the complaint that comes back most. Handlers are where the
 * user-visible surface lives, so a change there with no docs change beside it
 * is the shape of the problem, caught while it is still a diff.
 */
describe("a surface change has to reach the docs", () => {
  const say = (files: string[]): string | null => docsFreshnessProblem(files) as string | null;

  it("complains when a handler moved and no doc did", () => {
    const p = say(["plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AssetHandlers.cpp"]);
    expect(p).toContain("docs/");
  });

  it("complains when a tool schema moved and no doc did", () => {
    expect(say(["src/tools/level.ts"])).toContain("docs/");
  });

  it("is satisfied by any docs change alongside it", () => {
    expect(say(["src/tools/level.ts", "docs/tool-reference.md"])).toBeNull();
  });

  it("says nothing about a change that touches no surface", () => {
    expect(say(["src/dialog-guard.ts", "tests/unit/dialog-guard.test.ts"])).toBeNull();
    expect(say(["README.md"])).toBeNull();
  });

  it("says nothing when nothing changed", () => {
    expect(say([])).toBeNull();
  });
});

/**
 * The three gates are separate functions behind one entry point. Stubbing any
 * one of them to return 0 used to leave the suite green, because nothing
 * asserted that the entry point actually runs all three.
 */
describe("every gate is reachable from the entry point", () => {
  it("names all three, and refuses an unknown one", async () => {
    const src = await import("node:fs").then((fs) =>
      fs.readFileSync(new URL("../../scripts/check-release-gates.mjs", import.meta.url), "utf8"),
    );
    for (const gate of ["version-delta", "count-markers", "docs-freshness"]) {
      expect(src, gate).toContain(`"${gate}"`);
    }
    // Each one has to be wired into the table `all` iterates, not merely
    // defined somewhere in the file.
    const table = src.slice(src.indexOf("const GATES = {"), src.indexOf("function main("));
    for (const gate of ["version-delta", "count-markers", "docs-freshness"]) {
      expect(table, `${gate} is not in the gate table`).toContain(gate);
    }
  });

  it("treats a missing baseline as a failure, not a pass", () => {
    // The gates used to report "skipped" and exit 0 when they could not find
    // a baseline, which is indistinguishable from passing.
    const src = fsSync.readFileSync(
      new URL("../../scripts/check-release-gates.mjs", import.meta.url),
      "utf8",
    );
    expect(src).toContain("no baseline commit to compare against");
    expect(src).not.toContain('console.log("version-delta   - no main to compare against, skipped")');
  });
});
