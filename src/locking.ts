import crypto from "node:crypto";
import type { IBridge } from "./bridge.js";
import { McpError, ErrorCode } from "./errors.js";
import { debug } from "./log.js";

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

/** Stable id for this server process; sent with every lock op. */
export const SESSION_ID = crypto.randomUUID();

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

// Action-name prefixes that mutate an asset. Matched against the action segment
// of a task name ("asset.create_data_asset" -> "create_data_asset"). Read verbs
// are excluded first, so an unrecognized action falls through to "not mutating"
// and is never locked (fail-open — locking never blocks a call we can't
// confidently classify).
const READ_PREFIXES = [
  "list", "search", "read", "get", "describe", "reflect", "find", "has", "status",
  "exists", "inspect", "preview", "validate", "count", "resolve", "diff",
];
const MUTATE_PREFIXES = [
  "create", "set", "add", "remove", "delete", "rename", "move", "duplicate",
  "import", "reimport", "save", "update", "connect", "disconnect", "spawn",
  "compile", "apply", "assign", "insert", "replace", "clear", "reset", "modify",
  "write", "recenter", "bulk", "batch", "attach", "detach", "enable", "disable",
  "bake", "generate", "build",
];

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

function firstSegmentVerb(action: string): string {
  // "create_data_asset" -> "create"; "bulk_rename" -> "bulk".
  const seg = action.split(/[._]/, 1)[0] ?? action;
  return seg.toLowerCase();
}

function looksLikeAssetPath(v: unknown): v is string {
  return typeof v === "string" && v.length > 0 && v.includes("/");
}

/**
 * Decide whether a task mutates an asset and which asset path(s) it touches.
 * Conservative: unknown verbs and unextractable paths yield mutates=false /
 * empty paths so the caller runs unlocked.
 */
export function classifyAction(taskName: string, params: Record<string, unknown>): ActionClassification {
  const action = taskName.includes(".") ? taskName.slice(taskName.indexOf(".") + 1) : taskName;
  const verb = firstSegmentVerb(action);

  if (READ_PREFIXES.includes(verb)) return { mutates: false, paths: [] };
  if (!MUTATE_PREFIXES.includes(verb)) return { mutates: false, paths: [] };

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
  return { mutates: true, paths: [...paths] };
}

async function releaseAll(bridge: IBridge, paths: string[]): Promise<void> {
  for (const p of paths) {
    try {
      await bridge.call("release_lock", { path: p, sessionId: SESSION_ID });
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
): Promise<T> {
  if (!cfg.enabled) return run();

  const { mutates, paths } = classifyAction(taskName, params);
  if (!mutates || paths.length === 0) return run();

  const held: string[] = [];
  for (const p of paths) {
    let res: { acquired?: boolean; holder?: { sessionId?: string; ttlSecondsRemaining?: number } } | undefined;
    try {
      res = (await bridge.call("acquire_lock", { path: p, sessionId: SESSION_ID, ttlSeconds: cfg.ttlSeconds })) as typeof res;
    } catch (e) {
      // Lock subsystem unavailable — release what we took and run unlocked
      // rather than failing a legitimate mutation.
      debug("lock", `acquire_lock unavailable for ${p}; running unlocked`, e);
      await releaseAll(bridge, held);
      return run();
    }
    if (!res?.acquired) {
      await releaseAll(bridge, held);
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
    await releaseAll(bridge, held);
  }
}
