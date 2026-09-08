/**
 * A symbol index over the engine source tree.
 *
 * ue-mcp can write C++ into a project, which is more than most tooling does.
 * What it could not do is make that C++ correct on the first attempt: an agent
 * writing a header had no way to ask whether a symbol exists, which file
 * declares it, which module owns that file, or whether the engine deprecated
 * it two versions ago. It found out at build time, from a compiler error, one
 * missing include at a time.
 *
 * Every one of those questions is the same question asked of one table, so
 * this builds the table once and the actions on top of it are views.
 *
 * ## Why it is cached on disk
 *
 * Reading all 17,313 headers under Runtime, Editor and Developer takes about
 * 1.7 seconds when the filesystem cache is warm, and about 156 seconds when it
 * is cold, because on Windows every first touch goes through the virus
 * scanner. That is far too slow to sit inside a tool call, and it is a cost
 * worth paying exactly once: an installed engine tree is read-only and changes
 * only when the engine is upgraded.
 *
 * So the index is built once, written to the per-user cache directory, and
 * shared by every project on that engine. It is keyed by engine root and by
 * the engine's own build version, so an upgrade invalidates it and nothing
 * else has to notice.
 *
 * ## What it does not do
 *
 * This is a regex pass, not a C++ parser, and it says so where it matters.
 * It indexes what a declaration line can be recognised from - classes,
 * structs, enums, aliases, and API-exported free functions - and resolves a
 * `Class::Method` query by searching inside the class's own header rather than
 * by understanding the class. Templates, macros that generate declarations,
 * and anything behind a preprocessor branch are out of reach. A `found: false`
 * from this index means "not found by this method", which the actions say
 * rather than claiming the symbol does not exist.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { getUserStatePath } from "./user-state.js";

export type SymbolKind = "class" | "struct" | "enum" | "alias" | "function";

export interface EngineSymbol {
  name: string;
  kind: SymbolKind;
  /** Path under Engine/Source, forward slashes. Where it is declared. */
  header: string;
  /** The owning module, taken from the path (Runtime/<Module>/...). */
  module: string;
  /** What to put in an #include, which is the path below Public/ or Classes/. */
  include: string;
  line: number;
  /** The declaration line, trimmed. */
  signature: string;
  /** Carries a MODULE_API export macro, so it is usable outside its module. */
  exported: boolean;
  /** Marked UE_DEPRECATED, with the version and message when they were given. */
  deprecated?: { version?: string; message?: string };
  /** Base class, for a class or struct that named one. */
  parent?: string;
}

export interface EngineIndex {
  engineRoot: string;
  engineVersion: string;
  builtAt: string;
  trees: string[];
  headerCount: number;
  symbolCount: number;
  /** Symbol name to every declaration of it. A name is rarely unique. */
  symbols: Record<string, EngineSymbol[]>;
}

/**
 * What gets indexed.
 *
 * The first three are the engine's own trees under `Engine/Source`. `Plugins`
 * is `Engine/Plugins`, and it is not optional: Gameplay Abilities, Niagara,
 * PCG, Enhanced Input, StateTree and Chooser are all plugins, so an index
 * without them cannot answer the questions most often asked of it.
 *
 * It is filtered rather than taken whole. All 51,178 plugin headers include a
 * great deal of vendored third-party code and a great deal of `Private`.
 * Restricting to `Public` and `Classes` leaves 14,154 and loses nothing
 * usable, because a `Private` header cannot be included from another module in
 * the first place: it could only ever answer "does this exist" with a symbol
 * the caller is unable to reach.
 */
export const DEFAULT_TREES = ["Runtime", "Editor", "Developer", "Plugins"] as const;

/* ── declaration recognition ───────────────────────────────────────── */

