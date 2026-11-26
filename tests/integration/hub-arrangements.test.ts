/**
 * A project's named arrangements through a real daemon's HTTP API:
 * /v1/health advertises hub.arrangements, the tracks made before stand as the
 * first arrangement, Main, and each arrangement is created, edited, run,
 * duplicated and deleted apart from the others. No editor: runs are queued
 * behind a paused queue and read back.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const WALK = "/Game/Clips/CL_Walk";

let sb: Sandbox;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("HubArrangements");
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: "1" });
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("a project's arrangements", () => {
  it("keeps Main, edits and queues each arrangement apart, duplicates one and refuses deleting the last", async () => {
    covers("daemon.hub.arrangements");
    expect((await call(d, "GET", "/v1/health")).body.features).toEqual(expect.arrayContaining(["hub.arrangement", "hub.arrangements"]));

    expect((await call(d, "POST", "/v1/hub/set/edit", { ops: `track add Walk\nadd Walk clip ${WALK}` })).status).toBe(200);
    const first = await call(d, "GET", "/v1/hub/arrangements");
    expect(first.body).toMatchObject({ default: "main", arrangements: [{ id: "main", title: "Main", tracks: 1 }] });

    const made = await call(d, "POST", "/v1/hub/arrangements", { title: "Night", level: "/Game/Maps/Night" });
    expect(made.status, made.text).toBe(200);
    const night = made.body.arrangement.id as string;
    const edit = await call(d, "POST", "/v1/hub/set/edit", { arrangement: night, ops: `track add Guard player=1\nadd Guard clip ${WALK}` });
    expect(edit.status, edit.text).toBe(200);
    expect(edit.body.arrangement).toMatchObject({ id: night, level: "/Game/Maps/Night" });
    expect((await call(d, "GET", "/v1/hub/set")).body.tracks.map((t: { title: string }) => t.title)).toEqual(["Walk"]);

    expect((await call(d, "PUT", "/v1/hub/queue/paused", { paused: true })).status).toBe(200);
    const run = await call(d, "POST", "/v1/hub/run", { scope: { kind: "arrangement", arrangementId: night } });
    expect(run.status, run.text).toBe(200);
    expect(run.body.plan).toMatchObject({ arrangementId: night, items: [{ check: { kind: "arrangement", target: night } }] });
    expect((await call(d, "GET", `/v1/hub/queue?arrangement=${night}`)).body.queued).toHaveLength(1);
    expect((await call(d, "GET", "/v1/hub/queue?arrangement=main")).body.queued).toHaveLength(0);
    // While its run waits, the arrangement is not deleted.
    expect((await call(d, "DELETE", `/v1/hub/arrangements/${night}`)).status).toBe(409);
    expect((await call(d, "POST", `/v1/hub/plans/${run.body.plan.id}/cancel`)).status).toBe(200);

    const copy = await call(d, "POST", `/v1/hub/arrangements/${night}/duplicate`, { title: "Night 2" });
    expect(copy.status, copy.text).toBe(200);
    const copied = await call(d, "GET", `/v1/hub/set?arrangement=${encodeURIComponent("Night 2")}`);
    expect(copied.body.tracks.map((t: { title: string; player: number }) => [t.title, t.player])).toEqual([["Guard", 1]]);

    expect((await call(d, "PUT", `/v1/hub/arrangements/${night}`, { title: "Night 1" })).body.arrangement.title).toBe("Night 1");
    expect((await call(d, "DELETE", `/v1/hub/arrangements/${night}`)).status).toBe(204);
    expect((await call(d, "DELETE", "/v1/hub/arrangements/main")).status).toBe(204);
    const last = await call(d, "DELETE", `/v1/hub/arrangements/${copy.body.arrangement.id}`);
    expect(last.status).toBe(409);
    expect((await call(d, "GET", "/v1/hub/arrangements")).body.arrangements.map((a: { title: string }) => a.title)).toEqual(["Night 2"]);
  }, 60_000);
});
