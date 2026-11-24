/** The Playtest Hub's contract, as `ue-mcp/daemon` exports it: check health, acceptance, goal status, goal ids and run outcomes. */
import { describe, expect, it } from "vitest";
import {
  DEFAULT_ACCEPTANCE,
  DEFAULT_HEALTH_RULE,
  RESULT_OUTCOMES,
  checkHealth,
  checkId,
  checkSatisfied,
  checkStateOf,
  goalChecksDeep,
  goalLoops,
  goalParents,
  goalSlug,
  goalStatus,
  goalTree,
  loopIfRequired,
  requirementsKey,
  rollUpGoals,
  needsSignOff,
  signOffLapse,
  outcomeOf,
  parseCheckId,
  GOAL_ID,
  linkHost,
  trackerLinkOf,
  trackerUrlProblem,
  uniqueLinks,
  type CheckRef,
  type HealthRule,
  type CurrentProvenance,
  type Goal,
  type QaStep,
  type RunOutcome,
  type RunProvenance,
  type RunRecord,
  type SignOff,
} from "../../src/daemon-api.js";

describe("tracker links", () => {
  it("takes web, mail and app links, and refuses schemes that run code or read local data", () => {
    for (const ok of ["https://git.example.com/ember/hollow/issues/42", "http://localhost:3000/x", "mailto:qa@example.com", "jira://HOL-1", "trello:card/abc"]) expect(trackerUrlProblem(ok), ok).toBeNull();
    for (const bad of ["javascript:alert(1)", "JavaScript:x", "data:text/html,x", "vbscript:x", "file:///C:/x", "https://", "no-scheme.example.com", "https://a b", "", "jira:"]) expect(trackerUrlProblem(bad), bad).not.toBeNull();
  });

  it("normalizes a link and keeps one per URL", () => {
    expect(trackerLinkOf({ url: " https://x.y/1 ", title: "  T ", rel: "" })).toEqual({ link: { url: "https://x.y/1", title: "T" } });
    expect(trackerLinkOf({ url: "https://x.y", title: 3 })).toHaveProperty("problem");
    expect(uniqueLinks([{ url: "a:1" }, { url: "a:1", title: "b" }, { url: "a:2" }])).toEqual([{ url: "a:1" }, { url: "a:2" }]);
  });

  it("names a link by its host, its address, or its app", () => {
    expect(linkHost("https://www.Example.com:8080/a?b")).toBe("example.com");
    expect(linkHost("https://user@git.example.com/x")).toBe("git.example.com");
    expect(linkHost("mailto:qa%40x@example.com?subject=1")).toBe("qa@x@example.com");
    expect(linkHost("jira://HOL-1")).toBe("jira");
    expect(linkHost("nonsense")).toBe("");
  });
});

const NOW = Date.parse("2026-11-23T12:00:00Z");
const DAY = 86_400_000;
const script: CheckRef = { kind: "script", target: "/Game/Tests/Door" };

let n = 0;
/** What a run tested: by default the workspace HEAD, clean, at the script's first version. */
const prov = (over: Partial<RunProvenance> = {}): RunProvenance => ({ vcs: "git", revision: "head", dirty: 0, scriptHash: "sha256:v1", engine: "5.5", players: null, ...over });
const HEAD: CurrentProvenance = { vcs: "git", revision: "head", dirty: 0 };
/** Runs newest first, each an hour older than the one before, all on the same provenance. */
const runs = (...outcomes: RunOutcome[]): RunRecord[] => outcomes.map((outcome, i) => {
  const at = new Date(NOW - (i + 1) * 3600_000).toISOString();
  return { runId: `r${n++}`, kind: "script", target: script.target, name: "Door", outcome, startedAt: at, endedAt: at, source: "person", provenance: prov() };
});
/** The same runs, each given its own provenance, trigger or none. */
const tested = (rs: RunRecord[], ...over: Array<Partial<RunRecord> | null>): RunRecord[] => rs.map((r, i) => {
  const o = over[i];
  if (o === null) {
    const { provenance: _none, ...rest } = r;
    return rest;
  }
  return { ...r, ...o };
});

