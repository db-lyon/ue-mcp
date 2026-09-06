import crypto from "node:crypto";
import { isDialogRefusal } from "./dialog-guard.js";
import type { IBridge } from "./bridge.js";
import { McpError, ErrorCode, type McpErrorDetails } from "./errors.js";
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
// and is never locked (fail-open - locking never blocks a call we can't
// confidently classify).
// Exported because the routing gate (#817, action-class.ts) classifies the same
// surface for a different question and seeds itself from this lexicon rather
// than restating it. Its own matching rule is stricter; the lists are shared so
// a verb added for one is never missing from the other.
export const READ_PREFIXES = [
  "list", "search", "read", "get", "describe", "reflect", "find", "has", "status",
  "exists", "inspect", "preview", "validate", "count", "resolve", "diff",
];
export const MUTATE_PREFIXES = [
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
  const segments = action.toLowerCase().split(/[._]/);
  const first = segments[0] ?? action.toLowerCase();
  // "bulk" and "batch" describe the SHAPE of a call, not what it does, and
  // both are in MUTATE_PREFIXES because every batch action there has been so
  // far was a write. `bulk_read_properties` is not, and taking an asset lock
  // for a read would block a human's checkout on a call that only looks. When
  // the shape word is followed by a read verb, that verb is the answer.
  if ((first === "bulk" || first === "batch") && segments[1] && READ_PREFIXES.includes(segments[1])) {
    return segments[1];
  }
  return first;
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
