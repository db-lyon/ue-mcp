// @live-editor headless
/**
 * Test scripts and replays in multiplayer PIE, in the two one-process net modes: a listen-server host with one
 * client, and two clients of an in-process dedicated server.
 *
 * - An act entry with no client drives the editor's own player, one exact local player (the host, or the lowest
 *   client PIE instance), and an entry with client 1 the remote player; act_result names who each drove (#1814).
 * - script_run's pie_instance points those entries at another client (#2012).
 * - A place on client 0 and replay_state apply move the dedicated server's pawn too, so the server does not pull
 *   the client back; the replayer's frame-0 teleport does the same and reports start_alignment (#2010).
 * - A replay's start_when finds the possessed pawn of a client of a dedicated server (#2011).
 * - record_diff aligns two runs a few rows apart before it pairs rows (#2009), offline, before any PIE.
 *
 * The play settings are restored afterwards and PIE is stopped after every session.
 * Refs #1814 #2012 #2011 #2010 #2009
 */
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/PieMultiplayer";
const IA = `${DIR}/IA_MPInteract.IA_MPInteract`;
const CUBE = "MP_Pie_Cube";
const CUBE_Y = -16000;
// Far from any spawn point, so a position near it can only come from the move under test.
const SPOT = { x: 1500, y: CUBE_Y, z: 400 };
const TMP = path.join(os.tmpdir(), `ue-mcp-mp-${process.pid}-${Date.now()}`);

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let savedPlay: Json = {};

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 60 && (await pieRunning()); i++) await sleep(500);
  }
}

interface PieWorld { pie_instance: number; net_mode: string; players: Json[] }

const pawned = (w: PieWorld): boolean => typeof obj(w.players[0]).pawn === "string";

/** Waits until every remote player has joined and every player world has a pawn. */
async function waitForSession(remotes: number): Promise<PieWorld[]> {
  let worlds: PieWorld[] = [];
  for (let i = 0; i < 240; i++) {
    const clients = await callBridge(bridge, "pie_clients", {});
    if (succeeded(clients)) {
      const ready = rows(body(clients).clients).filter((c) => c.ready === true).length;
      worlds = rows(body(clients).pie_worlds).map((w) => w as unknown as PieWorld);
      const players = worlds.filter((w) => w.players.length > 0);
      if (ready >= remotes && players.length > 0 && players.every(pawned)) return worlds;
    }
    await sleep(500);
  }
  return worlds;
}

async function startSession(netMode: "listen" | "client"): Promise<PieWorld[]> {
  await stopPie();
  await call("configure_pie", { numClients: 2, netMode, runUnderOneProcess: true, launchSeparateServer: false });
  await call("pie_control", { pieAction: "start" });
  return waitForSession(netMode === "listen" ? 1 : 2);
}

/** Polls a script run until it finishes. */
async function finished(runId: string, tries = 160): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_script_status", { run_id: runId });
    if (status.state === "finished") return status;
    await sleep(250);
  }
  return status;
}

async function runScript(steps: Json[], extra: Json = {}): Promise<Json> {
  const started = await call("pie_script_run", { name: "mp", steps, record: false, ...extra });
  return finished(String(started.run_id));
}

const stepOf = (s: Json, i: number): Json => rows(s.steps)[i] ?? {};
const targets = (s: Json, i: number): string[] => rows(obj(stepOf(s, i).act_result).actions).map((a) => String(a.target ?? ""));

