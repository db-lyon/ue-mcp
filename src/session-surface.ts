/**
 * Per-session tool surface (#817, plan items 4.1 and 4.2).
 *
 * Plugin loading and Epic catalog enrichment are project-scoped facts: the
 * `plugins:` list lives in each project's `ue-mcp.yml`, and the Epic toolset
 * catalog is whatever that project's editor reports. Both used to run once
 * against one shared tool graph, so a second editor inherited the first one's
 * plugins and toolsets, and a call to an action the second project's engine
 * does not have would reach its bridge and fail there instead of being
 * refused here.
 *
 * Each session now owns a graph cloned from the pristine declaration and
 * enriched from its own project. What the client is advertised is the union
 * of those graphs, with each action remembering which sessions provide it, so
 * dispatch to a session that lacks one can say which sessions have it.
 */
import { z } from "zod";
import type { ToolDef, ActionSpec } from "./types.js";
import { cloneToolDef, cloneToolGraph } from "./types.js";
import type { EditorSession } from "./session.js";
import type { PluginRecord } from "./plugin/loader.js";

/** One editor's fully-built surface. */
export interface SessionSurface {
  session: EditorSession;
  /** This session's own graph: cloned, plugin-injected, Epic-enriched. */
  tools: ToolDef[];
  /** Categories this session's `ue-mcp.yml` disabled. */
  disabled: Set<string>;
  /** Plugin load records, for `plugins(list)` and `get_status`. */
  pluginRecords: PluginRecord[];
  /** Per-category markdown this session's plugins contribute. */
  knowledgeByCategory: Record<string, string[]>;
}

/** Which sessions provide one action, and which of them disabled its category. */
export interface ActionProviders {
  /** Session names whose graph declares the action. */
  providers: string[];
  /** Session names that declare it but disabled the category in config. */
  disabledIn: string[];
}

export interface UnionSurface {
  /** One ToolDef per category name, carrying the union of every action. */
  tools: ToolDef[];
  /** `category.action` to the sessions that provide it. */
  providers: Map<string, ActionProviders>;
}

/** A fresh, unenriched graph for one session to build on. */
export function baseGraphFor(pristine: ToolDef[]): ToolDef[] {
  return cloneToolGraph(pristine);
}

/**
 * Merge every session's graph into the one surface the client sees.
 *
 * The first session that declares a category owns its description and schema,
 * which keeps a single-editor server byte-identical to what it advertised
 * before sessions existed. Actions only later sessions have are added to that
 * category, so a toolset present in one project is still addressable there.
 *
 * A category a session disabled stays in the union rather than disappearing:
 * hiding it would take a working tool away from the OTHER editors, which is
 * not what that user's `disable:` asked for. The refusal happens at dispatch,
 * for that session only, naming the config.
 */
export function unionSurface(surfaces: SessionSurface[]): UnionSurface {
  // The merged category is a copy, never the first session's own object:
  // folding another session's actions into that object would put them back
  // into the graph this whole split exists to keep separate.
  const providers = new Map<string, ActionProviders>();
  const byName = new Map<string, ToolDef>();
  const order: string[] = [];

  const noteProvider = (tool: string, action: string, surface: SessionSurface) => {
    const key = `${tool}.${action}`;
    let entry = providers.get(key);
    if (!entry) {
      entry = { providers: [], disabledIn: [] };
      providers.set(key, entry);
    }
    if (!entry.providers.includes(surface.session.name)) entry.providers.push(surface.session.name);
    if (surface.disabled.has(tool) && !entry.disabledIn.includes(surface.session.name)) {
      entry.disabledIn.push(surface.session.name);
    }
  };

  for (const surface of surfaces) {
    for (const tool of surface.tools) {
      const existing = byName.get(tool.name);
      if (!existing) {
        byName.set(tool.name, surfaces.length > 1 ? cloneToolDef(tool) : tool);
        order.push(tool.name);
        for (const action of Object.keys(tool.actions)) noteProvider(tool.name, action, surface);
        continue;
      }
      const added: Record<string, ActionSpec> = {};
      for (const [action, spec] of Object.entries(tool.actions)) {
        noteProvider(tool.name, action, surface);
        if (!(action in existing.actions)) added[action] = spec;
      }
      if (Object.keys(added).length > 0) mergeActions(existing, added, tool);
    }
  }

  return { tools: order.map((n) => byName.get(n)!), providers };
}

/**
 * Fold actions from a later session's category into the advertised one.
 *
 * The action enum has to be rebuilt or MCP will reject the added names, and
 * any schema key the donor category carries has to come along or the caller
 * has no way to pass its arguments.
 */
function mergeActions(target: ToolDef, added: Record<string, ActionSpec>, donor: ToolDef): void {
  Object.assign(target.actions, added);
  for (const [key, schema] of Object.entries(donor.schema)) {
    if (key === "action") continue;
    if (!(key in target.schema)) target.schema[key] = schema;
  }
  rebuildActionEnum(target);
}

/** Re-derive a category's `action` enum from its live action keys. */
function rebuildActionEnum(tool: ToolDef): void {
  const names = Object.keys(tool.actions) as [string, ...string[]];
  if (names.length === 0) return;
  tool.schema.action = z.enum(names).describe("Action to perform");
}

/**
 * Why a dispatch to one session failed when the action exists elsewhere.
 * Returns null when the session can serve it.
 */
export function explainMissingAction(
  union: UnionSurface,
  taskName: string,
  sessionName: string,
  sessionHasTask: boolean,
): string | null {
  const entry = union.providers.get(taskName);
  if (!entry) return null;
  if (entry.disabledIn.includes(sessionName)) {
    const category = taskName.split(".")[0];
    return (
      `'${taskName}' is disabled for editor '${sessionName}': that project's ue-mcp.yml lists '${category}' under 'disable'. ` +
      (entry.providers.length > 1
        ? `Editors that can run it: ${entry.providers.filter((p) => !entry.disabledIn.includes(p)).join(", ") || "none"}.`
        : "Remove it from 'disable' to use it there.")
    );
  }
  if (sessionHasTask) return null;
  if (entry.providers.length === 0) return null;
  return (
    `'${taskName}' is not available in editor '${sessionName}'. ` +
    `Editors that provide it: ${entry.providers.join(", ")}. ` +
    `Address one with the 'editor' parameter.`
  );
}

/** Union of every session's plugin knowledge, first contributor wins per blob. */
export function unionKnowledge(surfaces: SessionSurface[]): Record<string, string[]> {
  const out: Record<string, string[]> = {};
  const seen = new Set<string>();
  for (const surface of surfaces) {
    for (const [category, blobs] of Object.entries(surface.knowledgeByCategory)) {
      for (const blob of blobs) {
        const key = `${category}::${blob}`;
        if (seen.has(key)) continue;
        seen.add(key);
        (out[category] ??= []).push(blob);
      }
    }
  }
  return out;
}
