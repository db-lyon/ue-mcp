/**
 * A shard editor that dies or hangs before it reports ready (#1609). The editor
 * is a node process standing in for UnrealEditor.exe, so no engine is needed.
 */
import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { launch } from "../../scripts/live-sharded-run.mjs";
import { nextLaunch } from "../../scripts/live-shards.mjs";

const spawned: number[] = [];
const dirs: string[] = [];

afterEach(async () => {
  for (const pid of spawned.splice(0)) {
    try {
      process.kill(pid);
    } catch {
      // already gone
    }
    for (let i = 0; i < 50 && isAlive(pid); i++) await new Promise((r) => setTimeout(r, 100));
  }
  for (const dir of dirs.splice(0)) fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
});

function shardDir(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-startup-"));
  dirs.push(dir);
  return dir;
}

/** A fake editor: runs `body` as a node script in the shard directory. */
function fakeEditor(dir: string, body: string) {
  return () => {
    const child = spawn(process.execPath, ["-e", body], { cwd: dir, stdio: "ignore", windowsHide: true });
    if (child.pid) spawned.push(child.pid);
    return child;
  };
}

/** What the real wait does for an editor that left no snapshot: polls to its budget, or until aborted. */
function pollsToBudget(_uproject: string, _dir: string, maxWaitSeconds: number, opts: { signal?: AbortSignal } = {}) {
  return new Promise((resolve) => {
    const timer = setTimeout(() => resolve({ ready: false, elapsedSeconds: maxWaitSeconds, timeline: [], reason: `still not ready after ${maxWaitSeconds}s` }), maxWaitSeconds * 1000);
    opts.signal?.addEventListener("abort", () => {
      clearTimeout(timer);
      resolve({ ready: false, elapsedSeconds: 0, timeline: [], reason: "aborted" });
    });
  });
}

const unreachable = async () => {
  throw new Error("the bridge must not be asked about an editor that never became ready");
};

const writeLog = (lines: string[]) => `
  const fs = require("fs");
  fs.mkdirSync("Saved/Logs", { recursive: true });
  fs.writeFileSync("Saved/Logs/ue_mcp.log", ${JSON.stringify(lines.join("\n") + "\n")});
`;

function isAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

describe("an editor that exits during startup", () => {
  it("is reported promptly with the crash its log records", async () => {
    const dir = shardDir();
    const job: Record<string, unknown> = { name: "h1", kind: "headless", dir, files: ["tests/live/a.test.ts"] };
    const started = Date.now();
    const crashLine = "[2026.10.28-10.00.01:000][  0]LogWindows: Error: appError called: Assertion failed: Module != nullptr";
    await expect(launch(job, "unused", null, {
      spawn: fakeEditor(dir, `${writeLog(["[2026.10.28-10.00.00:000][  0]LogInit: starting", crashLine])} process.exit(3);`),
      waitForEditorReady: pollsToBudget,
      answeringBridge: unreachable,
      readyTimeoutS: 60,
    })).rejects.toMatchObject({ startupDeath: true, death: { reason: expect.stringMatching(/^it crashed: Assertion failed: Module != nullptr/) } });
    expect(Date.now() - started).toBeLessThan(5000);
  }, 10_000);

  it("is reported as exiting with no crash recorded when its log has none", async () => {
    const dir = shardDir();
    const job: Record<string, unknown> = { name: "r1", kind: "render", dir, files: [] };
    const started = Date.now();
    await expect(launch(job, "unused", null, {
      spawn: fakeEditor(dir, `${writeLog(["[2026.10.28-10.00.00:000][  0]LogInit: starting"])} process.exit(0);`),
      waitForEditorReady: pollsToBudget,
      answeringBridge: unreachable,
      readyTimeoutS: 60,
    })).rejects.toMatchObject({ startupDeath: true, death: { reason: expect.stringMatching(/no crash in its log/) } });
    expect(Date.now() - started).toBeLessThan(5000);
  }, 10_000);
});

describe("an editor that never becomes ready", () => {
  it("fails its shard at the readiness budget and is ended", async () => {
    const dir = shardDir();
    const job: Record<string, unknown> = { name: "h2", kind: "headless", dir, files: [] };
    const started = Date.now();
    // A readiness wait that never returns, as when a probe inside it hangs.
    const neverSettles = () => new Promise(() => {});
    await expect(launch(job, "unused", null, {
      spawn: fakeEditor(dir, "setInterval(() => {}, 1000);"),
      waitForEditorReady: neverSettles,
      answeringBridge: unreachable,
      readyTimeoutS: 1,
      readyGraceMs: 500,
    })).rejects.toMatchObject({ startupTimeout: true, message: expect.stringMatching(/did not report ready within 1s/) });
    expect(Date.now() - started).toBeLessThan(5000);
    const pid = job.pid as number;
    for (let i = 0; i < 50 && isAlive(pid); i++) await new Promise((r) => setTimeout(r, 100));
    expect(isAlive(pid)).toBe(false);
  }, 10_000);
});

describe("the launcher", () => {
  it("finishes when nothing runs and no editor can start, even with headless files queued", () => {
    // Two headless editors failed to start, so the pool wants none; nothing else is queued or running.
    expect(nextLaunch({ wantPool: false, poolEditors: 0, fixedKind: null, running: 0 })).toEqual({ kind: null, done: true });
  });

  it("waits while an editor still runs, and starts the pool's first editor before fixed shards", () => {
    expect(nextLaunch({ wantPool: false, poolEditors: 1, fixedKind: null, running: 1 })).toEqual({ kind: null, done: false });
    expect(nextLaunch({ wantPool: true, poolEditors: 0, fixedKind: "render", running: 0 })).toEqual({ kind: "headless", poolFirst: true, done: false });
    expect(nextLaunch({ wantPool: true, poolEditors: 1, fixedKind: "render", running: 1 })).toEqual({ kind: "render", poolFirst: false, done: false });
  });
});
