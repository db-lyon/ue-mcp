// @live-editor headless
/**
 * Map Check and Python's additional search paths (#1828).
 *
 * run_map_check runs Map Check on the open map and log_map_check_message writes to the MapCheck listing, read
 * back through get_message_log; both run on every engine. add_python_path, remove_python_path and
 * list_python_paths drive UE 5.9's additional-path registry with a scratch folder under the OS temp
 * directory, and refuse with unsupported_engine_version before 5.9. Actors are labelled BCov_MapCheck* and
 * deleted afterwards.
 */
import { mkdirSync, rmSync } from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PREFIX = "BCov_MapCheck";
const OWNER = "BCovPythonPaths";
const ROOT = path.join(os.tmpdir(), `ue-mcp-bcov-pypaths-${process.pid}`);
const PATH_A = path.join(ROOT, "a");
const PATH_B = path.join(ROOT, "b");

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const same = (a: string, b: string) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();

let bridge: EditorBridge;
let is59 = false;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function expectUnsupported(method: string, params: Json, fn: string): Promise<void> {
  const r = await callBridge(bridge, method, params);
  const out = body(r);
  expect(succeeded(r), JSON.stringify(out)).toBe(false);
  expect(out.errorCode).toBe("unsupported_engine_version");
  expect(String(out.error)).toContain(`${fn} requires Unreal Engine 5.9 or newer`);
}

const listed = async (owner?: string): Promise<string[]> =>
  ((await call("list_python_paths", owner ? { owner } : {})).paths as string[]);

beforeAll(async () => {
  bridge = await getBridge();
  const caps = body(await callBridge(bridge, "get_bridge_capabilities", {}));
  const [major, minor] = String(caps.engineVersion).split(".").map(Number);
  is59 = major > 5 || (major === 5 && minor >= 9);
  await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
  mkdirSync(PATH_A, { recursive: true });
  mkdirSync(PATH_B, { recursive: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
    if (is59) await callBridge(bridge, "remove_python_path", { owner: OWNER });
    disconnectBridge();
  }
  rmSync(ROOT, { recursive: true, force: true });
}, 120_000);

describe("Map Check", () => {
  const EMPTY = `${PREFIX}_EmptyMesh`;
  const MARKER = `${PREFIX} marker ${process.pid}`;

  it("log_map_check_message adds a warning linked to an actor that get_message_log reads back", async () => {
    await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label: EMPTY, location: { x: 0, y: 63000, z: 100 } });
    const out = await call("log_map_check_message", { severity: "warning", message: MARKER, actorLabel: EMPTY });
    expect(out.actorLabel).toBe(EMPTY);
    expect(Number(out.messagesAfter)).toBe(Number(out.messagesBefore) + 1);
    expect(out.rollbackPossible).toBe(false);
    const log = await call("get_message_log", { logName: "MapCheck", maxLines: 1000 });
    const row = (log.messages as Json[]).find((m) => String(m.text).includes(MARKER));
    expect(row?.severity).toBe("Warning");

    const bad = await callBridge(bridge, "log_map_check_message", { severity: "fatal", message: MARKER });
    expect(succeeded(bad)).toBe(false);
    expect(String(body(bad).error)).toContain("fatal");
  }, 60_000);

  it("run_map_check starts a fresh page and reports the actor without a mesh", async () => {
    const out = await call("run_map_check", { maxMessages: 5000 });
    const rows = out.messages as Json[];
    expect(rows.some((m) => String(m.text).includes(MARKER))).toBe(false);
    const flagged = rows.find((m) => m.actorLabel === EMPTY);
    expect(flagged, JSON.stringify(rows.slice(0, 20))).toBeDefined();
    expect((flagged!.objects as string[]).some((o) => o.endsWith(EMPTY) || o.includes("StaticMeshActor"))).toBe(true);
    expect(Number(out.total)).toBeGreaterThanOrEqual(rows.length);
    expect(Number(out.errors) + Number(out.warnings)).toBeLessThanOrEqual(Number(out.total));

    const warningsOnly = await call("run_map_check", { severity: "warning" });
    for (const m of warningsOnly.messages as Json[]) expect(String(m.severity).toLowerCase()).toContain("warning");
  }, 60_000);
});

describe("Python additional paths", () => {
  it("add_python_path registers a folder once under an owner, and list_python_paths lists it", async () => {
    if (!is59) return expectUnsupported("add_python_path", { path: PATH_A, owner: OWNER }, "UPythonScriptLibrary::AddAdditionalPath");
    const added = await call("add_python_path", { path: PATH_A, owner: OWNER });
    expect(added.created).toBe(true);
    expect((added.rollback as Json).method).toBe("remove_python_path");
    expect((await listed(OWNER)).some((p) => same(p, PATH_A))).toBe(true);
    expect((await listed()).some((p) => same(p, PATH_A))).toBe(true);
    expect((await call("add_python_path", { path: PATH_A, owner: OWNER })).existed).toBe(true);

    const relative = await callBridge(bridge, "add_python_path", { path: "relative/folder", owner: OWNER });
    expect(succeeded(relative)).toBe(false);
  }, 60_000);

  it("list_python_paths says when an owner has registered nothing", async () => {
    if (!is59) return expectUnsupported("list_python_paths", { owner: "BCovNobody" }, "UPythonScriptLibrary::GetAdditionalPaths");
    const out = await call("list_python_paths", { owner: "BCovNobody" });
    expect(out.ownerRegistered).toBe(false);
    expect(out.paths).toEqual([]);
  }, 60_000);

  it("remove_python_path removes one path, then every path of an owner", async () => {
    if (!is59) return expectUnsupported("remove_python_path", { path: PATH_A, owner: OWNER }, "UPythonScriptLibrary::RemoveAdditionalPath");
    const one = await call("remove_python_path", { path: PATH_A, owner: OWNER });
    expect(one.updated).toBe(true);
    expect((one.removedPaths as string[]).some((p) => same(p, PATH_A))).toBe(true);
    expect((one.rollback as Json).method).toBe("add_python_path");
    expect((await listed(OWNER)).some((p) => same(p, PATH_A))).toBe(false);
    expect((await call("remove_python_path", { path: PATH_A, owner: OWNER })).alreadyRemoved).toBe(true);

    await call("add_python_path", { path: PATH_A, owner: OWNER });
    await call("add_python_path", { path: PATH_B, owner: OWNER });
    const all = await call("remove_python_path", { owner: OWNER });
    expect((all.removedPaths as string[]).length).toBe(2);
    expect(await listed(OWNER)).toEqual([]);
  }, 60_000);
});
