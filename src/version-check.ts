/**
 * Background check for newer ue-mcp releases on npm.
 *
 * MCP clients (Claude Code, Claude Desktop) don't currently render server
 * `notifications/message` or stderr to the user, so the only reliable way to
 * surface an upgrade hint is to inject it into a tool response - the agent
 * then becomes the messenger and tells the user conversationally.
 *
 * On startup we async-fetch the latest version from the npm registry, cache
 * the result in the OS temp dir for 24h, and stash a notice if a newer
 * release exists. The next tool response calls `consumeUpgradeNotice()`,
 * which returns the notice once and then clears it - one nudge per session.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { warn, debug } from "./log.js";

const CACHE_FILE = path.join(os.tmpdir(), "ue-mcp-version-check.json");
const CACHE_TTL_MS = 24 * 60 * 60 * 1000;
const FETCH_TIMEOUT_MS = 5000;
const REGISTRY_URL = "https://registry.npmjs.org/ue-mcp/latest";

interface CacheEntry {
  checkedAt: number;
  latest: string | null;
}

let pendingNotice: string | null = null;

function readCache(): CacheEntry | null {
  try {
    const raw = fs.readFileSync(CACHE_FILE, "utf8");
    const parsed = JSON.parse(raw) as CacheEntry;
    if (typeof parsed.checkedAt !== "number") return null;
    return parsed;
  } catch {
    return null;
  }
}

function writeCache(entry: CacheEntry): void {
  try {
    fs.writeFileSync(CACHE_FILE, JSON.stringify(entry));
  } catch {
    // best-effort; cache miss next run is fine
  }
}

async function fetchLatest(): Promise<string | null> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
  try {
    const res = await fetch(REGISTRY_URL, { signal: ctrl.signal });
    if (!res.ok) return null;
    const body = (await res.json()) as { version?: unknown };
    return typeof body.version === "string" ? body.version : null;
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

function parseVersion(v: string): [number, number, number, string] {
  const m = /^(\d+)\.(\d+)\.(\d+)(?:-(.+))?$/.exec(v.trim());
  if (!m) return [0, 0, 0, ""];
  return [Number(m[1]), Number(m[2]), Number(m[3]), m[4] ?? ""];
}

export function isNewer(latest: string, current: string): boolean {
  const [la, lb, lc, lp] = parseVersion(latest);
  const [ca, cb, cc, cp] = parseVersion(current);
  if (la !== ca) return la > ca;
  if (lb !== cb) return lb > cb;
  if (lc !== cc) return lc > cc;
  // Same x.y.z: a non-prerelease beats a prerelease, otherwise lex compare.
  if (lp === "" && cp !== "") return true;
  if (lp !== "" && cp === "") return false;
  return lp > cp;
}

function buildNotice(current: string, latest: string): string {
  return [
    `UE_MCP_UPGRADE_AVAILABLE: a newer ue-mcp release is published on npm.`,
    `  installed: ${current}`,
    `  latest:    ${latest}`,
    `  upgrade:   npm i -g ue-mcp@latest`,
    `Please tell the user about this upgrade once, then continue with their request.`,
  ].join("\n");
}

/**
 * Kick off the version check. Returns immediately; on a positive result it
 * stashes a notice that the next tool response will pick up. Never throws.
 *
 * Set `UE_MCP_DISABLE_UPDATE_CHECK=1` to suppress entirely (offline use,
 * locked-down environments, tests).
 */
export function startVersionCheck(currentVersion: string): void {
  if (process.env.UE_MCP_DISABLE_UPDATE_CHECK === "1") return;

  void (async () => {
    try {
      const cache = readCache();
      const now = Date.now();
      let latest: string | null;

      if (cache && now - cache.checkedAt < CACHE_TTL_MS) {
        latest = cache.latest;
        debug("update", `cached latest=${latest ?? "null"} (age ${Math.round((now - cache.checkedAt) / 1000)}s)`);
      } else {
        latest = await fetchLatest();
        writeCache({ checkedAt: now, latest });
        debug("update", `fetched latest=${latest ?? "null"}`);
      }

      if (latest && isNewer(latest, currentVersion)) {
        pendingNotice = buildNotice(currentVersion, latest);
        warn(
          "update",
          `newer version available: ${currentVersion} -> ${latest} (npm i -g ue-mcp@latest)`,
        );
      }
    } catch (e) {
      warn("update", "version check failed", e);
    }
  })();
}

/**
 * Returns the upgrade notice once if one is pending, then clears it.
 * Tool dispatchers call this on every response and prepend the result
 * to the response content blocks when non-null.
 */
export function consumeUpgradeNotice(): string | null {
  if (pendingNotice === null) return null;
  const out = pendingNotice;
  pendingNotice = null;
  return out;
}

/** Test hook. */
export function _resetForTests(): void {
  pendingNotice = null;
}

/** Test hook. */
export function _setNoticeForTests(notice: string | null): void {
  pendingNotice = notice;
}
