import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { debug, warn } from "./log.js";

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

export interface GitHubRepo {
  owner: string;
  repo: string;
}

/** ue-mcp core. Everything that is not plugin-owned lands here. */
export const CORE_REPO: GitHubRepo = { owner: "db-lyon", repo: "ue-mcp" };

/** The plugin registry's origin: UE_MCP_REGISTRY, else the public registry. */
export function registryBase(): string {
  return (process.env.UE_MCP_REGISTRY ?? "https://plugins.ue-mcp.com").replace(/\/+$/, "");
}

/**
 * Origin of the anonymous feedback signing service.
 *
 * Separate from the registry on purpose. Feedback about ue-mcp core has nothing
 * to do with the plugin catalog, and addressing it through the catalog's
 * hostname tied two unrelated surfaces together. Both names happen to be served
 * by the same deployment today, which is an operational detail, not a contract.
 *
 * `UE_MCP_FEEDBACK` overrides the origin; `UE_MCP_FEEDBACK_ENDPOINT` (read in
 * src/github-app.ts) overrides the full URL when the path differs too.
 */
export function feedbackBase(): string {
  return (process.env.UE_MCP_FEEDBACK ?? "https://feedback.ue-mcp.com").replace(/\/+$/, "");
}

/** `https://github.com/db-lyon/pie-studio(.git)` -> `{owner, repo}`. */
export function parseGitHubRepo(url: string | null | undefined): GitHubRepo | null {
  if (!url) return null;
  const cleaned = url
    .trim()
    .replace(/^git\+/, "")
    .replace(/\.git$/, "")
    .replace(/^git@github\.com:/, "https://github.com/");
  const m = cleaned.match(/github\.com[/:]([\w.-]+)\/([\w.-]+)/i);
  if (!m) return null;
  return { owner: m[1], repo: m[2] };
}

/** `owner/name` for display and comparison. */
export function repoSlug(r: GitHubRepo): string {
  return `${r.owner}/${r.repo}`;
}

export function sameRepo(a: GitHubRepo | null, b: GitHubRepo | null): boolean {
  if (!a || !b) return false;
  return a.owner.toLowerCase() === b.owner.toLowerCase() && a.repo.toLowerCase() === b.repo.toLowerCase();
}

/** Parse an `owner/name` string. Returns null for anything else. */
export function parseRepoSlug(s: string | null | undefined): GitHubRepo | null {
  if (!s) return null;
  const m = s.trim().match(/^([\w.-]+)\/([\w.-]+)$/);
  return m ? { owner: m[1], repo: m[2] } : null;
}

/**
 * Prefilled "new issue" URL. The escape hatch when the API post is refused
 * (issues disabled, private repo, bot not installed there) - the user can
 * still land the report in one click with the body already written.
 */
export function newIssueUrl(repo: GitHubRepo, title: string, body: string): string {
  // GitHub truncates very long query strings and some browsers cap the URL at
  // ~8KB. Keep the body well under that; the user can paste the rest.
  const MaxBody = 5000;
  const trimmed = body.length > MaxBody ? `${body.slice(0, MaxBody)}\n\n_(truncated - see the full report in your agent transcript)_` : body;
  const qs = `title=${encodeURIComponent(title)}&body=${encodeURIComponent(trimmed)}`;
  return `https://github.com/${repo.owner}/${repo.repo}/issues/new?${qs}`;
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
    process.env.UE_MCP_REGISTRY_CACHE ||
    path.join(os.homedir(), ".ue-mcp", "registry-catalog.json")
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
    const plugins = rows.filter(
      (r): r is RegistryPlugin => !!r && typeof r.slug === "string" && (r.status === undefined || r.status === "published"),
    );
    memoryCache = { at: Date.now(), base, plugins };
    writeDiskCache(base, plugins);
    return plugins;
  } catch (e) {
    const cached = readDiskCache(base);
    if (cached) {
      debug("registry", `catalog fetch failed, using cached copy (${cached.length} plugins)`, e);
      memoryCache = { at: Date.now(), base, plugins: cached };
      return cached;
    }
    warn("registry", `catalog unavailable; feedback routing falls back to ue-mcp core`, e);
    return [];
  }
}
