// @live-editor headless
/**
 * Editor window control (#1231): list_windows, close_window and
 * press_window_button against a real floating window.
 *
 * Opening an asset editor raises a floating window the main window does not
 * own as a tab, which is the case list_dialogs never reported. The window is
 * read, closed and read again; a press on a window that is gone is refused.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MATERIAL = `${TEST_PREFIX}/Windows/M_WindowProbe`;

type Json = Record<string, unknown>;
type Window = { windowId: number; title: string; isModal: boolean; receivesInput: boolean };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

async function windows(): Promise<Window[]> {
  const listed = await callBridge(bridge, "list_windows", { maxTexts: 1, maxButtons: 1 });
  expect(listed.ok, listed.error).toBe(true);
  return (body(listed).windows as Window[] | undefined) ?? [];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: MATERIAL, force: true });
  const created = await callBridge(bridge, "create_material", { name: "M_WindowProbe", packagePath: `${TEST_PREFIX}/Windows` });
  expect(created.ok && body(created).success !== false, created.error ?? JSON.stringify(created.result)).toBe(true);
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: MATERIAL, force: true });
    disconnectBridge();
  }
});

describe("editor windows (#1231)", () => {
  it("lists an asset editor's floating window and closes it by windowId", async () => {
    const opened = await callBridge(bridge, "open_asset", { assetPath: MATERIAL });
    expect(opened.ok && body(opened).success !== false, opened.error ?? JSON.stringify(opened.result)).toBe(true);

    const before = await windows();
    const editorWindow = before.find((w) => w.title === "M_WindowProbe");
    expect(editorWindow, `no window titled M_WindowProbe in ${JSON.stringify(before)}`).toBeDefined();
    expect(editorWindow!.isModal).toBe(false);
    expect(editorWindow!.receivesInput).toBe(true);

    const closed = await callBridge(bridge, "close_window", { windowId: editorWindow!.windowId });
    expect(closed.ok, closed.error).toBe(true);
    expect(body(closed).closeRequested).toBe(true);
    expect(body(closed).windowTitle).toBe("M_WindowProbe");

    // Destruction is queued for the next Slate tick.
    let after = await windows();
    for (let i = 0; i < 10 && after.some((w) => w.windowId === editorWindow!.windowId); i++) {
      await new Promise((r) => setTimeout(r, 300));
      after = await windows();
    }
    expect(after.some((w) => w.windowId === editorWindow!.windowId)).toBe(false);
  }, 120_000);

  it("refuses a button press on a window that is not open", async () => {
    const pressed = await callBridge(bridge, "press_window_button", { windowId: 987654, buttonIndex: 0 });
    const refused = !pressed.ok || body(pressed).success === false;
    expect(refused, JSON.stringify(pressed.result ?? pressed.error)).toBe(true);
    expect(JSON.stringify(pressed.result ?? pressed.error)).toMatch(/987654|window/i);
  }, 60_000);
});
