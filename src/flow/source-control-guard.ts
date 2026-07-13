/**
 * Source-control guard wiring.
 *
 * Bridges the transparent GuardedBridge to a plugin-supplied guard task without
 * any new plugin-activation concept. The contract is a convention: a plugin that
 * wants to gate writes registers a task named `sourcecontrol.before_write`
 * (an entry in its manifest `tasks:` block, referenced by no action - it is an
 * internal hook). If that task is in the registry, the server installs the
 * guard; otherwise the bridge stays a pass-through.
 *
 * The guard task receives `{ method, paths }` where `paths` are absolute,
 * existing on-disk files the write will touch. It returns success to allow the
 * write, or a failed result / throw to deny it.
 */
import * as fs from "node:fs";
import type { TaskRegistry } from "@db-lyon/flowkit";
import type { IBridge } from "../bridge.js";
import type { ProjectContext } from "../project.js";
import type { ToolContext } from "../types.js";
import type { FlowContext } from "./context.js";
import type { WriteGuardFn, WriteInfo, ResolveExistingFile } from "./guarded-bridge.js";
import { McpError, ErrorCode } from "../errors.js";
import { debug } from "../log.js";

/** Conventional registry name a plugin uses to gate writes. */
export const GUARD_TASK_NAME = "sourcecontrol.before_write";

/** True when a plugin has registered the write-guard hook task. */
export function guardTaskRegistered(registry: TaskRegistry): boolean {
  return registry.listRegistered().includes(GUARD_TASK_NAME);
}

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
 * Build the guard function that runs the plugin's hook task before a write.
 * `rawBridge` (not the guarded wrapper) backs the guard task's context, so a
 * guard that itself calls the bridge cannot recurse through the gate.
 */
export function makeSourceControlGuard(
  registry: TaskRegistry,
  rawBridge: IBridge,
  ctx: ToolContext,
): WriteGuardFn {
  return async (info: WriteInfo): Promise<void> => {
    const guardCtx: FlowContext = { ...ctx, bridge: rawBridge };
    let result;
    try {
      const task = await registry.create(GUARD_TASK_NAME, guardCtx, {
        method: info.method,
        paths: info.paths,
      });
      result = await task.run();
    } catch (e) {
      throw new McpError(
        ErrorCode.WRITE_BLOCKED,
        `source-control guard errored on ${info.method}: ${(e as Error).message}`,
      );
    }
    if (!result.success) {
      const reason = result.error?.message ?? "denied by source-control guard";
      throw new McpError(
        ErrorCode.WRITE_BLOCKED,
        `write blocked (${info.method}) on ${info.paths.join(", ")}: ${reason}`,
      );
    }
    debug("guard", `checked out ${info.paths.length} file(s) for ${info.method}`);
  };
}
