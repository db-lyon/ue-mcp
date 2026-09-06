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
import { DialogGuard, existingGuard } from "../dialog-guard.js";

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

  /**
   * Arm or clear the dialog latch from what the editor just said.
   *
   * Every bridge call funnels through here, so the latch is set by the first
   * refusal the plugin's gate emits rather than by a poll. Anything that comes
   * back normally proves the game thread is running again, which is what
   * clears it: an agent that answers the dialog does not then have to tell the
   * server it did.
   */
  /**
   * The dialog guard, at the boundary every route to the editor shares.
   *
   * A tool action, a flow step, a nested flow, a handler making its own calls:
   * they all arrive here, so refusing here is what makes the guarantee hold
   * without a second copy of the rule anywhere else. The plugin refuses too,
   * and agrees, but it cannot see a call that never reaches it and it does not
   * know this machine's dialog mode.
   */
  private async guardCall(method: string): Promise<Record<string, unknown> | null> {
    if (!this.session) return null;
    if (DialogGuard.bridgeAllowed(method)) return null;
    const guard = existingGuard(this.session);
    if (!guard) {
      // No guard means no way to know whether a modal is up. The HTTP route
      // already refuses in this state; failing open here while that failed
      // closed meant the same condition had two opposite answers.
      return {
        success: false,
        dialogBlocking: true,
        refusedMethod: method,
        error:
          `'${method}' was refused because this editor has no dialog guard, so whether a modal `
          + "is blocking it cannot be established. Re-register the editor with project(add_editor).",
      };
    }
    const decision = await guard.check(method, "bridge");
    return decision.allow ? null : decision.refusal;
  }

  /** Feed the reply back to the guard, which decides what it proves. */
  private observe<T>(method: string, result: T): T {
    if (this.session) existingGuard(this.session)?.observe(method, result);
    return result;
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
