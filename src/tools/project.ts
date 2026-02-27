import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { categoryTool, bp, type ToolDef, type ToolContext } from "../types.js";
import { deploy, deploySummary } from "../deployer.js";

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

const UCLASS_RE = /UCLASS\(([^)]*)\)\s*class\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+([\w:,\s]+))?\s*\{/g;
const USTRUCT_RE = /USTRUCT\(([^)]*)\)\s*struct\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+(\w+))?\s*\{/g;
const UENUM_RE = /UENUM\(([^)]*)\)\s*enum\s+(?:class\s+)?(\w+)/g;
const UPROPERTY_RE = /UPROPERTY\(([^)]*)\)\s*(?:(?:TArray|TMap|TSet|TSubclassOf|TSoftObjectPtr|TObjectPtr|TWeakObjectPtr)<[^>]+>|[\w:*&]+)\s+(\w+)/g;
const UFUNCTION_RE = /UFUNCTION\(([^)]*)\)\s*(?:virtual\s+)?(?:static\s+)?([\w:*&<>]+)\s+(\w+)\s*\(/g;
const ENUM_VALUE_RE = /(\w+)\s*(?:=\s*[^,]+)?\s*(?:UMETA\(([^)]*)\))?\s*,?/g;

function parseHeader(content: string, filePath: string) {
  const classes: unknown[] = [], structs: unknown[] = [], enums: unknown[] = [];
  for (const m of content.matchAll(UCLASS_RE)) classes.push({ name: m[2], specifiers: m[1].trim(), parent: m[3]?.trim() ?? null });
  for (const m of content.matchAll(USTRUCT_RE)) structs.push({ name: m[2], specifiers: m[1].trim(), parent: m[3]?.trim() ?? null });
  for (const m of content.matchAll(UENUM_RE)) {
    const enumName = m[2]; const afterEnum = content.slice(m.index! + m[0].length);
    const braceStart = afterEnum.indexOf("{"); const braceEnd = afterEnum.indexOf("}");
    const values: Array<{ name: string; meta?: string }> = [];
    if (braceStart >= 0 && braceEnd > braceStart) {
      for (const vm of afterEnum.slice(braceStart + 1, braceEnd).matchAll(ENUM_VALUE_RE))
        if (vm[1] && !vm[1].startsWith("//")) values.push({ name: vm[1], meta: vm[2]?.trim() || undefined });
    }
    enums.push({ name: enumName, specifiers: m[1].trim(), values });
  }
  const properties: unknown[] = [], functions: unknown[] = [];
  for (const m of content.matchAll(UPROPERTY_RE)) properties.push({ name: m[2], specifiers: m[1].trim() });
  for (const m of content.matchAll(UFUNCTION_RE)) functions.push({ name: m[3], returnType: m[2], specifiers: m[1].trim() });
  return { path: filePath, classes, structs, enums, properties, functions };
}

function collectFiles(dir: string, headers: string[], sources: string[]): void {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) collectFiles(full, headers, sources);
    else if (entry.name.endsWith(".h")) headers.push(full);
    else if (entry.name.endsWith(".cpp")) sources.push(full);
  }
}

