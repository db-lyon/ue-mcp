// @live-editor headless
/**
 * editor(set_object_property) on an object that is part of an asset, here a
 * Widget Blueprint's WidgetTree template: the write dirties the package, says
 * a compile and a save keep it, and does keep it across a reload.
 * Refs #1544
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/ObjectPropertyAsset";
const NAME = "WBP_ObjPropAsset";
const WBP = `${DIR}/${NAME}`;
const TEMPLATE = `${WBP}.${NAME}:WidgetTree.Txt_Persist`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "CanvasPanel", widgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Txt_Persist", parentWidgetName: "Root" });
  await call("compile_blueprint", { assetPath: WBP });
  await call("save_asset", { assetPath: WBP, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    disconnectBridge();
  }
});

describe("set_object_property on a Widget Blueprint template", () => {
  it("reports the asset it wrote, and compile plus save keep the value across a reload", async () => {
    const set = await call("set_object_property", { objectPath: TEMPLATE, propertyName: "RenderOpacity", value: 0.25 });
    expect(set.changed).toBe(true);
    expect(set.persisted).toBe(false);
    expect(set.owner).toBe("asset");
    expect(set.package).toBe(WBP);
    expect(set.packageDirty).toBe(true);
    expect(set.saveRequired).toBe(true);
    expect(String(set.blueprint)).toContain(WBP);
    expect(set.compileRequired).toBe(true);
    expect(String(set.note)).not.toContain("live instance");
    expect(String(set.note)).toContain("blueprint(compile)");

    await call("compile_blueprint", { assetPath: WBP });
    const saved = await call("save_asset", { assetPath: WBP });
    expect(saved.wasDirty).toBe(true);

    await call("force_reload_asset", { assetPath: WBP, discardUnsaved: false });
    const read = await call("get_object_properties", { objectPath: TEMPLATE, propertyNames: ["RenderOpacity"] });
    expect(Number((read.properties as Json).RenderOpacity)).toBeCloseTo(0.25, 5);
  }, 120_000);
});
