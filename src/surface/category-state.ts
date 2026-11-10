/**
 * Which of a project's categories are turned off, and in what scope.
 *
 * One config key decides it: `disable:` in the project's ue-mcp.yml. Each
 * entry is a category name, optionally with a scope:
 *
 *     disable:
 *       - gas              # the whole category
 *       - animation:epic   # only the actions that wrap Unreal's toolsets
 *       - level:native     # only ue-mcp's own actions
 *
 * `all` (a bare name) keeps meaning what `disable:` always meant: the category
 * is not advertised, and its tasks stay in the registry so flows still name
 * them. `native` and `epic` take the matching actions out of the category and
 * leave the rest advertised.
 *
 * `nativeTools.exclude: [gas]` said "do not enrich gas with Epic tools", which
 * is `gas:epic`. It is still read, as exactly that, with a deprecation warning,
 * and any write through set_category_enabled moves it into `disable:`.
 * `nativeTools.enabled: false` still means every category's Epic actions.
 */
import type { ActionSpec, ToolDef } from "../core/types.js";
import type { EditorSession } from "../sessions/session.js";
import { actionEnum } from "./category-tool.js";

export type CategoryScope = "all" | "native" | "epic";
export const CATEGORY_SCOPES: readonly CategoryScope[] = ["all", "native", "epic"];

/**
 * Categories a scope may not turn off: the ones that host the actions which
 * turn things back on. `project` holds set_category_enabled itself, and `epic`
 * holds status, the check every Epic toolset call depends on. Their Epic
 * actions may still go: nothing that re-enables lives there.
 */
const PROTECTED: Readonly<Record<string, readonly CategoryScope[]>> = {
  project: ["all", "native"],
  epic: ["all", "native"],
};

export interface CategoryState {
  /** Whole categories off. */
  all: Set<string>;
  /** Categories with ue-mcp's own actions off. */
  native: Set<string>;
  /** Categories with their Epic actions off. */
  epic: Set<string>;
  /** Every category's Epic actions off (`nativeTools.enabled: false`). */
  epicEverywhere: boolean;
  /** Entries `nativeTools.exclude` contributed, so the deprecation can name them. */
  deprecatedExclude: string[];
  /** The partial-scope entries `disable:` itself lists, as `category:scope`. */
  listed: Set<string>;
}

export interface CategoryConfigInput {
  disable?: string[];
  nativeTools?: { enabled?: boolean; exclude?: string[] };
}

/** `gas`, `gas:epic`, `gas:native` into a category and scope; null for anything else. */
export function parseDisableEntry(entry: string): { category: string; scope: CategoryScope } | null {
  const m = /^\s*([A-Za-z0-9_]+)\s*(?::\s*(all|native|epic)\s*)?$/.exec(String(entry));
  if (!m) return null;
  return { category: m[1].toLowerCase(), scope: (m[2] as CategoryScope | undefined) ?? "all" };
}

/** The state a project's config describes. */
export function readCategoryState(cfg: CategoryConfigInput): CategoryState {
  const state: CategoryState = {
    all: new Set(),
    native: new Set(),
    epic: new Set(),
    epicEverywhere: cfg.nativeTools?.enabled === false,
    deprecatedExclude: [],
    listed: new Set(),
  };
  for (const raw of cfg.disable ?? []) {
    const e = parseDisableEntry(raw);
    if (!e) continue;
    state[e.scope].add(e.category);
    if (e.scope !== "all") state.listed.add(`${e.category}:${e.scope}`);
  }
  for (const raw of cfg.nativeTools?.exclude ?? []) {
    const category = String(raw).trim().toLowerCase();
    if (!category) continue;
    state.epic.add(category);
    state.deprecatedExclude.push(category);
  }
  return state;
}

/** Whether one scope of a category is off. `all` asks whether the whole category is. */
export function isScopeOff(state: CategoryState, category: string, scope: CategoryScope): boolean {
  if (state.all.has(category)) return true;
  if (scope === "native") return state.native.has(category);
  if (scope === "epic") return state.epicEverywhere || state.epic.has(category);
  return false;
}

