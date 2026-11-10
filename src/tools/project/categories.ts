/**
 * Turning categories, and Unreal's toolsets, on and off (#1827).
 *
 * Two switches, and they are different things:
 *
 *   ue-mcp's surface    `disable:` in the project's ue-mcp.yml (category-state.ts).
 *                       Decides what this server advertises and dispatches.
 *   Unreal's registry   UToolsetRegistrySettings BlockedNames. Decides what the
 *                       editor's Toolset Registry, and so Epic's own MCP server
 *                       and the AI Assistant, expose and will execute.
 *
 * set_category_enabled with scope `epic` (or `all`) moves both: the category's
 * Epic actions leave the ue-mcp surface, and the toolsets the generator routes
 * to that category are blocked in the engine. set_toolset_enabled moves only
 * the engine's switch, for toolsets named directly.
 *
 * The engine's switch is reached three ways, in this order:
 *   1. the editor's bridge answers: epic_set_toolset_enabled writes the setting
 *      and updates the running registry, so the change is live;
 *   2. an editor has the project open but its bridge does not answer: refused,
 *      since a file written under a running editor either never reaches its
 *      registry or is overwritten when it saves its config;
 *   3. no editor: the setting file is written directly (DefaultGame.ini on
 *      UE 5.9, the per-user EditorPerProjectUserSettings.ini on UE 5.8) and the
 *      change applies when the editor next starts.
 */
import { z } from "zod";
import type { ActionSpec, ToolContext, ToolDef } from "../../core/types.js";
import { McpError, ErrorCode } from "../../core/errors.js";
import { projectConfigPath, readConfigDoc, writeConfigDoc } from "../../config/ue-mcp-config.js";
import { readUeMcpConfig } from "../../config/project.js";
import {
  CATEGORY_SCOPES,
  editDisableEntries,
  isEpicAction,
  isScopeOff,
  parseDisableEntry,
  protectedScopeRefusal,
  readCategoryState,
  type CategoryScope,
  type CategoryState,
} from "../../surface/category-state.js";
import { EPIC_TOOLSETS_BY_CATEGORY, routeEpicToolset } from "../epic/toolset-routes.js";
import { readBlockedNames, setBlockedNames, toolsetSettingsFile } from "../../integrations/epic/toolset-settings.js";
import { projectEngineVersion } from "../../editor/deployer.js";
import { findLiveInstanceRecord } from "../../bridge/editor-target.js";
import { editorOwnsProject, probeEditorProcessList } from "../../editor/engine-observer.js";

/** The bridge methods another part of this release adds to the `epic` category. */
export const SET_TOOLSETS_METHOD = "epic_set_toolset_enabled";
export const GET_TOOLSETS_METHOD = "epic_get_toolset_states";

/**
 * Whether an editor process has this project open, whatever its bridge says.
 * A live bridge instance record counts, and so does the process table. A
 * probe that cannot run says nothing either way, and is not taken as running:
 * refusing every editor-down write on a machine where the table cannot be
 * read would leave no way to change the setting at all.
 */
async function editorHoldsProject(ctx: ToolContext): Promise<boolean> {
  const dir = ctx.project.projectDir;
  const uproject = ctx.project.projectPath;
  if (!dir || !uproject) return false;
  if (findLiveInstanceRecord(dir) !== null) return true;
  try {
    const probe = await probeEditorProcessList();
    return !probe.failed && probe.processes.some((p) => editorOwnsProject(p, uproject));
  } catch {
    return false;
  }
}

/** Replaceable in tests, so the editor-down and editor-running paths can be told apart without a process table. */
export const categoryDeps = { editorHoldsProject };

/** One toolset's state as the engine reports it. */
interface ToolsetState {
  name: string;
  enabled: boolean;
}

/** Read epic_get_toolset_states' answer, whichever of the plausible shapes it has. */
export function parseToolsetStates(answer: unknown): ToolsetState[] {
  const root = answer as Record<string, unknown> | unknown[] | null;
  const list = Array.isArray(root)
    ? root
    : Array.isArray((root as Record<string, unknown> | null)?.toolsets)
      ? (root as Record<string, unknown>).toolsets as unknown[]
      : [];
  const out: ToolsetState[] = [];
  for (const raw of list) {
    if (typeof raw === "string") {
      out.push({ name: raw, enabled: true });
      continue;
    }
    const r = raw as Record<string, unknown> | null;
    const name = typeof r?.name === "string" ? r.name : typeof r?.toolset === "string" ? r.toolset : null;
    if (!name) continue;
    const enabled = typeof r?.enabled === "boolean" ? r.enabled : typeof r?.blocked === "boolean" ? !r.blocked : true;
    out.push({ name, enabled });
  }
  return out;
}

