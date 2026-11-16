// @session independent: The plugin registry itself, which no project owns.
import * as fs from "node:fs";
import * as path from "node:path";
import { debug, warn } from "../core/log.js";
import { userDir } from "../core/user-dir.js";
import { readEnv } from "../core/env.js";
import { isSuperseded } from "./superseded.js";

/**
 * Read-only client for the ue-mcp plugin registry (plugins.ue-mcp.com): the
 * one client feedback routing, the daemon's catalog route and `plugin publish`
 * all read it through.
 *
 * The registry is the list of published plugins, each with the repo that owns
 * its issue tracker. feedback(submit) consults it so a report about a plugin's
 * surface can be filed against that plugin instead of against ue-mcp core.
 *
 * The cached catalog (fetchRegistryCatalog) holds three rules:
 *
 *   1. Never throw. The registry is a convenience layer. A DNS failure, a
 *      500, or an offline laptop must degrade to "no candidates", never to a
 *      failed feedback submission.
 *   2. Never block for long. A short timeout keeps the elicitation prompt
 *      responsive; the whole point is a sub-second detour.
 *   3. Cache on disk. An offline session still routes correctly using the
 *      last catalog this machine saw.
 */

export interface RegistryPlugin {
  slug: string;
  name: string;
  packageName?: string;
  repoUrl?: string;
  repoPrivate?: boolean;
  homepageUrl?: string;
  tagline?: string;
  description?: string;
  readme?: string;
  category?: string;
  tags?: string[];
  provides?: string[];
  status?: string;
}

/** The plugin registry's origin: UE_MCP_REGISTRY, else the public registry. */
export function registryBase(): string {
  return (readEnv("registry") ?? "https://plugins.ue-mcp.com").replace(/\/+$/, "");
}

/** How a fetch can be tuned or replaced (tests hand in their own `fetchImpl`). */
export interface RegistryFetchOptions {
  timeoutMs?: number;
  fetchImpl?: typeof fetch;
}

/**
 * Every row the registry's /api/plugins answers, unfiltered. Throws when the
 * registry cannot be reached or answers an error; `plugin publish` reads this
 * to merge over curated fields.
 */
export async function fetchCatalogRows<Row extends { slug: string } = RegistryPlugin>(opts: RegistryFetchOptions = {}): Promise<Row[]> {
  const res = await (opts.fetchImpl ?? fetch)(`${registryBase()}/api/plugins`, {
    signal: AbortSignal.timeout(opts.timeoutMs ?? 4000),
    headers: { accept: "application/json", "user-agent": "ue-mcp" },
  });
  if (!res.ok) throw new Error(`the plugin registry answered HTTP ${res.status}`);
  const json = (await res.json()) as { plugins?: Row[] } | Row[];
  const rows = Array.isArray(json) ? json : (json.plugins ?? []);
  return rows.filter((r): r is Row => !!r && typeof r.slug === "string");
}

/* ── caching ───────────────────────────────────────────────────────── */

const MemoryTtlMs = 15 * 60 * 1000;

interface CacheFile {
  fetchedAt: string;
  base: string;
  plugins: RegistryPlugin[];
}

let memoryCache: { at: number; base: string; plugins: RegistryPlugin[] } | null = null;

function cachePath(): string {
  return (
    readEnv("registryCache") ||
    path.join(userDir(), "registry-catalog.json")
  );
}

function readDiskCache(base: string): RegistryPlugin[] | null {
  try {
    const raw = fs.readFileSync(cachePath(), "utf-8");
    const parsed = JSON.parse(raw) as CacheFile;
    if (parsed.base !== base || !Array.isArray(parsed.plugins)) return null;
    return parsed.plugins;
  } catch {
    return null;
  }
}

function writeDiskCache(base: string, plugins: RegistryPlugin[]): void {
  try {
    const file = cachePath();
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const payload: CacheFile = { fetchedAt: new Date().toISOString(), base, plugins };
    fs.writeFileSync(file, JSON.stringify(payload, null, 2));
  } catch (e) {
    debug("registry", "could not write catalog cache", e);
  }
}

/** Drop plugins that ship in core now, so nothing offers or routes to them. */
function withoutSuperseded(plugins: RegistryPlugin[]): RegistryPlugin[] {
  return plugins.filter((p) => !isSuperseded(p.slug) && !(p.packageName && isSuperseded(p.packageName)));
}

/** The published catalog, and where it came from; `error` says why the registry was not read. */
export interface RegistryCatalog {
  plugins: RegistryPlugin[];
  source: "registry" | "memory" | "disk" | "none";
  error?: string;
}

/**
 * The published catalog, from memory for 15 minutes, else the registry, else
 * the last copy this machine saved. `source: "none"` with an empty list when
 * none of them has one. Never throws.
 *
 * A stale disk cache is preferred over an empty list: routing a report to the
 * plugin that owned the surface last week beats not routing it at all.
 */
export async function loadRegistryCatalog(opts: RegistryFetchOptions & { force?: boolean } = {}): Promise<RegistryCatalog> {
  const base = registryBase();
  if (!opts.force && memoryCache && memoryCache.base === base && Date.now() - memoryCache.at < MemoryTtlMs) {
    return { plugins: memoryCache.plugins, source: "memory" };
  }
  try {
    const rows = await fetchCatalogRows<RegistryPlugin>(opts);
    const plugins = withoutSuperseded(rows.filter((r) => r.status === undefined || r.status === "published"));
    memoryCache = { at: Date.now(), base, plugins };
    writeDiskCache(base, plugins);
    return { plugins, source: "registry" };
  } catch (e) {
    const error = e instanceof Error ? e.message : String(e);
    const raw = readDiskCache(base);
    const cached = raw ? withoutSuperseded(raw) : null;
    if (cached) {
      debug("registry", `catalog fetch failed, using cached copy (${cached.length} plugins)`, e);
      memoryCache = { at: Date.now(), base, plugins: cached };
      return { plugins: cached, source: "disk", error };
    }
    return { plugins: [], source: "none", error };
  }
}

/** The published catalog as loadRegistryCatalog reads it, or [] when there is none. */
export async function fetchRegistryCatalog(opts: RegistryFetchOptions & { force?: boolean } = {}): Promise<RegistryPlugin[]> {
  const catalog = await loadRegistryCatalog(opts);
  if (catalog.source === "none") warn("registry", `catalog unavailable; feedback routing falls back to ue-mcp core`, catalog.error);
  return catalog.plugins;
}

/** Forget the in-memory catalog, so the next read goes to the registry. */
export function resetRegistryCatalogMemory(): void {
  memoryCache = null;
}
