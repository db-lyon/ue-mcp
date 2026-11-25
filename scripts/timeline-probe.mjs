#!/usr/bin/env node
// Determinism probe for timeline playtests (#2097, phase 1).
//
// Records the probe arena's autopilot once (a character walking, jumping and
// steering with the stick, a physics crate in its path, an AI-controlled chaser,
// an action with a Hold trigger and one with a Scalar modifier), replays it
// --replays times at a fixed timestep and --variable times without one, and
// reports the drift of each replay. With --splice it also replays the first
// --cut seconds with lock_input, hands the controls back to the autopilot, records
// the result and checks the state is continuous at the cut.
//
// --clip records the autopilot into a playtest clip with pie(clip_record) and replays it with pie(clip_run), with
// --splice punching in on a region; --menu presses a menu button through a clip's key event. Both need the clip
// actions.
//
// Runs only against the editor on this checkout's tests/ue_mcp (the bridge guard
// refuses anything else). Writes the measurements to --out as JSON.
//
//   node scripts/timeline-probe.mjs --replays 10 --variable 3 --splice --out probe.json
//   node scripts/timeline-probe.mjs --skip-setup --no-crate --no-chaser --replays 10 --out alone.json
import { tsImport } from "tsx/esm/api";
import fs from "node:fs";
import path from "node:path";

const { getBridge, callBridge, disconnectBridge } = await tsImport("../tests/setup.ts", import.meta.url);

const args = process.argv.slice(2);
const flag = (name) => args.includes(`--${name}`);
const opt = (name, def) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const REPLAYS = Number(opt("replays", "10"));
const VARIABLE = Number(opt("variable", "0"));
const SECONDS = Number(opt("seconds", "30"));
const CUT = Number(opt("cut", "10"));
const LABEL = opt("label", "probe");
const OUT = opt("out", "");
const SKIP_SETUP = flag("skip-setup");
const SOURCE = opt("source", "");

const DIR = "/Game/MCPTest/Probe";
const LEVEL = `${DIR}/L_Probe`;
const RECORDINGS = path.resolve("tests/ue_mcp/Saved/MCPRecordings");
const TRACK_VALUES = ["ChargeCount", "ThrottleSum", "JumpCount"];
const TRACK_ACTORS = ["ProbeCrate", "ProbeChaser"];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (...m) => console.log(`[probe ${new Date().toISOString().slice(11, 19)}]`, ...m);

const bridge = await getBridge();

async function call(method, params = {}, { allowFail = false } = {}) {
  const r = await callBridge(bridge, method, params);
  const body = r.result && typeof r.result === "object" ? r.result : {};
  if (!allowFail && (!r.ok || body.success === false)) {
    throw new Error(`${method} failed: ${r.error ?? JSON.stringify(r.result).slice(0, 600)}`);
  }
  return body;
}

async function pieRunning() {
  return (await call("pie_control", { pieAction: "status" }, { allowFail: true })).isPlaying === true;
}

async function stopPie() {
  if (await pieRunning()) {
    await call("pie_control", { pieAction: "stop" }, { allowFail: true });
    for (let i = 0; i < 120 && (await pieRunning()); i++) await sleep(250);
  }
}

async function waitForPawn(className = "MCPProbeCharacter") {
  for (let i = 0; i < 240; i++) {
    const pawn = await call("get_pie_pawn", {}, { allowFail: true });
    if (typeof pawn.class === "string" && pawn.class.includes(className)) return pawn;
    if (typeof pawn.path === "string" && pawn.path.includes("ProbeCharacter")) return pawn;
    await sleep(250);
  }
  throw new Error("the probe character never became the player pawn");
}

