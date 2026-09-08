/**
 * Reading the engine the way a maintainer reads it.
 *
 * `engine-index.ts` answers "does this symbol exist and where is it declared",
 * and `cpp-correctness.ts` turns that into includes and Build.cs lines. Both
 * answer questions about ONE symbol in isolation, which is enough to make a
 * file compile and not enough to make it idiomatic.
 *
 * The questions that remain are relational, and they are the ones an agent
 * asks when it is trying to match an engine convention rather than guess at
 * one:
 *
 *   - what does this derive from, and what derives from it (class_hierarchy)
 *   - where is this named at all (find_references)
 *   - who calls it, and what does it call (find_callers / find_callees)
 *   - what does the code around the declaration look like (symbol_context)
 *
 * Three of those are answered from the index that is already built. The index
 * records a `parent` per symbol, so ancestors are a walk up the chain and
 * descendants are the same table inverted, which costs one pass over it and is
 * cached per index object. Only the call questions need the files themselves,
 * because a call site is in a body and the index holds declarations.
 *
 * ## The launcher-install problem
 *
 * An engine installed from the Epic launcher ships headers and NO .cpp at all:
 * there are zero sources under `Engine/Source/Runtime` on such an install. A
 * caller-search that simply returned an empty list there would read as
 * "nothing calls this", which is the opposite of what happened, and the two
 * call for opposite next steps.
 *
 * So every file-reading function here reports `engineSourcesAvailable`, and
 * when it is false it degrades the way `findExampleUsage` does: to the inline
 * code Unreal keeps in headers, and to the project's own Source tree, with a
 * note saying which happened. The answer is narrower, and it says so.
 *
 * Like the index, this is a regex pass and not a C++ parser. It recognises
 * what a line can be recognised from. A `found: false` means "not found by
 * this method".
 */
import * as fs from "node:fs";
import * as path from "node:path";
import {
  lookupMember,
  lookupSymbol,
  splitQualified,
  type EngineIndex,
  type EngineSymbol,
} from "./engine-index.js";

/* ── shared shapes ─────────────────────────────────────────────────── */

/** One line of code, located. */
export interface SourceSite {
  /** Relative to the engine root, or to the project directory for `project`. */
  file: string;
  line: number;
  text: string;
  /** `source` is an engine .cpp, `header` an engine .h, `project` the user's own code. */
  kind: "source" | "header" | "project";
  /** The owning module, for an engine file. Crossing one is a Build.cs edit. */
  module?: string;
  /** The function the line sits in, when it could be recognised. */
  caller?: string;
}

/** What a file-reading answer has to say about the tree it read. */
export interface TreeStatus {
  /**
   * Whether the engine tree contains .cpp files at all. False on a launcher
   * install, where an empty result means "this install cannot answer that"
   * rather than "nothing uses this".
   */
  engineSourcesAvailable: boolean;
  /** Set when the search stopped at its limit rather than at the end. */
  truncated: boolean;
  filesScanned: number;
  note?: string;
}

const SKIP_DIRS = new Set([
  "Intermediate", "Binaries", "ThirdParty", "Saved", "node_modules", ".git", "DerivedDataCache",
]);

/** C++ keywords that take a parenthesis and are not calls. */
const NOT_CALLS = new Set([
  "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "throw",
  "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast", "decltype",
  "new", "delete", "case", "do", "else", "explicit", "operator", "noexcept",
  "and", "or", "not", "assert", "typeid", "constexpr",
]);

/**
 * The directory of the module owning a header.
 *
 * The engine and its plugins are laid out differently, and both have to give a
 * single directory that holds the module's Public, Private and Classes:
 *
 *   Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h
 *     -> Engine/Source/Runtime/Engine
 *   Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/X.h
 *     -> Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities
 *
 * It is what makes a definition search cheap: a member of `UAbilitySystem
 * Component` is defined in its own module or nowhere, so one module directory
 * is read instead of the whole tree.
 */
export function moduleDirFor(relHeader: string): string {
  const parts = relHeader.split("/");
  if (parts[0] === "Engine" && parts[1] === "Plugins") {
    const at = parts.lastIndexOf("Source");
    if (at >= 0 && at + 1 < parts.length) return parts.slice(0, at + 2).join("/");
    return parts.slice(0, -1).join("/");
  }
  // Engine/Source/<Tree>/<Module>/...
  return parts.slice(0, 4).join("/");
}

/**
 * The engine directories named by a `trees` list.
 *
 * `Plugins` is `Engine/Plugins`, a sibling of `Engine/Source` rather than a
 * child of it, so it cannot be joined the way the other three are. `all` is
 * every tree that exists on this install.
 */