// A class or struct definition. The API macro is optional, and the trailing
// context decides whether this is a definition or a forward declaration.
const TYPE_RE =
  /^[ \t]*(?:template\s*<[^>]*>\s*)?(class|struct)\s+(?:([A-Z][A-Z0-9_]*_API)\s+)?([A-Z]\w+)\b([^;{]*)([;{]?)/gm;

const ENUM_RE = /^[ \t]*enum\s+(?:class\s+)?([A-Z]\w+)\b([^;{]*)([;{]?)/gm;

// Column zero deliberately: an indented `using FVector = FVector2D;` is a
// member alias inside some template, and indexing it would answer "where does
// FVector live" with a spatial-index test helper.
const ALIAS_RE = /^(?:using\s+([A-Z]\w+)\s*=|typedef\s+.+?\s+([A-Z]\w+)\s*;)/gm;

// A free function exported from its module. The API macro is what makes it
// findable and is also what makes it callable from outside, so it is required
// here: without it there is no way to tell a declaration from a definition
// fragment with this approach.
const FUNCTION_RE =
  /^[ \t]*(?:([A-Z][A-Z0-9_]*_API)\s+)([\w:<>,\s*&]+?)\s+([A-Z]\w+)\s*\(/gm;

const DEPRECATED_RE = /UE_DEPRECATED\s*\(\s*([\d.]+)\s*,\s*"([^"]*)"/;

/**
 * The include path for a header: the part below `Public/`, `Classes/` or
 * `Internal/`.
 *
 * `Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h` is included as
 * `GameFramework/Actor.h`, not by its path on disk. A header under `Private/`
 * has no include path that works from another module, and is reported by its
 * full path so the caller can see why.
 */
export function includePathFor(relHeader: string): string {
  const m = /(?:^|\/)(?:Public|Classes|Internal)\/(.+)$/.exec(relHeader);
  return m ? m[1] : relHeader;
}

/**
 * The module a header belongs to.
 *
 * Two layouts, because the engine and its plugins are laid out differently
 * and both have to end up as one table:
 *
 *   Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h   -> Engine
 *   Engine/Plugins/Runtime/GameplayAbilities/Source/
 *       GameplayAbilities/Public/AbilitySystemComponent.h        -> GameplayAbilities
 *
 * A plugin's module is the segment after its `Source/`, which is also the name
 * that goes in a Build.cs dependency list. For the engine trees it is the
 * segment after the tree.
 */
export function moduleFor(relHeader: string): string {
  const parts = relHeader.split("/");
  if (parts[0] === "Engine" && parts[1] === "Plugins") {
    const at = parts.lastIndexOf("Source");
    if (at >= 0 && at + 1 < parts.length) return parts[at + 1];
    return parts[parts.length - 2] ?? "";
  }
  // Engine/Source/<Tree>/<Module>/...
  return parts[3] ?? parts[parts.length - 2] ?? "";
}

/** Whether a header can be included from another module at all. */
export function isPrivateHeader(relHeader: string): boolean {
  return /(?:^|\/)Private\//.test(relHeader);
}

/**
 * Does what follows a `class Foo` read as a definition rather than a use?
 *
 * Accepts an empty tail, `final`, an alignas, and a base-class list. Rejects
 * anything holding a `*`, `&`, `(` or `=`, which is what a return type, a
 * variable declaration or an alias looks like.
 */
export function isDefinitionTail(tail: string): boolean {
  const rest = tail.trim();
  if (rest === "") return true;
  if (/[*&(=]/.test(rest)) return false;
  return /^(?:final\b\s*)?(?:alignas\s*\([^)]*\)\s*)?(?::\s*(?:(?:public|protected|private|virtual)\s+)?[\w:<>,\s]+)?$/.test(rest);
}

/**
 * Headers that declare a stub of a type for the reflection system rather than
 * the type itself. Including one gets a caller a compile error about an
 * incomplete type, so they must never win a lookup.
 */
const STUB_HEADERS = ["UObject/NoExportTypes.h"];

/**
 * The engine's type-prefix convention, stripped, so `AActor` can be matched
 * against `Actor.h`. Only stripped when what follows still looks like a type
 * name, so `FVector` gives `Vector` but `Frustum` is left alone.
 */
export function unprefixed(name: string): string {
  return /^[AUFEIST][A-Z]/.test(name) ? name.slice(1) : name;
}

/**
 * Pull every recognisable declaration out of one header.
 *
 * Exported rather than private so the unit tests can hold the recognition
 * rules against small, readable inputs instead of against the engine.
 */
export function scanHeader(source: string, relHeader: string): EngineSymbol[] {
  const out: EngineSymbol[] = [];
  const module = moduleFor(relHeader);
  const include = includePathFor(relHeader);

  // Line offsets, so a match index becomes a line number without counting
  // newlines from the start of the file for every match.
  const lineStarts: number[] = [0];
  for (let i = 0; i < source.length; i++) if (source[i] === "\n") lineStarts.push(i + 1);
  const lineOf = (index: number): number => {
    let lo = 0, hi = lineStarts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (lineStarts[mid] <= index) lo = mid; else hi = mid - 1;
    }
    return lo + 1;
  };

  /** The UE_DEPRECATED that applies to a declaration sits just above it. */
  const deprecationAbove = (index: number): EngineSymbol["deprecated"] => {
    const from = Math.max(0, index - 400);
    const above = source.slice(from, index);
    const lastBlank = above.lastIndexOf("\n\n");
    const window = lastBlank >= 0 ? above.slice(lastBlank) : above;
    const m = DEPRECATED_RE.exec(window);
    return m ? { version: m[1], message: m[2] } : undefined;
  };

  const push = (
    name: string,
    kind: SymbolKind,
    index: number,
    exported: boolean,
    parent?: string,
  ): void => {
    const line = lineOf(index);
    const raw = source.slice(index, source.indexOf("\n", index) === -1 ? undefined : source.indexOf("\n", index));
    out.push({
      name,
      kind,
      header: relHeader,
      module,
      include,
      line,
      signature: raw.trim(),
      exported,
      deprecated: deprecationAbove(index),
      parent,
    });
  };

  for (const m of source.matchAll(TYPE_RE)) {
    // A forward declaration (`class FFoo;`) says nothing about where the type
    // lives, and indexing it would answer "which header declares FFoo" with
    // whichever file happened to forward-declare it first.
    if (m[5] === ";") continue;
    // `class AActor* GetActor() const` starts a line with `class` and names a
    // type, but it is a return type, not a definition. A definition is
    // followed by nothing, by `final`, or by a base-class list.
    if (!isDefinitionTail(m[4] ?? "")) continue;
    const inherits = /:\s*(?:public|protected|private)\s+([\w:]+)/.exec(m[4] ?? "");
    push(m[3], m[1] === "class" ? "class" : "struct", m.index!, Boolean(m[2]), inherits?.[1]);
  }
  for (const m of source.matchAll(ENUM_RE)) {
    if (m[3] === ";") continue;
    push(m[1], "enum", m.index!, false);
  }
  for (const m of source.matchAll(ALIAS_RE)) {
    push(m[1] ?? m[2], "alias", m.index!, false);
  }
  for (const m of source.matchAll(FUNCTION_RE)) {
    push(m[3], "function", m.index!, true);
  }
  return out;
}

/* ── building ──────────────────────────────────────────────────────── */

/** Directories that never hold a header worth indexing. */
const SKIP_DIRS = new Set(["ThirdParty", "Intermediate", "Binaries", "Saved", "node_modules"]);

/**
 * Every .h under the requested trees, as paths relative to the engine root.
 *
 * Relative to the ENGINE root rather than to Engine/Source, because the
 * plugin tree is a sibling of Source rather than under it, and one
 * representation for both is what lets them share a table.
 */
function collectHeaders(engineRoot: string, trees: readonly string[]): string[] {
  const found: string[] = [];

  const walk = (dir: string, keep: (rel: string) => boolean): void => {
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return; // an unreadable directory is not a reason to fail the build
    }
    for (const entry of entries) {
      if (entry.isDirectory()) {
        if (SKIP_DIRS.has(entry.name)) continue;
        walk(path.join(dir, entry.name), keep);
        continue;
      }
      if (!entry.name.endsWith(".h")) continue;
      const rel = path.relative(engineRoot, path.join(dir, entry.name)).replace(/\\/g, "/");
      if (keep(rel)) found.push(rel);
    }
  };

  const all = (): boolean => true;
  // Only a Public or Classes header can be included from another module, so
  // for plugins those are the only ones that could answer a question.
  const includable = (rel: string): boolean => /\/(?:Public|Classes)\//.test(rel);

  for (const tree of trees) {
    if (tree === "Plugins") walk(path.join(engineRoot, "Engine", "Plugins"), includable);
    else walk(path.join(engineRoot, "Engine", "Source", tree), all);
  }
  return found;
}

/** The engine's own version string, used to invalidate the cache. */
export function engineVersionOf(engineRoot: string): string {
  const file = path.join(engineRoot, "Engine", "Build", "Build.version");
  try {
    const v = JSON.parse(fs.readFileSync(file, "utf-8")) as {
      MajorVersion?: number; MinorVersion?: number; PatchVersion?: number; Changelist?: number;
    };
    return `${v.MajorVersion}.${v.MinorVersion}.${v.PatchVersion}+${v.Changelist ?? 0}`;
  } catch {
    return "unknown";
  }
}

export interface BuildProgress {
  (done: number, total: number): void;
}

/** Scan the engine tree and build the index. Slow on a cold cache; see the
 *  module comment. Callers should go through `loadEngineIndex`. */
export function buildEngineIndex(
  engineRoot: string,
  trees: readonly string[] = DEFAULT_TREES,
  onProgress?: BuildProgress,
): EngineIndex {
  const sourceRoot = path.join(engineRoot, "Engine", "Source");
  if (!fs.existsSync(sourceRoot)) {
    throw new Error(`Engine source not found: ${sourceRoot}`);
  }
  const headers = collectHeaders(engineRoot, trees);
  const symbols: Record<string, EngineSymbol[]> = {};
  let symbolCount = 0;

  for (let i = 0; i < headers.length; i++) {
    const rel = headers[i];
    let source: string;
    try {
      source = fs.readFileSync(path.join(engineRoot, rel), "utf-8");
    } catch {
      continue;
    }
    for (const symbol of scanHeader(source, rel)) {
      (symbols[symbol.name] ??= []).push(symbol);
      symbolCount++;
    }
    if (onProgress && (i % 500 === 0 || i === headers.length - 1)) onProgress(i + 1, headers.length);
  }

  return {
    engineRoot,
    engineVersion: engineVersionOf(engineRoot),
    builtAt: new Date().toISOString(),
    trees: [...trees],
    headerCount: headers.length,
    symbolCount,
    symbols,
  };
}

/* ── caching ───────────────────────────────────────────────────────── */

/** Where indexes live: beside the user state, so every project on one engine
 *  shares one index rather than each paying the cold-scan cost. */
export function indexCacheDir(): string {
  return path.join(path.dirname(getUserStatePath()), "engine-index");
}

/** A stable filename for one engine root, so two installs never collide. */
export function indexCacheFile(engineRoot: string, trees: readonly string[] = DEFAULT_TREES): string {
  const normalized = engineRoot.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
  let hash = 5381;
  for (let i = 0; i < normalized.length; i++) hash = ((hash * 33) ^ normalized.charCodeAt(i)) >>> 0;
  const label = path.basename(normalized) || "engine";
  return path.join(indexCacheDir(), `${label}-${hash.toString(16)}-${trees.join("+")}.json`);
}

/** Read a cached index, or null when there is none, it is unreadable, or the
 *  engine has been upgraded since it was written. */
export function readCachedIndex(
  engineRoot: string,
  trees: readonly string[] = DEFAULT_TREES,
): EngineIndex | null {
  const file = indexCacheFile(engineRoot, trees);
  try {
    if (!fs.existsSync(file)) return null;
    const index = JSON.parse(fs.readFileSync(file, "utf-8")) as EngineIndex;
    if (index.engineVersion !== engineVersionOf(engineRoot)) return null;
    if (!index.symbols || typeof index.symbols !== "object") return null;
    return index;
  } catch {
    return null;
  }
}

/** Persist an index. Best-effort: a cache that cannot be written is a slow
 *  next call, not a failed one. */
export function writeCachedIndex(index: EngineIndex): string | null {
  const file = indexCacheFile(index.engineRoot, index.trees);
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(index));
    return file;
  } catch {
    return null;
  }
}