const goal = (over: Partial<Goal> = {}): Goal => ({
  schema: 1, id: "g", kind: "goal", title: "G", acceptance: { ...DEFAULT_ACCEPTANCE }, checks: [script], ...over,
});
const state = (rs: RunRecord[], ref: CheckRef = script, rule: HealthRule = DEFAULT_HEALTH_RULE, missing = false) =>
  ({ state: checkStateOf({ check: ref, runs: rs, rule, now: NOW, goalIds: ["g"], missing }), runs: rs });
const staleAfter14: HealthRule = { ...DEFAULT_HEALTH_RULE, staleAfterDays: 14 };

describe("checkHealth", () => {
  it("missing first, whatever the runs say", () => {
    expect(checkHealth({ check: script, runs: runs("passed"), missing: true, now: NOW })).toBe("missing");
  });

  it("a live run is running, also while it waits for a verdict: the verdict is asked for live, never as a status", () => {
    expect(checkHealth({ check: script, runs: runs("needs_you", "passed"), now: NOW })).toBe("running");
    expect(checkHealth({ check: script, runs: runs("running", "failed"), now: NOW })).toBe("running");
  });

  it("never run with no finished run", () => {
    expect(checkHealth({ check: script, runs: [], now: NOW })).toBe("never_run");
  });

  it("failing when the newest result did not pass, incomplete included", () => {
    expect(checkHealth({ check: script, runs: runs("failed", "passed"), now: NOW })).toBe("failing");
    expect(checkHealth({ check: script, runs: runs("incomplete"), now: NOW })).toBe("failing");
  });

  it("a stopped or unjudged run is no result: the run before it speaks for the check", () => {
    expect(checkHealth({ check: script, runs: runs("stopped", "passed"), now: NOW })).toBe("passing");
    expect(checkHealth({ check: script, runs: runs("unjudged", "passed"), now: NOW })).toBe("passing");
    expect(checkSatisfied(runs("unjudged", "passed"), DEFAULT_ACCEPTANCE)).toBe(true);
    expect(checkHealth({ check: script, runs: runs("stopped", "failed"), now: NOW })).toBe("failing");
    expect(checkHealth({ check: script, runs: runs("stopped"), now: NOW })).toBe("never_run");
  });

  it("flaky when a failure sits in the project's window behind a pass, whichever goal looks", () => {
    expect(checkHealth({ check: script, runs: runs("passed", "failed", "passed"), now: NOW })).toBe("flaky");
    // The window is the project's rule, 3 by default: a failure four runs back is outside it.
    expect(checkHealth({ check: script, runs: runs("passed", "passed", "passed", "failed"), now: NOW })).toBe("passing");
    expect(checkHealth({ check: script, runs: runs("passed", "passed", "passed", "failed"), rule: { ...DEFAULT_HEALTH_RULE, flakyWindow: 4 }, now: NOW })).toBe("flaky");
  });

  it("stale when the newest pass is older than the rule's days, and never by age by default", () => {
    const old: RunRecord = { ...runs("passed")[0], startedAt: new Date(NOW - 20 * DAY).toISOString(), endedAt: new Date(NOW - 20 * DAY).toISOString() };
    expect(checkHealth({ check: script, runs: [old], now: NOW })).toBe("passing");
    expect(checkHealth({ check: script, runs: [old], rule: staleAfter14, now: NOW })).toBe("stale");
  });
});

