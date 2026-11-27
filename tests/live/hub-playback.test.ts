// @live-editor headless
/**
 * Playtest Hub playback end to end through the shipped server (#2146): the hub category over a daemon of the
 * sandbox's own, against a real editor. A clip alone, a region, a whole arrangement waiting on a manual verdict, an
 * audition from the playhead, a looping audition, a suite, a punch-in recording and a run on a hidden editor ue-mcp
 * launches on its own copy of the project. Every result is read back from the hub, the way Studio and agents read
 * it.
 * Refs #2146
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { editorFits } from "../../src/editor/editor-copy.js";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/HubPlayback";
let LEVEL = "";

type Json = Record<string, any>;
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let ia: ProbeActions;
let projectDir: string;
let server: LiveServer | null = null;

async function hub(action: string, args: Json = {}): Promise<Json> {
  const r = await server!.call("hub", { action, args });
  expect(r.isError, `hub.${action}: ${r.text}`).toBe(false);
  return resultJson<Json>(r);
}

async function pieRunning(): Promise<boolean> {
  const r = await callBridge(bridge, "pie_control", { pieAction: "status" });
  return (r.result as Json | undefined)?.isPlaying === true;
}

async function waitPieStopped(): Promise<boolean> {
  for (let i = 0; i < 60 && (await pieRunning()); i++) await sleep(500);
  return !(await pieRunning());
}

/** Queues one test or a suite and waits for every run of the batch to end. */
async function runToEnd(target: Json, want = 1, timeoutMs = 180_000, onRun?: (r: Json) => Promise<void>): Promise<Json[]> {
  const queued = await hub("run", target);
  expect(queued.queued, JSON.stringify(queued)).toHaveLength(want);
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const runs = ((await hub("list_runs", { limit: 50 })).runs as Json[]).filter((r) => r.batch === queued.batch);
    for (const r of runs) if (onRun) await onRun(r);
    if (runs.length === want && runs.every((r) => ["passed", "failed", "error", "stopped"].includes(r.state))) return runs;
    if (Date.now() > deadline) throw new Error(`the runs did not end: ${JSON.stringify({ runs, queue: await hub("read_queue") })}`);
    await sleep(500);
  }
}

async function playUntil(done: (p: Json) => boolean, timeoutMs = 120_000): Promise<Json> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const { play } = await hub("read_play");
    if (done(play) || ["finished", "stopped", "error"].includes(play.state)) return play;
    if (Date.now() > deadline) throw new Error(`the audition did not get there: ${JSON.stringify(play)}`);
    await sleep(200);
  }
}

const status = (run: Json, id: string): string | undefined => (run.checks as Json[]).find((c) => c.id === id)?.status;

