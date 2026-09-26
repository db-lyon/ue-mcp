import * as fs from "node:fs";
import * as path from "node:path";
import { parseHeader, collectFiles, findSourceRoots, resolveModuleDir } from "../../codeintel/cpp-parser.js";
import { listContent } from "../../codeintel/content-index.js";
import { specBp } from "../specs/project.generated.js";
import type { ToolContext, ActionSpec } from "../../core/types.js";

/**
 * Resolve a module name to its Source/<Module> directory, searching the project
 * Source roots AND every plugin under Plugins/<*>/Source/ (which findSourceRoots
 * does not cover). Empty moduleName returns the project's first module dir.
 * (#543: plugin-module source authoring.)
 */
function resolveSourceModuleDir(projectDir: string, projectName: string | null, moduleName: string): string | null {
  const roots = [...findSourceRoots(projectDir, projectName)];
  // Add each plugin's Source dir as a search root.
  const pluginsDir = path.join(projectDir, "Plugins");
  if (fs.existsSync(pluginsDir)) {
    const walk = (dir: string, depth: number) => {
      if (depth > 3) return;
      for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue;
        const full = path.join(dir, entry.name);
        if (entry.name === "Source") roots.push(full);
        else walk(full, depth + 1);
      }
    };
    try { walk(pluginsDir, 0); } catch { /* ignore unreadable plugin dirs */ }
  }
  for (const root of roots) {
    if (!moduleName) {
      // First module dir that holds a Build.cs.
      if (!fs.existsSync(root)) continue;
      for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
        if (entry.isDirectory() && fs.existsSync(path.join(root, entry.name, `${entry.name}.Build.cs`))) {
          return path.join(root, entry.name);
        }
      }
    } else {
      const modDir = path.join(root, moduleName);
      if (fs.existsSync(path.join(modDir, `${moduleName}.Build.cs`))) return modDir;
    }
  }
  return null;
}

