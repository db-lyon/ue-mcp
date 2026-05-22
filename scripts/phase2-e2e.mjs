// End-to-end exercise for the PIE phase-2 batch.
//   1. Arm pie_record with track_actors + track_values
//   2. Start PIE
//   3. Let a few seconds of frames record
//   4. Stop PIE
//   5. Inspect manifest.json / recording.csv / tracked.jsonl
//   6. Arm pie_replay against that recording in monitor mode + capture_frame_every=10
//   7. Start PIE
//   8. Stop PIE
//   9. Inspect drift.json + frames/ directory
//  10. Delete the recording

import { EditorBridge } from "../dist/bridge.js";
import { readFileSync, existsSync, readdirSync } from "node:fs";
import { join } from "node:path";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (...a) => console.log("[e2e]", ...a);
const err = (...a) => { console.error("[e2e]", ...a); process.exitCode = 1; };

async function call(b, method, params = {}) {
  const r = await b.call(method, params);
  return r;
}

function assert(cond, msg) {
  if (!cond) { err("ASSERT FAILED:", msg); throw new Error(msg); }
  log("ok:", msg);
}

const RECORDING_ID = `e2e-phase2-${Date.now()}`;
log("recording id:", RECORDING_ID);

const b = new EditorBridge("127.0.0.1", 9877);
await b.connect(5000);
log("connected");

// Clean slate.
await call(b, "pie_record_disarm").catch(() => {});
await call(b, "pie_replay_disarm").catch(() => {});

// ─── Record ─────────────────────────────────────────────────────────────────
log("=== RECORD ===");
const armR = await call(b, "pie_record_arm", {
  id: RECORDING_ID,
  sample_hz: 60,
  track_actors: ["PlayerCameraManager", "DefaultPawn_C"],
  track_values: ["GetActorLocation.X"],
  rng_seed: 12345,
});
log("arm:", JSON.stringify(armR));
assert(armR?.id === RECORDING_ID, "arm returned the requested id");

await call(b, "pie_control", { action: "start" });
log("PIE started, recording for 5s...");
await sleep(5000);

await call(b, "pie_mark", { label: "midpoint" });
log("marker dropped");
await sleep(2000);

await call(b, "pie_control", { action: "stop" });
log("PIE stopped");
await sleep(1500);

const status = await call(b, "pie_record_status");
log("status after PIE stop:", JSON.stringify(status));

// ─── Inspect recording artifacts ────────────────────────────────────────────
log("=== INSPECT RECORDING ===");
const recordingsRoot = join(process.cwd(), "tests", "ue_mcp", "Saved", "MCPRecordings", RECORDING_ID);
log("expecting recording dir:", recordingsRoot);
assert(existsSync(recordingsRoot), "recording dir exists on disk");

const manifestPath = join(recordingsRoot, "manifest.json");
assert(existsSync(manifestPath), "manifest.json exists");
const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
log("manifest keys:", Object.keys(manifest).join(", "));
log("total_frames:", manifest.total_frames, "duration:", manifest.duration_seconds);
assert(manifest.id === RECORDING_ID, "manifest id matches");
assert(Array.isArray(manifest.tracked_actors), "manifest has tracked_actors array");
assert(manifest.tracked_actors.length === 2, "tracked_actors has 2 ids");
assert(manifest.client_id === 0, "manifest client_id present (0)");
assert(manifest.total_frames > 0, "recorded at least one frame");

const csvPath = join(recordingsRoot, "recording.csv");
assert(existsSync(csvPath), "recording.csv exists");
const csv = readFileSync(csvPath, "utf8");
const csvLines = csv.split(/\r?\n/).filter(Boolean);
log("csv lines:", csvLines.length);
const header = csvLines.find((l) => !l.startsWith("#") && l.startsWith("frame,"));
assert(header, "csv has a header row");
log("csv header:", header);
assert(header.includes("t:GetActorLocation.X"), "csv has t: column for tracked value");

const jsonlPath = join(recordingsRoot, "tracked.jsonl");
if (manifest.files?.tracked_actors) {
  assert(existsSync(jsonlPath), "tracked.jsonl exists when manifest references it");
  const jsonl = readFileSync(jsonlPath, "utf8");
  const rows = jsonl.split(/\r?\n/).filter(Boolean);
  log("tracked.jsonl rows:", rows.length);
  assert(rows.length > 0, "tracked.jsonl has at least one row");
  const first = JSON.parse(rows[0]);
  log("first jsonl row keys:", Object.keys(first).join(", "));
  assert(typeof first.frame === "number", "row has frame");
  assert(typeof first.actors === "object", "row has actors object");
  log("actors in first row:", Object.keys(first.actors).join(", "));
} else {
  log("note: manifest did not reference tracked_actors file (probably no actor IDs resolved)");
}

// ─── Replay in monitor mode with frame capture ──────────────────────────────
log("=== REPLAY (monitor + capture) ===");
const replayR = await call(b, "pie_replay_arm", {
  recording_id: RECORDING_ID,
  mode: "monitor",
  capture_frame_every: 30,  // ~once a second at 60Hz
});
log("replay arm:", JSON.stringify(replayR));
assert(replayR?.armed === true, "replay armed");

await call(b, "pie_control", { action: "start" });
log("PIE replay-monitor started for 5s...");
await sleep(5000);

const repStatus = await call(b, "pie_replay_status");
log("replay status mid-flight:", JSON.stringify(repStatus));

await call(b, "pie_control", { action: "stop" });
await sleep(1500);
log("PIE stopped");

const driftPath = join(recordingsRoot, "drift.json");
if (existsSync(driftPath)) {
  const drift = JSON.parse(readFileSync(driftPath, "utf8"));
  log("drift.json keys:", Object.keys(drift).join(", "));
  log("frames_compared:", drift.frames_compared, "max_position_drift_cm:", drift.max_position_drift_cm);
  assert(typeof drift.tracked_value_max_deltas === "object", "drift.json has tracked_value_max_deltas object");
  assert("actor_drift" in drift, "drift.json has actor_drift field");
} else {
  log("note: drift.json not written (expected if replay never attached a pawn)");
}

const framesDir = join(recordingsRoot, "frames");
if (existsSync(framesDir)) {
  const files = readdirSync(framesDir).filter((f) => f.endsWith(".png"));
  log("frames captured:", files.length);
  assert(files.length > 0, "at least one frame.png was written");
} else {
  log("note: frames/ not created (expected if replay never attached a pawn)");
}

// ─── Cleanup ────────────────────────────────────────────────────────────────
log("=== CLEANUP ===");
const del = await call(b, "pie_record_delete", { id: RECORDING_ID, confirm: true });
log("delete:", JSON.stringify(del));

b.disconnect();
log("DONE");
