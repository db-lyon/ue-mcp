// @session independent: Loads extensions from the user's extensions directory and matches their routes. No editor is involved.
/**
 * Daemon extensions: modules the daemon loads at startup from the extensions
 * directory, each handed a versioned API. The open daemon is complete without
 * any; an extension adds HTTP routes under /v1/ext/<name>/, reads and
 * publishes events, runs actions in process, keeps its own data directory, and
 * may trust a key for UI bundles.
 *
 * An extension is a directory holding a package.json whose `main` exports
 * `activate(api)` and whose `ueMcpExtension` field names the extension API
 * versions it supports: `{ "name": "agent", "api": { "min": 1, "max": 1 } }`.
 * Versions sit side by side and each project loads its own (extension-versions.ts).
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { info, warn } from "../core/log.js";
import { ENV_VARS, readEnv } from "../core/env.js";
import { userDir } from "../core/user-dir.js";
import { EXTENSION_PERMISSIONS, type DaemonExtension, type DaemonExtensionApi, type ExtensionHandler, type ExtensionManifest, type ExtensionPermission, type ExtensionResponse, type RefusedExtension } from "../contract/daemon.js";
import { extensionRefusal } from "./extension-manifest.js";
import { packageVersion } from "../core/package-root.js";
import {
  chooseVersion,
  installedCopies,
  isLeftover,
  migrateLegacy,
  projectUsedExtension,
  recordedVersion,
  recordVersion,
  type ExtensionCopy,
} from "./extension-versions.js";

export function extensionsDir(): string {
  return readEnv("extensionsDir") ?? path.join(userDir(), "extensions");
}

type Refusal = NonNullable<ReturnType<typeof extensionRefusal>>;

/**
 * The version gate install and load share: extensionRefusal, except that
 * UE_MCP_EXTENSION_FLOOR=warn lets an extension needing a newer ue-mcp through
 * (a development daemon whose package version trails its code). `waived` is
 * the refusal let through that way, for the caller to warn about.
 */
export function extensionGate(name: string, ext: ExtensionManifest | undefined, installed: string = packageVersion()): { refusal: Refusal | null; waived: Refusal | null } {
  const refusal = extensionRefusal(name, ext, installed);
  if (refusal?.needsUeMcp && readEnv("extensionFloor") === "warn") return { refusal: null, waived: refusal };
  return { refusal, waived: null };
}

interface Route {
  method: string;
  path: string;
  handler: ExtensionHandler;
}

export interface LoadedExtension {
  name: string;
  version: string;
  /** The oldest UI bundle version it works with. */
  uiMin?: string;
  /** false when its bundles stay out of /ui/ (`ueMcpExtension.ui.root`). */
  uiRoot?: false;
  routes: Route[];
  busy: () => boolean;
  dispose?: () => void | Promise<void>;
}

interface ExtensionPackage {
  dir: string;
  name: string;
  version: string;
  main: string;
  uiMin?: string;
  uiRoot?: false;
  permissions?: ExtensionPermission[];
}

/** The declared permissions this daemon knows (undefined when the manifest declares none at all); an unknown one is named in the log and ignored. */
function declaredPermissions(name: string, declared: unknown): ExtensionPermission[] | undefined {
  if (declared === undefined) return undefined;
  const list = Array.isArray(declared) ? declared : [declared];
  const known = list.filter((p): p is ExtensionPermission => (EXTENSION_PERMISSIONS as readonly unknown[]).includes(p));
  const unknown = list.filter((p) => !known.includes(p as ExtensionPermission));
  if (unknown.length) {
    warn("extension", `${name} declares permissions this ue-mcp does not know, ignored: ${unknown.map(String).join(", ")} (known: ${EXTENSION_PERMISSIONS.join(", ")})`);
  }
  return known;
}

interface PackageJson {
  version?: string; main?: string; ueMcpExtension?: ExtensionManifest;
}

/**
 * The one copy of each extension this project loads (extension-versions.ts):
 * its recorded version, which is recorded on first load. Versions this
 * ue-mcp cannot load are passed over, and named in `refused` only when no
 * version of that extension loads.
 */
function readPackages(root: string, installed: string, refused: RefusedExtension[], projectDir?: string): ExtensionPackage[] {
  if (!fs.existsSync(root)) return [];
  migrateLegacy(root);
  warnForeignFolders(root);
  const out: ExtensionPackage[] = [];
  for (const [name, copies] of installedCopies(root)) {
    const usable = new Map<string, { copy: ExtensionCopy; pkg: PackageJson }>();
    const refusals = new Map<string, { reason: string; needsUeMcp?: string }>();
    for (const copy of copies) {
      let pkg: PackageJson;
      try {
        pkg = JSON.parse(fs.readFileSync(path.join(copy.dir, "package.json"), "utf-8")) as PackageJson;
      } catch (e) {
        warn("extension", `cannot read ${copy.dir}/package.json`, e);
        continue;
      }
      if (!pkg.main) continue;
      const { refusal, waived } = extensionGate(name, pkg.ueMcpExtension, installed);
      if (refusal) {
        refusals.set(copy.version, refusal);
        continue;
      }
      if (waived) warn("extension", `${waived.reason}. Loaded anyway (${ENV_VARS.extensionFloor}=warn).`);
      const held = usable.get(copy.version);
      if (!held || held.copy.legacy) usable.set(copy.version, { copy, pkg });
    }
    const recorded = projectDir ? recordedVersion(projectDir, name) : null;
    const legacy = copies.find((c) => c.legacy);
    const choice = chooseVersion(
      [...usable.keys()],
      recorded,
      legacy && projectDir ? { version: legacy.version, usedBefore: projectUsedExtension(projectDir, name) } : null,
    );
    if (!choice) {
      const version = recorded && refusals.has(recorded) ? recorded : [...refusals.keys()].at(-1);
      const refusal = version ? refusals.get(version) : undefined;
      if (version && refusal) {
        warn("extension", `${refusal.reason}. Not loaded.`);
        refused.push({ name, version, ...refusal });
      }
      continue;
    }
    if (recorded && recorded !== choice.version) {
      warn("extension", `${name} ${recorded} is recorded for this project but does not load here; loading ${choice.version}`);
    }
    if (choice.record && projectDir) recordVersion(projectDir, name, choice.version);
    const { copy, pkg } = usable.get(choice.version)!;
    const uiMin = typeof pkg.ueMcpExtension?.ui?.min === "string" ? pkg.ueMcpExtension.ui.min : undefined;
    const uiRoot = pkg.ueMcpExtension?.ui?.root === false ? { uiRoot: false as const } : {};
    const permissions = declaredPermissions(name, pkg.ueMcpExtension?.permissions);
    out.push({ dir: copy.dir, name, version: choice.version, main: path.join(copy.dir, pkg.main!), ...(uiMin ? { uiMin } : {}), ...uiRoot, ...(permissions ? { permissions } : {}) });
  }
  return out;
}

