/**
 * Prebuilt bridge binaries: where they come from and how they land in a project.
 *
 * A machine with no C++ toolchain cannot compile the bridge, so it gets the
 * plugin prebuilt for its engine and platform instead of source. The archive is
 * a plugin directory with no Source tree. It is staged in the update work
 * directory, checked, and swapped in by rename, so a failed download or a
 * broken archive never leaves half a plugin.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { readEnv } from "../core/env.js";
import { BRIDGE_PLUGIN_NAME, writeInstallMarker, type InstallMarker } from "./install-marker.js";
import { treeFiles, verifyBridgeTree } from "./bridge-tree.js";
import { isPidAlive } from "../bridge/editor-target.js";

export interface BinaryTarget {
  version: string;
  /** Engine major.minor, e.g. "5.7". */
  engine: string;
  /** Win64, Mac or Linux. */
  platform: string;
}

export const DEFAULT_BINARIES_BASE = "https://github.com/db-lyon/ue-mcp/releases/download";

export function binaryArchiveName(t: BinaryTarget): string {
  return `${BRIDGE_PLUGIN_NAME}-${t.version}-UE${t.engine}-${t.platform}.zip`;
}

/** The release asset listing every archive's SHA-256, beside the archives. */
export function binaryManifestName(version: string): string {
  return `${BRIDGE_PLUGIN_NAME}-${version}-manifest.json`;
}

export interface BinaryManifest {
  version: string;
  assets: Array<{ engine: string; platform: string; file: string; sha256: string; size: number }>;
}

export function binaryArchiveUrl(t: BinaryTarget, base: string = DEFAULT_BINARIES_BASE): string {
  return `${base.replace(/\/+$/, "")}/v${t.version}/${binaryArchiveName(t)}`;
}

export type BinarySource =
  | { kind: "dir"; path: string }
  | { kind: "zip"; path: string }
  | { kind: "url"; url: string };

/**
 * Where the binaries for `t` come from. An explicit value (flag, then
 * UE_MCP_BINARIES) may be a plugin directory, a .zip, a directory of archives,
 * or a URL base; otherwise the GitHub release for the version.
 */
export function resolveBinarySource(t: BinaryTarget, explicit?: string | null): BinarySource {
  const given = explicit ?? readEnv("binaries");
  if (!given) return { kind: "url", url: binaryArchiveUrl(t) };
  if (/^https?:\/\//i.test(given)) {
    return given.toLowerCase().endsWith(".zip") ? { kind: "url", url: given } : { kind: "url", url: binaryArchiveUrl(t, given) };
  }
  const resolved = path.resolve(given);
  if (resolved.toLowerCase().endsWith(".zip")) return { kind: "zip", path: resolved };
  const archive = path.join(resolved, binaryArchiveName(t));
  if (fs.existsSync(archive)) return { kind: "zip", path: archive };
  return { kind: "dir", path: resolved };
}

export function describeBinarySource(s: BinarySource): string {
  return s.kind === "url" ? s.url : s.path;
}

/** The plugin root inside an extracted tree: itself, or its UE_MCP_Bridge child. */
function pluginRootIn(dir: string): string | null {
  const descriptor = `${BRIDGE_PLUGIN_NAME}.uplugin`;
  if (fs.existsSync(path.join(dir, descriptor))) return dir;
  const nested = path.join(dir, BRIDGE_PLUGIN_NAME);
  return fs.existsSync(path.join(nested, descriptor)) ? nested : null;
}

async function download(url: string, dest: string, t: BinaryTarget): Promise<void> {
  const res = await fetch(url);
  if (res.status === 404) {
    throw new Error(`no prebuilt bridge for UE ${t.engine} ${t.platform} in ue-mcp ${t.version} (${url}). Build from source with a C++ toolchain, or use a release that ships one.`);
  }
  if (!res.ok) throw new Error(`download failed (${res.status} ${res.statusText}): ${url}`);
  fs.writeFileSync(dest, Buffer.from(await res.arrayBuffer()));
}

/**
 * Check a downloaded archive against the manifest published beside it. A
 * download is never installed unverified: no manifest, no entry, or a
 * different hash all refuse.
 */
export async function verifyDownload(zipUrl: string, zipPath: string, t: BinaryTarget): Promise<void> {
  const manifestUrl = zipUrl.replace(/[^/]+$/, binaryManifestName(t.version));
  const res = await fetch(manifestUrl);
  if (!res.ok) throw new Error(`no binary manifest at ${manifestUrl} (${res.status}); refusing to install unverified binaries`);
  const manifest = (await res.json()) as BinaryManifest;
  const name = zipUrl.split("/").pop() ?? "";
  const entry = manifest.assets?.find((a) => a.file === name);
  if (!entry) throw new Error(`${name} is not in ${manifestUrl}; refusing to install unverified binaries`);
  const actual = crypto.createHash("sha256").update(fs.readFileSync(zipPath)).digest("hex");
  if (actual !== entry.sha256.toLowerCase()) {
    throw new Error(`${name} does not match its manifest hash (expected ${entry.sha256}, got ${actual}); refusing to install it`);
  }
}

