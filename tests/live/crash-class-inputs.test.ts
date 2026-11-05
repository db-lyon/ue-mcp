// @live-editor headless
/**
 * Crash-class inputs (#1223, item 3).
 *
 * Smoke proves an action answers one well-formed call. The calls that have
 * taken the editor down are the ones it never makes: the map that is already
 * loaded (#1218), an asset with its editor open, an asset while PIE runs, and a
 * reference that is empty, null or names nothing. For each class this drives
 * the actions that take an asset or world reference, then asks the editor a
 * second question: an editor that died answers nothing.
 *
 * Where an action has an observable effect (a copy, a rename, a delete) the
 * effect is read back as well. Survival alone is not behavioural coverage, so
 * the survival tables are kept out of the test bodies the coverage inventory
 * reads; only the read-back cases are credited there.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MAP = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/CrashClass`;
const BP = `${DIR}/BP_CrashProbe`;
const BP_RENAME_FROM = `${DIR}/BP_CrashRenameFrom`;
const BP_RENAME_TO = `${DIR}/BP_CrashRenameTo`;
const BP_DOOMED = `${DIR}/BP_CrashDoomed`;
const BP_PIE_DOOMED = `${DIR}/BP_CrashPieDoomed`;
const SCRATCH_MAP = `${DIR}/L_CrashScratch`;
const MISSING = `${DIR}/BP_ThisAssetDoesNotExist`;

/** What a dead or wedged editor looks like from this side of the socket. */
const TRANSPORT_FAILURE = /connection lost|not connected|timed out|BRIDGE_TIMEOUT|CONNECTION_LOST|NOT_CONNECTED|Unknown method|Failed after retries/i;

let bridge: EditorBridge;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const refused = (r: TestResult): boolean => !r.ok || body(r).success === false;

/**
 * Call, then prove the editor is still there. A structured refusal is a pass;
 * a lost connection, a timeout or a bridge that no longer answers is not.
 */
async function survive(method: string, params: Json): Promise<TestResult> {
  const result = await callBridge(bridge, method, params);
  if (!result.ok) {
    expect(result.error ?? "", `${method} ${JSON.stringify(params)} did not answer`).not.toMatch(TRANSPORT_FAILURE);
  }
  const alive = await callBridge(bridge, "get_current_level", {});
  expect(alive.ok, `${method} ${JSON.stringify(params)} left the editor unreachable: ${alive.error}`).toBe(true);
  return result;
}

async function currentMap(): Promise<string | undefined> {
  const level = await callBridge(bridge, "get_current_level", {});
  expect(level.ok, level.error).toBe(true);
  return body(level).mapPackagePath as string | undefined;
}

async function makeBlueprint(path: string): Promise<void> {
  await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  const created = await callBridge(bridge, "create_blueprint", { assetPath: path, parentClass: "Actor" });
  expect(created.ok, created.error).toBe(true);
  const saved = await callBridge(bridge, "save_asset", { assetPath: path });
  expect(saved.ok, saved.error).toBe(true);
}

async function exists(path: string): Promise<boolean> {
  return !refused(await callBridge(bridge, "read_asset", { assetPath: path }));
}

beforeAll(async () => {
  bridge = await getBridge();
  // MCP_Home is not checked in; demo_go_home creates it on a fresh project and loads it.
  const home = await callBridge(bridge, "demo_go_home", {});
  expect(home.ok && body(home).success !== false, home.error ?? JSON.stringify(home.result)).toBe(true);
  const loaded = await callBridge(bridge, "load_level", { levelPath: MAP });
  expect(loaded.ok, loaded.error).toBe(true);
  for (const path of [BP, BP_RENAME_FROM, BP_DOOMED, BP_PIE_DOOMED]) await makeBlueprint(path);
  await callBridge(bridge, "delete_asset", { assetPath: BP_RENAME_TO, force: true });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "pie_control", { pieAction: "stop" });
  await callBridge(bridge, "load_level", { levelPath: MAP });
  for (const path of [BP, BP_RENAME_FROM, BP_RENAME_TO, BP_DOOMED, BP_PIE_DOOMED, SCRATCH_MAP, `${DIR}/MCP_Home_Copy`, `${DIR}/BP_CrashProbe_Copy`]) {
    await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  }
  disconnectBridge();
}, 300_000);