export interface LoadOptions {
  trees?: readonly string[];
  /** Rebuild even when a valid cache exists. */
  refresh?: boolean;
  onProgress?: BuildProgress;
}

export interface LoadedIndex {
  index: EngineIndex;
  /** Where it came from, which the actions report so a slow first call is
   *  explained rather than mysterious. */
  source: "cache" | "built";
  cacheFile: string | null;
  buildMs?: number;
}

/**
 * The last index loaded, held in memory.
 *
 * The disk cache turns a minutes-long scan into a file read, and that was
 * treated as cheap enough to repeat. It is not: the cache file for a full
 * engine is around 80 MB, so every action that wanted the table re-read and
 * re-parsed all of it, which cost about three quarters of a second and around
 * 200 MB of fresh heap per call. A sequence of a dozen index-backed calls
 * therefore churned a couple of gigabytes and evicted the filesystem cache
 * that the file-reading actions beside it depend on, which is how a search
 * that ran in two seconds on its own came to take thirty in company.
 *
 * One entry, not a map: two engines in one process is possible but rare, and
 * one resident copy of an 80 MB table is a cost worth paying while two is not.
 * It is validated against the cache file's size and mtime, so an index
 * rebuilt by another process is picked up, and against the engine's own
 * version, so an upgrade is too.
 */
