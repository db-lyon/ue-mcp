// @live-editor headless
/**
 * Widget navigation rules written, cleared and restored, each read back through
 * audit_widget_focus_chain, which reports every explicit rule in the tree.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Widget";
const WBP = `${DIR}/WBP_BCov_Nav`;

type Json = Record<string, unknown>;
type Focusable = { widgetName: string; hasExplicitRules: boolean };
type Link = { widgetName: string; direction: string; widgetToFocus: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function audit(): Promise<Json> {
  return call("audit_widget_focus_chain", { assetPath: WBP });
}

const explicitOn = (a: Json, name: string): boolean | undefined =>
  (a.focusable as Focusable[]).find((f) => f.widgetName === name)?.hasExplicitRules;

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "VerticalBox", widgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "Button", widgetName: "Btn_A", parentWidgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "Button", widgetName: "Btn_B", parentWidgetName: "Root" });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    disconnectBridge();
  }
});

describe("widget navigation", () => {
  it("set, clear and restore an explicit rule, read back by audit_widget_focus_chain", async () => {
    const baseline = await audit();
    expect(baseline.explicitRuleCount).toBe(0);
    expect(explicitOn(baseline, "Btn_A")).toBe(false);

    const set = await call("set_widget_navigation", {
      assetPath: WBP,
      widgetName: "Btn_A",
      direction: "Down",
      rule: "Explicit",
      widgetToFocus: "Btn_B",
    });
    expect(set.changedCount).toBe(1);
    let a = await audit();
    expect(a.explicitRuleCount).toBe(1);
    expect(explicitOn(a, "Btn_A")).toBe(true);
    const oneWay = a.oneWayLinks as Link[];
    expect(oneWay.some((l) => l.widgetName === "Btn_A" && l.direction === "Down" && l.widgetToFocus === "Btn_B")).toBe(true);

    const cleared = await call("clear_widget_navigation", { assetPath: WBP, widgetName: "Btn_A" });
    expect(Number(cleared.clearedDirections)).toBeGreaterThanOrEqual(1);
    a = await audit();
    expect(a.explicitRuleCount).toBe(0);
    expect(explicitOn(a, "Btn_A")).toBe(false);

    const snapshot = ((cleared.rollback as Json).payload as Json).previous;
    const restored = await call("restore_widget_navigation", { assetPath: WBP, previous: snapshot });
    expect(Number(restored.restoredCount)).toBeGreaterThanOrEqual(1);
    a = await audit();
    expect(a.explicitRuleCount).toBe(1);
    expect(explicitOn(a, "Btn_A")).toBe(true);
  }, 180_000);
});
