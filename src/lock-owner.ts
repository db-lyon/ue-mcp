/**
 * Who holds an asset lock.
 *
 * A leaf module, importing nothing. It was inside `locking.ts` until that
 * module started reading an action's declared effect off the tool graph:
 * `tools/asset.ts` imports the process id from here, so leaving it under
 * `locking.ts` would close the cycle locking -> the tool graph -> asset ->
 * locking.
 */
import crypto from "node:crypto";

/**
 * Stable id for this server process, and the fallback owner for a lock op with
 * no editor session behind it.
 *
 * Locks live in the bridge, which is per editor, so the owner of a lock has to
 * be per editor too (#817). Two editors sharing one owner id makes a lock taken
 * in one look re-entrant in the other, which is the opposite of what locking is
 * for. Sessions carry their own id and pass it in; this stays as the answer for
 * a caller with no session, which is what a single-editor server had.
 */
export const SESSION_ID = crypto.randomUUID();

/** Mint an owner id for one editor session. */
export function newLockOwnerId(): string {
  return crypto.randomUUID();
}
