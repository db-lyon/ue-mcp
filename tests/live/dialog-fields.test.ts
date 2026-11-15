// @live-editor interactive
/**
 * A dialog's pickers and text fields, read and set through the widgets' own
 * handlers, against the real DataTable Options dialog.
 *
 * Importing a .csv attended raises SCSVImportOptions, exactly as dropping one
 * on the Content Browser does: an "Import As" picker (an SComboBox), a row
 * type picker (an SComboButton over its own list, which commits only when its
 * menu closes), the Ignore Extra Fields and Ignore Missing Fields checkboxes
 * and the Import Key Field text box of a details panel, and Apply, which stays
 * disabled until a row type is chosen.
 *
 * Everything the test sets is checked on the table the import makes, not on
 * the dialog, so a change that only moved pixels would fail it: the row type
 * decides the rows, Ignore Extra Fields decides whether the stray column is a
 * problem, and Import Key Field is written to the table.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/DialogFields`;
const TITLE = "DataTable Options";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let importer: EditorBridge | undefined;
let scratch = "";

async function raw(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok, `${method}: ${r.error}`).toBe(true);
  return body(r);
}

async function dialogs(): Promise<any[]> {
  const listed = await raw("list_dialogs");
  return Array.isArray(listed.dialogs) ? (listed.dialogs as any[]) : [];
}

async function waitForDialog(present: boolean, timeoutMs = 60_000): Promise<any> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const found = (await dialogs()).find((d) => d.title === TITLE);
    if (present ? found : !found) return found;
    if (Date.now() > deadline) throw new Error(`timed out waiting for the ${TITLE} dialog to ${present ? "appear" : "close"}`);
    await new Promise((r) => setTimeout(r, 250));
  }
}

const comboLabelled = (d: any, label: string) => (d.combos as any[]).find((c) => c.label === label);
const itemLabelled = (d: any, label: string) => (d.items as any[]).find((i) => i.fieldLabel === label);
const fieldLabelled = (d: any, label: string) => (d.textFields as any[]).find((f) => f.label === label);
const choice = (d: any, label: string) => (d.choices as any[]).find((c) => c.buttonLabel === label);

beforeAll(async () => {
  bridge = await getBridge();
  scratch = mkdtempSync(join(tmpdir(), "ue-mcp-dialog-fields-"));
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await raw("create_folder", { path: DIR });
}, 120_000);

afterAll(async () => {
  // Never leave the modal up for the next file: Cancel it if a case failed mid-way.
  if (bridge) {
    try {
      for (let i = 0; i < 3; i++) {
        const open = await dialogs();
        if (open.length === 0) break;
        // Cancel ends the import; any other prompt it raised (a problem
        // report) is closed unanswered.
        await callBridge(bridge, "respond_to_dialog", open[0].title === TITLE
          ? { buttonLabel: "Cancel" }
          : { dialogAction: "close" });
      }
    } catch { /* the editor may already be gone */ }
    importer?.disconnect();
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
  if (scratch) rmSync(scratch, { recursive: true, force: true });
}, 120_000);

