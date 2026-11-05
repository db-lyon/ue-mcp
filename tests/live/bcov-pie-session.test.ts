// @live-editor headless
/**
 * The pie category against real Play-In-Editor sessions: a recording and an
 * observation of one session with injected input and a marker, read back from
 * the files they write; the offline readers over that recording (diff, perf,
 * state replay, tests, scenarios, assertions); replays; and multiplayer
 * acceptance runs. PIE is stopped after every session.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Pie/Session";
const IA = `${DIR}/IA_BCovSession`;
const IMC = `${DIR}/IMC_BCovSession`;
const PROFILE = `${DIR}/OP_BCovSession`;
const CUBE = "BCov_Pie_SessionCube";
const SKEL = "BCov_Pie_SessionSkel";
const REC = `bcov_pie_session_${Date.now()}`;
const HOME = "/Game/MCP_Home";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitForPie(playing: boolean, tries = 60): Promise<boolean> {
  for (let i = 0; i < tries; i++) {
    if ((await pieRunning()) === playing) return true;
    await sleep(500);
  }
  return false;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitForPie(false);
  }
}

/** Polls replay_status until the replay's PIE session has ended with a result. */
async function replayFinished(): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < 120; i++) {
    status = await call("pie_replay_status");
    if (status.pie_active === false && status.last_result) return status;
    await sleep(500);
  }
  return status;
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await call("demo_go_home");
  for (const label of [CUBE, SKEL]) await callBridge(bridge, "delete_actor", { actorLabel: label });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await callBridge(bridge, "pie_record_delete", { id: REC, confirm: true });
  await call("create_input_action", { name: "IA_BCovSession", packagePath: DIR });
  await call("create_input_mapping_context", { name: "IMC_BCovSession", packagePath: DIR });
  await call("add_imc_mapping", { imcPath: IMC, inputActionPath: IA, key: "SpaceBar" });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 0, y: -14000, z: 300 } });
  await call("place_actor", { actorClass: "SkeletalMeshActor", label: SKEL, location: { x: 400, y: -14000, z: 300 } });
  await call("set_component_property", { actorLabel: SKEL, componentName: "SkeletalMeshComponent0", propertyName: "SkeletalMeshAsset", value: "/Engine/EngineMeshes/SkeletalCube.SkeletalCube" });
  await call("set_component_property", { actorLabel: SKEL, componentName: "SkeletalMeshComponent0", propertyName: "AnimationMode", value: "AnimationSingleNode" });
  await call("pie_profile_create", { name: "OP_BCovSession", package_path: DIR, tracked_values: ["CustomTimeDilation"], tracked_actors: [CUBE] });
  await call("save_level", {});
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
    await callBridge(bridge, "pie_record_delete", { id: REC, confirm: true });
  } finally {
    for (const label of [CUBE, SKEL]) await callBridge(bridge, "delete_actor", { actorLabel: label });
    await callBridge(bridge, "save_level", {});
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("arming", () => {
  it("record_arm and record_disarm move the recorder between armed and idle", async () => {
    const armed = await call("pie_record_arm", { id: `${REC}_unused` });
    expect(armed.armed).toBe(true);
    expect((await call("pie_record_status")).state).toBe("armed");
    expect((await call("pie_record_disarm")).disarmed).toBe(true);
    expect((await call("pie_record_status")).state).toBe("idle");
  });

  it("observe_arm and observe_disarm move the observer between armed and idle", async () => {
    expect((await call("pie_observe_arm", { profile: PROFILE })).armed).toBe(true);
    expect((await call("pie_observe_status")).state).toBe("armed");
    expect((await call("pie_observe_disarm")).disarmed).toBe(true);
    expect((await call("pie_observe_status")).state).toBe("idle");
  });

  it("replay_arm and replay_disarm move the replayer between armed and idle", async () => {
    expect((await call("pie_replay_arm", { steps: [{ type: "wait", duration_ms: 100 }] })).armed).toBe(true);
    expect((await call("pie_replay_status")).state).toBe("armed");
    expect((await call("pie_replay_disarm")).disarmed).toBe(true);
    expect((await call("pie_replay_status")).state).toBe("idle");
  });
});

describe("a recorded and observed session", () => {
  let playing = false;
  let observationRun = "";

  beforeAll(async () => {
    await call("pie_record_arm", { id: REC, track_values: ["CustomTimeDilation"], track_actors: [CUBE] });
    await call("pie_observe_arm", { profile: PROFILE });
    await callBridge(bridge, "pie_control", { pieAction: "start" });
    playing = await waitForPie(true);
    for (let i = 0; i < 20 && playing; i++) {
      if ((await call("pie_record_status")).state === "recording") break;
      await sleep(250);
    }
  }, 120_000);

  it("record_status and observe_status report the live session", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const rec = await call("pie_record_status");
    expect(rec.state).toBe("recording");
    expect(rec.id).toBe(REC);
    const obs = await call("pie_observe_status");
    expect(obs.state).toBe("observing");
    observationRun = String(obs.run_id);
    expect(Number(obs.frames_sampled)).toBeGreaterThan(0);
  });

  it("inject_input, inject_input_start, inject_input_update, inject_input_stop and inject_input_tape drive a bound action", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    await call("apply_mapping_context", { mappingContext: IMC, priority: 1 });
    const once = await call("pie_inject_input", { action_path: IA });
    expect(once.value_type).toBe("Boolean");
    const hold = await call("pie_inject_input_start", { action_path: IA });
    const id = String(hold.injection_id);
    expect(id).toMatch(/^hold-/);
    expect((await call("pie_inject_input_update", { injection_id: id, value_x: 0 })).updated).toBe(true);
    const stopped = await call("pie_inject_input_stop", { injection_id: id });
    expect(stopped.stopped).toBe(true);
    expect((await call("pie_inject_input_stop", { injection_id: id })).alreadyStopped).toBe(true);
    const tape = await call("pie_inject_input_tape", { action_path: IA, values: [1, 0, 1, 0, 1] });
    expect(tape.frame_count).toBe(5);
    expect(String(tape.injection_id)).toMatch(/^tape-/);
    // The recorder discovers the action once input on it has been seen.
    let tracked = 0;
    for (let i = 0; i < 20 && tracked === 0; i++) {
      tracked = Number((await call("pie_record_status")).tracked_action_count);
      if (tracked === 0) await sleep(250);
    }
    expect(tracked).toBeGreaterThan(0);
  }, 60_000);

  it("mark writes a labelled marker into the recording in flight", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const marked = await call("pie_mark", { label: "bcov_marker" });
    expect(marked.active).toBe(true);
    expect(marked.label).toBe("bcov_marker");
  });

  it("anim_state and anim_properties read a PIE actor's anim instance", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const state = await call("pie_anim_state", { actorLabel: SKEL });
    expect(state.animClass).toBe("AnimSingleNodeInstance");
    expect(state.currentMontage).toBe("None");
    const props = await call("pie_anim_properties", { actorLabel: SKEL });
    expect(Object.keys(props.properties as Json).length).toBeGreaterThan(0);
    const named = await call("pie_anim_properties", { actorLabel: SKEL, propertyNames: ["NoSuchProperty"] });
    expect((named.properties as Json).NoSuchProperty).toBe("<not found>");
  });

  it("session_log and session_errors read the live session's captured log", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const log = await call("pie_session_log", { category: "LogMCPPIE", limit: 50 });
    expect(rows(log.rows).some((r) => String(r.message).includes(REC))).toBe(true);
    const errors = await call("pie_session_errors");
    expect(errors.active).toBe(true);
    expect(typeof (errors.errors as Json).warning_count).toBe("number");
  });

  it("replay_state with apply teleports the live pawn to a recorded pose", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    // The pose is read from the finalized files, so finish the recording first.
    const done = await call("pie_record_stop");
    expect(done.id).toBe(REC);
    expect(rows(done.markers).map((m) => m.label)).toContain("bcov_marker");
    expect(done.discovered_actions).toEqual(expect.arrayContaining([expect.stringContaining("IA_BCovSession")]));
    expect((await call("pie_record_status")).state).toBe("idle");

    const applied = await call("pie_replay_state", { recording_id: REC, at_frame: 0, apply: true });
    expect(applied.applied).toBe(true);
    expect(applied.previous_pawn_state).toBeDefined();
  });

  it("observe_stop finalizes the observation into its files", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const done = await call("pie_observe_stop");
    expect(done.run_id).toBe(observationRun);
    expect(Number(done.frames_sampled)).toBeGreaterThan(0);
    expect(String(done.csv_path)).toMatch(/observation\.csv$/);
    expect(String(done.tracked_actors_path)).toMatch(/tracked\.jsonl$/);
    await stopPie();
  }, 60_000);

  it("observe_list and observe_read read the finished observation back", async (ctx) => {
    if (!observationRun) ctx.skip("no observation ran");
    const listed = await call("pie_observe_list", { limit: 50 });
    expect(rows(listed.observations).map((o) => o.run_id)).toContain(observationRun);
    const manifest = await call("pie_observe_read", { run_id: observationRun });
    expect(String(manifest.content ?? JSON.stringify(manifest))).toContain(CUBE);
    const series = await call("pie_observe_read", { run_id: observationRun, file: "series", channels: "t:CustomTimeDilation" });
    expect(JSON.stringify(series)).toContain("CustomTimeDilation");
    const tracked = await call("pie_observe_read", { run_id: observationRun, file: "tracked", limit: 2 });
    expect(JSON.stringify(tracked)).toContain(CUBE);
  });
});

