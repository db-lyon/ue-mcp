import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { deploy, deploySummary, findEngineInstall } from "../deployer.js";
import { resolveConfigPath, findIniFiles, parseIni, buildTagTree } from "../config-parser.js";
import { parseHeader, collectFiles, findSourceRoots, resolveModuleDir } from "../cpp-parser.js";

export const projectTool: ToolDef = categoryTool(
  "project",
  "Project status, config INI files, and C++ source inspection.",
  {
    get_status: {
      description: "Check server mode and editor connection",
      handler: async (ctx) => {
        const flows = ctx.getFlows?.() ?? [];
        return {
          mode: ctx.bridge.isConnected ? "live" : "disconnected",
          editorConnected: ctx.bridge.isConnected,
          project: ctx.project.isLoaded ? { name: ctx.project.projectName, path: ctx.project.projectPath, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation, config: Object.keys(ctx.project.config).length > 0 ? ctx.project.config : undefined } : null,
          // Pre-built sequences for this project. If the user's request
          // matches a flow's name/description, prefer flow(action="run")
          // over composing the sequence by hand. See SERVER_INSTRUCTIONS.
          flows: flows.length > 0 ? flows : undefined,
        };
      },
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
        let resolved = headerPath;
        if (!path.isAbsolute(headerPath)) {
          const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
          const candidate = roots.map(r => path.join(r, headerPath)).find(c => fs.existsSync(c));
          resolved = candidate ?? path.join(ctx.project.projectDir!, "Source", headerPath);
        }
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
        return parseHeader(fs.readFileSync(resolved, "utf-8"), resolved);
      },
    },
    read_module: {
      description: "Read module source. Params: moduleName",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const moduleName = p.moduleName as string;
        const moduleDir = resolveModuleDir(ctx.project.projectDir!, ctx.project.projectName, moduleName);
        if (!moduleDir) {
          const tried = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
          throw new Error(`Module '${moduleName}' not found. Searched: ${tried.length ? tried.join(", ") : "(no Source/ directories)"}`);
        }
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
        const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
        if (roots.length === 0) throw new Error(`No Source/ directory found under ${ctx.project.projectDir}`);
        const modules: Array<{ name: string; path: string; hasBuildCs: boolean; sourceRoot: string }> = [];
        for (const root of roots) {
          for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
            if (!entry.isDirectory()) continue;
            const modDir = path.join(root, entry.name);
            modules.push({ name: entry.name, path: modDir, hasBuildCs: fs.existsSync(path.join(modDir, `${entry.name}.Build.cs`)), sourceRoot: root });
          }
        }
        return { sourceRoots: roots, moduleCount: modules.length, modules };
      },
    },
    search_cpp: {
      description: "Search .h/.cpp files. Params: query, directory?",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
        if (roots.length === 0) throw new Error(`No Source/ directory found under ${ctx.project.projectDir}`);
        // If directory is provided, resolve it relative to whichever root contains it.
        let searchDirs: string[] = roots;
        if (p.directory) {
          const sub = p.directory as string;
          if (path.isAbsolute(sub)) {
            if (!fs.existsSync(sub)) throw new Error(`Directory not found: ${sub}`);
            searchDirs = [sub];
          } else {
            const matches = roots.map(r => path.join(r, sub)).filter(d => fs.existsSync(d));
            if (matches.length === 0) throw new Error(`Directory '${sub}' not found under any source root: ${roots.join(", ")}`);
            searchDirs = matches;
          }
        }
        const query = (p.query as string).toLowerCase();
        const results: Array<{ file: string; line: number; content: string; sourceRoot: string }> = [];
        let stopped = false;
        function search(dir: string, root: string): void {
          if (stopped) return;
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (stopped) return;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) search(full, root);
            else if (/\.(h|cpp|inl)$/i.test(entry.name)) {
              const lines = fs.readFileSync(full, "utf-8").split(/\r?\n/);
              for (let i = 0; i < lines.length; i++) {
                if (lines[i].toLowerCase().includes(query)) {
                  results.push({ file: path.relative(root, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd(), sourceRoot: root });
                  if (results.length >= 500) { stopped = true; return; }
                }
              }
            }
          }
        }
        for (const d of searchDirs) {
          // Find which root this dir belongs to for relative-path reporting.
          const owningRoot = roots.find(r => d === r || d.startsWith(r + path.sep)) ?? d;
          search(d, owningRoot);
        }
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
        const sp = p.sourcePath as string;
        if (!sp) throw new Error("Missing 'sourcePath' parameter");
        let resolved = sp;
        if (!path.isAbsolute(sp)) {
          const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
          const candidate = roots.map(r => path.join(r, sp)).find(c => fs.existsSync(c));
          resolved = candidate ?? path.join(ctx.project.projectDir!, "Source", sp);
        }
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

    add_cpp_member: {
      // #423: append a UPROPERTY / UFUNCTION declaration to an existing UCLASS
      // header in the right access-specifier block. The recurring trap is that
      // raw appending lands the declaration in whatever access section the
      // class happened to end in (often private:), which makes UHT reject
      // BlueprintReadWrite ("should not be used on private members"). This
      // handler inserts the requested access specifier before the declaration
      // and restores the previous one after, so the caller doesn't need to
      // know what section was active at the end of the class body.
      description:
        "Append a UPROPERTY/UFUNCTION declaration to an existing UCLASS header inside the access specifier you choose. Idempotent: if a declaration containing the same memberName is already present, returns existed:true. Params: headerPath (relative to Source/ or absolute), declaration (full multi-line UPROPERTY(...) / UFUNCTION(...) block plus its single-line member or function signature), memberName (the identifier the declaration introduces - used for idempotency), access? ('public'|'protected'|'private', default 'public').",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const headerPath = p.headerPath as string;
        const declaration = p.declaration as string;
        const memberName = p.memberName as string;
        const access = (((p.access as string) || "public").toLowerCase()) as "public" | "protected" | "private";
        if (!headerPath) throw new Error("Missing 'headerPath'");
        if (!declaration) throw new Error("Missing 'declaration'");
        if (!memberName) throw new Error("Missing 'memberName'");
        if (access !== "public" && access !== "protected" && access !== "private") {
          throw new Error("'access' must be 'public' | 'protected' | 'private'");
        }
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const resolved = path.isAbsolute(headerPath) ? path.resolve(headerPath) : path.resolve(sourceDir, headerPath);
        const sourceAbs = path.resolve(sourceDir);
        if (!resolved.startsWith(sourceAbs + path.sep) && resolved !== sourceAbs) {
          throw new Error(`Refusing to write outside project Source/: ${resolved}`);
        }
        if (!/\.h$/i.test(resolved)) {
          throw new Error(`add_cpp_member only accepts .h files (got '${path.extname(resolved)}')`);
        }
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);

        const original = fs.readFileSync(resolved, "utf-8");

        // Idempotency: does a declaration with this memberName already exist?
        // Match identifier as a whole word - tolerant of pointer/ref/const sigils.
        const wordRe = new RegExp(`\\b${memberName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\b`);
        if (wordRe.test(original)) {
          return { status: "existed", path: resolved, memberName };
        }

        // Find the class's terminating "};" - last occurrence in the file is
        // the conservative choice; UCLASS headers rarely have nested types.
        const closeIdx = original.lastIndexOf("};");
        if (closeIdx < 0) {
          throw new Error(`Could not find class closing '};' in ${resolved}`);
        }

        // Walk backward from closeIdx to find the most recent access specifier.
        // Default to "private" if none found (C++ class default).
        const before = original.slice(0, closeIdx);
        const accessRe = /(^|\n)\s*(public|protected|private)\s*:\s*(\/\/[^\n]*)?\s*(?=\n)/g;
        let lastAccess: "public" | "protected" | "private" = "private";
        let m: RegExpExecArray | null;
        while ((m = accessRe.exec(before)) !== null) {
          lastAccess = m[2] as "public" | "protected" | "private";
        }

        // Indent the declaration to match the class body (one tab is the
        // convention used by UE templates).
        const indented = declaration
          .replace(/\r\n/g, "\n")
          .split("\n")
          .map(line => (line.length === 0 ? line : (line.startsWith("\t") ? line : `\t${line}`)))
          .join("\n");

        // If the requested access section already exists and is the most recent
        // one before the closing brace, we can append the declaration directly
        // without restoring a different prior access.
        const sameAsPrior = access === lastAccess;
        const insertion = sameAsPrior
          ? `\n${indented}\n`
          : `\n${access}:\n${indented}\n${lastAccess}:\n`;

        const updated = `${original.slice(0, closeIdx)}${insertion}${original.slice(closeIdx)}`;
        fs.writeFileSync(resolved, updated, "utf-8");
        return {
          status: "added",
          path: resolved,
          memberName,
          access,
          restoredPrior: sameAsPrior ? null : lastAccess,
          hint: "Call live_coding_compile to hot-reload, or build_project for a full rebuild.",
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
    declaration: z.string().optional().describe("For add_cpp_member: full UPROPERTY(...) / UFUNCTION(...) block plus the member or function signature."),
    memberName: z.string().optional().describe("For add_cpp_member: the identifier the declaration introduces (used for idempotency)."),
    access: z.enum(["public", "private"]).optional().describe("For add_module_dependency: 'public' (PublicDependencyModuleNames) or 'private' (default)."),
  },
);
