// @session per-session: Installs prebuilt binaries into one project's plugin directory.
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
import { extractZip, sha256Of } from "../core/archive.js";
import { readEnv } from "../core/env.js";
import { BRIDGE_PLUGIN_NAME, INSTALL_MARKER_NAME, writeInstallMarker, type InstallMarker } from "./install-marker.js";
import { treeFiles, verifyBridgeTree } from "./bridge-tree.js";
import { isPidAlive } from "../bridge/editor-target.js";
import { EngineBuildMismatchError, buildIdsDiffer, pluginBuildId } from "./engine-build-id.js";

export interface BinaryTarget {
  version: string;
  /** Engine major.minor, e.g. "5.7". */
  engine: string;
  /** Win64, Mac or Linux. */
  platform: string;
  /**
   * The target engine's BuildId (engine-build-id.ts). When set, binaries built
   * for another BuildId are refused with EngineBuildMismatchError, since Unreal
   * would skip every module in them. Null or unset skips the check.
   */
  engineBuildId?: string | null;
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
  /** `buildId` is the engine build the archive's modules were compiled against; manifests before #2072 have none. */
  assets: Array<{ engine: string; platform: string; file: string; sha256: string; size: number; buildId?: string }>;
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
export async function verifyDownload(zipUrl: string, zipPath: string, t: BinaryTarget): Promise<BinaryManifest["assets"][number]> {
  const manifestUrl = zipUrl.replace(/[^/]+$/, binaryManifestName(t.version));
  const res = await fetch(manifestUrl);
  if (!res.ok) throw new Error(`no binary manifest at ${manifestUrl} (${res.status}); refusing to install unverified binaries`);
  const manifest = (await res.json()) as BinaryManifest;
  const name = zipUrl.split("/").pop() ?? "";
  const entry = manifest.assets?.find((a) => a.file === name);
  if (!entry) throw new Error(`${name} is not in ${manifestUrl}; refusing to install unverified binaries`);
  const actual = sha256Of(zipPath);
  if (actual !== entry.sha256.toLowerCase()) {
    throw new Error(`${name} does not match its manifest hash (expected ${entry.sha256}, got ${actual}); refusing to install it`);
  }
  return entry;
}

export interface BinaryInstallResult {
  pluginDir: string;
  from: string;
  marker: InstallMarker;
}

export interface StagedBinaries {
  /**
   * A complete plugin tree in the update work directory, ready to swap in. It
   * has no install marker yet: writeMarkerFromTree writes it once the tree is
   * where it will be used, so the marker never claims a tree that is not there.
   */
  staging: string;
  from: string;
  /** The marker writeMarkerFromTree will write for this tree. */
  marker: InstallMarker;
  /** Every file the installed release holds, relative to the plugin root, marker included. */
  files: string[];
  /** The BuildId the staged modules were built for, or null when the manifest names none. */
  buildId: string | null;
}

/** The VersionName a plugin tree's descriptor carries, or null. */
function descriptorVersion(pluginDir: string): string | null {
  try {
    const v = JSON.parse(fs.readFileSync(path.join(pluginDir, `${BRIDGE_PLUGIN_NAME}.uplugin`), "utf-8")).VersionName;
    return typeof v === "string" && v.length > 0 ? v : null;
  } catch {
    return null;
  }
}

/**
 * Write the binary install marker for the tree at `pluginDir`, from what that
 * tree holds: the version is its descriptor's VersionName, and a tree that is
 * not `t.version` gets no marker and an error instead. Called last, once the
 * tree is in place, so an interrupted install never leaves a marker naming a
 * release whose files are not there.
 */
export function writeMarkerFromTree(pluginDir: string, t: BinaryTarget): InstallMarker {
  const version = descriptorVersion(pluginDir);
  if (version !== t.version) {
    throw new Error(`${pluginDir} holds bridge ${version ?? "with no VersionName"}, not ${t.version}, so no install marker was written`);
  }
  const marker: InstallMarker = { kind: "binary", engine: t.engine, platform: t.platform, version };
  writeInstallMarker(pluginDir, marker);
  return marker;
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
      // Plugins/ may not exist yet on a fresh project; without it the stat fails and a spare lands under Plugins/.
      fs.mkdirSync(parent, { recursive: true });
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

/** A fresh path in the update work directory to build a tree in before swapIn moves it to `target`. */
export function stagingDir(target: string): string {
  const updateDir = updateWorkDir(target);
  fs.mkdirSync(updateDir, { recursive: true });
  return path.join(updateDir, uniqueName("staging"));
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
    let listed: BinaryManifest["assets"][number] | null = null;
    if (source.kind === "dir") {
      extracted = source.path;
    } else {
      const zip = source.kind === "zip" ? source.path : path.join(work, binaryArchiveName(t));
      if (source.kind === "url") {
        await download(source.url, zip, t);
        listed = await verifyDownload(source.url, zip, t);
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
    // The archive's own manifest is what Unreal compares, so it outranks the release manifest's note.
    const buildId = pluginBuildId(root, t.platform) ?? listed?.buildId ?? null;
    if (t.engineBuildId && buildId && buildIdsDiffer(t.engineBuildId, buildId)) {
      throw new EngineBuildMismatchError(t.engineBuildId, buildId, t.engine, describeBinarySource(source));
    }
    const shipped = descriptorVersion(root);
    if (shipped !== t.version) {
      throw new Error(`${describeBinarySource(source)} holds bridge ${shipped ?? "with no VersionName"}, not ${t.version}`);
    }

    const staging = stagingDir(targetPluginDir);
    try {
      fs.cpSync(root, staging, { recursive: true });
      fs.rmSync(path.join(staging, "Source"), { recursive: true, force: true });
      fs.rmSync(path.join(staging, "Intermediate"), { recursive: true, force: true });
      fs.rmSync(path.join(staging, INSTALL_MARKER_NAME), { force: true });
      const problems = verifyBridgeTree(staging, { version: t.version, platform: t.platform, checkMarker: false });
      if (problems.length) {
        throw new Error(`the bridge in ${describeBinarySource(source)} would not load: ${problems.join("; ")}`);
      }
      const marker: InstallMarker = { kind: "binary", engine: t.engine, platform: t.platform, version: t.version };
      return { staging, from: describeBinarySource(source), marker, files: [...treeFiles(staging), INSTALL_MARKER_NAME], buildId };
    } catch (e) {
      fs.rmSync(staging, { recursive: true, force: true });
      dropEmptyWorkDir(path.dirname(staging));
      throw e;
    }
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

/**
 * Swap a staged tree in as the whole plugin folder, check it, then write the
 * marker from what landed. A tree that fails the check, or a folder that cannot
 * be moved (a running editor holds its DLLs), leaves the previous tree as it
 * was and throws naming why.
 */
export function swapInStaged(
  staged: StagedBinaries,
  targetPluginDir: string,
  t: BinaryTarget,
  verify: (files: string[]) => string[] = (files) =>
    verifyBridgeTree(targetPluginDir, { version: t.version, platform: t.platform, files, checkMarker: false }),
): InstallMarker {
  const withoutMarker = staged.files.filter((f) => f !== INSTALL_MARKER_NAME);
  swapIn(staged.staging, targetPluginDir, { verify: () => verify(withoutMarker) });
  return writeMarkerFromTree(targetPluginDir, t);
}

/**
 * Install prebuilt binaries for `t` into `targetPluginDir`, replacing whatever
 * is there, and write the `binary` marker last.
 */
export async function installBinaryPlugin(
  targetPluginDir: string,
  t: BinaryTarget,
  source: BinarySource = resolveBinarySource(t),
): Promise<BinaryInstallResult> {
  const staged = await stageBinaryPlugin(targetPluginDir, t, source);
  const marker = swapInStaged(staged, targetPluginDir, t);
  return { pluginDir: targetPluginDir, from: staged.from, marker };
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
        `could not replace ${target} (${e instanceof Error ? e.message : String(e)}), so the previous bridge is unchanged. ` +
          "A running editor or another program holds a file in it; close it and run again.",
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
    const staging = /^staging-(\d+)-/.exec(name);
    if (staging && Number(staging[1]) !== process.pid && isPidAlive(Number(staging[1]))) continue;
    removeQuietly(full);
  }
  dropEmptyWorkDir(updateDir);
}
