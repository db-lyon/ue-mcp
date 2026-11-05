// @live-editor headless
/**
 * Mass entity config authoring: trait discovery, then a config created,
 * reordered, trimmed and validated, each step read back through
 * read_mass_entity_config. Needs the MassGameplay plugin in tests/ue_mcp.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Gameplay/Mass";
const CONFIG = `${DIR}/MEC_BCov`;

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);

let bridge: EditorBridge;
/** Three concrete trait class paths, picked from what list_mass_types reports. */
let traits: string[] = [];

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const order = async (): Promise<string[]> =>
  ((await call("read_mass_entity_config", { assetPath: CONFIG })).traitClasses as string[]).map(String);

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
});

describe("Mass entity config", () => {
  it("list_mass_types reports loaded trait classes and filters them by name", async () => {
    const all = await call("list_mass_types", { kind: "traits", limit: 2000 });
    expect(all.unavailable).toBeUndefined();
    const found = rows(all.traits);
    expect(found.length).toBeGreaterThanOrEqual(3);
    traits = found.slice(0, 3).map((t) => String(t.classPath));

    const name = String(found[0].className);
    const filtered = await call("list_mass_types", { kind: "traits", filter: name });
    expect(rows(filtered.traits).map((t) => t.className)).toContain(name);
    expect(rows(filtered.traits).every((t) => String(t.className).toLowerCase().includes(name.toLowerCase()))).toBe(true);
    expect(filtered.processors).toBeUndefined();
  }, 120_000);

  it("ensure_mass_entity_config, reorder_mass_traits and remove_mass_trait change the order read_mass_entity_config reads", async () => {
    expect(traits).toHaveLength(3);
    const [a, b, c] = traits;
    await call("ensure_mass_entity_config", { name: "MEC_BCov", packagePath: DIR, traits: traits.map((t) => ({ class: t })) });
    expect(await order()).toEqual([a, b, c]);

    await call("reorder_mass_traits", { assetPath: CONFIG, order: [2, 0, 1] });
    expect(await order()).toEqual([c, a, b]);

    const byClass = await call("remove_mass_trait", { assetPath: CONFIG, traitClass: a });
    expect(byClass.removedClass).toBe(a);
    expect(await order()).toEqual([c, b]);

    await call("remove_mass_trait", { assetPath: CONFIG, index: 0 });
    expect(await order()).toEqual([b]);
  }, 120_000);

  it("validate_mass_entity_config walks the config's traits", async () => {
    const report = await call("validate_mass_entity_config", { assetPath: CONFIG });
    expect(report.traitCount).toBe(1);
    expect(rows(report.traits).map((t) => t.classPath)).toEqual([traits[1]]);
    expect((report.parentChain as string[]).some((p) => String(p).startsWith(CONFIG))).toBe(true);
    expect(Array.isArray(report.problems)).toBe(true);
  }, 120_000);
});