describe("checkSatisfied", () => {
  it("the latest stableRuns finished runs all passed, and there are that many", () => {
    const two = { ...DEFAULT_ACCEPTANCE, stableRuns: 2 };
    expect(checkSatisfied(runs("passed"), DEFAULT_ACCEPTANCE)).toBe(true);
    expect(checkSatisfied(runs("passed"), two)).toBe(false);
    expect(checkSatisfied(runs("passed", "passed"), two)).toBe(true);
    expect(checkSatisfied(runs("passed", "failed"), two)).toBe(false);
  });

  it("live runs do not count either way", () => {
    expect(checkSatisfied(runs("running", "passed"), DEFAULT_ACCEPTANCE)).toBe(true);
    expect(checkSatisfied(runs("needs_you"), DEFAULT_ACCEPTANCE)).toBe(false);
  });
});

describe("provenance", () => {
  const two = { ...DEFAULT_ACCEPTANCE, stableRuns: 2 };

  it("flaky only when results are mixed on the same revision and script version", () => {
    expect(checkHealth({ check: script, runs: runs("passed", "failed"), now: NOW })).toBe("flaky");
    // A fail on an older revision, then a pass: fixed, not flaky.
    const fixed = tested(runs("passed", "failed"), {}, { provenance: prov({ revision: "old" }) });
    expect(checkHealth({ check: script, runs: fixed, now: NOW })).toBe("passing");
    expect(checkStateOf({ check: script, runs: fixed, now: NOW, goalIds: [] }).change).toMatchObject({ kind: "fixed", runId: fixed[0].runId });
    // The script was edited between them: also a change, not flakiness.
    expect(checkHealth({ check: script, runs: tested(runs("passed", "failed"), {}, { provenance: prov({ scriptHash: "sha256:v0" }) }), now: NOW })).toBe("passing");
  });

  it("reads a fail after a pass on another revision as regressed, and on the same one as no change", () => {
    const broke = tested(runs("failed", "passed"), {}, { provenance: prov({ revision: "old" }) });
    expect(checkStateOf({ check: script, runs: broke, now: NOW, goalIds: [] }).change).toMatchObject({ kind: "regressed", runId: broke[0].runId });
    expect(checkStateOf({ check: script, runs: runs("failed", "passed"), now: NOW, goalIds: [] }).change).toBeUndefined();
    expect(checkStateOf({ check: script, runs: runs("passed", "passed"), now: NOW, goalIds: [] }).change).toBeUndefined();
  });

  it("never counts replays or fix loop attempts toward health, pass rate or acceptance", () => {
    const rs = tested(runs("failed", "passed", "passed"), { trigger: "replay", replayOf: "x" }, { trigger: "loop" });
    expect(checkHealth({ check: script, runs: rs, now: NOW })).toBe("passing");
    const s = checkStateOf({ check: script, runs: rs, now: NOW, goalIds: [] });
    expect(s.recent).toEqual(["passed"]);
    expect(s.passRate).toBe(1);
    expect(s.last?.runId).toBe(rs[0].runId);
    // A loop's pass does not make a failing check pass.
    expect(checkHealth({ check: script, runs: tested(runs("passed", "failed"), { trigger: "loop" }), now: NOW })).toBe("failing");
    expect(checkSatisfied(tested(runs("passed", "passed"), { trigger: "loop" }), two, HEAD)).toBe(false);
    // A record kept before triggers were is a replay by its replayOf.
    expect(checkHealth({ check: script, runs: tested(runs("failed", "passed"), { replayOf: "x" }), now: NOW })).toBe("passing");
  });

  it("takes a dirty run as the newest word for health, but not for flakiness or acceptance", () => {
    const dirty = tested(runs("failed", "passed"), { provenance: prov({ dirty: 3 }) });
    expect(checkHealth({ check: script, runs: dirty, now: NOW })).toBe("failing");
    const passedDirty = tested(runs("passed", "failed"), { provenance: prov({ dirty: 3 }) });
    expect(checkHealth({ check: script, runs: passedDirty, now: NOW })).toBe("passing");
    expect(checkSatisfied(passedDirty, DEFAULT_ACCEPTANCE, HEAD)).toBe(false);
    expect(checkSatisfied(tested(runs("passed", "passed"), { provenance: prov({ dirty: 1 }) }), DEFAULT_ACCEPTANCE, HEAD)).toBe(true);
  });

  it("counts stable runs only on the current revision and the newest script version", () => {
    expect(checkSatisfied(runs("passed", "passed"), two, HEAD)).toBe(true);
    expect(checkSatisfied(runs("passed", "passed"), two, { ...HEAD, revision: "next" })).toBe(false);
    expect(checkSatisfied(tested(runs("passed", "passed"), {}, { provenance: prov({ revision: "old" }) }), two, HEAD)).toBe(false);
    expect(checkSatisfied(tested(runs("passed", "passed"), {}, { provenance: prov({ scriptHash: "sha256:v0" }) }), two, HEAD)).toBe(false);
    // A project under no source control compares nothing but the script.
    const none = { vcs: "none" as const, revision: null, dirty: null };
    expect(checkSatisfied(tested(runs("passed"), { provenance: prov({ vcs: "none", revision: null, dirty: null }) }), DEFAULT_ACCEPTANCE, none)).toBe(true);
  });

  it("never treats a run of unknown revision as current", () => {
    const old = tested(runs("passed", "passed"), null, null);
    expect(checkHealth({ check: script, runs: old, now: NOW })).toBe("passing");
    expect(checkSatisfied(old, DEFAULT_ACCEPTANCE)).toBe(false);
    expect(checkSatisfied(old, DEFAULT_ACCEPTANCE, HEAD)).toBe(false);
    expect(checkSatisfied(tested(runs("passed"), { provenance: prov({ revision: null }) }), DEFAULT_ACCEPTANCE, HEAD)).toBe(false);
    // Unknown never matches, so mixed old results are a change, not flakiness.
    expect(checkHealth({ check: script, runs: tested(runs("passed", "failed"), null, null), now: NOW })).toBe("passing");
    expect(goalStatus(goal(), [{ state: checkStateOf({ check: script, runs: old, now: NOW, goalIds: ["g"], current: HEAD }), runs: old }], [], [], HEAD).status).toBe("in_progress");
  });

  it("is stale when a dependency changed since the pass, where both have fingerprints, else by age", () => {
    const pass = tested(runs("passed"), { provenance: prov({ dependencies: { "/Game/Maps/Glade": "a", "/Game/BP_Door": "b" } }) });
    expect(checkHealth({ check: script, runs: pass, now: NOW, current: { ...HEAD, dependencies: { "/Game/BP_Door": "b" } } })).toBe("passing");
    expect(checkHealth({ check: script, runs: pass, now: NOW, current: { ...HEAD, dependencies: { "/Game/BP_Door": "c" } } })).toBe("stale");
    const oldPass = tested(pass, { startedAt: new Date(NOW - 20 * DAY).toISOString(), endedAt: new Date(NOW - 20 * DAY).toISOString() });
    // Fingerprints that say nothing changed outweigh age; none at all fall back to it.
    expect(checkHealth({ check: script, runs: oldPass, now: NOW, rule: staleAfter14, current: { ...HEAD, dependencies: { "/Game/BP_Door": "b" } } })).toBe("passing");
    expect(checkHealth({ check: script, runs: oldPass, now: NOW, rule: staleAfter14, current: HEAD })).toBe("stale");
  });
});

