// @live-editor headless
/**
 * widget(invoke_runtime_function) pressing a CommonUI button in PIE: a
 * CommonButtonBase subclass placed as a child is clicked through childName,
 * and the button's own selection state proves its click handler ran.
 * Refs #1455
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Widget";
const BUTTON = `${DIR}/WBP_BCov_CommonButton`;
const HOST_NAME = "WBP_BCov_CommonHost";
const HOST = `${DIR}/${HOST_NAME}`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let playing = false;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  const status = await callBridge(bridge, "pie_control", { pieAction: "status" });
  return body(status).isPlaying === true;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "pie_control", { pieAction: "stop" });
  await callBridge(bridge, "delete_asset", { assetPath: HOST, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: BUTTON, force: true });

  const created = await call("create_widget_blueprint", { assetPath: BUTTON, parentClass: "CommonButtonBase" });
  expect(String(created.parentClass)).toContain("CommonButtonBase");
  await call("create_widget_blueprint", { assetPath: HOST });
  await call("add_widget", { assetPath: HOST, widgetClass: "CanvasPanel", widgetName: "Root" });
  await call("add_widget", { assetPath: HOST, widgetClass: BUTTON, widgetName: "CBtn", parentWidgetName: "Root" });
  for (const propertyName of ["bSelectable", "bToggleable", "bInteractableWhenSelected"]) {
    await call("set_widget_property", { assetPath: HOST, widgetName: "CBtn", propertyName, value: "true" });
  }

  await call("pie_control", { pieAction: "start" });
  for (let i = 0; i < 30 && !playing; i++) {
    playing = await pieRunning();
    if (!playing) await new Promise((r) => setTimeout(r, 1000));
  }
}, 240_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await new Promise((r) => setTimeout(r, 500));
    await callBridge(bridge, "delete_asset", { assetPath: HOST, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: BUTTON, force: true });
    disconnectBridge();
  }
});

describe("CommonUI button click", () => {
  it("invoke_runtime_function clicks a CommonButtonBase child and toggles its selection", async () => {
    expect(playing, "PIE did not start").toBe(true);
    const added = await call("add_to_viewport", { assetPath: HOST, zOrder: 5 });
    const instanceName = String(added.instanceName);

    const first = await call("invoke_runtime_function", { widgetName: instanceName, childName: "CBtn" });
    expect(first.childClass).toBe("WBP_BCov_CommonButton_C");
    expect(first.interaction).toBe("click");
    expect(first.handler).toBe("HandleButtonClicked");
    expect(first.invoked).toBe("OnClicked");
    expect(first.locked).toBe(false);
    expect(first.wasSelected).toBe(false);
    expect(first.isSelected).toBe(true);

    const second = await call("invoke_runtime_function", { widgetName: instanceName, childName: "CBtn", functionName: "OnClicked" });
    expect(second.wasSelected).toBe(true);
    expect(second.isSelected).toBe(false);

    const hover = await callBridge(bridge, "invoke_runtime_function", {
      widgetName: instanceName,
      childName: "CBtn",
      functionName: "OnHovered",
    });
    expect(succeeded(hover)).toBe(false);
    expect(JSON.stringify(hover.result ?? hover.error)).toContain("OnClicked, OnPressed, OnReleased");
  }, 60_000);
});
