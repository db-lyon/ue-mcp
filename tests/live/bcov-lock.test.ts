// @live-editor headless
/**
 * Asset locks: acquire_lock, release_lock and release_session_locks, each read
 * back through list_locks. The paths are virtual; nothing is created on disk.
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Editor";
const PATH_A = `${DIR}/BCov_Lock_A`;
const PATH_B = `${DIR}/BCov_Lock_B`;
const SESSION = "bcov-lock-session-a";
const OTHER = "bcov-lock-session-b";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function heldBy(path: string): Promise<string | undefined> {
  const listed = await call("list_locks", {});
  const lock = (listed.locks as Json[]).find((l) => l.path === path);
  return lock ? String(lock.sessionId) : undefined;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "release_session_locks", { sessionId: SESSION });
  await callBridge(bridge, "release_session_locks", { sessionId: OTHER });
}, 60_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "release_session_locks", { sessionId: SESSION });
    await callBridge(bridge, "release_session_locks", { sessionId: OTHER });
    disconnectBridge();
  }
});

describe("asset locks", () => {
  it("acquire_lock holds a path, refuses a second session and release_lock frees it", async () => {
    const got = await call("acquire_lock", { path: PATH_A, sessionId: SESSION, ttlSeconds: 120 });
    expect(got.acquired).toBe(true);
    expect(Number(got.ttlSecondsRemaining)).toBeGreaterThan(100);
    expect(await heldBy(PATH_A)).toBe(SESSION);

    const busy = await call("acquire_lock", { path: PATH_A, sessionId: OTHER });
    expect(busy.acquired).toBe(false);
    expect((busy.holder as Json).sessionId).toBe(SESSION);

    const wrong = await call("release_lock", { path: PATH_A, sessionId: OTHER });
    expect(wrong.released).toBe(false);
    expect(await heldBy(PATH_A)).toBe(SESSION);

    const freed = await call("release_lock", { path: PATH_A, sessionId: SESSION });
    expect(freed.released).toBe(true);
    expect(await heldBy(PATH_A)).toBeUndefined();
  }, 60_000);

  it("release_session_locks frees every path a session holds and leaves other sessions alone", async () => {
    await call("acquire_lock", { path: PATH_A, sessionId: SESSION });
    await call("acquire_lock", { path: PATH_B, sessionId: SESSION });
    const other = `${DIR}/BCov_Lock_C`;
    await call("acquire_lock", { path: other, sessionId: OTHER });

    const released = await call("release_session_locks", { sessionId: SESSION });
    expect(released.released).toBe(2);
    expect(await heldBy(PATH_A)).toBeUndefined();
    expect(await heldBy(PATH_B)).toBeUndefined();
    expect(await heldBy(other)).toBe(OTHER);

    await call("release_lock", { path: other, sessionId: OTHER });
  }, 60_000);
});
