/**
 * Context commands: the calls that create, choose or move the editor a call
 * targets (set_project, add_editor, use_editor, drop_editor).
 *
 * They are advertised as project actions, and dispatch runs them here, as
 * runtime commands, outside the task registry. A flow cannot call one: a flow
 * acts on the editor it was started in, and one that re-pointed that editor
 * mid-run would run its later steps somewhere else.
 */
import type { TaskResult } from "@db-lyon/flowkit";
import { deploy, deploySummary, attach, attachSummary } from "../editor/deployer.js";
import { collapsingEnvWarnings } from "../config/session-env.js";
import { startEditor } from "../editor/editor-control.js";
import { switchProject } from "../sessions/project-switch.js";
import { startProgress } from "../cli/ui/progress.js";
import { runHandler } from "../flow/run-action.js";
import type { CallPreparation } from "../dispatch/call-pipeline.js";
import type { FlowContext } from "../flow/context.js";
import type { ToolContext } from "../core/types.js";

/**
 * The environment variables flattening every registered editor into one, right
 * now. Recomputed wherever the session set is read or changed, because
 * add_editor grows it at runtime. Undefined rather than an empty array when
 * there is nothing to report, so a single-editor response is unchanged.
 */
export function envWarningsFor(ctx: ToolContext): string[] | undefined {
  if (!ctx.sessions) return undefined;
  const lines = collapsingEnvWarnings(ctx.sessions.list().map((s) => s.name));
  return lines.length > 0 ? lines : undefined;
}

/** project(set_project). */
export async function setProjectCommand(ctx: ToolContext, p: Record<string, unknown>): Promise<unknown> {
  const projectPath = p.projectPath as string;
  if (!projectPath) throw new Error("Missing 'projectPath'");

  // #817: with several editors registered, switching this session onto a
  // project another session already holds would leave two sessions
  // pointed at one editor. Name the one that already has it instead.
  const existing = ctx.sessions?.find(projectPath);
  if (existing && existing !== ctx.session) {
    throw new Error(
      `'${existing.name}' is already registered for that project. ` +
        `Use project(action='use_editor', editorTarget='${existing.name}') to switch to it.`,
    );
  }

  // switchProject moves the bridge and the path resolver together (#818).
  // Doing it here by hand is what left the socket on the previous
  // project's editor while every path resolved against the new one.
  const switched = await switchProject(ctx.project, ctx.bridge, projectPath);
  // Sessions are keyed by project root, so the key has to move with the
  // project. Without this the session stays addressable only under the
  // project it just left.
  const editor = ctx.sessions && ctx.session ? ctx.sessions.rekey(ctx.session) : undefined;
  // The flows, task overrides, plugins and guards belong to the project
  // too, so they move with it.
  if (ctx.sessions && ctx.session) {
    try {
      await ctx.sessions.reload(ctx.session);
    } catch (e) {
      throw new Error(
        `Switched to ${ctx.project.projectName} but could not build its tool surface, so nothing is dispatched to it: ` +
          `${e instanceof Error ? e.message : String(e)}. Fix that project's ue-mcp.yml and call set_project again.`,
      );
    }
  }
  const result = deploy(ctx.project);
  return {
    success: true,
    editor: editor?.name,
    projectName: ctx.project.projectName,
    contentDir: ctx.project.contentDir,
    engineAssociation: ctx.project.engineAssociation,
    previousProject: switched.previousProjectPath ?? undefined,
    editorConnected: switched.connected,
    // The editor this connection belongs to. Always the project above.
    editorTarget: {
      projectPath: switched.target.projectPath,
      port: switched.target.port,
      portSource: switched.target.portSource,
    },
    // Present when no editor answered: the switch still completed, and
    // nothing can reach the previous project's editor any more.
    editorUnreachable: switched.connectError,
    bridgeSetup: deploySummary(result),
  };
}

/** project(use_editor). */
export async function useEditorCommand(ctx: ToolContext, p: Record<string, unknown>): Promise<unknown> {
  if (!ctx.sessions) throw new Error("This server drives one editor; there is nothing to switch between.");
  const target = p.editorTarget as string;
  if (!target) throw new Error("Missing 'editorTarget'");
  const session = ctx.sessions.use(target);
  return {
    success: true,
    activeEditor: session.name,
    projectPath: session.project.projectPath,
    bridgePort: session.bridge.port,
    editorConnected: session.bridge.isConnected,
  };
}

