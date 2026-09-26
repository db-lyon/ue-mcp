import * as fs from "node:fs";
import * as path from "node:path";
import { resolveConfigPath, findIniFiles, parseIni, buildTagTree } from "../../config-parser.js";
import { specBp } from "../specs/project.generated.js";
import { specBp as blueprintSpecBp } from "../specs/blueprint.generated.js";
import type { ActionSpec } from "../../types.js";

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
  resolve_collision_profile: blueprintSpecBp("read",
    "Read one collision profile's resolved per-channel responses: collisionEnabled, objectType, and every channel with Block/Overlap/Ignore. This is the project-side half of blueprint(get_component_collision) (#925): a component's ResponseArray only lists the channels it OVERRIDES, so the profile is where an inherited response actually comes from. Project trace and object channels appear under their configured names, with enumName (ECC_GameTraceChannel1) alongside so a caller can key on something stable. By default the eight engine channels plus every channel the project configured are returned; includeAllChannels=true adds the unused slots. channel narrows it to one. A profile that does not exist lists the ones that do.",
    "resolve_collision_profile",
  ),

};