async function setup() {
  await stopPie();
  await call("delete_folder", { path: DIR, force: true }, { allowFail: true });
  for (const [name, valueType] of [
    ["IA_ProbeMove", "Axis2D"],
    ["IA_ProbeJump", "Boolean"],
    ["IA_ProbeCharge", "Boolean"],
    ["IA_ProbeThrottle", "Axis1D"],
  ]) {
    await call("create_input_action", { name, packagePath: DIR, valueType });
  }
  const ia = (n) => `${DIR}/${n}.${n}`;
  await call("set_action_triggers", {
    inputActionPath: ia("IA_ProbeCharge"),
    triggers: [{ class: "/Script/EnhancedInput.InputTriggerHold", properties: { HoldTimeThreshold: 0.5, bIsOneShot: true } }],
  });
  await call("set_action_triggers", {
    inputActionPath: ia("IA_ProbeThrottle"),
    modifiers: [{ class: "/Script/EnhancedInput.InputModifierScalar", properties: { Scalar: { X: 2.5, Y: 2.5, Z: 2.5 } } }],
  });
  await call("create_input_mapping_context", { name: "IMC_Probe", packagePath: DIR });
  const imc = `${DIR}/IMC_Probe.IMC_Probe`;
  const mappings = [
    ["IA_ProbeMove", "W", [{ class: "/Script/EnhancedInput.InputModifierSwizzleAxis", properties: { Order: "YXZ" } }]],
    ["IA_ProbeMove", "S", [{ class: "/Script/EnhancedInput.InputModifierSwizzleAxis", properties: { Order: "YXZ" } }, { class: "/Script/EnhancedInput.InputModifierNegate" }]],
    ["IA_ProbeMove", "D", []],
    ["IA_ProbeMove", "A", [{ class: "/Script/EnhancedInput.InputModifierNegate" }]],
    ["IA_ProbeMove", "Gamepad_Left2D", []],
    ["IA_ProbeJump", "SpaceBar", []],
    ["IA_ProbeCharge", "Q", []],
    ["IA_ProbeThrottle", "E", []],
  ];
  for (let i = 0; i < mappings.length; i++) {
    const [action, key, modifiers] = mappings[i];
    await call("add_imc_mapping", { imcPath: imc, inputActionPath: ia(action), key });
    if (modifiers.length) await call("set_mapping_modifiers", { imcPath: imc, mappingIndex: i, modifiers });
  }
  // A level of its own, so nothing else in the open level touches the arena.
  await call("save_level_as", { levelPath: LEVEL }, { allowFail: true });
  await call("place_actor", { actorClass: "/Script/ue_mcp.MCPProbeArena", label: "ProbeArena", location: { x: 0, y: 40000, z: 4000 } });
  await call("save_level", {}, { allowFail: true });
}

function readCsv(file) {
  const lines = fs.readFileSync(file, "utf8").split(/\r?\n/).filter((l) => l && !l.startsWith("#"));
  const header = lines[0].split(",");
  return lines.slice(1).map((l) => {
    const cols = l.split(",");
    const row = {};
    header.forEach((h, i) => (row[h] = Number(cols[i])));
    return row;
  });
}

async function record(id, { startAt = 0 } = {}) {
  await stopPie();
  await call("pie_record_arm", { id, sample_hz: 60, track_values: TRACK_VALUES, track_actors: TRACK_ACTORS });
  await call("pie_control", { pieAction: "start" });
  await waitForPawn();
  await call("invoke_function", { world: "pie", actorLabel: "ProbeCharacter", functionName: "StartAutopilot", args: { StartAtSeconds: startAt } });
  await sleep(SECONDS * 1000 + 1000);
  await stopPie();
  return path.join(RECORDINGS, id);
}

async function waitReplayDone(timeoutMs = SECONDS * 4000 + 60000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const s = await call("pie_replay_status", {}, { allowFail: true });
    if (s.pie_active === false && s.state !== "replaying" && s.state !== "waiting_for_pawn" && s.state !== "armed") return s;
    await sleep(500);
  }
  throw new Error("replay did not finish");
}