/** Say which folders hold an extension under a name that is not theirs, so they never load. */
function warnForeignFolders(root: string): void {
  for (const e of fs.readdirSync(root, { withFileTypes: true })) {
    if (!e.isDirectory() || isLeftover(e.name)) continue;
    try {
      const pkg = JSON.parse(fs.readFileSync(path.join(root, e.name, "package.json"), "utf-8")) as PackageJson;
      const name = pkg.ueMcpExtension?.name;
      if (!name || !pkg.main || e.name === name || e.name === `${name}@${pkg.version ?? "0.0.0"}`) continue;
      warn("extension", `skipping ${path.join(root, e.name)}: it holds ${name} ${pkg.version ?? "0.0.0"}, which loads from ${path.join(root, `${name}@${pkg.version ?? "0.0.0"}`)}`);
    } catch {
      // No package.json: not an extension.
    }
  }
}

/** Load every extension, handing each an API built by `apiFor`. A failing one is named and skipped. */
export async function loadExtensions(
  apiFor: (name: string, routes: Route[], setBusy: (fn: () => boolean) => void, pkg: { version: string; uiMin?: string; uiRoot?: false; permissions?: readonly ExtensionPermission[] }) => DaemonExtensionApi,
  root: string = extensionsDir(),
  refused: RefusedExtension[] = [],
  installed: string = packageVersion(),
  /** The project whose recorded versions load; without one, the newest of each loads and nothing is recorded. */
  projectDir?: string,
): Promise<LoadedExtension[]> {
  const loaded: LoadedExtension[] = [];
  for (const pkg of readPackages(root, installed, refused, projectDir)) {
    const routes: Route[] = [];
    let busy: () => boolean = () => false;
    try {
      const mod = (await import(pathToFileURL(pkg.main).href)) as Partial<DaemonExtension> & { default?: Partial<DaemonExtension> };
      const activate = mod.activate ?? mod.default?.activate;
      if (typeof activate !== "function") throw new Error("exports no activate(api)");
      const ui = { ...(pkg.uiMin ? { uiMin: pkg.uiMin } : {}), ...(pkg.uiRoot === false ? { uiRoot: false as const } : {}) };
      const handle = await activate(apiFor(pkg.name, routes, (fn) => (busy = fn), { version: pkg.version, ...ui, ...(pkg.permissions ? { permissions: pkg.permissions } : {}) }));
      loaded.push({ name: pkg.name, version: pkg.version, ...ui, routes, busy: () => busy(), dispose: handle?.dispose });
      info("extension", `loaded ${pkg.name} ${pkg.version}`);
    } catch (e) {
      warn("extension", `${pkg.name} failed to load`, e);
    }
  }
  return loaded;
}

/** Find the route for an /v1/ext/<name>/<path> request. */
export function matchRoute(
  extensions: readonly LoadedExtension[],
  method: string,
  pathname: string,
): { ext: LoadedExtension; route: Route; params: Record<string, string> } | null {
  const m = /^\/v1\/ext\/([^/]+)(\/.*)?$/.exec(pathname);
  if (!m) return null;
  const ext = extensions.find((e) => e.name === m[1]);
  if (!ext) return null;
  const sub = m[2] ?? "/";
  for (const route of ext.routes) {
    if (route.method !== method) continue;
    const params = matchPath(route.path, sub);
    if (params) return { ext, route, params };
  }
  return null;
}

/** `/sessions/:id` against `/sessions/42` gives `{ id: "42" }`. */
function matchPath(pattern: string, actual: string): Record<string, string> | null {
  const p = pattern.split("/").filter(Boolean);
  const a = actual.split("/").filter(Boolean);
  if (p.length !== a.length) return null;
  const params: Record<string, string> = {};
  for (let i = 0; i < p.length; i++) {
    if (p[i].startsWith(":")) params[p[i].slice(1)] = decodeURIComponent(a[i]);
    else if (p[i] !== a[i]) return null;
  }
  return params;
}

export function normalizeResponse(r: ExtensionResponse | undefined): { status: number; headers: Record<string, string>; body: string | Buffer } {
  if (!r) return { status: 204, headers: {}, body: "" };
  const status = r.status ?? 200;
  if (r.body === undefined) return { status, headers: r.headers ?? {}, body: "" };
  if (typeof r.body === "string" || Buffer.isBuffer(r.body)) return { status, headers: r.headers ?? {}, body: r.body };
  return { status, headers: { "content-type": "application/json", ...(r.headers ?? {}) }, body: JSON.stringify(r.body) };
}
