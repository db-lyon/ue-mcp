// @session per-session: Wraps one session's bridge in that session's guards.
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
import type { BridgeCallOptions, BridgeTarget, IBridge } from "../bridge/bridge.js";
import type { EditorSession } from "../sessions/session.js";
import { explainEditorDownWithEvidence } from "./offline.js";
import { GuardRegistry, makeCallContext, type ResolveExistingFile } from "./guard.js";
import { callIsReadOnly, readOnlyGuard } from "./session-access.js";
import { ensureGuard, existingGuard, refuseIfBlocked } from "../editor/dialog-guard.js";
import { listsDialogs, nativeDialogReply, repliesToNativeDialog } from "../editor/native-dialog-reply.js";

export type { ResolveExistingFile } from "./guard.js";

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
 * A reply this process gives while the editor shows a native Win32 dialog
 * (editor/native-dialog-reply.ts), or undefined, and the call goes to the
 * editor as usual.
 */
export async function serveNativeDialog(
  session: EditorSession | undefined,
  method: string,
  params: Record<string, unknown> | undefined,
): Promise<Record<string, unknown> | undefined> {
  if (!session || !repliesToNativeDialog(method)) return undefined;
  return nativeDialogReply(existingGuard(session) ?? (await ensureGuard(session)), method, params);
}

/**
 * A call already sent to the editor, raced against a native dialog appearing.
 *
 * A native dialog parks the editor where the bridge cannot answer, so a call
 * in flight would otherwise wait out its whole timeout. When one appears, the
 * caller is told now, with the dialog, marked `inFlight` because the call had
 * already been sent and may still run once the dialog is answered.
 *
 * Modal-safe methods are raced too: the plugin answers them from Slate's modal
 * loop, which does not run inside a Win32 box. A list_dialogs in flight (a UI
 * polls it every few seconds) is answered with the native dialog instead.
 */
export function raceNativeDialog<T>(
  session: EditorSession | undefined,
  method: string,
  call: Promise<T>,
  params?: Record<string, unknown>,
): Promise<T | Record<string, unknown>> {
  if (!session) return call;
  const guard = existingGuard(session);
  if (!guard) return call;
  return new Promise((resolve, reject) => {
    let settled = false;
    const off = guard.onNativeDialog((dialog) => {
      if (settled) return;
      settled = true;
      off();
      // The bridge may still answer, or fail when the editor exits; neither is anyone's to handle now.
      call.catch(() => undefined);
      if (listsDialogs(method)) {
        void serveNativeDialog(session, method, params).then((listed) => resolve(listed ?? { success: true, dialogs: [], windows: [] }), reject);
        return;
      }
      const refusal = guard.refusal(method, dialog, { canElicit: false });
      resolve({
        ...refusal,
        inFlight: true,
        error:
          `'${method}' had already been sent to the editor when a native dialog appeared, so whether it ran is not known. `
          + String(refusal.error ?? ""),
      });
    });
    call.then(
      (v) => {
        if (settled) return;
        settled = true;
        off();
        resolve(v);
      },
      (e) => {
        if (settled) return;
        settled = true;
        off();
        reject(e);
      },
    );
  });
}

/** A reply that came from the editor, as opposed to one this file made while a native dialog was up. */
function fromEditor(result: unknown): boolean {
  return !(typeof result === "object" && result !== null && (result as { inFlight?: unknown }).inFlight === true
    && (result as { nativeDialog?: unknown }).nativeDialog === true);
}

/**
 * An IBridge that forwards the connection lifecycle to the bridge it wraps.
 * Subclasses decide only what `call` does.
 */
abstract class ForwardingBridge implements IBridge {
  constructor(protected readonly inner: IBridge) {}

  get isConnected(): boolean {
    return this.inner.isConnected;
  }

  get capabilities(): IBridge["capabilities"] {
    return this.inner.capabilities;
  }

  get registeredActions(): IBridge["registeredActions"] {
    return this.inner.registeredActions;
  }

  get epicToolsets(): IBridge["epicToolsets"] {
    return this.inner.epicToolsets;
  }

