/**
 * The bridge instances watch: a watcher that fails after it started emits
 * 'error', and on Windows that is EPERM when the folder goes away. Without a
 * listener the emit throws and the daemon exits, so the watch closes, warns
 * once, and rewatches when it can.
 */
import { afterAll, afterEach, beforeAll, describe, expect, it, vi } from "vitest";
import { EventEmitter } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { watchDir, type WatchFn } from "../../src/daemon/dir-watch.js";
import { startDaemon, type Daemon } from "../../src/daemon/daemon.js";
import { restoreEnv } from "../helpers/env.js";
import { userSandboxEnv } from "../helpers/user-sandbox.js";

class FakeWatcher extends EventEmitter {
  closed = false;
  close(): void {
    this.closed = true;
  }
}

function eperm(): NodeJS.ErrnoException {
  return Object.assign(new Error("EPERM: operation not permitted, watch"), { code: "EPERM", errno: -4048, syscall: "watch" });
}

function fakeWatch(): { watch: WatchFn; made: FakeWatcher[]; fail: { next: boolean } } {
  const made: FakeWatcher[] = [];
  const fail = { next: false };
  const watch: WatchFn = () => {
    if (fail.next) throw eperm();
    const w = new FakeWatcher();
    made.push(w);
    return w as never;
  };
  return { watch, made, fail };
}

let sandbox: string;

beforeAll(() => {
  sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-dirwatch-"));
});

afterAll(() => {
  fs.rmSync(sandbox, { recursive: true, force: true });
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe("watchDir", () => {
  it("survives an error event, closes the watcher and warns once", () => {
    vi.useFakeTimers();
    const stderr = vi.spyOn(console, "error").mockImplementation(() => {});
    const { watch, made, fail } = fakeWatch();
    const w = watchDir(path.join(sandbox, "a"), () => {}, { watch, retryMs: 1000 });
    expect(w.watching).toBe(true);

    fail.next = true;
    expect(() => made[0].emit("error", eperm())).not.toThrow();
    expect(made[0].closed).toBe(true);
    expect(w.watching).toBe(false);

    // Retries that keep failing stay quiet.
    vi.advanceTimersByTime(3000);
    const warnings = stderr.mock.calls.map((c) => String(c[0])).filter((l) => l.includes("cannot watch"));
    expect(warnings).toHaveLength(1);
    w.close();
  });

  it("watches again once it can, and reports a change so nothing published meanwhile is missed", () => {
    vi.useFakeTimers();
    vi.spyOn(console, "error").mockImplementation(() => {});
    const { watch, made } = fakeWatch();
    const onChange = vi.fn();
    const w = watchDir(path.join(sandbox, "b"), onChange, { watch, retryMs: 1000 });

    made[0].emit("error", eperm());
    expect(onChange).not.toHaveBeenCalled();
    vi.advanceTimersByTime(1000);
    expect(made).toHaveLength(2);
    expect(w.watching).toBe(true);
    expect(onChange).toHaveBeenCalledTimes(1);

    // The new watcher fails too: still handled.
    expect(() => made[1].emit("error", eperm())).not.toThrow();
    w.close();
    vi.advanceTimersByTime(5000);
    expect(made).toHaveLength(2);
  });

  it("retries a watch that throws at the start", () => {
    vi.useFakeTimers();
    vi.spyOn(console, "error").mockImplementation(() => {});
    const { watch, made, fail } = fakeWatch();
    fail.next = true;
    const w = watchDir(path.join(sandbox, "c"), () => {}, { watch, retryMs: 1000 });
    expect(w.watching).toBe(false);
    fail.next = false;
    vi.advanceTimersByTime(1000);
    expect(made).toHaveLength(1);
    expect(w.watching).toBe(true);
    w.close();
    expect(made[0].closed).toBe(true);
  });
});

describe("daemon with a failing instances watch", () => {
  const savedEnv = { ...process.env };
  let daemon: Daemon;
  const { watch, made } = fakeWatch();

  beforeAll(async () => {
    for (const k of Object.keys(process.env)) if (k.startsWith("UE_MCP_")) delete process.env[k];
    process.env.UE_MCP_PORT = "1";
    process.env.UE_MCP_DISABLE_UPDATE_CHECK = "1";
    const home = path.join(sandbox, "home");
    Object.assign(process.env, userSandboxEnv(home));
    process.env.HOME = home;
    process.env.USERPROFILE = home;
    const projectDir = path.join(sandbox, "Proj");
    fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
    const uproject = path.join(projectDir, "Proj.uproject");
    fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));
    daemon = await startDaemon({
      projects: [uproject],
      publish: false,
      extensionsDir: path.join(sandbox, "extensions"),
      uiDir: path.join(sandbox, "ui"),
      watch,
    });
  }, 180_000);

  afterAll(async () => {
    await daemon?.close();
    restoreEnv(savedEnv);
  });

  it("keeps answering after the watcher emits EPERM", async () => {
    expect(made.length).toBeGreaterThan(0);
    expect(() => made[0].emit("error", eperm())).not.toThrow();
    expect(made[0].closed).toBe(true);
    const res = await fetch(`http://127.0.0.1:${daemon.port}/v1/health`, { headers: { authorization: `Bearer ${daemon.token}` } });
    expect(res.status).toBe(200);
  });
});
