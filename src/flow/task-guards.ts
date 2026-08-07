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
 * `manifest.tasks` entry by name, so a `guard.*.*` task is picked up here. The
 * core knows nothing about what a guard does - source control, access policy,
 * audit, and rate limiting are all just guards.
 *
 * The naming convention and the discovery walk are flowkit's; this module binds
 * them to the bridge: the `write` scope comes from `write-methods.ts`, the guard
 * task is invoked with `{ method, params, paths }` (plus `result` for `after`
 * guards), and a denial surfaces as an `McpError` the tool layer already knows
 * how to render.
 */
import * as fs from "node:fs";
import { discoverTaskGuards as discoverFlowkitGuards } from "@db-lyon/flowkit/guard";
import type { Logger, TaskRegistry } from "@db-lyon/flowkit";
import type { IBridge } from "../bridge.js";
import type { ProjectContext } from "../project.js";
import type { ToolContext } from "../types.js";
import type { FlowContext } from "./context.js";
import { writeScope, type BridgeGuard, type CallContext, type ResolveExistingFile } from "./guard.js";
import { McpError, ErrorCode } from "../errors.js";
import { debug, info } from "../log.js";

/** Adapts flowkit's logger to this server's `guard`-component logging. */
const GUARD_LOGGER: Logger = {
  debug: (...args) => debug("guard", args.map(String).join(" ")),
  info: (...args) => info("guard", args.map(String).join(" ")),
  warn: (...args) => info("guard", args.map(String).join(" ")),
  error: (...args) => info("guard", args.map(String).join(" ")),
  child: () => GUARD_LOGGER,
};

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
  return discoverFlowkitGuards<CallContext, unknown>(registry, {
    scopes: { write: writeScope },

    // Bind the guard to the editor whose call it is guarding, not to whichever
    // bridge happened to be built first. cc.bridge is the RAW bridge of the
    // session serving this call, so a guard can neither recurse through the
    // pipeline nor act on another project's editor.
    contextFor: (cc): FlowContext =>
      cc.session
        ? { ...ctx, bridge: cc.bridge, project: cc.session.project, session: cc.session }
        : { ...ctx, bridge: rawBridge },

    optionsFor: (cc, result) => ({
      method: cc.method,
      params: cc.params,
      paths: cc.writeFiles(),
      ...(result !== undefined ? { result } : {}),
    }),

    onDeny: (info) => {
      const files = info.ctx.writeFiles();
      const scope = files.length ? ` on ${files.join(", ")}` : "";
      return new McpError(
        ErrorCode.WRITE_BLOCKED,
        `blocked (${info.ctx.method})${scope}: ${info.reason}`,
      );
    },

    onError: (info) =>
      new McpError(
        ErrorCode.WRITE_BLOCKED,
        `guard '${info.guard}' errored on ${info.ctx.method}: ${info.reason}`,
      ),

    // A failing after-guard is logged but does not fail the already-completed call.
    onAfterFailure: (info) =>
      debug("guard", `after-guard '${info.guard}' reported failure on ${info.ctx.method}: ${info.reason}`),

    logger: GUARD_LOGGER,
  });
}
