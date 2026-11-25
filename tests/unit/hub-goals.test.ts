/**
 * The goal store and its rules: goals as one file per record outside the
 * project, versioned and never taken back by id; the goal graph; what a
 * check says about itself; sign-offs and the health rule as records; the
 * one-time, read-only import of QA/Goals; and moving the store as a file.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { HUB_EXPORT_FORMAT, type HubExport, type SignOff } from "../../src/hub/contract.js";
import { GoalFiles } from "../../src/hub/goal-files.js";
import { Goals } from "../../src/hub/goals.js";

let root: string;
let project: string;
let data: string;
let changes: Array<string | undefined>;

const goals = (dir = data) => new Goals({ store: new GoalFiles(dir, () => "2026-11-23T10:00:00.000Z"), projectDir: project, now: () => "2026-11-23T10:00:00.000Z", changed: (id) => void changes.push(id) });
const record = (folder: string, id: string, dir = data) => JSON.parse(fs.readFileSync(path.join(dir, "hub", folder, `${encodeURIComponent(id)}.json`), "utf-8"));
const writeLegacy = (name: string, v: unknown) => {
  fs.mkdirSync(path.join(project, "QA", "Goals"), { recursive: true });
  fs.writeFileSync(path.join(project, "QA", "Goals", name), typeof v === "string" ? v : JSON.stringify(v));
};
const signOff = (goalId: string, at = "2026-11-23T11:00:00.000Z"): SignOff => ({ goalId, by: "ama", at, revision: "abc", runIds: ["r1"], requirements: "{}" });

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-goals-"));
  project = path.join(root, "Game");
  data = path.join(root, "data");
  fs.mkdirSync(project, { recursive: true });
  changes = [];
});
afterEach(() => fs.rmSync(root, { recursive: true, force: true }));

describe("the goal store on this computer", () => {
  it("keeps each goal as its own versioned record under the data folder, and nothing in the project", () => {
    const g = goals();
    const goal = g.create({ title: "A new player finishes the tutorial", owner: "Ama", priority: "p1", due: "2026-12-05" });
    expect(goal.id).toMatch(/^a-new-player-finishes-the-tutorial-[a-z0-9]{4}$/);
    const r = record("goals", goal.id);
    expect(r).toMatchObject({ schema: 1, type: "goal", id: goal.id, rev: 1, at: "2026-11-23T10:00:00.000Z", data: { title: "A new player finishes the tutorial", owner: "Ama", priority: "p1", due: "2026-12-05" } });
    expect(typeof r.origin).toBe("string");
    expect(JSON.parse(fs.readFileSync(path.join(data, "hub", "store.json"), "utf-8")).origin).toBe(r.origin);
    g.update(goal.id, { owner: null, priority: null });
    expect(record("goals", goal.id)).toMatchObject({ rev: 2 });
    expect(record("goals", goal.id).data.owner).toBeUndefined();
    expect(fs.readdirSync(project)).toEqual([]);
    expect(goals().get(goal.id)?.title).toBe("A new player finishes the tutorial");
  });

  it("a removal is a version with no data, and the id is never given again", () => {
    const g = goals();
    g.create({ id: "jump", title: "Jump" });
    g.delete("jump");
    expect(record("goals", "jump")).toMatchObject({ rev: 2, deleted: true });
    expect(record("goals", "jump").data).toBeUndefined();
    expect(g.get("jump")).toBeUndefined();
    expect(() => g.create({ id: "jump", title: "Jump again" })).toThrow(/exists, or did/);
  });

  it("each store has its own origin, so records made on two computers say where they came from", () => {
    goals();
    const other = path.join(root, "other");
    goals(other);
    const a = JSON.parse(fs.readFileSync(path.join(data, "hub", "store.json"), "utf-8")).origin;
    const b = JSON.parse(fs.readFileSync(path.join(other, "hub", "store.json"), "utf-8")).origin;
    expect(a).not.toBe(b);
  });

  it("names a record file that does not read, and reads the rest", () => {
    goals().create({ id: "ok", title: "Fine" });
    fs.writeFileSync(path.join(data, "hub", "goals", "bad.json"), "{ nope");
    fs.writeFileSync(path.join(data, "hub", "goals", "newer.json"), JSON.stringify({ schema: 9, type: "goal", id: "newer" }));
    const g = goals();
    expect(g.list().map((x) => x.id)).toEqual(["ok"]);
    expect(g.warnings().join("\n")).toMatch(/goals\/bad\.json was not read/);
    expect(g.warnings().join("\n")).toMatch(/newer ue-mcp/);
  });
});

describe("goals", () => {
  it("validates fields and keeps what it was not given", () => {
    const g = goals();
    const goal = g.create({ title: "Door", acceptance: { stableRuns: 3 }, checks: [{ kind: "script", target: "/Game/Tests/PT_Door.PT_Door" }] });
    expect(goal.checks).toEqual([{ kind: "script", target: "/Game/Tests/PT_Door" }]);
    expect(() => g.update(goal.id, { priority: "p9" })).toThrow(/priority/);
    expect(() => g.update(goal.id, { due: "soon" })).toThrow(/due/);
    expect(() => g.update(goal.id, { acceptance: { staleAfterDays: 3 } })).toThrow(/health rule/);
    expect(() => g.update(goal.id, { checks: [{ kind: "uat", target: "x" }] })).toThrow(/kind: script/);
    expect(g.update(goal.id, { description: "Opens" })).toMatchObject({ description: "Opens", acceptance: { stableRuns: 3, signOff: false }, checks: goal.checks });
  });

  it("a goal sits under any number of milestones, the edge held by each milestone", () => {
    const g = goals();
    const a = g.create({ id: "slice", kind: "milestone", title: "Slice" });
    const b = g.create({ id: "demo", kind: "milestone", title: "Demo" });
    g.create({ id: "jump", title: "Jump", parents: [a.id, b.id] });
    expect(g.get("slice")?.requires).toEqual([{ goal: "jump" }]);
    expect(g.get("demo")?.requires).toEqual([{ goal: "jump" }]);
    expect(g.get("jump")?.requires).toBeUndefined();
    g.update("jump", { parents: ["demo"] });
    expect(g.get("slice")?.requires).toBeUndefined();
    g.delete("jump");
    expect(g.get("demo")?.requires).toBeUndefined();
  });

  it("refuses a loop with its path, and reports a required goal that is gone", () => {
    const g = goals();
    g.create({ id: "a", title: "A" });
    g.create({ id: "b", title: "B", requires: [{ goal: "a" }] });
    expect(() => g.addRequire("a", { goal: "b" })).toThrow(/a > b > a/);
    expect(() => g.update("a", { parents: ["b"] })).not.toThrow();
    expect(() => g.update("a", { requires: [{ goal: "b" }] })).toThrow(/loop/);
  });

  it("adds, re-flags, reorders and removes required goals and checks", () => {
    const g = goals();
    g.create({ id: "a", title: "A" });
    g.create({ id: "b", title: "B" });
    g.create({ id: "m", title: "M" });
    g.addRequire("m", { goal: "a" });
    g.addRequire("m", { goal: "b", required: false });
    g.addRequire("m", { goal: "a", required: false });
    expect(g.get("m")?.requires).toEqual([{ goal: "a", required: false }, { goal: "b", required: false }]);
    g.orderRequires("m", { order: ["b", "a"] });
    g.removeRequire("m", "a");
    expect(g.get("m")?.requires).toEqual([{ goal: "b", required: false }]);
    g.addChecks("a", [{ kind: "script", target: "/Game/T/One" }, { kind: "script", target: "/Game/T/Two" }, { kind: "script", target: "/Game/T/One" }]);
    g.orderChecks("a", { order: ["script:/Game/T/Two", "script:/Game/T/One"] });
    g.updateGoalCheck("a", "script:/Game/T/One", { required: false });
    expect(g.get("a")?.checks).toEqual([{ kind: "script", target: "/Game/T/Two" }, { kind: "script", target: "/Game/T/One", required: false }]);
    g.removeCheck("a", "script:/Game/T/Two");
    expect(() => g.removeCheck("a", "script:/Game/T/Two")).toThrow(/no clip/);
  });
});

describe("a check's own record, sign-offs and the health rule", () => {
  it("keeps a check's title and links in one record, wherever it is listed, and removes an empty one", () => {
    const g = goals();
    const ref = { kind: "script" as const, target: "/Game/T/Jump" };
    expect(g.updateMeta(ref, { title: "Jump", links: [{ url: "https://t.example.com/1" }] })).toEqual({ title: "Jump", links: [{ url: "https://t.example.com/1" }] });
    expect(record("checks", "script:/Game/T/Jump")).toMatchObject({ type: "check", data: { check: ref, title: "Jump" } });
    expect(goals().meta("script:/Game/T/Jump")?.title).toBe("Jump");
    g.updateMeta(ref, { title: null, links: [] });
    expect(record("checks", "script:/Game/T/Jump").deleted).toBe(true);
    expect(() => g.updateMeta(ref, { links: [{ url: "javascript:x" }] })).toThrow();
  });

  it("keeps one sign-off per goal as a record, and clears it", () => {
    const g = goals();
    g.create({ id: "jump", title: "Jump" });
    g.signOff(signOff("jump"));
    g.signOff(signOff("jump", "2026-11-23T12:00:00.000Z"));
    expect(g.signOffs().get("jump")?.at).toBe("2026-11-23T12:00:00.000Z");
    expect(fs.readdirSync(path.join(data, "hub", "signoffs")).filter((n) => !record("signoffs", decodeURIComponent(n.slice(0, -5))).deleted)).toHaveLength(1);
    expect(goals().clearSignOff("jump")).toBe(true);
    expect(goals().signOffs().size).toBe(0);
  });

  it("deleting a goal removes its sign-off", () => {
    const g = goals();
    g.create({ id: "jump", title: "Jump" });
    g.signOff(signOff("jump"));
    g.delete("jump");
    expect(g.signOffs().size).toBe(0);
  });

  it("keeps the project's health rule, validated", () => {
    const g = goals();
    expect(g.rules()).toEqual({ staleAfterDays: null, flakyWindow: 3 });
    expect(g.setRules({ staleAfterDays: 14 })).toEqual({ staleAfterDays: 14, flakyWindow: 3 });
    expect(goals().rules().staleAfterDays).toBe(14);
    expect(() => g.setRules({ flakyWindow: 1 })).toThrow(/flakyWindow/);
  });
});

describe("the one-time import of QA/Goals", () => {
  const files = () => {
    writeLegacy("slice.json", { title: "Slice", kind: "milestone" });
    writeLegacy("jump.json", {
      title: "Jump",
      parent: "slice",
      owner: "Ama",
      tags: ["onboarding"],
      acceptance: { stableRuns: 2, staleAfterDays: 14 },
      checks: [
        { kind: "script", target: "/Game/T/Jump", title: "Jump test", links: [{ url: "https://t.example.com/1" }] },
        { kind: "manual", target: "Feels weighty", required: false },
      ],
    });
    writeLegacy("broken.json", "{ nope");
    writeLegacy("goal.schema.json", { $schema: "x" });
  };
  const snapshot = () => fs.readdirSync(path.join(project, "QA", "Goals")).sort().map((n) => [n, fs.readFileSync(path.join(project, "QA", "Goals", n), "utf-8")]);

  it("imports an empty store's goals once, and leaves the files as they were", () => {
    files();
    const before = snapshot();
    const g = goals();
    expect(snapshot()).toEqual(before);
    expect(g.get("slice")?.requires).toEqual([{ goal: "jump" }]);
    expect(g.get("jump")).toMatchObject({ owner: "Ama", acceptance: { stableRuns: 2, signOff: false }, checks: [{ kind: "script", target: "/Game/T/Jump" }], unsupported: [{ kind: "manual", target: "Feels weighty", required: false }] });
    expect(g.meta("script:/Game/T/Jump")).toEqual({ title: "Jump test", links: [{ url: "https://t.example.com/1" }] });
    const imported = g.imported()!;
    expect(imported).toMatchObject({ from: "QA/Goals", goals: ["jump", "slice"] });
    expect(imported.problems.join("\n")).toMatch(/broken\.json is not valid JSON/);
    expect(imported.problems.join("\n")).toMatch(/tags were not imported/);
  });

  it("never imports again, even once every goal is gone", () => {
    files();
    const g = goals();
    g.delete("jump");
    g.delete("slice");
    writeLegacy("later.json", { title: "Later" });
    expect(goals().list()).toEqual([]);
  });

  it("does nothing without files, so a later checkout that brings them still imports", () => {
    expect(goals().imported()).toBeNull();
    writeLegacy("jump.json", { title: "Jump" });
    expect(goals().list().map((x) => x.id)).toEqual(["jump"]);
  });

  it("leaves a store that already has goals alone", () => {
    goals().create({ id: "mine", title: "Mine" });
    writeLegacy("jump.json", { title: "Jump" });
    expect(goals().list().map((x) => x.id)).toEqual(["mine"]);
  });

  it("takes in an earlier sign-offs.json once, for goals the store has, and leaves the file as it was", () => {
    goals().create({ id: "jump", title: "Jump" });
    const file = path.join(data, "hub", "sign-offs.json");
    const text = JSON.stringify([signOff("jump"), signOff("gone"), { goalId: "bad" }]);
    fs.writeFileSync(file, text);
    const withFile = () => new Goals({ store: new GoalFiles(data, () => "2026-11-23T10:00:00.000Z"), projectDir: project, legacySignOffs: file, now: () => "2026-11-23T10:00:00.000Z" });
    const g = withFile();
    expect(g.signOffs().get("jump")).toEqual(signOff("jump"));
    expect(g.signOffs().size).toBe(1);
    expect(fs.readFileSync(file, "utf-8")).toBe(text);
    g.clearSignOff("jump");
    // Read once: a cleared sign-off does not come back from the file.
    expect(withFile().signOffs().size).toBe(0);
  });
});

describe("export and import", () => {
  const exported = (): HubExport => {
    const g = goals();
    g.create({ id: "jump", title: "Jump", requires: [] });
    g.updateMeta({ kind: "script", target: "/Game/T/Jump" }, { title: "Jump test" });
    g.signOff(signOff("jump"));
    g.setRules({ flakyWindow: 5 });
    return g.exportAll();
  };

  it("exports every live record and imports them into an empty store as they were", () => {
    const file = exported();
    expect(file).toMatchObject({ format: HUB_EXPORT_FORMAT, schema: 1 });
    expect(file.records.map((r) => r.type).sort()).toEqual(["check", "goal", "rules", "signoff"]);
    const other = goals(path.join(root, "other"));
    expect(other.importAll(JSON.parse(JSON.stringify(file)))).toEqual({ added: 4, unchanged: 0, conflicts: [] });
    expect(other.get("jump")?.title).toBe("Jump");
    expect(other.rules().flakyWindow).toBe(5);
    expect(other.signOffs().get("jump")?.by).toBe("ama");
    // The same file again changes nothing.
    expect(other.importAll(file)).toEqual({ added: 0, unchanged: 4, conflicts: [] });
  });

  it("merges by id: what is here and differs stays, and is reported", () => {
    const file = exported();
    const other = goals(path.join(root, "other"));
    other.create({ id: "jump", title: "Jump, ours" });
    const report = other.importAll(file);
    expect(report.conflicts).toEqual([{ type: "goal", id: "jump", title: "Jump, ours" }]);
    expect(report.added).toBe(3);
    expect(other.get("jump")?.title).toBe("Jump, ours");
  });

  it("never brings back a record removed here, and reports it", () => {
    const file = exported();
    const other = goals(path.join(root, "other"));
    other.importAll(file);
    other.delete("jump");
    const report = other.importAll(file);
    expect(report.conflicts).toContainEqual({ type: "goal", id: "jump", title: "Jump (removed here)" });
    expect(other.get("jump")).toBeUndefined();
  });

  it("refuses a file with any record that does not read, and changes nothing", () => {
    const file = exported();
    const broken = { ...file, records: [...file.records, { ...file.records[0], id: "two", data: { title: "" } }] };
    const other = goals(path.join(root, "other"));
    expect(() => other.importAll(broken)).toThrow(/nothing was imported: goal two/);
    expect(other.list()).toEqual([]);
    expect(() => other.importAll({ format: "x" })).toThrow(/not an exported hub file/);
    expect(() => other.importAll({ ...file, schema: 2 })).toThrow(/newer ue-mcp/);
  });
});