/** The categories this session can turn on or off: its graph as built. */
function sessionView(ctx: ToolContext): { tools: readonly ToolDef[]; categories: CategoryState; pristine: Map<string, { actions: ToolDef["actions"] }> } {
  const view = ctx.session && ctx.sessions?.categories?.view(ctx.session);
  if (!view) {
    throw new McpError(
      ErrorCode.NOT_FOUND,
      "This server has no tool surface built for the addressed editor, so there are no categories to turn on or off here.",
    );
  }
  return view;
}

function requireCategory(ctx: ToolContext, raw: unknown): string {
  const name = typeof raw === "string" ? raw.trim().toLowerCase() : "";
  if (!name) throw new McpError(ErrorCode.INVALID_PARAMS, "Missing 'category'. project(action='list_categories') names them.");
  const view = sessionView(ctx);
  if (!view.pristine.has(name)) {
    throw new McpError(ErrorCode.INVALID_PARAMS, `Unknown category '${raw}'. Categories: ${[...view.pristine.keys()].sort().join(", ")}.`);
  }
  return name;
}

function requireEnabled(raw: unknown): boolean {
  if (typeof raw !== "boolean") throw new McpError(ErrorCode.INVALID_PARAMS, "'enabled' is required: true turns it on, false turns it off.");
  return raw;
}

function readScope(raw: unknown): CategoryScope {
  if (raw === undefined || raw === null || raw === "") return "all";
  if (typeof raw === "string" && (CATEGORY_SCOPES as readonly string[]).includes(raw)) return raw as CategoryScope;
  throw new McpError(ErrorCode.INVALID_PARAMS, `'scope' is one of ${CATEGORY_SCOPES.join(", ")} (got '${String(raw)}').`);
}

function readToolsets(raw: unknown): string[] {
  const list = Array.isArray(raw) ? raw : typeof raw === "string" ? raw.split(",") : [];
  const names = list.filter((n): n is string => typeof n === "string").map((n) => n.trim()).filter(Boolean);
  if (names.length === 0) {
    throw new McpError(ErrorCode.INVALID_PARAMS, "'toolsets' names at least one toolset, e.g. ['EditorToolset.MaterialToolset']. epic(list_toolsets) lists them.");
  }
  return [...new Set(names)];
}

/** The toolsets the editor reports right now, or null without a live bridge. */
export async function liveToolsetStates(ctx: Pick<ToolContext, "bridge">): Promise<{ states: ToolsetState[] | null; error?: string }> {
  if (!ctx.bridge.isConnected) return { states: null };
  try {
    const answer = await ctx.bridge.call(GET_TOOLSETS_METHOD, {});
    // The bridge answers a refusal (an engine older than 5.8, a non-Win64
    // editor, no ToolsetRegistry) rather than throwing it. Read as a listing,
    // it would report that the editor has no toolsets at all.
    const body = answer && typeof answer === "object" ? (answer as Record<string, unknown>) : null;
    if (body?.success === false) {
      throw new Error(typeof body.error === "string" ? body.error : `${GET_TOOLSETS_METHOD} was refused`);
    }
    return { states: parseToolsetStates(answer) };
  } catch (e) {
    // An older bridge without the method still reports which toolsets exist.
    const names = ctx.bridge.epicToolsets ?? null;
    return {
      states: names ? names.map((name) => ({ name, enabled: true })) : null,
      error: e instanceof Error ? e.message : String(e),
    };
  }
}

/** Every toolset that belongs to a category: the recorded ones, and any live one the same routing places there. */
function toolsetsOfCategory(category: string, live: readonly string[] | null): string[] {
  const out = new Set<string>(EPIC_TOOLSETS_BY_CATEGORY[category] ?? []);
  for (const name of live ?? []) if (routeEpicToolset(name) === category) out.add(name);
  return [...out].sort();
}

export interface EngineToggle {
  /** `editor` changed the running registry; `settings-file` wrote the file for the next start. */
  route: "editor" | "settings-file" | "none";
  toolsets: string[];
  enabled: boolean;
  file?: string;
  fileScope?: "project" | "user";
  changed?: boolean;
  /** What the editor answered, when it was reached. */
  editor?: unknown;
  note?: string;
}

/**
 * Turn toolsets on or off in Unreal's registry, by whichever route is open.
 * Throws before changing anything when the route is closed.
 */
