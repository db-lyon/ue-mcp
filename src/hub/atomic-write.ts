// @session independent: File writes over the paths they are handed; process-wide plumbing with no editor state.
/**
 * Writes that go through a temporary file and a rename, so a reader or a
 * restart never sees half a file.
 *
 * Windows refuses the rename while another process has the destination open:
 * a read of the file at that moment, an antivirus scan, the search indexer.
 * The refusal is EPERM, EBUSY or EACCES and lasts milliseconds, so the rename
 * is tried again a few times with a growing pause before it counts as failed.
 *
 * A store written from an event handler (the session store above all) must
 * never throw out of it: Studio runs inside the ue-mcp daemon, and an
 * exception there ends every session and the editor bridge with it.
 * PendingWrites keeps what could not be written in memory and writes it on the
 * next change, or a little later on its own.
 */
import * as fs from "node:fs";

/** The codes a rename gets while something holds the destination open. */
const TRANSIENT = new Set(["EPERM", "EBUSY", "EACCES"]);

/** The pauses between tries of a refused rename: about a third of a second in all. */
export const RENAME_PAUSES_MS: readonly number[] = [5, 10, 20, 40, 80, 160];

const sleeper = new Int32Array(new SharedArrayBuffer(4));

/** What the writes call, so a test can refuse a rename the way Windows does. */
export const atomicIo = {
  rename: (from: string, to: string): void => fs.renameSync(from, to),
  /** A synchronous pause: the stores' saves are synchronous, and the pauses are a few milliseconds. */
  pause: (ms: number): void => void Atomics.wait(sleeper, 0, 0, ms),
};

export function isTransientFsError(e: unknown): boolean {
  return TRANSIENT.has(String((e as { code?: unknown } | null)?.code ?? ""));
}

/** Rename, trying again while the refusal looks like a lock held for a moment. Throws the last error once the pauses run out. */
export function renameRetrying(from: string, to: string, pauses: readonly number[] = RENAME_PAUSES_MS): void {
  for (let i = 0; ; i++) {
    try {
      atomicIo.rename(from, to);
      return;
    } catch (e) {
      if (!isTransientFsError(e) || i >= pauses.length) throw e;
      atomicIo.pause(pauses[i]);
    }
  }
}

/** Write a file through a temporary one and a rename. Throws when it cannot, leaving the previous file and no temporary one. */
export function writeFileAtomic(file: string, data: string | Uint8Array, opts: { mode?: number } = {}): void {
  const tmp = `${file}.${process.pid}.tmp`;
  fs.writeFileSync(tmp, data, opts.mode !== undefined ? { mode: opts.mode } : undefined);
  try {
    renameRetrying(tmp, file);
  } catch (e) {
    fs.rmSync(tmp, { force: true });
    throw e;
  }
}

export interface PendingWritesOptions {
  /** Told once when a file cannot be written, and once when it is written again. */
  log: (msg: string, e?: unknown) => void;
  /** How long after a failure the next try comes on its own; it doubles up to a minute. */
  retryMs?: number;
}

/**
 * Writes that never throw. One that fails is kept, by its file, and tried again
 * on the next write through this, and on a timer, until it goes through or a
 * newer write of the same file replaces it.
 */
export class PendingWrites {
  private readonly failed = new Map<string, () => void>();
  private timer: ReturnType<typeof setTimeout> | null = null;
  private delay: number;

  constructor(private readonly o: PendingWritesOptions) {
    this.delay = o.retryMs ?? 2000;
  }

  /** Run `write` for `file`. False when it failed and is kept for later. */
  write(file: string, write: () => void): boolean {
    const was = this.failed.delete(file);
    const ok = this.attempt(file, write, was);
    // The others go again only after a write that went through: one that just failed would only wait out its pauses twice.
    if (ok) this.flush();
    else this.schedule();
    return ok;
  }

  /** A file that is gone now: a kept write of it must not bring it back. */
  forget(file: string): void {
    this.failed.delete(file);
    if (!this.failed.size) this.stopTimer();
  }

  /** Files waiting to be written again. */
  pending(): string[] {
    return [...this.failed.keys()];
  }

  /** Try every kept write again now. */
  flush(): void {
    for (const [file, write] of [...this.failed]) {
      this.failed.delete(file);
      this.attempt(file, write, true);
    }
    if (!this.failed.size) {
      this.stopTimer();
      this.delay = this.o.retryMs ?? 2000;
    } else this.schedule();
  }

  dispose(): void {
    this.stopTimer();
  }

  private attempt(file: string, write: () => void, retried: boolean): boolean {
    try {
      write();
      if (retried) this.o.log(`wrote ${file} on a later try`);
      return true;
    } catch (e) {
      if (!retried) this.o.log(`could not write ${file}; it is kept in memory and written again on the next change`, e);
      this.failed.set(file, write);
      return false;
    }
  }

  private schedule(): void {
    if (this.timer) return;
    this.timer = setTimeout(() => {
      this.timer = null;
      this.delay = Math.min(this.delay * 2, 60_000);
      this.flush();
    }, this.delay);
    this.timer.unref?.();
  }

  private stopTimer(): void {
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
  }
}
