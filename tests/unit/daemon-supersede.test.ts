import { describe, expect, it, vi } from "vitest";
import type { DaemonDiscovery } from "../../src/daemon/discovery.js";
import { drainInflight, waitForExit, watchSupersede } from "../../src/daemon/supersede-watch.js";

const record = (pid: number): DaemonDiscovery => ({
  pid,
  port: 1,
  token: "t",
  version: "1.0.0",
  apiVersion: 1,
  projectRoot: "p",
  startedAt: "",
});

describe("watchSupersede", () => {
  const setup = (current: DaemonDiscovery | null, alive: (pid: number) => boolean) => {
    let rec = current;
    const onSuperseded = vi.fn();
    const w = watchSupersede({ projectDir: "p", pid: 10, read: () => rec, isAlive: alive, onSuperseded, watchFile: false, intervalMs: 60_000 });
    return { w, onSuperseded, set: (r: DaemonDiscovery | null) => (rec = r) };
  };

  it("stays up while the record is its own, missing, or names a dead process", () => {
    const { w, onSuperseded, set } = setup(record(10), () => true);
    expect(w.check()).toBe(false);
    set(null);
    expect(w.check()).toBe(false);
    const dead = setup(record(20), () => false);
    expect(dead.w.check()).toBe(false);
    expect(onSuperseded).not.toHaveBeenCalled();
    expect(dead.onSuperseded).not.toHaveBeenCalled();
    w.stop();
    dead.w.stop();
  });

  it("steps down once when the record names another live daemon", () => {
    const { w, onSuperseded, set } = setup(record(10), () => true);
    set(record(20));
    expect(w.check()).toBe(true);
    expect(w.check()).toBe(true);
    expect(onSuperseded).toHaveBeenCalledTimes(1);
    expect(onSuperseded.mock.calls[0][0]).toMatchObject({ pid: 20 });
  });
});

describe("drainInflight", () => {
  it("returns once calls finish", async () => {
    let n = 2;
    const drained = await drainInflight(() => n, 10_000, async () => {
      n--;
    });
    expect(drained).toBe(true);
  });

  it("gives up after the deadline when a call never returns", async () => {
    const now = vi.spyOn(Date, "now");
    let t = 0;
    now.mockImplementation(() => t);
    const drained = await drainInflight(() => 1, 1_000, async (ms) => {
      t += ms;
    });
    now.mockRestore();
    expect(drained).toBe(false);
  });
});

describe("waitForExit", () => {
  it("returns true once the process is gone", async () => {
    let alive = 3;
    expect(await waitForExit(1, 10_000, () => alive-- > 0, async () => undefined)).toBe(true);
  });

  it("returns false when the process outlives the deadline", async () => {
    const now = vi.spyOn(Date, "now");
    let t = 0;
    now.mockImplementation(() => t);
    const exited = await waitForExit(1, 1_000, () => true, async (ms) => {
      t += ms;
    });
    now.mockRestore();
    expect(exited).toBe(false);
  });
});
