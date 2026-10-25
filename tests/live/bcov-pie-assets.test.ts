// @live-editor headless
/**
 * The pie category's editor-time surface, read back after every write:
 * observation profiles and test scripts as assets, scenario validation, and
 * the multiplayer player and acceptance-run readers with no session running.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Pie/Assets";
const PROFILE = `${DIR}/OP_BCovAssets`;
const SCRIPT = `${DIR}/TS_BCovAssets`;
const CUBE = "BCov_Pie_AssetsCube";

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
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 300, y: -15000, z: 300 } });
}, 60_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
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

describe("test scripts", () => {
  const steps = [
    { instruction: "Look down", judge: "auto", expect: { value: "BaseEyeHeight", op: "decreasing", within_ms: 3000 } },
    { instruction: "The door opens smoothly", judge: "manual" },
    { instruction: "The cube is in the level", expect: { value: CUBE, op: "exists" } },
  ];

  /** The structured refusal: every problem as { step, field, message, candidates? }. */
  async function refusedWith(method: string, params: Json): Promise<{ error: string; errors: Json[] }> {
    const r = await callBridge(bridge, method, params);
    expect(succeeded(r), `${method} should be refused: ${JSON.stringify(r.result)}`).toBe(false);
    expect(body(r).errorCode).toBe("INVALID_SCRIPT_STEPS");
    return { error: String(body(r).error ?? ""), errors: (body(r).errors as Json[]) ?? [] };
  }

  async function scriptListed(name: string): Promise<boolean> {
    return JSON.stringify((await call("pie_script_list", { directory: DIR })).scripts).includes(name);
  }

  it("script_create, script_read, script_update, script_list and script_delete round-trip one asset", async () => {
    const created = await call("pie_script_create", { name: "TS_BCovAssets", package_path: DIR, steps, description: "door check", default_within_ms: 4000 });
    expect(created.created).toBe(true);

    const read = await call("pie_script_read", { path: SCRIPT });
    const readSteps = read.steps as Json[];
    expect(readSteps.map((s) => s.judge)).toEqual(["auto", "manual", "auto"]);
    expect((readSteps[0].expect as Json).op).toBe("decreasing");
    expect((readSteps[0].expect as Json).within_ms).toBe(3000);
    expect(read.description).toBe("door check");
    expect(read.default_within_ms).toBe(4000);
    expect(read.manual_only).toBe(false);

    // Cut down to a manual step, the script checks nothing until it says so.
    const noAuto = await refusedWith("pie_script_update", { path: SCRIPT, steps: [steps[1]], description: "manual only" });
    expect(noAuto.errors).toEqual([expect.objectContaining({ step: null, field: "steps", candidates: ["manual_only"] })]);
    expect(((await call("pie_script_read", { path: SCRIPT })).steps as Json[]).length).toBe(3);

    const updated = await call("pie_script_update", { path: SCRIPT, steps: [steps[1]], description: "manual only", manual_only: true });
    expect(updated.updated).toBe(true);
    const after = await call("pie_script_read", { path: SCRIPT });
    expect((after.steps as Json[]).length).toBe(1);
    expect(after.description).toBe("manual only");
    expect(after.manual_only).toBe(true);

    // Clearing manual_only checks the stored steps again.
    expect((await refusedWith("pie_script_update", { path: SCRIPT, manual_only: false })).errors[0]).toMatchObject({ step: null, field: "steps" });
    expect((await call("pie_script_read", { path: SCRIPT })).manual_only).toBe(true);

    expect(await scriptListed("TS_BCovAssets")).toBe(true);

    const deleted = await call("pie_script_delete", { path: SCRIPT, confirm: true });
    expect(deleted.deleted ?? deleted.alreadyDeleted === false).toBe(true);
    expect(await scriptListed("TS_BCovAssets")).toBe(false);
  }, 60_000);

  it("script_create refuses a step whose expectation cannot be evaluated", async () => {
    const error = await refused("pie_script_create", {
      name: "TS_BCovBad", package_path: DIR, steps: [{ instruction: "x", expect: { value: "BaseEyeHeight", op: "eq" } }],
    });
    expect(error).toMatch(/target/i);
    const badOp = await refusedWith("pie_script_create", {
      name: "TS_BCovBad", package_path: DIR, steps: [{ instruction: "x", expect: { value: "BaseEyeHeight", op: "sideways" } }],
    });
    expect(badOp.errors[0]).toMatchObject({ step: 0, field: "expect.op" });
    expect(await scriptListed("TS_BCovBad")).toBe(false);
  }, 30_000);

  it("script_create refuses a value the observer cannot read, naming the step, the value and the closest candidates", async () => {
    const r = await refusedWith("pie_script_create", {
      name: "TS_BCovUnknown", package_path: DIR,
      steps: [
        { instruction: "Look", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } },
        { instruction: "Typo", expect: { value: "BaseEyeHieght", op: "gt", target: 0 } },
        { instruction: "Wrong actor", expect: { value: `${CUBE}x`, op: "exists" } },
        { instruction: "No widget", expect: { value: "WBP_BCovNoSuchWidget", op: "visible" } },
      ],
    });
    expect(r.errors.map((e) => [e.step, e.field])).toEqual([[1, "expect.value"], [2, "expect.value"], [3, "expect.value"]]);
    expect(String(r.errors[0].message)).toContain("BaseEyeHieght");
    expect(r.errors[0].candidates).toContain("BaseEyeHeight");
    expect(r.errors[1].candidates).toContain(CUBE);
    expect(String(r.errors[2].message)).toContain("resolve_in_pie");
    expect(r.error).toContain("BaseEyeHieght");
    expect(await scriptListed("TS_BCovUnknown")).toBe(false);
  }, 60_000);

  it("script_create refuses an op or target the value's type cannot meet", async () => {
    const r = await refusedWith("pie_script_create", {
      name: "TS_BCovTypes", package_path: DIR,
      steps: [
        { instruction: "A bool compared as a number", expect: { value: "bCanBeDamaged", op: "gt", target: 0 } },
        { instruction: "A float against true", expect: { value: "BaseEyeHeight", op: "gt", target: true } },
        { instruction: "exists against 2", expect: { value: CUBE, op: "exists", target: 2 } },
        { instruction: "A bool against true", expect: { value: "bCanBeDamaged", op: "eq", target: true } },
      ],
    });
    expect(r.errors.map((e) => [e.step, e.field])).toEqual([[0, "expect.op"], [1, "expect.target"], [2, "expect.target"]]);
    expect(r.errors[0].candidates).toEqual(["eq"]);
    expect(await scriptListed("TS_BCovTypes")).toBe(false);
  }, 30_000);

  it("resolve_in_pie lets a value through that only exists once PIE runs, and is stored on the step", async () => {
    const later = `${DIR}/TS_BCovLater`;
    const spawned = { instruction: "The spawned helper appears", expect: { value: "BP_BCovSpawnedLater", op: "exists" } };
    expect((await refusedWith("pie_script_create", { name: "TS_BCovLater", package_path: DIR, steps: [spawned] })).errors[0])
      .toMatchObject({ step: 0, field: "expect.value" });

    const created = await call("pie_script_create", { name: "TS_BCovLater", package_path: DIR, steps: [{ ...spawned, resolve_in_pie: true }] });
    expect(created.created).toBe(true);
    const read = await call("pie_script_read", { path: later });
    expect((read.steps as Json[])[0]).toMatchObject({ resolve_in_pie: true, expect: { value: "BP_BCovSpawnedLater", op: "exists" } });

    // An update naming an unreadable value is refused and leaves the stored steps alone.
    const bad = await refusedWith("pie_script_update", {
      path: later, steps: [{ instruction: "Typo", expect: { value: "BaseEyeHieght", op: "lt", target: 500 } }],
    });
    expect(bad.errors[0]).toMatchObject({ step: 0, field: "expect.value" });
    expect(((await call("pie_script_read", { path: later })).steps as Json[])[0].resolve_in_pie).toBe(true);
    await call("pie_script_delete", { path: later, confirm: true });
  }, 60_000);

  it("script_create refuses a script with no auto step unless it is manual_only", async () => {
    const manual = [{ instruction: "Does the menu feel right?", judge: "manual" }];
    const r = await refusedWith("pie_script_create", { name: "TS_BCovManual", package_path: DIR, steps: manual });
    expect(r.errors).toEqual([expect.objectContaining({ step: null, field: "steps" })]);
    expect(await scriptListed("TS_BCovManual")).toBe(false);

    const created = await call("pie_script_create", { name: "TS_BCovManual", package_path: DIR, steps: manual, manual_only: true });
    expect(created.created).toBe(true);
    expect((await call("pie_script_read", { path: `${DIR}/TS_BCovManual` })).manual_only).toBe(true);
    await call("pie_script_delete", { path: `${DIR}/TS_BCovManual`, confirm: true });
  }, 30_000);
});

