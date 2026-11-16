import * as fs from "node:fs";
import * as path from "node:path";
import { debug, warn } from "../core/log.js";
import { userDir } from "../core/user-dir.js";
import { readEnv } from "../core/env.js";
import { isSuperseded } from "./superseded.js";

/**
 * Read-only client for the ue-mcp plugin registry (plugins.ue-mcp.com).
 *
 * The registry is the list of published plugins, each with the repo that owns
 * its issue tracker. feedback(submit) consults it so a report about a plugin's
 * surface can be filed against that plugin instead of against ue-mcp core.
 *
 * Three rules hold everywhere in this module:
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
  category?: string;
  tags?: string[];
  provides?: string[];
  status?: string;
}

/** The plugin registry's origin: UE_MCP_REGISTRY, else the public registry. */
export function registryBase(): string {
  return (readEnv("registry") ?? "https://plugins.ue-mcp.com").replace(/\/+$/, "");
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

/**
 * Fetch the published catalog. Returns [] when the registry is unreachable
 * and no cached copy exists on this machine.
 *
 * A stale disk cache is preferred over an empty list: routing a report to the
 * plugin that owned the surface last week beats not routing it at all.
 */
export async function fetchRegistryCatalog(
  opts: { timeoutMs?: number; force?: boolean } = {},
): Promise<RegistryPlugin[]> {
  const base = registryBase();
  const timeoutMs = opts.timeoutMs ?? 4000;

  if (!opts.force && memoryCache && memoryCache.base === base && Date.now() - memoryCache.at < MemoryTtlMs) {
    return memoryCache.plugins;
  }

  try {
    const res = await fetch(`${base}/api/plugins`, {
      signal: AbortSignal.timeout(timeoutMs),
      headers: { accept: "application/json", "user-agent": "ue-mcp" },
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const json = (await res.json()) as { plugins?: RegistryPlugin[] } | RegistryPlugin[];
    const rows = Array.isArray(json) ? json : (json.plugins ?? []);
    const plugins = withoutSuperseded(rows.filter(
      (r): r is RegistryPlugin => !!r && typeof r.slug === "string" && (r.status === undefined || r.status === "published"),
    ));
    memoryCache = { at: Date.now(), base, plugins };
    writeDiskCache(base, plugins);
    return plugins;
  } catch (e) {
    const raw = readDiskCache(base);
    const cached = raw ? withoutSuperseded(raw) : null;
    if (cached) {
      debug("registry", `catalog fetch failed, using cached copy (${cached.length} plugins)`, e);
      memoryCache = { at: Date.now(), base, plugins: cached };
      return cached;
    }
    warn("registry", `catalog unavailable; feedback routing falls back to ue-mcp core`, e);
    return [];
  }
}