/** A recording folder (recording.csv, sequence.json) whose pawn rows are given as [x, y, z] per 60 Hz row. */
function writeRecording(name: string, positions: number[][], steps: Json[] = []): string {
  const dir = path.join(TMP, name);
  fs.mkdirSync(dir, { recursive: true });
  const header = "frame,time,pos_x,pos_y,pos_z,rot_pitch,rot_yaw,rot_roll,vel_x,vel_y,vel_z,speed2d";
  const lines = positions.map(([x, y, z], i) => `${i},${(i / 60).toFixed(6)},${x},${y},${z},0,0,0,0,0,0,0`);
  fs.writeFileSync(path.join(dir, "recording.csv"), `${header}\n${lines.join("\n")}\n`);
  fs.writeFileSync(path.join(dir, "sequence.json"), JSON.stringify({ version: 2, settle_ms: 0, sample_hz: 60, steps }));
  return dir;
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  savedPlay = await call("get_pie_config");
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_input_action", { name: "IA_MPInteract", packagePath: DIR });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 0, y: CUBE_Y, z: 300 } });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
    await callBridge(bridge, "configure_pie", {
      numClients: Number(savedPlay.numClients ?? 1),
      netMode: String(savedPlay.netMode ?? "standalone"),
      runUnderOneProcess: savedPlay.runUnderOneProcess !== false,
      launchSeparateServer: savedPlay.launchSeparateServer === true,
    });
  } finally {
    await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    fs.rmSync(TMP, { recursive: true, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("record_diff alignment (#2009)", () => {
  // Still for 10 rows, then 10 cm a row; b is the same motion 3 rows later.
  const motion = (lag: number) => Array.from({ length: 80 }, (_, i) => [Math.max(0, i - 10 - lag) * 10, 0, 100]);

  it("auto shifts b by the rows its input landed late and reports the offset; index pairs rows as before", async () => {
    writeRecording("diff_a", motion(0));
    writeRecording("diff_b", motion(3));
    const auto = await call("pie_record_diff", { a_id: "diff_a", b_id: "diff_b", recording_dir: TMP });
    expect(obj(auto.alignment)).toMatchObject({ mode: "auto", offset_rows: 3 });
    expect(Number(obj(auto.alignment).offset_ms)).toBeCloseTo(50, 0);
    expect(Number(auto.max_position_drift_cm)).toBeLessThan(0.01);
    expect(Number(obj(auto.alignment).unaligned_max_position_drift_cm)).toBeCloseTo(30, 3);
    expect(rows(auto.frames_over_threshold)).toHaveLength(0);

    const index = await call("pie_record_diff", { a_id: "diff_a", b_id: "diff_b", recording_dir: TMP, align: "index" });
    expect(obj(index.alignment)).toMatchObject({ mode: "index", offset_rows: 0 });
    expect(Number(index.max_position_drift_cm)).toBeCloseTo(30, 3);
    expect(rows(index.frames_over_threshold).length).toBeGreaterThan(0);

    const self = await call("pie_record_diff", { a_id: "diff_a", b_id: "diff_a", recording_dir: TMP });
    expect(obj(self.alignment).offset_rows).toBe(0);
    expect(Number(self.frames_compared)).toBe(80);
  }, 30_000);
});

describe("listen server: the host and one client", () => {
  let host = -1;

  it("starts with the host and a joined client", async () => {
    const worlds = await startSession("listen");
    const listen = worlds.find((w) => w.net_mode === "listenServer");
    expect(listen, JSON.stringify(worlds)).toBeDefined();
    expect(worlds.some((w) => w.net_mode === "client")).toBe(true);
    host = Number(listen?.pie_instance);
  }, 180_000);

  it("an act with no client drives the host's own player, and client 1 the remote player (#1814)", async () => {
    expect(host).toBeGreaterThanOrEqual(0);
    const status = await runScript([{
      instruction: "Host presses and holds, the client presses",
      // One hold or tape per action and player at a time, so the hold waits for the press to play out.
      act: [{ press: { action: IA } }, { wait_ms: 300 }, { client: 0, inject: { action: IA, value: 1 } }, { hold: { action: IA, value: 1, ms: 300 } }, { client: 1, press: { action: IA } }],
      expect: { value: CUBE, op: "exists" },
    }]);
    expect(stepOf(status, 0).status, JSON.stringify(stepOf(status, 0))).toBe("passed");
    const [press, wait, inject, hold, remote] = targets(status, 0);
    expect(wait).toBe("");
    for (const t of [press, inject, hold]) expect(t).toMatch(new RegExp(`^pie_instance ${host} local player 0 in .+ \\(listenServer\\)$`));
    expect(remote).toBe("remote client 1");
  }, 60_000);

  it("stops", async () => {
    await stopPie();
    expect(await pieRunning()).toBe(false);
  }, 60_000);
});

describe("dedicated server: two clients", () => {
  let first = -1;
  let second = -1;

  it("starts with two joined clients and no host player", async () => {
    const worlds = await startSession("client");
    expect(worlds.some((w) => w.net_mode === "dedicatedServer"), JSON.stringify(worlds)).toBe(true);
    const clients = worlds.filter((w) => w.net_mode === "client").map((w) => w.pie_instance).sort((a, b) => a - b);
    expect(clients).toHaveLength(2);
    [first, second] = clients;
  }, 180_000);

  it("an act with no client drives the first client window, and script_run's pie_instance another client (#1814, #2012)", async () => {
    const step = { instruction: "Press", act: [{ press: { action: IA } }], expect: { value: CUBE, op: "exists" } };
    const own = await runScript([step]);
    expect(stepOf(own, 0).status, JSON.stringify(stepOf(own, 0))).toBe("passed");
    expect(targets(own, 0)[0]).toMatch(new RegExp(`^pie_instance ${first} local player 0 in .+ \\(client\\)$`));

    const other = await runScript([step], { pie_instance: second });
    expect(stepOf(other, 0).status, JSON.stringify(stepOf(other, 0))).toBe("passed");
    expect(targets(other, 0)[0]).toMatch(new RegExp(`^pie_instance ${second} local player 0 in .+ \\(client\\)$`));
  }, 90_000);

  it("place on client 0 moves the server's pawn, so the client stays where it was put (#2010)", async () => {
    const status = await runScript([{
      instruction: "Stand at the spot and stay there",
      act: [{ place: { location: [SPOT.x, SPOT.y, SPOT.z], yaw: 0 } }, { wait_ms: 1500 }],
      expect: { value: "RootComponent.RelativeLocation.Y", op: "lt", target: CUBE_Y + 500, within_ms: 2000 },
    }], { pie_instance: first });
    const step = stepOf(status, 0);
    expect(step.status, JSON.stringify(step)).toBe("passed");
    expect(targets(status, 0)[0]).toMatch(/\(dedicatedServer\)$/);
    expect(Number(obj(step.evidence).at_end)).toBeLessThan(CUBE_Y + 500);
  }, 60_000);

  it("replay_state apply moves the server's pawn for that client too (#2010)", async () => {
    // Move the second client somewhere else first, so the apply has to bring it back.
    writeRecording("state", [[SPOT.x + 600, SPOT.y, SPOT.z]]);
    const applied = await call("pie_replay_state", { recording_dir: TMP, recording_id: "state", at_frame: 0, apply: true, pie_instance: second });
    expect(applied.applied, JSON.stringify(applied)).toBe(true);
    expect(applied.applied_on).toBe("server_and_client");
    expect(String(applied.server_pawn)).not.toBe("");
    expect(String(applied.apply_target)).toMatch(new RegExp(`^pie_instance ${second} `));

    // Two seconds later the server has not pulled the client back to its own position.
    const held = await runScript([{
      instruction: "Still where replay_state put it",
      act: [{ wait_ms: 2000 }],
      expect: { value: "RootComponent.RelativeLocation.X", op: "gt", target: SPOT.x + 300, within_ms: 2000 },
    }], { pie_instance: second });
    expect(stepOf(held, 0).status, JSON.stringify(stepOf(held, 0))).toBe("passed");
  }, 60_000);

  it("a replay's start_when opens on a client of the dedicated server, which starts from the recorded spot (#2011, #2010)", async () => {
    await stopPie();
    const rest = Array.from({ length: 120 }, () => [SPOT.x - 600, SPOT.y, SPOT.z]);
    const dir = writeRecording("rest", rest, [{ type: "mark", delay_ms: 1500, label: "end" }]);
    await call("pie_replay_run", {
      recording_dir: dir,
      pie_instance: first,
      pin_fps: 0,
      start_when: { pawn_at_rest: true, max_speed_cms: 50, hold_ms: 300, timeout_ms: 30000 },
    });
    let status: Json = {};
    let alignment = "";
    for (let i = 0; i < 240; i++) {
      status = await call("pie_replay_status");
      if (typeof status.start_alignment === "string") alignment = status.start_alignment;
      if (status.pie_active === false && status.last_result) break;
      await sleep(500);
    }
    expect(String(status.last_error ?? ""), JSON.stringify(status)).not.toMatch(/start_when|no pawn/);
    expect(alignment).toMatch(/^server and client \(/);
    const last = obj(status.last_result);
    expect(Number(last.frames_compared)).toBeGreaterThan(0);
    expect(Number(last.max_position_drift_cm), JSON.stringify(last)).toBeLessThan(50);
  }, 180_000);
});
