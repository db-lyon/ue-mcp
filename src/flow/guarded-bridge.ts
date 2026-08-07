/**
 * GuardedBridge - runs a `GuardRegistry` pipeline around the editor bridge.
 *
 * Before a call reaches Unreal, each applicable guard's `before` hook runs in
 * order (any may throw to deny). After a successful call, each guard's `after`
 * hook runs in reverse order and may replace the result. With an empty registry
 * this is a pure pass-through, so it is always safe to install.
 *
 * The pipeline itself is `runGuarded` from flowkit; what this class adds is the
 * `IBridge` shape. Only `call` is gated; connection lifecycle delegates
 * straight through.
 */
import { runGuarded } from "@db-lyon/flowkit/guard";
import type { BridgeTarget, IBridge } from "../bridge.js";
import type { EditorSession } from "../session.js";
import { GuardRegistry, makeCallContext, type ResolveExistingFile } from "./guard.js";

export type { ResolveExistingFile } from "./guard.js";

export class GuardedBridge implements IBridge {
  constructor(
    private readonly inner: IBridge,
    private readonly registry: GuardRegistry,
    private readonly resolveExistingFile: ResolveExistingFile,
    /** The session this pipeline belongs to, so guards can see which editor
     *  they are guarding rather than assuming the process has only one. */
    private readonly session?: EditorSession,
  ) {}

  get isConnected(): boolean {
    return this.inner.isConnected;
  }

  connect(timeoutMs?: number): Promise<void> {
    return this.inner.connect(timeoutMs);
  }

  retargetProject(uprojectPath: string, configPort?: number): BridgeTarget {
    return this.inner.retargetProject(uprojectPath, configPort);
  }

  getTarget(): BridgeTarget {
    return this.inner.getTarget();
  }

  async call(
    method: string,
    params?: Record<string, unknown>,
    timeoutMs?: number,
  ): Promise<unknown> {
    // `runGuarded` is already a pass-through on an empty registry, but building
    // the context is not free and every bridge call lands here. Most servers run
    // with no guards at all, so skip the allocation outright.
    if (this.registry.size === 0) {
      return this.inner.call(method, params, timeoutMs);
    }

    const ctx = makeCallContext(
      method,
      params ?? {},
      timeoutMs,
      this.inner,
      this.resolveExistingFile,
      this.session,
    );
    return runGuarded(ctx, this.registry, () => this.inner.call(method, params, timeoutMs));
  }
}
