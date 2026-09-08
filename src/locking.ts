import { isDialogRefusal } from "./dialog-guard.js";
import type { IBridge } from "./bridge.js";
import { McpError, ErrorCode, type McpErrorDetails } from "./errors.js";
import { debug } from "./log.js";
import { taskEffect } from "./action-effects.js";
import { SESSION_ID } from "./lock-owner.js";

// Per-asset exclusive locking, orchestrated from the dispatch layer. The lock
// registry itself lives in the C++ bridge (the one editor every agent shares);
// this module just wraps each mutating dispatch with acquire/release calls
// carrying a stable per-process session id. Two agents editing the same asset
// serialize; a single agent never blocks itself (same session re-acquires are
// re-entrant); a crashed agent's locks expire on their TTL.
//
// Enforcement is opt-in (ue-mcp.yml `ue-mcp.locking.enabled`) because it adds
// two bridge round-trips per mutating call and only matters when more than one
// agent drives one editor. The explicit asset(lock/unlock/list_locks) actions
// work regardless of this setting.

/**
 * The lock owner ids, re-exported for callers that still import them here.
 * They live in `lock-owner.ts`, a leaf, for the same reason the verb lexicon
 * does: this module now imports the tool graph.
 */
export { SESSION_ID, newLockOwnerId } from "./lock-owner.js";

export interface LockingConfig {
  enabled: boolean;
  ttlSeconds: number;
}

export function resolveLockingConfig(cfg?: { enabled?: boolean; ttlSeconds?: number }): LockingConfig {
  return {
    enabled: cfg?.enabled === true,
    ttlSeconds: typeof cfg?.ttlSeconds === "number" && cfg.ttlSeconds > 0 ? cfg.ttlSeconds : 300,
  };
}

/**
 * The verb lexicon this module used to own, re-exported for the callers that
 * still import it from here.
 *
 * It moved to `action-verbs.ts` when locking started reading an action's
 * DECLARED effect: a lexicon underneath `locking.ts` closes an import cycle the
 * moment locking imports the tool graph.
 */
export { READ_PREFIXES, MUTATE_PREFIXES } from "./action-verbs.js";

/** Keys whose string value is an in-editor asset path (not a filesystem source). */
const PATH_KEYS = [
  "assetPath", "path", "blueprintPath", "sourcePath", "destinationPath",
  "targetPath", "materialPath",
];

export interface ActionClassification {
  mutates: boolean;
  /** Distinct asset paths this call would mutate (may be empty even when mutating). */
  paths: string[];
}

function looksLikeAssetPath(v: unknown): v is string {
  return typeof v === "string" && v.length > 0 && v.includes("/");
}

/**
 * The lock-management actions themselves, which must never take a lock.
 *
 * They change the editor's lock registry, so they declare `mutate` and the
 * routing gate is right to treat them as one. Locking is a different question:
 * taking an asset lock around `asset(lock)` would have this module acquire a
 * lock in order to acquire a lock. The exclusion was invisible before because
 * "lock" and "unlock" simply were not in the verb list; it is stated here
 * rather than left to an omission nobody could see.
 */
const NEVER_LOCKED = new Set(["asset.lock", "asset.unlock", "asset.unlock_all", "asset.list_locks"]);

/**
 * Decide whether a task mutates an asset and which asset path(s) it touches.
 *
 * The mutation half is the action's DECLARED effect, and `unknown` counts: an
 * action whose effect its parameters decide may well write the asset it names,
 * and being wrong costs one serialised call rather than two agents writing the
 * same package.
 *
 * This module used to answer from a verb list of its own and fail OPEN, so an
 * unrecognised verb ran unlocked. That was the right call while the answer was
 * a guess, and it is why `unwrap_uvs` and `fixup_redirectors` never took a
 * lock: neither verb was in the list, and nothing said so out loud. The answer
 * is not a guess any more, so there is nothing left to fail open about, and a
 * name this server does not carry gets the same `mutate` default every other
 * gate gives it.
 *
 * An unextractable path still yields an empty list, so a declared mutation that
 * names no asset runs unlocked exactly as before. That is what keeps this from
 * locking the world: the path, not the verdict, is the narrow part.
 */
