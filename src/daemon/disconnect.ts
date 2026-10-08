/**
 * Why the editor went away, decided from evidence rather than guessed.
 *
 * - restarting / rebuild: the daemon said beforehand it was about to do that.
 * - unknown: the editor process is still alive; only the socket dropped.
 * - crashed: the process is gone and its instance record is still on disk.
 *   The bridge deletes its own record on a clean shutdown, so a record that
 *   outlives its process is what a crash leaves.
 * - closed: the process is gone and so is its record.
 */
import * as fs from "node:fs";
import { isPidAlive, type BridgeInstanceRecord } from "../bridge/editor-target.js";

export type DisconnectCause = "closed" | "crashed" | "restarting" | "rebuild" | "unknown";

export interface DisconnectEvidence {
  /** The editor's instance record as it was while connected, if one was found. */
  record: BridgeInstanceRecord | null;
  /** What the daemon announced it was about to do, if it did, and is still within its window. */
  expected: "restarting" | "rebuild" | null;
  isAlive?: (pid: number) => boolean;
  recordExists?: (recordPath: string) => boolean;
}

export function classifyDisconnect(e: DisconnectEvidence): DisconnectCause {
  if (e.expected) return e.expected;
  if (!e.record) return "unknown";
  const alive = (e.isAlive ?? isPidAlive)(e.record.pid);
  if (alive) return "unknown";
  const exists = (e.recordExists ?? fs.existsSync)(e.record.recordPath);
  return exists ? "crashed" : "closed";
}

/** An announced restart or rebuild, valid for a window so a stale one cannot mislabel a later crash. */
export class ExpectedDisconnect {
  private current: { cause: "restarting" | "rebuild"; until: number } | null = null;

  expect(cause: "restarting" | "rebuild", windowMs = 10 * 60_000): void {
    this.current = { cause, until: Date.now() + windowMs };
  }

  clear(): void {
    this.current = null;
  }

  get(): "restarting" | "rebuild" | null {
    if (!this.current) return null;
    if (Date.now() > this.current.until) {
      this.current = null;
      return null;
    }
    return this.current.cause;
  }
}
