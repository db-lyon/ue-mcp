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
import { explainEditorDownWithEvidence } from "../offline.js";
import { GuardRegistry, makeCallContext, type ResolveExistingFile } from "./guard.js";
import { DialogGuard, ensureGuard, existingGuard } from "../dialog-guard.js";

export type { ResolveExistingFile } from "./guard.js";

/**
 * The dialog gate, at the boundary every route to the editor shares.
 *
 * A tool action, a flow step, a nested flow, a guard task making its own
 * calls: they all arrive here, so refusing here is what makes the guarantee
 * hold without a second copy of the rule anywhere else. The plugin refuses too,
 * and agrees, but it cannot see a call that never reaches it and it does not
 * know this machine's dialog mode.
 *
 * Returns the refusal to send back, or null to let the call through.
 */
export async function refuseIfBlocked(
  session: EditorSession | undefined,
  method: string,
): Promise<Record<string, unknown> | null> {
  if (!session) return null;
  if (DialogGuard.bridgeAllowed(method)) return null;
  // Built from the session when it has none, rather than refused for not
  // having one. Failing closed is right, but only because no session can reach
  // this without a guard: they were created in one startup pass, so a session
  // registered any other way was refused every call it ever made.
  const guard = await ensureGuard(session);
  const decision = await guard.check(method, "bridge");
  return decision.allow ? null : decision.refusal;
}

/**
 * Arm or clear the dialog latch from what the editor just said.
 *
 * Every bridge call funnels through here, so the latch is set by the first
 * refusal the plugin's gate emits rather than by a poll. Anything that comes
 * back normally proves the game thread is running again, which is what clears
 * it: an agent that answers the dialog does not then have to tell the server
 * it did.
 */
export function observeReply<T>(session: EditorSession | undefined, method: string, result: T): T {
  if (session) existingGuard(session)?.observe(method, result);
  return result;
}

/**
 * The dialog gate alone, with no flow pipeline around it.
 *
 * A guard task runs on the RAW bridge on purpose, so that a guard which itself
 * calls the editor cannot re-enter the pipeline that is running it. That is a
 * statement about the flow registry, not about dialogs: raw also meant those
 * calls were the one route that reached a parked game thread unrefused, where
 * they hung until they timed out. This restores the gate and nothing else.
 */
export class DialogGatedBridge implements IBridge {
  constructor(
    private readonly inner: IBridge,
    private readonly session: EditorSession,
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
    const blocked = await refuseIfBlocked(this.session, method);
    if (blocked) return blocked;
    return observeReply(this.session, method, await this.inner.call(method, params, timeoutMs));
  }
}

export class GuardedBridge implements IBridge {
  constructor(
    private readonly inner: IBridge,
    private readonly registry: GuardRegistry,
    private readonly resolveExistingFile: ResolveExistingFile,
    /** The session this pipeline belongs to, so guards can see which editor
     *  they are guarding rather than assuming the process has only one. */
    private readonly session?: EditorSession,
  ) {}

  private guardCall(method: string): Promise<Record<string, unknown> | null> {
    return refuseIfBlocked(this.session, method);
  }

  /** Feed the reply back to the guard, which decides what it proves. */
  private observe<T>(method: string, result: T): T {
    return observeReply(this.session, method, result);
  }

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
    try {
      // `runGuarded` is already a pass-through on an empty registry, but building
      // the context is not free and every bridge call lands here. Most servers run
      // with no guards at all, so skip the allocation outright.
      if (this.registry.size === 0) {
        const blocked = await this.guardCall(method);
        if (blocked) return blocked;
        return this.observe(method, await this.inner.call(method, params, timeoutMs));
      }

      const ctx = makeCallContext(
        method,
        params ?? {},
        timeoutMs,
        this.inner,
        this.resolveExistingFile,
        this.session,
      );
      const blocked = await this.guardCall(method);
      if (blocked) return blocked;
      return this.observe(
        method,
        await runGuarded(ctx, this.registry, () => this.inner.call(method, params, timeoutMs)),
      );
    } catch (e) {
      // Every route into the editor comes through here (an MCP tool call, a
      // flow step, the micro gateway), so this is the one place a missing
      // editor can be explained once rather than three times. Only a
      // connection failure is rewritten; everything else is rethrown as it
      // was. See src/offline.ts (T16).
      throw await explainEditorDownWithEvidence(e, {
        method,
        projectPath: this.inner.getTarget().projectPath,
        port: this.inner.getTarget().port,
        portSource: this.inner.getTarget().portSource,
      });
    }
  }
}
