// @live-editor headless
/**
 * epic(get_toolset_states) and epic(set_toolset_enabled) (#1827): a real
 * registered toolset is turned off, which must take it out of
 * epic(list_toolsets) and make epic(call_tool) refuse its tools in this same
 * session, and then turned back on. The setting is written to the project's
 * config as well, so the test restores the list it found and removes a config
 * file that only it created, leaving the tracked test project as it was.
 */
import * as fs from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

interface ToolsetState {
  name: string;
  enabled: boolean;
  blockedBy: string[];
  allowedBy: string[];
  toggledHere: boolean;
  disabledBy?: string;
}
interface ListedToolset { name: string; toolCount: number; tools: string[] }

let bridge: EditorBridge;
let target: ListedToolset;
let configFile = "";
let configExisted = false;
let configBefore = "";
let blockedBefore: string[] = [];

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function states(): Promise<{ all: Json; byName: Map<string, ToolsetState> }> {
  const all = await call("epic_get_toolset_states", {});
  const byName = new Map((all.toolsets as ToolsetState[]).map((t) => [t.name, t]));
  return { all, byName };
}

async function listedNames(): Promise<string[]> {
  const listed = await call("epic_list_toolsets", {});
  return (listed.toolsets as ListedToolset[]).map((t) => t.name);
}

const exact = (name: string): string => `/^\\Q${name}\\E$/`;

/** Set when this editor cannot drive toggles; every case then asserts that refusal instead of skipping. */
let unavailable: Json | null = null;
const REFUSALS = ["unsupported_engine_version", "PLUGIN_NOT_AVAILABLE", "unsupported_platform"];

async function expectRefused(): Promise<void> {
  const r = body(await callBridge(bridge, "epic_set_toolset_enabled", { toolsets: ["AnyToolset"], enabled: false }));
  expect(r.success).toBe(false);
  expect(r.errorCode).toBe(unavailable!.errorCode);
  expect(String(r.error)).toBe(String(unavailable!.error));
}

beforeAll(async () => {
  bridge = await getBridge();
  const probe = body(await callBridge(bridge, "epic_get_toolset_states", {}));
  if (probe.success === false && REFUSALS.includes(String(probe.errorCode))) {
    unavailable = probe;
    return;
  }
  const listed = await call("epic_list_toolsets", {});
  const toolsets = (listed.toolsets as ListedToolset[]).filter((t) => t.toolCount > 0);
  // A toolset no other pattern touches, so turning it off and on is ours alone.
  const { all, byName } = await states();
  blockedBefore = all.blockedNames as string[];
  target = toolsets.find((t) => byName.get(t.name)?.enabled && byName.get(t.name)?.blockedBy.length === 0)!;
  configFile = String(all.configFile ?? "");
  configExisted = configFile !== "" && fs.existsSync(configFile);
  configBefore = configExisted ? fs.readFileSync(configFile, "utf8") : "";
}, 60_000);

afterAll(async () => {
  if (bridge && target) {
    await callBridge(bridge, "epic_set_toolset_enabled", { toolsets: [target.name], enabled: true });
  }
  // The registry setting is written to the tracked project's config; put it back as it was.
  if (configFile) {
    if (configExisted) fs.writeFileSync(configFile, configBefore);
    else if (fs.existsSync(configFile)) fs.rmSync(configFile);
  }
  if (bridge) disconnectBridge();
}, 60_000);

describe("epic toolset toggles", () => {
  it("epic_get_toolset_states lists every registered toolset with its enabled state and the patterns that match it", async () => {
    if (unavailable) return expectRefused();
    const all = await call("epic_get_toolset_states", {});
    const byName = new Map((all.toolsets as ToolsetState[]).map((t) => [t.name, t]));
    expect(target, "no enabled toolset with tools that no pattern blocks").toBeTruthy();
    const listed = await listedNames();
    // The listing shows enabled toolsets only; the states cover every one of them.
    for (const name of listed) expect(byName.get(name)?.enabled, name).toBe(true);
    expect(Number(all.toolsetCount)).toBe((all.toolsets as unknown[]).length);
    expect(Number(all.enabledCount)).toBeGreaterThanOrEqual(listed.length);
    const t = byName.get(target.name)!;
    expect(t.blockedBy).toEqual([]);
    expect(t.toggledHere).toBe(false);
    expect(String(all.configSection)).toBe("/Script/ToolsetRegistry.ToolsetRegistrySettings");
    expect(configFile).toMatch(/Default\w+\.ini$/);
  }, 60_000);

  it("epic_set_toolset_enabled turns a toolset off in the live registry and the config, and back on", async () => {
    if (unavailable) return expectRefused();
    const tool = target.tools[0];

    const off = await call("epic_set_toolset_enabled", { toolsets: [target.name], enabled: false });
    expect(off.updated).toBe(true);
    expect(off.changedCount).toBe(1);
    expect(off.toolsets).toEqual([{ name: target.name, previousEnabled: true, enabled: false, changed: true }]);
    expect(off.blockedNames).toContain(exact(target.name));
    expect(off.settingsBlockedNames).toContain(exact(target.name));
    expect(off.persisted).toBe(true);
    expect(off.rollback).toEqual({
      method: "epic_set_toolset_enabled",
      payload: { toolsets: [target.name], enabled: true },
    });

    // Gone from the catalog and refused by the dispatcher, in this session.
    expect(await listedNames()).not.toContain(target.name);
    const { byName } = await states();
    expect(byName.get(target.name)).toMatchObject({ enabled: false, disabledBy: "blockList", toggledHere: true });
    expect(byName.get(target.name)!.blockedBy).toEqual([exact(target.name)]);
    const refused = await callBridge(bridge, "epic_call_tool", { toolset: target.name, tool, input: {} });
    expect(succeeded(refused), JSON.stringify(refused.result)).toBe(false);
    expect(JSON.stringify(refused.result ?? refused.error)).toMatch(/not found/i);

    // The config file now carries the pattern.
    expect(fs.readFileSync(configFile, "utf8")).toContain(exact(target.name));

    // Asking for the state that already holds reports it and has nothing to undo.
    const again = await call("epic_set_toolset_enabled", { toolsets: [target.name], enabled: false });
    expect(again.unchanged).toBe(true);
    expect(again.changedCount).toBe(0);
    expect(again.rollback).toBeUndefined();

    const on = await call("epic_set_toolset_enabled", { toolsets: [target.name], enabled: true });
    expect(on.toolsets).toEqual([{ name: target.name, previousEnabled: false, enabled: true, changed: true }]);
    expect(on.blockedNames).toEqual(blockedBefore);
    expect(on.rollback).toEqual({
      method: "epic_set_toolset_enabled",
      payload: { toolsets: [target.name], enabled: false },
    });
    expect(await listedNames()).toContain(target.name);
    expect(fs.readFileSync(configFile, "utf8")).not.toContain(exact(target.name));
  }, 120_000);

  it("epic_set_toolset_enabled refuses a name that is not a registered toolset and changes nothing", async () => {
    if (unavailable) return expectRefused();
    const missing = `${target.name}DoesNotExist`;
    const r = await callBridge(bridge, "epic_set_toolset_enabled", { toolsets: [target.name, missing], enabled: false });
    const refused = body(r);
    expect(succeeded(r)).toBe(false);
    expect(refused.errorCode).toBe("unknown_toolset");
    expect(refused.unknown).toEqual([missing]);
    expect(refused.validNames).toContain(target.name);
    const { byName } = await states();
    expect(byName.get(target.name)?.enabled).toBe(true);
  }, 60_000);
});