async function toggleEngineToolsets(
  ctx: ToolContext,
  toolsets: string[],
  enabled: boolean,
  /**
   * For a category toggle: an engine with no registry to change (older than
   * 5.8, or a plugin without the method) skips the engine half with a note
   * rather than refusing the ue-mcp half too. A running editor whose bridge
   * does not answer still refuses.
   */
  lenient = false,
): Promise<EngineToggle> {
  if (toolsets.length === 0) return { route: "none", toolsets, enabled };
  if (ctx.bridge.isConnected) {
    let answer: unknown;
    try {
      answer = await ctx.bridge.call(SET_TOOLSETS_METHOD, { toolsets, enabled });
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      const unknown = /unknown method|not registered by the running plugin/i.test(msg);
      if (unknown && lenient) {
        return {
          route: "none",
          toolsets,
          enabled,
          note: `The running bridge plugin has no ${SET_TOOLSETS_METHOD}, so only the ue-mcp surface changed; the editor's Toolset Registry did not. Update the plugin (\`ue-mcp update\`) to switch both.`,
        };
      }
      throw new McpError(
        unknown ? ErrorCode.NOT_FOUND : ErrorCode.BRIDGE_ERROR,
        unknown
          ? `The running bridge plugin has no ${SET_TOOLSETS_METHOD}, so the editor's Toolset Registry cannot be changed live. `
            + "Update the plugin (`ue-mcp update`) and restart the editor, or close the editor and run this again to write the setting file."
          : `The editor refused to change toolsets ${toolsets.join(", ")}: ${msg}`,
      );
    }
    const refused = answer && typeof answer === "object" && (answer as Record<string, unknown>).success === false;
    if (refused) {
      throw new McpError(ErrorCode.BRIDGE_ERROR, `The editor refused to change toolsets ${toolsets.join(", ")}: ${String((answer as Record<string, unknown>).error ?? "no reason given")}`);
    }
    // What the surface withholds follows the registry.
    await ctx.session?.bridge.refreshEpicToolsets?.().catch(() => null);
    return { route: "editor", toolsets, enabled, editor: answer };
  }

  if (await categoryDeps.editorHoldsProject(ctx)) {
    throw new McpError(
      ErrorCode.NOT_CONNECTED,
      `An editor has ${ctx.project.projectName ?? "this project"} open, but its bridge is not answering, so its Toolset Registry cannot be changed live. `
      + "Writing the setting file underneath it would not reach the running registry, and the editor would overwrite it when it saves its config. "
      + "Wait for the bridge (project(action='get_status')) and run this again, or close the editor first.",
    );
  }

  const dir = ctx.project.projectDir;
  if (!dir) throw new McpError(ErrorCode.PROJECT_NOT_LOADED, "No project is loaded, so there is no setting file to write.");
  const target = toolsetSettingsFile(dir, projectEngineVersion(ctx.project));
  if ("reason" in target) {
    if (lenient) return { route: "none", toolsets, enabled, note: `Unreal's Toolset Registry was not changed: ${target.reason}.` };
    throw new McpError(ErrorCode.INVALID_PARAMS, `Cannot turn Unreal toolsets ${enabled ? "on" : "off"}: ${target.reason}.`);
  }
  const write = setBlockedNames(target.file, toolsets, !enabled);
  return {
    route: "settings-file",
    toolsets,
    enabled,
    file: write.file,
    fileScope: target.scope,
    changed: write.changed,
    note: `No editor is running; UE ${target.engineVersion} reads this when it next starts.`
      + (target.scope === "user" ? " On UE 5.8 the setting is per user, so it applies to this machine only." : ""),
  };
}

/** The `disable:` list in the project's own ue-mcp.yml, with nativeTools.exclude folded in and removed. */
function writeDisable(projectDir: string, edit: (entries: string[]) => string[]): { file: string; disable: string[]; migrated: string[] } {
  const file = projectConfigPath(projectDir);
  const doc = readConfigDoc(file);
  const block = (doc["ue-mcp"] && typeof doc["ue-mcp"] === "object" && !Array.isArray(doc["ue-mcp"]))
    ? doc["ue-mcp"] as Record<string, unknown>
    : {};
  let entries = Array.isArray(block.disable) ? (block.disable as unknown[]).filter((e): e is string => typeof e === "string") : [];
  const nativeTools = block.nativeTools && typeof block.nativeTools === "object" ? block.nativeTools as Record<string, unknown> : null;
  const migrated: string[] = [];
  if (nativeTools && Array.isArray(nativeTools.exclude)) {
    for (const raw of nativeTools.exclude) {
      if (typeof raw !== "string" || !raw.trim()) continue;
      entries = editDisableEntries(entries, raw.trim(), "epic", false);
      migrated.push(raw.trim().toLowerCase());
    }
    delete nativeTools.exclude;
    if (Object.keys(nativeTools).length === 0) delete block.nativeTools;
  }
  entries = edit(entries);
  if (entries.length > 0) block.disable = entries;
  else delete block.disable;
  doc["ue-mcp"] = block;
  writeConfigDoc(file, doc);
  return { file, disable: entries, migrated };
}