function summarizeDrift(dir) {
  const drift = JSON.parse(fs.readFileSync(path.join(dir, "drift.json"), "utf8"));
  const over = drift.frames_over_threshold ?? [];
  const first = over.length ? over[0].time : null;
  const replayRows = fs.existsSync(path.join(dir, "replay.csv")) ? readCsv(path.join(dir, "replay.csv")) : [];
  const last = replayRows.length ? replayRows[replayRows.length - 1] : {};
  return {
    frames_compared: drift.frames_compared,
    max_position_drift_cm: drift.max_position_drift_cm,
    max_velocity_drift_cms: drift.max_velocity_drift_cms,
    max_rotation_drift_deg: drift.max_rotation_drift_deg,
    frames_over_threshold: drift.frames_over_threshold_total ?? over.length,
    first_over_threshold_s: first,
    first_divergence: drift.summary?.first_divergence ?? null,
    input_onset: drift.input_onset ?? null,
    tracked_value_max_deltas: drift.tracked_value_max_deltas ?? {},
    actor_drift: drift.actor_drift ?? {},
    replay_final: Object.fromEntries(TRACK_VALUES.map((v) => [v, last[`t:${v}`]])),
  };
}

async function replay(id, { fixed, label }) {
  await stopPie();
  await call("pie_replay_run", { recording_id: id, fixed_timestep: fixed, settle_ms: 0, auto_stop_pie: true });
  await waitReplayDone();
  const s = summarizeDrift(path.join(RECORDINGS, id));
  log(`${label}: max pos ${s.max_position_drift_cm?.toFixed(2)} cm, first over ${s.first_over_threshold_s ?? "never"}, ` +
    `deltas ${JSON.stringify(s.tracked_value_max_deltas)}, crate ${s.actor_drift?.ProbeCrate?.max_position_cm?.toFixed?.(1)}, ` +
    `chaser ${s.actor_drift?.ProbeChaser?.max_position_cm?.toFixed?.(1)}`);
  return s;
}

async function splice(sourceId) {
  // Replay the first CUT seconds with the player's input locked, then hand over.
  const dir = path.join(RECORDINGS, sourceId);
  const seq = JSON.parse(fs.readFileSync(path.join(dir, "sequence.json"), "utf8"));
  const cutMs = CUT * 1000;
  const hz = seq.sample_hz ?? 60;
  const steps = [];
  for (const s of seq.steps) {
    if (s.delay_ms >= cutMs) continue;
    const c = { ...s };
    if (c.type === "hold") c.duration_ms = Math.min(c.duration_ms, cutMs - c.delay_ms);
    if (c.type === "input_tape") c.values = c.values.slice(0, Math.max(1, Math.floor(((cutMs - c.delay_ms) * hz) / 1000)));
    steps.push(c);
  }
  const preroll = { ...seq, steps };
  const prerollPath = path.join(dir, "preroll.json");
  fs.writeFileSync(prerollPath, JSON.stringify(preroll, null, 2));

  const spliceId = `${sourceId}_splice`;
  await stopPie();
  await call("pie_record_arm", { id: spliceId, sample_hz: 60, track_values: TRACK_VALUES, track_actors: TRACK_ACTORS });
  await call("pie_replay_arm", { sequence_path: prerollPath, settle_ms: 0, lock_input: true, fixed_timestep: false, auto_stop_pie: false, record_drift: false });
  await call("pie_control", { pieAction: "start" });
  await waitForPawn();
  // Wait for the pre-roll to finish, then the autopilot takes over where the source had it at the cut.
  const t0 = Date.now();
  let handoverWallMs = null;
  while (Date.now() - t0 < cutMs + 30000) {
    const s = await call("pie_replay_status", {}, { allowFail: true });
    if (s.state === "completed" || s.state === "idle") {
      handoverWallMs = Date.now() - t0;
      break;
    }
    await sleep(20);
  }
  await call("invoke_function", { world: "pie", actorLabel: "ProbeCharacter", functionName: "StartAutopilot", args: { StartAtSeconds: CUT } });
  await sleep((SECONDS - CUT) * 1000 + 1000);
  await stopPie();

  const src = readCsv(path.join(dir, "recording.csv"));
  const spl = readCsv(path.join(RECORDINGS, spliceId, "recording.csv"));
  const t = (rows) => rows.map((r) => r.time - rows[0].time);
  const ts = t(src);
  const tp = t(spl);
  const near = (times, x) => times.reduce((best, v, i) => (Math.abs(v - x) < Math.abs(times[best] - x) ? i : best), 0);
  const pos = (r) => [r.pos_x, r.pos_y, r.pos_z];
  const vel = (r) => [r.vel_x, r.vel_y, r.vel_z];
  const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
  // Pre-roll faithfulness: the spliced take against the source before the cut.
  let prerollMax = 0;
  for (let i = 0; i < tp.length && tp[i] < CUT - 0.05; i++) {
    prerollMax = Math.max(prerollMax, dist(pos(spl[i]), pos(src[near(ts, tp[i])])));
  }
  // Continuity at the cut: the largest frame-to-frame jump around it against the largest elsewhere in the take.
  const step = (rows, i) => dist(pos(rows[i]), pos(rows[i - 1])) / Math.max(1e-4, rows[i].time - rows[i - 1].time);
  const cutIdx = near(tp, CUT);
  let jumpAtCut = 0;
  for (let i = Math.max(1, cutIdx - 30); i < Math.min(spl.length, cutIdx + 30); i++) jumpAtCut = Math.max(jumpAtCut, step(spl, i));
  let jumpElsewhere = 0;
  for (let i = 1; i < spl.length; i++) if (Math.abs(i - cutIdx) > 30) jumpElsewhere = Math.max(jumpElsewhere, step(spl, i));
  const velAtCut = dist(vel(spl[cutIdx]), vel(src[near(ts, CUT)]));

  // The spliced take is a recording like any other: it must replay as well as the source does.
  const replays = [];
  for (let i = 0; i < 3; i++) replays.push(await replay(spliceId, { fixed: true, label: `splice replay ${i + 1}` }));
  return {
    cut_s: CUT,
    handover_wall_ms: handoverWallMs,
    preroll_max_position_delta_cm: prerollMax,
    max_speed_near_cut_cms: jumpAtCut,
    max_speed_elsewhere_cms: jumpElsewhere,
    velocity_delta_vs_source_at_cut_cms: velAtCut,
    replays,
  };
}

