/**
 * Building guards from what somebody declared.
 *
 * A guard is a first-class thing here, not a task that happens to be named a
 * certain way. Two sources declare them in the same shape, a plugin manifest
 * and a project's `ue-mcp.yml`, and both arrive here as `GuardDeclaration`
 * records that are turned into pipeline guards directly.
 *
 * A guard is IMPLEMENTED BY a task, named by `class_path`, but it is not one.
 * The task registry holds tasks; this holds guards; a name collision between
 * the two is now impossible rather than load-bearing.
 */
import type { TaskRegistry } from "@db-lyon/flowkit";
import type { IBridge } from "../bridge.js";
import type { ToolContext } from "../types.js";
import type { FlowContext } from "./context.js";
import {
  mutationScope,
  readScope,
  unknownScope,
  writeScope,
  type BridgeGuard,
  type CallContext,
} from "./guard.js";
import { withoutDialogActuation } from "../dialog-guard.js";
import { DialogGatedBridge } from "./guarded-bridge.js";
import { McpError, ErrorCode } from "../errors.js";
import { debug } from "../log.js";
import type { GuardDeclarations, GuardHook } from "./guard-schema.js";
import {
  GUARD_CALL_KEY,
  GUARD_CONFIG_KEY,
  type GuardHookPayload,
  type GuardPhase,
} from "../guard-task.js";

/** Where a declaration came from, so an error can say which file to open. */
export interface GuardSource {
  /** `ue-mcp.yml`, or a plugin's name. */
  readonly label: string;
}

export interface BuildGuardsDeps {
  registry: TaskRegistry;
  ctx: ToolContext;
  /**
   * The bridge a guard's own calls go out on when no session is attached.
   *
   * Raw on purpose: a guard that calls the editor must not re-enter the flow
   * pipeline that is running it. That is a statement about the registry and
   * not about dialogs, so the per-session path below still puts the dialog
   * gate back, with no pipeline attached.
   */
  rawBridge: IBridge;
}

/** The result body a hook task returns. */
interface HookResult {
  success?: boolean;
  error?: { message?: string };
  message?: string;
  data?: unknown;
}

/**
 * The context a hook task runs in.
 *
 * Bound to the editor whose call is being guarded rather than to whichever
 * bridge was built first, so a guard can neither recurse through the pipeline
 * nor act on another project's editor. `withoutDialogActuation` sits on top
 * because arming a dialog policy is served during a modal, so a guard could
 * otherwise answer the dialog already on screen.
 */
function hookContext(cc: CallContext, deps: BuildGuardsDeps): FlowContext {
  if (!cc.session) return { ...deps.ctx, bridge: deps.rawBridge };
  return {
    ...deps.ctx,
    bridge: withoutDialogActuation(cc.session, new DialogGatedBridge(cc.bridge, cc.session)),
    project: cc.session.project,
    session: cc.session,
  };
}

/**
 * What a hook is told about the call it is guarding.
 *
 * Two shapes, deliberately. The flattened keys are what a guard written
 * against `UeMcpTask` reads, and they keep their old meaning exactly: the
 * declaration goes in first so the call being guarded wins a collision, which
 * is the behaviour those guards were written against.
 *
 * The two reserved keys are what `UeMcpGuard` reads, and they exist because
 * that flattening is a hazard: a guard whose option is named `method` loses
 * it, and the phase can only be inferred from `result` happening to be there.
 * Nested, the configuration and the subject cannot touch each other, and the
 * phase is stated rather than detected.
 */
function hookOptions(
  cc: CallContext,
  declared: Record<string, unknown>,
  phase: GuardPhase,
  result?: unknown,
): Record<string, unknown> {
  const paths = cc.writeFiles();
  return {
    ...declared,
    method: cc.method,
    params: cc.params,
    paths,
    ...(result !== undefined ? { result } : {}),
    [GUARD_CALL_KEY]: {
      phase,
      method: cc.method,
      params: cc.params,
      paths,
      session: cc.session,
      ...(result !== undefined ? { result } : {}),
    } satisfies GuardHookPayload,
    [GUARD_CONFIG_KEY]: declared,
  };
}

/** Run one hook and give back what it answered. */
async function runHook(
  hook: GuardHook,
  cc: CallContext,
  deps: BuildGuardsDeps,
  phase: GuardPhase,
  result?: unknown,
): Promise<HookResult> {
  const task = await deps.registry.create(
    hook.class_path,
    hookContext(cc, deps) as never,
    hookOptions(cc, hook.options, phase, result),
  );
  return (await task.execute()) as HookResult;
}

/** The message a denial carries, from wherever the hook put it. */
function refusalReason(r: HookResult): string {
  return r.error?.message ?? r.message ?? "no reason given";
}

/**
 * Resolve every declared hook up front.
 *
 * A `class_path` that cannot be resolved is fatal at build time, not at the
 * moment a guarded call arrives. A guard that cannot be constructed would
 * leave the calls it names ungated while the configuration says otherwise,
 * which is the one failure this whole mechanism exists to prevent.
 */
