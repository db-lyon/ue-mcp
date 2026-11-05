// @live-editor base
/**
 * Project: module listings and load probes, set_config against a probe ini the
 * test owns, plugin references in the .uproject (restored byte for byte),
 * Live Coding, create_cpp_class (files removed after) and epic_call_tool.
 * Refs #1223.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { liveTarget } from "./harness.js";

const DIR = "/Game/MCPTest/BCov/Project";
const EPIC_BP = `${DIR}/BP_BCov_Project_Epic`;
const EPIC_BP_OBJECT = `${EPIC_BP}.BP_BCov_Project_Epic`;
const BLUEPRINT_TOOLSET = "editor_toolset.toolsets.blueprint.BlueprintTools";
const CONFIG_FILE = "BCovProjectProbe.ini";
const CONFIG_SECTION = "/Script/BCovProject.ProbeSettings";
const CONFIG_KEY = "ProbeValue";
const CPP_SUBPATH = "BCovProject";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let projectDir = "";
let uprojectBefore: string | null = null;
let createdCppFiles: string[] = [];
let createdCppDirs: string[] = [];

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** For handlers registered with a long server timeout. */
async function slow(method: string, params: Json, timeoutMs: number): Promise<Json> {
  const result = (await bridge.call(method, params, timeoutMs)) as Json;
  expect(result.success !== false, `${method}: ${JSON.stringify(result)}`).toBe(true);
  return result;
}

const configPath = (): string => path.join(projectDir, "Config", CONFIG_FILE);
const uprojectPath = (): string => path.join(projectDir, "ue_mcp.uproject");

async function pluginRow(name: string): Promise<Json> {
  const res = await call("list_available_plugins", { filter: name, limit: 2000 });
  const row = (res.plugins as Json[]).find((p) => p.name === name);
  expect(row, `plugin ${name} listed`).toBeDefined();
  return row!;
}

beforeAll(async () => {
  bridge = await getBridge();
  projectDir = (await liveTarget()).projectDir;
  uprojectBefore = fs.readFileSync(uprojectPath(), "utf8");
  fs.rmSync(configPath(), { force: true });
  await callBridge(bridge, "delete_asset", { assetPath: EPIC_BP, force: true });
  await call("create_blueprint", { assetPath: EPIC_BP, parentClass: "Actor" });
}, 120_000);