// --clip: the same autopilot recorded into a playtest clip with clip_record, then replayed with clip_run. The run's
// drift is measured against the take's own pawn track, the clip's reference.
async function clipProbe() {
  const clipPath = `${DIR}/CL_Probe_${LABEL}`;
  // Zero-length checks near the end read where the crate and the chaser are, in the take and in every replay.
  const at = SECONDS * 60 - 90;
  const probes = {
    crate_x: "ProbeCrate.StaticMeshComponent.RelativeLocation.X",
    crate_y: "ProbeCrate.StaticMeshComponent.RelativeLocation.Y",
    chaser_x: "ProbeChaser.RootComponent.RelativeLocation.X",
    chaser_y: "ProbeChaser.RootComponent.RelativeLocation.Y",
  };
  const lines = ["clip rate=60", "check c1 0 0 by_end pos_z gt -100000"];
  Object.entries(probes).forEach(([name, value], i) => lines.push(`check p${i + 1} ${at} 0 by_end ${value} gt -1000000000 name=${name} resolve_in_pie=true`));
  await call("pie_clip_create", { name: `CL_Probe_${LABEL}`, package_path: DIR, text: lines.join("\n") + "\n", onConflict: "replace" });
  const readProbes = (s) => Object.fromEntries((s.checks ?? []).filter((c) => c.name).map((c) => [c.name, c.evidence?.last]));
  let takeProbes = {};
  await stopPie();
  const rec = await call("pie_clip_record", { path: clipPath });
  for (let i = 0; i < 400; i++) {
    const s = await call("pie_clip_status", { run_id: rec.run_id }, { allowFail: true });
    if (s.state === "running" && s.take?.punched_in) break;
    await sleep(100);
  }
  await call("invoke_function", { world: "pie", actorLabel: "ProbeCharacter", functionName: "StartAutopilot", args: { StartAtSeconds: 0 } });
  await sleep(SECONDS * 1000);
  const stopped = await call("pie_clip_record_stop", {});
  await stopPie();
  takeProbes = readProbes(stopped);
  log(`take ${rec.run_id}: ${stopped.take?.samples} frames, saved ${stopped.take?.saved}, at frame ${at} ${JSON.stringify(takeProbes)}`);
  const runs = [];
  for (let i = 0; i < REPLAYS; i++) {
    await stopPie();
    const started = await call("pie_clip_run", { path: clipPath, start_pie: true });
    let s = {};
    for (let j = 0; j < SECONDS * 40 + 600; j++) {
      s = await call("pie_clip_status", { run_id: started.run_id }, { allowFail: true });
      if (s.state === "finished" || s.state === "stopped") break;
      await sleep(100);
    }
    await stopPie();
    const p = readProbes(s);
    const crate = Math.hypot((p.crate_x ?? 0) - (takeProbes.crate_x ?? 0), (p.crate_y ?? 0) - (takeProbes.crate_y ?? 0));
    const chaser = Math.hypot((p.chaser_x ?? 0) - (takeProbes.chaser_x ?? 0), (p.chaser_y ?? 0) - (takeProbes.chaser_y ?? 0));
    runs.push({ ...s.drift, crate_cm: crate, chaser_cm: chaser, passed: s.passed });
    log(`clip replay ${i + 1}/${REPLAYS}: ${JSON.stringify(s.drift)} crate ${crate.toFixed(2)} cm chaser ${chaser.toFixed(2)} cm`);
  }
  const out = { take: rec.run_id, samples: stopped.take?.samples, runs };
  if (flag("splice")) out.splice = await clipSplice(clipPath);
  return out;
}

