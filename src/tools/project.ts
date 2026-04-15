import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { categoryTool, bp, type ToolDef, type ToolContext } from "../types.js";
import { deploy, deploySummary, findEngineInstall } from "../deployer.js";

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
      description: "Check server mode and editor connection",
      handler: async (ctx) => ({
        mode: ctx.bridge.isConnected ? "live" : "disconnected",
        editorConnected: ctx.bridge.isConnected,
        project: ctx.project.isLoaded ? { name: ctx.project.projectName, path: ctx.project.projectPath, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation, config: Object.keys(ctx.project.config).length > 0 ? ctx.project.config : undefined } : null,
      }),
    },
    set_project: {
      description: "Switch project. Params: projectPath",
      handler: async (ctx, p) => {
        ctx.project.setProject(p.projectPath as string);
        const result = deploy(ctx.project);
        try { await ctx.bridge.connect(); } catch { /* editor might not be running */ }
        return { success: true, projectName: ctx.project.projectName, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation, editorConnected: ctx.bridge.isConnected, bridgeSetup: deploySummary(result) };
      },
    },
    get_info: {
      description: "Read .uproject file details",
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        return { projectName: ctx.project.projectName, engineAssociation: ctx.project.engineAssociation, contentDir: ctx.project.contentDir, uprojectContents: JSON.parse(fs.readFileSync(ctx.project.projectPath!, "utf-8")) };
      },
    },
    read_config: {
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
      description: "Extract gameplay tags from config",
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
      description: "Parse a .h file. Params: headerPath",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const headerPath = p.headerPath as string;
        const resolved = path.isAbsolute(headerPath) ? headerPath : path.join(ctx.project.projectDir!, "Source", headerPath);
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
        return parseHeader(fs.readFileSync(resolved, "utf-8"), resolved);
      },
    },
    read_module: {
      description: "Read module source. Params: moduleName",
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
      description: "List C++ modules",
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        if (!fs.existsSync(sourceDir)) throw new Error(`Source directory not found: ${sourceDir}`);
        const modules = fs.readdirSync(sourceDir, { withFileTypes: true }).filter(e => e.isDirectory()).map(e => ({ name: e.name, path: path.join(sourceDir, e.name), hasBuildCs: fs.existsSync(path.join(sourceDir, e.name, `${e.name}.Build.cs`)) }));
        return { sourceDir, moduleCount: modules.length, modules };
      },
    },
    search_cpp: {
      description: "Search .h/.cpp files. Params: query, directory?",
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
    read_engine_header: {
      description: "Parse a .h file from the engine source tree. Params: headerPath (relative to Engine/Source, or absolute)",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const engineRoot = findEngineInstall(ctx.project.engineAssociation ?? null);
        if (!engineRoot) throw new Error("Could not resolve engine install path");
        const headerPath = p.headerPath as string;
        const resolved = path.isAbsolute(headerPath)
          ? headerPath
          : path.join(engineRoot, "Engine", "Source", headerPath);
        if (!fs.existsSync(resolved)) throw new Error(`Engine header not found: ${resolved}`);
        const content = fs.readFileSync(resolved, "utf-8");
        return { ...parseHeader(content, resolved), engineRoot };
      },
    },
    find_engine_symbol: {
      description: "Grep engine headers for a symbol. Params: symbol, maxResults?",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const engineRoot = findEngineInstall(ctx.project.engineAssociation ?? null);
        if (!engineRoot) throw new Error("Could not resolve engine install path");
        const engineSource = path.join(engineRoot, "Engine", "Source", "Runtime");
        if (!fs.existsSync(engineSource)) throw new Error(`Engine source not found: ${engineSource}`);
        const symbol = p.symbol as string;
        const maxResults = (p.maxResults as number) ?? 100;
        const results: Array<{ file: string; line: number; content: string }> = [];
        const needle = symbol;
        function scan(dir: string): void {
          if (results.length >= maxResults) return;
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (results.length >= maxResults) return;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) { scan(full); continue; }
            if (!/\.(h|inl)$/i.test(entry.name)) continue;
            const lines = fs.readFileSync(full, "utf-8").split(/\r?\n/);
            for (let i = 0; i < lines.length; i++) {
              if (lines[i].includes(needle)) {
                results.push({ file: path.relative(engineSource, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd() });
                if (results.length >= maxResults) return;
              }
            }
          }
        }
        scan(engineSource);
        return { symbol, engineRoot, resultCount: results.length, results };
      },
    },
    list_engine_modules: {
      description: "List modules in Engine/Source/Runtime",
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        const engineRoot = findEngineInstall(ctx.project.engineAssociation ?? null);
        if (!engineRoot) throw new Error("Could not resolve engine install path");
        const runtimeDir = path.join(engineRoot, "Engine", "Source", "Runtime");
        if (!fs.existsSync(runtimeDir)) throw new Error(`Runtime dir not found: ${runtimeDir}`);
        const modules = fs.readdirSync(runtimeDir, { withFileTypes: true })
          .filter(e => e.isDirectory())
          .map(e => ({ name: e.name, hasBuildCs: fs.existsSync(path.join(runtimeDir, e.name, `${e.name}.Build.cs`)) }));
        return { engineRoot, moduleCount: modules.length, modules };
      },
    },
    search_engine_cpp: {
      description: "Search engine .h/.cpp/.inl files across Runtime/Editor/Developer/Plugins. Params: query, tree? (Runtime|Editor|Developer|Plugins|all — default Runtime), subdirectory?, maxResults? (default 500)",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const resolvedEngineRoot = findEngineInstall(ctx.project.engineAssociation ?? null);
        if (!resolvedEngineRoot) throw new Error("Could not resolve engine install path");
        const engineRoot: string = resolvedEngineRoot;
        const query = (p.query as string)?.toLowerCase();
        if (!query) throw new Error("Missing required parameter 'query'");
        const tree = (p.tree as string) ?? "Runtime";
        const maxResults = (p.maxResults as number) ?? 500;
        const subdir = p.subdirectory as string | undefined;
        const engineSource = path.join(engineRoot, "Engine", "Source");
        const roots: string[] = [];
        if (tree === "all") {
          for (const t of ["Runtime", "Editor", "Developer"]) {
            const d = path.join(engineSource, t);
            if (fs.existsSync(d)) roots.push(d);
          }
          const pluginsDir = path.join(engineRoot, "Engine", "Plugins");
          if (fs.existsSync(pluginsDir)) roots.push(pluginsDir);
        } else if (tree === "Plugins") {
          const d = path.join(engineRoot, "Engine", "Plugins");
          if (!fs.existsSync(d)) throw new Error(`Engine plugins dir not found: ${d}`);
          roots.push(d);
        } else {
          const d = path.join(engineSource, tree);
          if (!fs.existsSync(d)) throw new Error(`Engine tree '${tree}' not found: ${d}`);
          roots.push(subdir ? path.join(d, subdir) : d);
        }
        const results: Array<{ file: string; line: number; content: string }> = [];
        function scan(dir: string): boolean {
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (results.length >= maxResults) return true;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) {
              if (entry.name === "Intermediate" || entry.name === "Binaries") continue;
              if (scan(full)) return true;
            } else if (/\.(h|cpp|inl)$/i.test(entry.name)) {
              let content: string;
              try { content = fs.readFileSync(full, "utf-8"); } catch { continue; }
              const lines = content.split(/\r?\n/);
              for (let i = 0; i < lines.length; i++) {
                if (lines[i].toLowerCase().includes(query)) {
                  results.push({ file: path.relative(engineRoot, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd() });
                  if (results.length >= maxResults) return true;
                }
              }
            }
          }
          return false;
        }
        for (const r of roots) { if (scan(r)) break; }
        return { query: p.query, tree, subdirectory: subdir ?? "(root)", engineRoot, resultCount: results.length, results };
      },
    },
    set_config: bp("Write to INI. Params: configName, section, key, value", "set_config"),
    build: bp("Build C++ project. Params: configuration?, platform?, clean?", "build_project"),
    generate_project_files: bp("Generate IDE project files (Visual Studio, Xcode, etc.)", "generate_project_files"),

    // v0.7.13 — native C++ authoring. Bridge handlers wrap
    // GameProjectUtils / ILiveCodingModule (same APIs used by the editor's
    // File → New C++ Class and Live Coding menus).
    create_cpp_class: {
      description: "Create a new native UCLASS in a project module. Uses the same engine template path as File → New C++ Class. Writes .h + .cpp; returns both paths plus needsEditorRestart (true unless Live Coding successfully hot-reloaded). Params: className (no prefix), parentClass? (default UObject; accepts short names like 'Actor' or /Script/<Module>.<Class> paths), moduleName? (default: first project module, use list_project_modules to pick), classDomain? ('public'|'private'|'classes', default public), subPath?",
      bridge: "create_cpp_class",
      // AddCodeToProject regenerates IDE project files synchronously — can
      // easily exceed the default 30-second cap on first use.
      timeoutMs: 300_000,
      mapParams: (p) => ({
        className: p.className,
        parentClass: p.parentClass,
        moduleName: p.moduleName,
        classDomain: p.classDomain,
        subPath: p.subPath,
      }),
    },
    list_project_modules: bp(
      "List native modules in the current project (name, host type, source path). Feed moduleName from here into create_cpp_class.",
      "list_project_modules",
      () => ({}),
    ),
    live_coding_compile: {
      description: "Trigger a Live Coding compile (Windows only). Hot-patches method bodies of existing UCLASSes without editor restart — the fast inner loop for UFUNCTION implementations. Does NOT reliably register brand-new UCLASSes; use build_project + editor restart for those. Params: wait? (default false — fire and return 'in_progress').",
      bridge: "live_coding_compile",
      timeoutMs: 300_000,
      mapParams: (p) => ({ wait: p.wait }),
    },
    live_coding_status: bp(
      "Report Live Coding availability/state (available, started, enabledForSession, compiling). Helps choose between live_coding_compile and build_project.",
      "live_coding_status",
      () => ({}),
    ),

    write_cpp_file: {
      description:
        "Write a .h / .cpp / .inl file under the project's Source/ tree. Used to append UPROPERTYs/UFUNCTIONs or method bodies after create_cpp_class. Writes are scoped to Source/ for safety. Params: path (relative to Source/ or absolute within Source/), content (full file contents). After editing, call live_coding_compile (for existing classes) or build_project (for new classes).",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const rel = p.path as string;
        if (!rel) throw new Error("Missing 'path' parameter");
        const content = p.content as string;
        if (typeof content !== "string") throw new Error("Missing or invalid 'content' parameter (must be a string)");

        const resolved = path.isAbsolute(rel) ? path.resolve(rel) : path.resolve(sourceDir, rel);
        const sourceAbs = path.resolve(sourceDir);
        if (!resolved.startsWith(sourceAbs + path.sep) && resolved !== sourceAbs) {
          throw new Error(`Refusing to write outside project Source/: ${resolved}`);
        }
        if (!/\.(h|cpp|inl|cs)$/i.test(resolved)) {
          throw new Error(`write_cpp_file only accepts .h/.cpp/.inl/.cs files (got '${path.extname(resolved)}')`);
        }

        const overwrote = fs.existsSync(resolved);
        fs.mkdirSync(path.dirname(resolved), { recursive: true });
        fs.writeFileSync(resolved, content, "utf-8");
        return {
          path: resolved,
          bytesWritten: Buffer.byteLength(content, "utf-8"),
          overwrote,
          hint: overwrote
            ? "Overwrote existing file. Call live_coding_compile (existing class edits) or build_project for a full rebuild."
            : "Created new file. Call generate_project_files if you also want the IDE project refreshed, then build_project.",
        };
      },
    },
    read_cpp_source: {
      description: "Read a .cpp file from the project Source/ tree. Companion to read_cpp_header for round-trip edits. Params: sourcePath (relative to Source/ or absolute).",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const sp = p.sourcePath as string;
        if (!sp) throw new Error("Missing 'sourcePath' parameter");
        const resolved = path.isAbsolute(sp) ? sp : path.join(sourceDir, sp);
        if (!fs.existsSync(resolved)) throw new Error(`File not found: ${resolved}`);
        const content = fs.readFileSync(resolved, "utf-8");
        return { path: resolved, bytes: content.length, content };
      },
    },
    add_module_dependency: {
      description:
        "Add a module to a target module's Build.cs dependency array. Params: moduleName (the Build.cs to edit — must exist in the project), dependency (module name to add, e.g. 'UMG'), access? ('public'|'private', default 'private'). Creates the corresponding AddRange block if missing. Rebuild required afterward.",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const moduleName = p.moduleName as string;
        const dependency = p.dependency as string;
        const access = ((p.access as string) || "private").toLowerCase();
        if (!moduleName || !dependency) throw new Error("Missing 'moduleName' and/or 'dependency'");
        if (access !== "public" && access !== "private") {
          throw new Error("'access' must be 'public' or 'private'");
        }

        const buildCs = path.join(ctx.project.projectDir!, "Source", moduleName, `${moduleName}.Build.cs`);
        if (!fs.existsSync(buildCs)) {
          throw new Error(`Build.cs not found for module '${moduleName}' at ${buildCs}`);
        }

        let content = fs.readFileSync(buildCs, "utf-8");
        const fieldName = access === "public" ? "PublicDependencyModuleNames" : "PrivateDependencyModuleNames";

        // Already present?
        const existingArrayRe = new RegExp(`${fieldName}\\.AddRange\\s*\\(\\s*new\\s+string\\s*\\[\\s*\\]\\s*\\{([\\s\\S]*?)\\}\\s*\\)\\s*;`, "m");
        const existingMatch = content.match(existingArrayRe);

        if (existingMatch) {
          const body = existingMatch[1];
          const entries = new Set<string>();
          for (const m of body.matchAll(/"([A-Za-z0-9_]+)"/g)) entries.add(m[1]);
          if (entries.has(dependency)) {
            return { status: "existed", buildCs, access, dependency };
          }
          entries.add(dependency);
          const sortedList = [...entries].sort();
          const replacement = `${fieldName}.AddRange(\n\t\t\tnew string[]\n\t\t\t{\n${sortedList.map(e => `\t\t\t\t"${e}",`).join("\n")}\n\t\t\t}\n\t\t);`;
          content = content.replace(existingArrayRe, replacement);
        } else {
          // Insert a new AddRange block before the closing brace of the ModuleRules ctor.
          const ctorCloseRe = /(\n\s*\}\s*\n\s*\})\s*$/;
          if (!ctorCloseRe.test(content)) {
            throw new Error(`Could not locate module ctor in ${buildCs} — edit manually.`);
          }
          const newBlock = `\n\t\t${fieldName}.AddRange(\n\t\t\tnew string[]\n\t\t\t{\n\t\t\t\t"${dependency}",\n\t\t\t}\n\t\t);\n`;
          content = content.replace(ctorCloseRe, `${newBlock}$1`);
        }

        fs.writeFileSync(buildCs, content, "utf-8");
        return {
          status: "updated",
          buildCs,
          access,
          dependency,
          hint: "Rebuild the project (project(build)) for the new dependency to take effect.",
        };
      },
    },
  },
  undefined,
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
    configuration: z.string().optional().describe("Build configuration: Development, Debug, Shipping"),
    platform: z.string().optional().describe("Target platform: Win64, Linux, Mac"),
    clean: z.boolean().optional().describe("Clean build"),
    symbol: z.string().optional().describe("Symbol name for find_engine_symbol"),
    maxResults: z.number().optional().describe("Cap on find_engine_symbol / search_engine_cpp hits (default 100 / 500)"),
    tree: z.string().optional().describe("For search_engine_cpp: Runtime|Editor|Developer|Plugins|all (default Runtime)"),
    subdirectory: z.string().optional().describe("For search_engine_cpp: subdirectory within the chosen tree"),

    // v0.7.13 — native C++ authoring
    className: z.string().optional().describe("For create_cpp_class: new class name (no A/U prefix — handled by parent type)"),
    parentClass: z.string().optional().describe("For create_cpp_class: parent UClass. Short native names ('Actor') or /Script/<Module>.<Class> paths work. Default UObject."),
    classDomain: z.enum(["public", "private", "classes"]).optional().describe("For create_cpp_class: which folder under the module (Public/Private/Classes). Default 'public'."),
    subPath: z.string().optional().describe("For create_cpp_class: nested folder under the class domain (e.g. 'Gameplay/Abilities')."),
    wait: z.boolean().optional().describe("For live_coding_compile: block until compile finishes. Default false."),
    path: z.string().optional().describe("For write_cpp_file: path to write (relative to Source/ or absolute within Source/)."),
    content: z.string().optional().describe("For write_cpp_file: full file contents."),
    sourcePath: z.string().optional().describe("For read_cpp_source: path to .cpp (relative to Source/ or absolute)."),
    dependency: z.string().optional().describe("For add_module_dependency: module name to add (e.g. 'UMG')."),
    access: z.enum(["public", "private"]).optional().describe("For add_module_dependency: 'public' (PublicDependencyModuleNames) or 'private' (default)."),
  },
);
