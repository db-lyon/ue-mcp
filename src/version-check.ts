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

// Strict semver: the registry value is interpolated into a string the agent
// reads as a system-level message, so anything that doesn't match this shape
// is dropped to close a prompt-injection channel through a poisoned response.
const STRICT_SEMVER_RE = /^(\d{1,8})\.(\d{1,8})\.(\d{1,8})(?:-[A-Za-z0-9.-]{1,32})?$/;

async function fetchLatest(): Promise<string | null> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
  try {
    const res = await fetch(REGISTRY_URL, { signal: ctrl.signal });
    if (!res.ok) return null;
    const body = (await res.json()) as { version?: unknown };
    if (typeof body.version !== "string") return null;
    if (!STRICT_SEMVER_RE.test(body.version)) {
      warn("update", `registry returned non-semver version; dropping`);
      return null;
    }
    return body.version;
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Release channel helpers.
 *
 * These mirror the rules in scripts/release-version.mjs, which the publish job
 * uses to pick the npm dist-tag. The two cannot be one module: the publish job
 * runs before tsc has produced dist/, and the package ships only dist/ and
 * plugin/, so neither side can import the other. The parity test in
 * tests/unit/release-channel-parity.test.ts holds them together.
 *
 * Both degrade to the stable channel on an unparseable version instead of
 * throwing. These sit in CLI and startup paths where a crash is worse than a
 * conservative answer.
 */
const CHANNEL_SEMVER_RE =
  /^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z][0-9A-Za-z.-]*))?(?:\+[0-9A-Za-z][0-9A-Za-z.-]*)?$/;
const SAFE_TAG_RE = /^[A-Za-z][0-9A-Za-z._-]*$/;
const FALLBACK_TAG = "next";
const STABLE_TAG = "latest";

function prereleaseIdOf(version: string): string | null {
  const m = CHANNEL_SEMVER_RE.exec(String(version ?? "").trim());
  return m ? (m[4] ?? null) : null;
}

/** True when the version carries a prerelease identifier (1.2.0-beta.2). */
export function isPrereleaseVersion(version: string): boolean {
  return prereleaseIdOf(version) !== null;
}

/** The npm dist-tag a version belongs to: `latest` for X.Y.Z, else its channel. */
export function distTagForVersion(version: string): string {
  const pre = prereleaseIdOf(version);
  if (pre === null) return STABLE_TAG;
  const first = pre.split(".")[0];
  if (!SAFE_TAG_RE.test(first)) return FALLBACK_TAG;
  if (first.toLowerCase() === STABLE_TAG) return FALLBACK_TAG;
  return first;
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

/**
 * The version an install should move to, or null when it is already there.
 *
 * Only ever moves forward, and never crosses from the stable line onto a
 * prerelease. Comparing installed against latest for plain inequality treats
 * "ahead of the stable line" as "out of date", which rolls a prerelease tester
 * back onto an older stable on their next update, and it would install a
 * prerelease on every stable user the moment `latest` pointed at one.
 */
export function resolveUpdateTarget(installed: string, latest: string): string | null {
  if (isPrereleaseVersion(latest) && !isPrereleaseVersion(installed)) return null;
  return isNewer(latest, installed) ? latest : null;
}

function buildNotice(current: string, latest: string): string {
  return [
    `UE_MCP_UPGRADE_AVAILABLE: a newer ue-mcp release is published on npm.`,
    `  installed: ${current}`,
    `  latest:    ${latest}`,
    `  upgrade:   ue-mcp update --build  (or: npm i -g ue-mcp@latest)`,
    `  diagnose:  ue-mcp doctor  (flags a local node_modules/ue-mcp shadowing the global install)`,
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

      // A stable install is never nudged onto a prerelease. The registry's
      // `latest` should only ever name a plain X.Y.Z, but this is the one
      // place the answer reaches the user as an instruction, so the check does
      // not rely on that holding. A prerelease install still gets told about a
      // newer stable, which is the release that supersedes it.
      if (latest && isPrereleaseVersion(latest) && !isPrereleaseVersion(currentVersion)) {
        debug("update", `registry latest ${latest} is a prerelease; not offering it to a stable install`);
      } else if (latest && isNewer(latest, currentVersion)) {
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