/** project(add_editor). */
export async function addEditorCommand(ctx: ToolContext, p: Record<string, unknown>): Promise<unknown> {
  if (!ctx.sessions) throw new Error("This server was built without a session registry.");
  const projectPath = p.projectPath as string;
  if (!projectPath) throw new Error("Missing 'projectPath'");
  const before = ctx.sessions.size;
  const session = ctx.sessions.register({
    projectPath,
    name: typeof p.editorName === "string" && p.editorName ? p.editorName : undefined,
  });
  const alreadyRegistered = ctx.sessions.size === before;

  // D1: build this editor's own tool graph, plugins, task registry and
  // guards BEFORE it is addressable. Without it every per-session lookup
  // missed and fell back to the first project's load, so a call or a
  // flow targeted at this editor ran the FIRST project's steps inside it.
  // A failure here is reported rather than swallowed: an editor that
  // cannot be given its own surface must not borrow another's.
  try {
    await ctx.sessions.prepare(session);
  } catch (e) {
    throw new Error(
      `Registered '${session.name}' but could not build its tool surface, so it is not safe to dispatch to: ` +
        `${e instanceof Error ? e.message : String(e)}. ` +
        `Drop it with project(action='drop_editor', editorTarget='${session.name}') and check that project's ue-mcp.yml.`,
    );
  }

  const attachResult = attach(session.project);
  let started: unknown;
  if (p.start === true) {
    const timeout = typeof p.timeout === "number" && p.timeout > 0 ? p.timeout : 300;
    started = await startEditor(session.project, timeout, ctx.onProgress, { openDisplay: startProgress });
  }
  try { await session.bridge.connect(); } catch { /* editor may not be running yet */ }

  // Same lines the startup path prints, on the same stream, because the
  // person watching the console is the one who exported the variable.
  const envWarnings = envWarningsFor(ctx);
  for (const line of envWarnings ?? []) console.error(`[ue-mcp] ${line}`);

  return {
    success: true,
    editor: session.name,
    alreadyRegistered: alreadyRegistered || undefined,
    projectName: session.project.projectName,
    projectPath: session.project.projectPath,
    bridgePort: session.bridge.port,
    editorConnected: session.bridge.isConnected,
    bridgeSetup: attachSummary(attachResult),
    started,
    editorCount: ctx.sessions.size,
    // The env vars that flatten every editor into one are reported at
    // startup from the startup session set, and at one editor there is
    // nothing to report - so a server that started on one project and
    // grew to two here had never said anything and never would.
    //
    // That is the whole failure: `UE_MCP_TEST_ENGINE_ROOT` exported for
    // the first project decides the engine tree for THIS one too, ahead
    // of its own editor.path, so a 5.6 project launches and builds
    // against a 5.8 engine and the only symptom is a build that should
    // not have worked. The list is a function of the session set, and
    // this call is the one that changes it, so it is recomputed here and
    // said out loud on the same response that created the second editor.
    envWarnings,
    hint: `Call any action with editor="${session.name}" to run it there, or project(action="use_editor", editorTarget="${session.name}") to make it the default.`,
  };
}

/** project(drop_editor). */
export async function dropEditorCommand(ctx: ToolContext, p: Record<string, unknown>): Promise<unknown> {
  if (!ctx.sessions) throw new Error("This server drives one editor; there is nothing to drop.");
  const target = p.editorTarget as string;
  if (!target) throw new Error("Missing 'editorTarget'");
  const dropped = ctx.sessions.drop(target);
  return {
    success: true,
    dropped: dropped.name,
    projectPath: dropped.projectPath,
    editorLeftRunning: true,
    activeEditor: ctx.sessions.active.name,
    editorCount: ctx.sessions.size,
  };
}

/** Every context command, by the task name it is advertised under. */
const CONTEXT_COMMANDS: Readonly<Record<string, (ctx: ToolContext, p: Record<string, unknown>) => Promise<unknown>>> = {
  "project.set_project": setProjectCommand,
  "project.add_editor": addEditorCommand,
  "project.use_editor": useEditorCommand,
  "project.drop_editor": dropEditorCommand,
};

/** Whether `taskName` is a context command, and so never a registered task. */
export function isContextCommand(taskName: string): boolean {
  return Object.hasOwn(CONTEXT_COMMANDS, taskName);
}

/**
 * Run a context command through the per-call path a handler action takes
 * (preparation, dialog gate, verdict), without a task. A throw is reported as
 * a failed result, as a task's run() reports one.
 */
export async function runContextCommand(
  ctx: FlowContext,
  taskName: string,
  options: Record<string, unknown>,
  prep?: CallPreparation,
): Promise<TaskResult> {
  const command = CONTEXT_COMMANDS[taskName];
  if (!command) throw new Error(`'${taskName}' is not a context command.`);
  try {
    return await runHandler(ctx, taskName, command, options, prep);
  } catch (e) {
    return { success: false, error: e instanceof Error ? e : new Error(String(e)) };
  }
}
