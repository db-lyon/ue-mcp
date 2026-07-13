/**
 * GuardedBridge - runs a `GuardRegistry` pipeline around the editor bridge.
 *
 * Before a call reaches Unreal, each applicable guard's `before` hook runs in
 * order (any may throw to deny). After a successful call, each guard's `after`
 * hook runs in reverse order and may replace the result. With an empty registry
 * this is a pure pass-through, so it is always safe to install.
 *
 * Only `call` is gated; connection lifecycle delegates straight through.
 */
import type { IBridge } from "../bridge.js";
import {
  GuardRegistry,
  makeCallContext,
  type BridgeGuard,
  type ResolveExistingFile,
} from "./guard.js";

export type { ResolveExistingFile } from "./guard.js";

export class GuardedBridge implements IBridge {
  constructor(
    private readonly inner: IBridge,
    private readonly registry: GuardRegistry,
    private readonly resolveExistingFile: ResolveExistingFile,
  ) {}

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
    if (this.registry.size === 0) {
      return this.inner.call(method, params, timeoutMs);
    }

    const ctx = makeCallContext(method, params ?? {}, timeoutMs, this.inner, this.resolveExistingFile);

    // Resolve applicability once so `before`/`after` see a consistent set.
    const applicable: BridgeGuard[] = [];
    for (const g of this.registry.list()) {
      if (!g.appliesTo || (await g.appliesTo(ctx))) applicable.push(g);
    }

    // before: in order. A throw denies the call and propagates unchanged.
    for (const g of applicable) {
      if (g.before) await g.before(ctx);
    }

    let result = await this.inner.call(method, params, timeoutMs);

    // after: in reverse order. A returned value replaces the result.
    for (let i = applicable.length - 1; i >= 0; i--) {
      const g = applicable[i];
      if (g.after) {
        const replaced = await g.after(ctx, result);
        if (replaced !== undefined) result = replaced;
      }
    }

    return result;
  }
}
