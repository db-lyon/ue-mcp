/**
 * The dependency graph the release selects its live and automation tests from (#1509).
 *
 * Nodes are repo-relative files; `deps[a]` lists the files `a` reads code from.
 *   TS   relative imports, read with the TypeScript compiler's preProcessFile, plus a spawn edge
 *        to src/index.ts from any test file that starts the server as a process.
 *   C++  `#include "..."` lines, resolved against the plugin's module folders. A header also
 *        depends on the .cpp of its name in the same module, which holds what it declares.
 *        clang-scan-deps is not installed with this machine's toolchains, so this is a regex
 *        scan; the plugin includes its own headers by quoted name only, which it resolves.
 * `actions[m]` names the files bridge method m's code lives in (the .cpp that registers it, the
 * .cpp that defines its handler, its category tool module) and the categories that declare it.
 * `keyed` marks the category-keyed modules under src/tools: data the shared registries look up
 * per action. `dispatch` is the TS path every bridge call takes in the shipped server.
 *
 * Per-file parses are cached under node_modules/.cache and reparsed only for files that changed.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import ts from "typescript";

import { listRegistrations } from "./cpp-registrations.mjs";
import { packageOf } from "./lockfile-delta.js";
import { maskLiterals } from "./tool-source.mjs";
import { helperSpans, walk } from "./test-scan.js";
import { bridgeActionMap, cppDispatches, forwardingHelpers, tsDispatches } from "../behavioural-coverage.js";
import { readKind } from "../live-shards.mjs";

export interface TestNode {
  /** The live file, or the automation test's full name. */
  id: string;
  suite: "live" | "automation";
  /** Repo-relative file the test lives in. */
  file: string;
  /** The live file's `// @live-editor` kind. */
  kind?: string;
  /** Bridge methods the file sends, setup calls included; "*" for a test that reads every one. */
  actions: string[] | "*";
  /** Categories the file calls through the server; "*" for one that lists every tool. */
  categories: string[] | "*";
}

export interface ActionNode {
  files: string[];
  categories: string[];
}

export interface KeyedModule {
  categories: string[];
  methods: string[];
}

export interface TestGraph {
  deps: Record<string, string[]>;
  actions: Record<string, ActionNode>;
  keyed: Record<string, KeyedModule>;
  dispatch: string[];
  /** File -> the npm packages it imports by bare specifier, for a lockfile change to start from. */
  packages?: Record<string, string[]>;
  /** The files that run the tests; a change to one reruns everything. */
  runners: string[];
  tests: TestNode[];
}

export const PLUGIN_SOURCE = "plugin/ue_mcp_bridge/Source";
const TS_DIRS = ["src", "tests", "scripts"];
const TS_EXT = /\.(?:ts|mts|mjs|js)$/;
const CPP_EXT = /\.(?:h|hpp|inl|cpp)$/;
/** Where every bridge call passes in the shipped server, before it reaches the plugin. */
export const DISPATCH_ROOTS = ["src/server/mcp-server.ts", "src/dispatch/server-dispatch.ts", "src/bridge/bridge.ts"];
/** Entry points of the live and automation runners; their import closures are the runners. */
export const RUNNER_ROOTS = ["scripts/live-tests.mjs", "scripts/automation-tests.mjs", "vitest.config.ts"];
/** The pseudo-specifier a file gets when it starts the server as a process. */
const SPAWN_SPEC = "=src/index.ts";

// ---------------------------------------------------------------------------
// Parsing, pure
// ---------------------------------------------------------------------------

