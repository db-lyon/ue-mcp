// @live-editor headless
/**
 * Localization end to end: string table entry metadata, a Text property's
 * namespace and key (and a string table reference), the project's cultures, a
 * localization target created, gathered and compiled, and removed with its
 * files. The gather and compile run the GatherText commandlet in a process of
 * their own, so the manifest and LocRes they write are read back off disk.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";

const DIR = "/Game/MCPTest/Loc";
const ST = `${DIR}/ST_MCPTestLoc`;
const WBP = `${DIR}/WBP_MCPTestLoc`;
const TARGET = "MCPTestLoc";

type Json = Record<string, unknown>;
type FileEntry = { path: string; exists: boolean; culture?: string };
type TargetState = {
  target: string;
  cultures: string[];
  nativeCulture: string;
  gatherFromPackages: boolean;
  includePaths: string[];
  files: { manifest: FileEntry; archives: FileEntry[]; locres: FileEntry[] };
};

const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

/** The engine writes its manifests as UTF-16 with a byte order mark. */
function readEngineText(file: string): string {
  const bytes = fs.readFileSync(file);
  if (bytes[0] === 0xff && bytes[1] === 0xfe) return bytes.subarray(2).toString("utf16le");
  if (bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) return bytes.subarray(3).toString("utf8");
  return bytes.toString("utf8");
}

// Adding and removing a target saves ULocalizationSettings into the tracked
// DefaultEditor.ini; the file is put back byte for byte afterwards.
const EDITOR_INI = path.join(TEST_PROJECT_DIR, "Config", "DefaultEditor.ini");
let editorIniBefore: string | null = null;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function entryMetadata(key: string): Promise<Record<string, string>> {
  return (await call("get_stringtable_entry", { assetPath: ST, key })).metadata as Record<string, string>;
}

async function titleText(): Promise<Json> {
  return call("get_text_localization", { assetPath: WBP, widgetName: "Title", propertyName: "Text" });
}

async function target(): Promise<TargetState | undefined> {
  const listed = await call("list_localization_targets", {});
  return (listed.targets as TargetState[]).find((t) => t.target === TARGET);
}

beforeAll(async () => {
  editorIniBefore = fs.existsSync(EDITOR_INI) ? fs.readFileSync(EDITOR_INI, "utf8") : null;
  bridge = await getBridge();
  await callBridge(bridge, "remove_localization_target", { target: TARGET, deleteFiles: true });
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
  await call("create_stringtable", { name: "ST_MCPTestLoc", packagePath: DIR, namespace: "MCPTestLoc" });
  await call("set_stringtable_entry", { assetPath: ST, key: "Greeting", sourceString: "Hello from the MCP localization test" });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Title" });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "remove_localization_target", { target: TARGET, deleteFiles: true });
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
    disconnectBridge();
  }
  if (editorIniBefore === null) fs.rmSync(EDITOR_INI, { force: true });
  else if (fs.readFileSync(EDITOR_INI, "utf8") !== editorIniBefore) fs.writeFileSync(EDITOR_INI, editorIniBefore);
});