async function assertResolvable(
  name: string,
  which: "before" | "after",
  hook: GuardHook,
  deps: BuildGuardsDeps,
  source: GuardSource,
): Promise<void> {
  try {
    await deps.registry.resolve(hook.class_path);
  } catch (e) {
    const why = e instanceof Error ? e.message : String(e);
    throw new Error(
      `${source.label} declares guard '${name}' with a ${which} hook whose class_path `
      + `'${hook.class_path}' could not be resolved: ${why}. A guard that cannot be built would `
      + "leave the calls it covers ungated while the configuration says they are guarded, so the "
      + "server stops here rather than starting without it.",
    );
  }
}

/**
 * Turn declarations into pipeline guards.
 *
 * Every guard is one object with the hooks it declared, so a guard wanting
 * both sides is one guard that knows it is one, and ordering is a number
 * rather than something the name could not say.
 */
export async function buildGuards(
  declarations: GuardDeclarations,
  deps: BuildGuardsDeps,
  source: GuardSource,
): Promise<BridgeGuard[]> {
  const guards: BridgeGuard[] = [];

  for (const [name, decl] of Object.entries(declarations)) {
    if (decl.before) await assertResolvable(name, "before", decl.before, deps, source);
    if (decl.after) await assertResolvable(name, "after", decl.after, deps, source);

    const guard: BridgeGuard = {
      name,
      order: decl.order,
      // Both narrower scopes are lazy: a guard that does not ask pays nothing,
      // and a pipeline of guards that all ask pays once.
      ...(decl.scope === "writes" ? { appliesTo: (cc: CallContext) => writeScope(cc) } : {}),
      ...(decl.scope === "mutations" ? { appliesTo: (cc: CallContext) => mutationScope(cc) } : {}),
      ...(decl.scope === "reads" ? { appliesTo: (cc: CallContext) => readScope(cc) } : {}),
      ...(decl.scope === "unknown" ? { appliesTo: (cc: CallContext) => unknownScope(cc) } : {}),

      ...(decl.before
        ? {
            before: async (cc: CallContext): Promise<void> => {
              let answered: HookResult;
              try {
                answered = await runHook(decl.before!, cc, deps, "before");
              } catch (e) {
                // A hook that threw denies the call. It is the same outcome as
                // returning failure, and a guard that errors is not a guard
                // that approved.
                const why = e instanceof Error ? e.message : String(e);
                throw new McpError(
                  ErrorCode.WRITE_BLOCKED,
                  `guard '${name}' errored on ${cc.method}: ${why}`,
                );
              }
              if (answered?.success === false) {
                const files = cc.writeFiles();
                const on = files.length > 0 ? ` on ${files.join(", ")}` : "";
                throw new McpError(
                  ErrorCode.WRITE_BLOCKED,
                  `blocked (${cc.method})${on}: ${refusalReason(answered)}`,
                );
              }
            },
          }
        : {}),

      ...(decl.after
        ? {
            after: async (cc: CallContext, result: unknown): Promise<unknown | void> => {
              let answered: HookResult;
              try {
                answered = await runHook(decl.after!, cc, deps, "after", result);
              } catch (e) {
                // The call already happened. Failing it now would report a
                // mutation as not having occurred, which is worse than an
                // audit hook that did not run.
                const why = e instanceof Error ? e.message : String(e);
                debug("guard", `after hook of '${name}' errored on ${cc.method}: ${why}`);
                return undefined;
              }
              if (answered?.success === false) {
                debug(
                  "guard",
                  `after hook of '${name}' reported failure on ${cc.method}: ${refusalReason(answered)}`,
                );
                return undefined;
              }
              // Returning data replaces the result; returning nothing leaves it.
              return answered?.data;
            },
          }
        : {}),
    };

    guards.push(guard);
  }

  return guards;
}

/** The dead naming convention: a task called `guard.<name>.<phase>`. */
const LEGACY_GUARD_TASK = /^guard\.[^.]+\.(before|after)[A-Za-z0-9]*$/;

/**
 * Refuse to start when a task is still named like a guard.
 *
 * Before guards were declared, a task with this name WAS a guard. Under the
 * declaration model nothing discovers it, so it becomes an ordinary task
 * nobody calls: a source-control write guard would keep appearing in the
 * config and stop gating anything, silently.
 *
 * That is the exact failure the declaration model exists to prevent, so the
 * migration must not reintroduce it. A leftover name is fatal and says what to
 * write instead.
 */
export function assertNoLegacyGuardTasks(
  taskNames: string[],
  source: GuardSource,
): void {
  const legacy = taskNames.filter((n) => LEGACY_GUARD_TASK.test(n));
  if (legacy.length === 0) return;

  const examples = legacy.slice(0, 3).map((name) => {
    const [, guardName, phase] = /^guard\.([^.]+)\.(.+)$/.exec(name)!;
    const scope = /Write$/.test(phase) ? "writes" : "all";
    const hook = phase.startsWith("after") ? "after" : "before";
    return (
      `  ${name}\n`
      + "    becomes:\n"
      + "      guards:\n"
      + `        ${guardName}:\n`
      + `          scope: ${scope}\n`
      + `          ${hook}:\n`
      + "            class_path: <the class_path that task had>"
    );
  });

  throw new Error(
    `${source.label} declares ${legacy.length} task(s) named like a guard, which no longer makes `
    + "them one. A guard is declared in a `guards:` section now, so these are ordinary tasks that "
    + "nothing calls: whatever they were gating is ungated while the config still says it is "
    + `guarded.\n\n${examples.join("\n\n")}\n\n`
    + "See the guards documentation for the full shape, including order and the after hook.",
  );
}
