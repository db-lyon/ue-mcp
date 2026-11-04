/**
 * The sharded live run leaves the developer's editor layout as it found it:
 * tabs its editors opened (Project Settings from open_settings, the Content
 * Browser an Interchange import summons) are closed again in the shared
 * per-user layout files, and nothing else in them changes.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { closeTabsOpenedSince, guardUserLayouts, openTabs, userLayoutFiles } from "../../scripts/user-layout-guard.mjs";

const tab = (id: string, state: string) => ({ TabId: id, TabState: state });
const stack = (fg: string, tabs: object[]) => ({ SizeCoefficient: 1, Type: "Stack", HideTabWell: false, ForegroundTab: fg, Tabs: tabs });
const floating = (tabs: object[]) => ({
  SizeCoefficient: 1, Type: "Area", Orientation: "Orient_Horizontal", WindowPlacement: "Placement_Specified",
  WindowPosition_X: 16, WindowPosition_Y: 16, WindowSize_X: 2850, WindowSize_Y: 1514, bIsMaximized: false, Nodes: [stack("None", tabs)],
});
const unrealEd = (settings: string, browser: string) => ({
  Type: "Layout", Name: "UnrealEd_Layout_v1.6", PrimaryAreaIndex: 0,
  Areas: [
    { SizeCoefficient: 1, Type: "Area", Orientation: "Orient_Horizontal", WindowPlacement: "Placement_NoWindow",
      Nodes: [stack("LevelEditor", [tab("HomeScreen", "OpenedTab"), tab("LevelEditor", "OpenedTab")])] },
    floating([tab("EditorSettings", "ClosedTab"), tab("ProjectSettings", settings), tab("PluginsEditor", "ClosedTab")]),
    floating([tab("ContentBrowserTab1", browser)]),
  ],
});
const json = (layouts: Record<string, object>) => `﻿${JSON.stringify(layouts, null, "\t").replace(/\n/g, "\r\n")}`;
// The ini form: one layout per line, objects in parentheses, no spaces after commas.
const iniLine = (name: string, layout: object) =>
  `${name}=${JSON.stringify(layout).replace(/\{/g, "(").replace(/\}/g, ")").replace(/":/g, "\": ")}`;
const ini = (lines: string[]) => [";METADATA=(Diff=true, UseCommands=true)", "[EditorLayouts]", ...lines, ""].join("\r\n");

let dir: string | null = null;
afterEach(() => {
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
  dir = null;
});

describe("reading open tabs", () => {
  it("reads the open tabs of each layout in both file forms", () => {
    const layout = unrealEd("OpenedTab", "ClosedTab");
    const fromJson = openTabs("EditorLayout.json", json({ "UnrealEd_Layout_v1.6": layout }));
    const fromIni = openTabs("EditorLayout.ini", ini([iniLine("UnrealEd_Layout_v1.6", layout)]));
    for (const read of [fromJson, fromIni]) {
      expect([...(read?.get("UnrealEd_Layout_v1.6") ?? [])]).toEqual(["HomeScreen", "LevelEditor", "ProjectSettings"]);
    }
  });
});

describe("closing what the run opened", () => {
  it("closes Project Settings and the Content Browser a test editor saved as open, in the JSON every engine shares", () => {
    const before = json({ "UnrealEd_Layout_v1.6": unrealEd("ClosedTab", "ClosedTab") });
    const after = json({ "UnrealEd_Layout_v1.6": unrealEd("OpenedTab", "OpenedTab") });
    const { text, closed } = closeTabsOpenedSince("EditorLayout.json", before, after);
    expect(closed).toEqual(["UnrealEd_Layout_v1.6:ProjectSettings", "UnrealEd_Layout_v1.6:ContentBrowserTab1"]);
    expect(text).toBe(before);
  });

  it("closes them in the per-engine ini and leaves every other line as written", () => {
    const other = 'Standalone_TextureEditor_Layout_v7=("Type": "Layout","Name": "x","Areas": [])';
    const before = ini([iniLine("UnrealEd_Layout_v1.6", unrealEd("ClosedTab", "ClosedTab")), other]);
    const after = ini([iniLine("UnrealEd_Layout_v1.6", unrealEd("OpenedTab", "OpenedTab")), other]);
    const { text, closed } = closeTabsOpenedSince("EditorLayout.ini", before, after);
    expect(closed).toHaveLength(2);
    expect(text).toBe(before);
  });

  it("keeps a tab that was already open before the run", () => {
    const before = json({ "UnrealEd_Layout_v1.6": unrealEd("OpenedTab", "ClosedTab") });
    const after = json({ "UnrealEd_Layout_v1.6": unrealEd("OpenedTab", "OpenedTab") });
    const { text, closed } = closeTabsOpenedSince("EditorLayout.json", before, after);
    expect(closed).toEqual(["UnrealEd_Layout_v1.6:ContentBrowserTab1"]);
    expect(openTabs("EditorLayout.json", text)?.get("UnrealEd_Layout_v1.6")?.has("ProjectSettings")).toBe(true);
  });

  it("leaves a layout the snapshot did not have, and a file it cannot parse", () => {
    const before = json({});
    const after = json({ "UnrealEd_Layout_v1.6": unrealEd("OpenedTab", "OpenedTab") });
    expect(closeTabsOpenedSince("EditorLayout.json", before, after)).toEqual({ text: after, closed: [] });
    expect(closeTabsOpenedSince("EditorLayout.json", "{ not json", after)).toEqual({ text: after, closed: [] });
  });
});

describe("the guard around a run", () => {
  it("finds the shared JSON and each engine's ini, then closes on release only what the run opened", () => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "layout-guard-"));
    const jsonFile = path.join(dir, "Editor", "EditorLayout.json");
    const iniFile = path.join(dir, "5.8", "Saved", "Config", "WindowsEditor", "EditorLayout.ini");
    fs.mkdirSync(path.dirname(jsonFile), { recursive: true });
    fs.mkdirSync(path.dirname(iniFile), { recursive: true });
    fs.mkdirSync(path.join(dir, "Common"));
    const closedJson = json({ "UnrealEd_Layout_v1.6": unrealEd("ClosedTab", "ClosedTab") });
    const closedIni = ini([iniLine("UnrealEd_Layout_v1.6", unrealEd("ClosedTab", "ClosedTab"))]);
    fs.writeFileSync(jsonFile, closedJson);
    fs.writeFileSync(iniFile, closedIni);

    const files = userLayoutFiles(dir);
    expect(files.sort()).toEqual([iniFile, jsonFile].sort());
    const guard = guardUserLayouts(files);
    // What r1 left behind on 2026-11-04: both tabs saved open by a test editor.
    fs.writeFileSync(jsonFile, json({ "UnrealEd_Layout_v1.6": unrealEd("OpenedTab", "OpenedTab") }));
    const report = guard.release();

    expect(report).toEqual([
      `${jsonFile}: UnrealEd_Layout_v1.6:ProjectSettings`,
      `${jsonFile}: UnrealEd_Layout_v1.6:ContentBrowserTab1`,
    ]);
    expect(fs.readFileSync(jsonFile, "utf8")).toBe(closedJson);
    expect(fs.readFileSync(iniFile, "utf8")).toBe(closedIni);
  });

  it("finds nothing when the folder does not exist", () => {
    expect(userLayoutFiles(path.join(os.tmpdir(), "no-such-unreal-engine-dir"))).toEqual([]);
  });
});
