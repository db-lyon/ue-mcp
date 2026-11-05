/**
 * A directory watch that cannot take the daemon down. fs.watch reports a
 * failure after it started (the folder deleted, or EPERM on Windows) as an
 * 'error' event, and an EventEmitter with no 'error' listener throws, which
 * ends the process. The watch is only an accelerator, so a failure closes it,
 * warns once, and retries until the folder can be watched again.
 */
import * as fs from "node:fs";
import { info, warn } from "../core/log.js";

/** The slice of fs.watch the daemon needs, injectable for tests. */
export type WatchFn = (dir: string, listener: () => void) => Pick<fs.FSWatcher, "on" | "close">;

export interface DirWatchOptions {
  watch?: WatchFn;
  /** How often to try watching again after a failure. */
  retryMs?: number;
}

export interface DirWatch {
  /** Whether a watcher is live now. */
  readonly watching: boolean;
  close(): void;
}

const DEFAULT_RETRY_MS = 10_000;

/** Watch `dir`, creating it, and call `onChange` on every change and after a rewatch. */
export function watchDir(dir: string, onChange: () => void, opts: DirWatchOptions = {}): DirWatch {
  const watch: WatchFn = opts.watch ?? ((d, l) => fs.watch(d, l));
  const retryMs = opts.retryMs ?? DEFAULT_RETRY_MS;
  let watcher: Pick<fs.FSWatcher, "on" | "close"> | null = null;
  let retry: NodeJS.Timeout | null = null;
  let warned = false;
  let closed = false;

  const fail = (e: unknown): void => {
    if (!warned) {
      warned = true;
      warn("daemon", `cannot watch ${dir}; reattach falls back to the reconnect interval until it can`, e);
    }
    if (!closed && !retry) {
      retry = setInterval(() => {
        if (start()) {
          info("daemon", `watching ${dir} again`);
          onChange();
        }
      }, retryMs);
      retry.unref();
    }
  };

  const start = (): boolean => {
    try {
      fs.mkdirSync(dir, { recursive: true });
      const w = watch(dir, onChange);
      w.on("error", (e: unknown) => {
        if (watcher !== w) return;
        watcher = null;
        try {
          w.close();
        } catch {
          // Already closed by the failure.
        }
        fail(e);
      });
      watcher = w;
      if (retry) clearInterval(retry);
      retry = null;
      return true;
    } catch (e) {
      fail(e);
      return false;
    }
  };

  start();
  return {
    get watching() {
      return watcher !== null;
    },
    close() {
      closed = true;
      if (retry) clearInterval(retry);
      retry = null;
      watcher?.close();
      watcher = null;
    },
  };
}
