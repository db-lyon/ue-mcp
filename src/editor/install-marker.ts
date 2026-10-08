/**
 * The bridge install marker and where the installed plugin lives.
 *
 * `<pluginDir>/.ue-mcp-install.json` says how the plugin got there: `source`
 * (compiled in the project) or `binary` (prebuilt for one engine and platform).
 * Every writer of the plugin tree reads it first, because source written over a
 * binary install is newer than the binaries and turns the next launch into a
 * rebuild prompt that fails without a toolchain.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { findLiveInstanceRecord, isPidAlive } from "../bridge/editor-target.js";

export const INSTALL_MARKER_NAME = ".ue-mcp-install.json";
export const BRIDGE_PLUGIN_NAME = "UE_MCP_Bridge";

export type InstallKind = "source" | "binary";

export interface InstallMarker {
  kind: InstallKind;
  /** Engine major.minor the plugin was installed for, e.g. "5.7". */
  engine: string | null;
  /** Unreal platform name: Win64, Mac or Linux. */
  platform: string | null;
  /** ue-mcp package version the plugin came from. */
  version: string | null;
}

export function installMarkerPath(pluginDir: string): string {
  return path.join(pluginDir, INSTALL_MARKER_NAME);
}

export function readInstallMarker(pluginDir: string): InstallMarker | null {
  try {
    const raw = JSON.parse(fs.readFileSync(installMarkerPath(pluginDir), "utf-8")) as Record<string, unknown>;
    if (raw.kind !== "source" && raw.kind !== "binary") return null;
    const str = (v: unknown): string | null => (typeof v === "string" && v.length > 0 ? v : null);
    return { kind: raw.kind, engine: str(raw.engine), platform: str(raw.platform), version: str(raw.version) };
  } catch {
    return null;
  }
}

export function writeInstallMarker(pluginDir: string, marker: InstallMarker): void {
  fs.mkdirSync(pluginDir, { recursive: true });
  fs.writeFileSync(installMarkerPath(pluginDir), JSON.stringify(marker, null, 2) + "\n");
}

/**
 * How the plugin in `pluginDir` was installed, or null when nothing is there.
 * A tree with no marker predates it: a Source directory means source, and
 * binaries without one mean binary.
 */
export function installKindOf(pluginDir: string): InstallKind | null {
  const marker = readInstallMarker(pluginDir);
  if (marker) return marker.kind;
  if (!fs.existsSync(path.join(pluginDir, `${BRIDGE_PLUGIN_NAME}.uplugin`))) return null;
  if (fs.existsSync(path.join(pluginDir, "Source"))) return "source";
  return fs.existsSync(path.join(pluginDir, "Binaries")) ? "binary" : null;
}

/** Unreal's name for the platform this process runs on. */
export function unrealPlatform(platform: NodeJS.Platform = process.platform): string {
  if (platform === "win32") return "Win64";
  if (platform === "darwin") return "Mac";
  return "Linux";
}

/** Engine "major.minor" from an engine root's Build.version, or null. */
export function engineMajorMinor(engineRoot: string | null | undefined): string | null {
  if (!engineRoot) return null;
  try {
    const v = JSON.parse(fs.readFileSync(path.join(engineRoot, "Engine", "Build", "Build.version"), "utf-8")) as {
      MajorVersion?: number; MinorVersion?: number;
    };
    return typeof v.MajorVersion === "number" && typeof v.MinorVersion === "number"
      ? `${v.MajorVersion}.${v.MinorVersion}`
      : null;
  } catch {
    return null;
  }
}

export function defaultPluginDir(projectDir: string): string {
  return path.join(projectDir, "Plugins", BRIDGE_PLUGIN_NAME);
}

export interface ResolvedPluginDir {
  dir: string;
  source: "instance-record" | "default" | "scan";
}

/** How deep under Plugins/ a relocated bridge is looked for. */
const SCAN_DEPTH = 3;

/**
 * Where this project's bridge plugin is installed, or null when it is not.
 *
 * A running editor's instance record names the directory it loaded the plugin
 * from, so it is asked first. Then the default location, then a shallow scan
 * of Plugins/ for a plugin moved into a subfolder.
 */
export function resolvePluginDir(
  projectDir: string,
  isAlive: (pid: number) => boolean = isPidAlive,
): ResolvedPluginDir | null {
  const record = findLiveInstanceRecord(projectDir, isAlive);
  const recorded = record ? recordedPluginDir(record.recordPath) : null;
  if (recorded && hasDescriptor(recorded)) return { dir: path.resolve(recorded), source: "instance-record" };

  const fallback = defaultPluginDir(projectDir);
  if (hasDescriptor(fallback)) return { dir: fallback, source: "default" };

  const found = scanForDescriptor(path.join(projectDir, "Plugins"), SCAN_DEPTH);
  return found ? { dir: found, source: "scan" } : null;
}

function recordedPluginDir(recordPath: string): string | null {
  try {
    const v = (JSON.parse(fs.readFileSync(recordPath, "utf-8")) as { pluginDir?: unknown }).pluginDir;
    return typeof v === "string" && v.length > 0 ? v : null;
  } catch {
    return null;
  }
}

function hasDescriptor(dir: string): boolean {
  return fs.existsSync(path.join(dir, `${BRIDGE_PLUGIN_NAME}.uplugin`));
}

function scanForDescriptor(dir: string, depth: number): string | null {
  if (depth < 0) return null;
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return null;
  }
  if (entries.some((e) => e.isFile() && e.name.toLowerCase() === `${BRIDGE_PLUGIN_NAME.toLowerCase()}.uplugin`)) {
    return dir;
  }
  for (const e of entries) {
    if (!e.isDirectory() || e.name === "Binaries" || e.name === "Intermediate" || e.name === "Source") continue;
    const hit = scanForDescriptor(path.join(dir, e.name), depth - 1);
    if (hit) return hit;
  }
  return null;
}
