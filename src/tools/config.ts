import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import type { ToolDef } from "../types.js";

export const configTools: ToolDef[] = [
  {
    name: "read_config",
    description:
      "Read an Unreal config INI file (e.g. 'Engine', 'Game', 'Input', 'Editor'). " +
      "Returns all sections and key-value pairs. Prefix 'Default' is auto-added if missing.",
    schema: {
      configName: z
        .string()
        .describe("Config file name (e.g. 'Engine', 'Game', 'DefaultEngine.ini')"),
    },
    handler: async (ctx, params) => {
      ctx.project.ensureLoaded();
      const filePath = resolveConfigPath(ctx.project.configDir!, params.configName as string);
      if (!fs.existsSync(filePath)) {
        throw new Error(`Config file not found: ${filePath}`);
      }
      const sections = parseIni(fs.readFileSync(filePath, "utf-8"));
      return { path: filePath, configName: params.configName, sectionCount: Object.keys(sections).length, sections };
    },
  },
  {
    name: "search_config",
    description:
      "Search across all project config INI files for a string. " +
      "Returns matching lines with file, section, and line number.",
    schema: {
      query: z.string().describe("Text to search for (case-insensitive)"),
    },
    handler: async (ctx, params) => {
      ctx.project.ensureLoaded();
      const configDir = ctx.project.configDir!;
      if (!fs.existsSync(configDir)) throw new Error(`Config directory not found: ${configDir}`);

      const query = (params.query as string).toLowerCase();
      const results: Array<{ file: string; section: string; line: number; content: string }> = [];

      for (const file of findIniFiles(configDir)) {
        const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/);
        let currentSection = "";
        for (let i = 0; i < lines.length; i++) {
          const line = lines[i].trim();
          if (line.startsWith("[") && line.endsWith("]")) {
            currentSection = line.slice(1, -1);
            continue;
          }
          if (line.toLowerCase().includes(query)) {
            results.push({ file: path.basename(file), section: currentSection, line: i + 1, content: line });
          }
        }
      }

      return { query: params.query, resultCount: results.length, results: results.slice(0, 200) };
    },
  },
  {
    name: "list_config_tags",
    description:
      "Extract gameplay tags defined in project config INI files. " +
      "Returns a flat sorted list and a hierarchical tree.",
    schema: {},
    handler: async (ctx) => {
      ctx.project.ensureLoaded();
      const configDir = ctx.project.configDir!;
      if (!fs.existsSync(configDir)) throw new Error(`Config directory not found: ${configDir}`);

      const tags = new Set<string>();

      for (const file of findIniFiles(configDir)) {
        const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/);
        let inTagSection = false;
        for (const line of lines) {
          const trimmed = line.trim();
          if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
            inTagSection = trimmed.toLowerCase().includes("gameplaytag");
            continue;
          }
          if (!inTagSection) continue;

          let match = trimmed.match(/Tag="?([^"]+)"?/);
          if (match) { tags.add(match[1]); continue; }
          match = trimmed.match(/TagName="([^"]+)"/);
          if (match) tags.add(match[1]);
        }
      }

      const sorted = [...tags].sort();
      return { source: "config_files", count: sorted.length, tags: sorted, tree: buildTagTree(sorted) };
    },
  },
];

function resolveConfigPath(configDir: string, configName: string): string {
  if (path.isAbsolute(configName)) return configName;
  if (!configName.endsWith(".ini")) configName += ".ini";
  if (!configName.startsWith("Default")) {
    const defaultPath = path.join(configDir, `Default${configName}`);
    if (fs.existsSync(defaultPath)) return defaultPath;
  }
  return path.join(configDir, configName);
}

function findIniFiles(dir: string): string[] {
  const results: string[] = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) results.push(...findIniFiles(full));
    else if (entry.name.endsWith(".ini")) results.push(full);
  }
  return results;
}

function parseIni(content: string): Record<string, Record<string, string>> {
  const sections: Record<string, Record<string, string>> = {};
  let current = "Global";
  for (const line of content.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith(";") || trimmed.startsWith("#")) continue;
    if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
      current = trimmed.slice(1, -1);
      if (!sections[current]) sections[current] = {};
      continue;
    }
    if (!sections[current]) sections[current] = {};
    const eq = trimmed.indexOf("=");
    if (eq > 0) {
      const key = trimmed.slice(0, eq).replace(/^[+\-.]/, "");
      sections[current][key] = trimmed.slice(eq + 1);
    }
  }
  return sections;
}

function buildTagTree(tags: string[]): Record<string, unknown> {
  const tree: Record<string, unknown> = {};
  for (const tag of tags) {
    let current = tree;
    for (const part of tag.split(".")) {
      if (!current[part]) current[part] = {};
      current = current[part] as Record<string, unknown>;
    }
  }
  return tree;
}