describe("draft checks without saving", () => {
  it("script_validate answers the checks script_create runs, and saves nothing", async () => {
    const bad = await call("pie_script_validate", {
      steps: [
        { instruction: "Typo", expect: { value: "BaseEyeHieght", op: "gt", target: 0 } },
        { instruction: "A bool compared as a number", expect: { value: "bCanBeDamaged", op: "lt", target: 1 } },
      ],
    });
    expect(bad.valid).toBe(false);
    const errors = bad.errors as Json[];
    expect(errors.map((e) => [e.step, e.field])).toEqual([[0, "expect.value"], [1, "expect.op"]]);
    expect(errors[0].candidates).toContain("BaseEyeHeight");
    expect(String(bad.summary)).toContain("BaseEyeHieght");

    const good = await call("pie_script_validate", {
      steps: [{ instruction: "Look", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } }],
    });
    expect(good).toMatchObject({ valid: true, errors: [] });
    expect((good.steps as Json[])[0]).toMatchObject({ judge: "auto", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } });

    const manual = await call("pie_script_validate", { steps: [{ instruction: "Feels right?", judge: "manual" }] });
    expect((manual.errors as Json[])[0]).toMatchObject({ step: null, field: "steps" });
    expect((await call("pie_script_validate", { steps: [{ instruction: "Feels right?", judge: "manual" }], manual_only: true })).valid).toBe(true);
    expect((await call("pie_script_list", { directory: DIR })).count).toBe(0);
  }, 30_000);

  it("script_values lists what a script can name, each of which script_validate accepts", async () => {
    const values = await call("pie_script_values", { target: "/Script/Engine.DefaultPawn", limit: 400 });
    const paths = (values.values as Json[]).map((v) => String(v.path));
    expect(paths).toContain("BaseEyeHeight");
    expect((values.values as Json[]).find((v) => v.path === "bCanBeDamaged")).toMatchObject({ kind: "boolean", type: "bool" });
    expect(values.actors as string[]).toContain(CUBE);
    expect((values.pawn as Json).class).toBeTruthy();
    expect(values.target).toMatchObject({ requested: "/Script/Engine.DefaultPawn", found: true });

    // A sample of the listed numbers reads back as valid steps.
    const numbers = (values.values as Json[]).filter((v) => v.kind === "number").slice(0, 10);
    expect(numbers.length).toBeGreaterThan(0);
    const check = await call("pie_script_validate", {
      steps: [
        ...numbers.map((v) => ({ instruction: `Watch ${v.path}`, expect: { value: v.path, op: "increasing" } })),
        { instruction: "The cube is there", expect: { value: CUBE, op: "exists" } },
      ],
    });
    expect(check.errors).toEqual([]);
    expect(await refused("pie_script_values", { profile: "/Game/MCPTest/NoSuchProfile" })).toMatch(/Profile not found/);
  }, 30_000);
});

