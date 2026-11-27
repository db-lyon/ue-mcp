// @live-editor headless
/**
 * The pie category's editor-time surface, read back after every write:
 * observation profiles as assets, scenario validation, and the multiplayer
 * player and acceptance-run readers with no session running.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Pie/Assets";
const PROFILE = `${DIR}/OP_BCovAssets`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json = {}): Promise<string> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should be refused: ${JSON.stringify(r.result)}`).toBe(false);
  return String(body(r).error ?? r.error ?? "");
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 60_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
});

describe("observation profiles", () => {
  it("profile_create, profile_read, profile_update, profile_list and profile_delete round-trip one asset", async () => {
    const created = await call("pie_profile_create", {
      name: "OP_BCovAssets", package_path: DIR,
      tracked_values: ["CustomTimeDilation", { path: "BaseEyeHeight", drift_threshold: 2 }],
      tracked_actors: ["SomeActor"], tracked_widgets: ["WBP_Hud"], position_threshold_cm: 7,
    });
    expect(created.created).toBe(true);
    expect(created.tracked_widgets).toEqual(["WBP_Hud"]);

    const read = await call("pie_profile_read", { path: PROFILE });
    expect((read.tracked_values as Json[]).map((v) => v.path)).toEqual(["CustomTimeDilation", "BaseEyeHeight"]);
    expect((read.tracked_values as Json[])[1].drift_threshold).toBe(2);
    expect(read.tracked_actors).toEqual(["SomeActor"]);
    expect(read.position_threshold_cm).toBe(7);

    const updated = await call("pie_profile_update", { path: PROFILE, tracked_actors: ["Other"], tracked_widgets: [], capture_montage: false });
    expect(updated.updated).toBe(true);
    const after = await call("pie_profile_read", { path: PROFILE });
    expect(after.tracked_actors).toEqual(["Other"]);
    expect(after.tracked_widgets).toEqual([]);
    expect(after.capture_montage).toBe(false);
    expect((after.tracked_values as Json[]).length).toBe(2);

    const listed = await call("pie_profile_list", { directory: DIR });
    expect((listed.profiles as Json[]).map((p) => p.name)).toContain("OP_BCovAssets");

    expect(await refused("pie_profile_delete", { path: PROFILE })).toMatch(/confirm/);
    const deleted = await call("pie_profile_delete", { path: PROFILE, confirm: true });
    expect(deleted.deleted).toBe(true);
    expect((await call("pie_profile_delete", { path: PROFILE, confirm: true })).alreadyDeleted).toBe(true);
    expect(((await call("pie_profile_list", { directory: DIR })).profiles as Json[]).length).toBe(0);
  }, 60_000);
});

describe("scenarios", () => {
  it("scenario_validate accepts a well-formed scenario and names each problem in a broken one", async () => {
    const good = await call("pie_scenario_validate", {
      scenario: { name: "ok", arrange: [], act: [], assert: [{ name: "no errors", channel: "$errors", op: "lte", value: 0, hold: "always" }] },
    });
    expect((good.validation as Json).valid).toBe(true);
    expect((good.validation as Json).assert_count).toBe(1);
    const bad = await callBridge(bridge, "pie_scenario_validate", {
      scenario: { name: "bad", arrange: [{ kind: "teleport_moon" }], act: [], assert: [{ name: "p", channel: "pos_z", op: "frobnicate" }] },
    });
    const v = body(bad).validation as Json;
    expect(v.valid).toBe(false);
    expect(JSON.stringify(v.errors)).toContain("teleport_moon");
    expect(JSON.stringify(v.errors)).toContain("frobnicate");
  }, 30_000);
});

describe("multiplayer readers with no session", () => {
  it("clients reports a session that is not networked", async () => {
    const clients = await call("pie_clients");
    expect(clients.networked).toBe(false);
    expect(clients.server_net_mode).toBe("none");
    expect(clients.clients).toEqual([]);
  });

  it("console points the editor's own player at execute_command and needs a remote player otherwise", async () => {
    expect(await refused("pie_console", { command: "stat fps", client: 0 })).toMatch(/execute_command/);
    expect(await refused("pie_console", { command: "stat fps", client: 1 })).toMatch(/remote players/i);
  });

  it("remote_result names a request that was never relayed", async () => {
    expect(await refused("pie_remote_result", { request_id: "inject-404" })).toMatch(/inject-404/);
  });

  it("uat_status reports no run in progress", async () => {
    const status = (await call("pie_uat_status")).status as Json;
    expect(["idle", "finished"]).toContain(status.state);
  });
});
