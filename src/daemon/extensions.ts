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
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { info, warn } from "../core/log.js";
import { readEnv } from "../core/env.js";
import { userDir } from "../core/user-dir.js";
import type { DaemonExtension, DaemonExtensionApi, ExtensionHandler, ExtensionResponse } from "./extension-api.js";
import { EXTENSION_API_VERSION } from "./extension-api.js";

export function extensionsDir(): string {
  return readEnv("extensionsDir") ?? path.join(userDir(), "extensions");
}

interface Route {
  method: string;
  path: string;
  handler: ExtensionHandler;
}

export interface LoadedExtension {
  name: string;
  version: string;
  routes: Route[];
  busy: () => boolean;
  dispose?: () => void | Promise<void>;
}

interface ExtensionPackage {
  dir: string;
  name: string;
  version: string;
  main: string;
}

function readPackages(root: string): ExtensionPackage[] {
  if (!fs.existsSync(root)) return [];
  const out: ExtensionPackage[] = [];
  for (const e of fs.readdirSync(root, { withFileTypes: true })) {
    if (!e.isDirectory()) continue;
    const dir = path.join(root, e.name);
    try {
      const pkg = JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf-8")) as {
        version?: string; main?: string; ueMcpExtension?: { name?: string; api?: { min?: number; max?: number } };
      };
      const ext = pkg.ueMcpExtension;
      if (!ext?.name || !pkg.main) continue;
      const min = ext.api?.min ?? 1;
      const max = ext.api?.max ?? min;
      if (EXTENSION_API_VERSION < min || EXTENSION_API_VERSION > max) {
        warn("extension", `${ext.name} supports extension API ${min}..${max}; this daemon has ${EXTENSION_API_VERSION}. Not loaded.`);
        continue;
      }
      out.push({ dir, name: ext.name, version: pkg.version ?? "0.0.0", main: path.join(dir, pkg.main) });
    } catch (e) {
      warn("extension", `cannot read ${dir}/package.json`, e);
    }
  }
  return out;
}

/** Load every extension, handing each an API built by `apiFor`. A failing one is named and skipped. */
export async function loadExtensions(
  apiFor: (name: string, routes: Route[], setBusy: (fn: () => boolean) => void) => DaemonExtensionApi,
  root: string = extensionsDir(),
): Promise<LoadedExtension[]> {
  const loaded: LoadedExtension[] = [];
  for (const pkg of readPackages(root)) {
    const routes: Route[] = [];
    let busy: () => boolean = () => false;
    try {
      const mod = (await import(pathToFileURL(pkg.main).href)) as Partial<DaemonExtension> & { default?: Partial<DaemonExtension> };
      const activate = mod.activate ?? mod.default?.activate;
      if (typeof activate !== "function") throw new Error("exports no activate(api)");
      const handle = await activate(apiFor(pkg.name, routes, (fn) => (busy = fn)));
      loaded.push({ name: pkg.name, version: pkg.version, routes, busy: () => busy(), dispose: handle?.dispose });
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