function scopeSummary(state: CategoryState, category: string): { native: boolean; epic: boolean } {
  return { native: !isScopeOff(state, category, "native"), epic: !isScopeOff(state, category, "epic") };
}

/** Turning categories and Unreal's toolsets on and off. */
export const categoryActions: Record<string, ActionSpec> = {
  list_categories: {
    kind: "handler",
    effect: "read",
    description:
      "Every category with what is turned on in it: ue-mcp's own actions (native) and the actions wrapping Unreal's toolsets (epic), "
      + "each with its action count, plus the Unreal toolsets the category maps to and, with an editor attached, which of them its Toolset Registry has on. "
      + "With no editor, the toolsets the project's setting file blocks. Params: none",
    handler: async (ctx) => {
      const view = sessionView(ctx);
      const live = await liveToolsetStates(ctx);
      const liveNames = live.states?.map((s) => s.name) ?? null;
      const liveByName = new Map((live.states ?? []).map((s) => [s.name, s.enabled]));
      let blockedInFile: string[] | null = null;
      let settingsFile: string | null = null;
      if (!ctx.bridge.isConnected && ctx.project.projectDir) {
        const target = toolsetSettingsFile(ctx.project.projectDir, projectEngineVersion(ctx.project));
        if (!("reason" in target)) {
          settingsFile = target.file;
          blockedInFile = readBlockedNames(target.file);
        }
      }
      const isBlocked = (name: string): boolean => (blockedInFile ?? []).some((b) => name.toLowerCase().includes(b.toLowerCase()));

      const categories = [...view.pristine.entries()]
        .sort((a, b) => a[0].localeCompare(b[0]))
        .map(([name, snap]) => {
          const specs = Object.entries(snap.actions);
          const epicCount = specs.filter(([a, s]) => isEpicAction(a, s)).length;
          const toolsets = toolsetsOfCategory(name, liveNames);
          const state = scopeSummary(view.categories, name);
          const allOff = view.categories.all.has(name);
          return {
            name,
            enabled: !allOff && (state.native || state.epic),
            native: { enabled: state.native, actions: specs.length - epicCount },
            epic: {
              enabled: state.epic,
              actions: epicCount,
              toolsets: toolsets.map((t) => ({
                name: t,
                ...(live.states ? { present: liveByName.has(t), enabled: liveByName.get(t) ?? null } : {}),
                ...(blockedInFile ? { blockedInSettings: isBlocked(t) } : {}),
              })),
            },
            ...(protectedScopeRefusal(name, "all") ? { protected: "native actions cannot be turned off" } : {}),
          };
        });
      return {
        categoryCount: categories.length,
        categories,
        disable: [...view.categories.all, ...view.categories.listed].sort(),
        engine: {
          connected: ctx.bridge.isConnected,
          ...(live.error ? { toolsetStatesError: live.error } : {}),
          ...(settingsFile ? { settingsFile, blocked: blockedInFile } : {}),
        },
        ...(view.categories.deprecatedExclude.length > 0
          ? { deprecated: `nativeTools.exclude is read as disable: [${view.categories.deprecatedExclude.map((c) => `${c}:epic`).join(", ")}]; the next set_category_enabled moves it.` }
          : {}),
        hint: "project(action='set_category_enabled', category, enabled, scope?) turns one on or off; project(action='set_toolset_enabled', toolsets, enabled) switches Unreal toolsets by name.",
      };
    },
  },
  set_category_enabled: {
    kind: "handler",
    effect: "mutate",
    description:
      "Turn a category on or off for this project, live: the surface is rebuilt and clients get tools/list_changed. "
      + "scope 'native' is ue-mcp's own actions, 'epic' is the actions wrapping Unreal's toolsets AND those toolsets in the editor's Toolset Registry "
      + "(changed live through the bridge, or in the project's setting file when no editor runs; refused when an editor runs but its bridge does not answer), "
      + "'all' (default) is both. Written to `disable:` in ue-mcp.yml. A call to an action turned off is refused naming this action. "
      + "project and epic keep their own actions (only scope 'epic' applies to them). "
      + "Params: category, enabled (bool), scope? (all|native|epic, default all)",
    handler: async (ctx, p) => {
      const category = requireCategory(ctx, p.category);
      const enabled = requireEnabled(p.enabled);
      const scope = readScope(p.scope);
      if (!enabled) {
        const refused = protectedScopeRefusal(category, scope);
        if (refused) throw new McpError(ErrorCode.INVALID_PARAMS, refused);
      }
      const dir = ctx.project.projectDir;
      if (!dir) throw new McpError(ErrorCode.PROJECT_NOT_LOADED, "No project is loaded, so there is no ue-mcp.yml to record this in.");
      const view = sessionView(ctx);
      const before = view.categories;
      if (enabled && scope !== "native" && before.epicEverywhere) {
        throw new McpError(
          ErrorCode.INVALID_PARAMS,
          "This project's ue-mcp.yml sets nativeTools.enabled: false, which turns off every category's Epic actions. Remove that setting (or set it to true) first.",
        );
      }

      // The engine first, so a closed route refuses before anything is written.
      const touchesEpic = scope !== "native";
      const live = touchesEpic ? await liveToolsetStates(ctx) : { states: null };
      const toolsets = touchesEpic ? toolsetsOfCategory(category, live.states?.map((s) => s.name) ?? null) : [];
      const engine = await toggleEngineToolsets(ctx, toolsets, enabled, true);

      const written = writeDisable(dir, (entries) => editDisableEntries(entries, category, scope, enabled));
      ctx.project.config = readUeMcpConfig(dir);
      const after = readCategoryState(ctx.project.config);
      await ctx.sessions!.categories!.apply(ctx.session!, after);

      const wanted = scope === "all" ? ["native", "epic"] as const : [scope] as const;
      const overridden = wanted.some((s) => isScopeOff(after, category, s) === enabled);
      return {
        category,
        scope,
        enabled,
        before: scopeSummary(before, category),
        after: scopeSummary(after, category),
        config: { file: written.file, disable: written.disable, ...(written.migrated.length > 0 ? { migratedFromNativeToolsExclude: written.migrated } : {}) },
        engine,
        ...(overridden
          ? { warning: `ue-mcp.yml now says '${category}' is ${enabled ? "on" : "off"}, but a higher config layer (ue-mcp.local.yml, an env overlay or ~/.ue-mcp/config.yml) sets 'disable' and overrides it.` }
          : {}),
        undo: `project(action='set_category_enabled', category='${category}', enabled=${!enabled}${scope === "all" ? "" : `, scope='${scope}'`})`,
      };
    },
  },
  set_toolset_enabled: {
    kind: "handler",
    effect: "mutate",
    description:
      "Turn Unreal toolsets on or off in the editor's Toolset Registry by name, which is what Epic's own MCP server and the AI Assistant read too. "
      + "Live through the bridge when the editor answers; written to the project's setting file when no editor runs (UE 5.9 Config/DefaultGame.ini, "
      + "UE 5.8 the per-user EditorPerProjectUserSettings.ini), applied at its next start; refused when an editor runs but its bridge does not answer. "
      + "The ue-mcp surface is not changed: set_category_enabled with scope 'epic' does both. "
      + "Params: toolsets (names, as epic(list_toolsets) gives them), enabled (bool)",
    handler: async (ctx, p) => {
      const toolsets = readToolsets(p.toolsets);
      const enabled = requireEnabled(p.enabled);
      const engine = await toggleEngineToolsets(ctx, toolsets, enabled);
      return {
        ...engine,
        categories: [...new Set(toolsets.map(routeEpicToolset))].sort(),
        undo: `project(action='set_toolset_enabled', toolsets=${JSON.stringify(toolsets)}, enabled=${!enabled})`,
      };
    },
  },
};

/** The parameters those actions take, merged into the project category's schema. */
export const categorySchema: Record<string, z.ZodType> = {
  enabled: z.boolean().optional().describe("set_category_enabled / set_toolset_enabled: true turns it on, false turns it off"),
  scope: z.string().optional().describe("set_category_enabled: all (default), native (ue-mcp's own actions) or epic (the Unreal toolset actions and the toolsets themselves)"),
  toolsets: z.union([z.string(), z.array(z.string())]).optional().describe("set_toolset_enabled: Unreal toolset names, as epic(list_toolsets) gives them"),
};

/** Whether a `disable:` entry names a scope rather than a whole category. */
export function isScopedEntry(entry: string): boolean {
  const e = parseDisableEntry(entry);
  return e !== null && e.scope !== "all";
}
