import * as fs from "node:fs";
import * as path from "node:path";
import { resolveConfigPath, findIniFiles, parseIni, buildTagTree } from "../../codeintel/ue-ini.js";
import { specBp } from "../specs/project.generated.js";
import { specBp as blueprintSpecBp } from "../specs/blueprint.generated.js";
import type { ActionSpec, ToolContext } from "../../core/types.js";
import {
  checkPlatform,
  configCategory,
  configLayers,
  displayValue,
  explainKey,
  layerReport,
  loadLayers,
  resolveAll,
  type LoadedLayer,
} from "../../codeintel/config-hierarchy.js";

function requireText(value: unknown, name: string): string {
  if (typeof value !== "string" || value.trim() === "") throw new Error(`Missing '${name}'`);
  return value.trim();
}

/** Build-target names map to the config platform they read: Win64 is Windows. */
const PLATFORM_ALIASES: Record<string, string> = { win64: "Windows", windows: "Windows", mac: "Mac", macos: "Mac", linux: "Linux" };

/** One category's layers for this project and its engine, read from disk. */
function hierarchyFor(ctx: ToolContext, p: Record<string, unknown>): {
  loaded: LoadedLayer[];
  head: { category: string; platform: string; engineRoot: string | null };
  note: { note?: string };
} {
  ctx.project.ensureLoaded();
  const projectDir = ctx.project.projectDir;
  if (!projectDir) throw new Error("No project is set: call project(set_project) first");
  const category = configCategory(p.configName as string);
  const asked = typeof p.platform === "string" ? p.platform.trim() : undefined;
  const platform = checkPlatform(asked ? PLATFORM_ALIASES[asked.toLowerCase()] ?? asked : undefined);
  const engineRoot = ctx.project.resolveEngineRoot();
  const loaded = loadLayers(configLayers({ engineRoot, projectDir, category, platform }));
  return {
    loaded,
    head: { category, platform, engineRoot },
    note: engineRoot
      ? {}
      : { note: "The engine install was not found, so the engine layers (Base.ini, Base<Category>.ini and the engine platform file) were not read. Point editor.path in ue-mcp.yml or UE_EDITOR_PATH at the editor, or fix the project's EngineAssociation, to include them." },
  };
}

