import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  bridgeLockfilePath,
  isPidAlive,
  lockfileIsFromThisLaunch,
  readBridgeLockfileIn,
  resolveBridgeTarget,
} from "../../src/editor-target.js";

const temporaryRoots: string[] = [];

function makeProjectDir(): string {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-target-"));
  temporaryRoots.push(root);
  return root;
}

function writeLockfile(projectDir: string, contents: unknown): string {
  const file = bridgeLockfilePath(projectDir);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, typeof contents === "string" ? contents : JSON.stringify(contents));
  return file;
}

const savedEnvPort = process.env.UE_MCP_PORT;

beforeEach(() => {
  delete process.env.UE_MCP_PORT;
});

afterEach(() => {
  if (savedEnvPort === undefined) delete process.env.UE_MCP_PORT;
  else process.env.UE_MCP_PORT = savedEnvPort;
  while (temporaryRoots.length > 0) {
    fs.rmSync(temporaryRoots.pop()!, { recursive: true, force: true });
  }
});

describe("readBridgeLockfileIn", () => {
  it("reads the port, pid and write time the bridge published", () => {
    const projectDir = makeProjectDir();
    writeLockfile(projectDir, { port: 51234, pid: 4242, startedAt: "2026-08-05T10:00:00Z", apiVersion: 1 });

    const lockfile = readBridgeLockfileIn(projectDir);
    expect(lockfile?.port).toBe(51234);
    expect(lockfile?.pid).toBe(4242);
    expect(lockfile?.startedAt).toBe("2026-08-05T10:00:00Z");
    expect(lockfile!.writtenAtMs).toBeGreaterThan(0);
  });

  it("reports no pid rather than a bogus one on older plugin builds", () => {
    const projectDir = makeProjectDir();
    writeLockfile(projectDir, { port: 51234 });
    expect(readBridgeLockfileIn(projectDir)?.pid).toBeNull();
  });

  it("rejects a malformed or out-of-range port", () => {
    const projectDir = makeProjectDir();
    writeLockfile(projectDir, { port: 0 });
    expect(readBridgeLockfileIn(projectDir)).toBeNull();
    writeLockfile(projectDir, { port: 70000 });
    expect(readBridgeLockfileIn(projectDir)).toBeNull();
    writeLockfile(projectDir, "not json at all");
    expect(readBridgeLockfileIn(projectDir)).toBeNull();
  });

  it("returns null when the file is absent", () => {
    expect(readBridgeLockfileIn(makeProjectDir())).toBeNull();
  });
});

describe("resolveBridgeTarget", () => {
  it("returns the port the project published for itself", () => {
    const projectDir = makeProjectDir();
    writeLockfile(projectDir, { port: 52001, pid: 99 });

    const target = resolveBridgeTarget(projectDir);
    expect(target.ok).toBe(true);
    if (!target.ok) return;
    expect(target.port).toBe(52001);
    expect(target.pid).toBe(99);
    expect(target.lockfilePath).toBe(bridgeLockfilePath(projectDir));
  });

  it("fails with the lockfile path it checked when nothing is published", () => {
    const projectDir = makeProjectDir();
    const target = resolveBridgeTarget(projectDir);
    expect(target.ok).toBe(false);
    if (target.ok) return;
    expect(target.lockfilePath).toBe(bridgeLockfilePath(projectDir));
    expect(target.reason).toContain(bridgeLockfilePath(projectDir));
  });

  it("never falls back to the legacy fixed port 9877", () => {
    // A guessed port is how a stop request reached an editor pinned to 9877
    // that belonged to a completely different project (#819).
    const target = resolveBridgeTarget(makeProjectDir());
    expect(target.ok).toBe(false);
    expect(JSON.stringify(target)).not.toContain("9877");
  });

  it("never falls back to UE_MCP_PORT", () => {
    process.env.UE_MCP_PORT = "9877";
    const target = resolveBridgeTarget(makeProjectDir());
    expect(target.ok).toBe(false);
  });

  it("fails without inventing a path when no project is loaded", () => {
    const target = resolveBridgeTarget(undefined);
    expect(target.ok).toBe(false);
    if (target.ok) return;
    expect(target.lockfilePath).toBeNull();
    expect(target.reason).toContain("set_project");
  });

  it("ignores a lockfile whose port is unusable", () => {
    const projectDir = makeProjectDir();
    writeLockfile(projectDir, { port: -1, pid: 5 });
    const target = resolveBridgeTarget(projectDir);
    expect(target.ok).toBe(false);
  });
});

describe("lockfileIsFromThisLaunch", () => {
  it("accepts any lockfile when no launch time is known", () => {
    expect(lockfileIsFromThisLaunch(0)).toBe(true);
  });

  it("rejects a lockfile a crashed earlier session left behind", () => {
    const launchedAt = Date.now();
    expect(lockfileIsFromThisLaunch(launchedAt - 3_600_000, launchedAt)).toBe(false);
  });

  it("accepts the lockfile this launch published", () => {
    const launchedAt = Date.now();
    expect(lockfileIsFromThisLaunch(launchedAt + 12_000, launchedAt)).toBe(true);
  });

  it("tolerates coarse filesystem timestamps around the launch instant", () => {
    const launchedAt = Date.now();
    expect(lockfileIsFromThisLaunch(launchedAt - 1000, launchedAt)).toBe(true);
  });
});

describe("isPidAlive", () => {
  it("sees this process", () => {
    expect(isPidAlive(process.pid)).toBe(true);
  });

  it("reports a pid that cannot exist as gone", () => {
    // Above every platform's pid_max, so it is never allocated.
    expect(isPidAlive(0x7ffffff0)).toBe(false);
  });
});
