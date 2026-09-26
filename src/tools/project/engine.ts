import * as fs from "node:fs";
import * as path from "node:path";
import { selectEngine } from "../../engine-root.js";
import { parseHeader } from "../../codeintel/cpp-parser.js";
import { loadEngineIndex, type EngineIndex } from "../../codeintel/engine-index.js";
import {
  verifySymbols,
  suggestBuildDeps,
  findExampleUsage,
  lintHeader,
  findBuildCs,
} from "../../codeintel/cpp-correctness.js";
import {
  classHierarchy,
  findCallees,
  findCallers,
  findReferences,
  symbolContext,
} from "../../codeintel/engine-analysis.js";
import { specBp as reflectionSpecBp } from "../specs/reflection.generated.js";
import type { ToolContext, ActionSpec } from "../../core/types.js";

/**
 * The engine tree the engine-source readers work against.
 *
 * These used to ask the .uproject's EngineAssociation and nothing else, so a
 * project whose engine is a source build beside it got "Could not resolve
 * engine install path" while `Build.bat` sat one directory up (#962). They now
 * go through the same resolver `build_project` uses, and the failure it throws
 * names every path probed instead of one env var.
 */
function requireEngineRoot(ctx: ToolContext): string {
  ctx.project.ensureLoaded();
  const engineRoot = selectEngine(ctx.project.engineLookup(), "engineRoot").engineRoot;
  if (!engineRoot) throw new Error("Could not resolve engine install path");
  return engineRoot;
}

/**
 * The engine symbol index for this project's engine.
 *
 * Cached on disk per engine, so the first call on a machine pays a scan of
 * roughly 31,000 headers and every call after it is a file read. The scan is
 * slow only because of first-touch I/O, so the actions that use this declare a
 * long timeout rather than pretending it is instant.
 */
function requireEngineIndex(ctx: ToolContext, refresh = false): {
  index: EngineIndex;
  source: string;
  cacheFile: string | null;
  buildMs?: number;
} {
  const engineRoot = requireEngineRoot(ctx);
  const loaded = loadEngineIndex(engineRoot, { refresh });
  return {
    index: loaded.index,
    source: loaded.source,
    cacheFile: loaded.cacheFile,
    buildMs: loaded.buildMs,
  };
}

/** Read a names[] parameter that also accepts a single string. */
function nameList(value: unknown, field: string): string[] {
  const list = typeof value === "string"
    ? value.split(",").map((v) => v.trim()).filter(Boolean)
    : Array.isArray(value)
      ? value.filter((v): v is string => typeof v === "string" && v.trim() !== "")
      : [];
  if (list.length === 0) throw new Error(`Missing '${field}'. Pass an array of symbol names, or a comma-separated string.`);
  if (list.length > 200) throw new Error(`'${field}' is capped at 200 names per call (got ${list.length}).`);
  return list;
}