export function treeRoots(engineRoot: string, trees: readonly string[]): string[] {
  const wanted = trees.length === 1 && trees[0] === "all"
    ? ["Runtime", "Editor", "Developer", "Plugins"]
    : trees;
  const out: string[] = [];
  for (const tree of wanted) {
    const dir = tree === "Plugins"
      ? path.join(engineRoot, "Engine", "Plugins")
      : path.join(engineRoot, "Engine", "Source", tree);
    if (fs.existsSync(dir)) out.push(dir);
  }
  return out;
}

/**
 * Does this engine ship sources?
 *
 * Answered by walking directories until the first .cpp, which never reads a
 * file. A source build answers in milliseconds because the first module has
 * one; a launcher install pays a directory walk to prove the absence, which is
 * the honest cost of the honest answer.
 */
export function engineSourcesAvailable(engineRoot: string, trees: readonly string[] = ["Runtime"]): boolean {
  const seek = (dir: string): boolean => {
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return false;
    }
    for (const entry of entries) {
      if (entry.isDirectory()) {
        if (SKIP_DIRS.has(entry.name)) continue;
        if (seek(path.join(dir, entry.name))) return true;
        continue;
      }
      if (entry.name.endsWith(".cpp")) return true;
    }
    return false;
  };
  return treeRoots(engineRoot, trees).some(seek);
}

/** Strip a namespace qualification and any template arguments off a type name. */
export function baseTypeName(name: string): string {
  const withoutArgs = name.replace(/<[\s\S]*$/, "").trim();
  const cut = withoutArgs.lastIndexOf("::");
  return (cut >= 0 ? withoutArgs.slice(cut + 2) : withoutArgs).trim();
}

/* ── class hierarchy ───────────────────────────────────────────────── */

export interface HierarchyNode {
  name: string;
  kind: EngineSymbol["kind"];
  module: string;
  include: string;
  header: string;
  line: number;
  exported: boolean;
  parent?: string;
  deprecated?: EngineSymbol["deprecated"];
  /** Distance from the queried symbol: 1 is a direct parent or child. */
  depth: number;
  /** True when this node's module differs from the queried symbol's, which is
   *  what forces a Build.cs dependency to reach it. */
  crossesModule: boolean;
}

export interface HierarchyResult {
  symbol: string;
  found: boolean;
  self?: HierarchyNode;
  /** Nearest parent first, up to the last one the index could resolve. */
  ancestors: HierarchyNode[];
  /**
   * The first base name the walk could not resolve, when there was one. It is
   * not proof the type does not exist: a base behind a template or a macro is
   * out of reach of a regex index.
   */
  unresolvedAncestor?: string;
  descendants: HierarchyNode[];
  directDescendantCount: number;
  /** Every module named anywhere in the answer, so the Build.cs cost is visible. */
  modules: string[];
  /** Modules other than the queried symbol's own. */
  crossModuleDependencies: string[];
  truncated: boolean;
  note?: string;
}

/**
 * Children by parent name, computed once per index object.
 *
 * The index is a name-to-declarations table, so descendants are that table
 * inverted. Inverting it is one pass over every symbol, which is fast enough
 * to do per call and wasteful to do per call, so it is memoised against the
 * index object itself: the cache dies with the index, and a refreshed index is
 * a different object, so an upgrade cannot serve a stale tree.
 */
const childCache = new WeakMap<EngineIndex, Map<string, EngineSymbol[]>>();

export function childIndex(index: EngineIndex): Map<string, EngineSymbol[]> {
  const cached = childCache.get(index);
  if (cached) return cached;
  const children = new Map<string, EngineSymbol[]>();
  for (const declarations of Object.values(index.symbols)) {
    for (const symbol of declarations) {
      if (!symbol.parent) continue;
      const base = baseTypeName(symbol.parent);
      if (!base) continue;
      const list = children.get(base);
      if (list) list.push(symbol);
      else children.set(base, [symbol]);
    }
  }
  childCache.set(index, children);
  return children;
}

function nodeOf(symbol: EngineSymbol, depth: number, ownModule: string): HierarchyNode {
  return {
    name: symbol.name,
    kind: symbol.kind,
    module: symbol.module,
    include: symbol.include,
    header: symbol.header,
    line: symbol.line,
    exported: symbol.exported,
    parent: symbol.parent,
    deprecated: symbol.deprecated,
    depth,
    crossesModule: symbol.module !== ownModule,
  };
}

/**
 * What a class derives from and what derives from it.
 *
 * Ancestors are a walk up `parent`, which the index already records, resolving
 * each name through the same ranking a lookup uses so the answer is the header
 * a caller would include. The walk stops at the first name that does not
 * resolve and says which one it was, because "the chain ends here" and "I
 * could not follow it further" are different facts.
 *
 * Descendants default to one generation. Asking for every transitive subclass
 * of `UObject` is a list of tens of thousands of names that no caller can act
 * on, so depth is opt-in and the result says when it was cut short.
 */
