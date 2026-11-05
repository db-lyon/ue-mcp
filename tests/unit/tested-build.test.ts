import { describe, expect, it } from "vitest";

import { pickTestedBuild, WORKFLOW_PATH } from "../../scripts/tested-build.mjs";

const TREE = "a".repeat(40);
const good = (over: Record<string, unknown> = {}) => ({
  artifactId: 1,
  expired: false,
  createdAt: "2026-10-24T10:00:00Z",
  runId: 100,
  repositoryId: 7,
  headRepositoryId: 7,
  run: { event: "pull_request", conclusion: "success", path: WORKFLOW_PATH },
  headTree: TREE,
  ...over,
});

describe("pickTestedBuild", () => {
  it("takes a successful pull request run of ci.yml whose head has the tree", () => {
    expect(pickTestedBuild(TREE, [good()]).pick?.runId).toBe(100);
  });

  it("takes the newest qualifying build", () => {
    const { pick } = pickTestedBuild(TREE, [good({ runId: 1, createdAt: "2026-10-01T00:00:00Z" }), good({ runId: 2, createdAt: "2026-10-02T00:00:00Z" })]);
    expect(pick?.runId).toBe(2);
  });

  it.each([
    ["an expired artifact", { expired: true }, "expired"],
    ["a fork's run", { headRepositoryId: 8 }, "built for a fork"],
    ["a push run", { run: { event: "push", conclusion: "success", path: WORKFLOW_PATH } }, "from a push run"],
    ["another workflow", { run: { event: "pull_request", conclusion: "success", path: ".github/workflows/x.yml" } }, "from .github/workflows/x.yml"],
    ["a failed run", { run: { event: "pull_request", conclusion: "failure", path: WORKFLOW_PATH } }, "concluded failure"],
    ["a run whose head has another tree", { headTree: "b".repeat(40) }, "has tree"],
    ["a run it could not read", { run: null, headTree: null }, "from a unknown run"],
  ])("refuses %s, so the publish job builds the tree itself", (_label, over, why) => {
    const { pick, rejected } = pickTestedBuild(TREE, [good(over)]);
    expect(pick).toBeNull();
    expect(rejected[0].why).toContain(why);
  });

  it("falls through a refused newer build to a qualifying older one", () => {
    const { pick, rejected } = pickTestedBuild(TREE, [
      good({ runId: 1, createdAt: "2026-10-01T00:00:00Z" }),
      good({ runId: 2, createdAt: "2026-10-02T00:00:00Z", run: { event: "pull_request", conclusion: "cancelled", path: WORKFLOW_PATH } }),
    ]);
    expect(pick?.runId).toBe(1);
    expect(rejected).toEqual([{ runId: 2, why: "its run concluded cancelled" }]);
  });
});
