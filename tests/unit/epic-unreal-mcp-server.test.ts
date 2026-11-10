/**
 * Epic's Unreal MCP server as a project has it, read from the .uproject and
 * the EditorPerProjectUserSettings layers alone (#1826), and the ini edit
 * init makes to turn its auto-start off or move its port.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  UNREAL_MCP_SECTION,
  UNREAL_MCP_SECTION_58,
  describeUnrealMcpServer,
  engineAtLeast,
  iniBool,
  readUnrealMcpServer,
  setIniValues,
  userIniPath,
  writeUnrealMcpServerSettings,
} from "../../src/integrations/epic/unreal-mcp-server.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-epic-mcp-");
});
afterEach(() => fixture.cleanup());

/** An engine tree holding the plugin and, optionally, a Base ini that sets auto-start. */
function engine(name: string, baseAutoStart: boolean | null): string {
  const root = path.join(fixture.root, name);
  const plugin = path.join(root, "Engine", "Plugins", "Experimental", "ModelContextProtocol");
  fs.mkdirSync(plugin, { recursive: true });
  fs.writeFileSync(path.join(plugin, "ModelContextProtocol.uplugin"), JSON.stringify({ EnabledByDefault: true }));
  fs.mkdirSync(path.join(root, "Engine", "Config"), { recursive: true });
  if (baseAutoStart !== null) {
    fs.writeFileSync(
      path.join(root, "Engine", "Config", "BaseEditorPerProjectUserSettings.ini"),
      `[Other]\nx=1\n\n[${UNREAL_MCP_SECTION}]\nbAutoStartServer=${baseAutoStart ? "True" : "False"}\n`,
    );
  }
  return root;
}

function inputs(uproject: string, engineVersion: string | null, engineRoot: string | null) {
  return { uprojectPath: uproject, projectDir: path.dirname(uproject), engineVersion, engineRoot, platform: "win32" as const };
}

function write(file: string, text: string): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, text);
}

describe("readUnrealMcpServer", () => {
  it("is null for an engine without the server", () => {
    const uproject = fixture.makeProject("Old");
    expect(readUnrealMcpServer(inputs(uproject, "5.6", null))).toBeNull();
    const bare = path.join(fixture.root, "Bare");
    fs.mkdirSync(path.join(bare, "Engine"), { recursive: true });
    expect(readUnrealMcpServer(inputs(uproject, "5.9", bare))).toBeNull();
    // A 5.6 project is not given a newer engine's server by a fallback engine root.
    expect(readUnrealMcpServer(inputs(uproject, "5.6", engine("Newer", true)))).toBeNull();
  });

  it("follows UE 5.9's Base ini: on by default, on 8000 at /mcp, beside ue-mcp", () => {
    const uproject = fixture.makeProject("Fresh", { engine: "5.9" });
    const s = readUnrealMcpServer(inputs(uproject, "5.9", engine("E59", true)), { bridge: 9877 })!;
    expect(s).toMatchObject({
      pluginEnabled: true,
      enabledBy: "default",
      autoStart: true,
      autoStartSource: "engine",
      port: 8000,
      portSource: "default",
      url: "http://127.0.0.1:8000/mcp",
      section: UNREAL_MCP_SECTION,
      runsBeside: true,
      portConflicts: [],
    });
    expect(s.userIni).toBe(path.join(path.dirname(uproject), "Saved", "Config", "WindowsEditor", "EditorPerProjectUserSettings.ini"));
    expect(describeUnrealMcpServer(s)).toContain("starts with the editor on http://127.0.0.1:8000/mcp");
  });

  it("assumes 5.9's default when there is no engine tree to read", () => {
    const uproject = fixture.makeProject("NoTree", { engine: "5.9" });
    expect(readUnrealMcpServer(inputs(uproject, "5.9", null))).toMatchObject({ autoStart: true, autoStartSource: "engine" });
    expect(readUnrealMcpServer(inputs(uproject, "5.8", null))).toMatchObject({ autoStart: false, autoStartSource: "default", section: UNREAL_MCP_SECTION_58 });
  });

  it("lets the project's Config and then the user ini win over the engine", () => {
    const uproject = fixture.makeProject("Layered", { engine: "5.9" });
    const dir = path.dirname(uproject);
    const root = engine("E59b", true);
    write(path.join(dir, "Config", "DefaultEditorPerProjectUserSettings.ini"), `[${UNREAL_MCP_SECTION}]\nbAutoStartServer=False\nServerPortNumber=8100\n`);
    expect(readUnrealMcpServer(inputs(uproject, "5.9", root))).toMatchObject({ autoStart: false, autoStartSource: "project", port: 8100, portSource: "project", runsBeside: false });

    // The 5.8 section name is redirected, so a user file still written with it counts.
    write(userIniPath(dir, "win32"), `﻿[${UNREAL_MCP_SECTION_58}]\nbAutoStartServer=1\nServerPortNumber=8200\nServerUrlPath=mcp2\n`);
    expect(readUnrealMcpServer(inputs(uproject, "5.9", root))).toMatchObject({ autoStart: true, autoStartSource: "user", port: 8200, portSource: "user", url: "http://127.0.0.1:8200/mcp2" });
  });

  it("reports the plugin turned off in the .uproject, and port collisions", () => {
    const uproject = fixture.makeProject("Off", { engine: "5.9" });
    const root = engine("E59c", true);
    const descriptor = JSON.parse(fs.readFileSync(uproject, "utf8"));
    descriptor.Plugins = [{ Name: "ModelContextProtocol", Enabled: false }];
    fs.writeFileSync(uproject, JSON.stringify(descriptor));
    const off = readUnrealMcpServer(inputs(uproject, "5.9", root), { bridge: 8000 })!;
    expect(off).toMatchObject({ pluginEnabled: false, enabledBy: "uproject", runsBeside: false, portConflicts: [] });
    expect(describeUnrealMcpServer(off)).toBe("plugin disabled in the .uproject");

    descriptor.Plugins = [{ Name: "ModelContextProtocol", Enabled: true }];
    fs.writeFileSync(uproject, JSON.stringify(descriptor));
    expect(readUnrealMcpServer(inputs(uproject, "5.9", root), { bridge: 8000, flow: 8000 })!.portConflicts).toEqual([
      "ue-mcp's bridge port for this project is also 8000",
      "ue-mcp's flow HTTP port is also 8000",
    ]);
  });
});

