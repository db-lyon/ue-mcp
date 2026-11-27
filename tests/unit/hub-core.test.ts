/**
 * The Playtest Hub's core (#2146): the contract's rules (clips without checks,
 * arrangements with a Checks track and regions, tests, nested suites, stable
 * file text, health, flakiness and pass-rate history) and the Hub service
 * over a fake runner (a test queued, played, judged by a verdict and
 * recorded on this machine; the agent actions over the same methods).
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  arrangementOf,
  clipOf,
  fileText,
  flakiness,
  parseTestId,
  passRateHistory,
  stateFromChecks,
  suiteHealth,
  suiteLoop,
  suiteOf,
  suiteTests,
  testChecks,
  testHealth,
  type Clip,
  type PlayRequest,
  type PlayStatus,
  type RunRecord,
} from "../../src/contract/hub.js";
import { spawn, type ChildProcess } from "node:child_process";
import { Hub } from "../../src/hub/hub.js";
import { HeadlessRunner, type HeadlessRunnerOptions } from "../../src/daemon/hub-headless.js";
import { runHubAction } from "../../src/hub/hub-tools.js";
import type { HubRunner } from "../../src/hub/ports.js";

const walk = { title: "Walk", performer: "player", input: [{ action: "/Game/IA_Move.IA_Move", type: "axis2d", keys: [{ id: "k2", t: 120, v: [0, 0] }, { id: "k1", t: 0, v: [0, 1] }] }] };
const arr = (over: Record<string, unknown> = {}) => ({
  title: "Door",
  tracks: [{ id: "hero", title: "Hero", performer: { kind: "player", client: 0 }, instances: [{ id: "i1", clip: "walk", at: 0 }] }],
  checks: [
    { id: "moved", title: "Moved", at: 0, length: 120, mode: "by_end", expect: { value: "pos_x", op: "increasing" } },
    { id: "open", title: "Open", at: 200, length: 0, mode: "manual", instruction: "Is it open?" },
  ],
  regions: [{ id: "walk", name: "Walk", start: 0, end: 150 }],
  ...over,
});

const run = (o: Partial<RunRecord>): RunRecord => ({ runId: "r", test: "t", title: "t", runner: "editor", source: "person", batch: "b", startedAt: "2026-01-01T00:00:00Z", endedAt: "x", state: "passed", checks: [], revision: "a", dirty: 0, ...o });

describe("the contract", () => {
  it("refuses checks in a clip, any lane but Input Action lanes, and a performer that is not a player", () => {
    expect(clipOf({ ...walk, checks: [] }, "walk").findings.map((f) => f.path)).toContain("checks");
    expect(clipOf({ ...walk, lanes: [{ id: "l1", kind: "move", points: [] }] }, "walk").findings.some((f) => f.path === "lanes" && f.blocks)).toBe(true);
    expect(clipOf({ ...walk, performer: "actor" }, "walk").findings.some((f) => f.path === "performer" && f.blocks)).toBe(true);
    expect(clipOf({ title: "No kind", input: [] }, "nokind").clip.performer).toBe("player");
    expect("lanes" in clipOf(walk, "walk").clip).toBe(false);
    const { clip, findings } = clipOf(walk, "walk");
    expect(findings).toEqual([]);
    expect(clip.length).toBe(120);
    expect(clip.input[0]!.keys.map((k) => k.id)).toEqual(["k1", "k2"]);
  });

  it("writes the same bytes for the same value, whatever the order given", () => {
    const a = clipOf(walk, "walk").clip;
    const b = clipOf({ ...walk, input: [{ ...walk.input[0], keys: [...walk.input[0]!.keys].reverse() }] }, "walk").clip;
    expect(fileText("clip", a)).toBe(fileText("clip", b));
    expect(fileText("clip", a).endsWith("}\n")).toBe(true);
    expect(fileText("clip", clipOf(JSON.parse(fileText("clip", a)), "walk").clip)).toBe(fileText("clip", a));
  });

  it("validates an arrangement's tracks, Checks track and regions", () => {
    const clips = (id: string) => (id === "walk" ? clipOf(walk, "walk").clip : undefined);
    expect(arrangementOf(arr(), "door", clips).findings).toEqual([]);
    const bad = arrangementOf(arr({
      tracks: [
        { id: "g", title: "G", performer: { kind: "actor", actor: "Guard" }, instances: [] },
        { id: "p", title: "P", performer: { kind: "player", client: 1 }, instances: [{ id: "a", clip: "walk", at: 0 }, { id: "b", clip: "walk", at: 60 }] },
      ],
      checks: [{ id: "m", at: 0, length: 0, mode: "manual" }, { id: "x", at: 0, length: 1, mode: "throughout" }],
      regions: [{ id: "r1", start: 0, end: 10 }, { id: "r2", start: 5, end: 20 }],
    }), "door", clips).findings.map((f) => f.message).join("\n");
    expect(bad).toMatch(/performer kind 'actor' is not one of player/);
    expect(bad).toMatch(/before a ends/);
    expect(bad).toMatch(/needs an instruction/);
    expect(bad).toMatch(/needs expect/);
    expect(bad).toMatch(/overlap/);
  });

  it("makes tests of an arrangement and its regions", () => {
    const a = arrangementOf(arr(), "door").arrangement;
    expect(parseTestId("door:walk")).toEqual({ arrangement: "door", region: "walk" });
    expect(parseTestId("Door")).toBeNull();
    expect(testChecks(a).map((c) => c.id)).toEqual(["moved", "open"]);
    expect(testChecks(a, "walk").map((c) => c.id)).toEqual(["moved"]);
  });

  it("nests suites, finds loops and refuses one holding itself", () => {
    const suites = new Map([
      ["a", suiteOf({ items: [{ test: "door" }, { suite: "b" }] }, "a").suite],
      ["b", suiteOf({ items: [{ test: "door:walk" }, { test: "door" }, { suite: "gone" }] }, "b").suite],
    ]);
    expect(suiteTests(suites, "a")).toEqual({ tests: ["door", "door:walk"], missingSuites: ["gone"] });
    expect(suiteLoop(suites, "a")).toBeNull();
    suites.set("b", suiteOf({ items: [{ suite: "a" }] }, "b").suite);
    expect(suiteLoop(suites, "a")).toEqual(["a", "b", "a"]);
    expect(suiteOf({ items: [{ suite: "a" }] }, "a").findings[0]!.message).toMatch(/itself/);
  });

  it("works out health, flakiness on one revision, and pass-rate history per batch", () => {
    const pass = run({ state: "passed", checks: [{ id: "c", status: "passed" }] });
    const fail = run({ state: "failed", checks: [{ id: "c", status: "failed" }] });
    expect(testHealth([])).toBe("never_run");
    expect(testHealth([fail, pass])).toBe("failing");
    expect(testHealth([pass, fail, pass])).toBe("flaky");
    expect(flakiness([pass, fail]).checks).toEqual(["c"]);
    expect(flakiness([pass, { ...fail, revision: "older" }]).flaky).toBe(false);
    expect(testHealth([run({ state: "running", endedAt: null }), pass])).toBe("running");
    expect(stateFromChecks([{ status: "passed" }, { status: "skipped" }])).toBe("passed");
    expect(stateFromChecks([])).toBe("error");
    const counted = { quarantined: false, flaky: { detected: false, marked: false, checks: [] } };
    expect(suiteHealth([{ ...counted, health: "passing" }, { ...counted, health: "failing", quarantined: true }])).toBe("passing");
    expect(suiteHealth([{ ...counted, health: "passing", flaky: { detected: false, marked: true, checks: [] } }])).toBe("flaky");
    expect(passRateHistory(new Set(["t", "u"]), [run({ batch: "b1" }), run({ batch: "b1", test: "u", state: "failed" }), run({ batch: "b2", startedAt: "2026-02-01T00:00:00Z" })]))
      .toEqual([{ batch: "b1", at: "2026-01-01T00:00:00Z", passed: 1, total: 2, rate: 0.5 }, { batch: "b2", at: "2026-02-01T00:00:00Z", passed: 1, total: 1, rate: 1 }]);
  });
});

class FakeRunner implements HubRunner {
  kind: HubRunner["kind"] = "editor";
  played: PlayRequest[] = [];
  status_: PlayStatus | null = null;
  verdicts: string[] = [];
  unavailable(): string | null {
    return null;
  }
  async play(r: PlayRequest) {
    this.played.push(r);
    return { pieStarted: true };
  }
  async status(runId: string): Promise<PlayStatus> {
    return this.status_ ?? { runId, state: "running", checks: [] };
  }
  async stop(): Promise<void> {}
  async verdict(_r: string, check: string, verdict: string): Promise<void> {
    this.verdicts.push(`${check}:${verdict}`);
  }
}

describe("the Hub service", () => {
  let dir: string;
  let runner: FakeRunner;
  let hub: Hub;
  const events: Array<{ type: string; data?: Record<string, unknown> }> = [];

  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "hub-core-"));
    runner = new FakeRunner();
    events.length = 0;
    hub = new Hub({
      host: {
        projectDir: path.join(dir, "project"),
        dataDir: path.join(dir, "data"),
        callAction: async () => ({ isError: true, content: [{ type: "text", text: "no editor" }] }),
        editors: () => [{ connected: true }],
        events: { publish: (type, data) => events.push({ type, data }), subscribe: () => () => undefined },
        log: { warn: () => undefined },
      },
      editor: { pie: () => false, dialogs: () => 0, busy: () => ({ now: 0, calls: [] }), inputAgeMs: () => null },
      editorRunner: runner,
      workspace: async () => ({ vcs: "git", revision: "abc", dirty: 0 }),
      timers: false,
    });
  });

  afterEach(() => {
    hub.dispose();
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it("plays a queued test with its audible tracks and clips, takes a verdict and records the result", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr({ tracks: [...arr().tracks, { id: "muted", title: "Muted", performer: { kind: "player", client: 1 }, mute: true, instances: [] }] }));
    const q = hub.enqueue({ test: "door" }, "person");
    expect(q.queued).toHaveLength(1);
    await hub.tick();
    expect(runner.played).toHaveLength(1);
    const req = runner.played[0]!;
    expect(req.arrangement.tracks.map((t) => t.id)).toEqual(["hero"]);
    expect(Object.keys(req.clips)).toEqual(["walk"]);
    expect(hub.leaseView().lease).toMatchObject({ pie: true, runId: req.runId });

    runner.status_ = { runId: req.runId, state: "awaiting_verdict", checks: [{ id: "moved", status: "passed", frame: 80 }, { id: "open", status: "awaiting_verdict" }] };
    await hub.tick();
    expect(hub.run(req.runId).state).toBe("awaiting_verdict");
    await hub.verdict(req.runId, { check: "open", verdict: "pass" }, "person");
    expect(runner.verdicts).toEqual(["open:pass"]);

    runner.status_ = { runId: req.runId, state: "finished", checks: [{ id: "moved", status: "passed", frame: 80 }, { id: "open", status: "passed" }] };
    await hub.tick();
    const r = hub.run(req.runId);
    expect(r).toMatchObject({ state: "passed", revision: "abc", source: "person" });
    expect(r.checks.find((c) => c.id === "open")!.verdict).toMatchObject({ by: "person" });
    expect(hub.leaseView().lease).toBeNull();
    expect(hub.queueView().queued).toEqual([]);
    expect(hub.test("door").test).toMatchObject({ health: "passing", passRate: 1 });
    expect(fs.existsSync(path.join(dir, "data", "hub", "runs.json"))).toBe(true);
  });

  it("plays a region with only its checks, and refuses an agent's verdict on a person-only check", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr({ checks: [...arr().checks.slice(0, 1), { id: "open", at: 100, length: 0, mode: "manual", instruction: "Look", personOnly: true }] }));
    hub.enqueue({ test: "door:walk" }, "agent");
    await hub.tick();
    const req = runner.played[0]!;
    expect(req.region).toBe("walk");
    expect(hub.run(req.runId).checks.map((c) => c.id)).toEqual(["moved", "open"]);
    await expect(hub.verdict(req.runId, { check: "open", verdict: "pass" }, "agent")).rejects.toThrow(/person's verdict only/);
    runner.status_ = { runId: req.runId, state: "finished", checks: [{ id: "moved", status: "failed", message: "pos_x did not increase" }] };
    await hub.tick();
    expect(hub.run(req.runId)).toMatchObject({ state: "failed" });
    expect(hub.run(req.runId).checks.find((c) => c.id === "open")!.status).toBe("skipped");
  });

  it("waits while the editor is busy and records a failed start as an error", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr());
    runner.play = async () => {
      throw new Error("PIE could not start");
    };
    hub.enqueue({ test: "door" }, "person");
    await hub.tick();
    expect(hub.runs().runs[0]).toMatchObject({ state: "error", error: "PIE could not start" });
    expect(hub.leaseView().lease).toBeNull();
  });

  it("serves the agent actions over the same files", async () => {
    await runHubAction(hub, "write_clip", { body: walk });
    await expect(runHubAction(hub, "write_arrangement", { body: arr({ tracks: [{ id: "h", title: "H", performer: { kind: "player", client: 0 }, instances: [{ id: "i", clip: "wlak", at: 0 }] }] }) })).rejects.toThrow(/did you mean walk/);
    await runHubAction(hub, "write_arrangement", { body: JSON.stringify(arr()) });
    const suite = (await runHubAction(hub, "write_suite", { body: { title: "Smoke", items: [{ test: "door" }] } })) as { id: string };
    expect(suite.id).toBe("smoke");
    const quarantined = (await runHubAction(hub, "quarantine", { suite: "smoke", test: "door" })) as { health: string };
    expect(quarantined.health).toBe("empty");
    const queued = (await runHubAction(hub, "run", { suite: "smoke" })) as { queued: Array<{ source: string }> };
    expect(queued.queued[0]!.source).toBe("agent");
    const file = fs.readFileSync(path.join(dir, "project", "PlaytestHub", "arrangements", "door.json"), "utf8");
    expect(JSON.parse(file)).toMatchObject({ schema: 2, title: "Door", checks: [{ id: "moved" }, { id: "open" }] });
    expect(events.some((e) => e.type === "hub.changed")).toBe(true);
  });

  it("refuses a test with no checks with the way to add one", () => {
    hub.createClip(walk);
    hub.createArrangement(arr({ checks: [] }));
    expect(() => hub.enqueue({ test: "door" }, "person")).toThrow(/add one to the arrangement's Checks track/);
    expect((hub.clip("walk") as Clip).performer).toBe("player");
  });

  it("runs headless by default when a hidden editor fits, and on the open editor with the reason when not", async () => {
    const headless = Object.assign(new FakeRunner(), { kind: "headless" as const, why: null as string | null });
    headless.unavailable = () => headless.why;
    const h = new Hub({
      host: { projectDir: path.join(dir, "project"), dataDir: path.join(dir, "data2"), callAction: async () => ({ isError: true, content: [] }), editors: () => [{ connected: true }], events: { publish: () => undefined, subscribe: () => () => undefined }, log: { warn: () => undefined } },
      editor: { pie: () => false, dialogs: () => 0, busy: () => ({ now: 0, calls: [] }), inputAgeMs: () => null },
      editorRunner: runner,
      headless: () => headless as unknown as HubRunner,
      workspace: async () => ({ vcs: null, revision: null, dirty: null }),
      timers: false,
    });
    try {
      h.createClip(walk);
      h.createArrangement(arr());
      // The region has no manual check, so it goes headless; the whole arrangement has one, so a person judges it in the editor.
      const region = h.enqueue({ test: "door:walk" }, "person").queued[0]!;
      expect(region).toMatchObject({ runner: "headless", runnerReason: "a hidden editor fits in memory" });
      const whole = h.enqueue({ test: "door" }, "person").queued[0]!;
      expect(whole).toMatchObject({ runner: "editor", runnerReason: expect.stringMatching(/manual checks/) });
      headless.why = "a hidden editor needs about 3.5 GB and 1 GB is free";
      expect(h.enqueue({ test: "door:walk" }, "person").queued[0]).toMatchObject({ runner: "editor", runnerReason: expect.stringMatching(/3.5 GB/) });
      expect(() => h.enqueue({ test: "door", runner: "headless" }, "person")).toThrow(/manual checks/);
      await h.tick();
      expect(headless.played.map((p) => p.region)).toEqual(["walk"]);
      headless.status_ = { runId: headless.played[0]!.runId, state: "finished", checks: [{ id: "moved", status: "passed" }] };
      await h.tick();
      expect(h.runs().runs.find((r) => r.runner === "headless")).toMatchObject({ state: "passed", test: "door:walk" });
    } finally {
      h.dispose();
    }
  });

  it("auditions from the playhead and in a loop, leaving manual checks out and the run history alone", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr());
    const { play } = await hub.play({ test: "door", from: 30, loop: true });
    expect(play).toMatchObject({ test: "door", from: 30, loop: true, state: "starting" });
    const req = runner.played[0]!;
    expect(req).toMatchObject({ from: 30, loop: true });
    expect(req.arrangement.checks.map((c) => c.id)).toEqual(["moved"]);
    expect(hub.leaseView().lease).toMatchObject({ purpose: "Audition Door", pie: true });
    await expect(hub.play({ test: "door" })).rejects.toThrow(/an audition of door is playing/);
    expect(() => hub.enqueue({ test: "door:walk", runner: "editor" }, "person")).not.toThrow();
    await hub.tick();
    expect(runner.played).toHaveLength(1);
    runner.status_ = { runId: req.runId, state: "running", frame: 44, pass: 3, checks: [{ id: "moved", status: "running" }] };
    expect((await hub.readPlay()).play).toMatchObject({ state: "running", frame: 44, pass: 3 });
    runner.status_ = { runId: req.runId, state: "stopped", checks: [] };
    expect((await hub.stopPlay()).play.state).toBe("stopped");
    expect(hub.leaseView().lease).toBeNull();
    await expect(hub.play({ test: "door", from: -1 })).rejects.toThrow(/from is the playhead/);
    await expect(hub.play({ test: "nope" })).rejects.toThrow(/no arrangement nope/);
    expect(hub.runs().runs.filter((r) => r.runId === req.runId)).toEqual([]);
    await hub.tick();
    expect(runner.played.map((p) => p.region)).toEqual([undefined, "walk"]);
  });

  it("keeps the recording under way readable until it stops, so a reloaded client can show it", async () => {
    const calls: string[] = [];
    const recEvents: Array<{ type: string; data?: Record<string, unknown> }> = [];
    const answer = (v: unknown) => ({ isError: false, content: [{ type: "text", text: JSON.stringify(v) }] });
    const h = new Hub({
      host: {
        projectDir: path.join(dir, "project"),
        dataDir: path.join(dir, "data3"),
        callAction: async (_tool: string, action: string) => (calls.push(action), action === "clip_record_stop" ? answer({ clip: { ...walk, rate: 60, length: 120 } }) : answer({ success: true })),
        editors: () => [{ connected: true }],
        events: { publish: (type, data) => recEvents.push({ type, data }), subscribe: () => () => undefined },
        log: { warn: () => undefined },
      },
      editor: { pie: () => false, dialogs: () => 0, busy: () => ({ now: 0, calls: [] }), inputAgeMs: () => null },
      editorRunner: runner,
      workspace: async () => ({ vcs: null, revision: null, dirty: null }),
      timers: false,
    });
    try {
      expect(h.readRecording()).toEqual({ recording: null });
      h.createClip(walk);
      await h.recordStart({ clip: "walk", from: 30, to: 90, arrangement: "door", track: "hero", at: 30, seconds: 1, junk: 1 });
      expect(h.readRecording().recording).toEqual({ performer: { kind: "player", client: 0 }, clip: "walk", from: 30, to: 90, rate: 60, arrangement: "door", track: "hero", at: 30, seconds: 1, startedAt: expect.any(String) });
      expect(await runHubAction(h, "read_recording", {})).toMatchObject({ recording: { clip: "walk" } });
      expect(recEvents.some((e) => e.type === "hub.changed" && (e.data?.what as string[]).includes("recording"))).toBe(true);
      await h.recordStop({});
      expect(calls).toEqual(["clip_record_start", "clip_record_stop"]);
      expect(h.readRecording()).toEqual({ recording: null });
    } finally {
      h.dispose();
    }
  });
});

describe("the headless runner", () => {
  let dir: string;
  let project: string;
  const children: ChildProcess[] = [];
  const sleeper = (): ChildProcess => {
    const c = spawn(process.execPath, ["-e", "setTimeout(() => {}, 60000)"], { stdio: "ignore" });
    children.push(c);
    return c;
  };

  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "hub-headless-"));
    project = path.join(dir, "Game");
    fs.mkdirSync(path.join(project, "Content"), { recursive: true });
    fs.mkdirSync(path.join(project, "Saved", "Logs"), { recursive: true });
    fs.writeFileSync(path.join(project, "Game.uproject"), "{}");
    fs.writeFileSync(path.join(project, "Content", "Map.umap"), "map");
    fs.writeFileSync(path.join(project, "Saved", "Logs", "Game.log"), "old");
  });

  afterEach(() => {
    for (const c of children) c.kill();
    children.length = 0;
    fs.rmSync(dir, { recursive: true, force: true });
  });

  const runnerFor = (reported: () => string, calls: string[]) => new HeadlessRunner({
    projectDir: project,
    projectPath: path.join(project, "Game.uproject"),
    workDir: path.join(dir, "work"),
    editorExe: () => "UnrealEditor.exe",
    memory: () => ({ freeMB: 16000, totalMB: 32000 }),
    log: { warn: () => undefined },
    spawn: () => sleeper(),
    stop: async (_dir, pid) => {
      children.find((c) => c.pid === pid)?.kill();
    },
    waitReady: async (_u, copy) => {
      fs.mkdirSync(path.join(copy, "Saved", "UE_MCP_Bridge"), { recursive: true });
      fs.writeFileSync(path.join(copy, "Saved", "UE_MCP_Bridge", "port.json"), JSON.stringify({ port: 4321 }));
      return { ready: true };
    },
    bridge: () => ({
      connect: async () => undefined,
      disconnect: () => undefined,
      call: async (method) => {
        calls.push(method);
        return method === "execute_python" ? { output: `MCP_PROJECT_DIR:${reported()}/\n` } : { success: true, runId: "r1", state: "running", checks: [] };
      },
    }),
  });

  it("holds back when no editor fits in memory, and copies the project without its saved state", async () => {
    const calls: string[] = [];
    const r = runnerFor(() => "", calls);
    expect(new HeadlessRunner({ ...(r as unknown as { o: HeadlessRunnerOptions }).o, memory: () => ({ freeMB: 6000, totalMB: 32000 }) }).unavailable()).toMatch(/GB is free/);
    expect(new HeadlessRunner({ ...(r as unknown as { o: HeadlessRunnerOptions }).o, editorExe: () => null }).unavailable()).toMatch(/no Unreal Editor/);
    const copy = r.copyDir.replace(/\\/g, "/");
    const ok = runnerFor(() => copy, calls);
    await ok.play({ runId: "r1", arrangement: arrangementOf(arr(), "door").arrangement, clips: {} });
    expect(calls).toEqual(["execute_python", "pie_arrangement_play"]);
    expect(fs.existsSync(path.join(ok.copyDir, "Content", "Map.umap"))).toBe(true);
    expect(fs.existsSync(path.join(ok.copyDir, "Saved", "Logs", "Game.log"))).toBe(false);
    expect((await ok.status("r1")).state).toBe("running");
    await expect(ok.verdict()).rejects.toThrow(/open editor/);
    ok.close("the test ended");
  });

  it("sends nothing to an editor that has another project open, and closes the one it started", async () => {
    const calls: string[] = [];
    const r = runnerFor(() => "C:/Somewhere/Else", calls);
    await expect(r.play({ runId: "r1", arrangement: arrangementOf(arr(), "door").arrangement, clips: {} })).rejects.toThrow(/nothing was sent/);
    expect(calls).toEqual(["execute_python"]);
    await new Promise((res) => setTimeout(res, 500));
    expect(children[0]!.exitCode !== null || children[0]!.signalCode !== null || children[0]!.killed).toBe(true);
  });
});