// ---------------------------------------------------------------------------
// 1. The map that is already loaded
// ---------------------------------------------------------------------------

/** Reads and writes aimed at the loaded world. Each must leave the same map loaded. */
const LOADED_MAP_PROBES: Array<[string, Json]> = [
  ["open_asset", { assetPath: MAP }],
  ["load_level", { levelPath: MAP }],
  ["read_asset", { assetPath: MAP }],
  ["read_asset_properties", { assetPath: MAP }],
  ["read_blueprint", { assetPath: MAP }],
  ["list_blueprint_graphs", { assetPath: MAP }],
  ["validate_assets", { assetPath: MAP }],
  ["get_asset_referencers", { packages: [MAP] }],
  ["get_asset_dependencies", { packages: [MAP] }],
  ["reload_package", { assetPath: MAP }],
  ["save_asset", { assetPath: MAP }],
  ["save_level", {}],
];

describe("the currently loaded map", () => {
  it("survives every reference to the loaded world and keeps it loaded", async () => {
    expect(await currentMap()).toBe(MAP);
    for (const [method, params] of LOADED_MAP_PROBES) {
      await survive(method, params);
      expect(await currentMap(), `${method} changed the loaded map`).toBe(MAP);
    }
  }, 600_000);

  it("duplicates the loaded map into a new asset without switching to it", async () => {
    const copy = `${DIR}/MCP_Home_Copy`;
    await callBridge(bridge, "delete_asset", { assetPath: copy, force: true });
    const duplicated = await survive("duplicate_asset", { sourcePath: MAP, destinationPath: copy });
    expect(refused(duplicated), duplicated.error ?? JSON.stringify(duplicated.result)).toBe(false);
    expect(await exists(copy)).toBe(true);
    expect(await currentMap()).toBe(MAP);
  }, 300_000);

  it("deletes a map while it is the loaded one, and the editor carries on", async () => {
    await callBridge(bridge, "delete_asset", { assetPath: SCRATCH_MAP, force: true });
    const created = await survive("create_new_level", { levelPath: SCRATCH_MAP });
    expect(refused(created), created.error ?? JSON.stringify(created.result)).toBe(false);
    if ((await currentMap()) !== SCRATCH_MAP) await survive("load_level", { levelPath: SCRATCH_MAP });
    expect(await currentMap()).toBe(SCRATCH_MAP);

    const deleted = await survive("delete_asset", { assetPath: SCRATCH_MAP, force: true });
    // Refusing to delete the open map is as good an answer as deleting it; what
    // is not allowed is reporting a delete that did not happen, or dying.
    if (!refused(deleted)) expect(await exists(SCRATCH_MAP)).toBe(false);
    else expect(await exists(SCRATCH_MAP)).toBe(true);

    const back = await survive("load_level", { levelPath: MAP });
    expect(refused(back), back.error).toBe(false);
    expect(await currentMap()).toBe(MAP);
  }, 300_000);
});

// ---------------------------------------------------------------------------
// 2. An asset open in its editor
// ---------------------------------------------------------------------------

const OPEN_ASSET_PROBES: Array<[string, Json]> = [
  ["read_blueprint", { assetPath: BP }],
  ["read_asset_properties", { assetPath: BP }],
  ["compile_blueprint", { assetPath: BP }],
  ["validate_blueprint", { assetPath: BP }],
  ["save_asset", { assetPath: BP }],
  ["reload_package", { assetPath: BP }],
  ["open_asset", { assetPath: BP }],
];

async function openInEditor(path: string): Promise<void> {
  const opened = await survive("open_asset", { assetPath: path });
  expect(refused(opened), opened.error ?? JSON.stringify(opened.result)).toBe(false);
  const editors = await callBridge(bridge, "get_open_asset_editors", {});
  expect(editors.ok, editors.error).toBe(true);
  expect(JSON.stringify(editors.result), `${path} is not open in an editor`).toContain(path.split("/").pop()!);
}