describe("the DataTable Options dialog, answered through its fields", () => {
  it("lists the pickers, checkboxes and text field, sets them, and applies the import", async () => {
    const name = `DT_DialogFields_${Date.now()}`;
    const csv = join(scratch, `${name}.csv`);
    // "Stray" is not a GameplayTagTableRow field: only Ignore Extra Fields
    // being really on keeps it from being reported as a problem.
    writeFileSync(csv, "Key,Tag,DevComment,Stray\nRowA,Test.DialogFields.A,first,x\nRowB,Test.DialogFields.B,second,y\n");

    // Attended, as a Content Browser drop is: the factory raises the modal
    // and this call waits inside it until the dialog is answered. The bridge
    // answers one request per connection at a time, so the import gets its
    // own connection and the dialog calls are not queued behind it.
    importer = new EditorBridge("127.0.0.1", bridge.getTarget().port);
    await importer.connect(10_000);
    const importing = importer.call("import_file", { filePath: csv, packagePath: DIR, automated: false }, 180_000);
    importing.catch(() => undefined);

    let dialog = await waitForDialog(true);
    expect(dialog.blocksGameThread).toBe(true);
    // The details panel generates its rows when Slate first paints the
    // window, which can be a frame after the window is on the modal stack.
    for (const deadline = Date.now() + 20_000; !itemLabelled(dialog, "Ignore Extra Fields") && Date.now() < deadline;) {
      await new Promise((r) => setTimeout(r, 250));
      dialog = await waitForDialog(true);
    }

    // The pickers, labelled from the text above each one.
    const importAs = comboLabelled(dialog, "Import As");
    expect(importAs, JSON.stringify(dialog.combos)).toBeDefined();
    expect(importAs.value).toBe("DataTable");
    expect(importAs.optionsReadable).toBe(true);
    expect(importAs.options).toEqual(expect.arrayContaining(["DataTable", "CurveTable"]));
    const rowType = comboLabelled(dialog, "Choose DataTable Row Type");
    expect(rowType, JSON.stringify(dialog.combos)).toBeDefined();
    expect(rowType.value).toBe("");
    expect(rowType.optionsReadable).toBe(true);
    expect(rowType.options).toContain("GameplayTagTableRow");

    // The details panel's fields, labelled by the property name beside each.
    const ignoreExtra = itemLabelled(dialog, "Ignore Extra Fields");
    expect(ignoreExtra, JSON.stringify(dialog.items)).toBeDefined();
    expect(ignoreExtra.checked).toBe(false);
    expect(ignoreExtra.label).toBe("Ignore Extra Fields");
    expect(itemLabelled(dialog, "Ignore Missing Fields")).toBeDefined();
    const keyField = fieldLabelled(dialog, "Import Key Field");
    expect(keyField, JSON.stringify(dialog.textFields)).toBeDefined();
    expect(keyField.readOnly).toBe(false);

    // Apply waits for a row type, and a press is refused while it does.
    expect(choice(dialog, "Apply").enabled).toBe(false);
    expect(choice(dialog, "Cancel").enabled).toBe(true);
    expect(dialog.buttons).not.toContain("DataTable");
    const refused = await raw("respond_to_dialog", { buttonLabel: "Apply" });
    expect(refused.success).toBe(false);
    expect(String(refused.error)).toContain("disabled");
    expect((await waitForDialog(true)).title).toBe(TITLE);

    // An SComboBox, set alone: CurveTable hides the row type picker, and
    // DataTable brings it back.
    const toCurve = await raw("respond_to_dialog", { combos: [{ index: importAs.index, option: "CurveTable" }] });
    expect(toCurve.success, JSON.stringify(toCurve)).not.toBe(false);
    expect(toCurve.pressed).toBe(false);
    expect((toCurve.combosApplied as any[])[0]).toMatchObject({ option: "CurveTable", value: "CurveTable", committed: true });
    dialog = await waitForDialog(true);
    expect(comboLabelled(dialog, "Import As").value).toBe("CurveTable");
    expect(comboLabelled(dialog, "Choose DataTable Row Type")).toBeUndefined();
    const back = await raw("respond_to_dialog", { combos: [{ index: comboLabelled(dialog, "Import As").index, option: "DataTable" }] });
    expect(back.success, JSON.stringify(back)).not.toBe(false);
    dialog = await waitForDialog(true);
    expect(comboLabelled(dialog, "Choose DataTable Row Type")).toBeDefined();

    // The row type, the checkbox and the key field, then Apply, in one call.
    const answered = await raw("respond_to_dialog", {
      combos: [{ index: comboLabelled(dialog, "Choose DataTable Row Type").index, option: "GameplayTagTableRow" }],
      textFields: [{ index: fieldLabelled(dialog, "Import Key Field").index, text: "Key" }],
      items: [{ index: itemLabelled(dialog, "Ignore Extra Fields").index, checked: true }],
      buttonLabel: "Apply",
    });
    expect(answered.success, JSON.stringify(answered)).not.toBe(false);
    expect(answered.clickedButton).toBe("Apply");
    expect((answered.combosApplied as any[])[0]).toMatchObject({ value: "GameplayTagTableRow", committed: true });
    expect((answered.textFieldsApplied as any[])[0]).toMatchObject({ text: "Key" });

    const imported = (await importing) as Json;
    expect(imported.success, JSON.stringify(imported)).not.toBe(false);
    expect(imported.importedCount).toBe(1);
    await waitForDialog(false);

    // What the dialog was told is what the table holds.
    const table = `${DIR}/${name}`;
    const read = await raw("read_datatable", { assetPath: table });
    expect(read.rowStruct).toBe("GameplayTagTableRow");
    const rows = read.rows as Array<Record<string, unknown>>;
    // The key field names the row column of the export.
    expect(rows.map((r) => r.Key ?? r.Name).sort()).toEqual(["RowA", "RowB"]);
    expect(JSON.stringify(rows)).toContain("Test.DialogFields.B");
    const prop = async (propertyName: string) =>
      (await raw("get_asset_properties", { assetPath: table, propertyName, valueFormat: "json" })).value;
    expect(await prop("bIgnoreExtraFields")).toBe(true);
    expect(await prop("ImportKeyField")).toBe("Key");
  }, 240_000);
});