describe("localization", () => {
  it("sets, replays, changes and removes string table entry metadata", async () => {
    expect(await entryMetadata("Greeting")).toEqual({});

    const set = await call("set_stringtable_entry_metadata", { assetPath: ST, key: "Greeting", metaDataId: "Comment", metaDataValue: "Shown on boot" });
    expect(set.created).toBe(true);
    expect((set.rollback as Json).method).toBe("remove_stringtable_entry_metadata");
    expect(await entryMetadata("Greeting")).toEqual({ Comment: "Shown on boot" });

    const replay = await call("set_stringtable_entry_metadata", { assetPath: ST, key: "Greeting", metaDataId: "Comment", metaDataValue: "Shown on boot" });
    expect(replay.unchanged).toBe(true);

    const changed = await call("set_stringtable_entry_metadata", { assetPath: ST, key: "Greeting", metaDataId: "Comment", metaDataValue: "Shown on the title screen" });
    expect(changed.updated).toBe(true);
    expect(changed.previousMetaDataValue).toBe("Shown on boot");

    const read = await call("read_stringtable", { assetPath: ST });
    const row = (read.entries as Array<{ key: string; metadata: Record<string, string> }>).find((e) => e.key === "Greeting");
    expect(row?.metadata).toEqual({ Comment: "Shown on the title screen" });

    const removed = await call("remove_stringtable_entry_metadata", { assetPath: ST, key: "Greeting", metaDataId: "Comment" });
    expect(removed.deleted).toBe(true);
    expect(await entryMetadata("Greeting")).toEqual({});
    const again = await call("remove_stringtable_entry_metadata", { assetPath: ST, key: "Greeting", metaDataId: "Comment" });
    expect(again.alreadyRemoved).toBe(true);

    const rollback = removed.rollback as { method: string; payload: Json };
    await call(rollback.method, rollback.payload);
    expect(await entryMetadata("Greeting")).toEqual({ Comment: "Shown on the title screen" });
  }, 60_000);

  it("sets a Text property's namespace and key, then points it at a string table entry, and rolls back", async () => {
    const keyed = await call("set_text_localization", {
      assetPath: WBP,
      widgetName: "Title",
      propertyName: "Text",
      namespace: "MCPTestLoc",
      key: "TitleKey",
      sourceString: "Main Menu",
    });
    expect(keyed.updated).toBe(true);
    let read = await titleText();
    expect(read).toMatchObject({ namespace: "MCPTestLoc", key: "TitleKey", sourceString: "Main Menu", isFromStringTable: false, isLocalizable: true });

    const replay = await call("set_text_localization", { assetPath: WBP, widgetName: "Title", propertyName: "Text", key: "TitleKey" });
    expect(replay.unchanged).toBe(true);

    const tabled = await call("set_text_localization", {
      assetPath: WBP,
      widgetName: "Title",
      propertyName: "Text",
      stringTableId: ST,
      stringTableKey: "Greeting",
    });
    expect(tabled.updated).toBe(true);
    read = await titleText();
    expect(read.isFromStringTable).toBe(true);
    expect(read.stringTableKey).toBe("Greeting");
    expect(read.text).toBe("Hello from the MCP localization test");

    const rollback = tabled.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("set_text_localization");
    await call(rollback.method, rollback.payload);
    read = await titleText();
    expect(read).toMatchObject({ namespace: "MCPTestLoc", key: "TitleKey", sourceString: "Main Menu", isFromStringTable: false });
  }, 120_000);

  it("creates a localization target, replays it unchanged, and reports its cultures", async () => {
    expect(await target()).toBeUndefined();
    const created = await call("set_localization_target", {
      target: TARGET,
      cultures: ["en", "fr"],
      nativeCulture: "en",
      gatherFromPackages: true,
      includePaths: ["Content/MCPTest/Loc/*"],
    });
    expect(created.created).toBe(true);
    expect((created.rollback as Json).method).toBe("remove_localization_target");

    const state = await target();
    expect(state).toMatchObject({ cultures: ["en", "fr"], nativeCulture: "en", gatherFromPackages: true, includePaths: ["Content/MCPTest/Loc/*"] });

    const replay = await call("set_localization_target", { target: TARGET, cultures: ["en", "fr"], nativeCulture: "en" });
    expect(replay.unchanged).toBe(true);

    const cultures = await call("list_localization_cultures", {});
    expect(typeof cultures.currentCulture).toBe("string");
    const row = (cultures.targets as Array<{ target: string; nativeCulture: string; cultures: string[] }>).find((t) => t.target === TARGET);
    expect(row).toMatchObject({ nativeCulture: "en", cultures: ["en", "fr"] });
    expect(cultures.targetCultures as string[]).toEqual(expect.arrayContaining(["en", "fr"]));
  }, 60_000);

  it("gathers the target into a manifest and compiles it into LocRes per culture", async () => {
    const gathered = await call("gather_localization_target", { target: TARGET });
    expect(gathered.exitCode).toBe(0);
    const afterGather = gathered.targetState as TargetState;
    expect(afterGather.files.manifest.exists).toBe(true);
    const manifest = readEngineText(afterGather.files.manifest.path);
    expect(manifest).toContain("Hello from the MCP localization test");
    expect(afterGather.files.archives.every((a) => a.exists)).toBe(true);

    const compiled = await call("compile_localization_target", { target: TARGET });
    expect(compiled.exitCode).toBe(0);
    const afterCompile = compiled.targetState as TargetState;
    expect(afterCompile.files.locres.map((l) => [l.culture, l.exists])).toEqual([["en", true], ["fr", true]]);
  }, 600_000);

  it("removes the target with its files, and the rollback restores its settings", async () => {
    const before = await target();
    const removed = await call("remove_localization_target", { target: TARGET, deleteFiles: true });
    expect(removed.deleted).toBe(true);
    expect(await target()).toBeUndefined();
    expect(fs.existsSync(before!.files.manifest.path)).toBe(false);

    const again = await call("remove_localization_target", { target: TARGET });
    expect(again.alreadyRemoved).toBe(true);

    const rollback = removed.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("set_localization_target");
    await call(rollback.method, rollback.payload);
    expect(await target()).toMatchObject({ cultures: ["en", "fr"], nativeCulture: "en", includePaths: ["Content/MCPTest/Loc/*"] });
  }, 60_000);
});
