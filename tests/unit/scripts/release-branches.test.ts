/**
 * CI publishes from main and from experimental. A push to either compares
 * against that branch's previous commit, never against itself.
 */
import { describe, expect, it } from "vitest";
import { RELEASE_BRANCHES, baselineRef } from "../../../scripts/check-release-gates.mjs";

function fakeGit(head: string) {
  return (...args: string[]) => {
    if (args[0] === "rev-parse" && args[1] === "--abbrev-ref") return head;
    if (args[0] === "rev-parse") return "sha";
    if (args[0] === "merge-base") return "merge-base-sha";
    throw new Error(`unexpected git ${args.join(" ")}`);
  };
}

describe("release branches", () => {
  it("publishes from main and experimental", () => {
    expect(RELEASE_BRANCHES).toEqual(["main", "experimental"]);
  });

  it.each(["main", "experimental"])("a push to %s compares against HEAD~1", (branch) => {
    expect(baselineRef({ GITHUB_REF: `refs/heads/${branch}` }, fakeGit("HEAD"))).toBe("HEAD~1");
    expect(baselineRef({}, fakeGit(branch))).toBe("HEAD~1");
  });

  it("a feature branch compares against its merge base with main", () => {
    expect(baselineRef({}, fakeGit("feat/x"))).toBe("merge-base-sha");
  });
});
