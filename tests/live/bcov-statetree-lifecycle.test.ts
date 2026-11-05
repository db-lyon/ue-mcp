// @live-editor headless
/**
 * StateTree lifecycle: a bare tree from the generic asset route repaired with
 * set_schema, then compiled and validated, each read back through read.
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/StateTree";
const ST = `${DIR}/ST_BCovBare`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
  await call("create_asset_by_class", { name: "ST_BCovBare", className: "/Script/StateTreeModule.StateTree", packagePath: DIR });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
    disconnectBridge();
  }
});

describe("statetree lifecycle", () => {
  it("set_state_tree_schema gives a bare tree editor data, a schema and a root state", async () => {
    const bare = await callBridge(bridge, "read_state_tree", { assetPath: ST });
    expect(succeeded(bare)).toBe(false);

    const res = await call("set_state_tree_schema", { assetPath: ST });
    expect(res.createdEditorData).toBe(true);
    expect(res.createdRootState).toBe(true);
    expect(String(res.schema)).toMatch(/^\/Script\//);

    const tree = await call("read_state_tree", { assetPath: ST });
    expect(tree.schemaClass).toBe(res.schema);
    expect(tree.subTrees as Json[]).toHaveLength(1);
  }, 120_000);

  it("compile_state_tree compiles and saves the repaired tree", async () => {
    const states = (await call("list_state_tree_states", { assetPath: ST })).states as Json[];
    await call("add_state_tree_state", { assetPath: ST, stateId: states[0].id, name: "BCov_Leaf" });

    const res = await call("compile_state_tree", { assetPath: ST });
    expect(res.compiled, JSON.stringify(res.errors)).toBe(true);
    expect(res.errors as unknown[]).toHaveLength(0);
    expect(res.saved).toBe(true);
  }, 120_000);

  it("validate_state_tree validates against the schema read_state_tree reports", async () => {
    const tree = await call("read_state_tree", { assetPath: ST });
    const res = await call("validate_state_tree", { assetPath: ST });
    expect(res.validated).toBe(true);
    expect(res.schemaClass).toBe(tree.schemaClass);
    expect(String(res.assetPath)).toContain("ST_BCovBare");
  }, 120_000);
});
