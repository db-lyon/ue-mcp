/**
 * Actor performers through a real daemon's HTTP API (#2137): /v1/health
 * advertises hub.performers, a track is given an actor to drive and keeps it
 * across reads, a run of it is refused with its findings, and
 * GET /v1/hub/performers lists the players with the reason no actor is listed
 * while no editor answers.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("HubPerformers");
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: "1" });
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("actor performers", () => {
  it("gives a track an actor to drive, keeps it, and lists what a track can drive", async () => {
    covers("daemon.hub.performers");
    expect((await call(d, "GET", "/v1/health")).body.features).toEqual(expect.arrayContaining(["hub.performers"]));

    const made = await call(d, "POST", "/v1/hub/set/edit", { ops: 'track add Hero\ntrack add Guard performer=actor:"Guard 2"' });
    expect(made.status, made.text).toBe(200);
    const read = await call(d, "GET", "/v1/hub/set");
    expect(read.body.tracks.map((t: { title: string; performer: unknown; player: number }) => [t.title, t.performer, t.player])).toEqual([
      ["Hero", { kind: "player", client: 0 }, 0],
      ["Guard", { kind: "actor", actor: "Guard 2" }, 0],
    ]);
    expect(read.body.text).toContain('performer=actor:"Guard 2"');

    const back = await call(d, "POST", "/v1/hub/set/edit", { ops: [{ op: "track.performer", track: "Guard", performer: { kind: "player", client: 2 } }] });
    expect(back.status, back.text).toBe(200);
    expect(back.body.tracks[1]).toMatchObject({ performer: { kind: "player", client: 2 }, player: 2 });

    const wrong = await call(d, "POST", "/v1/hub/set/edit", { ops: "track performer Guard ghost:1" });
    expect(wrong.status).toBe(400);
    expect(wrong.body.finding).toMatchObject({ field: "performer", blocks: true });

    const list = await call(d, "GET", "/v1/hub/performers");
    expect(list.status, list.text).toBe(200);
    expect(list.body.performers.map((p: { performer: unknown }) => p.performer)).toEqual([0, 1, 2, 3].map((client) => ({ kind: "player", client })));
    expect(list.body.performers[0]).toMatchObject({ label: "Editor player", capabilities: ["input", "action", "value"] });
    expect(list.body.reason).toMatch(/could not be read/);
  }, 60_000);
});