/**
 * bsdtar reads zip and ships with Windows 10+ and macOS. On Windows it is named
 * by path, because a Git Bash PATH puts GNU tar first and GNU tar cannot.
 */
function extractZip(zip: string, into: string): void {
  fs.mkdirSync(into, { recursive: true });
  const tar = process.platform === "win32"
    ? path.join(process.env.SystemRoot ?? process.env.SYSTEMROOT ?? "C:\\Windows", "System32", "tar.exe")
    : "tar";
  try {
    execFileSync(tar, ["-xf", zip, "-C", into], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
  } catch {
    execFileSync("unzip", ["-q", "-o", zip, "-d", into], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
  }
}

export interface BinaryInstallResult {
  pluginDir: string;
  from: string;
  marker: InstallMarker;
}

export interface StagedBinaries {
  /** A complete plugin tree in the update work directory, marker written, ready to swap in. */
  staging: string;
  from: string;
  marker: InstallMarker;
  /** Every file the release ships, relative to the plugin root, marker included. */
  files: string[];
}

/**
 * Where an update keeps whole plugin trees it is not using: the staged new
 * one and the retired old one. Never under Plugins/, where the editor would
 * find a second UE_MCP_Bridge.uplugin and load whichever copy it reached first.
 * The project's Intermediate when the plugin sits under a project's Plugins/
 * on the same volume (so every move is a rename), else a hidden directory
 * beside it, which only a plugin outside any project gets.
 */
export function updateWorkDir(pluginDir: string): string {
  const parent = path.dirname(path.resolve(pluginDir));
  for (let dir = parent; path.dirname(dir) !== dir; dir = path.dirname(dir)) {
    if (path.basename(dir).toLowerCase() !== "plugins") continue;
    const project = path.dirname(dir);
    let hasProject = false;
    try {
      hasProject = fs.readdirSync(project).some((f) => f.toLowerCase().endsWith(".uproject"));
    } catch {
      hasProject = false;
    }
    if (!hasProject) continue;
    const work = path.join(project, "Intermediate", "UE_MCP_Update");
    try {
      fs.mkdirSync(work, { recursive: true });
      if (fs.statSync(work).dev === fs.statSync(parent).dev) return work;
    } catch {
      // Not writable; fall back to the plugin's parent.
    }
    break;
  }
  return path.join(parent, `.${path.basename(pluginDir)}.ue-mcp-update`);
}

/** Remove the work directory once nothing is left in it. */
function dropEmptyWorkDir(updateDir: string): void {
  try {
    fs.rmdirSync(updateDir);
  } catch {
    // Not empty, or already gone.
  }
}

function uniqueName(prefix: string): string {
  return `${prefix}-${process.pid}-${Date.now()}-${crypto.randomBytes(3).toString("hex")}`;
}

/**
 * Fetch, verify and unpack the binaries for `t` into a fresh directory in the
 * update work directory, so the swap that follows is a rename. Throws with the
 * source named when they cannot be fetched, hold no plugin for this platform,
 * or are not a tree the editor would load.
 */
export async function stageBinaryPlugin(
  targetPluginDir: string,
  t: BinaryTarget,
  source: BinarySource = resolveBinarySource(t),
): Promise<StagedBinaries> {
  const work = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-bin-"));
  try {
    let extracted: string;
    if (source.kind === "dir") {
      extracted = source.path;
    } else {
      const zip = source.kind === "zip" ? source.path : path.join(work, binaryArchiveName(t));
      if (source.kind === "url") {
        await download(source.url, zip, t);
        await verifyDownload(source.url, zip, t);
      }
      extracted = path.join(work, "x");
      extractZip(zip, extracted);
    }

    const root = pluginRootIn(extracted);
    if (!root) throw new Error(`no ${BRIDGE_PLUGIN_NAME}.uplugin in ${describeBinarySource(source)}`);
    const binDir = path.join(root, "Binaries", t.platform);
    if (!fs.existsSync(binDir)) {
      throw new Error(`${describeBinarySource(source)} has no Binaries/${t.platform}; it was built for another platform`);
    }

    const updateDir = updateWorkDir(targetPluginDir);
    fs.mkdirSync(updateDir, { recursive: true });
    const staging = path.join(updateDir, uniqueName("staging"));
    try {
      fs.cpSync(root, staging, { recursive: true });
      fs.rmSync(path.join(staging, "Source"), { recursive: true, force: true });
      fs.rmSync(path.join(staging, "Intermediate"), { recursive: true, force: true });
      const marker: InstallMarker = { kind: "binary", engine: t.engine, platform: t.platform, version: t.version };
      writeInstallMarker(staging, marker);
      const problems = verifyBridgeTree(staging, { version: t.version, platform: t.platform });
      if (problems.length) {
        throw new Error(`the bridge in ${describeBinarySource(source)} would not load: ${problems.join("; ")}`);
      }
      return { staging, from: describeBinarySource(source), marker, files: treeFiles(staging) };
    } catch (e) {
      fs.rmSync(staging, { recursive: true, force: true });
      dropEmptyWorkDir(updateDir);
      throw e;
    }
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

/**
 * Install prebuilt binaries for `t` into `targetPluginDir`, replacing whatever
 * is there, and write the `binary` marker.
 */
export async function installBinaryPlugin(
  targetPluginDir: string,
  t: BinaryTarget,
  source: BinarySource = resolveBinarySource(t),
): Promise<BinaryInstallResult> {
  const staged = await stageBinaryPlugin(targetPluginDir, t, source);
  swapIn(staged.staging, targetPluginDir, {
    verify: () => verifyBridgeTree(targetPluginDir, { version: t.version, platform: t.platform, files: staged.files }),
  });
  return { pluginDir: targetPluginDir, from: staged.from, marker: staged.marker };
}

function removeQuietly(dir: string): boolean {
  try {
    fs.rmSync(dir, { recursive: true, force: true });
    return !fs.existsSync(dir);
  } catch {
    return false;
  }
}

export interface SwapOptions {
  /** Checks the installed tree once it is in place; any problem puts the old tree back and throws. */
  verify?: () => string[];
}

/**
 * Replace `target` with `staging`. The old tree is moved into the update work
 * directory first; if the new one then fails `verify`, it is put back. An old
 * tree that cannot be deleted yet (an editor still maps its DLLs) stays in the
 * work directory, out of the editor's sight, for sweepUpdateWork to take later.
 */
export function swapIn(staging: string, target: string, opts: SwapOptions = {}): void {
  fs.mkdirSync(path.dirname(target), { recursive: true });
  const updateDir = updateWorkDir(target);
  fs.mkdirSync(updateDir, { recursive: true });
  const old = path.join(updateDir, uniqueName("old"));
  const hadTarget = fs.existsSync(target);
  if (hadTarget) {
    try {
      fs.renameSync(target, old);
    } catch (e) {
      removeQuietly(staging);
      throw new Error(
        `could not replace ${target} (${e instanceof Error ? e.message : String(e)}). Close the editor and run again.`,
      );
    }
  }
  try {
    fs.renameSync(staging, target);
  } catch (e) {
    if (hadTarget) fs.renameSync(old, target);
    removeQuietly(staging);
    throw e;
  }
  const problems = opts.verify?.() ?? [];
  if (problems.length) {
    const failed = path.join(updateDir, uniqueName("failed"));
    fs.renameSync(target, failed);
    if (hadTarget) fs.renameSync(old, target);
    removeQuietly(failed);
    throw new Error(`the new bridge did not install cleanly, so the previous one was put back: ${problems.join("; ")}`);
  }
  if (hadTarget) removeQuietly(old);
  dropEmptyWorkDir(updateDir);
}

/** Sibling directories earlier ue-mcp releases staged or retired whole trees in, under Plugins/. */
function legacySiblings(pluginDir: string): string[] {
  const parent = path.dirname(path.resolve(pluginDir));
  const base = path.basename(pluginDir).replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const spare = new RegExp(`^${base}\\.(staging-\\d+|old-\\d+|pending-update)$`, "i");
  try {
    return fs.readdirSync(parent, { withFileTypes: true })
      .filter((e) => e.isDirectory() && spare.test(e.name))
      .map((e) => path.join(parent, e.name));
  } catch {
    return [];
  }
}

/**
 * Clear what earlier updates left behind: spare trees in the work directory,
 * and the `<plugin>.staging-<pid>`, `.old-<pid>` and `.pending-update` copies
 * releases before this one left under Plugins/, each a second bridge the
 * editor could load. A copy that cannot be deleted (mapped DLLs) is moved out
 * of Plugins/ instead, which a loaded DLL allows. `keep` names a tree still in
 * use. Never throws.
 */
export function sweepUpdateWork(pluginDir: string, keep: string[] = []): void {
  const kept = new Set(keep.map((k) => path.resolve(k).toLowerCase()));
  const updateDir = updateWorkDir(pluginDir);
  for (const sibling of legacySiblings(pluginDir)) {
    if (kept.has(sibling.toLowerCase()) || removeQuietly(sibling)) continue;
    try {
      fs.mkdirSync(updateDir, { recursive: true });
      fs.renameSync(sibling, path.join(updateDir, uniqueName("old")));
    } catch {
      // Left for the next sweep.
    }
  }
  let entries: string[] = [];
  try {
    entries = fs.readdirSync(updateDir);
  } catch {
    return;
  }
  for (const name of entries) {
    const full = path.join(updateDir, name);
    if (kept.has(full.toLowerCase())) continue;
    // Another process staging right now is left alone; a tree retired by anyone goes.
    const staging = /^staging-(d+)-/.exec(name);
    if (staging && Number(staging[1]) !== process.pid && isPidAlive(Number(staging[1]))) continue;
    removeQuietly(full);
  }
  dropEmptyWorkDir(updateDir);
}