beforeAll(async () => {
  bridge = await getBridge();
  const target = await liveTarget();
  projectDir = target.projectDir;
  fs.rmSync(path.join(projectDir, "PlaytestHub"), { recursive: true, force: true });
  ia = await setupProbeArena(bridge, DIR, "HubPlayback", { chaser: false });
  // The arena's level is saved under a name of its own; the arrangements name it, so a hidden editor loads it too.
  const world = await callBridge(bridge, "execute_python", { code: "import unreal\nprint('LEVEL:' + unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_outermost().get_name())" });
  LEVEL = /LEVEL:(\/Game\/[^\s"\\]+)/.exec(JSON.stringify(world.result))?.[1] ?? "";
  expect(LEVEL, JSON.stringify(world.result)).toMatch(/^\/Game\/MCPTest\/HubPlayback\/L_HubPlayback/);
  server = await LiveServer.start({ projects: [target.uproject], daemon: true });
}, 300_000);

afterAll(async () => {
  await server?.close();
  if (projectDir) fs.rmSync(path.join(projectDir, "PlaytestHub"), { recursive: true, force: true });
  if (bridge) {
    if (await pieRunning()) await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await teardownProbeArena(bridge, DIR);
    disconnectBridge();
  }
  closeLiveBridges();
}, 180_000);

describe("hub playback through the shipped server", () => {
  it("writes the clips and arrangements the runs play", async () => {
    await hub("write_clip", { id: "walk", body: { title: "Walk", rate: 60, length: 120, input: [{ action: ia.move, type: "axis2d", keys: [{ id: "k1", t: 0, v: [0, 1], interp: "constant" }, { id: "k2", t: 90, v: [0, 0], interp: "constant" }] }] } });
    await hub("write_clip", { id: "throttle", body: { title: "Throttle", rate: 60, length: 40, input: [{ action: ia.throttle, type: "axis1d", notes: [{ id: "n1", start: 0, length: 30, value: 1 }] }] } });
    await hub("write_clip", { id: "jumps", body: { title: "Jumps", rate: 60, length: 180, input: [{ action: ia.jump, type: "bool", notes: [{ id: "a", start: 10, length: 10 }, { id: "b", start: 80, length: 10 }, { id: "c", start: 150, length: 10 }] }] } });
    const walked = { id: "walked", title: "Walked", at: 0, length: 100, mode: "by_end", expect: { value: "pos_x", op: "increasing", target: 200 } };
    await hub("write_arrangement", { id: "walk-only", body: { title: "Walk only", level: LEVEL, tracks: [{ id: "hero", title: "Hero", performer: { kind: "player", client: 0 }, instances: [{ id: "i1", clip: "walk", at: 0 }] }], checks: [walked], regions: [] } });
    const made = await hub("write_arrangement", {
      id: "door",
      body: {
        title: "Door",
        level: LEVEL,
        tracks: [{ id: "hero", title: "Hero", performer: { kind: "player", client: 0 }, instances: [{ id: "i1", clip: "walk", at: 0 }, { id: "i2", clip: "throttle", at: 180 }] }],
        checks: [
          walked,
          { id: "still", title: "At rest", at: 100, length: 0, timeout: 120, mode: "hold", expect: { value: "speed2d", op: "lt", target: 1 } },
          { id: "throttled", title: "Throttled", at: 215, length: 40, mode: "throughout", expect: { value: "ThrottleSum", op: "gt", target: 1.2 } },
          { id: "looks", title: "Looks right", at: 260, length: 0, mode: "manual", instruction: "Did the character walk, stop and throttle?" },
        ],
        regions: [{ id: "walk", name: "Walk", start: 0, end: 120 }, { id: "throttle", name: "Throttle", start: 120, end: 260 }],
      },
    });
    expect(made.findings).toEqual([]);
    expect(made.tests.map((t: Json) => t.id)).toEqual(["door", "door:walk", "door:throttle"]);
    expect((await hub("list_performers")).performers).toHaveLength(4);
  }, 60_000);

  it("plays a clip on its own and passes", async () => {
    const [run] = await runToEnd({ test: "walk-only", runner: "editor" });
    expect(run!.state, JSON.stringify(run)).toBe("passed");
    expect(run!.runner).toBe("editor");
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("plays one region and judges only its checks", async () => {
    const [run] = await runToEnd({ test: "door:walk", runner: "editor" });
    expect(run!.state, JSON.stringify(run)).toBe("passed");
    expect((run!.checks as Json[]).map((c) => [c.id, c.status])).toEqual([["walked", "passed"], ["still", "passed"]]);
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("plays the whole arrangement, waits for a manual verdict and records it", async () => {
    let gave = false;
    const [run] = await runToEnd({ test: "door" }, 1, 240_000, async (r) => {
      if (!gave && r.state === "awaiting_verdict") {
        gave = true;
        expect(r.runner, "a test with a manual check plays on the open editor").toBe("editor");
        await hub("verdict", { run_id: r.runId, check: "looks", verdict: "pass", note: "walked, stopped, throttled" });
      }
    });
    expect(gave).toBe(true);
    expect(run!.state, JSON.stringify(run)).toBe("passed");
    expect((run!.checks as Json[]).map((c) => [c.id, c.status])).toEqual([["walked", "passed"], ["still", "passed"], ["throttled", "passed"], ["looks", "passed"]]);
    expect((run!.checks as Json[]).find((c) => c.id === "looks")!.verdict).toMatchObject({ by: "agent" });
    expect(await waitPieStopped()).toBe(true);
  }, 300_000);

  it("auditions from the playhead without touching the run history", async () => {
    const before = (await hub("list_runs", { limit: 200 })).runs.length;
    const { play } = await hub("play", { test: "door", from: 170 });
    expect(play).toMatchObject({ test: "door", from: 170, loop: false });
    const moving = await playUntil((p) => typeof p.frame === "number");
    expect(moving.frame, JSON.stringify(moving)).toBeGreaterThanOrEqual(170);
    const done = await playUntil(() => false, 120_000);
    expect(done.state, JSON.stringify(done)).toBe("finished");
    expect(status(done, "walked")).toBe("skipped");
    expect(status(done, "throttled")).toBe("passed");
    expect(status(done, "looks")).toBeUndefined();
    expect((await hub("list_runs", { limit: 200 })).runs.length).toBe(before);
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("loops a region until stopped", async () => {
    await hub("play", { test: "door:walk", loop: true });
    const looping = await playUntil((p) => Number(p.pass) >= 2 && Number(p.frame) > 20, 120_000);
    expect(looping.state, JSON.stringify(looping)).toBe("running");
    expect(Number(looping.frame)).toBeLessThanOrEqual(120);
    expect(status(looping, "walked")).toBeDefined();
    const stopped = (await hub("stop_play")).play;
    expect(stopped.state).toBe("stopped");
    expect((await hub("read_queue")).waiting).toEqual(expect.any(Array));
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("runs a suite and rolls its health up", async () => {
    const suite = await hub("write_suite", { id: "smoke", body: { title: "Smoke", items: [{ test: "walk-only" }, { test: "door:throttle" }] } });
    expect(suite.tests).toHaveLength(2);
    const runs = await runToEnd({ suite: "smoke", runner: "editor" }, 2, 300_000);
    expect(runs.map((r) => [r.test, r.state]).sort(), JSON.stringify(runs)).toEqual([["door:throttle", "passed"], ["walk-only", "passed"]]);
    const after = await hub("read_suite", { id: "smoke" });
    expect(after.health).toBe("passing");
    expect(after.history.at(-1)).toMatchObject({ passed: 2, total: 2 });
    expect(await waitPieStopped()).toBe(true);
  }, 360_000);

  it("records a punch-in over a clip's range and keeps what lies outside it", async () => {
    await hub("record_start", { clip: "jumps", from: 60, to: 120 });
    // The take ends at the punch-out; nobody plays, so the range comes back silent.
    for (let i = 0; i < 600; i++) {
      await sleep(100);
      if (i > 20 && !(await pieRunning())) break;
    }
    const { clip } = await hub("record_stop");
    expect(clip.id).toBe("jumps");
    const notes = (clip.input as Json[]).find((l) => String(l.action).includes("IA_ProbeJump"))!.notes as Json[];
    expect(notes.map((n) => n.start), JSON.stringify(clip)).toEqual([10, 150]);
    const file = JSON.parse(fs.readFileSync(path.join(projectDir, "PlaytestHub", "clips", "jumps.json"), "utf8"));
    expect(file.input[0].notes.map((n: Json) => n.start)).toEqual([10, 150]);
    expect(file.lanes).toBeUndefined();
  }, 240_000);

  it("runs a test on a hidden editor on its own copy of the project, into the same history", async () => {
    const mb = (n: number) => Math.round(n / 1048576);
    expect(editorFits("headless", mb(os.freemem()), mb(os.totalmem())), `a hidden editor does not fit: ${mb(os.freemem())} MB free of ${mb(os.totalmem())} MB`).toBe(true);
    const [run] = await runToEnd({ test: "door:walk", runner: "headless" }, 1, 900_000);
    expect(run!.runner).toBe("headless");
    expect(run!.state, JSON.stringify(run)).toBe("passed");
    expect((await hub("read_test", { test: "door:walk" })).runs.map((r: Json) => r.runner)).toEqual(expect.arrayContaining(["headless", "editor"]));
  }, 960_000);
});
