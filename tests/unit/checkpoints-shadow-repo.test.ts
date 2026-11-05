/**
 * The shadow history behind checkpoints (#1482), engine-free: lazy recording,
 * baselines, changes with text diffs, subset restore, the plain-copy path for
 * large files, outside edits, and a project .git that is never touched.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ShadowRepo, appendPendingRecord, isExcludedPath, relativeKey } from "../../src/checkpoints/shadow-repo.js";
import { unifiedDiff } from "../../src/checkpoints/text-diff.js";

let dir: string;
let repo: ShadowRepo;

const write = (rel: string, content: string | Buffer) => {
  const file = path.join(dir, rel);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, content);
};
const read = (rel: string) => fs.readFileSync(path.join(dir, rel), "utf-8");
const exists = (rel: string) => fs.existsSync(path.join(dir, rel));
/** A write through the server: record, then write. */
const agentWrite = (rel: string, content: string | Buffer) => {
  repo.recordBeforeWrite(path.join(dir, rel));
  write(rel, content);
};

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-shadow-"));
  repo = new ShadowRepo(dir, { largeFileBytes: 64 });
});

afterEach(() => {
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("paths", () => {
  it("keys paths relative to the project and excludes build output and Saved", () => {
    expect(relativeKey(dir, path.join(dir, "Config", "DefaultGame.ini"))).toBe("Config/DefaultGame.ini");
    expect(relativeKey(dir, path.join(dir, "..", "elsewhere.txt"))).toBeNull();
    expect(isExcludedPath("Saved/Logs/x.log")).toBe(true);
    expect(isExcludedPath("Intermediate/Build/x")).toBe(true);
    expect(isExcludedPath("Plugins/Foo/Binaries/Win64/x.dll")).toBe(true);
    expect(isExcludedPath("Content/Maps/Main.umap")).toBe(false);
  });
});

describe("checkpoints", () => {
  it("creates nothing until the first checkpoint", () => {
    expect(repo.exists()).toBe(false);
  });

  it("records only written paths and lists what changed between two checkpoints", async () => {
    write("Config/DefaultGame.ini", "[A]\nx=1\n");
    write("Content/Untouched.uasset", "untouched");
    const a = await repo.checkpoint({ label: "turn 1 start", trigger: "client" });
    expect(a.changes).toEqual([]);

    agentWrite("Config/DefaultGame.ini", "[A]\nx=2\n");
    agentWrite("Source/New.h", "#pragma once\n");
    write("Content/Untouched.uasset", "edited by hand");
    const b = await repo.checkpoint({ label: "turn 1 end", trigger: "client" });
    expect(b.changes.map((c) => [c.path, c.status])).toEqual([
      ["Config/DefaultGame.ini", "modified"],
      ["Source/New.h", "added"],
    ]);

    const diff = await repo.changes(a.info.id, b.info.id);
    const ini = diff.changes.find((c) => c.path === "Config/DefaultGame.ini")!;
    expect(ini.kind).toBe("config");
    expect(ini.diff).toContain("-x=1");
    expect(ini.diff).toContain("+x=2");
    expect(diff.changes.some((c) => c.path === "Content/Untouched.uasset")).toBe(false);

    const list = await repo.list();
    expect(list.map((c) => c.label)).toEqual(["turn 1 end", "turn 1 start"]);
  });

  it("restores a subset and leaves the rest of the turn, and the restore can be undone", async () => {
    write("Config/DefaultGame.ini", "[A]\nx=1\n");
    write("Source/Keep.cpp", "v1\n");
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    agentWrite("Config/DefaultGame.ini", "[A]\nx=2\n");
    agentWrite("Source/Keep.cpp", "v2\n");
    agentWrite("Source/Added.h", "new\n");
    await repo.checkpoint({ label: "end", trigger: "client" });

    const before = await repo.checkpoint({ label: "before", trigger: "restore-before" });
    const { plan, refused } = await repo.planRestore(a.info.id, before.info.id, ["Config/DefaultGame.ini", "Source/Added.h"], false);
    expect(refused).toEqual([]);
    for (const e of plan) await repo.apply(e);
    expect(read("Config/DefaultGame.ini")).toBe("[A]\nx=1\n");
    expect(exists("Source/Added.h")).toBe(false);
    expect(read("Source/Keep.cpp")).toBe("v2\n");
    const after = await repo.checkpoint({ label: "after", trigger: "restore-after", expected: plan.map((p) => p.path) });
    expect(after.changes.every((c) => !c.untracked)).toBe(true);

    // Undo the restore by restoring to the checkpoint taken before it.
    const undo = await repo.planRestore(before.info.id, after.info.id, undefined, false);
    for (const e of undo.plan) await repo.apply(e);
    expect(read("Config/DefaultGame.ini")).toBe("[A]\nx=2\n");
    expect(read("Source/Added.h")).toBe("new\n");
  });

  it("refuses a path edited outside the agent after the checkpoint unless forced", async () => {
    write("Config/DefaultEngine.ini", "one\n");
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    agentWrite("Config/DefaultEngine.ini", "agent\n");
    await repo.checkpoint({ label: "end", trigger: "client" });
    write("Config/DefaultEngine.ini", "user\n");
    const now = await repo.changes(a.info.id);
    expect(now.changes[0].untracked).toBe(true);

    const before = await repo.checkpoint({ label: "before", trigger: "restore-before" });
    const refusedPlan = await repo.planRestore(a.info.id, before.info.id, undefined, false);
    expect(refusedPlan.plan).toEqual([]);
    expect(refusedPlan.refused).toEqual([expect.objectContaining({ path: "Config/DefaultEngine.ini", reason: "edited-outside" })]);

    const forced = await repo.planRestore(a.info.id, before.info.id, undefined, true);
    for (const e of forced.plan) await repo.apply(e);
    expect(read("Config/DefaultEngine.ini")).toBe("one\n");
  });

  it("refuses a path nothing recorded", async () => {
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    const before = await repo.checkpoint({ label: "before", trigger: "restore-before" });
    const plan = await repo.planRestore(a.info.id, before.info.id, ["Content/Never.uasset"], false);
    expect(plan.refused).toEqual([expect.objectContaining({ reason: "not-recorded" })]);
  });

  it("folds in pre-save copies from a bridge generation and restores deletes", async () => {
    write("Content/BP_Door.uasset", Buffer.from([1, 2, 3]));
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    // What the bridge does from PreSavePackageWithContextEvent: copy, then the save lands.
    const gen = repo.newGeneration("bridge");
    appendPendingRecord(gen, "Content/BP_Door.uasset", path.join(dir, "Content/BP_Door.uasset"));
    write("Content/BP_Door.uasset", Buffer.from([9, 9]));
    appendPendingRecord(gen, "Content/BP_New.uasset", path.join(dir, "Content/BP_New.uasset"));
    write("Content/BP_New.uasset", Buffer.from([7]));
    const b = await repo.checkpoint({ label: "end", trigger: "client" });
    expect(b.changes.map((c) => [c.path, c.status, c.kind])).toEqual([
      ["Content/BP_Door.uasset", "modified", "package"],
      ["Content/BP_New.uasset", "added", "package"],
    ]);
    expect(fs.existsSync(gen)).toBe(false);

    const before = await repo.checkpoint({ label: "before", trigger: "restore-before" });
    const { plan } = await repo.planRestore(a.info.id, before.info.id, undefined, false);
    for (const e of plan) await repo.apply(e);
    expect([...fs.readFileSync(path.join(dir, "Content/BP_Door.uasset"))]).toEqual([1, 2, 3]);
    expect(exists("Content/BP_New.uasset")).toBe(false);
  });

  it("stores files over the cap as plain copies and restores them byte for byte", async () => {
    const big = Buffer.alloc(200, 5);
    write("Content/Big.umap", big);
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    agentWrite("Content/Big.umap", Buffer.alloc(300, 6));
    const b = await repo.checkpoint({ label: "end", trigger: "client" });
    expect(b.changes[0].large).toBe(true);
    expect(fs.readdirSync(repo.largeDir).length).toBe(2);

    const before = await repo.checkpoint({ label: "before", trigger: "restore-before" });
    const { plan } = await repo.planRestore(a.info.id, before.info.id, undefined, false);
    for (const e of plan) await repo.apply(e);
    expect(fs.readFileSync(path.join(dir, "Content/Big.umap")).equals(big)).toBe(true);
  });

  it("survives a fresh instance, as after a restart", async () => {
    write("Config/A.ini", "1\n");
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    agentWrite("Config/A.ini", "2\n");
    await repo.checkpoint({ label: "end", trigger: "client" });

    const again = new ShadowRepo(dir, { largeFileBytes: 64 });
    const before = await again.checkpoint({ label: "before", trigger: "restore-before" });
    const { plan } = await again.planRestore(a.info.id.slice(0, 8), before.info.id, undefined, false);
    for (const e of plan) await again.apply(e);
    expect(read("Config/A.ini")).toBe("1\n");
  });

  it("never touches the project's own .git", async () => {
    write(".git/HEAD", "ref: refs/heads/main\n");
    write(".git/config", "[core]\n");
    const snapshot = () => fs.readdirSync(path.join(dir, ".git"), { recursive: true }).map(String).sort();
    const files = snapshot();
    write("Config/A.ini", "1\n");
    const a = await repo.checkpoint({ label: "start", trigger: "client" });
    agentWrite("Config/A.ini", "2\n");
    repo.recordBeforeWrite(path.join(dir, ".git", "HEAD"));
    await repo.checkpoint({ label: "end", trigger: "client" });
    await repo.changes(a.info.id);
    expect(snapshot()).toEqual(files);
    expect(read(".git/HEAD")).toBe("ref: refs/heads/main\n");
    expect(fs.existsSync(path.join(dir, "Saved", "UE_MCP_Bridge", "shadow.git", "HEAD"))).toBe(true);
  });

  it("clear deletes the history", async () => {
    await repo.checkpoint({ label: "start", trigger: "client" });
    repo.clear();
    expect(repo.exists()).toBe(false);
    expect(await repo.list()).toEqual([]);
  });
});

describe("unifiedDiff", () => {
  it("returns null for equal text and hunks for a change", () => {
    expect(unifiedDiff("a\nb\n", "a\nb\n", "f")).toBeNull();
    const d = unifiedDiff("a\nb\nc\n", "a\nB\nc\nd\n", "f.ini")!;
    expect(d).toContain("--- a/f.ini");
    expect(d).toContain("@@ -1,3 +1,4 @@");
    expect(d).toContain("-b\n+B\n");
    expect(d).toContain("+d\n");
  });
});
