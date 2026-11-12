// @live-editor headless
/**
 * Widget subtree authoring (#1300), widget structure checks (#1350) and
 * Blueprint subclass discovery (#1211), each read back from the editor.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BpWidget`;
const WIDGET = `${DIR}/WBP_TreeProbe`;
const PARENT = `${DIR}/BP_SubParent`;
const CHILD = `${DIR}/BP_SubChild`;

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

function names(node: TreeNode | undefined): string[] {
  if (!node) return [];
  return [node.name, ...(node.children ?? []).flatMap(names)];
}

async function treeNames(): Promise<string[]> {
  const tree = await call("read_widget_tree", { assetPath: WIDGET });
  return names(tree.widgetTree as TreeNode);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of [WIDGET, CHILD, PARENT]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("create_widget_blueprint", { assetPath: WIDGET });
  await call("add_widget", { assetPath: WIDGET, widgetClass: "CanvasPanel", widgetName: "Root" });
  await call("create_blueprint", { assetPath: PARENT, parentClass: "Actor" });
  await call("create_blueprint", { assetPath: CHILD, parentClass: `${PARENT}.BP_SubParent_C` });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const path of [WIDGET, CHILD, PARENT]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("widget build_tree (#1300)", () => {
  it("builds a nested subtree in one call and removes it with the rollback", async () => {
    const built = await call("build_tree", {
      assetPath: WIDGET,
      parentWidgetName: "Root",
      tree: {
        widgetClass: "VerticalBox", widgetName: "Form", children: [
          { widgetClass: "HorizontalBox", widgetName: "Row", children: [
            { widgetClass: "TextBlock", widgetName: "Lbl", properties: { Text: "Hi" } },
            { widgetClass: "Button", widgetName: "Btn", children: [{ widgetClass: "TextBlock", widgetName: "BtnTxt", properties: { Text: "Ok" } }] },
          ] },
        ],
      },
    });
    expect(built.compileStatus).toBe("upToDate");
    expect(await treeNames()).toEqual(["Root", "Form", "Row", "Lbl", "Btn", "BtnTxt"]);

    const duplicate = await callBridge(bridge, "build_tree", {
      assetPath: WIDGET, parentWidgetName: "Root",
      tree: { widgetClass: "VerticalBox", widgetName: "Form2", children: [{ widgetClass: "TextBlock", widgetName: "Lbl" }] },
    });
    expect(succeeded(duplicate)).toBe(false);
    expect(await treeNames()).toEqual(["Root", "Form", "Row", "Lbl", "Btn", "BtnTxt"]);

    const rollback = built.rollback as { method: string; payload: Json };
    await call(rollback.method, rollback.payload);
    expect(await treeNames()).toEqual(["Root"]);
  }, 180_000);
});

describe("widget slot alignment on every slot class (#1541)", () => {
  const slotProps = async (widgetName: string): Promise<Record<string, string>> => {
    const read = await call("get_widget_properties", { assetPath: WIDGET, widgetName });
    return (read.widget as { slot: { properties: Record<string, string> } }).slot.properties;
  };

  it("build_tree and set_widget_property set alignment on SizeBox, WidgetSwitcher and box slots", async () => {
    const built = await call("build_tree", {
      assetPath: WIDGET,
      parentWidgetName: "Root",
      tree: {
        widgetClass: "VerticalBox", widgetName: "AlignForm", children: [
          { widgetClass: "SizeBox", widgetName: "Sized", children: [
            { widgetClass: "TextBlock", widgetName: "InSize", properties: { "slot.HorizontalAlignment": "Center", "slot.VerticalAlignment": "Bottom" } },
          ] },
          { widgetClass: "WidgetSwitcher", widgetName: "Switch", children: [
            { widgetClass: "TextBlock", widgetName: "InSwitch", properties: { "slot.HorizontalAlignment": "HAlign_Right", "slot.VerticalAlignment": "top" } },
          ] },
          { widgetClass: "TextBlock", widgetName: "InVBox", properties: { "slot.HorizontalAlignment": "HAlign_Right" } },
        ],
      },
    });

    const inSize = await slotProps("InSize");
    expect(inSize.HorizontalAlignment).toBe("HAlign_Center");
    expect(inSize.VerticalAlignment).toBe("VAlign_Bottom");
    const inSwitch = await slotProps("InSwitch");
    expect(inSwitch.HorizontalAlignment).toBe("HAlign_Right");
    expect(inSwitch.VerticalAlignment).toBe("VAlign_Top");
    expect((await slotProps("InVBox")).HorizontalAlignment).toBe("HAlign_Right");

    await call("set_widget_property", { assetPath: WIDGET, widgetName: "InSize", propertyName: "slot.hAlign", value: "left" });
    expect((await slotProps("InSize")).HorizontalAlignment).toBe("HAlign_Left");

    const bad = await callBridge(bridge, "set_widget_property", {
      assetPath: WIDGET, widgetName: "InSize", propertyName: "slot.HorizontalAlignment", value: "sideways",
    });
    expect(succeeded(bad)).toBe(false);
    expect(JSON.stringify(bad.result ?? bad.error)).toContain("sideways");
    expect((await slotProps("InSize")).HorizontalAlignment).toBe("HAlign_Left");

    const rollback = built.rollback as { method: string; payload: Json };
    await call(rollback.method, rollback.payload);
    expect(await treeNames()).toEqual(["Root"]);
  }, 180_000);
});

describe("widget set_property enum names and JSON values (#1540)", () => {
  const read = async (widgetName: string): Promise<{ properties: Record<string, string>; slot: { properties: Record<string, string> } }> =>
    (await call("get_widget_properties", { assetPath: WIDGET, widgetName })).widget as never;
  const refused = async (propertyName: string, value: unknown, mention: string): Promise<void> => {
    const r = await callBridge(bridge, "set_widget_property", { assetPath: WIDGET, widgetName: "EnumText", propertyName, value });
    expect(succeeded(r)).toBe(false);
    expect(JSON.stringify(r.result ?? r.error)).toContain(mention);
  };

  it("takes long, prefixed and numeric enum values, refuses bad ones, and fills structs from JSON", async () => {
    const built = await call("build_tree", {
      assetPath: WIDGET,
      parentWidgetName: "Root",
      tree: { widgetClass: "VerticalBox", widgetName: "EnumForm", children: [{ widgetClass: "TextBlock", widgetName: "EnumText" }] },
    });
    const set = (propertyName: string, value: unknown) =>
      call("set_widget_property", { assetPath: WIDGET, widgetName: "EnumText", propertyName, value });

    await set("Slot.VerticalAlignment", "VAlign_Bottom");
    expect((await read("EnumText")).slot.properties.VerticalAlignment).toBe("VAlign_Bottom");
    await set("Slot.HorizontalAlignment", "EHorizontalAlignment::HAlign_Right");
    expect((await read("EnumText")).slot.properties.HorizontalAlignment).toBe("HAlign_Right");
    await set("Slot.VerticalAlignment", 2);
    expect((await read("EnumText")).slot.properties.VerticalAlignment).toBe("VAlign_Center");
    await refused("Slot.VerticalAlignment", "9", "9");
    expect((await read("EnumText")).slot.properties.VerticalAlignment).toBe("VAlign_Center");

    await set("Justification", "ETextJustify::Right");
    expect((await read("EnumText")).properties.Justification).toMatch(/Right$/);
    await set("Justification", "Center");
    expect((await read("EnumText")).properties.Justification).toMatch(/Center$/);
    await refused("Justification", "Sideways", "Sideways");
    expect((await read("EnumText")).properties.Justification).toMatch(/Center$/);

    const padded = await set("Slot.Padding", { Left: 14, Top: 10, Right: 14, Bottom: 10 });
    expect(String(padded.propertyValue)).toContain("Left=14");
    const padding = (await read("EnumText")).slot.properties.Padding;
    for (const field of ["Left=14", "Top=10", "Right=14", "Bottom=10"]) expect(padding).toContain(field);
    expect((padded.rollback as { payload: Json }).payload.value).toMatch(/^\(Left=0/);
    await refused("Slot.Padding", { Sideways: 3 }, "Sideways");

    const rollback = built.rollback as { method: string; payload: Json };
    await call(rollback.method, rollback.payload);
    expect(await treeNames()).toEqual(["Root"]);
  }, 180_000);
});

describe("widget check_structure (#1350)", () => {
  it("passes a healthy Widget Blueprint with each widget's outer reported", async () => {
    const checked = await call("check_widget_structure", { assetPath: WIDGET });
    expect(checked.pass).toBe(true);
    expect(checked.failures).toBe(0);
    const designer = checked.designerTree as { tree: { actualOuter: string } };
    expect(designer.tree.actualOuter).toBe(`${WIDGET}.WBP_TreeProbe`);
  }, 120_000);
});

describe("list_subclasses (#1211)", () => {
  it("lists Blueprint subclasses with their depth, direct and transitive", async () => {
    const all = await call("list_subclasses", { className: "Actor", directory: DIR });
    const rows = all.subclasses as Array<{ path: string; depth: number }>;
    expect(rows.find((r) => r.path.includes("BP_SubParent"))?.depth).toBe(1);
    expect(rows.find((r) => r.path.includes("BP_SubChild"))?.depth).toBe(2);

    const direct = await call("list_subclasses", { className: "Actor", directory: DIR, includeTransitive: false });
    expect((direct.subclasses as Array<{ path: string }>).some((r) => r.path.includes("BP_SubChild"))).toBe(false);

    const ofParent = await call("list_subclasses", { className: PARENT });
    expect((ofParent.subclasses as Array<{ path: string }>).map((r) => r.path)).toEqual([`${CHILD}.BP_SubChild`]);
  }, 120_000);
});
