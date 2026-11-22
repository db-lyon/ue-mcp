/**
 * Play sessions on the daemon's event stream: each notifications/pie frame the editor pushes is republished as
 * pie.<state>, and every attach reads pie_control(status) once and publishes the current state marked resync, so a
 * client that missed events while the editor was away is put right.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let status = { isPlaying: false, isSimulating: false, isPaused: false };

const lastId = async (): Promise<number> => (await call(d, "GET", "/v1/health")).body.lastEventId as number;
const pieEvents = (from: number, count: number) =>
  readSse(
    `${base(d)}/v1/events`,
    { ...bearer(d), "last-event-id": String(from) },
    (es) => es.filter((e) => e.type.startsWith("pie.")).length >= count,
  ).then((es) => es.filter((e) => e.type.startsWith("pie.")));

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("Pie");
  bridge = await FakeBridge.start({
    projectDir: path.dirname(project),
    handlers: { pie_control: (p) => ({ success: true, action: p.pieAction, ...status, changed: false }) },
  });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

describe("PIE events", () => {
  it("advertises events.pie and publishes the state it reads when the editor attaches", async () => {
    covers("daemon.pie-events");
    expect((await call(d, "GET", "/v1/health")).body.features).toContain("events.pie");
    const [first] = await pieEvents(0, 1);
    expect(first.type).toBe("pie.ended");
    expect(first.data).toMatchObject({ editor: "Pie", state: "ended", simulate: false, resync: true });
    expect(bridge.calls.find((c) => c.method === "pie_control")?.params).toEqual({ pieAction: "status" });
  }, 30_000);

  it("republishes each Play session frame the editor pushes, in order", async () => {
    covers("daemon.pie-events");
    const before = await lastId();
    const stream = pieEvents(before, 4);
    const at = (s: number) => new Date(Date.UTC(2026, 10, 21, 12, 0, s)).toISOString();
    bridge.notify("notifications/pie", { state: "started", simulate: false, at: at(1) });
    bridge.notify("notifications/pie", { state: "paused", simulate: false, at: at(2) });
    bridge.notify("notifications/pie", { state: "resumed", simulate: false, at: at(3) });
    bridge.notify("notifications/pie", { state: "ended", simulate: true, at: at(4) });
    // Neither a frame of another method nor an unknown state becomes an event.
    bridge.notify("notifications/other", { state: "started" });
    bridge.notify("notifications/pie", { state: "exploded" });

    const events = await stream;
    expect(events.map((e) => e.type)).toEqual(["pie.started", "pie.paused", "pie.resumed", "pie.ended"]);
    expect(events.map((e) => e.data.at)).toEqual([at(1), at(2), at(3), at(4)]);
    expect(events.every((e) => e.data.editor === "Pie" && e.data.resync === undefined)).toBe(true);
    expect(events[3].data.simulate).toBe(true);
  }, 30_000);

  it("resyncs a session that started while the editor was detached", async () => {
    covers("daemon.pie-events");
    const before = await lastId();
    const stream = pieEvents(before, 1);
    status = { isPlaying: true, isSimulating: false, isPaused: true };
    bridge.dropClients();
    const [resync] = await stream;
    expect(resync.type).toBe("pie.started");
    expect(resync.data).toMatchObject({ editor: "Pie", state: "started", simulate: false, paused: true, resync: true });
  }, 45_000);
});
