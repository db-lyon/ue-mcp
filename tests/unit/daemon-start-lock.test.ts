/**
 * One daemon per project: the start lock beside the discovery file, the wait
 * a racing starter does instead of starting its own, and the real thing with
 * several `daemon run` processes started at once.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { spawn, type ChildProcess } from "node:child_process";
import { fileURLToPath } from "node:url";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { normalizeProjectRoot } from "../../src/bridge/port.js";
import { isPidAlive } from "../../src/bridge/editor-target.js";
import { daemonDir, projectKey, readLiveDiscovery, writeDiscovery, type DaemonDiscovery } from "../../src/daemon/discovery.js";
import { isOlder } from "../../src/daemon/daemon.js";
import { ensureDaemon } from "../../src/daemon/shim.js";
import { packageVersion } from "../../src/core/package-root.js";
import { userSandboxEnv } from "../helpers/user-sandbox.js";
import { covers } from "../helpers/covers.js";
import {
  liveStartLock, releaseStartLock, startLockPath, tryTakeStartLock, waitForStartTurn, START_LOCK_STALE_MS,
} from "../../src/daemon/start-lock.js";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

let dir: string;
let saved: string | undefined;
beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-start-lock-"));
  saved = process.env.UE_MCP_DAEMON_DIR;
  process.env.UE_MCP_DAEMON_DIR = path.join(dir, "daemons");
});
afterEach(() => {
  if (saved === undefined) delete process.env.UE_MCP_DAEMON_DIR;
  else process.env.UE_MCP_DAEMON_DIR = saved;
  fs.rmSync(dir, { recursive: true, force: true });
});

const project = path.join(os.tmpdir(), "Some", "LockedProject");
const record = (pid: number, version: string): DaemonDiscovery => ({
  pid, port: 1000 + pid, token: "t", version, apiVersion: 1,
  projectRoot: normalizeProjectRoot(project), startedAt: "now",
});
const sameOrNewer = (version: string) => (d: DaemonDiscovery) => !isOlder(d.version, version);

describe("the start lock", () => {
  const alive = new Set<number>();
  const isAlive = (pid: number) => alive.has(pid);
  beforeEach(() => alive.clear());

  it("is held by one starter at a time and released only by its holder", () => {
    alive.add(1).add(2);
    expect(tryTakeStartLock(project, { pid: 1, isAlive })).toBe(true);
    expect(tryTakeStartLock(project, { pid: 2, isAlive })).toBe(false);
    releaseStartLock(project, 2);
    expect(liveStartLock(project, { isAlive })?.pid).toBe(1);
    releaseStartLock(project, 1);
    expect(fs.existsSync(startLockPath(project))).toBe(false);
    expect(tryTakeStartLock(project, { pid: 2, isAlive })).toBe(true);
  });

  it("is reclaimed from a dead pid", () => {
    alive.add(2);
    expect(tryTakeStartLock(project, { pid: 1, isAlive: () => true })).toBe(true);
    expect(liveStartLock(project, { isAlive })).toBeNull();
    expect(tryTakeStartLock(project, { pid: 2, isAlive })).toBe(true);
    expect(liveStartLock(project, { isAlive })?.pid).toBe(2);
  });

  it("is reclaimed once it is older than the start timeout, whoever holds it", () => {
    alive.add(1).add(2);
    const t0 = Date.now();
    expect(tryTakeStartLock(project, { pid: 1, isAlive, now: () => t0 })).toBe(true);
    const later = () => t0 + START_LOCK_STALE_MS;
    expect(liveStartLock(project, { isAlive, now: later })).toBeNull();
    expect(tryTakeStartLock(project, { pid: 2, isAlive, now: later })).toBe(true);
  });

  it("treats a lock it cannot read as held while it may still be mid-write", () => {
    fs.mkdirSync(path.dirname(startLockPath(project)), { recursive: true });
    fs.writeFileSync(startLockPath(project), "");
    expect(tryTakeStartLock(project, { pid: 1, isAlive: () => true })).toBe(false);
    expect(tryTakeStartLock(project, { pid: 1, isAlive: () => true, now: () => Date.now() + 60_000 })).toBe(true);
  });
});

describe("waitForStartTurn", () => {
  const alive = new Set<number>();
  const isAlive = (pid: number) => alive.has(pid);
  beforeEach(() => alive.clear());

  it("lets one of several concurrent starters start, and the rest defer to it", async () => {
    const pids = [11, 12, 13, 14, 15];
    for (const p of pids) alive.add(p);
    const started: number[] = [];
    const turns = await Promise.all(pids.map(async (pid) => {
      const turn = await waitForStartTurn(project, sameOrNewer("1.4.6"), { pid, isAlive, pollMs: 10 });
      if (turn.action === "start") {
        started.push(pid);
        // Starting takes a while; nobody else may start meanwhile.
        await new Promise((r) => setTimeout(r, 100));
        writeDiscovery(project, record(pid, "1.4.6"));
        releaseStartLock(project, pid);
      }
      return turn;
    }));
    expect(started).toHaveLength(1);
    for (const t of turns) if (t.action === "defer") expect(t.live.pid).toBe(started[0]);
    expect(readLiveDiscovery(project, isAlive)?.pid).toBe(started[0]);
    expect(fs.existsSync(startLockPath(project))).toBe(false);
  });

  it("starts after a holder dies without publishing", async () => {
    alive.add(21).add(22);
    expect(tryTakeStartLock(project, { pid: 21, isAlive })).toBe(true);
    const waiting = waitForStartTurn(project, sameOrNewer("1.4.6"), { pid: 22, isAlive, pollMs: 10 });
    await new Promise((r) => setTimeout(r, 50));
    alive.delete(21);
    expect(await waiting).toEqual({ action: "start", existing: null });
  });

  it("defers to a live same-or-newer daemon, and takes over from an older one", async () => {
    alive.add(31).add(32);
    writeDiscovery(project, record(31, "1.4.6"));
    expect(await waitForStartTurn(project, sameOrNewer("1.4.6"), { pid: 32, isAlive })).toMatchObject({ action: "defer", live: { pid: 31 } });
    expect(fs.existsSync(startLockPath(project))).toBe(false);

    writeDiscovery(project, record(31, "1.4.5"));
    const turn = await waitForStartTurn(project, sameOrNewer("1.4.6"), { pid: 32, isAlive });
    expect(turn).toMatchObject({ action: "start", existing: { pid: 31, version: "1.4.5" } });
    expect(liveStartLock(project, { isAlive })?.pid).toBe(32);
  });

  it("an older starter waits out a newer one and then defers to it", async () => {
    alive.add(41).add(42);
    expect(tryTakeStartLock(project, { pid: 41, isAlive })).toBe(true);
    const waiting = waitForStartTurn(project, sameOrNewer("1.4.5"), { pid: 42, isAlive, pollMs: 10 });
    await new Promise((r) => setTimeout(r, 50));
    writeDiscovery(project, record(41, "1.4.6"));
    releaseStartLock(project, 41);
    expect(await waiting).toMatchObject({ action: "defer", live: { pid: 41 } });
  });
});

describe("ensureDaemon", () => {
  it("spawns nothing while a live starter holds the lock, and returns what it publishes", async () => {
    expect(tryTakeStartLock(project)).toBe(true);
    const ready = ensureDaemon(path.join(project, "LockedProject.uproject"));
    await new Promise((r) => setTimeout(r, 600));
    writeDiscovery(project, record(process.pid, packageVersion()));
    releaseStartLock(project);
    expect((await ready).pid).toBe(process.pid);
    // The spawn path is the only thing that opens the daemon log.
    expect(fs.existsSync(path.join(daemonDir(), `${projectKey(project)}.log`))).toBe(false);
  });
});

describe("several `daemon run` processes at once", () => {
  const children: ChildProcess[] = [];
  afterEach(() => {
    for (const c of children) if (c.exitCode === null) c.kill();
    children.length = 0;
  });

  it("leave exactly one daemon running, the one that published", async () => {

    covers("daemon.start-lock", "cli.daemon.run");
    const projectDir = path.join(dir, "Raced");
    fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
    const uproject = path.join(projectDir, "Raced.uproject");
    fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));
    const env: Record<string, string> = {};
    for (const [k, v] of Object.entries(process.env)) if (v !== undefined && !k.startsWith("UE_MCP_")) env[k] = v;
    Object.assign(env, {
      UE_MCP_PORT: "1",
      UE_MCP_HOST: "127.0.0.1",
      ...userSandboxEnv(path.join(dir, "home")),
      UE_MCP_DAEMON_DIR: path.join(dir, "daemons"),
      UE_MCP_DISABLE_UPDATE_CHECK: "1",
      UE_MCP_DAEMON_IDLE_MS: "60000",
    });

    const exits: Promise<number | null>[] = [];
    for (let i = 0; i < 4; i++) {
      const child = spawn(process.execPath, ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), "daemon", "run", uproject], {
        cwd: REPO_ROOT, env, stdio: ["ignore", "ignore", fs.openSync(path.join(dir, `run-${i}.log`), "a")], windowsHide: true,
      });
      children.push(child);
      exits.push(new Promise((resolve) => child.once("exit", (code) => resolve(code))));
    }

    const deadline = Date.now() + 90_000;
    let published: DaemonDiscovery | null = null;
    const publishers = new Set<number>();
    while (Date.now() < deadline) {
      published = readLiveDiscovery(projectDir);
      if (published) publishers.add(published.pid);
      if (published && children.filter((c) => c.exitCode === null).length === 1) break;
      await new Promise((r) => setTimeout(r, 200));
    }
    expect(published).not.toBeNull();
    expect([...publishers]).toEqual([published!.pid]);
    const running = children.filter((c) => c.exitCode === null);
    expect(running.map((c) => c.pid)).toEqual([published!.pid]);
    for (const c of children) if (c !== running[0]) expect(c.exitCode).toBe(0);
    expect(fs.existsSync(startLockPath(projectDir))).toBe(false);

    await fetch(`http://127.0.0.1:${published!.port}/v1/shutdown`, { method: "POST", headers: { authorization: `Bearer ${published!.token}` } });
    await Promise.race([Promise.all(exits), new Promise((r) => setTimeout(r, 10_000))]);
    expect(isPidAlive(published!.pid)).toBe(false);
  }, 120_000);
});
