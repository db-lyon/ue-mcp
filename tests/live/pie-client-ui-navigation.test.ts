// @live-editor render
/**
 * Slate focus and key events aimed at one PIE client's UI (#2018, #1928).
 *
 * A menu in UI-only input mode does not take Enhanced Input, so pie(send_key) sends the key through Slate as the
 * player's Slate user. The same client choice reaches add_to_viewport, stage_game_input and the two focus actions,
 * and a script's `key` act. Two buttons sit side by side: Right moves focus from the first to the second, Left
 * back. Run once in a single-player session, then on each client of a dedicated server, where the first world
 * context is the server and has no viewport.
 *
 * PIE is stopped after every session and the play settings are restored.
 * Refs #2018 #1928
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/PieClientUI";
const NAME = "WBP_ClientNav";
const WBP = `${DIR}/${NAME}`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

interface PieWorld { pie_instance: number; net_mode: string; players: Json[] }

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

async function startSession(numClients: number, netMode: "standalone" | "client"): Promise<PieWorld[]> {
  await stopPie();
  await call("configure_pie", { numClients, netMode, runUnderOneProcess: true, launchSeparateServer: false });
  await call("pie_control", { pieAction: "start" });
  return waitForSession(netMode === "standalone" ? 0 : numClients);
}

/** The widget on the player's screen, focusable, in UI-only mode, with focus on the first button. */
async function stageMenu(pieInstance: number): Promise<void> {
  const added = await call("add_to_viewport", { assetPath: WBP, pieInstance });
  expect(added.inViewport).toBe(true);
  expect(added.pieInstance).toBe(pieInstance);
  const staged = await call("stage_game_input", { inputMode: "uiOnly", pieInstance });
  expect(staged.pieInstance).toBe(pieInstance);
  await focusButton(pieInstance, "Btn_A");
}

async function focusButton(pieInstance: number, widgetName: string): Promise<void> {
  const focused = await call("set_runtime_focus", { widgetName, pieInstance });
  expect(focused.pieInstance).toBe(pieInstance);
  expect(obj(focused.rollback).payload).toMatchObject({ pieInstance });
  const path = await call("get_runtime_focus_path", { pieInstance });
  expect(path.pieInstance).toBe(pieInstance);
  expect(path.inTargetViewport).toBe(true);
  expect(path.focusedWidgetName).toBe(widgetName);
}

const widgetOf = (focus: unknown): string => String(obj(focus).widget);

/** Presses a key as one client's Slate user and checks the focus moved from one button to the other. */
async function navigate(pieInstance: number, key: string, from: string, to: string): Promise<void> {
  const sent = await call("pie_send_key", { key, pie_instance: pieInstance });
  expect(sent.pie_instance).toBe(pieInstance);
  expect(widgetOf(sent.focus_before), JSON.stringify(sent)).toBe(from);
  expect(widgetOf(sent.focus_after), JSON.stringify(sent)).toBe(to);
  expect(sent.focus_changed).toBe(true);
  expect(sent.down_handled, JSON.stringify(sent)).toBe(true);
  expect(typeof sent.up_handled).toBe("boolean");
  expect(obj(sent.focus_after).in_viewport).toBe(true);
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  savedPlay = await call("get_pie_config");
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "HorizontalBox", widgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "Button", widgetName: "Btn_A", parentWidgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "Button", widgetName: "Btn_B", parentWidgetName: "Root" });
}, 180_000);

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
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("one player in UI-only input mode", () => {
  let instance = -1;

  it("Right and Left move focus between the buttons and report key down and up handled", async () => {
    const worlds = await startSession(1, "standalone");
    instance = worlds.find((w) => w.players.length > 0)?.pie_instance ?? -1;
    expect(instance).toBeGreaterThanOrEqual(0);
    await stageMenu(instance);

    await navigate(instance, "Right", "Btn_A", "Btn_B");
    await navigate(instance, "Left", "Btn_B", "Btn_A");
  }, 180_000);

  it("down and up are separate events, and an unnamed player is the editor's own", async () => {
    const down = await call("pie_send_key", { key: "Right", event: "down" });
    expect(down.pie_instance).toBe(instance);
    expect(down.down_handled).toBe(true);
    expect(down.up_handled).toBeNull();
    expect(widgetOf(down.focus_after)).toBe("Btn_B");

    const up = await call("pie_send_key", { key: "Right", event: "up" });
    expect(up.down_handled).toBeNull();
    expect(typeof up.up_handled).toBe("boolean");
    expect(widgetOf(up.focus_after)).toBe("Btn_B");
    await focusButton(instance, "Btn_A");
  }, 60_000);

  it("a script's key act sends the key to the run's player", async () => {
    const started = await call("pie_script_run", {
      name: "client_ui_key",
      record: false,
      steps: [{ instruction: "Move focus right", act: [{ key: { key: "Right" } }], expect: { value: "Btn_B", op: "exists" } }],
    });
    let status: Json = {};
    for (let i = 0; i < 160 && status.state !== "finished"; i++) {
      status = await call("pie_script_status", { run_id: String(started.run_id) });
      if (status.state !== "finished") await sleep(250);
    }
    const act = obj(rows(status.steps)[0]?.act_result);
    expect(act.status, JSON.stringify(act)).toBe("done");
    expect(String(rows(act.actions)[0]?.target)).toMatch(new RegExp(`^pie_instance ${instance} `));
    const path = await call("get_runtime_focus_path", { pieInstance: instance });
    expect(path.focusedWidgetName).toBe("Btn_B");
  }, 90_000);

  it("a bad key and a missing client are refused with a way forward", async () => {
    const bad = await callBridge(bridge, "pie_send_key", { key: "NotAKey" });
    expect(succeeded(bad)).toBe(false);
    expect(JSON.stringify(bad)).toMatch(/not a key name/);
    const none = await callBridge(bridge, "pie_send_key", { key: "Right", pie_instance: 9 });
    expect(succeeded(none)).toBe(false);
    expect(JSON.stringify(none)).toMatch(/pie_instance/);
    await stopPie();
  }, 60_000);
});