describe("an asset open in its editor", () => {
  it("survives every reference to a Blueprint whose editor is open", async () => {
    await openInEditor(BP);
    for (const [method, params] of OPEN_ASSET_PROBES) await survive(method, params);
    // Reloading the package under an open editor must still leave a readable Blueprint.
    const read = await callBridge(bridge, "read_blueprint", { assetPath: BP });
    expect(refused(read), read.error).toBe(false);
  }, 600_000);

  it("compiles an open Blueprint and reports it compiled", async () => {
    await openInEditor(BP);
    const compiled = await survive("compile_blueprint", { assetPath: BP });
    expect(refused(compiled), compiled.error ?? JSON.stringify(compiled.result)).toBe(false);
    const read = await callBridge(bridge, "read_blueprint", { assetPath: BP });
    expect(String(body(read).parentClass ?? "")).toMatch(/Actor/);
  }, 300_000);

  it("duplicates an open Blueprint into a second readable asset", async () => {
    const copy = `${DIR}/BP_CrashProbe_Copy`;
    await callBridge(bridge, "delete_asset", { assetPath: copy, force: true });
    await openInEditor(BP);
    const duplicated = await survive("duplicate_asset", { sourcePath: BP, destinationPath: copy });
    expect(refused(duplicated), duplicated.error ?? JSON.stringify(duplicated.result)).toBe(false);
    const read = await callBridge(bridge, "read_blueprint", { assetPath: copy });
    expect(refused(read), read.error).toBe(false);
    expect(await exists(BP)).toBe(true);
  }, 300_000);

  it("renames an open Blueprint: the new path reads, the old one does not", async () => {
    await openInEditor(BP_RENAME_FROM);
    const renamed = await survive("rename_asset", { sourcePath: BP_RENAME_FROM, destinationPath: BP_RENAME_TO });
    if (refused(renamed)) {
      // A refusal must leave the asset where it was.
      expect(await exists(BP_RENAME_FROM)).toBe(true);
      return;
    }
    expect(await exists(BP_RENAME_TO)).toBe(true);
    const old = await callBridge(bridge, "read_blueprint", { assetPath: BP_RENAME_FROM });
    expect(refused(old)).toBe(true);
  }, 300_000);

  it("deletes an open Blueprint and closes its editor rather than leaving it on a dead object", async () => {
    await openInEditor(BP_DOOMED);
    const deleted = await survive("delete_asset", { assetPath: BP_DOOMED, force: true });
    if (refused(deleted)) {
      expect(await exists(BP_DOOMED)).toBe(true);
      return;
    }
    expect(await exists(BP_DOOMED)).toBe(false);
    const editors = await callBridge(bridge, "get_open_asset_editors", {});
    expect(editors.ok, editors.error).toBe(true);
    expect(JSON.stringify(editors.result)).not.toContain("BP_CrashDoomed");
  }, 300_000);
});

// ---------------------------------------------------------------------------
// 3. Mid-PIE
// ---------------------------------------------------------------------------

/** Editor-world mutations and reads while a PIE world exists beside it. */
const PIE_PROBES: Array<[string, Json]> = [
  ["read_asset", { assetPath: MAP }],
  ["open_asset", { assetPath: MAP }],
  ["load_level", { levelPath: MAP }],
  ["open_asset", { assetPath: BP }],
  ["compile_blueprint", { assetPath: BP }],
  ["save_asset", { assetPath: BP }],
  ["reload_package", { assetPath: BP }],
  ["read_blueprint", { assetPath: BP }],
  ["save_level", {}],
  ["reload_package", { assetPath: MAP }],
  ["get_world_outliner", {}],
];

async function pieRunning(): Promise<boolean> {
  const status = await callBridge(bridge, "pie_control", { pieAction: "status" });
  return status.ok && body(status).isPlaying === true;
}

