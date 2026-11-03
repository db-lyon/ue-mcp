/**
 * A daemon that owns its project's discovery record steps down once the record
 * names another live process: a newer daemon took the project over, and two
 * daemons attached to one editor fight over it. Checked when the record changes
 * and on a timer, since a file watch can miss a rename.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { isPidAlive } from "../bridge/editor-target.js";
import { discoveryPath, readDiscovery, type DaemonDiscovery } from "./discovery.js";

export const SUPERSEDE_CHECK_MS = 5_000;

export interface SupersedeWatchOptions {
  projectDir: string;
  /** Called once, with the record of the process that took over. */
  onSuperseded(by: DaemonDiscovery): void;
  pid?: number;
  intervalMs?: number;
  read?: (projectDir: string) => DaemonDiscovery | null;
  isAlive?: (pid: number) => boolean;
  /** Off in tests that drive `check` themselves. */
  watchFile?: boolean;
}

export interface SupersedeWatch {
  /** Look at the record now. True when this process has been superseded. */
  check(): boolean;
  stop(): void;
}

export function watchSupersede(o: SupersedeWatchOptions): SupersedeWatch {
  const pid = o.pid ?? process.pid;
  const read = o.read ?? readDiscovery;
  const isAlive = o.isAlive ?? isPidAlive;
  let done = false;
  let watcher: fs.FSWatcher | null = null;

  const check = (): boolean => {
    if (done) return true;
    // A missing record is a stop in progress or a rename mid-flight, not a takeover.
    const d = read(o.projectDir);
    if (!d || d.pid === pid || !isAlive(d.pid)) return false;
    stop();
    done = true;
    o.onSuperseded(d);
    return true;
  };

  const timer = setInterval(check, o.intervalMs ?? SUPERSEDE_CHECK_MS);
  timer.unref();
  if (o.watchFile !== false) {
    const file = discoveryPath(o.projectDir);
    try {
      watcher = fs.watch(path.dirname(file), (_event, name) => {
        if (!name || name.toString() === path.basename(file)) check();
      });
      watcher.on("error", () => undefined);
      watcher.unref();
    } catch {
      // The timer alone still catches it.
    }
  }

  function stop(): void {
    clearInterval(timer);
    watcher?.close();
    watcher = null;
  }

  return {
    check,
    stop() {
      done = true;
      stop();
    },
  };
}

/** Wait for in-flight calls to finish, up to `ms`. True when they drained. */
export async function drainInflight(inflight: () => number, ms: number, sleep = (t: number) => new Promise((r) => setTimeout(r, t))): Promise<boolean> {
  const deadline = Date.now() + ms;
  while (inflight() > 0) {
    if (Date.now() >= deadline) return false;
    await sleep(250);
  }
  return true;
}
