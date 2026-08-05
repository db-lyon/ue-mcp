import { describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {
  assertLoopbackHost,
  assertTestProjectDir,
  bridgePortCandidates,
  describeMissingBridge,
  deriveProjectPort,
  extractReportedProjectDir,
  isLoopbackHost,
  isTestProjectDir,
  LEGACY_BRIDGE_PORT,
  readPortLockfile,
  TEST_PORT_LOCKFILE,
  TEST_PROJECT_DIR,
  TEST_PROJECT_UPROJECT,
  verifyTestProjectTarget,
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

describe("host guard", () => {
  it("accepts loopback only", () => {
    expect(isLoopbackHost("127.0.0.1")).toBe(true);
    expect(isLoopbackHost("localhost")).toBe(true);
    expect(isLoopbackHost("::1")).toBe(true);
    expect(isLoopbackHost("192.168.1.20")).toBe(false);
    expect(isLoopbackHost("build-box.local")).toBe(false);
  });

  it("refuses to point the harness at another machine", () => {
    expect(() => assertLoopbackHost("192.168.1.20")).toThrow(/Refusing/);
    expect(() => assertLoopbackHost("127.0.0.1")).not.toThrow();
  });
});

describe("test-project guard", () => {
  it("accepts the repo test project in any path spelling", () => {
    expect(isTestProjectDir(TEST_PROJECT_DIR)).toBe(true);
    expect(isTestProjectDir(`${TEST_PROJECT_DIR}/`)).toBe(true);
    expect(isTestProjectDir(TEST_PROJECT_DIR.replace(/\\/g, "/").toUpperCase())).toBe(true);
  });

  it("refuses any other project", () => {
    expect(isTestProjectDir("C:/Users/dev/RealGame/")).toBe(false);
    expect(isTestProjectDir(`${TEST_PROJECT_DIR}_other`)).toBe(false);
    expect(isTestProjectDir(null)).toBe(false);
    expect(() => assertTestProjectDir("C:/Users/dev/RealGame/")).toThrow(/not the smoke test project/);
    expect(() => assertTestProjectDir("C:/Users/dev/RealGame/")).toThrow(/RealGame/);
  });

  it("extracts the reported project directory from a python result", () => {
    expect(extractReportedProjectDir({ output: "MCP_PROJECT_DIR:C:/x/y/\n" })).toBe("C:/x/y/");
    expect(extractReportedProjectDir("MCP_PROJECT_DIR:/home/dev/proj/")).toBe("/home/dev/proj/");
    expect(extractReportedProjectDir({ logs: ["noise", "MCP_PROJECT_DIR:/a/b/"] })).toBe("/a/b/");
    expect(extractReportedProjectDir({ output: "nothing here" })).toBeNull();
    // A backslashed Windows path survives JSON escaping.
    expect(extractReportedProjectDir({ output: "MCP_PROJECT_DIR:C:\\work\\proj\\" })).toBe("C:\\work\\proj\\");
  });

  it("passes when the editor reports the test project", async () => {
    await expect(
      verifyTestProjectTarget(async () => ({ output: `MCP_PROJECT_DIR:${TEST_PROJECT_DIR}/` })),
    ).resolves.toBeTruthy();
  });

  it("aborts when the editor has a different project open", async () => {
    await expect(
      verifyTestProjectTarget(async () => ({ output: "MCP_PROJECT_DIR:C:/Users/dev/RealGame/" })),
    ).rejects.toThrow(/not the smoke test project/);
  });

  it("aborts when the project cannot be identified at all", async () => {
    await expect(
      verifyTestProjectTarget(async () => {
        throw new Error("Unknown method: execute_python");
      }),
    ).rejects.toThrow(/could not confirm which project/);
    await expect(
      verifyTestProjectTarget(async () => ({ output: "" })),
    ).rejects.toThrow(/not the smoke test project/);
  });
});
