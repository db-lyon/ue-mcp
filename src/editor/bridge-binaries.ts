/**
 * Prebuilt bridge binaries: where they come from and how they land in a project.
 *
 * A machine with no C++ toolchain cannot compile the bridge, so it gets the
 * plugin prebuilt for its engine and platform instead of source. The archive is
 * a plugin directory with no Source tree. It is staged beside the target and
 * swapped in by rename, so a failed download never leaves half a plugin.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { readEnv } from "../core/env.js";
import { BRIDGE_PLUGIN_NAME, writeInstallMarker, type InstallMarker } from "./install-marker.js";

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
    execFileSync(tar, ["-xf", zip, "-C", into], { stdio: ["ignore", "ignore", "pipe"] });
  } catch {
    execFileSync("unzip", ["-q", "-o", zip, "-d", into], { stdio: ["ignore", "ignore", "pipe"] });
  }
}

export interface BinaryInstallResult {
  pluginDir: string;
  from: string;
  marker: InstallMarker;
}

export interface StagedBinaries {
  /** A complete plugin tree beside the target, marker written, ready to swap in. */
  staging: string;
  from: string;
  marker: InstallMarker;
}

/**
 * Fetch, verify and unpack the binaries for `t` into a staging directory on the
 * target's volume, so the swap that follows is a rename. Throws with the source
 * named when they cannot be fetched or hold no plugin for this platform.
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

    const staging = `${targetPluginDir}.staging-${process.pid}`;
    fs.rmSync(staging, { recursive: true, force: true });
    fs.cpSync(root, staging, { recursive: true });
    fs.rmSync(path.join(staging, "Source"), { recursive: true, force: true });
    fs.rmSync(path.join(staging, "Intermediate"), { recursive: true, force: true });
    const marker: InstallMarker = { kind: "binary", engine: t.engine, platform: t.platform, version: t.version };
    writeInstallMarker(staging, marker);
    return { staging, from: describeBinarySource(source), marker };
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
  swapIn(staged.staging, targetPluginDir);
  return { pluginDir: targetPluginDir, from: staged.from, marker: staged.marker };
}

/** Replace `target` with `staging`. A loaded plugin DLL makes the first rename fail. */
export function swapIn(staging: string, target: string): void {
  fs.mkdirSync(path.dirname(target), { recursive: true });
  const old = `${target}.old-${process.pid}`;
  const hadTarget = fs.existsSync(target);
  if (hadTarget) {
    try {
      fs.renameSync(target, old);
    } catch (e) {
      fs.rmSync(staging, { recursive: true, force: true });
      throw new Error(
        `could not replace ${target} (${e instanceof Error ? e.message : String(e)}). Close the editor and run again.`,
      );
    }
  }
  try {
    fs.renameSync(staging, target);
  } catch (e) {
    if (hadTarget) fs.renameSync(old, target);
    throw e;
  }
  if (hadTarget) fs.rmSync(old, { recursive: true, force: true });
}