describe("checkStateOf", () => {
  it("recent and passRate from finished runs only", () => {
    const s = checkStateOf({ check: script, runs: runs("running", "passed", "failed"), now: NOW, goalIds: [] });
    expect(s.id).toBe("script:/Game/Tests/Door");
    expect(s.name).toBe("Door");
    expect(s.recent).toEqual(["passed", "failed"]);
    expect(s.passRate).toBe(0.5);
    expect(s.last?.outcome).toBe("running");
    expect(checkStateOf({ check: script, runs: [], goalIds: [] }).passRate).toBeNull();
  });
});

describe("goalStatus", () => {
  it("not started with no checks, or none run", () => {
    expect(goalStatus(goal({ checks: [] }), []).status).toBe("not_started");
    expect(goalStatus(goal(), [state([])]).status).toBe("not_started");
  });

  it("met when every required check passed, with progress", () => {
    const r = goalStatus(goal(), [state(runs("passed"))]);
    expect(r).toEqual({ status: "met", progress: { met: 1, total: 1 } });
  });

  it("failing when a required check fails; an optional one does not count", () => {
    expect(goalStatus(goal(), [state(runs("failed"))]).status).toBe("failing");
    const optional: CheckRef = { kind: "script", target: "/Game/Tests/Extra", required: false };
    const g = goal({ checks: [script, optional] });
    expect(goalStatus(g, [state(runs("passed")), state(runs("failed"), optional)]).status).toBe("met");
  });

  it("a required check missing on this checkout fails nothing: the goal waits for it", () => {
    const other: CheckRef = { kind: "script", target: "/Game/Tests/Other" };
    expect(goalStatus(goal(), [state(runs("passed"), script, DEFAULT_HEALTH_RULE, true)]).status).toBe("not_started");
    expect(goalStatus(goal({ checks: [script, other] }), [state(runs("passed")), state(runs("passed"), other, DEFAULT_HEALTH_RULE, true)])).toEqual({ status: "in_progress", progress: { met: 1, total: 2 } });
  });

  it("failing above a run that holds for a verdict, which is only running", () => {
    const other: CheckRef = { kind: "script", target: "/Game/Tests/Other" };
    expect(goalStatus(goal({ checks: [script, other] }), [state(runs("needs_you")), state(runs("failed"), other)]).status).toBe("failing");
    expect(goalStatus(goal({ checks: [script, other] }), [state(runs("needs_you")), state(runs("passed"), other)]).status).toBe("running");
  });

  it("at risk only when met but flaky or stale; partial evidence is in progress", () => {
    expect(goalStatus(goal(), [state(runs("passed", "failed", "passed"))]).status).toBe("at_risk");
    const other: CheckRef = { kind: "script", target: "/Game/Tests/Other" };
    expect(goalStatus(goal({ checks: [script, other] }), [state(runs("passed")), state([], other)]).status).toBe("in_progress");
  });

  it("sign-off: ready to sign off once everything passes, met once given", () => {
    const g = goal({ acceptance: { ...DEFAULT_ACCEPTANCE, signOff: true } });
    expect(needsSignOff(g)).toBe(true);
    expect(goalStatus(g, [state(runs("passed"))]).status).toBe("ready_to_sign_off");
    expect(goalStatus(g, [state(runs("passed"))], [], [], undefined, true).status).toBe("met");
  });

  it("a milestone follows its goals", () => {
    const m = goal({ id: "m", kind: "milestone", checks: [] });
    expect(goalStatus(m, [], ["met", "met"]).status).toBe("met");
    expect(goalStatus(m, [], ["met", "failing"]).status).toBe("failing");
    expect(goalStatus(m, [], ["met", "at_risk"]).status).toBe("at_risk");
    expect(goalStatus(m, [], ["not_started"]).status).toBe("not_started");
  });

  it("an optional goal never blocks, but a run in it shows", () => {
    const m = goal({ id: "m", kind: "milestone", checks: [] });
    expect(goalStatus(m, [], ["met"], ["failing"]).status).toBe("met");
    expect(goalStatus(m, [], ["met"], ["running"]).status).toBe("running");
  });
});

