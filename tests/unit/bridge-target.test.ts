import { describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {
  bridgePortCandidates,
  describeMissingBridge,
  deriveProjectPort,
  LEGACY_BRIDGE_PORT,
  readPortLockfile,
  TEST_PORT_LOCKFILE,
  TEST_PROJECT_DIR,
  TEST_PROJECT_UPROJECT,
} from "../../scripts/bridge-target.mjs";
import { deriveProjectPort as srcDeriveProjectPort } from "../../src/port.js";

function withTempLockfile(contents: string | null): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-lock-"));
  const file = path.join(dir, "port.json");
  if (contents !== null) fs.writeFileSync(file, contents, "utf8");
  return file;
}

describe("port derivation parity", () => {
  it("matches src/port.ts, which matches the C++ bridge", () => {
    for (const p of [
      TEST_PROJECT_DIR,
      "C:/Users/dev/GameA",
      "C:\\Users\\Dev\\GameA\\",
      "/home/dev/projects/thing",
    ]) {
      expect(deriveProjectPort(p)).toBe(srcDeriveProjectPort(p));
    }
  });
});

describe("readPortLockfile", () => {
  it("reports a missing lockfile without throwing, keeping the path", () => {
    const file = path.join(os.tmpdir(), "ue-mcp-lock-does-not-exist", "port.json");
    const lock = readPortLockfile(file);
    expect(lock.exists).toBe(false);
    expect(lock.port).toBeNull();
    expect(lock.error).toBe("not found");
    expect(lock.path).toBe(file);
  });

  it("reads the bound port the bridge published", () => {
    const file = withTempLockfile(
      JSON.stringify({ port: 63300, pid: 4242, startedAt: "2026-01-01T00:00:00Z", apiVersion: 1 }),
    );
    const lock = readPortLockfile(file);
    expect(lock.port).toBe(63300);
    expect(lock.pid).toBe(4242);
    expect(lock.exists).toBe(true);
    expect(lock.error).toBeNull();
  });

  it("rejects a malformed lockfile instead of yielding a bogus port", () => {
    expect(readPortLockfile(withTempLockfile("{not json")).error).toMatch(/not valid JSON/);
    expect(readPortLockfile(withTempLockfile(JSON.stringify({ port: 0 }))).port).toBeNull();
    expect(readPortLockfile(withTempLockfile(JSON.stringify({ port: 99999 }))).port).toBeNull();
    expect(readPortLockfile(withTempLockfile(JSON.stringify({}))).error).toMatch(/no usable "port" field/);
  });
});

describe("bridgePortCandidates", () => {
  const noLockfile = { path: TEST_PORT_LOCKFILE, exists: false, port: null, pid: null, startedAt: null, error: "not found", pidAlive: null };

  it("tries the published port first, then the derived one, then the legacy one", () => {
    const lockfile = { ...noLockfile, exists: true, port: 63300, error: null };
    const { candidates } = bridgePortCandidates({ lockfile });
    expect(candidates.map((c) => c.port)).toEqual([
      63300,
      deriveProjectPort(TEST_PROJECT_DIR),
      LEGACY_BRIDGE_PORT,
    ]);
    expect(candidates[0].source).toBe("lockfile");
  });

  it("still finds a bridge with no lockfile at all", () => {
    const { candidates } = bridgePortCandidates({ lockfile: noLockfile });
    expect(candidates.map((c) => c.port)).toEqual([
      deriveProjectPort(TEST_PROJECT_DIR),
      LEGACY_BRIDGE_PORT,
    ]);
  });

  it("does not probe the same port twice", () => {
    const lockfile = { ...noLockfile, exists: true, port: LEGACY_BRIDGE_PORT, error: null };
    const { candidates } = bridgePortCandidates({ lockfile });
    expect(candidates.filter((c) => c.port === LEGACY_BRIDGE_PORT)).toHaveLength(1);
  });

  it("honours an explicit port and probes nothing else", () => {
    const lockfile = { ...noLockfile, exists: true, port: 63300, error: null };
    const { candidates } = bridgePortCandidates({ explicitPort: 51234, lockfile });
    expect(candidates).toEqual([{ port: 51234, source: "explicit" }]);
  });
});

describe("describeMissingBridge", () => {
  it("names the exact lockfile path and every port it tried", () => {
    const lockfile = readPortLockfile(path.join(os.tmpdir(), "ue-mcp-absent", "port.json"));
    const { candidates } = bridgePortCandidates({ lockfile });
    const msg = describeMissingBridge({ host: "127.0.0.1", candidates, lockfile, lastError: "ECONNREFUSED" });

    expect(msg).toContain(lockfile.path);
    expect(msg).toContain(TEST_PROJECT_UPROJECT);
    for (const c of candidates) expect(msg).toContain(`ws://127.0.0.1:${c.port}`);
    expect(msg).toContain("not found");
    expect(msg).toContain("ECONNREFUSED");
  });

  it("calls out a stale lockfile whose editor is gone", () => {
    const lockfile = { path: TEST_PORT_LOCKFILE, exists: true, port: 63300, pid: 4242, startedAt: null, error: null, pidAlive: false };
    const { candidates } = bridgePortCandidates({ lockfile });
    expect(describeMissingBridge({ host: "127.0.0.1", candidates, lockfile })).toContain("stale lockfile");
  });
});

