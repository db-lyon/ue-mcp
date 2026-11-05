// @live-editor headless
/**
 * Live widget actions in a Play-In-Editor session: a widget added to the
 * viewport, found by the runtime listing and inspection reads, driven through a
 * child interaction, and given keyboard focus that the focus path reports.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Widget";
const NAME = "WBP_BCov_Runtime";
const WBP = `${DIR}/${NAME}`;
const TEXT = "BCov runtime";

type Json = Record<string, unknown>;
type RuntimeNode = { name: string; class: string; text?: string; children?: RuntimeNode[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let playing = false;
let instanceName = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  const status = await callBridge(bridge, "pie_control", { pieAction: "status" });
  return body(status).isPlaying === true;
}

function find(node: RuntimeNode | undefined, name: string): RuntimeNode | undefined {
  if (!node) return undefined;
  if (node.name === name) return node;
  for (const child of node.children ?? []) {
    const hit = find(child, name);
    if (hit) return hit;
  }
  return undefined;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "pie_control", { pieAction: "stop" });
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "CanvasPanel", widgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "Button", widgetName: "Btn_Focus", parentWidgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "CheckBox", widgetName: "Chk", parentWidgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Txt", parentWidgetName: "Root" });
  await call("set_widget_property", { assetPath: WBP, widgetName: "Txt", propertyName: "Text", value: TEXT });

  await callBridge(bridge, "pie_control", { pieAction: "start" });
  for (let i = 0; i < 30 && !playing; i++) {
    playing = await pieRunning();
    if (!playing) await new Promise((r) => setTimeout(r, 1000));
  }
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await new Promise((r) => setTimeout(r, 500));
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    disconnectBridge();
  }
});

describe("runtime widgets", () => {
  it("add_to_viewport creates an instance list_runtime_widgets and get_runtime_widget find", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const added = await call("add_to_viewport", { assetPath: WBP, zOrder: 5 });
    instanceName = String(added.instanceName);
    expect(added.inViewport).toBe(true);
    expect(added.class).toBe(`${NAME}_C`);

    const listed = await call("list_runtime_widgets", { classFilter: NAME, viewportOnly: true });
    const rows = listed.widgets as Array<{ name: string; inViewport: boolean; rootWidgetName?: string }>;
    const row = rows.find((w) => w.name === instanceName);
    expect(row?.inViewport).toBe(true);
    expect(row?.rootWidgetName).toBe("Root");

    const live = await call("get_runtime_widget", { widgetName: instanceName });
    expect(live.class).toBe(`${NAME}_C`);
    const t = live.tree as RuntimeNode;
    expect(t.name).toBe("Root");
    expect(find(t, "Txt")?.text).toBe(TEXT);
    expect(find(t, "Btn_Focus")?.class).toBe("Button");
  }, 120_000);

  it("get_runtime_delegates reads the live instance's multicast delegates", async (ctx) => {
    if (!playing || !instanceName) ctx.skip("no live instance");
    const delegates = await call("get_runtime_delegates", { widgetName: instanceName });
    expect(delegates.widgetName).toBe(instanceName);
    expect(delegates.widgetClass).toBe(`${NAME}_C`);
    expect(delegates.delegateCount).toBe((delegates.delegates as unknown[]).length);
  }, 60_000);

  it("invoke_runtime_function checks the live CheckBox and inspect_runtime_instances reads its state", async (ctx) => {
    if (!playing || !instanceName) ctx.skip("no live instance");
    const checked = await call("invoke_runtime_function", { widgetName: instanceName, childName: "Chk", value: true });
    expect(checked.interaction).toBe("check");
    expect(checked.value).toBe("checked");

    const inspected = await call("inspect_runtime_instances", {
      widgetName: instanceName,
      childName: "Chk",
      propertyNames: ["CheckedState"],
    });
    const instance = (inspected.instances as Array<{ name: string; nodes: Array<{ name: string; properties: Json }> }>)[0];
    expect(instance.name).toBe(instanceName);
    const node = instance.nodes.find((n) => n.name === "Chk");
    const state = JSON.stringify(node?.properties.CheckedState);
    expect(state).toMatch(/Checked/);
    expect(state).not.toMatch(/Unchecked/);

    const unchecked = await call("invoke_runtime_function", { widgetName: instanceName, childName: "Chk", value: false });
    expect(unchecked.previousValue).toBe("checked");
    expect(unchecked.value).toBe("unchecked");
  }, 60_000);

  it("set_runtime_focus moves keyboard focus to the live button get_runtime_focus_path reports", async (ctx) => {
    if (!playing || !instanceName) ctx.skip("no live instance");
    const focused = await call("set_runtime_focus", { widgetName: "Btn_Focus", className: NAME });
    expect(focused.widgetClass).toBe("Button");
    expect(focused.slateType).toBe("SButton");

    const path = await call("get_runtime_focus_path", { userIndex: 0 });
    expect(path.hasFocus).toBe(true);
    expect(path.focusedWidgetType).toBe("SButton");
  }, 60_000);
});
