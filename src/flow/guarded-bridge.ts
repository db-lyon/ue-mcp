/**
 * GuardedBridge - a transparent source-control gate around the editor bridge.
 *
 * Every mutating action reaches Unreal through `IBridge.call`. This wrapper sits
 * on that one seam: before a write reaches the editor, it asks a guard to make
 * the target files writable (check them out) or to refuse (a human holds the
 * lock). The agent calls `level(save_level)` or `asset(save)` exactly as before;
 * the checkout happens underneath it.
 *
 * When no guard is set, this is a pure pass-through with a single map lookup of
 * overhead, so it is always safe to install even for users without source
 * control.
 *
 * Only the `call` path is gated. Connection lifecycle (connect / isConnected)
 * delegates straight through. The guard is invoked with resolved, on-disk file
 * paths that ALREADY EXIST - new files are filtered out upstream because a
 * not-yet-created asset needs `p4 add` at submit time, not a checkout now.
 */
import type { IBridge } from "../bridge.js";
import { classifyWrite } from "./write-methods.js";

export interface WriteInfo {
  /** Bare bridge method being called, e.g. "save_asset". */
  method: string;
  /** The call's params, passed through untouched for guard context. */
  params: Record<string, unknown>;
  /** Absolute, existing on-disk files the call will modify. */
  paths: string[];
}

/** Runs before a guarded write. Throw to DENY the write; return to allow it. */
export type WriteGuardFn = (info: WriteInfo) => Promise<void>;

/**
 * Resolve a UE content path (e.g. "/Game/Foo") to an absolute on-disk file,
 * returning null when it does not resolve or does not yet exist. Injected so the
 * bridge stays testable without a real project on disk.
 */
export type ResolveExistingFile = (contentPath: string) => string | null;

export class GuardedBridge implements IBridge {
  private guard: WriteGuardFn | null = null;

  constructor(
    private readonly inner: IBridge,
    private readonly resolveExistingFile: ResolveExistingFile,
  ) {}

  /** Install (or clear, with null) the write guard. */
  setGuard(guard: WriteGuardFn | null): void {
    this.guard = guard;
  }

  get isConnected(): boolean {
    return this.inner.isConnected;
  }

  connect(timeoutMs?: number): Promise<void> {
    return this.inner.connect(timeoutMs);
  }

  async call(
    method: string,
    params?: Record<string, unknown>,
    timeoutMs?: number,
  ): Promise<unknown> {
    const guard = this.guard;
    if (guard) {
      const p = params ?? {};
      const { writes, contentPaths } = classifyWrite(method, p);
      if (writes && contentPaths.length > 0) {
        const files = contentPaths
          .map((cp) => this.resolveExistingFile(cp))
          .filter((f): f is string => f !== null);
        if (files.length > 0) {
          // Throws to deny; the rejection propagates to the caller unchanged.
          await guard({ method, params: p, paths: files });
        }
      }
    }
    return this.inner.call(method, params, timeoutMs);
  }
}