/** The project's own files and C++ source: read, search, write, add modules and members. */
export const sourceActions: Record<string, ActionSpec> = {
  read_cpp_header: {
    kind: "handler",
    effect: "read",
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
    kind: "handler",
    effect: "read",
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
    kind: "handler",
    effect: "read",
    description: "List C++ modules. Params: none",
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
    kind: "handler",
    effect: "read",
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
  list_content_assets: {
    kind: "handler",
    effect: "read",
    description:
      "List the project's assets from the package files on DISK, which is the one asset query that "
      + "works with no editor running. Takes a mount path (/Game, /Game/Characters, or a plugin's "
      + "/MyPlugin) and resolves it through the same mount table the live path uses, so an offline "
      + "listing names assets exactly as the editor would. It answers existence, layout, size and "
      + "modified time, and it deliberately does not answer class, registry tags or dependencies: "
      + "those live in the editor's asset registry and are not in the file, so asset(list) and "
      + "asset(search) remain the answer once an editor is up. maxResults stops the walk rather "
      + "than trimming the result, and truncated says when it did. "
      + "Params: contentPath? (default /Game), recursive? (default true), namePattern? "
      + "(case-insensitive substring of the asset name), maxResults? (default 1000)",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => listContent(ctx.project, {
      contentPath: p.contentPath as string | undefined,
      recursive: p.recursive as boolean | undefined,
      namePattern: p.namePattern as string | undefined,
      maxResults: p.maxResults as number | undefined,
    }),
  },
  list_files: {
    kind: "handler",
    effect: "read",
    description: "List files on disk under a directory, optionally filtered by extension(s). Runs in the MCP server process (no editor round-trip). Params: directory (absolute, or relative to the project dir), extensions? (e.g. ['png','exr'] or 'png'), recursive? (default false), maxResults? (default 1000) (#608)",
    handler: async (ctx, p) => {
      ctx.project.ensureLoaded();
      const dirArg = p.directory as string;
      if (!dirArg) throw new Error("Missing 'directory'");
      const base = path.isAbsolute(dirArg) ? dirArg : path.join(ctx.project.projectDir!, dirArg);
      if (!fs.existsSync(base)) throw new Error(`Directory not found: ${base}`);
      const extsRaw = p.extensions;
      const exts = (Array.isArray(extsRaw) ? extsRaw : extsRaw ? [extsRaw] : [])
        .map((e) => String(e).replace(/^\./, "").toLowerCase());
      const recursive = (p.recursive as boolean) ?? false;
      const maxResults = (p.maxResults as number) ?? 1000;
      const results: Array<{ path: string; name: string; sizeBytes: number; ext: string }> = [];
      const walk = (dir: string): void => {
        if (results.length >= maxResults) return;
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
          if (results.length >= maxResults) return;
          const full = path.join(dir, entry.name);
          if (entry.isDirectory()) { if (recursive) walk(full); continue; }
          const ext = path.extname(entry.name).replace(/^\./, "").toLowerCase();
          if (exts.length > 0 && !exts.includes(ext)) continue;
          let sizeBytes = 0;
          try { sizeBytes = fs.statSync(full).size; } catch { /* race */ }
          results.push({ path: full, name: entry.name, sizeBytes, ext });
        }
      };
      walk(base);
      return { directory: base, extensions: exts, recursive, count: results.length, files: results };
    },
  },
  // v0.7.13 - native C++ authoring. Bridge handlers wrap
  // GameProjectUtils / ILiveCodingModule (same APIs used by the editor's
  // File → New C++ Class and Live Coding menus).
  create_cpp_class: {
    ...specBp("mutate", "Create a new native UCLASS in a project module. Uses the same engine template path as File → New C++ Class. Writes .h + .cpp; returns both paths plus needsEditorRestart (true unless Live Coding successfully hot-reloaded).", "create_cpp_class"),
    // AddCodeToProject regenerates IDE project files synchronously - can
    // easily exceed the default 30-second cap on first use.
    timeoutMs: 300_000,
  },
  list_project_modules: specBp("read", 
    "List native modules in the current project (name, host type, source path), in the .uproject's own declaration order. Feed moduleName from here into create_cpp_class.",
    "list_project_modules",
  ),
  write_cpp_file: {
    kind: "handler",
    effect: "mutate",
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
    kind: "handler",
    effect: "read",
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
  write_source_file: {
    kind: "handler",
    effect: "mutate",
    description:
      "Write a .h/.cpp/.inl into a named module's Public/Private folder (resolves the module dir for you, including plugin modules under Plugins/*/Source/ that write_cpp_file refuses). After a new file, build_project + restart; after a body edit, live_coding_compile. Params: module (module name, default the project's primary module), visibility (Public|Private, default Private), fileName, content.",
    handler: async (ctx, p) => {
      ctx.project.ensureLoaded();
      const moduleName = (p.module as string) ?? "";
      const fileName = p.fileName as string;
      if (!fileName) throw new Error("Missing 'fileName' parameter");
      const content = p.content as string;
      if (typeof content !== "string") throw new Error("Missing or invalid 'content' parameter (must be a string)");
      const visibility = ((p.visibility as string) || "Private");
      const vis = /^public$/i.test(visibility) ? "Public" : /^private$/i.test(visibility) ? "Private" : "";

      const moduleDir = resolveSourceModuleDir(ctx.project.projectDir!, ctx.project.projectName, moduleName);
      if (!moduleDir) throw new Error(`Module not found: '${moduleName || "(default)"}'. Use list_modules to see available modules.`);

      const target = vis ? path.join(moduleDir, vis, fileName) : path.join(moduleDir, fileName);
      if (!/\.(h|cpp|inl)$/i.test(target)) throw new Error(`write_source_file only accepts .h/.cpp/.inl files (got '${path.extname(target)}')`);

      const overwrote = fs.existsSync(target);
      fs.mkdirSync(path.dirname(target), { recursive: true });
      fs.writeFileSync(target, content, "utf-8");
      return {
        module: path.basename(moduleDir),
        path: target,
        bytesWritten: Buffer.byteLength(content, "utf-8"),
        overwrote,
        hint: overwrote
          ? "Overwrote existing file. live_coding_compile for body edits, build_project for structural changes."
          : "New file written. Run build_project and restart the editor for UE to register new types.",
      };
    },
  },
  read_source_file: {
    kind: "handler",
    effect: "read",
    description:
      "Read a .h/.cpp/.inl from a named module's folder (companion to write_source_file; resolves plugin modules too). With no visibility it tries Public then Private then the module root. Params: module, visibility?, fileName.",
    handler: async (ctx, p) => {
      ctx.project.ensureLoaded();
      const moduleName = (p.module as string) ?? "";
      const fileName = p.fileName as string;
      if (!fileName) throw new Error("Missing 'fileName' parameter");
      const visibility = (p.visibility as string) || "";

      const moduleDir = resolveSourceModuleDir(ctx.project.projectDir!, ctx.project.projectName, moduleName);
      if (!moduleDir) throw new Error(`Module not found: '${moduleName || "(default)"}'`);

      const candidates = visibility
        ? [path.join(moduleDir, /^public$/i.test(visibility) ? "Public" : "Private", fileName)]
        : [path.join(moduleDir, "Public", fileName), path.join(moduleDir, "Private", fileName), path.join(moduleDir, fileName)];
      const found = candidates.find(c => fs.existsSync(c));
      if (!found) throw new Error(`Source file not found: ${fileName} in module '${path.basename(moduleDir)}'`);
      const content = fs.readFileSync(found, "utf-8");
      return { module: path.basename(moduleDir), path: found, bytes: content.length, content };
    },
  },
  add_module_dependency: {
    kind: "handler",
    effect: "mutate",
    description:
      "Add a module to a target module's Build.cs dependency array. Params: moduleName (the Build.cs to edit - must exist in the project), dependency (module name to add, e.g. 'UMG'), access? ('public'|'private', default 'private'). Creates the corresponding AddRange block if missing. Rebuild required afterward.",
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
          throw new Error(`Could not locate module ctor in ${buildCs} - edit manually.`);
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
    kind: "handler",
    effect: "mutate",
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
};
