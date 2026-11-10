/**
 * The config hierarchy: one category read through every layer the engine
 * reads, Base.ini up to the per-user Saved copy, with Unreal's array
 * operations. Fixture engine and project trees, no editor.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import {
  applyOp,
  configCategory,
  configLayers,
  parseConfigLines,
} from "../../src/codeintel/config-hierarchy.js";
import { configActions } from "../../src/tools/project/config.js";
import type { ToolContext } from "../../src/core/types.js";

const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ini-"));
const engine = path.join(root, "UE");
const project = path.join(root, "Game");
afterAll(() => fs.rmSync(root, { recursive: true, force: true }));

function write(file: string, text: string): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, text);
}

beforeAll(() => {
  write(path.join(engine, "Engine", "Config", "Base.ini"), "[Core.System]\nPaths=../../../Engine/Content\n");
  write(path.join(engine, "Engine", "Config", "BaseEngine.ini"), [
    "; engine defaults",
    "[/Script/Engine.RendererSettings]",
    "r.DefaultFeature.Bloom=True",
    "r.Shadow.Virtual.Enable=0",
    "",
    "[/Script/Engine.Engine]",
    "+ActiveGameNameRedirects=(OldGameName=\"A\",NewGameName=\"B\")",
    "+ActiveGameNameRedirects=(OldGameName=\"C\",NewGameName=\"D\")",
    "GameViewportClientClassName=/Script/Engine.GameViewportClient",
    "",
  ].join("\n"));
  write(path.join(engine, "Engine", "Config", "Windows", "BaseWindowsEngine.ini"), "[/Script/Engine.RendererSettings]\nr.Shadow.Virtual.Enable=1\n");
  write(path.join(project, "Config", "DefaultEngine.ini"), [
    "[/script/engine.renderersettings]",
    "r.DefaultFeature.Bloom=False",
    "",
    "[/Script/Engine.Engine]",
    "-ActiveGameNameRedirects=(OldGameName=\"A\",NewGameName=\"B\")",
    ".ActiveGameNameRedirects=(OldGameName=\"C\",NewGameName=\"D\")",
    "+ActiveGameNameRedirects=(OldGameName=\"C\",NewGameName=\"D\")",
    "",
    "[/Script/Game.MySettings]",
    "Speed=600",
    "",
  ].join("\r\n"));
  write(path.join(project, "Config", "Windows", "WindowsEngine.ini"), "[/Script/Engine.Engine]\n!ActiveGameNameRedirects=ClearArray\n");
  write(path.join(project, "Saved", "Config", "WindowsEditor", "Engine.ini"), "[/Script/Game.MySettings]\nSpeed=900\n");
});

const ctx = (engineRoot: string | null = engine): ToolContext => ({
  project: {
    ensureLoaded: () => {},
    projectDir: project,
    resolveEngineRoot: () => engineRoot,
  },
} as unknown as ToolContext);

const run = async (action: string, params: Record<string, unknown>, c = ctx()) => {
  const spec = configActions[action];
  if (spec.kind !== "handler") throw new Error(`${action} is not a local action`);
  return spec.handler(c, params) as Promise<Record<string, any>>;
};

describe("parsing and operations", () => {
  it("reads each line's operation and skips comments", () => {
    expect(parseConfigLines("; c\n# c\n[S]\nA=1\n+A=2\n.A=2\n-A=1\n!A\n!B=x\n")).toEqual([
      { section: "S", key: "A", op: "set", value: "1", line: 4 },
      { section: "S", key: "A", op: "add", value: "2", line: 5 },
      { section: "S", key: "A", op: "addDuplicate", value: "2", line: 6 },
      { section: "S", key: "A", op: "remove", value: "1", line: 7 },
      { section: "S", key: "A", op: "clear", value: "", line: 8 },
      { section: "S", key: "B", op: "clear", value: "x", line: 9 },
    ]);
  });

  it("applies set, unique add, duplicate add, remove and clear", () => {
    let v: string[] = [];
    v = applyOp(v, { op: "add", value: "a" });
    v = applyOp(v, { op: "add", value: "a" });
    expect(v).toEqual(["a"]);
    v = applyOp(v, { op: "addDuplicate", value: "a" });
    expect(v).toEqual(["a", "a"]);
    v = applyOp(v, { op: "remove", value: "a" });
    expect(v).toEqual([]);
    expect(applyOp(["x", "y"], { op: "set", value: "z" })).toEqual(["z"]);
    expect(applyOp(["x"], { op: "clear", value: "" })).toEqual([]);
  });

  it("normalises the category name and refuses a path", () => {
    expect(configCategory("Engine")).toBe("Engine");
    expect(configCategory("DefaultGame.ini")).toBe("Game");
    expect(configCategory("BaseInput")).toBe("Input");
    expect(() => configCategory("../Engine")).toThrow(/not a config category/);
  });

  it("orders the layers engine, project, user", () => {
    const layers = configLayers({ engineRoot: engine, projectDir: project, category: "Engine", platform: "Windows" });
    expect(layers.map((l) => l.name)).toEqual([
      "EngineBase", "EngineBaseCategory", "EnginePlatform", "EnginePlatform",
      "ProjectDefault", "ProjectPlatform", "ProjectPlatform", "UserSaved",
    ]);
    expect(layers.at(-1)!.file).toBe(path.join(project, "Saved", "Config", "WindowsEditor", "Engine.ini"));
  });
});

describe("project(resolve_setting)", () => {
  it("returns the final value and the layer that set it", async () => {
    const r = await run("resolve_setting", { configName: "Engine", section: "/Script/Engine.RendererSettings", key: "r.DefaultFeature.Bloom", platform: "Windows" });
    expect(r.value).toBe("False");
    expect(r.setBy).toMatchObject({ layer: "ProjectDefault", file: path.join(project, "Config", "DefaultEngine.ini"), line: 2, text: "r.DefaultFeature.Bloom=False" });
  });

  it("reads an engine platform override, and the user's Saved layer last", async () => {
    const shadow = await run("resolve_setting", { configName: "Engine", section: "/Script/Engine.RendererSettings", key: "r.Shadow.Virtual.Enable", platform: "Windows" });
    expect(shadow.value).toBe("1");
    expect(shadow.setBy.layer).toBe("EnginePlatform");
    const mac = await run("resolve_setting", { configName: "Engine", section: "/Script/Engine.RendererSettings", key: "r.Shadow.Virtual.Enable", platform: "Mac" });
    expect(mac.value).toBe("0");
    const speed = await run("resolve_setting", { configName: "Engine", section: "/Script/Game.MySettings", key: "Speed", platform: "Windows" });
    expect(speed).toMatchObject({ value: "900", setBy: { layer: "UserSaved" } });
  });

  it("reports a key nobody sets as unset, and says when there is no engine", async () => {
    const r = await run("resolve_setting", { configName: "Engine", section: "/Script/Game.MySettings", key: "Missing", platform: "Windows" });
    expect(r).toMatchObject({ value: null, set: false });
    const noEngine = await run("resolve_setting", { configName: "Engine", section: "/Script/Game.MySettings", key: "Speed", platform: "Windows" }, ctx(null));
    expect(noEngine.value).toBe("900");
    expect(noEngine.note).toMatch(/engine/i);
  });
});

describe("project(explain_setting)", () => {
  it("lists every layer that touches an array key, with the value after each", async () => {
    const r = await run("explain_setting", { configName: "Engine", section: "/Script/Engine.Engine", key: "ActiveGameNameRedirects", platform: "Windows" });
    expect(r.layers.map((l: { layer: string }) => l.layer)).toEqual(["EngineBaseCategory", "ProjectDefault", "ProjectPlatform"]);
    expect(r.layers[0].valueAfter).toEqual(['(OldGameName="A",NewGameName="B")', '(OldGameName="C",NewGameName="D")']);
    // -A removes A, .C adds a second C, +C adds nothing more.
    expect(r.layers[1].valueAfter).toEqual(['(OldGameName="C",NewGameName="D")', '(OldGameName="C",NewGameName="D")']);
    expect(r.layers[1].lines.map((l: { text: string }) => l.text[0])).toEqual(["-", ".", "+"]);
    expect(r.layers[2]).toMatchObject({ valueAfter: null, lines: [{ op: "clear" }] });
    expect(r.value).toBeNull();
    expect(r.layersRead.find((l: { layer: string }) => l.layer === "EngineBase").exists).toBe(true);
  });
});

describe("project(diff_config_from_default)", () => {
  it("lists the keys the project and user layers change from the engine's", async () => {
    const r = await run("diff_config_from_default", { configName: "Engine", platform: "Windows" });
    const byKey = Object.fromEntries(r.changes.map((c: { key: string }) => [c.key, c]));
    expect(byKey["r.DefaultFeature.Bloom"]).toMatchObject({ section: "/Script/Engine.RendererSettings", engineValue: "True", value: "False", setBy: { layer: "ProjectDefault" } });
    expect(byKey.ActiveGameNameRedirects).toMatchObject({ value: null, setBy: { layer: "ProjectPlatform" } });
    expect(byKey.Speed).toMatchObject({ engineValue: null, value: "900", setBy: { layer: "UserSaved" } });
    expect(byKey["r.Shadow.Virtual.Enable"]).toBeUndefined();
    expect(byKey.GameViewportClientClassName).toBeUndefined();
    expect(r.count).toBe(r.changes.length);
  });

  it("can leave out the per-user layer", async () => {
    const r = await run("diff_config_from_default", { configName: "Engine", platform: "Windows", includeUser: false });
    expect(r.changes.find((c: { key: string }) => c.key === "Speed")).toMatchObject({ value: "600", setBy: { layer: "ProjectDefault" } });
  });
});
