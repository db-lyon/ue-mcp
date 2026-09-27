import { isDialogRefusal } from "../editor/dialog-guard.js";
import type { IBridge } from "../bridge/bridge.js";
import { McpError, ErrorCode, type McpErrorDetails } from "../core/errors.js";
import { debug } from "../core/log.js";
import { bridgeMethodEffect, taskEffect } from "../surface/action-effects.js";
import { SESSION_ID } from "./lock-owner.js";
import type { AssetLockScopeLike, ToolContext, ToolDef } from "../core/types.js";

// Per-asset exclusive locking. The lock registry itself lives in the C++
// bridge (the one editor every agent shares); runAction acquires what each
// action writes into the run's scope, and the run releases it at the end,
// carrying the addressed editor's owner id. Two agents editing the same asset
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
export function classifyAction(
  taskName: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): ActionClassification {
  if (NEVER_LOCKED.has(taskName)) return { mutates: false, paths: [] };
  // A bare bridge method (an `ue-mcp.bridge` step) is judged by the method.
  const effect = taskName.includes(".") ? taskEffect(taskName, graph) : bridgeMethodEffect(taskName, params, graph);
  if (effect.effect === "read") return { mutates: false, paths: [] };

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
 * The locks one run holds: a live call, or a whole flow run. Each action
 * acquires what it writes as it starts, and everything is released once, when
 * the run ends, so no other agent writes between two steps of the same run.
 *
 * The editor's lock registry is re-entrant but not counted: a nested release
 * would free the outer holder's lock. Holding paths here and releasing only at
 * the end of the run is what keeps a child action from doing that.
 */
export class AssetLockScope implements AssetLockScopeLike {
  /** Held path to when its lease was last taken or refreshed. */
  private readonly held = new Map<string, number>();
  private unavailable = false;

  constructor(
    private readonly bridge: IBridge,
    private readonly cfg: LockingConfig,
    /** Who holds the locks. The addressed editor's id; omitted means this process. */
    private readonly ownerId: string = SESSION_ID,
    /** The graph whose declarations decide the effect. Omitted, the pristine one. */
    private readonly graph?: readonly ToolDef[],
  ) {}

  /** The paths currently held, for tests and status reporting. */
  get paths(): string[] {
    return [...this.held.keys()];
  }

  /**
   * Take the locks `taskName` needs before it runs. Throws a retryable
   * ASSET_LOCKED error on a busy asset, or the dialog refusal when a modal
   * refuses the request. An unreachable lock subsystem (older plugin, bridge
   * down) fails open and the run continues unlocked.
   */
  async acquireFor(taskName: string, params: Record<string, unknown>): Promise<void> {
    if (!this.cfg.enabled || this.unavailable) return;
    const { mutates, paths } = classifyAction(taskName, params, this.graph);
    if (!mutates || paths.length === 0) return;

    // A path held for over half its lease is taken again, which refreshes it.
    const now = Date.now();
    const halfLease = this.cfg.ttlSeconds * 500;
    const due = paths.filter((p) => {
      const at = this.held.get(p);
      return at === undefined || now - at > halfLease;
    });

    const taken: string[] = [];
    const releaseTaken = () => releaseAll(this.bridge, taken.filter((p) => !this.held.has(p)), this.ownerId);
    for (const p of due) {
      let res: { acquired?: boolean; holder?: { sessionId?: string; ttlSecondsRemaining?: number } } | undefined;
      try {
        res = (await this.bridge.call("acquire_lock", { path: p, sessionId: this.ownerId, ttlSeconds: this.cfg.ttlSeconds })) as typeof res;
      } catch (e) {
        // Lock subsystem unavailable - release what this call took and run
        // unlocked rather than failing a legitimate mutation.
        debug("lock", `acquire_lock unavailable for ${p}; running unlocked`, e);
        this.unavailable = true;
        await releaseTaken();
        return;
      }
      // A modal refuses the lock request itself. Reporting that as "another
      // session holds this asset" is false and tells the caller to retry, which
      // is the loop this whole mechanism exists to prevent. Hand the refusal up
      // unchanged so the guard shapes it.
      if (isDialogRefusal(res)) {
        await releaseTaken();
        // An McpError carrying the refusal as details, so the dispatcher can
        // recognise it: dialogBlocking is the field a client branches on.
        throw new McpError(
          ErrorCode.NOT_FOUND,
          String((res as Record<string, unknown>).error ?? "A modal dialog is blocking the editor."),
          res as unknown as McpErrorDetails,
        );
      }
      if (!res?.acquired) {
        await releaseTaken();
        const holder = res?.holder?.sessionId ?? "another session";
        const wait = res?.holder?.ttlSecondsRemaining;
        throw new McpError(
          ErrorCode.ASSET_LOCKED,
          `Asset '${p}' is locked by ${holder}${typeof wait === "number" ? ` (lease frees in ~${Math.ceil(wait)}s)` : ""}. Retry shortly or coordinate with the other session.`,
        );
      }
      taken.push(p);
    }
    for (const p of taken) this.held.set(p, now);
  }

  /** Release everything this run holds. Safe to call more than once. */
  async releaseAll(): Promise<void> {
    const paths = [...this.held.keys()];
    this.held.clear();
    await releaseAll(this.bridge, paths, this.ownerId);
  }
}

/**
 * Opens a lock scope for a run against the context's own bridge and owner, or
 * undefined when locking is off. Read at open time, so a context rebound to
 * another editor locks in that editor.
 */
export function lockScopeOpener(
  cfg: LockingConfig,
  graph: () => readonly ToolDef[],
): ((ctx: ToolContext) => AssetLockScope) | undefined {
  if (!cfg.enabled) return undefined;
  return (ctx) => new AssetLockScope(ctx.bridge, cfg, ctx.session?.lockOwnerId ?? SESSION_ID, graph());
}

/**
 * Run `run` while holding exclusive locks on every asset path the task would
 * mutate: one scope, acquired, run, released.
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
  const scope = new AssetLockScope(bridge, cfg, ownerId, graph);
  await scope.acquireFor(taskName, params);
  try {
    return await run();
  } finally {
    await scope.releaseAll();
  }
}
