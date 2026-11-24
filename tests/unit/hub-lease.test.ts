/**
 * Who holds the editor: the occupancy answer from each signal Studio has,
 * what the hub's own lease holds never counting against it, the lease
 * itself, the fix loop's per-package write lease, and the project watch that
 * marks a run tainted.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ActionResult } from "../../src/contract/daemon.js";
import { qaRunFromAnswer, type BusyState, type QaRun } from "../../src/hub/contract.js";
import { EditorLease, INPUT_QUIET_MS, occupancyOf, type OccupancySignals } from "../../src/hub/lease.js";
import { WriteLease, packageOf, type CallingHost } from "../../src/hub/write-lease.js";
import { ProjectWatch } from "../../src/hub/project-watch.js";

const run = (runId: string, state: string, extra: Partial<QaRun> = {}): QaRun => ({
  ...qaRunFromAnswer({ run_id: runId, script: "/Game/T/Door", script_name: "Door", state, steps: [] }, { sessionId: "s", seenAt: "2026-11-23T00:00:00.000Z" })!,
  ...extra,
});

let s: { connected: boolean; pie: boolean; dialogs: number; busy: BusyState; runs: QaRun[]; inputAgeMs: number | null };
const signals = (): OccupancySignals => ({
  connected: () => s.connected,
  pie: () => s.pie,
  dialogs: () => s.dialogs,
  busy: () => s.busy,
  runs: () => s.runs,
  inputAgeMs: () => s.inputAgeMs,
});
const kinds = (held = {}, o = {}) => occupancyOf(signals(), held, o).map((r) => r.kind);

beforeEach(() => {
  s = { connected: true, pie: false, dialogs: 0, busy: { now: 0, calls: [] }, runs: [], inputAgeMs: null };
});

describe("occupancyOf", () => {
  it("is free when nothing holds the editor", () => {
    expect(occupancyOf(signals())).toEqual([]);
  });

  it("names every signal that holds it, most decisive first", () => {
    s.busy = { now: 0, calls: [{ id: 1, category: "asset", action: "save", startedAt: 0, source: "client", client: "cli", stopped: false }, { id: 2, category: "asset", action: "list", startedAt: 0, source: "agent", sessionId: "chat", stopped: false }], operation: { op: "rebuild", step: "build", startedAt: 0, stepStartedAt: 0 } };
    s.dialogs = 1;
    s.pie = true;
    s.runs = [run("other", "running", { sessionId: null })];
    expect(occupancyOf(signals()).map((r) => r.text)).toEqual([
      "The editor is rebuilding",
      "A dialog is open in the editor",
      "Play In Editor is in use",
      "Another client is running a playtest",
      "A chat is using the editor",
      "Another MCP client is using the editor",
    ]);
  });

  it("the editor not connected says only that", () => {
    s.connected = false;
    s.pie = true;
    expect(kinds()).toEqual(["disconnected"]);
  });

  it("what the hub holds never counts: its run, the PIE its run started, its fix loop's session", () => {
    s.pie = true;
    s.runs = [run("mine", "running"), run("loop", "finished", { assignment: { callId: "c", sessionId: "fix", state: "running", at: "" } })];
    s.busy = { now: 0, calls: [{ id: 1, category: "asset", action: "save", startedAt: 0, source: "agent", sessionId: "fix", stopped: false }] };
    expect(kinds()).toEqual(["pie", "run", "agent"]);
    expect(kinds({ runId: "mine", pie: true, sessionId: "fix" })).toEqual([]);
  });

  it("drafts, manual runs and ended runs hold nothing; a fix loop elsewhere does", () => {
    s.runs = [run("d", "draft"), run("m", "awaiting_verdict", { manual: true }), run("f", "finished")];
    expect(kinds()).toEqual([]);
    s.runs.push(run("l", "finished", { assignment: { callId: "c", sessionId: "card", state: "running", at: "" } }));
    expect(occupancyOf(signals()).map((r) => r.text)).toEqual(["A fix loop is running"]);
  });

  it("someone using this computer holds it for unattended work only", () => {
    s.inputAgeMs = 30_000;
    expect(kinds()).toEqual([]);
    expect(kinds({}, { unattended: true })).toEqual(["input"]);
    s.inputAgeMs = INPUT_QUIET_MS;
    expect(kinds({}, { unattended: true })).toEqual([]);
  });
});

describe("EditorLease", () => {
  it("is taken for one item at a time, records what it started, and announces each change", () => {
    const seen: unknown[] = [];
    const lease = new EditorLease("workspace", (l) => seen.push(l));
    lease.take({ purpose: "Run Doors: Door", planRunId: "p1", item: 0, work: "run", since: "t" });
    expect(() => lease.take({ purpose: "Other", planRunId: "p2", item: 0, work: "run" })).toThrow(/already leased/);
    lease.update({ runId: "r1", pie: true });
    expect(lease.held()).toEqual({ runId: "r1", pie: true, sessionId: null });
    expect(lease.release()).toMatchObject({ editor: "workspace", runId: "r1" });
    expect(lease.held()).toEqual({});
    expect(seen.at(-1)).toBeNull();
    expect(seen).toHaveLength(3);
  });
});

describe("WriteLease", () => {
  const answer = (body: unknown): ActionResult => ({ isError: false, content: [{ type: "text", text: JSON.stringify(body) }] });
  let dirty: string[];
  let level: string;
  let reached: string[];
  const writes = () => new WriteLease(async (_c, action) => {
    if (action === "list_dirty_packages") return answer({ success: true, content: dirty.filter((p) => !p.includes("/Maps/")).map((p) => ({ package: p })), maps: dirty.filter((p) => p.includes("/Maps/")).map((p) => ({ package: p })) });
    if (action === "get_world_state") return answer({ success: true, persistentLevelPackage: level });
    return answer({ success: true });
  });
  const host = (): CallingHost => ({ callAction: async (c: string, a: string) => (reached.push(`${c}.${a}`), answer({ success: true })) });
  const refusedText = (r: ActionResult) => JSON.parse(r.content[0].text!) as { blocked?: boolean; reason?: string };

  beforeEach(() => {
    dirty = ["/Game/BP/BP_Door", "/Game/Maps/L_Town"];
    level = "/Game/Maps/L_Arena";
    reached = [];
  });

  it("refuses an attempt's save of a package the person has unsaved changes in, and lets its own through", async () => {
    const w = writes();
    await w.begin("fix");
    const h = w.wrap(host(), "fix");
    const r = await h.callAction("asset", "save", { assetPath: "/Game/BP/BP_Door.BP_Door" });
    expect(r.isError).toBe(true);
    expect(refusedText(r)).toMatchObject({ blocked: true, reason: expect.stringContaining("/Game/BP/BP_Door") });
    await h.callAction("asset", "save", { assetPath: "/Game/BP/BP_Lamp" });
    await h.callAction("blueprint", "add_variable", { blueprintPath: "/Game/BP/BP_Door", save: true }).then((x) => expect(x.isError).toBe(true));
    await h.callAction("blueprint", "add_variable", { blueprintPath: "/Game/BP/BP_Door" });
    expect(reached).toEqual(["asset.save", "blueprint.add_variable"]);
  });

  it("refuses a save of everything while the person's packages are still dirty, and a level save of their level", async () => {
    const w = writes();
    await w.begin("fix");
    const h = w.wrap(host(), "fix");
    expect((await h.callAction("editor", "save_dirty", {})).isError).toBe(true);
    expect((await h.callAction("level", "save", {})).isError).toBeFalsy();
    level = "/Game/Maps/L_Town";
    expect((await h.callAction("level", "save", {})).isError).toBe(true);
    // The person saved their work: saving everything is the attempt's own again.
    dirty = ["/Game/BP/BP_Lamp"];
    expect((await h.callAction("asset", "save_all_dirty", {})).isError).toBeFalsy();
  });

  it("holds nothing outside an attempt, or for another session", async () => {
    const w = writes();
    expect((await w.wrap(host(), "fix").callAction("asset", "save", { assetPath: "/Game/BP/BP_Door" })).isError).toBeFalsy();
    await w.begin("fix");
    expect((await w.wrap(host(), "chat").callAction("asset", "save", { assetPath: "/Game/BP/BP_Door" })).isError).toBeFalsy();
    w.end("fix");
    expect(w.held("fix")).toBeUndefined();
  });

  it("names packages without their object", () => {
    expect(packageOf("/Game/A/B.B")).toBe("/Game/A/B");
    expect(packageOf("/Game/A/B")).toBe("/Game/A/B");
  });
});

describe("ProjectWatch", () => {
  let dir: string;
  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "hub-watch-"));
  });
  afterEach(() => fs.rmSync(dir, { recursive: true, force: true }));

  it("a run that saw a change is tainted; one that did not, or was never seen starting, is not", () => {
    const w = new ProjectWatch(dir);
    w.runStarted("a");
    w.runStarted("b");
    expect(w.runEnded("a")).toBe(false);
    w.touched();
    expect(w.runEnded("b")).toBe(true);
    expect(w.runEnded("never")).toBeNull();
  });

  it("sees a save under Content, and not under Saved", async () => {
    fs.mkdirSync(path.join(dir, "Content", "Saved"), { recursive: true });
    const w = new ProjectWatch(dir);
    w.start();
    w.runStarted("r");
    fs.writeFileSync(path.join(dir, "Content", "Saved", "x.txt"), "1");
    await new Promise((r) => setTimeout(r, 300));
    expect(w.runEnded("r")).toBe(false);
    w.runStarted("q");
    fs.writeFileSync(path.join(dir, "Content", "Door.uasset"), "1");
    await new Promise((r) => setTimeout(r, 300));
    expect(w.runEnded("q")).toBe(true);
    w.stop();
  });
});