/** Quoted include targets in C++ source; angle-bracket includes are engine and system headers. */
export function parseIncludes(source: string): string[] {
  const out: string[] = [];
  for (const m of source.matchAll(/^[ \t]*#[ \t]*include[ \t]*"([^"]+)"/gm)) {
    if (!m[1].endsWith(".generated.h")) out.push(m[1]);
  }
  return out;
}

/** Relative import specifiers in TS or JS source, plus SPAWN_SPEC when it starts src/index.ts. */
export function parseImports(source: string): string[] {
  const info = ts.preProcessFile(source, true, true);
  const out = info.importedFiles.map((f) => f.fileName).filter((s) => s.startsWith("."));
  if (/(["'])src\1\s*,\s*(["'])index\.ts\2|["'`/]src\/index\.ts["'`]/.test(source)) out.push(SPAWN_SPEC);
  return out;
}

/** The npm packages TS or JS source imports by bare specifier, builtins left out. */
export function parsePackageImports(source: string): string[] {
  const info = ts.preProcessFile(source, true, true);
  const out = new Set<string>();
  for (const f of info.importedFiles) {
    const name = packageOf(f.fileName);
    if (name) out.add(name);
  }
  return [...out].sort();
}

/** Package imports ride in the cached spec list under this prefix. */
const PACKAGE_SPEC = "pkg:";
const parseTsSpecs = (source: string) => [...parseImports(source), ...parsePackageImports(source).map((p) => `${PACKAGE_SPEC}${p}`)];

/** A relative specifier as a repo file, or null when it leaves the known set. */
export function resolveImport(from: string, spec: string, known: Set<string>): string | null {
  if (spec.startsWith("=")) return known.has(spec.slice(1)) ? spec.slice(1) : null;
  const base = path.posix.normalize(path.posix.join(path.posix.dirname(from), spec));
  const tries = [base];
  if (/\.js$/.test(base)) tries.push(base.replace(/\.js$/, ".ts"));
  if (/\.mjs$/.test(base)) tries.push(base.replace(/\.mjs$/, ".mts"));
  if (!/\.\w+$/.test(base)) tries.push(`${base}.ts`, `${base}.js`, `${base}/index.ts`);
  return tries.find((t) => known.has(t)) ?? null;
}

const moduleRoot = (file: string) => file.split("/").slice(0, 4).join("/");

/**
 * An include target as a plugin file: beside the includer first, then under its module's
 * Public, Private and Classes folders, then any plugin file whose path ends with it.
 */
export function resolveInclude(from: string, spec: string, known: Set<string>, byName: Map<string, string[]>): string | null {
  const beside = path.posix.normalize(path.posix.join(path.posix.dirname(from), spec));
  if (known.has(beside)) return beside;
  const module = moduleRoot(from);
  for (const root of ["Public", "Private", "Classes"]) {
    const p = `${module}/${root}/${spec}`;
    if (known.has(p)) return p;
  }
  const hits = byName.get(path.posix.basename(spec))?.filter((f) => f === spec || f.endsWith(`/${spec}`)) ?? [];
  if (hits.length === 0) return null;
  // Same module first: two modules may each have a header of one name.
  return hits.find((h) => h.startsWith(`${module}/`)) ?? hits[0];
}

/** Every file reachable from `roots` through `deps`, roots included. */
export function closure(deps: Record<string, string[]>, roots: string[]): Set<string> {
  const seen = new Set<string>();
  const queue = [...roots];
  while (queue.length > 0) {
    const f = queue.pop()!;
    if (seen.has(f) || !(f in deps)) continue;
    seen.add(f);
    for (const d of deps[f]) if (!seen.has(d)) queue.push(d);
  }
  return seen;
}

/** The category a module under src/tools is keyed by, or null for a shared one. */
export function keyedCategory(file: string, categories: Set<string>): string | null {
  const m = /^src\/tools\/(?:(epic|specs)\/)?([\w-]+?)(?:\.generated)?\.ts$/.exec(file) ?? /^src\/tools\/([\w-]+)\/[\w./-]+\.ts$/.exec(file);
  if (!m) return null;
  const name = m.length === 3 ? m[2] : m[1];
  if (categories.has(name)) return name;
  const stem = name.split("-")[0];
  return categories.has(stem) ? stem : null;
}

/** The bridge methods a recorded handler-spec module lists, by its top-level keys. */
export function specMethods(source: string): string[] {
  return [...source.matchAll(/^ {2}"(\w+)": \{/gm)].map((m) => m[1]);
}

// ---------------------------------------------------------------------------
// The cache
// ---------------------------------------------------------------------------

interface CacheEntry { size: number; mtimeMs: number; sha1: string; specs: string[] }
interface Cache { version: number; files: Record<string, CacheEntry> }
const CACHE_VERSION = 3;

function readCache(file: string): Cache {
  try {
    const parsed = JSON.parse(fs.readFileSync(file, "utf8")) as Cache;
    if (parsed.version === CACHE_VERSION) return parsed;
  } catch {
    // missing or unreadable: rebuild
  }
  return { version: CACHE_VERSION, files: {} };
}

export interface BuildStats { files: number; reparsed: number }

// ---------------------------------------------------------------------------
// The graph
// ---------------------------------------------------------------------------

export function buildTestGraph(repo: string, opts: { cacheFile?: string; stats?: BuildStats } = {}): TestGraph {
  const rel = (abs: string) => path.relative(repo, abs).split(path.sep).join("/");
  const cacheFile = opts.cacheFile ?? path.join(repo, "node_modules", ".cache", "ue-mcp", "test-graph.json");
  const cache = readCache(cacheFile);
  const fresh: Cache = { version: CACHE_VERSION, files: {} };
  let reparsed = 0;

  const tsFiles = [
    ...TS_DIRS.flatMap((d) => walk(path.join(repo, d), (n) => TS_EXT.test(n) && !n.endsWith(".d.ts"))).map(rel),
    ...RUNNER_ROOTS.filter((f) => !f.includes("/") && fs.existsSync(path.join(repo, f))),
  ].filter((f) => !f.startsWith("tests/ue_mcp/") && !f.startsWith("tests/.shards/"));
  const cppFiles = walk(path.join(repo, PLUGIN_SOURCE), (n) => CPP_EXT.test(n)).map(rel);
  const sources = new Map<string, string>();
  const read = (f: string) => {
    let s = sources.get(f);
    if (s === undefined) {
      s = fs.readFileSync(path.join(repo, f), "utf8");
      sources.set(f, s);
    }
    return s;
  };
  const specsOf = (f: string, parse: (s: string) => string[]): string[] => {
    const st = fs.statSync(path.join(repo, f));
    const hit = cache.files[f];
    if (hit && hit.size === st.size && hit.mtimeMs === st.mtimeMs) {
      fresh.files[f] = hit;
      return hit.specs;
    }
    const text = read(f);
    const sha1 = crypto.createHash("sha1").update(text).digest("hex");
    if (hit && hit.sha1 === sha1) {
      fresh.files[f] = { ...hit, size: st.size, mtimeMs: st.mtimeMs };
      return hit.specs;
    }
    reparsed++;
    const specs = parse(text);
    fresh.files[f] = { size: st.size, mtimeMs: st.mtimeMs, sha1, specs };
    return specs;
  };

  const deps: Record<string, string[]> = {};
  const packages: Record<string, string[]> = {};
  const tsKnown = new Set(tsFiles);
  for (const f of tsFiles) {
    const out = new Set<string>();
    const pkgs: string[] = [];
    for (const spec of specsOf(f, parseTsSpecs)) {
      if (spec.startsWith(PACKAGE_SPEC)) {
        pkgs.push(spec.slice(PACKAGE_SPEC.length));
        continue;
      }
      const hit = resolveImport(f, spec, tsKnown);
      if (hit && hit !== f) out.add(hit);
    }
    deps[f] = [...out].sort();
    if (pkgs.length > 0) packages[f] = pkgs;
  }
  const cppKnown = new Set(cppFiles);
  const byName = new Map<string, string[]>();
  for (const f of cppFiles) {
    const b = path.posix.basename(f);
    byName.set(b, [...(byName.get(b) ?? []), f]);
  }
  for (const f of cppFiles) {
    const out = new Set<string>();
    for (const spec of specsOf(f, parseIncludes)) {
      const hit = resolveInclude(f, spec, cppKnown, byName);
      if (hit && hit !== f) out.add(hit);
    }
    if (f.endsWith(".h")) {
      for (const c of byName.get(`${path.posix.basename(f, ".h")}.cpp`) ?? []) {
        if (moduleRoot(c) === moduleRoot(f)) out.add(c);
      }
    }
    deps[f] = [...out].sort();
  }

  fs.mkdirSync(path.dirname(cacheFile), { recursive: true });
  fs.writeFileSync(cacheFile, JSON.stringify(fresh));
  if (opts.stats) Object.assign(opts.stats, { files: tsFiles.length + cppFiles.length, reparsed });

  const byCategory = bridgeActionMap();
  const categories = new Set(byCategory.keys());
  const keyed: Record<string, KeyedModule> = {};
  for (const f of tsFiles) {
    if (/^src\/tools\/specs\/\w+\.generated\.ts$/.test(f)) {
      keyed[f] = { categories: [], methods: specMethods(read(f)) };
      continue;
    }
    const category = keyedCategory(f, categories);
    if (category) keyed[f] = { categories: [category], methods: [] };
  }

  const actions = actionFiles(cppFiles, read, deps, byCategory);
  const methods = new Set(Object.keys(actions));
  return {
    deps,
    actions,
    keyed,
    dispatch: DISPATCH_ROOTS.filter((r) => r in deps),
    packages,
    runners: [...closure(deps, RUNNER_ROOTS)].sort(),
    tests: [...liveTests(repo, read, methods, byCategory), ...automationTests(read, cppFiles, methods)],
  };
}

/** bridge method -> the files its code lives in and the categories that declare it. */
function actionFiles(
  cppFiles: string[],
  read: (f: string) => string,
  deps: Record<string, string[]>,
  byCategory: Map<string, Map<string, string>>,
): Record<string, ActionNode> {
  // Handler definitions: `TSharedPtr<FJsonValue> FClass::Fn(const TSharedPtr<FJsonObject>& Params)`.
  const defs = new Map<string, Array<{ cls: string | null; file: string }>>();
  for (const f of cppFiles.filter((c) => c.endsWith(".cpp"))) {
    for (const m of read(f).matchAll(/TSharedPtr<FJsonValue>\s+(?:(F\w+)::)?(\w+)\s*\(\s*const\s+TSharedPtr<FJsonObject>/g)) {
      defs.set(m[2], [...(defs.get(m[2]) ?? []), { cls: m[1] ?? null, file: f }]);
    }
  }
  // Category tool modules: the imports src/tools.ts lists in ALL_TOOLS, in ALL_TOOLS order.
  const toolsSource = read("src/tools.ts");
  const moduleOf = new Map<string, string>();
  for (const m of toolsSource.matchAll(/import\s*\{\s*(\w+)\s*\}\s*from\s*"(\.\/tools\/[\w./-]+)"/g)) {
    const resolved = resolveImport("src/tools.ts", m[2], new Set(Object.keys(deps)));
    if (resolved) moduleOf.set(m[1], resolved);
  }
  const listed = /ALL_TOOLS\s*:[^=]*=\s*\[([^\]]*)\]/.exec(toolsSource)?.[1].match(/\w+/g) ?? [];
  const names = [...byCategory.keys()];
  const toolModule = new Map<string, string>();
  listed.forEach((ident, i) => {
    const mod = moduleOf.get(ident);
    if (mod && names[i]) toolModule.set(names[i], mod);
  });
  const categoriesOf = new Map<string, Set<string>>();
  for (const [category, acts] of byCategory) {
    for (const bridge of acts.values()) categoriesOf.set(bridge, (categoriesOf.get(bridge) ?? new Set()).add(category));
  }

  const out: Record<string, ActionNode> = {};
  const handlersDir = `${PLUGIN_SOURCE}/UE_MCP_Bridge/Private/Handlers/`;
  const fileByName = new Map(cppFiles.filter((f) => f.startsWith(handlersDir)).map((f) => [path.posix.basename(f), f]));
  for (const r of listRegistrations() as Array<{ method: string; className: string | null; fn: string; file: string }>) {
    const registering = fileByName.get(r.file);
    if (!registering) continue;
    const node = out[r.method] ?? { files: [], categories: [...(categoriesOf.get(r.method) ?? [])].sort() };
    const files = new Set(node.files).add(registering);
    let found = (defs.get(r.fn) ?? []).filter((d) => !r.className || d.cls === r.className);
    const near = found.filter((d) => path.posix.dirname(d.file) === path.posix.dirname(registering));
    if (near.length > 0) found = near;
    for (const d of found) files.add(d.file);
    for (const category of node.categories) {
      const mod = toolModule.get(category);
      if (mod) files.add(mod);
    }
    out[r.method] = { files: [...files].sort(), categories: node.categories };
  }
  return out;
}

/** Categories a live file calls through the server: `server.call("pcg", { action: ... })`. */
export function calledCategories(source: string, categories: Set<string>): string[] | "*" {
  if (/\b(?:listTools|listToolNames|ALL_TOOLS)\b/.test(source) || /golden\/capture/.test(source)) return "*";
  const out = new Set<string>();
  for (const m of source.matchAll(/\bcall\s*\(\s*(["'`])(\w+)\1\s*,/g)) if (categories.has(m[2])) out.add(m[2]);
  return [...out].sort();
}

function liveTests(
  repo: string,
  read: (f: string) => string,
  methods: Set<string>,
  byCategory: Map<string, Map<string, string>>,
): TestNode[] {
  const categories = new Set(byCategory.keys());
  const out: TestNode[] = [];
  for (const abs of walk(path.join(repo, "tests", "live"), (n) => n.endsWith(".test.ts"))) {
    const file = path.relative(repo, abs).split(path.sep).join("/");
    const source = read(file);
    const masked = maskLiterals(source);
    const helpers = helperSpans(masked, "ts");
    const whole = [{ start: 0, end: source.length }];
    const sent = [...tsDispatches(source, masked, whole, methods, byCategory, forwardingHelpers(masked, helpers))].sort();
    // The connected golden records the surface the editor advertises, which every registration shapes.
    const readsAll = /golden\/capture/.test(source);
    out.push({
      id: file,
      suite: "live",
      file,
      kind: readKind(source) ?? undefined,
      actions: readsAll ? "*" : sent,
      categories: calledCategories(source, categories),
    });
  }
  return out;
}

function automationTests(read: (f: string) => string, cppFiles: string[], methods: Set<string>): TestNode[] {
  const counts = new Map<string, Set<string>>();
  for (const r of listRegistrations() as Array<{ method: string; fn: string }>) counts.set(r.fn, (counts.get(r.fn) ?? new Set()).add(r.method));
  const byFunction = new Map<string, string>();
  for (const [fn, set] of counts) if (set.size === 1) byFunction.set(fn, [...set][0]);

  const out: TestNode[] = [];
  for (const file of cppFiles.filter((f) => f.endsWith(".cpp"))) {
    const source = read(file);
    if (!source.includes("_AUTOMATION_TEST")) continue;
    const names = [...source.matchAll(/IMPLEMENT_\w*AUTOMATION_TEST\w*\(\s*\w+\s*,\s*"(UE\.MCP\.[^"]+)"/g)].map((m) => m[1]);
    if (names.length === 0) continue;
    const masked = maskLiterals(source);
    const sent = [...cppDispatches(source, masked, [{ start: 0, end: source.length }], methods, byFunction)].sort();
    for (const id of names) out.push({ id, suite: "automation", file, actions: sent, categories: [] });
  }
  return out;
}