export function classHierarchy(
  index: EngineIndex,
  name: string,
  options: { depth?: number; limit?: number; direction?: "ancestors" | "descendants" | "both" } = {},
): HierarchyResult {
  const wanted = name.trim();
  const depth = Math.max(1, options.depth ?? 1);
  const limit = Math.max(1, options.limit ?? 100);
  const direction = options.direction ?? "both";

  const hits = lookupSymbol(index, baseTypeName(wanted));
  if (hits.length === 0) {
    return {
      symbol: wanted,
      found: false,
      ancestors: [],
      descendants: [],
      directDescendantCount: 0,
      modules: [],
      crossModuleDependencies: [],
      truncated: false,
      note:
        `No declaration of '${wanted}' is in the index. It may be a template, a macro-generated `
        + `type, or in a tree the index does not cover. verify_symbols reports close spellings.`,
    };
  }

  const self = hits[0];
  const ownModule = self.module;
  const ancestors: HierarchyNode[] = [];
  let unresolvedAncestor: string | undefined;

  if (direction !== "descendants") {
    const seen = new Set<string>([self.name]);
    let current: EngineSymbol | undefined = self;
    for (let step = 1; current?.parent; step++) {
      const parentName = baseTypeName(current.parent);
      if (!parentName || seen.has(parentName)) break;
      seen.add(parentName);
      const resolved: EngineSymbol | undefined = lookupSymbol(index, parentName)[0];
      if (!resolved) {
        unresolvedAncestor = parentName;
        break;
      }
      ancestors.push(nodeOf(resolved, step, ownModule));
      current = resolved;
      // A chain longer than this is a cycle the index recorded, not a real
      // inheritance tree.
      if (step > 64) break;
    }
  }

  const descendants: HierarchyNode[] = [];
  let directDescendantCount = 0;
  let truncated = false;

  if (direction !== "ancestors") {
    const children = childIndex(index);
    const seen = new Set<string>([self.name]);
    let frontier = [self.name];
    for (let level = 1; level <= depth && frontier.length > 0; level++) {
      const next: string[] = [];
      for (const parentName of frontier) {
        for (const child of children.get(parentName) ?? []) {
          if (level === 1) directDescendantCount++;
          if (seen.has(child.name)) continue;
          seen.add(child.name);
          next.push(child.name);
          if (descendants.length >= limit) {
            truncated = true;
            continue;
          }
          descendants.push(nodeOf(child, level, ownModule));
        }
      }
      frontier = next;
    }
  }

  const modules = new Set<string>([ownModule]);
  for (const node of [...ancestors, ...descendants]) modules.add(node.module);

  return {
    symbol: wanted,
    found: true,
    self: nodeOf(self, 0, ownModule),
    ancestors,
    unresolvedAncestor,
    descendants,
    directDescendantCount,
    modules: [...modules].sort(),
    crossModuleDependencies: [...modules].filter((m) => m !== ownModule).sort(),
    truncated,
    note: truncated
      ? `More than ${limit} descendants were found; raise 'limit' or narrow the query.`
      : undefined,
  };
}

/* ── file scanning ─────────────────────────────────────────────────── */

interface ScanTarget {
  dir: string;
  /** What the reported paths are relative to. */
  root: string;
  kind: "engine" | "project";
}

interface FileVisit {
  /** Path relative to the target's root, forward slashes. */
  rel: string;
  absolute: string;
  isCpp: boolean;
  lines: string[];
  kind: "engine" | "project";
}

/**
 * Read every source file under some directories and hand each one to a
 * visitor, stopping as soon as the visitor says it has enough.
 *
 * `needle` is a plain substring pre-filter: a file that does not contain the
 * text at all is never split into lines. That is the difference between a
 * search that takes seconds and one that takes minutes, because on any tree
 * most files do not mention the symbol.
 */
function scanTargets(
  targets: ScanTarget[],
  needle: string,
  wantHeaders: boolean,
  wantSources: boolean,
  visit: (file: FileVisit) => boolean,
): { filesScanned: number; stopped: boolean } {
  let filesScanned = 0;
  let stopped = false;

  const walk = (dir: string, target: ScanTarget): void => {
    if (stopped) return;
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      if (stopped) return;
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (SKIP_DIRS.has(entry.name)) continue;
        walk(full, target);
        continue;
      }
      const isCpp = entry.name.endsWith(".cpp");
      const isHeader = entry.name.endsWith(".h") || entry.name.endsWith(".inl");
      if (isCpp ? !wantSources : isHeader ? !wantHeaders : true) continue;

      let source: string;
      try {
        source = fs.readFileSync(full, "utf-8");
      } catch {
        continue;
      }
      filesScanned++;
      if (!source.includes(needle)) continue;
      const rel = path.relative(target.root, full).replace(/\\/g, "/");
      if (!visit({ rel, absolute: full, isCpp, lines: source.split(/\r?\n/), kind: target.kind })) {
        stopped = true;
        return;
      }
    }
  };

  for (const target of targets) walk(target.dir, target);
  return { filesScanned, stopped };
}