describe("the finished recording", () => {
  it("record_list and record_read return its manifest, sequence and csv", async () => {
    const listed = await call("pie_record_list", { limit: 200 });
    const row = rows(listed.recordings).find((r) => r.id === REC);
    expect(row?.pawn_class).toBeDefined();
    expect(Number(row?.total_frames)).toBeGreaterThan(0);
    const manifest = JSON.parse(String((await call("pie_record_read", { id: REC })).content)) as Json;
    expect(rows(manifest.markers).map((m) => m.label)).toContain("bcov_marker");
    const sequence = await call("pie_record_read", { id: REC, file: "sequence" });
    expect(String(sequence.content)).toContain("IA_BCovSession");
    const csv = await call("pie_record_read", { id: REC, file: "csv", limit: 1 });
    expect(String(csv.content)).toContain("t:CustomTimeDilation");
  });

  it("perf_summary aggregates its frame times", async () => {
    const perf = await call("pie_perf_summary", { recording_id: REC });
    expect(Number(perf.frames)).toBeGreaterThan(0);
    expect(Number((perf.frametime as Json).p50_ms)).toBeGreaterThan(0);
  });

  it("record_diff of a recording against itself finds no drift", async () => {
    const diff = await call("pie_record_diff", { a_id: REC, b_id: REC });
    expect(diff.max_position_drift_cm).toBe(0);
    expect(Number(diff.frames_compared)).toBeGreaterThan(0);
    expect(diff.frames_over_threshold).toEqual([]);
  });

  it("replay_state without apply reads the recorded pose at a frame", async () => {
    const pose = await call("pie_replay_state", { recording_id: REC, at_frame: 0 });
    expect(pose.deterministic).toBe(true);
    expect(pose.frame).toBe(0);
    expect((pose.pawn_state as Json).location).toBeDefined();
  });

  it("test_scaffold, test_list, scenario_scaffold and assert_eval build and evaluate tests from it", async () => {
    const scaffold = await call("pie_test_scaffold", { recording_id: REC, name: "bcov", max_errors: 50 });
    expect(String(scaffold.test_path)).toMatch(/tests[\\/]bcov\.json$/);
    const listed = await call("pie_test_list", { recording_id: REC });
    expect(JSON.stringify(listed)).toContain("bcov");
    const scenario = await call("pie_scenario_scaffold", { recording_id: REC, name: "bcov_scenario" });
    expect(String(scenario.scenario_path)).toMatch(/bcov_scenario/);
    expect((scenario.validation as Json).valid).toBe(true);
    const verdict = await call("pie_assert_eval", {
      recording_id: REC,
      assertions: [
        { name: "dilation stays 1", channel: "t:CustomTimeDilation", op: "eq", value: 1, hold: "always" },
        { name: "dilation hits 5", channel: "t:CustomTimeDilation", op: "gte", value: 5, hold: "eventually" },
      ],
    });
    const results = rows(verdict.results ?? verdict.assertions);
    expect(results.map((r) => r.passed)).toEqual([true, false]);
  });
});

