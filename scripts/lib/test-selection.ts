/**
 * Which live and automation tests a change needs (#1509), from the graph scripts/lib/test-graph.ts builds.
 *
 * Walk the changed files back through the graph to the actions they reach, then take every test that
 * sends one of those actions, imports or includes a changed file, or is itself changed.
 *
 *   - A change on the dispatch path reaches every action, so it selects the full suite through the graph.
 *   - A category-keyed module (src/tools/pcg.ts, src/tools/specs/pcg.generated.ts) is data the shared
 *     registries look up per action. The walk stops at the edge from it to a registry and credits its
 *     category instead, so a change behind it reaches that category's actions, not every action.
 *   - A dependency change starts from the src/ modules that import the top-level dependencies whose
 *     installed tree changed (scripts/lib/lockfile-delta.ts). A devDependency, or a package nothing in
 *     src/ imports, selects no editor test. Without both sides of the diff it selects the full suite.
 *   - A change the graph cannot see selects the full suite: Build.cs, the .uplugin, Config/, tests/ue_mcp,
 *     package.json outside its dependency fields, the test runners, and plugin files no test or action reaches.
 *   - A stable release always runs the full suite.
 */
import { dependencyDelta, type DependencySnapshot } from "./lockfile-delta.js";
import type { TestGraph, TestNode } from "./test-graph.js";

export interface SelectedTest {
  id: string;
  suite: TestNode["suite"];
  file: string;
  kind?: string;
  /** Why it runs, one line naming the changed file. */
  reason: string;
}

export interface Selection {
  full: boolean;
  /** Why the full suite runs, when it does. */
  fullReason?: string;
  tests: SelectedTest[];
  /** Bridge methods the change reaches. */
  actions: string[];
  /** Changed files that select no test. */
  inert: string[];
  /** What a package.json or package-lock.json change reached, when one was followed. */
  dependencies?: DependencyReport;
}

export interface DependencyReport {
  /** Runtime top-level dependency -> the src/ modules that import it. */
  importers: Record<string, string[]>;
  /** Runtime top-level dependencies whose tree changed and that nothing in src/ imports. */
  unimported: string[];
  /** devDependencies whose tree changed. */
  dev: string[];
}

export interface SelectOptions {
  /** A stable release runs everything. */
  stable?: boolean;
  /** package.json and package-lock.json at both ends of the diff. Without them a change to either selects the full suite. */
  dependencies?: { base: DependencySnapshot; head: DependencySnapshot };
}

const DEPENDENCY_FILES = ["package.json", "package-lock.json"];

/** Descriptor fields the release stamps on every version: a change to only these alters no behaviour. */
const DESCRIPTOR_STAMP_FIELDS = ["Version", "VersionName", "Description"];

/**
 * Whether two versions of a .uplugin differ only in the fields `generate:metadata` stamps (the version
 * and the action count in the description). Such a change is not opaque: it selects nothing.
 */
export function descriptorStampOnly(baseText: string | null, headText: string | null): boolean {
  if (baseText === null || headText === null) return false;
  try {
    const strip = (t: string) => {
      const o = JSON.parse(t) as Record<string, unknown>;
      for (const k of DESCRIPTOR_STAMP_FIELDS) delete o[k];
      return JSON.stringify(o);
    };
    return strip(baseText) === strip(headText);
  } catch {
    return false;
  }
}