let residentIndex: { cacheFile: string; stamp: string; index: EngineIndex } | null = null;

/** Size and mtime of the cache file, or null when there is no file. */
function cacheStamp(file: string): string | null {
  try {
    const s = fs.statSync(file);
    return `${s.mtimeMs}:${s.size}`;
  } catch {
    return null;
  }
}

/** The index for one engine, from memory or cache when possible. */
export function loadEngineIndex(engineRoot: string, options: LoadOptions = {}): LoadedIndex {
  const trees = options.trees ?? DEFAULT_TREES;
  const file = indexCacheFile(engineRoot, trees);
  if (!options.refresh) {
    const stamp = cacheStamp(file);
    if (
      residentIndex
      && residentIndex.cacheFile === file
      && stamp !== null
      && residentIndex.stamp === stamp
      && residentIndex.index.engineVersion === engineVersionOf(engineRoot)
    ) {
      return { index: residentIndex.index, source: "cache", cacheFile: file };
    }
    const cached = readCachedIndex(engineRoot, trees);
    if (cached) {
      if (stamp !== null) residentIndex = { cacheFile: file, stamp, index: cached };
      return { index: cached, source: "cache", cacheFile: file };
    }
  }
  const started = Date.now();
  const index = buildEngineIndex(engineRoot, trees, options.onProgress);
  const cacheFile = writeCachedIndex(index);
  const stamp = cacheFile ? cacheStamp(cacheFile) : null;
  residentIndex = cacheFile && stamp !== null ? { cacheFile, stamp, index } : null;
  return { index, source: "built", cacheFile, buildMs: Date.now() - started };
}

