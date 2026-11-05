/**
 * What a package.json or package-lock.json change reaches, for the test selection (#1509).
 *
 * The lockfile diff names the packages whose installed code changed. Each one is mapped, through the
 * lockfile's tree, to the top-level dependencies that pull it in. The selection then walks back from
 * the src/ modules that import those top-level dependencies. A manifest change outside the dependency
 * fields cannot be followed, so it is reported as opaque.
 */
import { builtinModules } from "node:module";

interface LockEntry {
  version?: string;
  resolved?: string;
  integrity?: string;
  link?: boolean;
  dependencies?: Record<string, string>;
  devDependencies?: Record<string, string>;
  optionalDependencies?: Record<string, string>;
  peerDependencies?: Record<string, string>;
}
interface Lockfile { lockfileVersion?: number; packages?: Record<string, LockEntry> }
type Manifest = Record<string, unknown> & {
  dependencies?: Record<string, string>;
  devDependencies?: Record<string, string>;
  optionalDependencies?: Record<string, string>;
  peerDependencies?: Record<string, string>;
};

/** The two files at one ref; null when the file does not exist there. */
export interface DependencySnapshot { manifest: string | null; lock: string | null }

export interface DependencyDelta {
  /** Why the change cannot be followed, or null when it can. */
  opaque: string | null;
  /** Runtime top-level dependency -> the changed packages it pulls in. */
  runtime: Map<string, string[]>;
  /** devDependencies whose tree changed. They ship nothing, so they select no editor test. */
  dev: string[];
}

/** Manifest fields a change to which the dependency walk accounts for, or which ship no code. */
const FOLLOWED_FIELDS = new Set(["version", "description", "dependencies", "devDependencies", "optionalDependencies", "peerDependencies"]);
const RUNTIME_FIELDS = ["dependencies", "optionalDependencies", "peerDependencies"] as const;
const BUILTINS = new Set(builtinModules);

/** The package a bare import specifier loads, or null for a relative, builtin or URL specifier. */
export function packageOf(spec: string): string | null {
  if (spec.startsWith(".") || spec.startsWith("/") || spec.startsWith("node:") || /^[a-z]+:/i.test(spec)) return null;
  const parts = spec.split("/");
  const name = spec.startsWith("@") ? parts.slice(0, 2).join("/") : parts[0];
  if (!name || BUILTINS.has(name)) return null;
  return name;
}

const nameOf = (key: string) => key.slice(key.lastIndexOf("node_modules/") + "node_modules/".length);
const parentOf = (key: string) => {
  const i = key.lastIndexOf("/node_modules/");
  return i === -1 ? "" : key.slice(0, i);
};
const fingerprint = (e: LockEntry | undefined) => (e ? JSON.stringify([e.version, e.resolved, e.integrity, e.link]) : null);

/** Lockfile keys whose installed package was added, removed or changed. The root entry is not one. */
export function changedPackageKeys(base: Lockfile, head: Lockfile): string[] {
  const a = base.packages ?? {};
  const b = head.packages ?? {};
  const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
  keys.delete("");
  return [...keys].filter((k) => fingerprint(a[k]) !== fingerprint(b[k])).sort();
}

/** Where node would load dependency `dep` from for the package installed at `from`. */
export function resolveFrom(packages: Record<string, LockEntry>, from: string, dep: string): string | null {
  let p = from;
  for (;;) {
    const candidate = p ? `${p}/node_modules/${dep}` : `node_modules/${dep}`;
    if (candidate in packages) return candidate;
    if (!p) return null;
    p = parentOf(p);
  }
}

/** Every lockfile key a top-level dependency's install reaches, its own included. */
export function treeOf(packages: Record<string, LockEntry>, top: string): Set<string> {
  const seen = new Set<string>();
  const start = resolveFrom(packages, "", top);
  const queue = start ? [start] : [];
  while (queue.length > 0) {
    const key = queue.pop()!;
    if (seen.has(key)) continue;
    seen.add(key);
    const e = packages[key];
    for (const field of RUNTIME_FIELDS) {
      for (const dep of Object.keys(e?.[field] ?? {})) {
        const hit = resolveFrom(packages, key, dep);
        if (hit && !seen.has(hit)) queue.push(hit);
      }
    }
  }
  return seen;
}

const depNames = (m: Manifest | LockEntry | undefined, fields: readonly string[]) =>
  new Set(fields.flatMap((f) => Object.keys(((m as Record<string, unknown> | undefined)?.[f] as Record<string, string>) ?? {})));

function parse<T>(text: string | null): T | null {
  if (text === null) return null;
  try {
    return JSON.parse(text) as T;
  } catch {
    return null;
  }
}

export function dependencyDelta(base: DependencySnapshot, head: DependencySnapshot): DependencyDelta {
  const none = (opaque: string): DependencyDelta => ({ opaque, runtime: new Map(), dev: [] });
  const [bm, hm] = [parse<Manifest>(base.manifest), parse<Manifest>(head.manifest)];
  if (!bm || !hm) return none("package.json is missing or unreadable on one side of the diff");
  const fields = new Set([...Object.keys(bm), ...Object.keys(hm)]);
  const moved = [...fields].filter((f) => !FOLLOWED_FIELDS.has(f) && JSON.stringify(bm[f]) !== JSON.stringify(hm[f]));
  if (moved.length > 0) return none(`package.json changed outside its dependencies (${moved.join(", ")})`);
  const [bl, hl] = [parse<Lockfile>(base.lock), parse<Lockfile>(head.lock)];
  if (!bl?.packages || !hl?.packages) return none("package-lock.json has no packages tree on one side of the diff");

  const runtimeTop = new Set([...depNames(bm, RUNTIME_FIELDS), ...depNames(hm, RUNTIME_FIELDS)]);
  const devTop = new Set([...depNames(bm, ["devDependencies"]), ...depNames(hm, ["devDependencies"])]);
  for (const root of [bl.packages[""], hl.packages[""]]) {
    for (const n of depNames(root, RUNTIME_FIELDS)) runtimeTop.add(n);
    for (const n of depNames(root, ["devDependencies"])) devTop.add(n);
  }

  // A top-level whose requested range moved changed even when the installed tree did not.
  const changed = new Set(changedPackageKeys(bl, hl));
  const rangeOf = (m: Manifest, n: string) => JSON.stringify(["dependencies", "devDependencies", "optionalDependencies", "peerDependencies"].map((f) => (m[f] as Record<string, string> | undefined)?.[n]));
  for (const n of new Set([...runtimeTop, ...devTop])) if (rangeOf(bm, n) !== rangeOf(hm, n)) changed.add(`node_modules/${n}`);

  const runtime = new Map<string, string[]>();
  const dev: string[] = [];
  for (const top of [...new Set([...runtimeTop, ...devTop])].sort()) {
    const hits = new Set<string>();
    for (const pkgs of [bl.packages, hl.packages]) {
      for (const key of treeOf(pkgs, top)) if (changed.has(key)) hits.add(nameOf(key));
    }
    if (changed.has(`node_modules/${top}`)) hits.add(top);
    if (hits.size === 0) continue;
    if (runtimeTop.has(top)) runtime.set(top, [...hits].sort());
    else dev.push(top);
  }
  return { opaque: null, runtime, dev };
}