export function classifyAction(taskName: string, params: Record<string, unknown>): ActionClassification {
  if (NEVER_LOCKED.has(taskName)) return { mutates: false, paths: [] };
  if (taskEffect(taskName).effect === "read") return { mutates: false, paths: [] };

  const paths = new Set<string>();
  for (const key of PATH_KEYS) {
    if (looksLikeAssetPath(params[key])) paths.add(params[key] as string);
  }
  // Batch shapes.
  if (Array.isArray(params.assetPaths)) {
    for (const p of params.assetPaths) if (looksLikeAssetPath(p)) paths.add(p);
  }
  if (Array.isArray(params.renames)) {
    for (const r of params.renames) {
      const rr = r as Record<string, unknown>;
      if (looksLikeAssetPath(rr?.sourcePath)) paths.add(rr.sourcePath as string);
      else if (looksLikeAssetPath(rr?.assetPath)) paths.add(rr.assetPath as string);
    }
  }
  if (Array.isArray(params.items)) {
    for (const item of params.items) {
      const descriptor = item as Record<string, unknown>;
      if (looksLikeAssetPath(descriptor?.assetPath)) paths.add(descriptor.assetPath as string);
    }
  }
  // Batch mesh material assignment: the mesh is written, the material is only
  // read, so only assetPath is locked.
  if (Array.isArray(params.assignments)) {
    for (const a of params.assignments) {
      const entry = a as Record<string, unknown>;
      if (looksLikeAssetPath(entry?.assetPath)) paths.add(entry.assetPath as string);
    }
  }
  return { mutates: true, paths: [...paths] };
}

async function releaseAll(bridge: IBridge, paths: string[], ownerId: string): Promise<void> {
  for (const p of paths) {
    try {
      await bridge.call("release_lock", { path: p, sessionId: ownerId });
    } catch (e) {
      debug("lock", `release_lock failed for ${p} (lease will expire)`, e);
    }
  }
}

/**
 * Run `run` while holding exclusive locks on every asset path the task would
 * mutate. On a busy asset, throws a retryable ASSET_LOCKED error. If the lock
 * subsystem is unreachable (older plugin without the handlers, bridge down),
 * fails open and runs unlocked.
 */
export async function withAssetLocks<T>(
  bridge: IBridge,
  cfg: LockingConfig,
  taskName: string,
  params: Record<string, unknown>,
  run: () => Promise<T>,
  /** Who holds the locks. The addressed editor's id; omitted means this process. */
  ownerId: string = SESSION_ID,
): Promise<T> {
  if (!cfg.enabled) return run();

  const { mutates, paths } = classifyAction(taskName, params);
  if (!mutates || paths.length === 0) return run();

  const held: string[] = [];
  for (const p of paths) {
    let res: { acquired?: boolean; holder?: { sessionId?: string; ttlSecondsRemaining?: number } } | undefined;
    try {
      res = (await bridge.call("acquire_lock", { path: p, sessionId: ownerId, ttlSeconds: cfg.ttlSeconds })) as typeof res;
    } catch (e) {
      // Lock subsystem unavailable - release what we took and run unlocked
      // rather than failing a legitimate mutation.
      debug("lock", `acquire_lock unavailable for ${p}; running unlocked`, e);
      await releaseAll(bridge, held, ownerId);
      return run();
    }
    // A modal refuses the lock request itself. Reporting that as "another
    // session holds this asset" is false and tells the caller to retry, which
    // is the loop this whole mechanism exists to prevent. Hand the refusal up
    // unchanged so the guard shapes it.
    if (isDialogRefusal(res)) {
      // Release first: the sibling branch below does, and not doing it here
      // stranded every lock already taken for this call until its TTL expired.
      await releaseAll(bridge, held, ownerId);
      // An McpError carrying the refusal as details, so the dispatcher can
      // recognise it. A bare Error reached the caller with no dialogBlocking
      // flag, which is the one field a client branches on, and machineErrorBlock
      // dropped the payload entirely because it only reads McpError.
      throw new McpError(
        ErrorCode.NOT_FOUND,
        String((res as Record<string, unknown>).error ?? "A modal dialog is blocking the editor."),
        res as unknown as McpErrorDetails,
      );
    }
    if (!res?.acquired) {
      await releaseAll(bridge, held, ownerId);
      const holder = res?.holder?.sessionId ?? "another session";
      const wait = res?.holder?.ttlSecondsRemaining;
      throw new McpError(
        ErrorCode.ASSET_LOCKED,
        `Asset '${p}' is locked by ${holder}${typeof wait === "number" ? ` (lease frees in ~${Math.ceil(wait)}s)` : ""}. Retry shortly or coordinate with the other session.`,
      );
    }
    held.push(p);
  }

  try {
    return await run();
  } finally {
    await releaseAll(bridge, held, ownerId);
  }
}
