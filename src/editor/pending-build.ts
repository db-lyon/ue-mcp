/**
 * A source bridge whose new source is deployed and not yet built (#1593).
 *
 * Unreal rebuilds a module on start only when its DLL is missing or was built
 * for another engine, never because its source is newer, so an update that
 * deploys source with the editor running would otherwise be loaded as the old
 * DLL on every restart. The marker records that a build is owed; the next
 * start through ue-mcp builds first, and any successful build clears it.
 *
 * A build of that source that failed is recorded on the marker. A start then
 * refuses once with the error, and after that opens the previous build rather
 * than paying the same failing build on every launch; the next deploy or a
 * passing build resets it.
 *
 * It lives under the plugin's Intermediate/, which a redeploy keeps.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { resolvePluginDir } from "./install-marker.js";

export const PENDING_BUILD_NAME = "ue-mcp-build-pending.json";

export interface PendingBuild {
  /** The bridge version whose source was deployed. */
  version: string;
  /** When the build was put off. */
  at: string;
  /** Why it was put off, e.g. "editor running". */
  reason: string;
  /** Set once a build of this source failed. */
  failed?: { at: string; message: string };
}

function pluginDirOf(uprojectPath: string): string | null {
  return resolvePluginDir(path.dirname(path.resolve(uprojectPath)))?.dir ?? null;
}

export function pendingBuildPath(pluginDir: string): string {
  return path.join(pluginDir, "Intermediate", PENDING_BUILD_NAME);
}

export function readPendingBuild(uprojectPath: string): PendingBuild | null {
  const dir = pluginDirOf(uprojectPath);
  if (!dir) return null;
  try {
    const parsed = JSON.parse(fs.readFileSync(pendingBuildPath(dir), "utf-8")) as PendingBuild;
    return parsed && typeof parsed.version === "string" ? parsed : null;
  } catch {
    return null;
  }
}

export function recordPendingBuild(uprojectPath: string, version: string, reason: string): void {
  const dir = pluginDirOf(uprojectPath);
  if (!dir) return;
  const file = pendingBuildPath(dir);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const entry: PendingBuild = { version, at: new Date().toISOString(), reason };
  fs.writeFileSync(file, JSON.stringify(entry, null, 2));
}

/** Record on an owed build that building it failed. Does nothing when no build is owed. */
export function markPendingBuildFailed(uprojectPath: string, message: string): void {
  try {
    const dir = pluginDirOf(uprojectPath);
    const owed = readPendingBuild(uprojectPath);
    if (!dir || !owed) return;
    const entry: PendingBuild = { ...owed, failed: { at: new Date().toISOString(), message } };
    fs.writeFileSync(pendingBuildPath(dir), JSON.stringify(entry, null, 2));
  } catch {
    // The failure is still reported by whoever ran the build.
  }
}

export function clearPendingBuild(uprojectPath: string): void {
  try {
    const dir = pluginDirOf(uprojectPath);
    if (dir) fs.rmSync(pendingBuildPath(dir), { force: true });
  } catch {
    // A marker that cannot be removed only costs one more build at the next start.
  }
}