export const projectTool: ToolDef = categoryTool(
  "project",
  "Project status, config INI files, and C++ source inspection.",
  {
    get_status: {
      handler: async (ctx) => ({
        mode: ctx.bridge.isConnected ? "live" : "disconnected",
        editorConnected: ctx.bridge.isConnected,
        project: ctx.project.isLoaded ? { name: ctx.project.projectName, path: ctx.project.projectPath, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation } : null,
      }),
    },
    set_project: {
      handler: async (ctx, p) => {
        ctx.project.setProject(p.projectPath as string);
        const result = deploy(ctx.project);
        try { await ctx.bridge.connect(); } catch { /* editor might not be running */ }
        return { success: true, projectName: ctx.project.projectName, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation, editorConnected: ctx.bridge.isConnected, bridgeSetup: deploySummary(result) };
      },
    },
    get_info: {
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        return { projectName: ctx.project.projectName, engineAssociation: ctx.project.engineAssociation, contentDir: ctx.project.contentDir, uprojectContents: JSON.parse(fs.readFileSync(ctx.project.projectPath!, "utf-8")) };
      },
    },
    read_config: {
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const filePath = resolveConfigPath(ctx.project.configDir!, p.configName as string);
        if (!fs.existsSync(filePath)) throw new Error(`Config file not found: ${filePath}`);
        const sections = parseIni(fs.readFileSync(filePath, "utf-8"));
        return { path: filePath, configName: p.configName, sectionCount: Object.keys(sections).length, sections };
      },
    },
    search_config: {
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
    read_cpp_header: {
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const headerPath = p.headerPath as string;
        const resolved = path.isAbsolute(headerPath) ? headerPath : path.join(ctx.project.projectDir!, "Source", headerPath);
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
        return parseHeader(fs.readFileSync(resolved, "utf-8"), resolved);
      },
    },
    read_module: {
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const moduleName = p.moduleName as string;
        const moduleDir = path.join(sourceDir, moduleName);
        if (!fs.existsSync(moduleDir)) throw new Error(`Module directory not found: ${moduleDir}`);
        const headers: string[] = [], sources: string[] = [];
        collectFiles(moduleDir, headers, sources);
        const buildCs = path.join(moduleDir, `${moduleName}.Build.cs`);
        return { moduleName, path: moduleDir, headerCount: headers.length, sourceCount: sources.length, headers: headers.map(h => path.relative(moduleDir, h).replace(/\\/g, "/")), sources: sources.map(s => path.relative(moduleDir, s).replace(/\\/g, "/")), buildCs: fs.existsSync(buildCs) ? fs.readFileSync(buildCs, "utf-8") : null };
      },
    },
    list_modules: {
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        if (!fs.existsSync(sourceDir)) throw new Error(`Source directory not found: ${sourceDir}`);
        const modules = fs.readdirSync(sourceDir, { withFileTypes: true }).filter(e => e.isDirectory()).map(e => ({ name: e.name, path: path.join(sourceDir, e.name), hasBuildCs: fs.existsSync(path.join(sourceDir, e.name, `${e.name}.Build.cs`)) }));
        return { sourceDir, moduleCount: modules.length, modules };
      },
    },
    search_cpp: {
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const searchDir = p.directory ? path.join(sourceDir, p.directory as string) : sourceDir;
        if (!fs.existsSync(searchDir)) throw new Error(`Directory not found: ${searchDir}`);
        const query = (p.query as string).toLowerCase();
        const results: Array<{ file: string; line: number; content: string }> = [];
        function search(dir: string): void {
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) search(full);
            else if (/\.(h|cpp|inl)$/i.test(entry.name)) {
              const lines = fs.readFileSync(full, "utf-8").split(/\r?\n/);
              for (let i = 0; i < lines.length; i++) {
                if (lines[i].toLowerCase().includes(query)) { results.push({ file: path.relative(sourceDir, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd() }); if (results.length >= 500) return; }
              }
            }
          }
        }
        search(searchDir);
        return { query: p.query, directory: p.directory ?? "(all)", resultCount: results.length, results };
      },
    },
    set_config: bp("set_config"),
  },
  `- get_status: Check server mode and editor connection
- set_project: Switch project. Params: projectPath
- get_info: Read .uproject file details
- read_config: Read INI config. Params: configName (e.g. 'Engine', 'Game')
- search_config: Search INI files. Params: query
- list_config_tags: Extract gameplay tags from config
- set_config: Write to INI. Params: configName, section, key, value
- read_cpp_header: Parse a .h file. Params: headerPath
- read_module: Read module source. Params: moduleName
- list_modules: List C++ modules
- search_cpp: Search .h/.cpp files. Params: query, directory?`,
  {
    projectPath: z.string().optional().describe("For set_project: path to .uproject"),
    configName: z.string().optional().describe("For read_config/set_config: config file name"),
    query: z.string().optional().describe("For search_config/search_cpp: search text"),
    headerPath: z.string().optional().describe("For read_cpp_header: path to .h file"),
    moduleName: z.string().optional().describe("For read_module: module name"),
    directory: z.string().optional().describe("For search_cpp: subdirectory"),
    section: z.string().optional().describe("For set_config: INI section"),
    key: z.string().optional().describe("For set_config: INI key"),
    value: z.string().optional().describe("For set_config: INI value"),
  },
);
