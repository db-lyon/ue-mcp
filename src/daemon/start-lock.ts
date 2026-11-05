/**
 * One daemon per project. A starter holds an exclusive lock beside the
 * discovery file from before it starts until it has published, and anyone
 * else who arrives meanwhile waits for that daemon instead of starting one.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { isPidAlive } from "../bridge/editor-target.js";
import { info } from "../core/log.js";
import { daemonDir, projectKey, readAnsweringDiscovery, readLiveDiscovery, type DaemonDiscovery } from "./discovery.js";

/** A lock older than this is stale even if its pid is alive, which covers pid reuse. */
export const START_LOCK_STALE_MS = 5 * 60_000;
/** A lock that cannot be read yet may be mid-write; it is stale only after this. */
const UNREADABLE_GRACE_MS = 10_000;

export interface StartLock {
  pid: number;
  /** Epoch ms the lock was taken. */
  at: number;
}

export interface StartLockOptions {
  pid?: number;
  isAlive?: (pid: number) => boolean;
  now?: () => number;
  staleMs?: number;
  /** Probe the published daemon and drop a record proven stale (#1460). */
  isStale?: (d: DaemonDiscovery) => Promise<boolean>;
}

export function startLockPath(projectDir: string): string {
  return path.join(daemonDir(), `${projectKey(projectDir)}.start.lock`);
}

function readLockFile(file: string): { lock: StartLock | null; mtimeMs: number } | null {
  let mtimeMs: number;
  let raw: string;
  try {
    mtimeMs = fs.statSync(file).mtimeMs;
    raw = fs.readFileSync(file, "utf-8");
  } catch {
    return null;
  }
  try {
    const v = JSON.parse(raw) as Partial<StartLock>;
    if (typeof v.pid === "number" && typeof v.at === "number") return { lock: { pid: v.pid, at: v.at }, mtimeMs };
  } catch {
    // half written, or junk
  }
  return { lock: null, mtimeMs };
}

/** The lock when a live starter holds it, else null. */
export function liveStartLock(projectDir: string, opts: StartLockOptions = {}): StartLock | null {
  const isAlive = opts.isAlive ?? isPidAlive;
  const now = (opts.now ?? Date.now)();
  const staleMs = opts.staleMs ?? START_LOCK_STALE_MS;
  const read = readLockFile(startLockPath(projectDir));
  if (!read) return null;
  if (!read.lock) return now - read.mtimeMs < UNREADABLE_GRACE_MS ? { pid: 0, at: read.mtimeMs } : null;
  if (!isAlive(read.lock.pid) || now - read.lock.at >= staleMs) return null;
  return read.lock;
}

/** Take the lock, reclaiming a stale one. False when a live starter holds it. */
export function tryTakeStartLock(projectDir: string, opts: StartLockOptions = {}): boolean {
  const file = startLockPath(projectDir);
  const pid = opts.pid ?? process.pid;
  fs.mkdirSync(path.dirname(file), { recursive: true, mode: 0o700 });
  for (let attempt = 0; attempt < 2; attempt++) {
    try {
      const fd = fs.openSync(file, "wx", 0o600);
      try {
        fs.writeSync(fd, JSON.stringify({ pid, at: (opts.now ?? Date.now)() }));
      } finally {
        fs.closeSync(fd);
      }
      return true;
    } catch (e) {
      // Windows reports EPERM for a file another process is deleting.
      const code = (e as NodeJS.ErrnoException).code;
      if (code !== "EEXIST" && code !== "EPERM") throw e;
    }
    if (liveStartLock(projectDir, opts)) return false;
    try {
      fs.rmSync(file, { force: true });
    } catch {
      return false;
    }
  }
  return false;
}

/** Remove the lock, but only if `pid` holds it. */
export function releaseStartLock(projectDir: string, pid: number = process.pid): void {
  const file = startLockPath(projectDir);
  const read = readLockFile(file);
  if (read?.lock?.pid !== pid) return;
  try {
    fs.rmSync(file, { force: true });
  } catch {
    // goes stale on its own
  }
}

export type StartTurn =
  /** A live daemon that will do is published; do not start another. */
  | { action: "defer"; live: DaemonDiscovery }
  /** This process holds the lock. `existing` is a live daemon it must take over from, if any. */
  | { action: "start"; existing: DaemonDiscovery | null };

/**
 * Wait until this process may start the project's daemon, or until a daemon
 * that satisfies it has published. Ends because a lock goes stale.
 */
export async function waitForStartTurn(
  projectDir: string,
  satisfies: (d: DaemonDiscovery) => boolean,
  opts: StartLockOptions & { pollMs?: number } = {},
): Promise<StartTurn> {
  const pid = opts.pid ?? process.pid;
  const live = async (): Promise<DaemonDiscovery | null> => {
    const d = opts.isStale
      ? await readAnsweringDiscovery(projectDir, { isAlive: opts.isAlive, isStale: opts.isStale })
      : readLiveDiscovery(projectDir, opts.isAlive);
    return d && d.pid !== pid ? d : null;
  };
  let reported = false;
  for (;;) {
    const before = await live();
    if (before && satisfies(before)) return { action: "defer", live: before };
    if (tryTakeStartLock(projectDir, opts)) {
      // A daemon may have published between the read and the take.
      const after = await live();
      if (after && satisfies(after)) {
        releaseStartLock(projectDir, pid);
        return { action: "defer", live: after };
      }
      return { action: "start", existing: after };
    }
    if (!reported) {
      const holder = liveStartLock(projectDir, opts);
      info("daemon", `another daemon is starting${holder?.pid ? ` (pid ${holder.pid})` : ""}; waiting for it`);
      reported = true;
    }
    await new Promise((r) => setTimeout(r, opts.pollMs ?? 250));
  }
}
