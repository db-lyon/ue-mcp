/**
 * A general guard pipeline around the editor bridge, in the shape of NestJS
 * guards/interceptors but agnostic to what any guard does.
 *
 * Every mutating and non-mutating action crosses `IBridge.call`. A `GuardedBridge`
 * runs a registry of `BridgeGuard`s on that seam: each guard may inspect the call,
 * run a `before` hook that can veto it (throw) or act on it (e.g. check a file
 * out), and an optional `after` hook that can observe or replace the result
 * (audit, transform). The chain itself knows nothing about source control,
 * policy, rate limiting, or any concrete concern - those are guards.
 *
 * `CallContext` is the per-call execution context handed to guards. It carries
 * the raw method/params and a lazy enrichment layer (`write()` / `writeFiles()`)
 * that write-oriented guards may consult, computed on demand and cached so a
 * guard that ignores writes pays nothing.
 */
import type { IBridge } from "../bridge.js";
import { classifyWrite, type WriteClassification } from "./write-methods.js";

/** Resolve a UE content path to an absolute on-disk file, or null if it does not exist. */
export type ResolveExistingFile = (contentPath: string) => string | null;

/** Per-call execution context passed to every guard. Analogous to a NestJS ExecutionContext. */
export interface CallContext {
  readonly method: string;
  readonly params: Record<string, unknown>;
  readonly timeoutMs?: number;
  /** The RAW bridge (never the guarded wrapper) for guards that must query the editor. */
  readonly bridge: IBridge;
  /** Scratch space shared across guards for the life of one call. */
  readonly meta: Map<string, unknown>;
  /** Lazy: how this call classifies as a write (which content paths it touches). Cached. */
  write(): WriteClassification;
  /** Lazy: absolute, existing on-disk files this call will modify (subset of write paths). Cached. */
  writeFiles(): string[];
}

/**
 * A guard on the bridge pipeline. Any of the hooks is optional. Guards are
 * agnostic: source control, access policy, audit, rate limiting, and approval
 * gating are all just guards.
 */
export interface BridgeGuard {
  /** Stable identifier, for logging and ordering ties. */
  readonly name: string;
  /** Lower runs first in `before` and last in `after`. Default 0. */
  readonly order?: number;
  /** Whether this guard participates for a given call. Default: always. */
  appliesTo?(ctx: CallContext): boolean | Promise<boolean>;
  /** Runs before the call. Throw to DENY the call; side effects are allowed. */
  before?(ctx: CallContext): Promise<void>;
  /** Runs after a successful call. Return a value to replace the result; return nothing to leave it. */
  after?(ctx: CallContext, result: unknown): Promise<unknown | void>;
}

/** An ordered set of guards. Built-in guards register directly; plugin guards are discovered. */
export class GuardRegistry {
  private guards: BridgeGuard[] = [];

  register(guard: BridgeGuard): this {
    this.guards.push(guard);
    // Stable order: by `order`, then by name for determinism.
    this.guards.sort((a, b) => (a.order ?? 0) - (b.order ?? 0) || a.name.localeCompare(b.name));
    return this;
  }

  list(): readonly BridgeGuard[] {
    return this.guards;
  }

  get size(): number {
    return this.guards.length;
  }
}

/** Build the per-call context, wiring the lazy write-enrichment helpers. */
export function makeCallContext(
  method: string,
  params: Record<string, unknown>,
  timeoutMs: number | undefined,
  rawBridge: IBridge,
  resolveExistingFile: ResolveExistingFile,
): CallContext {
  let writeCache: WriteClassification | undefined;
  let filesCache: string[] | undefined;

  const write = (): WriteClassification => (writeCache ??= classifyWrite(method, params));
  const writeFiles = (): string[] => {
    if (filesCache) return filesCache;
    const c = write();
    filesCache = c.writes
      ? c.contentPaths.map(resolveExistingFile).filter((f): f is string => f !== null)
      : [];
    return filesCache;
  };

  return { method, params, timeoutMs, bridge: rawBridge, meta: new Map(), write, writeFiles };
}
