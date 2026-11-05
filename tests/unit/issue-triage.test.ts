import { describe, it, expect } from "vitest";
import { planActions, resolveDecision, sanitize, targetsFor } from "../../scripts/issue-triage.mjs";

const issue = { number: 10, state: "open", locked: false, isPullRequest: false, labels: [] };
const ctx = {
  repoLabels: new Set(["bug", "enhancement", "duplicate", "invalid", "limitation", "fixed-pending-release", "editor", "gas", "planned"]),
  latestRelease: "v1.4.7",
};
const base = { confidence: "high", kind: "bug", areas: ["editor"], summary: "It is covered there.", evidence: "" };

describe("issue-triage targets", () => {
  it("skips maintainers' own issues on open", () => {
    expect(targetsFor("issues", { issue: { number: 5, state: "open", author_association: "OWNER" } })).toEqual([]);
    expect(targetsFor("issues", { issue: { number: 5, state: "open", author_association: "NONE" } })).toEqual([5]);
  });

  it("takes any listed issue on dispatch", () => {
    expect(targetsFor("workflow_dispatch", {}, "#12, 13 x")).toEqual([12, 13]);
  });
});

describe("issue-triage sanitize", () => {
  it("drops mentions, markup, outside links and banned punctuation", () => {
    const out = sanitize(`Ping @someone <!-- hi --> see [x](https://evil.example) and https://github.com/db-lyon/ue-mcp/issues/1 ${String.fromCharCode(0x2014)} done...`);
    expect(out).toBe("Ping someone see x and https://github.com/db-lyon/ue-mcp/issues/1, done.");
  });
});

describe("issue-triage resolveDecision", () => {
  it("refuses a duplicate of itself or of a missing issue", () => {
    const d = { ...base, outcome: "duplicate", duplicateOf: 10 };
    expect(resolveDecision(d, { issue, duplicate: { ...issue } }, "act").outcome).toBe("needs-human");
    expect(resolveDecision({ ...d, duplicateOf: 99 }, { issue, duplicate: null }, "act").outcome).toBe("needs-human");
  });

  it("never closes below high confidence", () => {
    const d = { ...base, outcome: "duplicate", duplicateOf: 9, confidence: "medium" };
    const dup = { ...issue, number: 9 };
    expect(resolveDecision(d, { issue, duplicate: dup }, "act").outcome).toBe("needs-human");
    expect(resolveDecision({ ...d, confidence: "high" }, { issue, duplicate: dup }, "act").outcome).toBe("duplicate");
  });

  it("turns closures into proposals in propose mode, but not open outcomes", () => {
    const dup = { ...issue, number: 9 };
    expect(resolveDecision({ ...base, outcome: "duplicate", duplicateOf: 9 }, { issue, duplicate: dup }, "propose").outcome).toBe("proposed");
    expect(resolveDecision({ ...base, outcome: "accept" }, { issue }, "propose").outcome).toBe("accept");
  });

  it("requires the fixing commit to be on main", () => {
    const d = { ...base, outcome: "fixed", fixCommit: "abc1234" };
    expect(resolveDecision(d, { issue, fix: null }, "act").outcome).toBe("needs-human");
    expect(resolveDecision(d, { issue, fix: { onMain: false } }, "act").outcome).toBe("needs-human");
  });

  it("needs the transfer token to move to a sibling repo", () => {
    const d = { ...base, outcome: "move", moveTo: "ue-mcp-studio" };
    expect(resolveDecision(d, { issue, canTransfer: false }, "act").outcome).toBe("needs-human");
    expect(resolveDecision(d, { issue, canTransfer: true }, "act").outcome).toBe("move");
    expect(resolveDecision({ ...d, moveTo: "perforce", confidence: "low" }, { issue, canTransfer: false }, "act").outcome).toBe("move");
  });

  it("is silent when the agent returned nothing", () => {
    const r = resolveDecision(null, { issue }, "act");
    expect(r).toMatchObject({ outcome: "needs-human", silent: true });
    expect(planActions(r, null, ctx)).toMatchObject({ add: ["needs-triage"], comment: null, close: null });
  });
});

describe("issue-triage planActions", () => {
  it("closes a released fix and labels a pending one", () => {
    const d = { ...base, outcome: "fixed", fixCommit: "abc1234" };
    const released = resolveDecision(d, { issue, fix: { onMain: true, short: "abc12345", releasedIn: "v1.3.10" } }, "act");
    const p1 = planActions(released, d, ctx);
    expect(p1.close).toBe("completed");
    expect(p1.comment).toBe("Fixed in abc12345, released in v1.3.10. It is covered there. Comment here if it still reproduces on v1.4.7 or later.");

    const pending = resolveDecision({ ...d, confidence: "low" }, { issue, fix: { onMain: true, short: "abc12345", releasedIn: null, prerelease: null } }, "act");
    const p2 = planActions(pending, d, ctx);
    expect(p2.close).toBeNull();
    expect(p2.add).toContain("fixed-pending-release");
  });

  it("keeps area labels to the repo's own, never meta labels", () => {
    const d = { ...base, outcome: "accept", areas: ["editor", "planned", "made-up", "gas"] };
    const p = planActions(resolveDecision(d, { issue }, "act"), d, ctx);
    expect(p.add.sort()).toEqual(["bug", "editor", "gas", "triaged"]);
    expect(p.comment).toBeNull();
  });

  it("writes a proposal instead of closing", () => {
    const d = { ...base, outcome: "duplicate", duplicateOf: 9 };
    const r = resolveDecision(d, { issue, duplicate: { ...issue, number: 9 } }, "propose");
    const p = planActions(r, d, ctx);
    expect(p.close).toBeNull();
    expect(p.add).toContain("needs-triage");
    expect(p.comment).toContain("> Duplicate of #9. It is covered there.");
  });
});