afterAll(async () => {
  // The plugin actions reformat the whole .uproject, so the original bytes go back.
  if (uprojectBefore !== null && projectDir) {
    fs.writeFileSync(uprojectPath(), uprojectBefore, "utf8");
  }
  if (projectDir) fs.rmSync(configPath(), { force: true });
  for (const f of createdCppFiles) fs.rmSync(f, { force: true });
  for (const d of createdCppDirs) fs.rmSync(d, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: EPIC_BP, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("modules", () => {
  it("list_project_modules names the test project's runtime module", async () => {
    const res = await call("list_project_modules", {});
    const mod = (res.modules as Json[]).find((m) => m.name === "ue_mcp");
    expect(mod).toBeDefined();
    expect(mod!.hostType).toBe("Runtime");
    expect(String(mod!.sourcePath).replace(/\\/g, "/")).toContain("Source/ue_mcp");
  }, 120_000);

  it("is_module_loaded answers true for the bridge module and false for a made-up one", async () => {
    const bridgeModule = await call("is_module_loaded", { moduleName: "UE_MCP_Bridge" });
    expect(bridgeModule.loaded).toBe(true);
    const missing = await call("is_module_loaded", { moduleName: "BCovProjectNoSuchModule" });
    expect(missing.loaded).toBe(false);
  }, 120_000);

  it("list_loaded_modules filters by name and reports load state", async () => {
    const res = await call("list_loaded_modules", { filter: "UE_MCP", loadedOnly: true });
    const rows = res.modules as Json[];
    expect(rows.map((m) => m.name)).toContain("UE_MCP_Bridge");
    expect(rows.every((m) => m.loaded === true && String(m.name).toLowerCase().includes("ue_mcp"))).toBe(true);
    expect(Number(res.totalLoaded)).toBeGreaterThan(rows.length);
  }, 120_000);
});

describe("config", () => {
  it("set_config writes a key to disk, reports the previous value and is a no-op on repeat", async () => {
    const first = await call("set_config", { configName: CONFIG_FILE, section: CONFIG_SECTION, key: CONFIG_KEY, value: "one" });
    expect(first.persisted).toBe(true);
    expect(fs.readFileSync(configPath(), "utf8")).toMatch(/ProbeValue=one/);

    const second = await call("set_config", { configName: CONFIG_FILE, section: CONFIG_SECTION, key: CONFIG_KEY, value: "two" });
    expect(second.value).toBe("two");
    const text = fs.readFileSync(configPath(), "utf8");
    expect(text).toContain(`[${CONFIG_SECTION}]`);
    expect(text).toMatch(/ProbeValue=two/);
    expect(text).not.toMatch(/ProbeValue=one/);
    expect(second.rollback).toMatchObject({ method: "set_config", payload: { value: "one" } });

    const repeat = await call("set_config", { configName: CONFIG_FILE, section: CONFIG_SECTION, key: CONFIG_KEY, value: "two" });
    expect(repeat.existed).toBe(true);

    // The editor keeps no copy of the file once the call returns, so a deleted
    // ini is not written back from its cache when the editor exits.
    fs.rmSync(configPath(), { force: true });
    const afterDelete = await call("set_config", { configName: CONFIG_FILE, section: CONFIG_SECTION, key: CONFIG_KEY, value: "two" });
    expect(afterDelete.existed).not.toBe(true);
    expect(afterDelete.rollback).toBeUndefined();
    expect(fs.readFileSync(configPath(), "utf8")).toMatch(/ProbeValue=two/);
  }, 120_000);
});

describe("plugin references", () => {
  it("enable_plugin and disable_plugin edit the .uproject entry that list_available_plugins reads", async () => {
    // An installed engine plugin the project neither loads nor references.
    const all = await call("list_available_plugins", { limit: 2000 });
    const candidate = (all.plugins as Json[]).find((p) =>
      p.type === "Engine" && p.enabled === false && p.enabledByDefault === false &&
      (p.projectReference as Json).present === false);
    expect(candidate, "an unreferenced, disabled engine plugin").toBeDefined();
    const name = String(candidate!.name);

    const enabled = await call("enable_plugin", { pluginName: name });
    expect(enabled.unchanged).toBe(false);
    expect(await pluginRow(name)).toMatchObject({ projectReference: { present: true, enabled: true } });
    expect(fs.readFileSync(uprojectPath(), "utf8")).toContain(`"${name}"`);

    const disabled = await call("disable_plugin", { pluginName: name });
    expect(disabled.unchanged).toBe(false);
    expect(await pluginRow(name)).toMatchObject({ projectReference: { present: true, enabled: false } });

    const removed = await call("disable_plugin", { pluginName: name, removeReference: true });
    expect(removed.unchanged).toBe(false);
    expect(await pluginRow(name)).toMatchObject({ projectReference: { present: false } });
    expect(fs.readFileSync(uprojectPath(), "utf8")).not.toContain(`"${name}"`);
  }, 120_000);
});

describe("Live Coding", () => {
  it("live_coding_compile starts Live Coding, which live_coding_status then reports", async () => {
    const before = await call("live_coding_status", {});
    expect(before.available).toBe(true);
    expect(typeof before.started).toBe("boolean");

    const compiled = await slow("live_coding_compile", { wait: true }, 300_000);
    expect(compiled.waited).toBe(true);
    expect(["success", "no_changes"]).toContain(compiled.result);

    const after = await call("live_coding_status", {});
    expect(after.started).toBe(true);
    expect(after.compiling).toBe(false);
  }, 330_000);
});

describe("C++ class creation", () => {
  it("create_cpp_class writes a header and source declaring the new class", async () => {
    const className = `BCovProbe${Date.now().toString(36)}`;
    const mod = (await call("list_project_modules", {})).modules as Json[];
    const sourceRoot = String(mod.find((m) => m.name === "ue_mcp")!.sourcePath);
    const target = path.join(sourceRoot, "Public", CPP_SUBPATH);
    if (!fs.existsSync(path.join(sourceRoot, "Public"))) createdCppDirs.push(path.join(sourceRoot, "Public"));
    if (!fs.existsSync(path.join(sourceRoot, "Private"))) createdCppDirs.push(path.join(sourceRoot, "Private"));
    createdCppDirs.push(target, path.join(sourceRoot, "Private", CPP_SUBPATH));

    const res = await slow("create_cpp_class", { className, parentClass: "Object", moduleName: "ue_mcp", subPath: CPP_SUBPATH }, 300_000);
    const header = String(res.headerPath);
    const cpp = String(res.cppPath);
    createdCppFiles = [header, cpp];

    expect(res.moduleName).toBe("ue_mcp");
    expect(path.basename(header)).toBe(`${className}.h`);
    expect(fs.readFileSync(header, "utf8")).toContain(`class UE_MCP_API U${className}`);
    expect(path.basename(cpp)).toBe(`${className}.cpp`);
    expect(fs.readFileSync(cpp, "utf8")).toContain(`${className}.h"`);
  }, 330_000);
});

describe("Epic toolsets", () => {
  it("epic_call_tool runs a wrapped Blueprint tool whose variable the bridge then lists", async () => {
    const added = await call("epic_call_tool", {
      toolset: BLUEPRINT_TOOLSET,
      tool: "add_variable",
      input: { blueprint: { refPath: EPIC_BP_OBJECT }, name: "BCovEpicCount", type_name: "int" },
    });
    expect(added.tool).toBe("add_variable");

    const vars = await call("list_blueprint_variables", { assetPath: EPIC_BP });
    const row = (vars.variables as Json[]).find((v) => v.name === "BCovEpicCount");
    expect(row, "BCovEpicCount listed").toBeDefined();
    expect(row!.type).toBe("int");

    const listed = await call("epic_call_tool", {
      toolset: BLUEPRINT_TOOLSET,
      tool: "list_variables",
      input: { blueprint: { refPath: EPIC_BP_OBJECT } },
    });
    const result = listed.result as Json | string[] | undefined;
    const names = Array.isArray(result) ? result : ((result as Json | undefined)?.returnValue as string[] | undefined) ?? [];
    expect(names).toContain("BCovEpicCount");
  }, 120_000);
});
