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
import { isDialogRefusal, noteDialogBlocking, clearDialogBlocking } from "../dialog-gate.js";

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
  private noteDialogState<T>(result: T): T {
    if (!this.session) return result;
    if (isDialogRefusal(result)) noteDialogBlocking(this.session, result);
    else clearDialogBlocking(this.session);
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
        return this.noteDialogState(await this.inner.call(method, params, timeoutMs));
      }

      const ctx = makeCallContext(
        method,
        params ?? {},
        timeoutMs,
        this.inner,
        this.resolveExistingFile,
        this.session,
      );
      return this.noteDialogState(
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
