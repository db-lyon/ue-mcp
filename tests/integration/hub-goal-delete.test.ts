/**
 * Deleting a Playtest Hub goal through a real daemon's HTTP API: /v1/health
 * advertises hub.goalDelete, the goals that required the deleted one stop
 * requiring it, its playtests stay in the library with their own records, and
 * a queued plan made for it refuses the delete with 409 until it is cancelled.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const DOOR = "/Game/Tests/DoorOpens";
const JUMP = "/Game/Tests/JumpCheck";
const enc = encodeURIComponent;

let sb: Sandbox;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("HubDelete");
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: "1" });
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

const create = async (body: Record<string, unknown>): Promise<string> => {
  const r = await call(d, "POST", "/v1/hub/goals", body);
  expect(r.status, r.text).toBe(200);
  return r.body.goal.id as string;
};

describe("deleting a hub goal", () => {
  it("advertises hub.goalDelete, unlinks the goal, keeps its playtests, and refuses while a plan for it is queued", async () => {
    covers("daemon.hub.goal-delete");
    expect((await call(d, "GET", "/v1/health")).body.features).toEqual(expect.arrayContaining(["hub", "hub.goalDelete"]));

    const doors = await create({ title: "Doors", checks: [{ kind: "script", target: DOOR }] });
    const jump = await create({ title: "Jump", checks: [{ kind: "script", target: DOOR }, { kind: "script", target: JUMP }] });
    const alpha = await create({ title: "Alpha", kind: "milestone", requires: [{ goal: doors }, { goal: jump }] });
    expect((await call(d, "PUT", `/v1/hub/checks/${enc(`script:${DOOR}`)}`, { title: "Door opens" })).status).toBe(200);

    // A queued plan made for the goal holds it.
    expect((await call(d, "PUT", "/v1/hub/queue/paused", { paused: true })).status).toBe(200);
    const planned = await call(d, "POST", "/v1/hub/plans", { scope: { kind: "goal", goalId: doors } });
    expect(planned.status, planned.text).toBe(200);
    const refused = await call(d, "DELETE", `/v1/hub/goals/${doors}`);
    expect(refused.status).toBe(409);
    expect(refused.body.error).toContain(`"Run Doors"`);
    expect(refused.body.error).toContain("Cancel that run");
    expect((await call(d, "POST", `/v1/hub/plans/${planned.body.plan.id}/cancel`)).status).toBe(200);

    const deleted = await call(d, "DELETE", `/v1/hub/goals/${doors}`);
    expect(deleted.status, deleted.text).toBe(204);
    expect((await call(d, "GET", `/v1/hub/goals/${doors}`)).status).toBe(404);
    expect((await call(d, "GET", `/v1/hub/goals/${alpha}`)).body.goal.requires).toEqual([{ goal: jump }]);

    // The playtests stay: in the library, on the other goal, and with their own record.
    const store = (await call(d, "GET", "/v1/hub/store")).body as { records: Array<{ type: string; id: string; data: Record<string, unknown> }> };
    expect(store.records.filter((r) => r.type === "goal").map((r) => r.id).sort()).toEqual([alpha, jump].sort());
    expect(store.records.find((r) => r.type === "goal" && r.id === jump)!.data.checks).toEqual([{ kind: "script", target: DOOR }, { kind: "script", target: JUMP }]);
    expect(store.records.find((r) => r.type === "check" && r.id === `script:${DOOR}`)!.data).toMatchObject({ title: "Door opens" });
    const overview = (await call(d, "GET", "/v1/hub")).body as { checks: Array<{ id: string }>; warnings: string[] };
    expect(overview.checks.map((c) => c.id).sort()).toEqual([`script:${DOOR}`, `script:${JUMP}`].sort());
    expect(overview.warnings.filter((w) => w.includes(doors))).toEqual([]);

    // A milestone deletes the same way; the goals it required stay.
    expect((await call(d, "DELETE", `/v1/hub/goals/${alpha}`)).status).toBe(204);
    expect((await call(d, "GET", `/v1/hub/goals/${jump}`)).status).toBe(200);
  }, 60_000);
});