/** The refusal for turning off what may not be, or null. */
export function protectedScopeRefusal(category: string, scope: CategoryScope): string | null {
  const blocked = PROTECTED[category];
  if (!blocked?.includes(scope)) return null;
  return category === "project"
    ? `'project' cannot be turned off${scope === "native" ? " (scope native)" : ""}: it holds set_category_enabled, the action that turns categories back on. `
      + "Its Epic actions can go with scope='epic'."
    : `'epic' cannot be turned off${scope === "native" ? " (scope native)" : ""}: epic(status) is how every Epic toolset call is checked first. `
      + "Its wrapped toolset actions can go with scope='epic'.";
}

/** Whether an action wraps one of Unreal's toolsets, which is what scope `epic` addresses. */
export function isEpicAction(name: string, spec: ActionSpec): boolean {
  if (spec.kind !== "bridge") return false;
  if (spec.epicTool) return true;
  return name.startsWith("epic_") && spec.bridge === "epic_call_tool";
}

/**
 * The `disable:` list with one scope of one category switched. Entries for
 * other categories keep their place, and a category ends with at most one
 * entry: both partial scopes off is written as the bare name.
 */
export function editDisableEntries(entries: readonly string[], category: string, scope: CategoryScope, enabled: boolean): string[] {
  const cat = category.toLowerCase();
  const others: string[] = [];
  let firstIndex = -1;
  const off = { native: false, epic: false };
  for (const raw of entries) {
    const e = parseDisableEntry(raw);
    if (!e || e.category !== cat) {
      others.push(raw);
      continue;
    }
    if (firstIndex < 0) firstIndex = others.length;
    if (e.scope === "all" || e.scope === "native") off.native = true;
    if (e.scope === "all" || e.scope === "epic") off.epic = true;
  }
  if (scope === "all" || scope === "native") off.native = !enabled;
  if (scope === "all" || scope === "epic") off.epic = !enabled;
  const entry = off.native && off.epic ? cat : off.native ? `${cat}:native` : off.epic ? `${cat}:epic` : null;
  if (entry === null) return others;
  const at = firstIndex < 0 ? others.length : firstIndex;
  return [...others.slice(0, at), entry, ...others.slice(at)];
}

/** One category's action names and description as they were before any scope was applied. */
export interface PristineTool {
  actions: Record<string, ActionSpec>;
  description: string;
  actionSchema: ToolDef["schema"][string] | undefined;
}

export type PristineGraph = Map<string, PristineTool>;

/** Remember a graph's categories as built, so a scope turned off can be turned back on. */
export function snapshotGraph(tools: readonly ToolDef[]): PristineGraph {
  const out: PristineGraph = new Map();
  for (const t of tools) {
    out.set(t.name, { actions: { ...t.actions }, description: t.description, actionSchema: t.schema.action });
  }
  return out;
}

export interface CategoryApplyResult {
  /** Per category, how many actions each partial scope took out. */
  removed: Record<string, { native: number; epic: number }>;
  /** Categories left with no actions, which are not advertised. */
  empty: string[];
}

/**
 * Put every category back to how it was built, then take out what the state
 * turns off, in place. In place because the advertised tool objects and the
 * registrations built on them stay the same objects across a change; a whole
 * category off is left intact here and withheld by the surface instead, the
 * way `disable:` always worked.
 */
export function applyCategoryState(tools: readonly ToolDef[], pristine: PristineGraph, state: CategoryState): CategoryApplyResult {
  const result: CategoryApplyResult = { removed: {}, empty: [] };
  for (const tool of tools) {
    const snap = pristine.get(tool.name);
    if (snap) restoreTool(tool, snap);
    if (state.all.has(tool.name)) continue;
    const nativeOff = isScopeOff(state, tool.name, "native");
    const epicOff = isScopeOff(state, tool.name, "epic");
    if (!nativeOff && !epicOff) continue;
    let native = 0;
    let epic = 0;
    const doomed = Object.entries(tool.actions)
      .filter(([name, spec]) => {
        const wrapped = isEpicAction(name, spec);
        if (wrapped && epicOff) { epic++; return true; }
        if (!wrapped && nativeOff) { native++; return true; }
        return false;
      })
      .map(([name]) => name);
    if (doomed.length === 0) continue;
    removeActions(tool, doomed);
    result.removed[tool.name] = { native, epic };
    if (Object.keys(tool.actions).length === 0) result.empty.push(tool.name);
  }
  return result;
}

