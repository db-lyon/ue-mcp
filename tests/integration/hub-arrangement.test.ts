/**
 * The Playtest Hub's arrangement through a real daemon's HTTP API (#2113):
 * /v1/health advertises hub.arrangement, set ops build tracks, instances and
 * scenes with candidates for a bad name, a goal made through the old routes
 * shows up as a track, a scene run queues as one item, and deleting a track
 * takes it out of its scenes. No editor: clips are laid out as given.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const DOOR = "/Game/Tests/DoorOpens";
const WALK = "/Game/Clips/CL_Walk";

let sb: Sandbox;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("HubArrangement");
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: "1" });
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("the hub's arrangement", () => {
  it("edits the set with ops, queues a scene as one run, and drops a deleted track from its scenes", async () => {
    covers("daemon.hub.arrangement");
    expect((await call(d, "GET", "/v1/health")).body.features).toEqual(expect.arrayContaining(["hub", "hub.arrangement"]));

    const old = await call(d, "POST", "/v1/hub/goals", { title: "Doors", checks: [{ kind: "script", target: DOOR }] });
    expect(old.status, old.text).toBe(200);

    const edit = await call(d, "POST", "/v1/hub/set/edit", { ops: `track add Walk color=green\ntrack add Guard player=1\nadd Walk clip ${WALK} out=120\nadd Walk script ${DOOR} at 120\nscene add Alpha\nscene track Alpha add Walk\nscene track Alpha add Guard\ntrack solo Walk` });
    expect(edit.status, edit.text).toBe(200);
    const walk = edit.body.tracks.find((t: { title: string }) => t.title === "Walk");
    expect(walk.layout.map((l: { id: string; start: number; end: number }) => [l.id, l.start, l.end])).toEqual([["i1", 0, 120], ["i2", 120, 720]]);
    expect(edit.body.tracks.map((t: { title: string }) => t.title)).toEqual(["Doors", "Walk", "Guard"]);
    expect(edit.body.text).toContain(`clip i1 ${WALK} 0 out=120`);

    const bad = await call(d, "POST", "/v1/hub/set/edit", { ops: "track mute Gaurd" });
    expect(bad.status).toBe(400);
    expect(bad.body.finding.candidates).toContain("Guard");

    // A goal made through the goal routes is a track; adding a playtest places it after the last instance.
    const added = await call(d, "POST", `/v1/hub/goals/${walk.id}/checks`, { checks: [{ kind: "script", target: "/Game/Tests/Jump" }] });
    expect(added.status, added.text).toBe(200);
    expect(added.body.goal.arrangement.instances.at(-1)).toMatchObject({ kind: "script", target: "/Game/Tests/Jump", start: 720 });

    expect((await call(d, "PUT", "/v1/hub/queue/paused", { paused: true })).status).toBe(200);
    const scene = edit.body.scenes[0].id as string;
    const run = await call(d, "POST", "/v1/hub/run", { scope: { kind: "scene", goalId: scene } });
    expect(run.status, run.text).toBe(200);
    expect(run.body.plan.items).toEqual([expect.objectContaining({ check: { kind: "scene", target: scene }, name: "Alpha", state: "queued" })]);
    expect((await call(d, "POST", `/v1/hub/plans/${run.body.plan.id}/cancel`)).status).toBe(200);

    const gone = await call(d, "POST", "/v1/hub/set/edit", { ops: "track delete Guard" });
    expect(gone.status, gone.text).toBe(200);
    expect(gone.body.scenes[0].tracks).toEqual([walk.id]);
  }, 60_000);
});