  refreshEpicToolsets(): Promise<readonly string[] | null> {
    return this.inner.refreshEpicToolsets?.() ?? Promise.resolve(this.inner.epicToolsets ?? null);
  }

  get epicMcpTools(): IBridge["epicMcpTools"] {
    return this.inner.epicMcpTools;
  }

  refreshEpicMcpTools(): Promise<readonly string[] | null> {
    return this.inner.refreshEpicMcpTools?.() ?? Promise.resolve(this.inner.epicMcpTools ?? null);
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

  abstract call(method: string, params?: Record<string, unknown>, timeoutMs?: number, options?: BridgeCallOptions): Promise<unknown>;
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
export class DialogGatedBridge extends ForwardingBridge {
  constructor(
    inner: IBridge,
    private readonly session: EditorSession,
  ) {
    super(inner);
  }

  async call(
    method: string,
    params?: Record<string, unknown>,
    timeoutMs?: number,
    options?: BridgeCallOptions,
  ): Promise<unknown> {
    const blocked = await refuseIfBlocked(this.session, method);
    if (blocked) return blocked;
    const native = await serveNativeDialog(this.session, method, params);
    if (native) return native;
    const reply = await raceNativeDialog(this.session, method, this.inner.call(method, params, timeoutMs, options), params);
    return fromEditor(reply) ? observeReply(this.session, method, reply) : reply;
  }
}

/**
 * The pipeline a read-only session's call runs through: the session's own
 * guards with the built-in read-only guard ahead of them. Built per call, and
 * only for a read-only session, because the mode belongs to the caller while
 * the registry is shared by every client of the editor.
 */
function withReadOnlyGuard(registry: GuardRegistry): GuardRegistry {
  return new GuardRegistry().registerAll([readOnlyGuard, ...registry.list()]) as GuardRegistry;
}

export class GuardedBridge extends ForwardingBridge {
  constructor(
    inner: IBridge,
    private readonly registry: GuardRegistry,
    private readonly resolveExistingFile: ResolveExistingFile,
    /** The session this pipeline belongs to, so guards can see which editor
     *  they are guarding rather than assuming the process has only one. */
    private readonly session?: EditorSession,
  ) {
    super(inner);
  }

  private guardCall(method: string): Promise<Record<string, unknown> | null> {
    return refuseIfBlocked(this.session, method);
  }

  /** Feed the reply back to the guard, which decides what it proves. */
  private observe<T>(method: string, result: T): T {
    return observeReply(this.session, method, result);
  }

  async call(
    method: string,
    params?: Record<string, unknown>,
    timeoutMs?: number,
    options?: BridgeCallOptions,
  ): Promise<unknown> {
    try {
      // `runGuarded` is already a pass-through on an empty registry, but building
      // the context is not free and every bridge call lands here. Most servers run
      // with no guards at all, so skip the allocation outright.
      const readOnly = callIsReadOnly();
      const send = async (): Promise<unknown> => {
        const native = await serveNativeDialog(this.session, method, params);
        if (native) return native;
        return raceNativeDialog(this.session, method, this.inner.call(method, params, timeoutMs, options), params);
      };
      if (this.registry.size === 0 && !readOnly) {
        const blocked = await this.guardCall(method);
        if (blocked) return blocked;
        const reply = await send();
        return fromEditor(reply) ? this.observe(method, reply) : reply;
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
      const reply = await runGuarded(ctx, readOnly ? withReadOnlyGuard(this.registry) : this.registry, send);
      return fromEditor(reply) ? this.observe(method, reply) : reply;
    } catch (e) {
      // Every route into the editor comes through here (an MCP tool call, a
      // flow step, the micro gateway), so this is the one place a missing
      // editor can be explained once rather than three times. Only a
      // connection failure is rewritten; everything else is rethrown as it
      // was. See src/flow/offline.ts (T16).
      throw await explainEditorDownWithEvidence(e, {
        method,
        projectPath: this.inner.getTarget().projectPath,
        port: this.inner.getTarget().port,
        portSource: this.inner.getTarget().portSource,
        toolGraph: this.session?.toolGraph,
      });
    }
  }
}