describe("two clients of a dedicated server", () => {
  let first = -1;
  let second = -1;

  it("each client's menu takes focus and keys on its own, and the server is never the target", async () => {
    const worlds = await startSession(2, "client");
    const clients = worlds.filter((w) => w.players.length > 0).map((w) => w.pie_instance).sort((a, b) => a - b);
    expect(clients.length).toBeGreaterThanOrEqual(2);
    [first, second] = clients;

    await stageMenu(first);
    await navigate(first, "Right", "Btn_A", "Btn_B");

    await stageMenu(second);
    await navigate(second, "Right", "Btn_A", "Btn_B");
    await navigate(second, "Left", "Btn_B", "Btn_A");

    // Every window shares one Slate user, so the first client's focus is now elsewhere, and says so.
    const away = await call("get_runtime_focus_path", { pieInstance: first });
    expect(away.inTargetViewport).toBe(false);
    expect(String(away.viewportNote)).toMatch(/set_runtime_focus/);
  }, 240_000);

  it("a key with no pie_instance reaches the lowest client window, not the server", async () => {
    await focusButton(first, "Btn_B");
    const sent = await call("pie_send_key", { key: "Left" });
    expect(sent.pie_instance).toBe(first);
    expect(String(sent.target)).toMatch(/\(client\)$/);
    expect(widgetOf(sent.focus_after)).toBe("Btn_A");
  }, 60_000);

  it("a script aimed at the second client moves that client's focus", async () => {
    await focusButton(second, "Btn_A");
    const started = await call("pie_script_run", {
      name: "client_ui_key_second",
      record: false,
      pie_instance: second,
      steps: [{ instruction: "Move focus right", act: [{ key: { key: "Right" } }], expect: { value: "Btn_B", op: "exists" } }],
    });
    let status: Json = {};
    for (let i = 0; i < 160 && status.state !== "finished"; i++) {
      status = await call("pie_script_status", { run_id: String(started.run_id) });
      if (status.state !== "finished") await sleep(250);
    }
    const act = obj(rows(status.steps)[0]?.act_result);
    expect(act.status, JSON.stringify(act)).toBe("done");
    expect(String(rows(act.actions)[0]?.target)).toMatch(new RegExp(`^pie_instance ${second} .*\\(client\\)$`));
    const path = await call("get_runtime_focus_path", { pieInstance: second });
    expect(path.focusedWidgetName).toBe("Btn_B");
    expect(path.inTargetViewport).toBe(true);
    await stopPie();
  }, 120_000);
});
