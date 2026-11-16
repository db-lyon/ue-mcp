// @session per-session: The lock registry lives in the addressed editor's bridge, under that session's owner id.
import { isDialogRefusal } from "../bridge/dialog-refusal.js";
import type { IBridge } from "../bridge/bridge.js";
import { McpError, ErrorCode, type McpErrorDetails } from "../core/errors.js";
import { debug } from "../core/log.js";
import { SESSION_ID } from "./lock-owner.js";
import { classifyAction } from "./written-paths.js";
import type { ToolDef } from "../surface/tool-types.js";

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
  /** The graph whose declarations decide the effect. Omitted, the pristine one. */
  graph?: readonly ToolDef[],
): Promise<T> {
  if (!cfg.enabled) return run();

  const { mutates, paths } = classifyAction(taskName, params, graph);
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
        ErrorCode.BRIDGE_ERROR,
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