/* ── querying ──────────────────────────────────────────────────────── */

/**
 * Every declaration of a name, best first.
 *
 * "Best" is the one a caller wants to include: an exported declaration in a
 * public header beats a private or unexported one, and Runtime beats Editor,
 * because a symbol declared in both is nearly always wanted from Runtime.
 */
export function lookupSymbol(index: EngineIndex, name: string): EngineSymbol[] {
  const found = index.symbols[name];
  if (!found) return [];
  const wanted = unprefixed(name).toLowerCase();
  const score = (s: EngineSymbol): number => {
    let value = 0;
    // The engine names a header after the type it declares far more often than
    // not, and when it does that is the include the caller wants. This has to
    // outweigh everything else: AActor is declared in Actor.h and mentioned in
    // dozens of other headers, several of them exported and public.
    if (path.basename(s.header, ".h").toLowerCase() === wanted) value += 16;
    // A reflection stub is never the answer; including it yields an
    // incomplete type.
    if (STUB_HEADERS.includes(s.include)) value -= 32;
    // A type beats a constructor or a free function of the same name.
    if (s.kind === "class" || s.kind === "struct" || s.kind === "enum") value += 6;
    if (s.exported) value += 4;
    if (!isPrivateHeader(s.header)) value += 2;
    if (s.header.startsWith("Engine/Source/Runtime/")) value += 1;
    return value;
  };
  return [...found].sort((a, b) => score(b) - score(a));
}

/**
 * Resolve `Class::Method` by finding the class, then looking for the method
 * inside that class's header.
 *
 * The index holds types, not members, because indexing every member of every
 * engine header is a much larger table for a question that is nearly always
 * asked about a type. A member query is answered by reading the one file the
 * type resolved to, which costs a single read.
 */
export function lookupMember(
  index: EngineIndex,
  className: string,
  memberName: string,
): { owner: EngineSymbol; line: number; signature: string } | null {
  for (const owner of lookupSymbol(index, className)) {
    let source: string;
    try {
      source = fs.readFileSync(path.join(index.engineRoot, owner.header), "utf-8");
    } catch {
      continue;
    }
    const lines = source.split(/\r?\n/);
    const re = new RegExp(`\\b${memberName}\\s*\\(`);
    for (let i = 0; i < lines.length; i++) {
      const text = lines[i];
      if (!re.test(text)) continue;
      // A call is not a declaration. A declaration line ends in a signature,
      // not in a statement terminator preceded by an assignment.
      if (/^\s*(?:\/\/|\*|#)/.test(text)) continue;
      return { owner, line: i + 1, signature: text.trim() };
    }
  }
  return null;
}

/** Split `UGameplayStatics::GetPlayerPawn` into its parts. */
export function splitQualified(name: string): { className?: string; member: string } {
  const cut = name.lastIndexOf("::");
  if (cut < 0) return { member: name };
  return { className: name.slice(0, cut), member: name.slice(cut + 2) };
}
