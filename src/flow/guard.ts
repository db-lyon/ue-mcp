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
import * as fs from "node:fs";
import type { IBridge } from "../bridge.js";
import type { ProjectContext } from "../project.js";
import type { EditorSession } from "../session.js";
import { classifyWrite, type WriteClassification } from "./write-methods.js";
import { bridgeMethodEffect, mayChangeState } from "../action-effects.js";
import type { ActionEffect } from "../types.js";

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

/**
 * Every call that is not a declared read.
 *
 * Wider than `writeScope`, and deliberately so. That one asks which existing
 * files a call modifies, which is the source-control question: it answers
 * "no" whenever it cannot extract a content path, and a call with no asset
 * path in its parameters is still a mutation of the editor.
 *
 * The guard pipeline sits on `IBridge.call`, so what it is handed is a bridge
 * METHOD name rather than a `category.action`. `bridgeMethodEffect` is what
 * turns one into the other, and it has no undefined answer: the effects the
 * forwarding actions declared, or the table of methods a handler calls
 * directly, or `mutate` for a method this server does not recognise.
 *
 * It still FAILS CLOSED, and now it does so by declaration rather than by not
 * finding a verb in a list. Listing mutating verbs was the wrong way round:
 * that list is open-ended, and every verb missing from it was a mutation
 * nobody guarded. Inverting it to a read list was the same guess pointed the
 * other way, and it put every read whose name does not open with a read verb
 * (`metasound_get_graph`, `line_trace`, `hit_test_viewport_pixel`) in front of
 * a guard that had no business seeing it.
 */
export function mutationScope(ctx: CallContext): boolean {
  return mayChangeState(effectOf(ctx));
}

/**
 * What THIS call does, method and arguments together.
 *
 * The arguments matter for one reason and it is not a detail: all 830 wrapped
 * engine tools dispatch through `epic_call_tool`, so the method alone is
 * `unknown` and every scope below was wrong about 830 of the 1920 actions on
 * this surface. `mutations` fired on every wrapped read, `reads` matched none
 * of them, and `unknown` matched all of them, which made a guard meant to put
 * a person in front of arbitrary code stop `epic_list_attributes` as well.
 *
 * `unknown` means an argument decides. The guard has the arguments.
 */
function effectOf(ctx: CallContext): ActionEffect {
  return bridgeMethodEffect(ctx.method, ctx.params).effect;
}

/**
 * Only the calls that observe.
 *
 * The complement of `mutationScope`, for auditing what an agent looked at
 * without the noise of everything it did. It could not be offered while the
 * answer came from a verb list: "not a read" tolerates a wrong guess in the
 * safe direction, and "IS a read" does not, because a mutation that slipped
 * into this scope is a mutation the guard was told to ignore.
 */
export function readScope(ctx: CallContext): boolean {
  return effectOf(ctx) === "read";
}

/**
 * Only the calls whose effect a PARAMETER decides.
 *
 * `editor(execute_python)`, `editor(execute_command)`, the wrapped Epic tools
 * behind `epic_call_tool`, and the reflected `invoke_*` family. The escape
 * hatches: the calls that can do anything, named as a set rather than
 * enumerated by hand in every project that wants to gate them.
 *
 * This is the scope that the declaration made possible rather than merely made
 * correct. A verb list cannot produce this set at all - `unknown` is not a
 * property of the name, it is the absence of one.
 */
export function unknownScope(ctx: CallContext): boolean {
  return effectOf(ctx) === "unknown";
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

/**
 * Turn a content path into the file on disk it names, or null.
 *
 * This is what lets a guard scoped to writes see which existing files a call
 * would modify. It is deliberately null-returning rather than throwing: a path
 * that resolves to nothing is a call that creates something, not an error.
 */
export function makeResolveExistingFile(project: ProjectContext): ResolveExistingFile {
  return (contentPath: string): string | null => {
    try {
      const abs = project.resolveContentPath(contentPath);
      return fs.existsSync(abs) ? abs : null;
    } catch {
      return null;
    }
  };
}
