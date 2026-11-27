/**
 * The Playtest Hub's core (#2146) through a real daemon's HTTP API, with no
 * editor: clips, arrangements and suites written as text files in the
 * project, refused with findings when they do not read, a hand-broken file
 * listed as a problem, the queue on this machine, and the performers a track
 * can drive.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { Sandbox, runCli, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let d: Discovery;
let projectDir: string;

const walk = { title: "Walk", performer: "player", rate: 60, input: [{ action: "/Game/Input/IA_Move.IA_Move", type: "axis2d", keys: [{ id: "k1", t: 0, v: [0, 1] }, { id: "k2", t: 120, v: [0, 0] }] }] };
const strafe = { title: "Strafe", performer: "player", input: [{ action: "/Game/Input/IA_Move.IA_Move", type: "axis2d", notes: [{ id: "n1", start: 0, length: 90, value: [1, 0] }] }] };
const patrol = { title: "Patrol", performer: "actor", lanes: [{ id: "m", kind: "move", points: [{ id: "p1", t: 0, at: [0, 0, 0] }, { id: "p2", t: 90, at: [500, 0, 0] }] }] };

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("HubCore");
  projectDir = path.dirname(project);
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: "1" });
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("the hub's files", () => {
  it("writes clips, arrangements and suites as stable text in the project and refuses what does not read", async () => {
    covers("daemon.hub.files");
    const health = await call(d, "GET", "/v1/health");
    expect(health.body.features).toEqual(expect.arrayContaining(["hub"]));

    expect((await call(d, "POST", "/v1/hub/clips", walk)).body.clip).toMatchObject({ id: "walk", length: 120 });
    expect((await call(d, "POST", "/v1/hub/clips", strafe)).status).toBe(200);
    const actorClip = await call(d, "POST", "/v1/hub/clips", patrol);
    expect(actorClip.status).toBe(400);
    expect(actorClip.body.findings.map((f: { path: string }) => f.path)).toEqual(expect.arrayContaining(["lanes", "performer"]));
    const withChecks = await call(d, "POST", "/v1/hub/clips", { ...walk, checks: [] });
    expect(withChecks.status).toBe(400);
    expect(withChecks.body.findings[0]).toMatchObject({ path: "checks", blocks: true });

    const clipFile = path.join(projectDir, "PlaytestHub", "clips", "walk.json");
    const text = fs.readFileSync(clipFile, "utf8");
    expect(text.endsWith("}\n")).toBe(true);
    expect((await call(d, "PUT", "/v1/hub/clips/walk", JSON.parse(text))).status).toBe(200);
    expect(fs.readFileSync(clipFile, "utf8")).toBe(text);

    const arrangement = {
      title: "Door",
      level: "/Game/Maps/Door",
      tracks: [
        { id: "hero", title: "Hero", performer: { kind: "player", client: 0 }, instances: [{ id: "i1", clip: "walk", at: 0 }] },
        { id: "guest", title: "Guest", performer: { kind: "player", client: 1 }, instances: [{ id: "i2", clip: "strafe", at: 30 }] },
      ],
      checks: [
        { id: "moved", title: "Hero moved", at: 0, length: 120, mode: "by_end", expect: { value: "pos_x", op: "increasing" } },
        { id: "looks", title: "Door looks open", at: 150, length: 0, mode: "manual", instruction: "Is the door open?" },
      ],
      regions: [{ id: "walk", name: "Walk in", start: 0, end: 140 }],
    };
    const actorTrack = await call(d, "POST", "/v1/hub/arrangements", { ...arrangement, tracks: [{ id: "g", title: "Guard", performer: { kind: "actor", actor: "Guard_2" }, instances: [] }] });
    expect(actorTrack.status).toBe(400);
    expect(actorTrack.body.error).toMatch(/performer kind 'actor' is not one of player/);
    const unknown = await call(d, "POST", "/v1/hub/arrangements", { ...arrangement, tracks: [{ ...arrangement.tracks[0], instances: [{ id: "x", clip: "wlak", at: 0 }] }] });
    expect(unknown.body.findings[0].candidates).toContain("walk");

    const made = await call(d, "POST", "/v1/hub/arrangements", arrangement);
    expect(made.status, made.text).toBe(200);
    expect(made.body.tests.map((t: { id: string; checks: number }) => [t.id, t.checks])).toEqual([["door", 2], ["door:walk", 1]]);

    const suite = await call(d, "POST", "/v1/hub/suites", { title: "Smoke", items: [{ test: "door:walk" }, { test: "door" }] });
    expect(suite.body).toMatchObject({ id: "smoke", health: "never_run" });
    const outer = await call(d, "POST", "/v1/hub/suites", { title: "Nightly", items: [{ suite: "smoke" }] });
    expect(outer.body.tests).toHaveLength(2);
    const loop = await call(d, "PUT", "/v1/hub/suites/smoke", { title: "Smoke", items: [{ suite: "nightly" }] });
    expect(loop.status).toBe(400);
    expect(loop.body.error).toMatch(/contain each other/);

    expect((await call(d, "DELETE", "/v1/hub/clips/walk")).status).toBe(409);
    expect((await call(d, "DELETE", "/v1/hub/arrangements/door")).status).toBe(409);
    expect((await call(d, "DELETE", "/v1/hub/suites/smoke")).status).toBe(409);

    fs.writeFileSync(path.join(projectDir, "PlaytestHub", "suites", "broken.json"), "{ not json");
    const overview = await call(d, "GET", "/v1/hub");
    expect(overview.body.problems).toEqual([expect.objectContaining({ kind: "suite", id: "broken" })]);
    expect(fs.readFileSync(path.join(projectDir, "PlaytestHub", "suites", "broken.json"), "utf8")).toBe("{ not json");
  }, 60_000);

  it("queues tests on this machine and keeps suite marks", async () => {
    covers("daemon.hub.queue");
    expect((await call(d, "POST", "/v1/hub/queue", { test: "door", runner: "headless" })).status).toBe(409);
    const queued = await call(d, "POST", "/v1/hub/queue", { suite: "nightly" });
    expect(queued.status, queued.text).toBe(200);
    expect(queued.body.queued.map((q: { test: string }) => q.test)).toEqual(["door:walk", "door"]);
    expect(queued.body.queue.waiting[0]).toMatchObject({ kind: "disconnected" });
    expect((await call(d, "DELETE", `/v1/hub/queue/${queued.body.queued[0].id}`)).body.queued).toHaveLength(1);

    const q = await call(d, "POST", "/v1/hub/suites/smoke/quarantine", { test: "door" });
    expect(q.body.quarantined).toEqual(["door"]);
    expect(q.body.tests.find((t: { id: string }) => t.id === "door").quarantined).toBe(true);
    const f = await call(d, "POST", "/v1/hub/suites/smoke/flaky", { test: "door:walk" });
    expect(f.body.health).toBe("flaky");
    expect(JSON.parse(fs.readFileSync(path.join(projectDir, "PlaytestHub", "suites", "smoke.json"), "utf8"))).toMatchObject({ quarantined: ["door"], flaky: ["door:walk"] });
  }, 60_000);

  it("lists the players a track can drive", async () => {
    covers("daemon.hub.performers");
    const list = await call(d, "GET", "/v1/hub/performers");
    expect(list.status, list.text).toBe(200);
    expect(list.body.performers.map((p: { performer: unknown }) => p.performer)).toEqual([0, 1, 2, 3].map((client) => ({ kind: "player", client })));
    expect(list.body.performers.map((p: { label: string }) => p.label)).toEqual(["Editor player", "Client 1", "Client 2", "Client 3"]);
  }, 60_000);

  it("refuses an audition while no editor answers, and has none to read or stop", async () => {
    covers("daemon.hub.play");
    const play = await call(d, "POST", "/v1/hub/play", { test: "door:walk", from: 30, loop: true });
    expect(play.status).toBe(409);
    expect(play.body.error).toMatch(/the editor is in use/);
    expect((await call(d, "POST", "/v1/hub/play", { test: "door", from: -2 })).status).toBe(400);
    expect((await call(d, "GET", "/v1/hub/play")).body).toEqual({ play: null });
    expect((await call(d, "POST", "/v1/hub/play/stop")).status).toBe(409);
    expect((await call(d, "GET", "/v1/hub/record")).body).toEqual({ recording: null });
    expect((await call(d, "POST", "/v1/hub/record/stop", {})).status).toBe(409);
  }, 60_000);

  it("runs a test from the command line, writes the report and exits non-zero when it does not pass", async () => {
    covers("cli.hub");
    const report = sb.path("report.json");
    const r = await runCli(sb, ["hub", "run", "door:walk", "--editor", "--timeout", "2", "--report", report, "--project", path.join(projectDir, "HubCore.uproject")], { timeoutMs: 60_000 });
    expect(r.code, r.stdout + r.stderr).toBe(1);
    const written = JSON.parse(fs.readFileSync(report, "utf8"));
    expect(written).toMatchObject({ target: "door:walk", kind: "test", ok: false, error: expect.stringMatching(/did not all end within 2s/) });
    expect(written.batch).toMatch(/^b_/);
    const suite = await runCli(sb, ["hub", "run", "nightly", "--headless", "--report", report, "--project", path.join(projectDir, "HubCore.uproject"), "--json"], { timeoutMs: 60_000 });
    expect(suite.code).toBe(1);
    expect(JSON.parse(suite.stdout)).toMatchObject({ kind: "suite", ok: false, error: expect.stringMatching(/headless/) });
    expect((await runCli(sb, ["hub", "run"])).code).toBe(2);
    const queue = await call(d, "GET", "/v1/hub/queue");
    for (const q of queue.body.queued as Array<{ id: string }>) await call(d, "DELETE", `/v1/hub/queue/${q.id}`);
  }, 120_000);
});
