// @live-editor headless
/**
 * execute_batch against a real editor: the steps run in order as one job and
 * each step's effect is visible to the next, the stop and continue policies
 * leave exactly what they say applied, and two transactional steps make one
 * Undo entry. The lock steps touch only the bridge's lock registry; the move
 * steps act on an actor placed here and deleted afterwards. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Editor";
const PATH_A = `${DIR}/BCov_Batch_A`;
const SESSION = "bcov-batch-session";
const ACTOR = "BCov_Batch_Cube";

type Json = Record<string, unknown>;
type StepResult = { index: number; method: string; success: boolean; result: Json };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

/** One batch, answered or not: the body is what every assertion reads. */
async function batch(calls: Array<{ method: string; params?: Json }>, extra: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, "execute_batch", { ops: calls, ...extra });
  expect(r.ok, `execute_batch: ${r.error}`).toBe(true);
  return body(r);
}

const steps = (b: Json): StepResult[] => (b.results as StepResult[]) ?? [];

async function heldBy(path: string): Promise<string | undefined> {
  const listed = body(await callBridge(bridge, "list_locks", {}));
  const lock = (listed.locks as Json[] | undefined)?.find((l) => l.path === path);
  return lock ? String(lock.sessionId) : undefined;
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "release_session_locks", { sessionId: SESSION });
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
}, 60_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
});

describe("execute_batch", () => {
  it("runs its steps in order as one job, each seeing the one before it", async () => {
    const out = await batch([
      { method: "acquire_lock", params: { path: PATH_A, sessionId: SESSION } },
      { method: "list_locks" },
      { method: "release_lock", params: { path: PATH_A, sessionId: SESSION } },
      { method: "list_locks" },
    ]);
    expect(out.success).toBe(true);
    expect(out).toMatchObject({ requestedCalls: 4, completedCalls: 4, failedCount: 0, stoppedEarly: false });
    const results = steps(out);
    expect(results.map((s) => [s.index, s.method, s.success])).toEqual([
      [0, "acquire_lock", true], [1, "list_locks", true], [2, "release_lock", true], [3, "list_locks", true],
    ]);
    const held = (r: StepResult) => (r.result.locks as Json[]).some((l) => l.path === PATH_A && l.sessionId === SESSION);
    expect(held(results[1]), "the listing after the acquire shows the lock").toBe(true);
    expect(held(results[3]), "the listing after the release does not").toBe(false);
    expect(await heldBy(PATH_A)).toBeUndefined();
  }, 60_000);

  it("stops at the first failing step by default and leaves the earlier steps applied", async () => {
    const out = await batch([
      { method: "acquire_lock", params: { path: PATH_A, sessionId: SESSION } },
      // No sessionId: the handler refuses it, and nothing after it runs.
      { method: "acquire_lock", params: { path: PATH_A } },
      { method: "release_lock", params: { path: PATH_A, sessionId: SESSION } },
    ]);
    expect(out.success).toBe(false);
    expect(out).toMatchObject({ requestedCalls: 3, completedCalls: 2, failedIndex: 1, failedCount: 1, stoppedEarly: true });
    expect(steps(out).map((s) => s.success)).toEqual([true, false]);
    expect(String(out.error)).toContain("ops[1] (acquire_lock) failed");
    // The release never ran: no rollback, the first step's lock stands.
    expect(await heldBy(PATH_A)).toBe(SESSION);
    await callBridge(bridge, "release_lock", { path: PATH_A, sessionId: SESSION });
  }, 60_000);

  it("runs every step under onError: continue and reports each verdict", async () => {
    const out = await batch([
      { method: "acquire_lock", params: { path: PATH_A, sessionId: SESSION } },
      { method: "acquire_lock", params: { path: PATH_A } },
      { method: "release_lock", params: { path: PATH_A, sessionId: SESSION } },
    ], { onError: "continue" });
    expect(out.success).toBe(false);
    expect(out).toMatchObject({ requestedCalls: 3, completedCalls: 3, failedIndex: 1, failedCount: 1, stoppedEarly: false });
    expect(steps(out).map((s) => s.success)).toEqual([true, false, true]);
    expect(await heldBy(PATH_A), "the release after the failure ran").toBeUndefined();
  }, 60_000);

  it("refuses the whole batch before running anything when a step names no handler", async () => {
    const out = await batch([
      { method: "acquire_lock", params: { path: PATH_A, sessionId: SESSION } },
      { method: "no_such_handler" },
    ]);
    expect(out.success).toBe(false);
    expect(String(out.error)).toContain("ops[1]: Unknown method: no_such_handler");
    expect(out.results).toBeUndefined();
    expect(await heldBy(PATH_A), "the first step never ran").toBeUndefined();
  }, 60_000);

  it("makes one Undo entry out of two transactional steps", async () => {
    const placed = body(await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: ACTOR, location: { x: 0, y: 13000, z: 200 },
    }));
    expect(placed.success).not.toBe(false);
    const before = body(await callBridge(bridge, "get_undo_state", {}));

    const out = await batch([
      { method: "move_actor", params: { actorLabel: ACTOR, location: { x: 100, y: 13000, z: 200 } } },
      { method: "move_actor", params: { actorLabel: ACTOR, location: { x: 200, y: 13000, z: 200 } } },
    ], { transactionDescription: "BCov batch move" });
    expect(out.success).toBe(true);
    expect(out.transaction).toMatchObject({ description: "BCov batch move", undoEntry: true, nested: false });

    const after = body(await callBridge(bridge, "get_undo_state", {}));
    expect(after.undoDescription, "the batch is what an undo would apply").toBe("BCov batch move");
    expect(Number(after.queueLength) - Number(before.queueLength), "one entry, not one per move").toBe(1);

    const details = body(await callBridge(bridge, "get_actor_details", { actorLabel: ACTOR }));
    expect((details.location as { x: number }).x).toBeCloseTo(200, 0);
  }, 60_000);
});