describe("the relationship graph", () => {
  const g = (id: string, requires: Array<string | { goal: string; required?: boolean }> = [], over: Partial<Goal> = {}): Goal =>
    goal({ id, checks: [], requires: requires.map((r) => (typeof r === "string" ? { goal: r } : r)), ...over });
  const door: CheckRef = { kind: "script", target: "/Game/Tests/Door" };
  const jump: CheckRef = { kind: "script", target: "/Game/Tests/Jump" };
  const swim: CheckRef = { kind: "script", target: "/Game/Tests/Swim" };

  it("rolls status up through a goal shared by two milestones, working each goal out once", () => {
    const shared = g("shared", [], { checks: [door] });
    const goals = [g("a", ["shared", "ok"], { kind: "milestone" }), g("b", ["shared"], { kind: "milestone" }), shared, g("ok", [], { checks: [jump] }), g("loose", [], { checks: [jump] })];
    let asked = 0;
    const out = rollUpGoals(goals, (x) => {
      asked++;
      return x.checks.map((c) => state(c === door ? runs("failed") : runs("passed"), c));
    });
    expect(asked).toBe(goals.length);
    const by = (id: string) => out.find((s) => s.goal.id === id)!;
    expect(by("shared").status).toBe("failing");
    expect(by("a").status).toBe("failing");
    expect(by("b").status).toBe("failing");
    expect(by("ok").status).toBe("met");
    expect(by("shared").parents).toEqual(["a", "b"]);
    expect(by("a").requires).toEqual([{ goal: "shared", required: true, status: "failing" }, { goal: "ok", required: true, status: "met" }]);
    expect(by("loose").unused).toBe(true);
    expect(by("shared").unused).toBeUndefined();
    expect(by("a").unused).toBeUndefined();
  });

  it("goes three deep, and an optional goal does not hold its parent back", () => {
    const goals = [g("top", ["mid", { goal: "extra", required: false }], { kind: "milestone" }), g("mid", ["leaf"]), g("leaf", [], { checks: [door] }), g("extra", [], { checks: [jump] })];
    const out = rollUpGoals(goals, (x) => x.checks.map((c) => state(c === jump ? runs("failed") : runs("passed"), c)));
    expect(out.map((s) => s.status)).toEqual(["met", "met", "met", "failing"]);
  });

  it("finds loops the same way every time, and rolls status up without going round", () => {
    const goals = [g("c", ["a"]), g("a", ["b"]), g("b", ["c"]), g("d", ["missing"])];
    expect(goalLoops(goals).cycles).toEqual([["a", "b", "c", "a"]]);
    expect([...goalLoops(goals).edges]).toEqual(["c>a"]);
    const out = rollUpGoals(goals, () => []);
    expect(out.map((s) => s.status)).toEqual(["not_started", "not_started", "not_started", "not_started"]);
    expect(out.find((s) => s.goal.id === "d")!.requires).toEqual([{ goal: "missing", required: true, status: null }]);
    expect(loopIfRequired(goals, "d", "d")).toEqual(["d", "d"]);
    expect(loopIfRequired([g("x", ["y"]), g("y")], "y", "x")).toEqual(["y", "x", "y"]);
    expect(loopIfRequired([g("x", ["y"]), g("y")], "x", "y")).toBeNull();
  });

  it("a goal's checks are its required goals' at any depth, then its own, each once", () => {
    const goals = [g("m", ["a", "b"], { checks: [swim] }), g("a", ["b"], { checks: [jump, door] }), g("b", [], { checks: [door] }), g("gone", [], { archived: true, checks: [swim] })];
    expect(goalTree(goals, "m")).toEqual(["b", "a", "m"]);
    expect(goalChecksDeep(goals, "m").map((c) => c.target)).toEqual([door.target, jump.target, swim.target]);
    // Archived goals are left out, and a loop is followed once.
    expect(goalChecksDeep([g("x", ["gone", "y"]), g("y", ["x"], { checks: [jump] }), goals[3]], "x").map((c) => c.target)).toEqual([jump.target]);
  });

  it("parents leave out archived goals; a check no goal lists is unused", () => {
    expect([...goalParents([g("m", ["a"]), g("old", ["a"], { archived: true })])]).toEqual([["a", ["m"]]]);
    expect(checkStateOf({ check: door, runs: [], goalIds: [] }).unused).toBe(true);
    expect(checkStateOf({ check: door, runs: [], goalIds: ["g"] }).unused).toBeUndefined();
  });
});

