/**
 * Plugin-supplied guards, discovered from the task registry.
 *
 * A plugin registers a guard by declaring a task whose name matches
 * `guard.<name>.<phase>`, where phase is one of:
 *   - `before`      run before every call; throw / fail to DENY it
 *   - `beforeWrite` run before a call only when it resolves to existing on-disk
 *                   files it will modify (the source-control checkout case)
 *   - `after`       run after every successful call (audit, side effects)
 *   - `afterWrite`  run after a write-classified call
 *
 * No new plugin-activation concept is needed: the loader already registers every
 * `manifest.tasks` entry by name, so a `guard.*.*` task is picked up here. Each
 * matched task becomes one `BridgeGuard`. The core knows nothing about what a
 * guard does - source control, access policy, audit, and rate limiting are all
 * just guards.
 *
 * A guard task is invoked with `{ method, params, paths }` (paths = the existing
 * on-disk files the call will touch, empty for non-writes) plus, for `after`
 * guards, `result`. A `before` guard denies the call by returning `success:false`
 * or throwing.
 */
import * as fs from "node:fs";
import type { TaskRegistry } from "@db-lyon/flowkit";
import type { IBridge } from "../bridge.js";
import type { ProjectContext } from "../project.js";
import type { ToolContext } from "../types.js";
import type { FlowContext } from "./context.js";
import type { BridgeGuard, CallContext, ResolveExistingFile } from "./guard.js";
import { McpError, ErrorCode } from "../errors.js";
import { debug } from "../log.js";

const GUARD_TASK_RE = /^guard\.(.+)\.(before|beforeWrite|after|afterWrite)$/;

/** Resolve a UE content path to an absolute file, or null if it does not exist. */
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

/**
 * Build a `BridgeGuard` for every `guard.<name>.<phase>` task in the registry.
 * `rawBridge` (not the guarded wrapper) backs each guard task's context, so a
 * guard that itself calls the bridge cannot recurse through the pipeline.
 */
export function discoverTaskGuards(
  registry: TaskRegistry,
  ctx: ToolContext,
  rawBridge: IBridge,
): BridgeGuard[] {
  const guards: BridgeGuard[] = [];

  for (const taskName of registry.listRegistered()) {
    const m = GUARD_TASK_RE.exec(taskName);
    if (!m) continue;
    const [, name, phase] = m;
    const writeScoped = phase.endsWith("Write");
    const isBefore = phase.startsWith("before");

    const runTask = async (cc: CallContext, result?: unknown) => {
      const guardCtx: FlowContext = { ...ctx, bridge: rawBridge };
      const options: Record<string, unknown> = {
        method: cc.method,
        params: cc.params,
        paths: cc.writeFiles(),
      };
      if (result !== undefined) options.result = result;
      try {
        const task = await registry.create(taskName, guardCtx, options);
        return await task.run();
      } catch (e) {
        throw new McpError(
          ErrorCode.WRITE_BLOCKED,
          `guard '${name}' errored on ${cc.method}: ${(e as Error).message}`,
        );
      }
    };

    const guard: BridgeGuard = {
      name: `${name}.${phase}`,
      appliesTo: writeScoped ? (cc) => cc.writeFiles().length > 0 : undefined,
    };

    if (isBefore) {
      guard.before = async (cc) => {
        const r = await runTask(cc);
        if (!r.success) {
          const reason = r.error?.message ?? `denied by guard '${name}'`;
          const scope = cc.writeFiles().length ? ` on ${cc.writeFiles().join(", ")}` : "";
          throw new McpError(ErrorCode.WRITE_BLOCKED, `blocked (${cc.method})${scope}: ${reason}`);
        }
        debug("guard", `guard '${name}' allowed ${cc.method}`);
      };
    } else {
      // `after` guards observe the result for side effects (audit); a failing
      // after-guard is logged but does not fail the already-completed call.
      guard.after = async (cc, result) => {
        const r = await runTask(cc, result);
        if (!r.success) debug("guard", `after-guard '${name}' reported failure on ${cc.method}: ${r.error?.message}`);
      };
    }

    guards.push(guard);
  }

  return guards;
}
