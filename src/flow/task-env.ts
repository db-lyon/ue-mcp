/**
 * What a task runs against, named once: the project and its config, the
 * editor and its facts, the call it serves and the locks its run holds.
 *
 * The existing task context (ToolContext / FlowContext) carries all of it
 * under older names and stays the shape plugin tasks read; this is a view
 * over it, built when a task first asks.
 */
import type { IBridge } from "../bridge/bridge.js";
import type { ProjectContext } from "../config/project.js";
import type { ProjectConfig } from "../config/project-config.js";
import type { AssetLockScopeLike, ElicitFn, ProgressFn, ToolContext } from "../core/types.js";
import type { EditorSession } from "../sessions/session.js";
import type { EditorFacts } from "../sessions/editor-facts.js";
import { McpError, ErrorCode } from "../core/errors.js";

/** What a task class declares it cannot run without. */
export interface TaskRequirements {
  /** A bridge to an editor. */
  editor?: boolean;
  /** A loaded project. */
  project?: boolean;
}

/** The editor a task runs in. */
export interface EditorView {
  /** The bridge calls go out on, guard pipeline included. */
  bridge: IBridge;
  /** The editor handle, absent for a context built outside the session registry. */
  handle?: EditorSession;
  facts?: EditorFacts;
}

/** The call a task serves. */
export interface CallView {
  /** True when the caller named its editor. */
  targeted: boolean;
  /** The caller's timeout budget in milliseconds, when it gave one. */
  timeoutMs?: number;
  elicit?: ElicitFn;
  onProgress?: ProgressFn;
  client?: { name: string; version?: string };
}

export interface TaskEnv {
  project: ProjectContext;
  /** The project's config snapshot; null when no project is loaded. */
  projectConfig: ProjectConfig | null;
  /** Absent only for a context with no bridge. */
  editor?: EditorView;
  facts?: EditorFacts;
  call: CallView;
  /** The asset locks the run holds, when locking is on. */
  locks?: AssetLockScopeLike;
}

/** The env of a task context. */
export function taskEnvOf(ctx: ToolContext): TaskEnv {
  const handle = ctx.session;
  const facts = handle?.facts;
  return {
    project: ctx.project,
    projectConfig: ctx.project?.projectConfig ?? null,
    editor: ctx.bridge ? { bridge: ctx.bridge, handle, facts } : undefined,
    facts,
    call: {
      targeted: ctx.callTargeted === true,
      timeoutMs: ctx.callTimeoutMs,
      elicit: ctx.elicit,
      onProgress: ctx.onProgress,
      client: ctx.client,
    },
    locks: ctx.assetLocks,
  };
}

/** Why `env` cannot run a task with these requirements, or null when it can. */
export function unmetRequirement(taskName: string, requires: TaskRequirements | undefined, env: TaskEnv): McpError | null {
  if (requires?.editor && !env.editor) {
    return new McpError(ErrorCode.NOT_CONNECTED, `${taskName} needs an editor, and this run has none.`);
  }
  if (requires?.project && !env.project) {
    return new McpError(ErrorCode.PROJECT_NOT_LOADED, `${taskName} needs a project, and this run has none.`);
  }
  return null;
}
