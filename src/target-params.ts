/**
 * Per-call editor targeting on a tool: injecting and removing the `editor`
 * and `toEditor` parameters, and re-pointing a context at one session.
 */
import { z } from "zod";
import { EDITOR_TARGET_PARAM, MIGRATE_TARGET_PARAM } from "./routing-params.js";
import type { EditorSession } from "./session.js";
import type { ToolContext, ToolDef } from "./types.js";

/**
 * Add the target parameter to a tool. Refuses when the tool already declares
 * `editor` of its own: silently shadowing a plugin's parameter would send its
 * value to the router instead of the handler, so the collision is reported
 * and that tool stays untargeted rather than quietly changing meaning.
 */
export function injectEditorTarget(
  tool: ToolDef,
  sessionNames: string[],
): { injected: boolean; reason?: string } {
  if (tool.injectedEditorParam) {
    tool.schema = { ...tool.schema, [EDITOR_TARGET_PARAM]: editorTargetSchema(sessionNames) };
    return { injected: true };
  }
  if (EDITOR_TARGET_PARAM in tool.schema) {
    return {
      injected: false,
      reason: `'${tool.name}' declares its own '${EDITOR_TARGET_PARAM}' parameter, so per-call targeting is unavailable for it. Rename that parameter to make the category targetable.`,
    };
  }
  tool.schema = { ...tool.schema, [EDITOR_TARGET_PARAM]: editorTargetSchema(sessionNames) };
  tool.injectedEditorParam = true;
  return { injected: true };
}

/** Undo injectEditorTarget, restoring the single-editor schema exactly. */
export function removeEditorTarget(tool: ToolDef): boolean {
  if (!tool.injectedEditorParam) return false;
  const { [EDITOR_TARGET_PARAM]: _dropped, ...rest } = tool.schema;
  tool.schema = rest;
  tool.injectedEditorParam = false;
  return true;
}

/** Does any of this tool's actions move content into a second editor? */
function hasDestinationEditorAction(tool: ToolDef): boolean {
  return Object.values(tool.actions).some((spec) => spec.destinationEditor === true);
}

/**
 * Add the destination parameter, under the same rule as the target parameter:
 * only while more than one editor is registered, and never over a parameter the
 * tool already declares.
 */
export function injectMigrateTarget(
  tool: ToolDef,
  sessionNames: string[],
): { injected: boolean; reason?: string } {
  if (!hasDestinationEditorAction(tool)) return { injected: false };
  if (!tool.injectedMigrateParam && MIGRATE_TARGET_PARAM in tool.schema) {
    return {
      injected: false,
      reason: `'${tool.name}' declares its own '${MIGRATE_TARGET_PARAM}' parameter, so cross-editor migration is unavailable for it.`,
    };
  }
  tool.schema = { ...tool.schema, [MIGRATE_TARGET_PARAM]: migrateTargetSchema(sessionNames) };
  tool.injectedMigrateParam = true;
  return { injected: true };
}

/** Undo injectMigrateTarget, restoring the single-editor schema exactly. */
export function removeMigrateTarget(tool: ToolDef): boolean {
  if (!tool.injectedMigrateParam) return false;
  const { [MIGRATE_TARGET_PARAM]: _dropped, ...rest } = tool.schema;
  tool.schema = rest;
  tool.injectedMigrateParam = false;
  return true;
}

function migrateTargetSchema(sessionNames: string[]): z.ZodType {
  return z
    .string()
    .optional()
    .describe(
      `migrate: the editor to migrate INTO (${sessionNames.join(", ")}), by session name, ` +
        `project name, or .uproject path. Its Content directory becomes destinationContentDir ` +
        `and its asset registry is rescanned afterwards, so the assets are visible there ` +
        `without a manual rescan. Pass this or destinationContentDir, not both.`,
    );
}

export function editorTargetSchema(sessionNames: string[]): z.ZodType {
  return z
    .string()
    .optional()
    .describe(
      `Editor session to run this call in: a session name (${sessionNames.join(", ")}), ` +
        `a project name, or a .uproject path. Defaults to the active session ` +
        `(project(action="list_editors") reports it).`,
    );
}

/**
 * Re-point a context at one editor. Bridge, project and session move together
 * so a handler can never resolve a path in one project while calling into
 * another project's editor.
 */
export function sessionContext(ctx: ToolContext, session: EditorSession): ToolContext {
  const { getFlows, getPlugins, getToolGraph } = ctx;
  return {
    ...ctx,
    bridge: session.guarded,
    project: session.project,
    session,
    // Rebound, not copied: these read per-project config, so leaving them
    // pointed at the context's previous session would report one editor's
    // flows and plugins under another editor's name.
    getFlows: getFlows ? () => getFlows(session) : undefined,
    getPlugins: getPlugins ? () => getPlugins(session) : undefined,
    getToolGraph: getToolGraph ? (forSession) => getToolGraph(forSession ?? session) : undefined,
  };
}

/**
 * The tool graph a call is answered against: the addressed editor's own, or
 * the process-wide live graph when the context carries no accessor (CLI,
 * scripts, direct unit calls). Every surface-introspection reader goes here.
 */
export async function toolGraphOf(ctx: ToolContext): Promise<ToolDef[]> {
  if (ctx.getToolGraph) return ctx.getToolGraph();
  const { getLiveToolGraph } = await import("./tools.js");
  return getLiveToolGraph();
}

/** Drop the routing parameter from a param bag. */
export function stripEditorTarget(params: Record<string, unknown>): Record<string, unknown> {
  if (!(EDITOR_TARGET_PARAM in params)) return params;
  const { [EDITOR_TARGET_PARAM]: _dropped, ...rest } = params;
  return rest;
}