/** The engine's source and symbol index: headers, modules, symbols, call graph, header lint. */
export const engineActions: Record<string, ActionSpec> = {
  read_engine_header: {
    kind: "handler",
    effect: "read",
    description: "Parse a .h file from the engine source tree. Params: headerPath (relative to Engine/Source, or absolute)",
    handler: async (ctx, p) => {
      const engineRoot = requireEngineRoot(ctx);
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
    kind: "handler",
    effect: "read",
    description: "Grep engine headers for a symbol. Params: symbol, maxResults?",
    handler: async (ctx, p) => {
      const engineRoot = requireEngineRoot(ctx);
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
    kind: "handler",
    effect: "read",
    description: "List modules in Engine/Source/Runtime. Params: none",
    handler: async (ctx) => {
      const engineRoot = requireEngineRoot(ctx);
      const runtimeDir = path.join(engineRoot, "Engine", "Source", "Runtime");
      if (!fs.existsSync(runtimeDir)) throw new Error(`Runtime dir not found: ${runtimeDir}`);
      const modules = fs.readdirSync(runtimeDir, { withFileTypes: true })
        .filter(e => e.isDirectory())
        .map(e => ({ name: e.name, hasBuildCs: fs.existsSync(path.join(runtimeDir, e.name, `${e.name}.Build.cs`)) }));
      return { engineRoot, moduleCount: modules.length, modules };
    },
  },
  search_engine_cpp: {
    kind: "handler",
    effect: "read",
    description: "Search engine .h/.cpp/.inl files across Runtime/Editor/Developer/Plugins. Params: query, tree? (Runtime|Editor|Developer|Plugins|all - default Runtime), subdirectory?, maxResults? (default 500)",
    handler: async (ctx, p) => {
      const engineRoot: string = requireEngineRoot(ctx);
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
  // Registered under reflection in C++; project exposes the same handlers.
  list_loaded_modules: reflectionSpecBp("read", 
    "Enumerate ALL engine+project modules with runtime load state (loaded/gameModule), not just uproject-declared ones. filter is a case-insensitive substring; loadedOnly defaults to false (#689).",
    "list_loaded_modules",
  ),
  is_module_loaded: reflectionSpecBp("read", 
    "Report whether a named module is currently loaded in the editor (#689).",
    "is_module_loaded",
  ),
  build_engine_index: {
    kind: "handler",
    effect: "mutate",
    description:
      "Build or refresh the engine symbol index that verify_symbols, lint_cpp_header and "
      + "suggest_build_deps read. Scans roughly 31,000 headers across Runtime, Editor, Developer "
      + "and the includable half of Engine/Plugins, and records for each symbol the header that "
      + "declares it, the module that owns it, its signature and any UE_DEPRECATED. The result is "
      + "cached per engine under the user directory and shared by every project on that engine, so "
      + "this is a one-time cost per engine install: expect several minutes cold (first touch of "
      + "each file goes through the virus scanner on Windows) and a few seconds warm. The other "
      + "actions build it on demand, so this is only needed to refresh after an engine upgrade or "
      + "to pay the cost deliberately. Params: refresh? (rebuild even when a valid cache exists)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const { index, source, cacheFile, buildMs } = requireEngineIndex(ctx, p.refresh === true);
      return {
        engineRoot: index.engineRoot,
        engineVersion: index.engineVersion,
        source,
        cacheFile,
        buildMs,
        builtAt: index.builtAt,
        trees: index.trees,
        headerCount: index.headerCount,
        symbolCount: index.symbolCount,
        uniqueNames: Object.keys(index.symbols).length,
      };
    },
  },
  verify_symbols: {
    kind: "handler",
    effect: "read",
    description:
      "Check that engine symbols exist BEFORE writing C++ that uses them, and get back what you "
      + "need to write it: the header to #include, the owning module for Build.cs, the exact "
      + "declaration, the base class, and any UE_DEPRECATED with its version and message. Accepts "
      + "a qualified 'UGameplayStatics::GetPlayerPawn' as well as a bare type name, and covers "
      + "plugin modules (GameplayAbilities, Niagara, PCG, EnhancedInput) as well as the engine. A "
      + "name that does not resolve comes back with close spellings; a member miss on a class that "
      + "does exist says so, which separates a misspelled method from a misspelled class. The "
      + "aggregate includes[] and modules[] are the whole edit you need to make. Builds the index "
      + "on first use, which can take several minutes on a cold filesystem. "
      + "Params: names (string[] or comma-separated string, max 200)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const names = nameList(p.names, "names");
      const { index, source } = requireEngineIndex(ctx);
      return { indexSource: source, ...verifySymbols(index, names) };
    },
  },
  suggest_build_deps: {
    kind: "handler",
    effect: "mutate",
    description:
      "Given the engine symbols a module uses, report which modules its Build.cs has to depend on "
      + "and which of those it does not list yet, plus the AddRange line to paste. Core and "
      + "CoreUObject are omitted because every module already has them. buildCsPath defaults to "
      + "the Build.cs owning modulePath, or the project's first module. Pair with "
      + "add_module_dependency, which performs the edit. "
      + "Params: names (string[] or comma-separated string), buildCsPath? (absolute), modulePath? "
      + "(a file or directory whose owning Build.cs to use)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const names = nameList(p.names, "names");
      const { index } = requireEngineIndex(ctx);
      let buildCs = (p.buildCsPath as string | undefined) ?? null;
      if (!buildCs && typeof p.modulePath === "string") {
        const start = fs.existsSync(p.modulePath) && fs.statSync(p.modulePath).isDirectory()
          ? p.modulePath
          : path.dirname(p.modulePath);
        buildCs = findBuildCs(start);
      }
      return suggestBuildDeps(index, names, buildCs);
    },
  },
  find_example_usage: {
    kind: "handler",
    effect: "read",
    description:
      "Find real call sites for an engine symbol in the engine's own .cpp files, which answers "
      + "'how is this actually used' with code that compiles. Better than a signature for anything "
      + "with a non-obvious calling convention. Searches sources rather than headers on purpose: a "
      + "header gives the declaration, which verify_symbols already returns. An engine installed "
      + "from the Epic launcher ships headers WITHOUT .cpp sources, so on those there are no engine "
      + "call sites to find; the result says so via engineSourcesAvailable and falls back to inline "
      + "code in headers and to this project's own Source tree, rather than returning an empty list "
      + "that reads as 'nothing uses this'. "
      + "Params: symbol (bare or Class::Member), limit? (default 10), trees? (Runtime|Editor|Developer, default Runtime)",
    timeoutMs: 600_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const symbol = (p.symbol as string | undefined)?.trim();
      if (!symbol) throw new Error("Missing 'symbol'");
      const engineRoot = requireEngineRoot(ctx);
      const trees = typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined);
      return findExampleUsage(engineRoot, symbol, {
        limit: (p.limit as number) ?? 10,
        trees,
        projectDir: ctx.project.projectDir,
      });
    },
  },
  class_hierarchy: {
    kind: "handler",
    effect: "read",
    description:
      "Report what a class derives from and what derives from it, which is the question behind "
      + "'what should I subclass' and 'what already does this'. Ancestors are the full chain up "
      + "to the root, nearest parent first; descendants default to the direct subclasses only, "
      + "because every transitive subclass of UObject is tens of thousands of names. Every node "
      + "carries its module, its include and whether it crosses a module boundary from the "
      + "queried class, since crossing one is what forces a Build.cs dependency; "
      + "crossModuleDependencies is that list on its own. Reads the engine symbol index and no "
      + "files, so it is fast once the index exists, and builds it on first use, which can take "
      + "several minutes on a cold filesystem. A base the index cannot resolve (a template, a "
      + "macro-generated type) stops the walk and is reported as unresolvedAncestor rather than "
      + "silently ending the chain. "
      + "Params: symbol (class or struct name, prefix optional), direction? "
      + "(ancestors|descendants|both, default both), depth? (generations of descendants, default "
      + "1), limit? (max descendants, default 100)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const symbol = (p.symbol as string | undefined)?.trim();
      if (!symbol) throw new Error("Missing 'symbol'");
      const direction = p.direction as "ancestors" | "descendants" | "both" | undefined;
      const { index, source } = requireEngineIndex(ctx);
      return {
        indexSource: source,
        engineVersion: index.engineVersion,
        ...classHierarchy(index, symbol, {
          depth: p.depth as number | undefined,
          limit: p.limit as number | undefined,
          direction,
        }),
      };
    },
  },
  find_references: {
    kind: "handler",
    effect: "read",
    description:
      "Find every line in the engine tree that names a symbol, which answers 'how is this woven "
      + "into the engine' and 'what would break if this changed'. Broader than find_callers on "
      + "purpose: a reference is a member declaration, a UPROPERTY type, a cast, a template "
      + "argument or a call, and both headers and .cpp files are searched. Comment lines and "
      + "preprocessor lines are skipped, since neither is a use. Each site reports its file, "
      + "line, text and owning module. An engine installed from the Epic launcher ships headers "
      + "WITHOUT .cpp sources, so engineSourcesAvailable says whether implementation files could "
      + "be searched at all and the note says what was searched instead. Scans files rather than "
      + "the index, so a rare name on a cold filesystem is slow. "
      + "Params: symbol (bare or Class::Member), limit? (max sites, default 40), trees? "
      + "(Runtime|Editor|Developer|Plugins|all, default Runtime), includeProject? (also search "
      + "this project's Source and Plugins, default true)",
    timeoutMs: 600_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const symbol = (p.symbol as string | undefined)?.trim();
      if (!symbol) throw new Error("Missing 'symbol'");
      const engineRoot = requireEngineRoot(ctx);
      return findReferences(engineRoot, symbol, {
        limit: p.limit as number | undefined,
        trees: typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined),
        projectDir: ctx.project.projectDir,
        includeProject: p.includeProject !== false,
      });
    },
  },
  find_callers: {
    kind: "handler",
    effect: "read",
    description:
      "Find who calls a function, and from which enclosing function, which is how to see the "
      + "conventions around a call before writing one: what is checked first, what is passed, "
      + "what is done with the result. Searches .cpp bodies first, since a mention in a header is "
      + "usually a declaration rather than a call, and excludes the function's own definition. "
      + "Each site reports file, line, text, module and, for a site in a .cpp, the Class::Method "
      + "it sits inside. An engine installed from the Epic launcher ships headers WITHOUT .cpp "
      + "sources, so on those there are no engine call sites to find: engineSourcesAvailable "
      + "reports that and the search falls back to inline code in headers and to this project's "
      + "own Source tree, rather than returning an empty list that reads as 'nothing calls this'. "
      + "Params: symbol (bare or Class::Method), limit? (max sites, default 25), trees? "
      + "(Runtime|Editor|Developer|Plugins|all, default Runtime), includeProject? (also search "
      + "this project's Source and Plugins, default true)",
    timeoutMs: 600_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const symbol = (p.symbol as string | undefined)?.trim();
      if (!symbol) throw new Error("Missing 'symbol'");
      const engineRoot = requireEngineRoot(ctx);
      return findCallers(engineRoot, symbol, {
        limit: p.limit as number | undefined,
        trees: typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined),
        projectDir: ctx.project.projectDir,
        includeProject: p.includeProject !== false,
      });
    },
  },
  find_callees: {
    kind: "handler",
    effect: "read",
    description:
      "Report what a function calls, by reading its body and looking every called name back up "
      + "in the engine index. Answers 'what does doing this properly actually involve': the "
      + "result carries each callee's module and include, and modules[] is the Build.cs cost of "
      + "writing code that does the same thing. The body is found via the index, which keeps the "
      + "search to the owning class's module rather than the whole tree, and the definition it "
      + "read is reported with its file and line range. An engine installed from the Epic "
      + "launcher ships headers WITHOUT .cpp sources, so only functions whose body is inline in a "
      + "header can be read there; engineSourcesAvailable and the note say so instead of "
      + "returning an empty list. Builds the index on first use. "
      + "Params: symbol (Class::Method, or a bare exported free function), limit? (max callees, "
      + "default 100), trees? (which trees to test for sources, default Runtime)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const symbol = (p.symbol as string | undefined)?.trim();
      if (!symbol) throw new Error("Missing 'symbol'");
      const { index, source } = requireEngineIndex(ctx);
      return {
        indexSource: source,
        ...findCallees(index, symbol, {
          projectDir: ctx.project.projectDir,
          limit: p.limit as number | undefined,
          trees: typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined),
        }),
      };
    },
  },
  symbol_context: {
    kind: "handler",
    effect: "read",
    description:
      "Return the lines of engine source around a declaration, so the API surrounding a symbol "
      + "can be read without opening the file: the sibling overloads, the UPROPERTY above it, the "
      + "comment saying which of three similar methods to call. verify_symbols returns the "
      + "declaration line alone, which is the signature and nothing else; this is that line in "
      + "its neighbourhood. Accepts Class::Member as well as a bare type and resolves both "
      + "exactly as verify_symbols does. When the declaration opens a body that closes inside the "
      + "window the result ends at the closing brace instead of mid-type, and reports "
      + "bodyEndLine. Builds the index on first use, which can take several minutes on a cold "
      + "filesystem. "
      + "Params: symbol (bare or Class::Member), contextBefore? (lines before the declaration, "
      + "default 8), contextAfter? (lines after, default 40)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const symbol = (p.symbol as string | undefined)?.trim();
      if (!symbol) throw new Error("Missing 'symbol'");
      const { index, source } = requireEngineIndex(ctx);
      return {
        indexSource: source,
        engineRoot: index.engineRoot,
        ...symbolContext(index, symbol, {
          before: p.contextBefore as number | undefined,
          after: p.contextAfter as number | undefined,
        }),
      };
    },
  },
  lint_cpp_header: {
    kind: "handler",
    effect: "read",
    description:
      "Check a header you just wrote against the engine it has to build against, and report what "
      + "the compiler would before the compiler runs. Covers the structural mistakes that produce "
      + "baffling Unreal build errors (a reflected type with no .generated.h include, a .generated.h "
      + "that is not last, a UCLASS or USTRUCT with no GENERATED_BODY, no #pragma once) and the "
      + "engine-facing ones (a symbol that does not exist, one used without its include, one whose "
      + "module is missing from Build.cs, one the engine deprecated). A forward declaration counts "
      + "as satisfying an include, since in a header it usually is. Run this after write_cpp_file "
      + "and before build_project. "
      + "Params: path (absolute, or relative to the project Source/), buildCsPath? (defaults to the owning module's)",
    timeoutMs: 1_800_000,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      ctx.project.ensureLoaded();
      const raw = p.path as string;
      if (!raw) throw new Error("Missing 'path'");
      const resolved = path.isAbsolute(raw)
        ? raw
        : path.join(ctx.project.projectDir ?? "", "Source", raw);
      if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
      const { index, source } = requireEngineIndex(ctx);
      const result = lintHeader(index, resolved, { buildCsPath: p.buildCsPath as string | undefined });
      return {
        indexSource: source,
        ...result,
        ok: result.findings.filter((f) => f.severity === "error").length === 0,
        errorCount: result.findings.filter((f) => f.severity === "error").length,
        warningCount: result.findings.filter((f) => f.severity === "warning").length,
      };
    },
  },
};