describe("sign-off", () => {
  const g = goal({ acceptance: { ...DEFAULT_ACCEPTANCE, signOff: true } });
  const signed = (rs: RunRecord[], over: Partial<SignOff> = {}): SignOff => ({
    goalId: "g", by: "octocat", at: new Date(NOW - 30 * 60_000).toISOString(), revision: "head", runIds: [rs[0].runId], requirements: requirementsKey(g), ...over,
  });

  it("the requirements ignore order and optional entries, and change with acceptance, required checks and required goals", () => {
    const other: CheckRef = { kind: "script", target: "/Game/Tests/Other" };
    const two = goal({ checks: [script, other], requires: [{ goal: "a" }, { goal: "b" }] });
    expect(requirementsKey({ ...two, checks: [other, script], requires: [{ goal: "b" }, { goal: "a" }] })).toBe(requirementsKey(two));
    expect(requirementsKey({ ...two, checks: [...two.checks, { ...script, target: "/Game/Opt", required: false }] })).toBe(requirementsKey(two));
    expect(requirementsKey({ ...two, checks: [script] })).not.toBe(requirementsKey(two));
    expect(requirementsKey({ ...two, requires: [{ goal: "a" }] })).not.toBe(requirementsKey(two));
    expect(requirementsKey({ ...two, acceptance: { ...two.acceptance, stableRuns: 2 } })).not.toBe(requirementsKey(two));
  });

  it("holds while it describes the goal, and counts toward its status only then", () => {
    const rs = runs("passed");
    const s = signed(rs);
    expect(signOffLapse(s, g, [state(rs)], HEAD)).toBeNull();
    const [st] = rollUpGoals([g], () => [state(rs)], HEAD, new Map([["g", s]]));
    expect(st).toMatchObject({ status: "met", signOff: { record: s, lapsed: null } });
    // Without the workspace the revision is not compared.
    expect(signOffLapse({ ...s, revision: "old" }, g, [state(rs)])).toBeNull();
  });

  it("lapses when the requirements change", () => {
    const rs = runs("passed");
    expect(signOffLapse(signed(rs), { ...g, acceptance: { ...g.acceptance, stableRuns: 2 } }, [state(rs)], HEAD)).toBe("requirements");
  });

  it("lapses when a required check fails after it, but not for an optional one or a failure before it", () => {
    const before = runs("passed", "failed");
    expect(signOffLapse(signed(before), g, [state(before)], HEAD)).toBeNull();
    const after = tested(runs("failed"), { startedAt: new Date(NOW).toISOString(), endedAt: new Date(NOW).toISOString() });
    expect(signOffLapse(signed(before), g, [state([...after, ...before])], HEAD)).toBe("failed");
    const optional: CheckRef = { ...script, required: false };
    const loose = { ...g, checks: [optional] };
    expect(signOffLapse(signed(before, { requirements: requirementsKey(loose) }), loose, [state([...after, ...before], optional)], HEAD)).toBeNull();
    const [st] = rollUpGoals([g], () => [state([...after, ...before])], HEAD, new Map([["g", signed(before)]]));
    expect(st.signOff?.lapsed).toBe("failed");
    expect(st.status).toBe("failing");
  });

  it("past its revision, holds only once the check passed there on the script and map it judged", () => {
    const old = tested(runs("passed"), { provenance: prov({ revision: "old" }), map: "/Game/Maps/A" });
    const s = signed(old, { revision: "old" });
    // Nothing has run on the new revision yet: unknown never holds.
    expect(signOffLapse(s, g, [state(old)], HEAD)).toBe("revision");
    const same = tested(runs("passed"), { map: "/Game/Maps/A" });
    expect(signOffLapse(s, g, [state([...same, ...old])], HEAD)).toBeNull();
    const script2 = tested(runs("passed"), { provenance: prov({ scriptHash: "sha256:v2" }), map: "/Game/Maps/A" });
    expect(signOffLapse(s, g, [state([...script2, ...old])], HEAD)).toBe("revision");
    const otherMap = tested(runs("passed"), { map: "/Game/Maps/B" });
    expect(signOffLapse(s, g, [state([...otherMap, ...old])], HEAD)).toBe("revision");
  });
});

