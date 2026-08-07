/**
 * The bridge's binding of flowkit's guard pipeline.
 *
 * Every mutating and non-mutating action crosses `IBridge.call`. That seam is
 * guarded: each guard may inspect the call, run a `before` hook that can veto
 * it (throw) or act on it (e.g. check a file out), and an `after` hook that can
 * observe or replace the result. The chain itself knows nothing about source
 * control, policy, rate limiting, or any concrete concern - those are guards.
 *
 * The pipeline, its ordering, and the `guard.<name>.<phase>` task convention
 * live in `@db-lyon/flowkit/guard`, because none of that is about Unreal. What
 * stays here is what is: the shape of a bridge call, and the write
 * classification in `write-methods.ts` that decides which content paths a call
 * is about to modify.
 */
import {
  GuardRegistry as FlowkitGuardRegistry,
  guardContextBase,
  lazy,
  type Guard,
  type GuardContext,
} from "@db-lyon/flowkit/guard";
import type { IBridge } from "../bridge.js";
import type { EditorSession } from "../session.js";
import { classifyWrite, type WriteClassification } from "./write-methods.js";

/** Resolve a UE content path to an absolute on-disk file, or null if it does not exist. */
export type ResolveExistingFile = (contentPath: string) => string | null;

/** Per-call execution context passed to every guard. */
export interface CallContext extends GuardContext {
  readonly method: string;
  readonly params: Record<string, unknown>;
  readonly timeoutMs?: number;
  /** The RAW bridge (never the guarded wrapper) for guards that must query the editor. */
  readonly bridge: IBridge;
  /** The editor this call is bound to. Absent only for a bridge built outside a session. */
  readonly session?: EditorSession;
  /** Lazy: how this call classifies as a write (which content paths it touches). Cached. */
  write(): WriteClassification;
  /** Lazy: absolute, existing on-disk files this call will modify (subset of write paths). Cached. */
  writeFiles(): string[];
}

/**
 * A guard on the bridge pipeline. Guards are agnostic: source control, access
 * policy, audit, rate limiting, and approval gating are all just guards.
 */
export type BridgeGuard = Guard<CallContext, unknown>;

/** The bridge's guard set. Built-in guards register directly; plugin guards are discovered. */
export class GuardRegistry extends FlowkitGuardRegistry<CallContext, unknown> {}

/**
 * The scope a `guard.<name>.<phase>Write` task binds to: the call resolves to
 * existing on-disk files it is about to modify. Declared here rather than in
 * the task layer so the hand-written and task-backed guards agree on what
 * "write" means.
 */
export function writeScope(ctx: CallContext): boolean {
  return ctx.writeFiles().length > 0;
}

/** Build the per-call context, wiring the lazy write-enrichment helpers. */
export function makeCallContext(
  method: string,
  params: Record<string, unknown>,
  timeoutMs: number | undefined,
  rawBridge: IBridge,
  resolveExistingFile: ResolveExistingFile,
  session?: EditorSession,
): CallContext {
  const ctx = {
    ...guardContextBase(),
    method,
    params,
    timeoutMs,
    bridge: rawBridge,
    session,
  } as CallContext;

  const write = lazy(ctx, "write", () => classifyWrite(method, params));
  const writeFiles = lazy(ctx, "writeFiles", () => {
    const c = write();
    return c.writes
      ? c.contentPaths.map(resolveExistingFile).filter((f): f is string => f !== null)
      : [];
  });

  return Object.assign(ctx, { write, writeFiles });
}