describe("an asset mid-PIE", () => {
  it("survives every editor-world reference while PIE runs, and PIE is still answerable after", async (ctx) => {
    const started = await callBridge(bridge, "pie_control", { pieAction: "start" });
    expect(started.error ?? "").not.toMatch(TRANSPORT_FAILURE);
    let playing = false;
    for (let i = 0; i < 20 && !playing; i++) {
      playing = await pieRunning();
      if (!playing) await new Promise((r) => setTimeout(r, 1000));
    }
    if (!playing) ctx.skip(`PIE did not start, so the mid-PIE inputs cannot be reached: ${started.error ?? JSON.stringify(started.result)}`);

    try {
      for (const [method, params] of PIE_PROBES) {
        await survive(method, params);
        const status = await callBridge(bridge, "pie_control", { pieAction: "status" });
        expect(status.ok, `${method} during PIE left pie_control unanswerable: ${status.error}`).toBe(true);
      }
    } finally {
      await callBridge(bridge, "pie_control", { pieAction: "stop" });
    }
  }, 600_000);

  it("deletes an asset during PIE and both worlds survive it", async (ctx) => {
    const started = await callBridge(bridge, "pie_control", { pieAction: "start" });
    expect(started.error ?? "").not.toMatch(TRANSPORT_FAILURE);
    let playing = false;
    for (let i = 0; i < 20 && !playing; i++) {
      playing = await pieRunning();
      if (!playing) await new Promise((r) => setTimeout(r, 1000));
    }
    if (!playing) ctx.skip("PIE did not start");

    try {
      const deleted = await survive("delete_asset", { assetPath: BP_PIE_DOOMED, force: true });
      if (refused(deleted)) expect(await exists(BP_PIE_DOOMED)).toBe(true);
      else expect(await exists(BP_PIE_DOOMED)).toBe(false);
      expect(await pieRunning()).toBe(true);
    } finally {
      const stopped = await callBridge(bridge, "pie_control", { pieAction: "stop" });
      expect(stopped.error ?? "").not.toMatch(TRANSPORT_FAILURE);
    }
    for (let i = 0; i < 20 && (await pieRunning()); i++) await new Promise((r) => setTimeout(r, 500));
    expect(await pieRunning()).toBe(false);
    expect(await currentMap()).toBe(MAP);
  }, 300_000);
});

// ---------------------------------------------------------------------------
// 4. A null, empty or missing reference
// ---------------------------------------------------------------------------

/** Each takes one asset reference, under the parameter name it declares. */
const REFERENCE_PARAMS: Array<[string, string]> = [
  ["open_asset", "assetPath"],
  ["read_asset", "assetPath"],
  ["read_asset_properties", "assetPath"],
  ["read_blueprint", "assetPath"],
  ["list_blueprint_graphs", "assetPath"],
  ["compile_blueprint", "assetPath"],
  ["validate_blueprint", "assetPath"],
  ["reload_package", "assetPath"],
  ["read_material", "assetPath"],
  ["read_widget_tree", "assetPath"],
  ["read_state_tree", "assetPath"],
  ["read_datatable", "assetPath"],
  ["get_mesh_geometry", "assetPath"],
  ["load_level", "levelPath"],
  ["duplicate_asset", "sourcePath"],
  ["rename_asset", "sourcePath"],
  ["delete_asset", "assetPath"],
  ["save_asset", "assetPath"],
];

/** These answer a missing reference with success by design: nothing to delete, nothing to save. */
const IDEMPOTENT_ON_MISSING = new Set(["delete_asset", "save_asset"]);

const BAD_REFERENCES: Array<[string, unknown]> = [
  ["missing", MISSING],
  ["empty", ""],
  ["null", null],
  ["whitespace", "   "],
  ["not a path", "not/a/real//path"],
];

describe("a null, empty or missing reference", () => {
  for (const [label, value] of BAD_REFERENCES) {
    it(`is refused, not crashed on, when the reference is ${label}`, async () => {
      const before = await currentMap();
      for (const [method, param] of REFERENCE_PARAMS) {
        const params: Json = { [param]: value };
        if (method === "duplicate_asset" || method === "rename_asset") params.destinationPath = `${DIR}/BP_NeverWritten`;
        const result = await survive(method, params);
        if (!IDEMPOTENT_ON_MISSING.has(method)) {
          expect(refused(result), `${method} reported success for a ${label} reference: ${JSON.stringify(result.result)}`).toBe(true);
        }
      }
      expect(await exists(`${DIR}/BP_NeverWritten`)).toBe(false);
      expect(await currentMap()).toBe(before);
    }, 600_000);
  }
});
