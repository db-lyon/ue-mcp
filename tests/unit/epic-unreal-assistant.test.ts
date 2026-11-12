/**
 * Attaching ue-mcp to Epic's Unreal Assistant through `mcp_servers` in
 * AIAssistant.json (#1825): which file the engine reads, merging without
 * clobbering, an idempotent repeat, and a removal that hands the list back
 * to the engine's own default.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  assistantSearchPaths,
  attachUnrealAssistant,
  detachUnrealAssistant,
  editorServerDescriptor,
  readUnrealAssistant,
  ueMcpAssistantDescriptor,
  userSettingsDir,
} from "../../src/integrations/epic/unreal-assistant.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
let localAppData: string;
beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-epic-assistant-");
  localAppData = path.join(fixture.root, "LocalAppData");
});
afterEach(() => fixture.cleanup());

function engine(name: string, opts: { installed: boolean; assistant?: boolean }): string {
  const root = path.join(fixture.root, name);
  fs.mkdirSync(path.join(root, "Engine", "Build"), { recursive: true });
  if (opts.installed) fs.writeFileSync(path.join(root, "Engine", "Build", "InstalledBuild.txt"), "");
  if (opts.assistant !== false) {
    const plugin = path.join(root, "Engine", "Plugins", "Experimental", "AIAssistant");
    fs.mkdirSync(plugin, { recursive: true });
    fs.writeFileSync(path.join(plugin, "AIAssistant.uplugin"), "{}");
  }
  return root;
}

const env = () => ({ LOCALAPPDATA: localAppData, USERPROFILE: fixture.root });
const inputs = (uproject: string, engineVersion: string | null, engineRoot: string | null) => ({
  uprojectPath: uproject, engineVersion, engineRoot, env: env(), platform: "win32" as const,
});
const common = () => path.join(localAppData, "UnrealEngine", "Common", "AIAssistant.json");
const read = (f: string) => JSON.parse(fs.readFileSync(f, "utf8"));

describe("where the assistant's config lives", () => {
  it("searches the engine's files first, then the version and Common user folders on an installed engine", () => {
    const root = engine("Installed", { installed: true });
    const { engine: engineFiles, user } = assistantSearchPaths(inputs("x.uproject", "5.9", root));
    expect(engineFiles).toEqual([
      path.join(root, "Engine", "Restricted", "NotForLicensees", "Config", "AIAssistant.json"),
      path.join(root, "Engine", "Restricted", "NoRedist", "Config", "AIAssistant.json"),
      path.join(root, "Engine", "Restricted", "LimitedAccess", "Config", "AIAssistant.json"),
      path.join(root, "Engine", "Config", "AIAssistant.json"),
    ]);
    expect(user).toEqual([path.join(localAppData, "UnrealEngine", "5.9", "AIAssistant.json"), common()]);
  });

  it("uses Engine/ itself as the user folder on a source build", () => {
    const root = engine("Source", { installed: false });
    expect(assistantSearchPaths(inputs("x.uproject", "5.9", root)).user).toEqual([path.join(root, "Engine", "AIAssistant.json")]);
  });

  it("reads UserSettingsDir per platform", () => {
    expect(userSettingsDir({ LOCALAPPDATA: "L" }, "win32")).toBe("L");
    expect(userSettingsDir({ HOME: "/h" }, "darwin")).toBe(path.join("/h", "Library", "Application Support"));
    expect(userSettingsDir({ HOME: "/h" }, "linux")).toBe(path.join("/h", ".config"));
  });
});

describe("readUnrealAssistant", () => {
  it("is unavailable before 5.9, without the plugin, or when the .uproject disables it", () => {
    const uproject = fixture.makeProject("Game", { engine: "5.9" });
    expect(readUnrealAssistant(inputs(uproject, "5.8", null))).toMatchObject({ available: false, reason: expect.stringContaining("5.9") });
    expect(readUnrealAssistant(inputs(uproject, "5.9", engine("NoPlugin", { installed: true, assistant: false })))).toMatchObject({ available: false });
    const d = JSON.parse(fs.readFileSync(uproject, "utf8"));
    d.Plugins = [{ Name: "AIAssistant", Enabled: false }];
    fs.writeFileSync(uproject, JSON.stringify(d));
    expect(readUnrealAssistant(inputs(uproject, "5.9", engine("WithPlugin", { installed: true })))).toMatchObject({ available: false, reason: expect.stringContaining("disables") });
  });

  it("targets Common when no file exists, the version folder's file when it does, and names an engine file that shadows both", () => {
    const uproject = fixture.makeProject("Game2", { engine: "5.9" });
    const root = engine("Inst", { installed: true });
    expect(readUnrealAssistant(inputs(uproject, "5.9", root))).toMatchObject({ available: true, configPath: common(), shadowedBy: null, attached: false });

    const versioned = path.join(localAppData, "UnrealEngine", "5.9", "AIAssistant.json");
    fs.mkdirSync(path.dirname(versioned), { recursive: true });
    fs.writeFileSync(versioned, "{}");
    expect(readUnrealAssistant(inputs(uproject, "5.9", root)).configPath).toBe(versioned);

    const shadow = path.join(root, "Engine", "Config", "AIAssistant.json");
    fs.mkdirSync(path.dirname(shadow), { recursive: true });
    fs.writeFileSync(shadow, "{}");
    expect(readUnrealAssistant(inputs(uproject, "5.9", root)).shadowedBy).toBe(shadow);
  });
});

describe("attach and detach", () => {
  const uproject = "C:/Games/Hero/Hero.uproject";
  const ue = () => ueMcpAssistantDescriptor(uproject, { pin: "1.2.3" });
  const editor = editorServerDescriptor("http://127.0.0.1:8000/mcp");

  it("writes ue-mcp beside the editor's own server into a new file, and a repeat changes nothing", () => {
    const file = common();
    expect(attachUnrealAssistant(file, ue(), editor)).toEqual({ changed: true, backup: null });
    expect(read(file)).toEqual({
      mcp_servers: [
        { name: "unreal-mcp", type: "http", url: "http://127.0.0.1:8000/mcp", headers: [] },
        { name: "ue-mcp", command: "npx", args: ["-y", "--min-release-age=0", "ue-mcp@1.2.3", uproject], env: [] },
      ],
    });
    expect(attachUnrealAssistant(file, ue(), editor)).toEqual({ changed: false, backup: null });
  });

  it("keeps every other key and server in an existing list, saves the file first, and replaces an older ue-mcp entry", () => {
    const file = common();
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const original = JSON.stringify({
      main_url: "https://example.invalid/",
      agent: { command: "claude-agent-acp", arguments: [] },
      mcp_servers: [{ name: "mine", type: "http", url: "http://127.0.0.1:9/mcp", headers: [] }, { name: "ue-mcp", command: "old", args: [], env: [] }],
    });
    fs.writeFileSync(file, original);
    const r = attachUnrealAssistant(file, ue(), editor);
    expect(r).toEqual({ changed: true, backup: `${file}.ue-mcp-backup` });
    expect(fs.readFileSync(r.backup!, "utf8")).toBe(original);
    const after = read(file);
    expect(after.main_url).toBe("https://example.invalid/");
    expect(after.agent).toEqual({ command: "claude-agent-acp", arguments: [] });
    // An existing list is the user's choice, so the editor's server is not added to it.
    expect(after.mcp_servers.map((s: { name: string }) => s.name)).toEqual(["mine", "ue-mcp"]);
    expect(after.mcp_servers[1].command).toBe("npx");
  });

  it("refuses a file the engine would refuse, rather than overwrite it", () => {
    const file = common();
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, "{ not json");
    expect(() => attachUnrealAssistant(file, ue(), editor)).toThrow(/not a JSON object/);
    fs.writeFileSync(file, JSON.stringify({ mcp_servers: {} }));
    expect(() => attachUnrealAssistant(file, ue(), editor)).toThrow(/not an array/);
  });

  it("detaches only ue-mcp, and gives the list back to the engine when only its default is left", () => {
    const file = common();
    attachUnrealAssistant(file, ue(), editor);
    expect(detachUnrealAssistant(file)).toMatchObject({ changed: true });
    // Nothing left but the stand-in for the default, so the file goes and the engine picks its default.
    expect(fs.existsSync(file)).toBe(false);
    expect(detachUnrealAssistant(file)).toEqual({ changed: false, backup: null });

    fs.writeFileSync(file, JSON.stringify({ main_url: "u", mcp_servers: [{ name: "mine" }, { name: "ue-mcp" }] }));
    detachUnrealAssistant(file);
    expect(read(file)).toEqual({ main_url: "u", mcp_servers: [{ name: "mine" }] });
    fs.writeFileSync(file, JSON.stringify({ main_url: "u", mcp_servers: [{ name: "ue-mcp" }] }));
    detachUnrealAssistant(file);
    expect(read(file)).toEqual({ main_url: "u" });
  });

  it("reports what is attached and which project it serves", () => {
    const game = fixture.makeProject("Attached", { engine: "5.9" });
    const root = engine("InstA", { installed: true });
    attachUnrealAssistant(common(), ueMcpAssistantDescriptor(game), null);
    const s = readUnrealAssistant(inputs(game, "5.9", root));
    expect(s).toMatchObject({ attached: true, invalid: false });
    expect(path.resolve(s.attachedProject!)).toBe(path.resolve(game));
  });
});
