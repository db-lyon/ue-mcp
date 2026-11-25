// @live-editor headless
/**
 * An agent arranging the Playtest Hub's tracks from clips and running a scene to a pass (#2113), through the hub
 * actions an agent calls (edit_set, run, read_run) on a Hub whose editor calls go to the live bridge. Two clips are
 * written from nothing in the probe arena: one walks forward and one charges. A track places the walk twice, the
 * second instance trimmed, and another track places the charge; a scene plays both tracks together in one PIE
 * session, and every instance passes at its place.
 * Refs #2113
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import type { ActionResult } from "../../src/contract/daemon.js";
import { Hub } from "../../src/hub/hub.js";
import { runHubAction } from "../../src/hub/hub-tools.js";
import { ScriptIndex } from "../../src/hub/script-index.js";
import { ScriptRunBroker } from "../../src/hub/script-broker.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/HubArrangement";
const WALK = `${DIR}/CL_Walk`;
const CHARGE = `${DIR}/CL_Charge`;

type Json = Record<string, unknown>;
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let a: ProbeActions;
let hub: Hub;
let root: string;

/** A pie or editor action, as the server would forward it to the bridge. */
async function forward(category: string, action: string, args: Json = {}): Promise<ActionResult> {
  const method = category === "editor" && action === "play_in_editor" ? "pie_control" : category === "asset" && action === "duplicate" ? "duplicate_asset" : action === "verdict" ? "pie_script_verdict" : `pie_${action}`;
  const r = await callBridge(bridge, method, args);
  const body = r.result && typeof r.result === "object" ? (r.result as Json) : {};
  return { isError: !r.ok, content: [{ type: "text", text: JSON.stringify(r.ok ? body : { success: false, error: r.error }) }] };
}

async function pieRunning(): Promise<boolean> {
  const r = await callBridge(bridge, "pie_control", { pieAction: "status" });
  return (r.result as Json | undefined)?.isPlaying === true;
}

async function stopPie(): Promise<void> {
  if (!(await pieRunning())) return;
  await callBridge(bridge, "pie_control", { pieAction: "stop" });
  for (let i = 0; i < 60 && (await pieRunning()); i++) await sleep(500);
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  a = await setupProbeArena(bridge, DIR, "HubArrangement", { chaser: false });
  const walk = ["clip rate=60 length=120", `track ${a.move} axis2d`, "  key k1 0 0,1 constant", "  key k2 90 0,0 constant", "check c1 0 100 by_end pos_x increasing 100"].join("\n");
  const charge = ["clip rate=60 length=90", `track ${a.charge} bool`, "  note n1 0 45", "check c1 0 80 by_end ChargeCount increasing 1 resolve_in_pie=true"].join("\n");
  for (const [name, text] of [["CL_Walk", walk], ["CL_Charge", charge]]) {
    const r = await callBridge(bridge, "pie_clip_create", { name, package_path: DIR, text, onConflict: "replace" });
    expect(r.ok, r.error).toBe(true);
  }
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-live-"));
  const host = {
    projectDir: path.join(root, "project"),
    dataDir: path.join(root, "data"),
    callAction: forward,
    editors: () => [{ connected: true }],
    events: { publish: () => undefined, subscribe: () => () => undefined },
    log: { info() {}, warn() {} },
  };
  fs.mkdirSync(host.projectDir, { recursive: true });
  hub = new Hub({ host, broker: new ScriptRunBroker(host), index: new ScriptIndex(host), timers: false, playtestRuns: () => "automatic", workspace: async () => ({ vcs: "none", revision: null, dirty: 0 }), editor: { pie: () => false, dialogs: () => 0, busy: () => ({ now: Date.now(), calls: [] }), inputAgeMs: () => null }, pieWaits: { startMs: 100, inUseMs: 100 } });
}, 240_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    hub?.dispose();
    await callBridge(bridge, "pie_clip_stop", {});
    await stopPie();
  } finally {
    await teardownProbeArena(bridge, DIR);
    if (root) fs.rmSync(root, { recursive: true, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("an agent arranging tracks from clips", () => {
  it("places, duplicates and trims instances, builds a scene, and runs it to a pass", async () => {
    const set = (await runHubAction(hub, "edit_set", {
      ops: [
        `track add Walker color=green`,
        `track add Charger color=orange`,
        `add Walker clip ${WALK}`,
        `duplicate Walker/i1`,
        `trim Walker/i2 out=110`,
        `add Charger clip ${CHARGE} at 60`,
        `scene add "Walk and charge"`,
        `scene track "Walk and charge" add Walker`,
        `scene track "Walk and charge" add Charger`,
      ].join("\n"),
    })) as { text: string; findings?: string[] };
    expect(set.findings ?? []).toEqual([]);
    expect(set.text).toContain(`clip i2 ${WALK} 120 out=110`);

    const queued = (await runHubAction(hub, "run", { scope: "scene Walk and charge" })) as { plan: { id: string } };
    let runId: string | null | undefined;
    for (let i = 0; i < 600; i++) {
      const plan = [hub.queue.state().current, ...hub.queue.state().recent].find((p) => p?.id === queued.plan.id);
      runId = plan?.items[0].runId;
      if (plan && plan.state !== "running" && plan.state !== "queued") break;
      if (runId) await hub.arrangements.refresh(runId);
      await sleep(250);
    }
    expect(runId).toBeTruthy();
    const result = (await runHubAction(hub, "read_run", { run_id: runId })) as { outcome: string; instances: string[]; tracks: string[] };
    expect(result.outcome, JSON.stringify(result)).toBe("passed");
    expect(result.instances).toHaveLength(3);
    for (const line of result.instances) expect(line, JSON.stringify(result)).toMatch(/ passed /);
    expect(result.tracks.every((t) => / passed /.test(t))).toBe(true);
    await stopPie();
  }, 300_000);
});