describe("replays", () => {
  it("replay_run drives the recording unattended, then replay_analyze and test_run read its drift", async () => {
    await stopPie();
    const started = await call("pie_replay_run", { recording_id: REC });
    expect(started.started).toBe(true);
    const status = await replayFinished();
    expect(status.pie_active).toBe(false);
    const last = status.last_result as Json;
    expect(String(last.drift_report_path)).toMatch(/drift\.json$/);
    const lead = await call("pie_replay_analyze", { recording_id: REC });
    expect(JSON.stringify(lead)).toMatch(/drift|divergence/);
    const ran = await call("pie_test_run", { recording_id: REC, name: "bcov", auto_run: false });
    expect(typeof ran.passed).toBe("boolean");
    expect(Array.isArray(ran.failures)).toBe(true);
  }, 180_000);

  it("replay_stop ends a replay in flight", async () => {
    await stopPie();
    await call("pie_replay_run", { recording_id: REC, auto_stop_pie: false, settle_ms: 3000 });
    await waitForPie(true);
    const stopped = await call("pie_replay_stop");
    expect(stopped.stopped).toBe(true);
    expect((await call("pie_replay_status")).state).not.toBe("replaying");
    await stopPie();
  }, 120_000);
});

describe("multiplayer acceptance runs", () => {
  const scenario = (name: string, pattern: string, windowMs: number) => ({
    name, map: HOME, pie: { players: 1, net_mode: "listen", one_process: true }, join_timeout_s: 60,
    steps: [{ name: "logs", do: [{ wait_ms: 200 }], expect: [{ log: "host", pattern, since: "run" }], window_ms: windowMs }],
  });

  it("uat_run judges a step from the host log and uat_status reports the verdict", async () => {
    await stopPie();
    await call("pie_uat_run", { scenario: scenario("bcov_pass", "LogMCPPIE", 3000) });
    let status: Json = {};
    for (let i = 0; i < 180; i++) {
      status = (await call("pie_uat_status")).status as Json;
      if (status.state === "finished") break;
      await sleep(500);
    }
    expect(status.state).toBe("finished");
    expect(status.verdict).toBe("PASS");
    expect(String(status.report_dir)).toContain("bcov_pass");
    await waitForPie(false);
  }, 180_000);

  it("uat_abort ends a run that is still going", async () => {
    await stopPie();
    await call("pie_uat_run", { scenario: scenario("bcov_abort", "never-logged-bcov-line", 60000) });
    for (let i = 0; i < 120; i++) {
      if (["acting", "watching"].includes(String(((await call("pie_uat_status")).status as Json).state))) break;
      await sleep(500);
    }
    const aborted = await call("pie_uat_abort", { reason: "bcov" });
    expect(aborted.was_running).toBe(true);
    let state = "";
    for (let i = 0; i < 60 && state !== "finished"; i++) {
      state = String(((await call("pie_uat_status")).status as Json).state);
      if (state !== "finished") await sleep(500);
    }
    expect(state).toBe("finished");
    await waitForPie(false);
  }, 180_000);
});

describe("deleting", () => {
  it("record_delete removes the recording folder and reports a second delete as already done", async () => {
    expect((await callBridge(bridge, "pie_record_delete", { id: REC })).ok).toBe(true);
    expect(body(await callBridge(bridge, "pie_record_delete", { id: REC })).success).toBe(false);
    const deleted = await call("pie_record_delete", { id: REC, confirm: true });
    expect(deleted.deleted).toBe(true);
    expect((await call("pie_record_delete", { id: REC, confirm: true })).alreadyDeleted).toBe(true);
    expect(rows((await call("pie_record_list", { limit: 200 })).recordings).map((r) => r.id)).not.toContain(REC);
  });
});
