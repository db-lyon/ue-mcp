// @live-editor headless
/**
 * Editor Utility Widget and Editor Utility Blueprint creation, read back by
 * search_assets, and running each, read back as a live instance of its class.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Widget/EditorUtility";
const EUW_NAME = "EUW_BCov_Widget";
const EUB_NAME = "EUB_BCov_Blueprint";
const EUW = `${DIR}/${EUW_NAME}`;
const EUB = `${DIR}/${EUB_NAME}`;

type Json = Record<string, unknown>;
type Row = { path: string; name: string; className?: string };
type Window = { windowId: number; title: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function searched(name: string): Promise<Row | undefined> {
  const found = await call("search_assets", { query: name, directory: DIR });
  return (found.results as Row[]).find((r) => r.name === name);
}

/** Live, non-default instances of a Blueprint's generated class in any world. */
async function instances(assetPath: string): Promise<number> {
  const found = await call("find_object", { className: assetPath, world: "any", limit: 50 });
  return Number(found.totalMatches ?? 0);
}

/** Close any floating window the utility widget tab opened in. */
async function closeWindowsTitled(fragment: string): Promise<void> {
  const listed = await callBridge(bridge, "list_windows", { maxTexts: 1, maxButtons: 1 });
  for (const w of (body(listed).windows as Window[] | undefined) ?? []) {
    if (w.title.includes(fragment)) await callBridge(bridge, "close_window", { windowId: w.windowId });
  }
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: EUW, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: EUB, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await closeWindowsTitled(EUW_NAME);
    await callBridge(bridge, "delete_asset", { assetPath: EUW, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: EUB, force: true });
    disconnectBridge();
  }
});

describe("editor utilities", () => {
  it("create_editor_utility_widget makes a utility widget run_editor_utility_widget opens", async () => {
    const created = await call("create_editor_utility_widget", { assetPath: EUW });
    expect(String(created.path)).toContain(EUW_NAME);
    expect((await searched(EUW_NAME))?.className).toBe("EditorUtilityWidgetBlueprint");

    const before = await instances(EUW);
    const ran = await call("run_editor_utility_widget", { assetPath: EUW });
    expect(ran.name).toBe(EUW_NAME);
    expect(await instances(EUW)).toBeGreaterThan(before);
  }, 120_000);

  it("create_editor_utility_blueprint makes a utility blueprint run_editor_utility_blueprint instantiates", async () => {
    const created = await call("create_editor_utility_blueprint", { assetPath: EUB });
    expect(String(created.path)).toContain(EUB_NAME);
    expect((await searched(EUB_NAME))?.className).toBe("EditorUtilityBlueprint");

    const before = await instances(EUB);
    const ran = await call("run_editor_utility_blueprint", { assetPath: EUB });
    expect(ran.name).toBe(EUB_NAME);
    expect(await instances(EUB)).toBeGreaterThan(before);
  }, 120_000);
});
