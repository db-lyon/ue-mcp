/**
 * Who can be asked on behalf of the call in flight.
 *
 * A guard pipeline is built once per editor, but the person to ask belongs to
 * each call: the MCP client that sent it, or the daemon extension that made it
 * in process. Bridge calls a task makes run inside the dispatch that started
 * it, so async context carries that caller down to the guard without threading
 * it through every task and bridge signature.
 */
import { AsyncLocalStorage } from "node:async_hooks";
import type { ElicitFn } from "../core/types.js";
import type { SessionAccessMode } from "./session-access.js";

export interface CallAsker {
  /** The caller's elicitation gate, if it has one. */
  readonly elicit?: ElicitFn;
  /** The `category.action` the caller asked for, when known. */
  readonly subject?: string;
  /** The sending session's access mode, read when a guard asks. Absent is read-write. */
  readonly access?: () => SessionAccessMode;
}

const store = new AsyncLocalStorage<CallAsker>();

/** Run `fn` with `asker` as the caller every bridge call inside it answers to. */
export function withCallAsker<T>(asker: CallAsker, fn: () => T): T {
  return store.run(asker, fn);
}

/** The caller of the call in flight, or undefined outside any dispatch. */
export function currentCallAsker(): CallAsker | undefined {
  return store.getStore();
}