/** The project's INI config: read, search, list tags, write. */
export const configActions: Record<string, ActionSpec> = {
  read_config: {
    kind: "handler",
    effect: "read",
    description: "Read INI config. Params: configName (e.g. 'Engine', 'Game')",
    handler: async (ctx, p) => {
      ctx.project.ensureLoaded();
      const filePath = resolveConfigPath(ctx.project.configDir!, p.configName as string);
      if (!fs.existsSync(filePath)) throw new Error(`Config file not found: ${filePath}`);
      const sections = parseIni(fs.readFileSync(filePath, "utf-8"));
      return { path: filePath, configName: p.configName, sectionCount: Object.keys(sections).length, sections };
    },
  },
  resolve_setting: {
    kind: "handler",
    effect: "read",
    description: "Final value of one ini key as the editor reads it, through Unreal's whole config hierarchy (engine Base.ini, Base<Category>.ini, the engine platform file, the project's Default<Category>.ini and platform file, then Saved/Config/<Platform>Editor), with +, -, ., ! array operations applied. Returns value (a string, a list for an array key, null when unset), setBy (layer, file, line, text) and the layers read. No editor needed. Params: configName (category, e.g. 'Engine', 'Game', 'Input'), section, key, platform? (default this machine's: Windows, Mac or Linux)",
    handler: async (ctx, p) => {
      const h = hierarchyFor(ctx, p);
      const { steps, values } = explainKey(h.loaded, requireText(p.section, "section"), requireText(p.key, "key"));
      const last = steps.at(-1);
      const lastLine = last?.lines.at(-1);
      return {
        ...h.head,
        section: p.section,
        key: p.key,
        set: values.length > 0,
        value: displayValue(values),
        setBy: last && lastLine ? { layer: last.layer, scope: last.scope, file: last.file, line: lastLine.line, text: lastLine.text } : null,
        layersRead: layerReport(h.loaded),
        ...h.note,
      };
    },
  },
  explain_setting: {
    kind: "handler",
    effect: "read",
    description: "Every config layer that touches one ini key, lowest first, with the lines it applies (+, -, ., ! array operations included) and the value after each, then the final value. The layers are resolve_setting's. No editor needed. Params: configName (category, e.g. 'Engine'), section, key, platform?",
    handler: async (ctx, p) => {
      const h = hierarchyFor(ctx, p);
      const { steps, values } = explainKey(h.loaded, requireText(p.section, "section"), requireText(p.key, "key"));
      return {
        ...h.head,
        section: p.section,
        key: p.key,
        value: displayValue(values),
        layers: steps,
        layersRead: layerReport(h.loaded),
        ...h.note,
      };
    },
  },
  diff_config_from_default: {
    kind: "handler",
    effect: "read",
    description: "The ini keys the project changes from the engine's defaults for one category: every key whose final value through the project (and, by default, the per-user Saved) layers differs from the value the engine layers alone give. Each change has section, key, engineValue, value and setBy. No editor needed. Params: configName (category, e.g. 'Engine'), platform?, includeUser? (default true: include Saved/Config/<Platform>Editor)",
    handler: async (ctx, p) => {
      const h = hierarchyFor(ctx, p);
      const engineOnly = resolveAll(h.loaded.filter((l) => l.scope === "engine"));
      const withProject = resolveAll(h.loaded.filter((l) => l.scope !== "user" || p.includeUser !== false));
      const changes: Array<Record<string, unknown>> = [];
      for (const [id, entry] of withProject) {
        if (entry.setBy?.scope === "engine") continue;
        const base = engineOnly.get(id);
        const before = base?.values ?? [];
        if (before.length === entry.values.length && before.every((v, i) => v === entry.values[i])) continue;
        changes.push({
          section: base?.section ?? entry.section,
          key: entry.key,
          engineValue: displayValue(before),
          value: displayValue(entry.values),
          setBy: entry.setBy ? { layer: entry.setBy.name, scope: entry.setBy.scope, file: entry.setBy.file } : null,
        });
      }
      return { ...h.head, count: changes.length, changes, layersRead: layerReport(h.loaded), ...h.note };
    },
  },
  search_config: {
    kind: "handler",
    effect: "read",
    description: "Search INI files. Params: query",
    handler: async (ctx, p) => {
      ctx.project.ensureLoaded();
      const configDir = ctx.project.configDir!;
      if (!fs.existsSync(configDir)) throw new Error(`Config directory not found: ${configDir}`);
      const query = (p.query as string).toLowerCase();
      const results: Array<{ file: string; section: string; line: number; content: string }> = [];
      for (const file of findIniFiles(configDir)) {
        const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/); let currentSection = "";
        for (let i = 0; i < lines.length; i++) {
          const line = lines[i].trim();
          if (line.startsWith("[") && line.endsWith("]")) { currentSection = line.slice(1, -1); continue; }
          if (line.toLowerCase().includes(query)) results.push({ file: path.basename(file), section: currentSection, line: i + 1, content: line });
        }
      }
      return { query: p.query, resultCount: results.length, results: results.slice(0, 200) };
    },
  },
  list_config_tags: {
    kind: "handler",
    effect: "read",
    description: "Extract gameplay tags from config. Params: none",
    handler: async (ctx) => {
      ctx.project.ensureLoaded();
      const configDir = ctx.project.configDir!;
      const tags = new Set<string>();
      for (const file of findIniFiles(configDir)) {
        const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/); let inTagSection = false;
        for (const line of lines) {
          const trimmed = line.trim();
          if (trimmed.startsWith("[") && trimmed.endsWith("]")) { inTagSection = trimmed.toLowerCase().includes("gameplaytag"); continue; }
          if (!inTagSection) continue;
          let match = trimmed.match(/Tag="?([^"]+)"?/); if (match) { tags.add(match[1]); continue; }
          match = trimmed.match(/TagName="([^"]+)"/); if (match) tags.add(match[1]);
        }
      }
      const sorted = [...tags].sort();
      return { source: "config_files", count: sorted.length, tags: sorted, tree: buildTagTree(sorted) };
    },
  },
  set_config: specBp("mutate", "Write to INI.", "set_config"),
  set_developer_setting: specBp("mutate",
    "Set a UPROPERTY(Config) value on a settings class default object (a UDeveloperSettings subclass or any config= class) and save it the way the Project Settings dialog does: PostEditChangeProperty on the default object, then UpdateSinglePropertyInConfigFile into Default<Config>.ini (TryUpdateDefaultConfigFile for a container), or the per-user config with persist=user. The ini is the one the class names, never a caller path. Unlike set_config, the settings object sees the change and the value survives a restart; unlike editor(set_object_property), it is saved. Returns configFile, section, key and the iniLines now on disk. Read the current value with editor(get_property) on the class path.",
    "set_developer_setting",
  ),
  get_maps_settings: specBp("read", "Read Project Settings > Maps & Modes: default GameMode, server GameMode, GameInstance class, game/server/editor startup/transition maps. settings is the live value in the editor, stored is what DefaultEngine.ini holds (null when the file has no entry) (#1566).", "get_maps_settings"),
  set_maps_settings: specBp("mutate", "Write Project Settings > Maps & Modes. Classes accept a /Script path, short name or Blueprint asset path and must derive from GameModeBase or GameInstance; maps must be existing world assets; an empty string clears. Every value is validated before any is written. Updates the live settings object and DefaultEngine.ini, then returns settings, stored (read back from the file) and persisted per write. For any other Project Settings page, editor(set_property) on the settings class path persists a config property the same way (#1566).", "set_maps_settings"),
  resolve_collision_profile: blueprintSpecBp("read",
    "Read one collision profile's resolved per-channel responses: collisionEnabled, objectType, and every channel with Block/Overlap/Ignore. This is the project-side half of blueprint(get_component_collision) (#925): a component's ResponseArray only lists the channels it OVERRIDES, so the profile is where an inherited response actually comes from. Project trace and object channels appear under their configured names, with enumName (ECC_GameTraceChannel1) alongside so a caller can key on something stable. By default the eight engine channels plus every channel the project configured are returned; includeAllChannels=true adds the unused slots. channel narrows it to one. A profile that does not exist lists the ones that do.",
    "resolve_collision_profile",
  ),

};