async function runClip(clipPath, extra = {}) {
  await stopPie();
  const started = await call("pie_clip_run", { path: clipPath, start_pie: true, ...extra });
  let s = {};
  for (let j = 0; j < SECONDS * 40 + 600; j++) {
    s = await call("pie_clip_status", { run_id: started.run_id }, { allowFail: true });
    if (s.state === "finished" || s.state === "stopped") break;
    await sleep(100);
  }
  await stopPie();
  return s;
}

// Punch-in on a recorded clip: replay to the cut, hand the controls to the autopilot, record a region, then check
// the state is continuous at the cut and the comped clip replays against its new take.
async function clipSplice(clipPath) {
  const cut = CUT * 60;
  const end = Math.min(SECONDS * 60 - 120, cut + 600);
  await call("pie_clip_edit", { path: clipPath, ops: [
    { op: "add", kind: "region", name: "Before", start: 0, end: cut },
    { op: "add", kind: "region", name: "Retake", start: cut, end },
    { op: "add", kind: "region", name: "After", start: end, end: SECONDS * 60 },
  ] });
  await stopPie();
  const rec = await call("pie_clip_record", { path: clipPath, region: "r2" });
  for (let i = 0; i < 2000; i++) {
    const s = await call("pie_clip_status", { run_id: rec.run_id }, { allowFail: true });
    if (s.take?.punched_in) break;
    await sleep(20);
  }
  await call("invoke_function", { world: "pie", actorLabel: "ProbeCharacter", functionName: "StartAutopilot", args: { StartAtSeconds: CUT } });
  let s = {};
  for (let j = 0; j < SECONDS * 40 + 600; j++) {
    s = await call("pie_clip_status", { run_id: rec.run_id }, { allowFail: true });
    if (s.state === "finished" || s.state === "stopped") break;
    await sleep(100);
  }
  await stopPie();
  const track = JSON.parse(fs.readFileSync(path.resolve("tests/ue_mcp/Saved/MCPObservations", rec.run_id, "track.json"), "utf8"));
  // Speed from position deltas; the largest frame-to-frame change in speed near the cut against everywhere else.
  const speeds = [];
  for (let i = 1; i < track.length; i++) {
    const dt = (track[i][0] - track[i - 1][0]) / 60;
    if (dt > 0) speeds.push([track[i][0], Math.hypot(track[i][1] - track[i - 1][1], track[i][2] - track[i - 1][2]) / dt]);
  }
  let nearCut = 0, elsewhere = 0;
  for (let i = 1; i < speeds.length; i++) {
    const jump = Math.abs(speeds[i][1] - speeds[i - 1][1]);
    if (Math.abs(speeds[i][0] - cut) <= 3) nearCut = Math.max(nearCut, jump);
    else elsewhere = Math.max(elsewhere, jump);
  }
  const replays = [];
  for (let i = 0; i < 3; i++) {
    const r = await runClip(clipPath);
    replays.push(r.drift);
    log(`splice replay ${i + 1}/3: ${JSON.stringify(r.drift)}`);
  }
  log(`splice at frame ${cut}: speed change at the cut ${nearCut.toFixed(1)} cm/s, largest elsewhere ${elsewhere.toFixed(1)} cm/s, take saved ${s.take?.saved}`);
  return { cut, end, take: rec.run_id, saved: s.take?.saved, speed_change_at_cut_cms: nearCut, largest_speed_change_elsewhere_cms: elsewhere, replays };
}

