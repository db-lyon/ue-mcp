// @live-editor render
/**
 * A call made right after a save is not refused by the validation progress window (#1851).
 *
 * Data validation runs on the tick after a save and shows "Validating Assets", a slow-task progress window with
 * a Cancel button. The editor puts it on Slate's modal stack without the modal loop, and the dialog gate used to
 * read it as a blocking dialog, so calls that landed while it was up (or before the status snapshot caught up
 * with it closing) came back as dialog refusals. Render kind, because the editor only draws that window with a
 * real renderer. The calls go straight to the bridge, so it is the plugin's gate under test.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/SaveValidation`;
const INPUT_ACTION = "/Script/EnhancedInput.InputAction";
const COUNT = 24;
const ASSETS = Array.from({ length: COUNT }, (_, i) => `${DIR}/IA_SaveValidation_${i}`);

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  for (let i = 0; i < COUNT; i++) {
    await call("create_data_asset", { name: `IA_SaveValidation_${i}`, className: INPUT_ACTION, packagePath: DIR });
  }
  await call("save_asset", { assetPaths: ASSETS, force: true });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
}, 300_000);

describe("calls right after a save", () => {
  it("run instead of being refused while validation shows its progress window", async () => {
    for (let round = 0; round < 3; round++) {
      for (const path of ASSETS) {
        await call("set_asset_property", { assetPath: path, propertyName: "bConsumeInput", value: round % 2 === 0, save: false });
      }
      const saved = await call("save_asset", { assetPaths: ASSETS, force: true });
      expect(saved.success).not.toBe(false);

      // Straight after the save returns, while validation runs on the next tick and its window comes and goes.
      const results: TestResult[] = [];
      const until = Date.now() + 3000;
      while (Date.now() < until) {
        results.push(await callBridge(bridge, "get_current_level", {}));
        results.push(await callBridge(bridge, "read_asset", { assetPath: ASSETS[0] }));
      }
      const refused = results.filter((r) => body(r).dialogBlocking === true);
      expect(refused.map((r) => `${r.method}: ${String(body(r).error)}`)).toEqual([]);
      const failed = results.filter((r) => !succeeded(r));
      expect(failed.map((r) => `${r.method}: ${r.error ?? JSON.stringify(r.result)}`)).toEqual([]);
    }
    // Nothing was left on screen for anybody to answer.
    expect((await call("list_dialogs")).count).toBe(0);
  }, 300_000);
});