describe("goalSlug and ids", () => {
  it("lowercase words joined by dashes, at most 64 characters, never empty", () => {
    expect(goalSlug("A new player finishes the tutorial!")).toBe("a-new-player-finishes-the-tutorial");
    expect(goalSlug("Café  au lait")).toBe("cafe-au-lait");
    expect(goalSlug("!!!")).toBe("goal");
    const long = goalSlug("word ".repeat(40));
    expect(long.length).toBeLessThanOrEqual(64);
    expect(long.endsWith("-")).toBe(false);
    expect(GOAL_ID.test(long)).toBe(true);
    // Only ASCII letters and digits, so the slug is always a valid id; room is left for a suffix when asked.
    expect(goalSlug("ジャンプ")).toBe("goal");
    expect(goalSlug("Naïve über jump", 10)).toBe("naive-uber");
  });

  it("check ids round-trip, and a bad one is null", () => {
    expect(parseCheckId(checkId(script))).toEqual({ kind: "script", target: "/Game/Tests/Door" });
    expect(parseCheckId("script:/Game/Has: a colon")).toEqual({ kind: "script", target: "/Game/Has: a colon" });
    // Only playtests are checks this Studio knows.
    expect(parseCheckId("manual:Looks right")).toBeNull();
    expect(parseCheckId("nope:x")).toBeNull();
    expect(parseCheckId("script:")).toBeNull();
  });
});