// --menu: a clip opens the arena's menu and presses Enter through the key event path ten times; the first button
// must register exactly one click each time.
async function menuProbe() {
  const clipPath = `${DIR}/CL_Menu`;
  const text = [
    "clip rate=60 length=150",
    `event e1 30 {"kind":"call","target":"ProbeArena","func":"OpenMenu"}`,
    `event e2 60 {"key":{"key":"Enter"}}`,
    "check m1 90 60 throughout ProbeArena.MenuClicks eq 1 resolve_in_pie=true",
  ].join("\n");
  await call("pie_clip_create", { name: "CL_Menu", package_path: DIR, text, onConflict: "replace" });
  const runs = [];
  for (let i = 0; i < REPLAYS; i++) {
    const s = await runClip(clipPath);
    const m1 = (s.checks ?? []).find((c) => c.id === "m1") ?? {};
    runs.push({ passed: s.passed, clicks: m1.evidence?.last, reason: m1.evidence?.reason, events: s.events });
    log(`menu ${i + 1}/${REPLAYS}: passed ${s.passed}, clicks ${m1.evidence?.last}${m1.evidence?.reason ? ` (${m1.evidence.reason})` : ""}`);
  }
  return runs;
}

const result = { label: LABEL, seconds: SECONDS, started: new Date().toISOString() };
try {
  if (!SKIP_SETUP) await setup();
  else await call("load_level", { levelPath: LEVEL }, { allowFail: true });
  // --no-crate and --no-chaser measure the character alone.
  await call("set_actor_property", { actorLabel: "ProbeArena", propertyName: "bSpawnCrate", value: !flag("no-crate") });
  await call("set_actor_property", { actorLabel: "ProbeArena", propertyName: "bSpawnChaser", value: !flag("no-chaser") });
  await call("save_level", {}, { allowFail: true });
  result.arena = { crate: !flag("no-crate"), chaser: !flag("no-chaser") };
  if (flag("clip")) result.clip = await clipProbe();
  if (flag("menu")) result.menu = await menuProbe();
  const id = SOURCE || `probe_${LABEL}_${Date.now()}`;
  if (flag("clip") || flag("menu")) {
    // The clip probe is the whole run.
  } else {
  if (!SOURCE) {
    log(`recording ${id} (${SECONDS} s)`);
    await record(id);
  }
  const srcRows = readCsv(path.join(RECORDINGS, id, "recording.csv"));
  const last = srcRows[srcRows.length - 1];
  result.source = {
    id,
    frames: srcRows.length,
    final: Object.fromEntries(TRACK_VALUES.map((v) => [v, last[`t:${v}`]])),
    manifest: JSON.parse(fs.readFileSync(path.join(RECORDINGS, id, "manifest.json"), "utf8")).input_values ?? "final",
  };
  log(`source final ${JSON.stringify(result.source.final)}`);
  result.fixed = [];
  for (let i = 0; i < REPLAYS; i++) result.fixed.push(await replay(id, { fixed: true, label: `fixed ${i + 1}/${REPLAYS}` }));
  result.variable = [];
  for (let i = 0; i < VARIABLE; i++) result.variable.push(await replay(id, { fixed: false, label: `variable ${i + 1}/${VARIABLE}` }));
  if (flag("splice")) result.splice = await splice(id);
  }
} catch (e) {
  result.error = String(e?.stack ?? e);
  log(result.error);
} finally {
  await stopPie().catch(() => {});
  disconnectBridge();
  result.ended = new Date().toISOString();
  if (OUT) fs.writeFileSync(OUT, JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result, null, 2).slice(0, 4000));
}