function restoreTool(tool: ToolDef, snap: PristineTool): void {
  for (const k of Object.keys(tool.actions)) delete tool.actions[k];
  Object.assign(tool.actions, snap.actions);
  tool.description = snap.description;
  if (snap.actionSchema) tool.schema.action = snap.actionSchema;
}

/**
 * Take actions out of a category and rebuild what names them: the action enum
 * and the catalog lines. The flat schema keeps every field, because a field
 * may be shared with an action that forwards its whole parameter bag, and
 * guessing ownership here could make a valid parameter disappear at parsing.
 */
export function removeActions(tool: ToolDef, doomed: readonly string[]): void {
  const removed = new Set(doomed);
  for (const name of doomed) delete tool.actions[name];
  const names = Object.keys(tool.actions) as [string, ...string[]];
  if (names.length > 0) tool.schema.action = actionEnum(names);
  if (removed.size === 0) return;
  tool.description = tool.description
    .split("\n")
    .filter((line) => {
      const match = /^-\s+([A-Za-z_][A-Za-z0-9_]*)\b/.exec(line);
      return !match || !removed.has(match[1]);
    })
    .join("\n");
}

/**
 * Read and change one session's category state at runtime. Installed on the
 * session registry by whatever built the surfaces; absent where none was built.
 */
export interface CategoryControl {
  /** The session's graph as built, its current state, and its live categories. */
  view(session: EditorSession): { pristine: PristineGraph; categories: CategoryState; tools: readonly ToolDef[] } | undefined;
  /** Apply a new state: the surface is rebuilt and every client re-advertised. */
  apply(session: EditorSession, state: CategoryState): Promise<void>;
}

/** The command that turns one scope of a category back on, as a call and as a terminal line. */
export function reenableHint(category: string, scope: CategoryScope): string {
  const scopeArg = scope === "all" ? "" : `, scope='${scope}'`;
  const flag = scope === "all" ? "" : ` --${scope}`;
  return `project(action='set_category_enabled', category='${category}', enabled=true${scopeArg}) or \`ue-mcp tools enable ${category}${flag}\``;
}

/**
 * Why a call to a turned-off action is refused, or null when it is not off.
 * Reads the pristine spec, since the action is no longer in the live graph.
 */
export function disabledActionRefusal(
  pristine: PristineGraph | undefined,
  state: CategoryState | undefined,
  taskName: string,
): string | null {
  if (!pristine || !state) return null;
  const dot = taskName.indexOf(".");
  if (dot < 0) return null;
  const category = taskName.slice(0, dot);
  const action = taskName.slice(dot + 1);
  const spec = pristine.get(category)?.actions[action];
  if (!spec) return null;
  if (state.all.has(category)) {
    return `'${taskName}' is turned off: this project's ue-mcp.yml lists '${category}' under 'disable'. `
      + `Turn it back on with ${reenableHint(category, "all")}.`;
  }
  const scope: CategoryScope = isEpicAction(action, spec) ? "epic" : "native";
  if (!isScopeOff(state, category, scope)) return null;
  const what = scope === "epic" ? "the actions wrapping Unreal's toolsets" : "ue-mcp's own actions";
  if (state.listed.has(`${category}:${scope}`)) {
    return `'${taskName}' is turned off: this project's ue-mcp.yml lists '${category}:${scope}' under 'disable', which takes ${what} out of '${category}'. `
      + `Turn them back on with ${reenableHint(category, scope)}.`;
  }
  if (state.deprecatedExclude.includes(category)) {
    return `'${taskName}' is turned off: this project's ue-mcp.yml lists '${category}' under nativeTools.exclude (deprecated; read as '${category}:epic'), which takes ${what} out of '${category}'. `
      + `Turn them back on with ${reenableHint(category, scope)}.`;
  }
  return `'${taskName}' is turned off: this project's ue-mcp.yml sets nativeTools.enabled: false, which takes ${what} out of every category. `
    + "Remove that setting (or set it to true) in ue-mcp.yml to turn them back on.";
}