/** Why the graph cannot see a file's effect, or null when it can. */
export function opaqueReason(file: string): string | null {
  if (/\.(?:Build|Target)\.cs$/.test(file)) return "build rules";
  if (file.endsWith(".uplugin")) return "the plugin descriptor";
  if (/(^|\/)Config\//.test(file) && (file.startsWith("plugin/") || file.startsWith("tests/ue_mcp/"))) return "engine config";
  if (file.startsWith("tests/ue_mcp/")) return "test project content";
  if (file === "package.json" || file === "package-lock.json") return "dependencies";
  return null;
}

/** Reverse edges: file -> the files that depend on it. */
export function dependents(deps: Record<string, string[]>): Map<string, string[]> {
  const out = new Map<string, string[]>();
  for (const [from, list] of Object.entries(deps)) {
    for (const to of list) out.set(to, [...(out.get(to) ?? []), from]);
  }
  return out;
}

interface Reach {
  /** Reached file -> the changed file it was reached from. */
  files: Map<string, string>;
  /** Category -> the changed file that reached a module keyed by it. */
  categories: Map<string, string>;
  /** Bridge method -> the changed file that reached a spec module listing it. */
  methods: Map<string, string>;
}

/** The reverse walk, stopping where a keyed module meets a dependent outside its category. */
export function reach(graph: TestGraph, rev: Map<string, string[]>, sources: string[], labels?: Map<string, string>): Reach {
  const out: Reach = { files: new Map(), categories: new Map(), methods: new Map() };
  const queue: string[] = [];
  const visit = (f: string, from: string) => {
    if (out.files.has(f)) return;
    out.files.set(f, from);
    queue.push(f);
  };
  for (const s of sources) visit(s, labels?.get(s) ?? s);
  while (queue.length > 0) {
    const f = queue.shift()!;
    const from = out.files.get(f)!;
    const key = graph.keyed[f];
    if (key) {
      for (const c of key.categories) if (!out.categories.has(c)) out.categories.set(c, from);
      for (const m of key.methods) if (!out.methods.has(m)) out.methods.set(m, from);
    }
    for (const d of rev.get(f) ?? []) {
      const dKey = graph.keyed[d];
      const sameCategory = key && dKey && dKey.categories.some((c) => key.categories.includes(c));
      // A test that imports a keyed module still depends on it; a registry that gathers it does not.
      if (key && !sameCategory && !d.startsWith("tests/")) continue;
      visit(d, from);
    }
  }
  return out;
}

export function selectTests(graph: TestGraph, changed: string[], opts: SelectOptions = {}): Selection {
  const all = (fullReason: string): Selection => ({
    full: true,
    fullReason,
    tests: graph.tests.map((t) => ({ id: t.id, suite: t.suite, file: t.file, kind: t.kind, reason: fullReason })),
    actions: Object.keys(graph.actions).sort(),
    inert: [],
  });
  if (opts.stable) return all("a stable release runs the full suite");
  const depChanged = changed.filter((f) => DEPENDENCY_FILES.includes(f));
  for (const f of changed) {
    if (opts.dependencies && depChanged.includes(f)) continue;
    const why = opaqueReason(f);
    if (why) return all(`${f} changed (${why}), which the graph cannot see`);
  }
  const runner = changed.find((f) => graph.runners.includes(f));
  if (runner) return all(`${runner} changed, and it runs every test`);
  const unseen = changed.find((f) => f.startsWith("plugin/") && !(f in graph.deps));
  if (unseen) return all(`${unseen} changed, and the graph has no node for it`);

  // A dependency change starts from the src/ modules that import a top-level dependency whose tree moved.
  let dependencies: DependencyReport | undefined;
  const labels = new Map<string, string>();
  if (opts.dependencies && depChanged.length > 0) {
    const delta = dependencyDelta(opts.dependencies.base, opts.dependencies.head);
    if (delta.opaque) return all(`${delta.opaque}, which the graph cannot see`);
    dependencies = { importers: {}, unimported: [], dev: delta.dev };
    for (const top of delta.runtime.keys()) {
      const files = Object.entries(graph.packages ?? {})
        .filter(([f, pkgs]) => f.startsWith("src/") && pkgs.includes(top))
        .map(([f]) => f)
        .sort();
      if (files.length === 0) dependencies.unimported.push(top);
      else dependencies.importers[top] = files;
      for (const f of files) {
        if (!changed.includes(f) && !labels.has(f)) labels.set(f, `${f} (imports ${top}, changed in ${depChanged.join(" and ")})`);
      }
    }
  }

  const rev = dependents(graph.deps);
  const known = changed.filter((f) => f in graph.deps);
  const r = reach(graph, rev, [...known, ...labels.keys()], labels);

  // A plugin file that reaches no action and no test holds code the graph cannot follow, such as
  // module startup or the socket server, which every action runs through.
  const actionFiles = new Set(Object.values(graph.actions).flatMap((n) => n.files));
  const testFiles = new Set(graph.tests.map((t) => t.file));
  for (const f of known.filter((c) => c.startsWith("plugin/"))) {
    const own = reach(graph, rev, [f]);
    if (![...own.files.keys()].some((x) => actionFiles.has(x) || testFiles.has(x))) {
      return all(`${f} changed and reaches no action or test, so the graph cannot see what it runs`);
    }
  }

  const dispatchHit = graph.dispatch.find((d) => r.files.has(d));
  const affected = new Map<string, string>();
  for (const [method, node] of Object.entries(graph.actions)) {
    if (dispatchHit) {
      affected.set(method, `${r.files.get(dispatchHit)} reaches the dispatch path (${dispatchHit})`);
      continue;
    }
    const file = node.files.find((f) => r.files.has(f));
    const category = node.categories.find((c) => r.categories.has(c));
    const from = file ? r.files.get(file) : category ? r.categories.get(category) : r.methods.get(method);
    if (from) affected.set(method, `${from} reaches ${method}`);
  }

  const tests: SelectedTest[] = [];
  for (const t of graph.tests) {
    let reason: string | null = null;
    const via = r.files.get(t.file);
    if (changed.includes(t.file)) reason = `${t.file} changed`;
    else if (via) reason = `${t.suite === "live" ? "imports" : "includes"} ${via}`;
    else if (t.actions === "*") {
      if (affected.size > 0) reason = `reads every action; ${affected.values().next().value}`;
    } else {
      const hit = t.actions.find((m) => affected.has(m));
      if (hit) reason = `sends ${hit}; ${affected.get(hit)}`;
    }
    if (!reason) {
      const cats = t.categories === "*" ? [...r.categories.keys()] : t.categories.filter((c) => r.categories.has(c));
      if (cats.length > 0) reason = `calls ${cats[0]}; ${r.categories.get(cats[0])} reaches it`;
    }
    if (reason) tests.push({ id: t.id, suite: t.suite, file: t.file, kind: t.kind, reason });
  }

  const actionCount = Object.keys(graph.actions).length;
  if (actionCount > 0 && affected.size === actionCount) {
    const counts = new Map<string, number>();
    for (const v of affected.values()) counts.set(v.split(" reaches ")[0], (counts.get(v.split(" reaches ")[0]) ?? 0) + 1);
    const [widest, n] = [...counts].sort((a, b) => b[1] - a[1])[0];
    const why = dispatchHit
      ? (affected.values().next().value as string)
      : n === actionCount ? `${widest} reaches every action` : "the changed files together reach every action";
    return all(why);
  }
  if (tests.length === graph.tests.length) return { ...all("the change reaches every test"), tests, dependencies };
  const inert = changed.filter((f) => !tests.some((t) => t.reason.includes(f)));
  return { full: false, tests, actions: [...affected.keys()].sort(), inert, dependencies };
}