describe("manual runs", () => {
  const MANUAL = `${DIR}/TS_BCovManualRun`;
  const steps = [
    { instruction: "Look up", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } },
    { instruction: "The door looks right", judge: "manual" },
    { instruction: "The cube is there", expect: { value: CUBE, op: "exists" } },
    { instruction: "The sound plays", judge: "manual" },
  ];
  const isPlaying = async () => body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;

  it("script_run mode=manual starts no PIE, takes verdicts in any order and writes results.json", async () => {
    await call("pie_script_create", { name: "TS_BCovManualRun", package_path: DIR, steps });
    const started = await call("pie_script_run", { script: MANUAL, mode: "manual", start_pie: true });
    const runId = String(started.run_id);
    expect(started).toMatchObject({ mode: "manual", state: "awaiting_verdict", pie_started: false });
    expect(started.recording_id).toBeUndefined();
    expect(await isPlaying()).toBe(false);
    const first = started.steps as Json[];
    expect(first.map((s) => s.status)).toEqual(["pending", "awaiting_verdict", "pending", "awaiting_verdict"]);
    expect((started.awaiting as Json[]).map((a) => a.index)).toEqual([1, 3]);
    expect(started.summary).toMatchObject({ unchecked: 2, pending: 2, awaiting: 2 });

    // The last manual step first, then the other.
    expect((await call("pie_script_verdict", { run_id: runId, step: 3, verdict: "fail", note: "silent" })).state).toBe("awaiting_verdict");
    expect(await refused("pie_script_verdict", { run_id: runId, step: 0, verdict: "pass" })).toMatch(/auto step/);
    const done = await call("pie_script_verdict", { run_id: runId, step: 1, verdict: "pass" });
    expect(done.state).toBe("finished");

    const results = await call("pie_script_results", { run_id: runId });
    expect(results).toMatchObject({ mode: "manual", state: "finished", stop_reason: "all manual steps settled" });
    expect(results.summary).toMatchObject({ passed: 1, failed: 1, unchecked: 2, pending: 0 });
    expect((results.steps as Json[]).map((s) => s.status)).toEqual(["pending", "passed", "pending", "failed"]);
    const status = await call("pie_script_status", { run_id: runId });
    const events = status.events as Json[];
    expect(events.length).toBeGreaterThan(3);
    expect(events.every((e) => (e.data as Json).mode === "manual")).toBe(true);
    expect(events.filter((e) => e.type === "awaiting_verdict").map((e) => (e.data as Json).blocking)).toEqual([false]);
    expect(await isPlaying()).toBe(false);

    // A full run of the same script is its own run, with none of those verdicts.
    const full = await call("pie_script_run", { script: MANUAL, record: false });
    expect(full).toMatchObject({ mode: "full", state: "armed" });
    expect(full.run_id).not.toBe(runId);
    expect((full.steps as Json[]).every((s) => s.status === "pending" && s.verdict_source === undefined)).toBe(true);
    expect(full.summary).toMatchObject({ unchecked: 0, passed: 0, failed: 0 });
    await call("pie_script_stop", { run_id: full.run_id, reason: "test" });
  }, 60_000);

  it("script_runs lists a script's runs newest first with their steps, and script_list names each script's last run", async () => {
    const LISTED = `${DIR}/TS_BCovRunList`;
    const NEVER = `${DIR}/TS_BCovRunListNever`;
    const checks = [
      { instruction: "The door opens", judge: "manual" },
      { instruction: "The light is on", judge: "manual" },
    ];
    await call("pie_script_create", { name: "TS_BCovRunList", package_path: DIR, steps: checks, manual_only: true });
    await call("pie_script_create", { name: "TS_BCovRunListNever", package_path: DIR, steps: checks, manual_only: true });

    // Two finished runs, then one left awaiting its verdicts.
    const first = String((await call("pie_script_run", { script: LISTED, mode: "manual" })).run_id);
    await call("pie_script_verdict", { run_id: first, step: 0, verdict: "pass" });
    expect((await call("pie_script_verdict", { run_id: first, step: 1, verdict: "pass" })).state).toBe("finished");
    const second = String((await call("pie_script_run", { script: LISTED, mode: "manual" })).run_id);
    await call("pie_script_verdict", { run_id: second, step: 0, verdict: "fail", note: "stuck" });
    expect((await call("pie_script_verdict", { run_id: second, step: 1, verdict: "skip" })).state).toBe("finished");
    const live = String((await call("pie_script_run", { script: LISTED, mode: "manual" })).run_id);

    try {
      const listed = await call("pie_script_runs", { script: `${LISTED}.TS_BCovRunList` });
      expect(listed.script).toBe(LISTED);
      const runs = listed.runs as Json[];
      expect(runs.every((r) => String(r.script).startsWith(LISTED))).toBe(true);
      expect(runs.slice(0, 3).map((r) => r.run_id)).toEqual([live, second, first]);
      expect(listed.total).toBeGreaterThanOrEqual(3);
      expect(runs[0]).toMatchObject({ state: "awaiting_verdict", mode: "manual", live: true, script_name: "TS_BCovRunList" });
      expect(runs[1]).toMatchObject({ state: "finished", live: false, summary: { failed: 1, skipped: 1, passed: 0 } });
      expect(runs[2]).toMatchObject({ state: "finished", live: false, summary: { passed: 2, failed: 0 } });
      expect((runs[1].steps as Json[]).map((s) => [s.index, s.status, s.note ?? null])).toEqual([[0, "failed", "stuck"], [1, "skipped", null]]);
      expect((runs[0].steps as Json[]).map((s) => s.status)).toEqual(["awaiting_verdict", "awaiting_verdict"]);
      for (const r of runs.slice(1, 3)) {
        expect(String(r.results_path)).toMatch(/results\.json$/);
        expect(String(r.started_at) <= String(r.ended_at)).toBe(true);
        // The path it names is the run script_results reads.
        expect((await call("pie_script_results", { results_path: r.results_path })).run_id).toBe(r.run_id);
      }

      const one = await call("pie_script_runs", { script: LISTED, limit: 1, include_steps: false });
      expect((one.runs as Json[]).map((r) => r.run_id)).toEqual([live]);
      expect((one.runs as Json[])[0].steps).toBeUndefined();
      expect(one.count).toBe(1);

      // Unfiltered, other scripts' runs are listed too, still newest first.
      const all = (await call("pie_script_runs", { limit: 200, include_steps: false })).runs as Json[];
      expect(all[0].run_id).toBe(live);
      const starts = all.map((r) => String(r.started_at ?? ""));
      expect(starts.filter((s) => s !== "")).toEqual([...starts.filter((s) => s !== "")].sort().reverse());
      expect((await call("pie_script_runs", { script: NEVER })).runs).toEqual([]);

      const scripts = (await call("pie_script_list", { directory: DIR })).scripts as Json[];
      const last = scripts.find((s) => s.path === LISTED)?.last_run as Json;
      expect(last).toMatchObject({ run_id: live, state: "awaiting_verdict", live: true });
      expect(last.steps).toBeUndefined();
      expect(scripts.find((s) => s.path === NEVER)?.last_run).toBeNull();
    } finally {
      await call("pie_script_stop", { run_id: live, reason: "test" });
    }

    // Stopped, the run reads from its results.json and is still the script's last run.
    const after = (await call("pie_script_list", { directory: DIR })).scripts as Json[];
    expect(after.find((s) => s.path === LISTED)?.last_run).toMatchObject({ run_id: live, state: "stopped", live: false });
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
