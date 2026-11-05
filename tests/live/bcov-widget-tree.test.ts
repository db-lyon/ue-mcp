// @live-editor headless
/**
 * Widget Blueprint authoring read back through the tree, property and audit
 * reads: creation and listing, tree edits, property and style writes, class
 * listing, subtree extraction, designer bindings and the contract audits.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Widget";
const NAME = "WBP_BCov_Tree";
const WBP = `${DIR}/${NAME}`;
const EXTRACTED = `${DIR}/WBP_BCov_Extracted`;

type Json = Record<string, unknown>;
type TreeNode = { name: string; class: string; children?: TreeNode[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function tree(assetPath = WBP): Promise<TreeNode> {
  const read = await call("read_widget_tree", { assetPath });
  expect(typeof read.widgetTree).toBe("object");
  return read.widgetTree as TreeNode;
}

function find(node: TreeNode, name: string): TreeNode | undefined {
  if (node.name === name) return node;
  for (const child of node.children ?? []) {
    const hit = find(child, name);
    if (hit) return hit;
  }
  return undefined;
}

const childNames = (node: TreeNode | undefined): string[] => (node?.children ?? []).map((c) => c.name);

async function fullProps(widgetName: string): Promise<Record<string, string>> {
  const read = await call("get_widget_properties", { assetPath: WBP, widgetName });
  return (read.widget as { properties: Record<string, string> }).properties;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: EXTRACTED, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: EXTRACTED, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    disconnectBridge();
  }
});

describe("widget blueprint tree", () => {
  it("create_widget_blueprint makes an asset list_widget_blueprints reports", async () => {
    const created = await call("create_widget_blueprint", { assetPath: WBP });
    expect(String(created.path)).toContain(NAME);
    expect(String(created.parentClass)).toContain("UserWidget");
    const listed = await call("list_widget_blueprints", { recursive: true, limit: 2000 });
    const assets = listed.assets as Array<{ name: string; packagePath: string }>;
    expect(assets.some((a) => a.name === NAME && a.packagePath === DIR)).toBe(true);
  }, 120_000);

  it("add_widget, move_widget, reorder_child and remove_widget reshape the tree read_widget_tree reads", async () => {
    const root = await call("add_widget", { assetPath: WBP, widgetClass: "CanvasPanel", widgetName: "Root" });
    expect(root.isRoot).toBe(true);
    await call("add_widget", { assetPath: WBP, widgetClass: "VerticalBox", widgetName: "Col", parentWidgetName: "Root" });
    await call("add_widget", { assetPath: WBP, widgetClass: "HorizontalBox", widgetName: "Row", parentWidgetName: "Root" });
    await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Txt_A", parentWidgetName: "Col" });
    await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Txt_B", parentWidgetName: "Col" });

    let t = await tree();
    expect(t.name).toBe("Root");
    expect(childNames(t)).toEqual(["Col", "Row"]);
    expect(childNames(find(t, "Col"))).toEqual(["Txt_A", "Txt_B"]);

    const moved = await call("move_widget", { assetPath: WBP, widgetName: "Txt_B", newParentWidgetName: "Row" });
    expect(moved.oldParent).toBe("Col");
    t = await tree();
    expect(childNames(find(t, "Col"))).toEqual(["Txt_A"]);
    expect(childNames(find(t, "Row"))).toEqual(["Txt_B"]);

    await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Txt_C", parentWidgetName: "Col" });
    const reordered = await call("reorder_child", { assetPath: WBP, widgetName: "Txt_C", index: 0 });
    expect(reordered.oldIndex).toBe(1);
    expect(reordered.newIndex).toBe(0);
    expect(childNames(find(await tree(), "Col"))).toEqual(["Txt_C", "Txt_A"]);

    const removed = await call("remove_widget", { assetPath: WBP, widgetName: "Txt_C" });
    expect(removed.deleted).toBe(true);
    t = await tree();
    expect(find(t, "Txt_C")).toBeUndefined();
    expect(childNames(find(t, "Col"))).toEqual(["Txt_A"]);
  }, 180_000);

  it("wrap_root_widget puts a panel above the root and set_root_widget swaps the root back", async () => {
    const wrapped = await call("wrap_root_widget", { assetPath: WBP, wrapperClass: "Overlay", wrapperName: "Wrap" });
    expect(wrapped.wrappedChild).toBe("Root");
    let t = await tree();
    expect(t.name).toBe("Wrap");
    expect(childNames(t)).toEqual(["Root"]);

    const swapped = await call("set_root_widget", { assetPath: WBP, widgetName: "Root" });
    expect(swapped.previousRoot).toBe("Wrap");
    t = await tree();
    expect(t.name).toBe("Root");
    expect(find(t, "Wrap")).toBeUndefined();
    expect(find(t, "Txt_A")).toBeDefined();
  }, 120_000);

  it("set_widget_property writes typed and reflected properties get_widget_details and get_widget_properties read", async () => {
    await call("set_widget_property", { assetPath: WBP, widgetName: "Txt_A", propertyName: "Text", value: "BCov hello" });
    const details = await call("get_widget_details", { assetPath: WBP, widgetName: "Txt_A" });
    const props = details.properties as Json;
    expect(props.text).toBe("BCov hello");
    expect(props.widgetType).toBe("TextBlock");
    expect(props.parentName).toBe("Col");

    await call("set_widget_property", { assetPath: WBP, widgetName: "Txt_A", propertyName: "RenderOpacity", value: "0.25" });
    expect(Number.parseFloat((await fullProps("Txt_A")).RenderOpacity)).toBeCloseTo(0.25, 3);

    const subtree = await call("get_widget_properties", { assetPath: WBP, widgetName: "Col", includeSubtree: true });
    expect((subtree.subtree as Array<{ name: string }>).map((w) => w.name)).toContain("Txt_A");
  }, 120_000);

  it("set_widget_style writes a style struct get_widget_details reads back", async () => {
    const styled = await call("set_widget_style", {
      assetPath: WBP,
      widgetName: "Txt_A",
      propertyName: "ColorAndOpacity",
      value: { SpecifiedColor: { R: 0.2, G: 0.4, B: 0.6, A: 1 } },
    });
    expect(styled.propertyName).toBe("ColorAndOpacity");
    const color = ((await call("get_widget_details", { assetPath: WBP, widgetName: "Txt_A" })).properties as Json).color as Json;
    expect(Number(color.r)).toBeCloseTo(0.2, 3);
    expect(Number(color.g)).toBeCloseTo(0.4, 3);
    expect(Number(color.b)).toBeCloseTo(0.6, 3);
  }, 120_000);

  it("bulk_set_widget_properties applies several writes get_widget_properties reads back", async () => {
    const bulk = await call("bulk_set_widget_properties", {
      assetPath: WBP,
      properties: [
        { widgetName: "Txt_A", propertyName: "RenderOpacity", value: 0.5 },
        { widgetName: "Row", propertyName: "RenderOpacity", value: 0.75 },
      ],
    });
    expect(bulk.applied).toBe(2);
    expect(bulk.failed).toBe(0);
    expect(Number.parseFloat((await fullProps("Txt_A")).RenderOpacity)).toBeCloseTo(0.5, 3);
    expect(Number.parseFloat((await fullProps("Row")).RenderOpacity)).toBeCloseTo(0.75, 3);
  }, 120_000);

  it("list_widget_classes reports native panels and this blueprint's generated class", async () => {
    const native = await call("list_widget_classes", { filter: "Panel", module: "UMG", limit: 500 });
    const classes = native.classes as Array<{ name: string; isPanel: boolean; module: string }>;
    const canvas = classes.find((c) => c.name === "CanvasPanel");
    expect(canvas?.isPanel).toBe(true);
    expect(canvas?.module).toBe("UMG");
    expect(classes.some((c) => c.name === "TextBlock")).toBe(false);

    const own = await call("list_widget_classes", { filter: NAME, includeBlueprint: true, limit: 50 });
    const rows = own.classes as Array<{ name: string; isBlueprint: boolean; isUserWidget: boolean }>;
    const generated = rows.find((c) => c.name === `${NAME}_C`);
    expect(generated?.isBlueprint).toBe(true);
    expect(generated?.isUserWidget).toBe(true);
  }, 120_000);

  it("extract_widget_subtree plans by default and then lifts the subtree into a new blueprint", async () => {
    const plan = await call("extract_widget_subtree", {
      sourceAssetPath: WBP,
      sourceWidgetName: "Col",
      destinationAssetPath: EXTRACTED,
    });
    expect(plan.dryRun).toBe(true);
    expect(plan.widgetCount).toBe(2);
    expect(succeeded(await callBridge(bridge, "read_widget_tree", { assetPath: EXTRACTED }))).toBe(false);

    const done = await call("extract_widget_subtree", {
      sourceAssetPath: WBP,
      sourceWidgetName: "Col",
      destinationAssetPath: EXTRACTED,
      dryRun: false,
    });
    expect(done.dryRun).toBe(false);
    const extracted = await tree(EXTRACTED);
    expect(extracted.name).toBe("Col");
    expect(childNames(extracted)).toEqual(["Txt_A"]);
    expect(find(await tree(), "Col")).toBeDefined();
  }, 180_000);

  it("list_widget_bindings reads a designer binding and clear_widget_binding removes it", async () => {
    await call("set_object_property", {
      objectPath: `${WBP}.${NAME}`,
      propertyName: "Bindings",
      value: [{ ObjectName: "Txt_A", PropertyName: "Text", FunctionName: "GetBCovText" }],
    });
    const listed = await call("list_widget_bindings", { assetPath: WBP, filterWidgetName: "Txt_A" });
    expect(listed.bindingCount).toBe(1);
    const binding = (listed.bindings as Json[])[0];
    expect(binding.propertyName).toBe("Text");
    expect(binding.functionName).toBe("GetBCovText");

    const cleared = await call("clear_widget_binding", { assetPath: WBP, widgetName: "Txt_A", propertyName: "Text" });
    expect(cleared.removed).toBe(1);
    expect((await call("list_widget_bindings", { assetPath: WBP })).bindingCount).toBe(0);
  }, 120_000);

  it("audit_widget_accessibility flags a font below the minimum and clears once it is raised", async () => {
    const flagged = (audit: Json) => (audit.smallFonts as Array<{ widgetName: string }>).some((f) => f.widgetName === "Txt_A");

    await call("set_widget_property", { assetPath: WBP, widgetName: "Txt_A", propertyName: "fontSize", value: "8" });
    const small = await call("audit_widget_accessibility", { assetPath: WBP, minFontSize: 12 });
    expect(flagged(small)).toBe(true);

    await call("set_widget_property", { assetPath: WBP, widgetName: "Txt_A", propertyName: "fontSize", value: "24" });
    const fixed = await call("audit_widget_accessibility", { assetPath: WBP, minFontSize: 12 });
    expect(flagged(fixed)).toBe(false);
  }, 120_000);

  it("get_bind_widget_contract reads the parent's contract and checks this tree against it", async () => {
    const contract = await call("get_bind_widget_contract", { assetPath: WBP });
    expect(contract.className).toBe("UserWidget");
    expect(contract.requiredCount).toBe(0);
    const check = contract.check as Json;
    expect(check.parentClass).toBe("UserWidget");
    expect(check.missing).toEqual([]);
    expect(check.compiles).toBe(true);
  }, 120_000);

  it("audit_commonui reports the plugin state the loaded widget classes agree with", async () => {
    const audit = await call("audit_commonui", { assetPath: WBP });
    const commonClasses = await call("list_widget_classes", { module: "CommonUI", includeAbstract: true, limit: 50 });
    const loaded = (commonClasses.classes as unknown[]).length > 0;
    expect(audit.pluginEnabled).toBe(loaded);
    const problems = audit.problems as Array<{ rule: string }>;
    expect(problems.some((p) => p.rule === "plugin_enabled")).toBe(!loaded);
    expect(audit.checked as string[]).toContain("plugin_enabled");
  }, 120_000);
});
