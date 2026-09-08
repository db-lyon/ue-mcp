/**
 * Public guard-authoring surface for ue-mcp.
 *
 * A guard is not a task. `flow/guards.ts` has said so since guards stopped
 * being tasks whose NAME encoded their phase and scope, and the declaration
 * side reflects it: a guard is declared with `scope`, `order`, `before` and
 * `after`, and is merely IMPLEMENTED BY a task named by `class_path`.
 *
 * The authoring side did not reflect it. Writing a guard meant extending
 * `UeMcpTask` and implementing `execute()`, which left three things wrong:
 *
 *   Nothing was typed. `this.options` is `Record<string, unknown>`, so every
 *   guard cast or guessed its way to `method` and `paths`.
 *
 *   Configuration and subject shared one namespace. The declared `options` and
 *   the call being guarded were flattened into one bag, and the call won any
 *   collision, so a guard with an option named `method` silently lost it.
 *
 *   The phase was implicit. One `execute()` served both hooks, and a class
 *   used for both could only tell them apart by sniffing for a `result` key.
 *   `TaskResult.data` meanwhile means "replace the call's result" after the
 *   call and means nothing before it.
 *
 * `UeMcpGuard` fixes all three without moving the runtime: it still registers
 * as a task, so `class_path` resolution, plugin loading and the pipeline are
 * unchanged, and a guard written against `UeMcpTask` keeps working exactly as
 * it did.
 *
 * ```ts
 * import { UeMcpGuard, type GuardedCall } from "ue-mcp/guard";
 *
 * export default class Freeze extends UeMcpGuard<{ enabled?: boolean }> {
 *   get taskName() { return "freeze"; }
 *   async before(call: GuardedCall) {
 *     if (this.config.enabled === false) return this.allow();
 *     return this.deny(`Blocked '${call.method}'.`);
 *   }
 * }
 * ```
 */
import type { TaskResult } from "@db-lyon/flowkit";
import type { EditorSession } from "./session.js";
import { UeMcpTask } from "./task.js";

/**
 * Where the pipeline puts the call being guarded, and the guard's own declared
 * options, so the two cannot collide with each other or with anything a guard
 * chooses to name.
 *
 * Reserved keys rather than symbols: the options object crosses the task
 * registry, and a key that survives being spread is the one thing that has to
 * hold. Both are also still flattened into the bag alongside these, because a
 * guard written against `UeMcpTask` reads them there and must keep working.
 */
export const GUARD_CALL_KEY = "__ueMcpGuardCall";
export const GUARD_CONFIG_KEY = "__ueMcpGuardConfig";

/** Which hook is running. */
export type GuardPhase = "before" | "after";

/** The call a guard is being asked about. */
export interface GuardedCall {
  /** Which hook this is. */
  readonly phase: GuardPhase;
  /** The bridge method. For a wrapped engine tool this is `epic_call_tool`. */
  readonly method: string;
  /** The arguments the call carries. */
  readonly params: Record<string, unknown>;
  /**
   * Existing files on disk the call would modify. Empty for a read, and empty
   * for a mutation that names no content path, which is why a guard that wants
   * "everything that changes" is declared `scope: mutations` rather than
   * inferring it from this being non-empty.
   */
  readonly paths: readonly string[];
  /** The editor this call is bound to, when it has one. */
  readonly session?: EditorSession;
}

/** What a `before` hook answers. Build one with `allow()` or `deny()`. */
export type GuardVerdict = TaskResult;

/** The payload `flow/guards.ts` hands a guard task. */
export interface GuardHookPayload {
  phase: GuardPhase;
  method: string;
  params: Record<string, unknown>;
  paths: string[];
  result?: unknown;
  session?: EditorSession;
}

/**
 * Base class for a ue-mcp guard.
 *
 * Implement `before`, `after`, or both. `execute()` is wired once here and
 * routes to whichever the pipeline asked for, so the phase is a signature
 * rather than something to detect.
 */
export abstract class UeMcpGuard<TConfig = Record<string, unknown>> extends UeMcpTask {
  /**
   * The options this guard was DECLARED with, and nothing else.
   *
   * `this.options` still holds the flattened bag for compatibility. This is
   * the one to read: the call being guarded cannot mask a key of yours in it.
   */
  protected get config(): TConfig {
    const bag = this.options as Record<string, unknown>;
    return (bag[GUARD_CONFIG_KEY] ?? {}) as TConfig;
  }

  /** Let the call proceed. */
  protected allow(): GuardVerdict {
    return { success: true };
  }

  /**
   * Refuse the call. The reason reaches the caller as a WRITE_BLOCKED error,
   * so write it for whoever is about to read it in an agent transcript.
   */
  protected deny(reason: string): GuardVerdict {
    return { success: false, error: new Error(reason) };
  }

  /**
   * Runs before the call. Returning `deny()` stops it; the call never happens.
   * Throwing denies it too, because a guard that errored is not one that
   * approved.
   */
  protected before?(call: GuardedCall): Promise<GuardVerdict> | GuardVerdict;

  /**
   * Runs after a call that succeeded, and cannot deny it: it already happened.
   * Return an object to REPLACE the result, or nothing to leave it alone.
   *
   * An object rather than anything, because that is what the pipeline can
   * actually carry: a replacement travels back as a task result's `data`, and
   * a scalar there has nowhere to go.
   */
  protected after?(
    call: GuardedCall,
    result: unknown,
  ): Promise<Record<string, unknown> | void> | Record<string, unknown> | void;

  async execute(): Promise<TaskResult> {
    const bag = (this.options ?? {}) as Record<string, unknown>;
    const payload = bag[GUARD_CALL_KEY] as GuardHookPayload | undefined;
    if (!payload) {
      // Reached by invoking a guard as an ordinary task, which nothing in the
      // pipeline does. Said plainly rather than reported as a guard that
      // approved, because a guard that silently allows is the failure mode
      // this whole layer exists to prevent.
      return {
        success: false,
        error: new Error(
          `${this.taskName} is a guard and was run as a task. Guards are invoked by the `
          + "bridge pipeline from a `guards:` declaration, not called directly.",
        ),
      };
    }

    const call: GuardedCall = {
      phase: payload.phase,
      method: payload.method,
      params: payload.params,
      paths: payload.paths,
      session: payload.session,
    };

    if (call.phase === "before") {
      if (!this.before) {
        return {
          success: false,
          error: new Error(
            `${this.taskName} is declared as a 'before' hook but implements no before().`,
          ),
        };
      }
      return await this.before(call);
    }

    if (!this.after) {
      return {
        success: false,
        error: new Error(
          `${this.taskName} is declared as an 'after' hook but implements no after().`,
        ),
      };
    }
    const replacement = await this.after(call, payload.result);
    // `data` is how the pipeline replaces a result. Returning nothing leaves
    // the call's own result standing, which is the common case for an audit.
    return replacement === undefined ? { success: true } : { success: true, data: replacement };
  }
}
