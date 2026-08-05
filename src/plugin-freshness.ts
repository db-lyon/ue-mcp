// #785: warn when the compiled bridge plugin is older than the plugin source.
//
// `npx ue-mcp update` refreshes the TypeScript side and the plugin SOURCE in
// the project, but nothing recompiles the plugin. The running DLL can therefore
// be arbitrarily old while the TS schema advertises handlers it does not have.
// The symptom is an "Unknown method" error, which reads as "this feature is not
// implemented yet" - one reporter spent a session hand-authoring around
// handlers that were working fine, and nearly filed bugs for them.
//
// The .uplugin VersionName is useless for this (it reads the same in the
// package and the deployed copy regardless of age), so compare file mtimes:
// the newest handler source against the compiled binary.

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

export interface PluginFreshness {
  /** False when something could not be located; the rest is then advisory. */
  checked: boolean;
  stale: boolean;
  binaryPath?: string;
  binaryModified?: string;
  newestSourcePath?: string;
  newestSourceModified?: string;
  /** Whole days the source is ahead of the binary; 0 when fresh. */
  daysBehind?: number;
  reason?: string;
  message?: string;
}

function selfDir(): string {
  return path.dirname(fileURLToPath(import.meta.url));
}

/** Newest mtime across the plugin's .h/.cpp/.cs sources. */
function newestSource(dir: string): { file: string; mtimeMs: number } | null {
  let best: { file: string; mtimeMs: number } | null = null;
  const walk = (current: string): void => {
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(current, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) {
        // Build output, not input.
        if (entry.name === "Binaries" || entry.name === "Intermediate") continue;
        walk(full);
        continue;
      }
      if (!/\.(h|cpp|cs)$/i.test(entry.name)) continue;
      try {
        const { mtimeMs } = fs.statSync(full);
        if (!best || mtimeMs > best.mtimeMs) best = { file: full, mtimeMs };
      } catch {
        // Unreadable file: ignore, it cannot be the newest we can prove.
      }
    }
  };
  walk(dir);
  return best;
}

/** The compiled bridge module for whichever platform we are on. */
function findCompiledBinary(pluginDir: string): { file: string; mtimeMs: number } | null {
  const binariesDir = path.join(pluginDir, "Binaries");
  const candidates: string[] = [];
  const walk = (current: string): void => {
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(current, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) {
        walk(full);
        continue;
      }
      // Ignore Live Coding patch modules: they are additive over the real DLL
      // and their timestamps say nothing about the base build.
      if (/\.patch_\d+\./i.test(entry.name)) continue;
      if (/UE_MCP_Bridge.*\.(dll|dylib|so)$/i.test(entry.name)) candidates.push(full);
    }
  };
  walk(binariesDir);

  let best: { file: string; mtimeMs: number } | null = null;
  for (const file of candidates) {
    try {
      const { mtimeMs } = fs.statSync(file);
      if (!best || mtimeMs > best.mtimeMs) best = { file, mtimeMs };
    } catch {
      // ignore
    }
  }
  return best;
}

/**
 * Compare the deployed plugin's compiled binary against the plugin source
 * shipped in this npm package. Never throws: a freshness check must not be able
 * to stop the server from starting.
 */
// get_status is the first call of every session and gets polled, so the walk
// below must not run per call. Cache by project path for a short window.
const CACHE_TTL_MS = 10_000;
const cache = new Map<string, { at: number; value: PluginFreshness }>();

/**
 * Drop a cached verdict. Called after a build, which is the only thing that
 * can flip a stale verdict to fresh sooner than the TTL.
 *
 * Per project (#817): a build targets one project, and clearing every entry
 * made the other editors re-walk their plugin binaries for no reason and, on
 * a slow disk, report a verdict they had already answered. Omitting the path
 * still clears everything, which is what a caller with no project in hand
 * wants.
 */
export function invalidatePluginFreshness(uprojectPath?: string | null): void {
  if (!uprojectPath) {
    cache.clear();
    return;
  }
  cache.delete(uprojectPath);
  // The key is whatever string the caller passed to checkPluginFreshness, so
  // a resolved path and the spelling it arrived in can both be present.
  cache.delete(path.resolve(uprojectPath));
}

export function checkPluginFreshness(uprojectPath: string | null): PluginFreshness {
  if (uprojectPath) {
    const hit = cache.get(uprojectPath);
    if (hit && Date.now() - hit.at < CACHE_TTL_MS) return hit.value;
  }
  const computed = computePluginFreshness(uprojectPath);
  if (uprojectPath) cache.set(uprojectPath, { at: Date.now(), value: computed });
  return computed;
}

function computePluginFreshness(uprojectPath: string | null): PluginFreshness {
  if (!uprojectPath) {
    return { checked: false, stale: false, reason: "no project loaded" };
  }
  try {
    const deployedPluginDir = path.join(path.dirname(uprojectPath), "Plugins", "UE_MCP_Bridge");
    if (!fs.existsSync(deployedPluginDir)) {
      return { checked: false, stale: false, reason: "bridge plugin is not deployed to this project" };
    }

    const binary = findCompiledBinary(deployedPluginDir);
    if (!binary) {
      return {
        checked: true,
        stale: true,
        reason: "no compiled bridge binary found",
        message:
          "The bridge plugin source is deployed but has never been compiled. Build it (npm run build, or open the project and let the editor compile) before expecting any handler to answer.",
      };
    }

    // Compare against the source that actually got deployed, falling back to
    // the copy inside this package when the project has no Source tree.
    const deployedSource = path.join(deployedPluginDir, "Source");
    const packagedSource = path.resolve(selfDir(), "..", "plugin", "ue_mcp_bridge", "Source");
    const source =
      newestSource(fs.existsSync(deployedSource) ? deployedSource : packagedSource) ??
      newestSource(packagedSource);
    if (!source) {
      return { checked: false, stale: false, reason: "no plugin source found to compare against" };
    }

    const driftMs = source.mtimeMs - binary.mtimeMs;
    const stale = driftMs > 0;
    const daysBehind = stale ? Math.floor(driftMs / 86_400_000) : 0;

    return {
      checked: true,
      stale,
      binaryPath: binary.file,
      binaryModified: new Date(binary.mtimeMs).toISOString(),
      newestSourcePath: source.file,
      newestSourceModified: new Date(source.mtimeMs).toISOString(),
      daysBehind,
      message: stale
        ? `The compiled bridge plugin is OLDER than its source (${path.basename(source.file)} is ${daysBehind >= 1 ? `${daysBehind} day(s)` : "minutes"} newer than the binary). Handlers added since the last build will answer "Unknown method" - that is a stale build, not a missing feature. Rebuild with: npm run build (stop the editor first).`
        : undefined,
    };
  } catch (e) {
    return {
      checked: false,
      stale: false,
      reason: `freshness check failed: ${e instanceof Error ? e.message : String(e)}`,
    };
  }
}