/** Targets for an engine search, plus the project's own tree when asked. */
function buildTargets(
  engineRoot: string,
  trees: readonly string[],
  projectDir: string | null | undefined,
  includeProject: boolean,
): ScanTarget[] {
  const targets: ScanTarget[] = treeRoots(engineRoot, trees).map((dir) => ({
    dir,
    root: engineRoot,
    kind: "engine" as const,
  }));
  if (includeProject && projectDir) {
    const source = path.join(projectDir, "Source");
    if (fs.existsSync(source)) targets.push({ dir: source, root: projectDir, kind: "project" });
    const plugins = path.join(projectDir, "Plugins");
    if (fs.existsSync(plugins)) targets.push({ dir: plugins, root: projectDir, kind: "project" });
  }
  return targets;
}

/** The module a scanned file belongs to, for an engine path. */
function moduleOfFile(rel: string, kind: "engine" | "project"): string | undefined {
  if (kind === "project") return undefined;
  const parts = rel.split("/");
  if (parts[0] === "Engine" && parts[1] === "Plugins") {
    const at = parts.lastIndexOf("Source");
    return at >= 0 && at + 1 < parts.length ? parts[at + 1] : undefined;
  }
  return parts[3];
}

const COMMENT_LINE = /^\s*(?:\/\/|\/\*|\*|#)/;

/* ── references ────────────────────────────────────────────────────── */

export interface ReferenceResult extends TreeStatus {
  symbol: string;
  siteCount: number;
  /** Distinct files among the reported sites. */
  fileCount: number;
  sites: SourceSite[];
  /** Every module the reported sites live in. */
  modules: string[];
}

/**
 * Where a symbol is named across the engine tree and, optionally, the project.
 *
 * Deliberately broader than `find_callers`: a reference is a member
 * declaration, a UPROPERTY type, a cast, a template argument, or a call. That
 * is the question "what would break if this changed", which is what an agent
 * is really asking when it wants to see how a type is woven into the engine.
 *
 * Both headers and sources are searched, because on any install a great deal
 * of the interesting usage is in headers, and a launcher install has nothing
 * else. `#include` lines and comments are skipped: neither is a use of the
 * symbol, and both would otherwise dominate the result for a common type.
 */
export function findReferences(
  engineRoot: string,
  symbol: string,
  options: {
    limit?: number;
    trees?: readonly string[];
    projectDir?: string | null;
    includeProject?: boolean;
  } = {},
): ReferenceResult {
  const limit = Math.max(1, options.limit ?? 40);
  const trees = options.trees?.length ? options.trees : ["Runtime"];
  const needle = baseTypeName(symbol.includes("::") ? symbol.split("::").pop()! : symbol);
  const escaped = needle.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const named = new RegExp(`\\b${escaped}\\b`);

  const targets = buildTargets(engineRoot, trees, options.projectDir, options.includeProject !== false);
  const sites: SourceSite[] = [];

  const { filesScanned, stopped } = scanTargets(targets, needle, true, true, (file) => {
    const module = moduleOfFile(file.rel, file.kind);
    for (let i = 0; i < file.lines.length; i++) {
      const text = file.lines[i].trim();
      if (!named.test(text)) continue;
      if (COMMENT_LINE.test(text)) continue;
      sites.push({
        file: file.rel,
        line: i + 1,
        text: text.slice(0, 240),
        kind: file.kind === "project" ? "project" : file.isCpp ? "source" : "header",
        module,
      });
      if (sites.length >= limit) return false;
    }
    return true;
  });

  const hasSources = engineSourcesAvailable(engineRoot, trees);
  const modules = [...new Set(sites.map((s) => s.module).filter((m): m is string => Boolean(m)))].sort();

  return {
    symbol,
    siteCount: sites.length,
    fileCount: new Set(sites.map((s) => s.file)).size,
    sites,
    modules,
    engineSourcesAvailable: hasSources,
    truncated: stopped,
    filesScanned,
    note: hasSources
      ? stopped
        ? `Stopped at the ${limit}-site limit; raise 'limit' for more.`
        : undefined
      : "This engine install ships headers without .cpp sources, which is normal for a launcher "
        + "install, so references from engine implementation files cannot exist here. What is "
        + "reported comes from headers and from this project's own sources. A source build of the "
        + "engine would answer this fully.",
  };
}

/* ── callers ───────────────────────────────────────────────────────── */

export interface CallerResult extends TreeStatus {
  symbol: string;
  siteCount: number;
  sites: SourceSite[];
  /** Unique enclosing functions, when they could be recognised. */
  callers: string[];
  modules: string[];
}

/**
 * The enclosing function of a line, tracked while walking a file.
 *
 * A definition in a .cpp starts at column zero and carries its class, which is
 * what makes it recognisable without parsing: `void AActor::BeginPlay()`. An
 * inline body in a header does not, so the enclosing name there is left
 * undefined rather than guessed at. That costs a field on some sites and never
 * attributes a call to the wrong function.
 */
const DEFINITION_START =
  /^[A-Za-z_][\w\s:*&<>,~]*?\b([A-Za-z_]\w*)::([~A-Za-z_]\w*)\s*\([^;]*$/;

/** A line that declares rather than calls. */
const DECLARATION_LINE =
  /^\s*(?:class|struct|enum|template|friend|typedef|using|UFUNCTION|UPROPERTY|UCLASS|USTRUCT|DECLARE_)/;

/** Words that can stand immediately before a call. */
const CALL_PREFIX_WORDS: ReadonlySet<string> = new Set([
  "return", "if", "while", "for", "switch", "case", "else", "do", "new", "delete",
  "throw", "co_await", "co_return", "and", "or", "not",
]);

/**
 * Is the name at `at` being called, or declared?
 *
 * Both read as `Name(`, and the difference is entirely in what comes before.
 * Nothing, an operator, a bracket or a `.`/`->`/`::` means a call.
 * An identifier means a return type, so it is a declaration: `virtual void
 * BeginPlay();` is not a call to BeginPlay, and counting it as one made every
 * header fallback report the declaration it was looking past.
 *
 * The two exceptions are the reason this is a function rather than a regex. A
 * trailing `*` or `&` belongs to a return type (`FVector* GetActor()`), and a
 * control-flow keyword is a word that legitimately precedes a call
 * (`return BeginPlay();`).
 */
export function looksLikeCall(text: string, at: number): boolean {
  const prefix = text.slice(0, at).trimEnd();
  if (prefix === "") return true;
  if (/(?:->|\.|::)$/.test(prefix)) return true;
  if (/[*&]$/.test(prefix)) return false;
  if (/[;{}(),=!|+\-/<>?:[\]%^~]$/.test(prefix)) return true;
  const word = /([A-Za-z_]\w*)$/.exec(prefix)?.[1];
  return word ? CALL_PREFIX_WORDS.has(word) : false;
}

/**
 * Who calls a function.
 *
 * Prefers .cpp files, because a call in a body is a caller and a mention in a
 * header is usually a declaration. On an install with no sources it falls back
 * to headers, where Unreal keeps a great deal of inline implementation, and to
 * the project's own tree, and says that is what it did.
 *
 * A line is a call when the name is followed by an open parenthesis and the
 * line is not itself a declaration of it. The definition of the function being
 * asked about is excluded too: it is not one of its own callers.
 */
export function findCallers(
  engineRoot: string,
  symbol: string,
  options: {
    limit?: number;
    trees?: readonly string[];
    projectDir?: string | null;
    includeProject?: boolean;
  } = {},
): CallerResult {
  const limit = Math.max(1, options.limit ?? 25);
  const trees = options.trees?.length ? options.trees : ["Runtime"];
  const needle = splitQualified(symbol.trim()).member;
  const escaped = needle.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const call = new RegExp(`\\b${escaped}\\s*\\(`);
  // `void UFoo::Bar(` is the definition of Bar, not a call to it. What
  // separates it from `Super::Bar();`, which IS a call, is that a definition
  // opens a body instead of ending the statement.
  const definitionOf = new RegExp(`\\b[A-Za-z_]\\w*::${escaped}\\s*\\(`);

  const hasSources = engineSourcesAvailable(engineRoot, trees);
  const targets = buildTargets(engineRoot, trees, options.projectDir, options.includeProject !== false);
  const sites: SourceSite[] = [];

  const collect = (wantHeaders: boolean, wantSources: boolean): { filesScanned: number; stopped: boolean } =>
    scanTargets(targets, needle, wantHeaders, wantSources, (file) => {
      const module = moduleOfFile(file.rel, file.kind);
      let enclosing: string | undefined;
      for (let i = 0; i < file.lines.length; i++) {
        const raw = file.lines[i];
        const start = DEFINITION_START.exec(raw);
        if (start) enclosing = `${start[1]}::${start[2]}`;
        const text = raw.trim();
        const found = call.exec(text);
        if (!found) continue;
        if (COMMENT_LINE.test(text)) continue;
        if (DECLARATION_LINE.test(text)) continue;
        // The definition of the function is not one of its own callers.
        if (definitionOf.test(text) && !/;\s*$/.test(text)) continue;
        // A declaration is the same shape as a call with a return type in
        // front of it, which is what tells them apart.
        if (!looksLikeCall(text, found.index)) continue;
        sites.push({
          file: file.rel,
          line: i + 1,
          text: text.slice(0, 240),
          kind: file.kind === "project" ? "project" : file.isCpp ? "source" : "header",
          module,
          caller: file.isCpp ? enclosing : undefined,
        });
        if (sites.length >= limit) return false;
      }
      return true;
    });

  // Pass one: sources, which is where a caller lives.
  let scan = collect(false, true);
  let note: string | undefined;

  if (!hasSources) {
    note =
      "This engine install ships headers without .cpp sources, which is normal for a launcher "
      + "install, so no engine call sites exist to find. What is reported comes from inline code "
      + "in headers and from this project's own sources, and the enclosing function is only "
      + "reported for sites in a .cpp. A source build of the engine would answer this fully.";
    if (sites.length < limit) {
      const second = collect(true, false);
      scan = { filesScanned: scan.filesScanned + second.filesScanned, stopped: second.stopped };
    }
  }

  return {
    symbol,
    siteCount: sites.length,
    sites,
    callers: [...new Set(sites.map((s) => s.caller).filter((c): c is string => Boolean(c)))].sort(),
    modules: [...new Set(sites.map((s) => s.module).filter((m): m is string => Boolean(m)))].sort(),
    engineSourcesAvailable: hasSources,
    truncated: scan.stopped,
    filesScanned: scan.filesScanned,
    note: note ?? (scan.stopped ? `Stopped at the ${limit}-site limit; raise 'limit' for more.` : undefined),
  };
}

/* ── callees ───────────────────────────────────────────────────────── */

export interface Callee {
  name: string;
  /** How many times the body calls it. */
  count: number;
  /** `member` is `->Name()` or `.Name()`, `scoped` is `Type::Name()`, `free` is neither. */
  call: "member" | "scoped" | "free";
  /** Where the callee is declared, when the index knows the name. */
  module?: string;
  include?: string;
  kind?: EngineSymbol["kind"];
}

export interface CalleeResult extends TreeStatus {
  symbol: string;
  found: boolean;
  definition?: {
    file: string;
    line: number;
    endLine: number;
    kind: "source" | "header" | "project";
    bodyLines: number;
  };
  callees: Callee[];
  calleeCount: number;
  /** Modules the resolvable callees live in, which is the Build.cs cost of
   *  writing code that does the same thing. */
  modules: string[];
}

/**
 * The body of a function, from the line its definition starts on.
 *
 * Brace matching from the first `{`, ignoring braces inside a string, a
 * character literal or a line comment. A block comment holding an unbalanced
 * brace would defeat it, which has not happened in practice and would produce
 * a body that runs long rather than a wrong answer.
 */
export function extractBody(lines: string[], startLine: number): { body: string[]; endLine: number } | null {
  let depth = 0;
  let opened = false;
  const body: string[] = [];
  for (let i = startLine - 1; i < lines.length; i++) {
    const line = lines[i];
    let inString = false;
    let inChar = false;
    for (let c = 0; c < line.length; c++) {
      const ch = line[c];
      if (inString) {
        if (ch === "\\") c++;
        else if (ch === '"') inString = false;
        continue;
      }
      if (inChar) {
        if (ch === "\\") c++;
        else if (ch === "'") inChar = false;
        continue;
      }
      if (ch === '"') { inString = true; continue; }
      if (ch === "'") { inChar = true; continue; }
      if (ch === "/" && line[c + 1] === "/") break;
      if (ch === "{") { depth++; opened = true; continue; }
      if (ch === "}") {
        depth--;
        if (opened && depth === 0) {
          body.push(line);
          return { body, endLine: i + 1 };
        }
      }
    }
    body.push(line);
    // A declaration ends without ever opening a body.
    if (!opened && /;\s*$/.test(line)) return null;
    if (i - startLine > 4000) return null;
  }
  return null;
}

/** Names a body calls, with how each call was written. */
export function callsIn(body: string, ownName: string): Map<string, { count: number; call: Callee["call"] }> {
  const stripped = body
    .replace(/\/\*[\s\S]*?\*\//g, " ")
    .replace(/\/\/[^\n]*/g, " ")
    .replace(/"(?:[^"\\]|\\.)*"/g, '""');
  const found = new Map<string, { count: number; call: Callee["call"] }>();
  const note = (name: string, call: Callee["call"]): void => {
    if (NOT_CALLS.has(name) || name === ownName) return;
    const existing = found.get(name);
    if (existing) existing.count++;
    else found.set(name, { count: 1, call });
  };
  for (const m of stripped.matchAll(/(->|\.|::)?\s*\b([A-Za-z_]\w*)\s*\(/g)) {
    const via = m[1];
    note(m[2], via === "::" ? "scoped" : via ? "member" : "free");
  }
  return found;
}

/**
 * What a function calls.
 *
 * The body has to be found before it can be read, and finding it is what the
 * index makes cheap: a member of a class is defined in that class's own module
 * or nowhere, so the search is one module directory rather than the tree. On
 * an install with no sources the same search finds the inline body in the
 * header instead, which is where Unreal keeps a large share of its small
 * functions, and reports that it did.
 *
 * Each callee is looked back up in the index, so the answer carries the module
 * and include of everything the function reaches. That is the Build.cs cost of
 * writing code that does the same thing, which is the reason to ask.
 */
export function findCallees(
  index: EngineIndex,
  symbol: string,
  options: { projectDir?: string | null; limit?: number; trees?: readonly string[] } = {},
): CalleeResult {
  const limit = Math.max(1, options.limit ?? 100);
  const trimmed = symbol.trim();
  const { className, member } = splitQualified(trimmed);
  const hasSources = engineSourcesAvailable(index.engineRoot, options.trees ?? ["Runtime"]);

  const empty = (note: string): CalleeResult => ({
    symbol: trimmed,
    found: false,
    callees: [],
    calleeCount: 0,
    modules: [],
    engineSourcesAvailable: hasSources,
    truncated: false,
    filesScanned: 0,
    note,
  });

  // Which module owns the thing being asked about. For a member it is the
  // class's module; for a free function it is the function's own.
  const owner = lookupSymbol(index, className ?? member)[0];
  if (!owner) {
    return empty(
      `No declaration of '${className ?? member}' is in the index, so there is no module to look `
      + `for a body in. verify_symbols reports close spellings.`,
    );
  }

  const searchDirs: ScanTarget[] = [
    { dir: path.join(index.engineRoot, moduleDirFor(owner.header)), root: index.engineRoot, kind: "engine" },
  ];
  if (options.projectDir) {
    const source = path.join(options.projectDir, "Source");
    if (fs.existsSync(source)) searchDirs.push({ dir: source, root: options.projectDir, kind: "project" });
  }

  const escaped = member.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  // A definition is the name preceded by its class, or, for an inline body in
  // a header, the name followed by an argument list and an opening brace.
  const qualified = new RegExp(`\\b(?:[A-Za-z_]\\w*::)?${escaped}\\s*\\(`);
  const definitionQualified = new RegExp(`\\b[A-Za-z_]\\w*::${escaped}\\s*\\(`);

  let definition: CalleeResult["definition"];
  let body: string[] = [];

  const takeDefinition = (file: FileVisit, requireQualifier: boolean): boolean => {
    for (let i = 0; i < file.lines.length; i++) {
      const text = file.lines[i];
      const trimmedLine = text.trim();
      if (COMMENT_LINE.test(trimmedLine)) continue;
      const matches = requireQualifier ? definitionQualified.test(text) : qualified.test(text);
      if (!matches) continue;
      if (DECLARATION_LINE.test(trimmedLine)) continue;
      const extracted = extractBody(file.lines, i + 1);
      if (!extracted || extracted.body.length === 0) continue;
      definition = {
        file: file.rel,
        line: i + 1,
        endLine: extracted.endLine,
        kind: file.kind === "project" ? "project" : file.isCpp ? "source" : "header",
        bodyLines: extracted.body.length,
      };
      body = extracted.body;
      return false; // stop the scan, the body is found
    }
    return true;
  };

  // Pass one: sources, where a real definition lives, and only a qualified
  // definition counts so a call inside another function is not mistaken for one.
  let scan = scanTargets(searchDirs, member, false, true, (f) => takeDefinition(f, true));
  if (!definition) {
    // Pass two: headers, for an inline body. A qualifier is not required here,
    // because an inline member is written without one.
    const second = scanTargets(searchDirs, member, true, false, (f) => takeDefinition(f, false));
    scan = { filesScanned: scan.filesScanned + second.filesScanned, stopped: second.stopped };
  }

  if (!definition) {
    return {
      ...empty(
        hasSources
          ? `No body for '${trimmed}' was found in ${moduleDirFor(owner.header)}. It may be `
            + `pure virtual, defined in another module, or generated by a macro.`
          : "This engine install ships headers without .cpp sources, which is normal for a launcher "
            + "install, so a function whose body is not inline in a header cannot be read here. A "
            + "source build of the engine would answer this fully.",
      ),
      filesScanned: scan.filesScanned,
    };
  }

  const calls = callsIn(body.join("\n"), member);
  const callees: Callee[] = [];
  const modules = new Set<string>();
  for (const [name, info] of [...calls.entries()].sort((a, b) => b[1].count - a[1].count || a[0].localeCompare(b[0]))) {
    if (callees.length >= limit) break;
    const resolved = lookupSymbol(index, name)[0];
    if (resolved) modules.add(resolved.module);
    callees.push({
      name,
      count: info.count,
      call: info.call,
      module: resolved?.module,
      include: resolved?.include,
      kind: resolved?.kind,
    });
  }

  return {
    symbol: trimmed,
    found: true,
    definition,
    callees,
    calleeCount: calls.size,
    modules: [...modules].sort(),
    engineSourcesAvailable: hasSources,
    truncated: calls.size > callees.length,
    filesScanned: scan.filesScanned,
    note: definition.kind === "header"
      ? "The body read is inline code in a header, which is all a launcher install has."
      : undefined,
  };
}

/* ── symbol context ────────────────────────────────────────────────── */

export interface ContextResult {
  symbol: string;
  found: boolean;
  header?: string;
  include?: string;
  module?: string;
  kind?: EngineSymbol["kind"] | "member";
  /** The declaration's own line. */
  declarationLine?: number;
  startLine?: number;
  endLine?: number;
  /** The lines themselves, as text, with the declaration inside them. */
  text?: string;
  /** Set when the declaration opens a body that closed inside the window. */
  bodyEndLine?: number;
  /** Other headers declaring the same name, when there is more than one. */
  alsoDeclaredIn?: string[];
  privateHeader?: boolean;
  note?: string;
}

/**
 * The code around a declaration.
 *
 * `verify_symbols` returns the declaration line, which is the signature and
 * nothing else. What a caller usually needs next is the twenty lines around
 * it: the sibling overloads, the UPROPERTY above it, the comment explaining
 * which of three similar methods to call. This returns that without the caller
 * having to know the engine's path layout or open the file.
 *
 * Accepts `Class::Member` as well as a bare type, resolved exactly as
 * `verify_symbols` resolves them, so the two agree about what a name means.
 */
export function symbolContext(
  index: EngineIndex,
  name: string,
  options: { before?: number; after?: number } = {},
): ContextResult {
  const before = Math.max(0, options.before ?? 8);
  const after = Math.max(1, options.after ?? 40);
  const trimmed = name.trim();
  const { className, member } = splitQualified(trimmed);

  let header: string | undefined;
  let line: number | undefined;
  let owner: EngineSymbol | undefined;
  let kind: ContextResult["kind"];
  let alsoDeclaredIn: string[] | undefined;

  if (className) {
    const hit = lookupMember(index, className, member);
    if (hit) {
      owner = hit.owner;
      header = hit.owner.header;
      line = hit.line;
      kind = "member";
    }
  } else {
    const hits = lookupSymbol(index, trimmed);
    if (hits.length > 0) {
      owner = hits[0];
      header = hits[0].header;
      line = hits[0].line;
      kind = hits[0].kind;
      if (hits.length > 1) alsoDeclaredIn = hits.slice(1, 4).map((h) => h.include);
    }
  }

  if (!owner || !header || !line) {
    return {
      symbol: trimmed,
      found: false,
      note: className
        ? `No member '${member}' was found in '${className}'. verify_symbols separates a misspelled `
          + `method from a misspelled class.`
        : `'${trimmed}' is not in the index. verify_symbols reports close spellings.`,
    };
  }

  let source: string;
  try {
    source = fs.readFileSync(path.join(index.engineRoot, header), "utf-8");
  } catch {
    return {
      symbol: trimmed,
      found: false,
      header,
      note: `${header} is in the index but could not be read from ${index.engineRoot}.`,
    };
  }

  const lines = source.split(/\r?\n/);
  const start = Math.max(1, line - before);
  // When the declaration opens a body that closes inside a reasonable window,
  // end there rather than mid-class: a complete type is far more useful than a
  // fixed number of lines that stops halfway through it.
  const extracted = extractBody(lines, line);
  const bodyEnd = extracted && extracted.endLine - line <= after ? extracted.endLine : undefined;
  const end = Math.min(lines.length, bodyEnd ?? line + after);

  return {
    symbol: trimmed,
    found: true,
    header,
    include: owner.include,
    module: owner.module,
    kind,
    declarationLine: line,
    startLine: start,
    endLine: end,
    bodyEndLine: bodyEnd,
    text: lines.slice(start - 1, end).join("\n"),
    alsoDeclaredIn,
    privateHeader: owner.include === owner.header ? true : undefined,
  };
}
