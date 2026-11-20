// @live-editor headless
/**
 * Replacing the editor world while a play session is up (#1912).
 *
 * level(load) used to end a running session whenever it found one and report
 * endedPlaySession: true afterwards, so a user mid-play lost the session
 * without being asked. Every world-replacing action now refuses while a
 * session is up and names the flag that goes ahead: endPIE=true. The session
 * must still be running after each refusal, and get_world_state must say a
 * session is up in one field, whatever kind of session it is.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/PlaySessionSwitch`;
const TARGET = `${DIR}/L_PlaySessionTarget`;
const CREATED = `${DIR}/L_PlaySessionCreated`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function currentMap(): Promise<string> {
  return String((await call("get_current_level")).mapPackagePath ?? "");
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitForPie(playing: boolean, tries = 60): Promise<boolean> {
  for (let i = 0; i < tries; i++) {
    if ((await pieRunning()) === playing) return true;
    await sleep(500);
  }
  return false;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitForPie(false);
  }
}

/** Start a play session on the home level, or skip when this editor cannot start one. */
async function startPie(ctx: { skip: (reason: string) => void }): Promise<void> {
  await stopPie();
  if ((await currentMap()) !== HOME) await call("demo_go_home", { discardUnsaved: true });
  const started = await callBridge(bridge, "pie_control", { pieAction: "start" });
  if (!(await waitForPie(true))) ctx.skip(`PIE did not start: ${started.error ?? JSON.stringify(started.result)}`);
}

/** The refusal every world-replacing action gives while play is up, with the session left running. */
async function expectPlayRefusal(r: TestResult): Promise<void> {
  expect(succeeded(r), "a world-replacing call must not go ahead during play without endPIE").toBe(false);
  const refused = body(r);
  expect(refused.reason).toBe("play_session_active");
  expect(refused.playSessionActive).toBe(true);
  expect(String(refused.error)).toContain("endPIE=true");
  expect(await pieRunning(), "the refusal must leave the session running").toBe(true);
  expect(await currentMap()).toBe(HOME);
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await call("demo_go_home", { discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: TARGET, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: CREATED, force: true });
  await call("create_new_level", { levelPath: TARGET, discardUnsaved: true });
  await call("demo_go_home", { discardUnsaved: true });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await stopPie();
  await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: TARGET, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: CREATED, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
}, 300_000);

describe("get_world_state during play", () => {
  it("reports playSessionActive while a session is up, and false once it ends", async (ctx) => {
    await startPie(ctx);
    const during = await call("get_world_state");
    expect(during.playSessionActive).toBe(true);
    await stopPie();
    const after = await call("get_world_state");
    expect(after.playSessionActive).toBe(false);
    expect(after.mode).toBe("editor");
  }, 300_000);
});

describe("world-replacing actions during play", () => {
  it("load_level refuses and leaves the session running", async (ctx) => {
    await startPie(ctx);
    await expectPlayRefusal(await callBridge(bridge, "load_level", { levelPath: TARGET }));
  }, 300_000);

  it("create_new_level refuses, creates nothing, and leaves the session running", async (ctx) => {
    if (!(await pieRunning())) await startPie(ctx);
    await expectPlayRefusal(await callBridge(bridge, "create_new_level", { levelPath: CREATED }));
    expect(succeeded(await callBridge(bridge, "read_asset", { assetPath: CREATED }))).toBe(false);
  }, 300_000);

  it("open_asset on a map refuses the same way", async (ctx) => {
    if (!(await pieRunning())) await startPie(ctx);
    await expectPlayRefusal(await callBridge(bridge, "open_asset", { assetPath: TARGET }));
  }, 300_000);

  it("demo_go_home from another map refuses during play", async (ctx) => {
    await stopPie();
    await call("load_level", { levelPath: TARGET });
    const started = await callBridge(bridge, "pie_control", { pieAction: "start" });
    if (!(await waitForPie(true))) ctx.skip(`PIE did not start: ${started.error ?? JSON.stringify(started.result)}`);
    const r = await callBridge(bridge, "demo_go_home", {});
    expect(succeeded(r)).toBe(false);
    expect(body(r).reason).toBe("play_session_active");
    expect(await pieRunning()).toBe(true);
    expect(await currentMap()).toBe(TARGET);

    const home = await call("demo_go_home", { endPIE: true });
    expect(home.endedPlaySession).toBe(true);
    expect(await pieRunning()).toBe(false);
    expect(await currentMap()).toBe(HOME);
  }, 300_000);

  it("load_level with endPIE=true ends the session and switches", async (ctx) => {
    await startPie(ctx);
    const loaded = await call("load_level", { levelPath: TARGET, endPIE: true });
    expect(loaded.endedPlaySession).toBe(true);
    expect(await pieRunning()).toBe(false);
    expect(await currentMap()).toBe(TARGET);
    expect((await call("get_world_state")).playSessionActive).toBe(false);
  }, 300_000);

  it("load_level outside play reports that it ended nothing", async () => {
    await stopPie();
    const loaded = await call("load_level", { levelPath: HOME });
    expect(loaded.endedPlaySession).toBe(false);
    expect(await currentMap()).toBe(HOME);
  }, 300_000);
});
