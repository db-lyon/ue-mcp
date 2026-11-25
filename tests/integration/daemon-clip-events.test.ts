/**
 * A playtest clip run started through the daemon streams on /v1/events: the clip watcher polls the editor's
 * pie_clip_status and the daemon publishes what changed between two answers as clip.<type> (#2097).
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const RUN = "clip-run-integ";
const CLIP = "/Game/PIEStudio/Clips/CL_Door";
const check = (id: string, status: string, extra: Record<string, unknown> = {}) => ({ id, mode: "by_end", status, start: 0, length: 60, ...extra });
const answer = (over: Record<string, unknown>) => ({
  success: true, run_id: RUN, clip: CLIP, clip_name: "CL_Door", mode: "play", state: "running", rate: 60, length: 120,
  position: 0, game_ms: 0, held_ms: 0, frames: 0, events: [], awaiting: [],
  checks: [check("c1", "pending"), check("c2", "pending", { start: 60 })],
  regions: [{ id: "r1", name: "Walk", start: 0, end: 120, status: "pending" }], ...over,
});

/** What pie_clip_status answers, poll by poll; the last one repeats. */
const STATUS = [
  answer({ position: 30, frames: 30, game_ms: 500, checks: [check("c1", "running"), check("c2", "pending", { start: 60 })] }),
  answer({
    position: 60, frames: 60, game_ms: 1000, holding: "c2",
    checks: [check("c1", "passed", { ended_frame: 41 }), check("c2", "running", { start: 60 })],
    events: [{ id: "e1", frame: 45, game_ms: 750, ok: true }],
  }),
  answer({
    state: "finished", position: 120, frames: 130, game_ms: 2160, held_ms: 160, passed: true, stop_reason: "reached the clip's end",
    summary: { total: 2, passed: 2, failed: 0, skipped: 0, pending: 0, awaiting: 0 },
    checks: [check("c1", "passed", { ended_frame: 41 }), check("c2", "passed", { start: 60, ended_frame: 70 })],
    events: [{ id: "e1", frame: 45, game_ms: 750, ok: true }],
    regions: [{ id: "r1", name: "Walk", start: 0, end: 120, status: "passed", drift: { max_position_cm: 0.02 } }],
  }),
];

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;
let polls = 0;

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Clips");
  bridge = await FakeBridge.start({
    projectDir: path.dirname(project),
    handlers: {
      pie_clip_run: () => answer({}),
      pie_clip_status: () => STATUS[Math.min(polls++, STATUS.length - 1)],
    },
  });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

describe("clip run events", () => {
  it("advertises events.clip and streams a clip run from pie(clip_run) to its end", async () => {
    covers("daemon.clip-events");
    const health = (await call(d, "GET", "/v1/health")).body;
    expect(health.features).toEqual(expect.arrayContaining(["events.clip"]));
    const stream = readSse(
      `${base(d)}/v1/events`,
      { ...bearer(d), "last-event-id": String(health.lastEventId) },
      (es) => es.some((e) => e.type === "clip.run_finished"),
    );

    const server = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    try {
      const run = await server.call("tools", { action: "call", category: "pie", method: "clip_run", args: { path: CLIP } });
      expect(run.isError, run.text).toBe(false);
      expect(run.text).toContain(RUN);
    } finally {
      await server.client.close();
    }

    const events = (await stream).filter((e) => e.type.startsWith("clip."));
    expect(events.map((e) => e.type)).toEqual([
      "clip.run_started",
      "clip.position",
      "clip.check",
      "clip.position",
      "clip.event_fired",
      "clip.check",
      "clip.check",
      "clip.position",
      "clip.check",
      "clip.region",
      "clip.run_finished",
    ]);
    expect(events.every((e) => e.data.runId === RUN && e.data.clip === CLIP)).toBe(true);
    expect(events.map((e) => e.data.seq)).toEqual(events.map((_, i) => i + 1));
    expect(events[3].data).toMatchObject({ position: 60, holding: "c2", gameMs: 1000 });
    expect(events[4].data).toMatchObject({ event: { id: "e1", frame: 45 } });
    expect(events[5].data).toMatchObject({ check: { id: "c1", status: "passed", ended_frame: 41 } });
    expect(events[9].data).toMatchObject({ region: { id: "r1", status: "passed", drift: { max_position_cm: 0.02 } } });
    expect(events[10].data).toMatchObject({ state: "finished", passed: true, summary: { passed: 2 } });

    // The watcher stopped polling once the run finished.
    const after = polls;
    await new Promise((r) => setTimeout(r, 800));
    expect(polls).toBe(after);
  }, 90_000);
});