describe("outcomeOf", () => {
  const step = (status: QaStep["status"], judge: QaStep["judge"] = "auto"): QaStep => ({ index: 0, instruction: "", judge, status });
  const run = (state: Parameters<typeof outcomeOf>[0]["state"], steps: QaStep[]) => outcomeOf({ state, steps: steps.map((s, index) => ({ ...s, index })), summary: undefined as never });

  it("needs you while a live run waits for a verdict; not judged, no result, once it ended without one", () => {
    expect(run("awaiting_verdict", [step("passed"), step("awaiting_verdict", "manual")])).toBe("needs_you");
    expect(run("stopped", [step("passed"), step("awaiting_verdict", "manual"), step("pending")])).toBe("unjudged");
    expect(run("finished", [step("passed"), step("awaiting_verdict", "manual")])).toBe("unjudged");
    // A failure before the hold still makes the run failed.
    expect(run("stopped", [step("failed"), step("awaiting_verdict", "manual")])).toBe("failed");
    expect(RESULT_OUTCOMES).not.toContain("unjudged");
  });

  it("running, or failed as soon as a step fails", () => {
    expect(run("running", [step("passed"), step("pending")])).toBe("running");
    expect(run("armed", [])).toBe("running");
    expect(run("running", [step("failed"), step("pending")])).toBe("failed");
  });

  it("an ended run: failed, stopped, incomplete or passed", () => {
    expect(run("finished", [step("passed"), step("failed")])).toBe("failed");
    expect(run("stopped", [step("passed"), step("pending")])).toBe("stopped");
    expect(run("finished", [step("passed"), step("pending")])).toBe("incomplete");
    expect(run("finished", [])).toBe("incomplete");
    expect(run("finished", [step("passed"), step("skipped")])).toBe("passed");
  });

  it("a stopped run whose every step settled is judged by its steps", () => {
    expect(run("stopped", [step("passed"), step("passed", "manual")])).toBe("passed");
    expect(run("stopped", [step("passed"), step("failed", "manual")])).toBe("failed");
    expect(run("stopped", [])).toBe("stopped");
  });
});