describe("writing the settings", () => {
  it("adds the section to a missing file, then edits it in place and changes nothing on a repeat", () => {
    const uproject = fixture.makeProject("Write", { engine: "5.9" });
    const s = readUnrealMcpServer(inputs(uproject, "5.9", engine("E59d", true)))!;
    expect(writeUnrealMcpServerSettings(s, { autoStart: false })).toBe(true);
    expect(fs.readFileSync(s.userIni, "utf8")).toBe(`[${UNREAL_MCP_SECTION}]\nbAutoStartServer=False\n`);
    expect(writeUnrealMcpServerSettings(s, { autoStart: false })).toBe(false);
    expect(readUnrealMcpServer(inputs(uproject, "5.9", engine("E59e", true)))).toMatchObject({ autoStart: false, autoStartSource: "user", runsBeside: false });

    expect(writeUnrealMcpServerSettings(s, { port: 8123 })).toBe(true);
    expect(fs.readFileSync(s.userIni, "utf8")).toBe(`[${UNREAL_MCP_SECTION}]\nbAutoStartServer=False\nServerPortNumber=8123\n`);
    expect(writeUnrealMcpServerSettings(s, {})).toBe(false);
  });

  it("keeps every other section, line ending and byte order mark", () => {
    const file = path.join(fixture.root, "Saved.ini");
    const before = `﻿[/Script/UnrealEd.EditorPerProjectUserSettings]\r\nbSomething=True\r\n\r\n[${UNREAL_MCP_SECTION}]\r\nbautostartserver=True\r\nServerUrlPath=/mcp\r\n\r\n[Last]\r\nk=v\r\n`;
    fs.writeFileSync(file, before);
    expect(setIniValues(file, UNREAL_MCP_SECTION, { bAutoStartServer: "False", ServerPortNumber: "8001" })).toBe(true);
    expect(fs.readFileSync(file, "utf8")).toBe(
      `﻿[/Script/UnrealEd.EditorPerProjectUserSettings]\r\nbSomething=True\r\n\r\n[${UNREAL_MCP_SECTION}]\r\nbAutoStartServer=False\r\nServerUrlPath=/mcp\r\nServerPortNumber=8001\r\n\r\n[Last]\r\nk=v\r\n`,
    );
  });

  it("appends a section after existing content", () => {
    const file = path.join(fixture.root, "Other.ini");
    fs.writeFileSync(file, "[A]\nx=1");
    setIniValues(file, "B", { y: "2" });
    expect(fs.readFileSync(file, "utf8")).toBe("[A]\nx=1\n\n[B]\ny=2\n");
  });
});

describe("helpers", () => {
  it("parses UE bools and compares engine versions", () => {
    expect([iniBool("True"), iniBool("yes"), iniBool("1"), iniBool("False"), iniBool("0"), iniBool(undefined)]).toEqual([true, true, true, false, false, undefined]);
    expect([engineAtLeast("5.9", 5, 9), engineAtLeast("5.10", 5, 9), engineAtLeast("5.8", 5, 9), engineAtLeast(null, 5, 9), engineAtLeast("6.0", 5, 9)]).toEqual([true, true, false, null, true]);
  });
});
