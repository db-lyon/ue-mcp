/**
 * Daemon extensions side by side by version (#1619). Each version installs to
 * `<extensions>/<name>@<version>/`, and each project records the version it
 * loads, so updating one project never changes what another loads.
 *
 * `<extensions>/<name>/` is the single copy older ue-mcp releases install and
 * load. It is never written here: daemons of those releases keep loading it,
 * and it is copied to its versioned folder the first time a newer daemon or
 * CLI sees it, which is the migration.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { compareVersions } from "../extensions/version.js";
import { userDir } from "../core/user-dir.js";
import { projectKey } from "./discovery.js";
import { getProjectExtensionVersion, recordedExtensionVersions, setProjectExtensionVersion } from "../config/user-state.js";

/** One installed copy of an extension. */
export interface ExtensionCopy {
  name: string;
  version: string;
  dir: string;
  /** The unversioned folder older ue-mcp releases install. */
  legacy: boolean;
}

const VERSION_CHARS = /^[0-9A-Za-z.+-]+$/;

/** The folder a version installs to. */
export function versionDir(root: string, name: string, version: string): string {
  if (!VERSION_CHARS.test(version)) throw new Error(`${name}: ${JSON.stringify(version)} is not a version`);
  return path.join(root, `${name}@${version}`);
}

/** A copy an interrupted install left: <folder>.staging-<pid> or <folder>.old-<pid>. */
export function isLeftover(folder: string): boolean {
  return /\.(staging|old)-\d+$/.test(folder);
}

function readCopy(root: string, folder: string): ExtensionCopy | null {
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(root, folder, "package.json"), "utf-8")) as {
      version?: string; ueMcpExtension?: { name?: string };
    };
    const name = pkg.ueMcpExtension?.name;
    const version = pkg.version ?? "0.0.0";
    if (!name) return null;
    if (folder === name) return { name, version, dir: path.join(root, folder), legacy: true };
    if (folder === `${name}@${version}`) return { name, version, dir: path.join(root, folder), legacy: false };
    return null;
  } catch {
    return null;
  }
}

/** Every installed copy under `root`, by extension name, oldest first. A folder holding something else is skipped. */
export function installedCopies(root: string): Map<string, ExtensionCopy[]> {
  const out = new Map<string, ExtensionCopy[]>();
  if (!fs.existsSync(root)) return out;
  for (const e of fs.readdirSync(root, { withFileTypes: true })) {
    if (!e.isDirectory() || isLeftover(e.name)) continue;
    const copy = readCopy(root, e.name);
    if (!copy) continue;
    out.set(copy.name, [...(out.get(copy.name) ?? []), copy]);
  }
  for (const copies of out.values()) copies.sort((a, b) => compareVersions(a.version, b.version) || Number(b.legacy) - Number(a.legacy));
  return out;
}

/** The distinct versions installed, oldest first. */
export function versionsOf(copies: readonly ExtensionCopy[]): string[] {
  return [...new Set(copies.map((c) => c.version))];
}

/**
 * Copy each unversioned folder to `<name>@<version>` when that version has no
 * folder of its own yet, leaving the original for older daemons. A copy that
 * fails is retried next time; the unversioned folder still loads meanwhile.
 */
export function migrateLegacy(root: string): void {
  for (const copies of installedCopies(root).values()) {
    const legacy = copies.find((c) => c.legacy);
    if (!legacy || copies.some((c) => !c.legacy && c.version === legacy.version)) continue;
    let target: string;
    try {
      target = versionDir(root, legacy.name, legacy.version);
    } catch {
      continue;
    }
    const staging = `${target}.staging-${process.pid}`;
    try {
      fs.cpSync(legacy.dir, staging, { recursive: true });
      fs.renameSync(staging, target);
    } catch {
      fs.rmSync(staging, { recursive: true, force: true });
    }
  }
}

/** Whether this project has run extension `name` before: the data folder the daemon gives it exists. */
export function projectUsedExtension(projectDir: string, name: string): boolean {
  return fs.existsSync(path.join(userDir(), "data", projectKey(projectDir), name));
}

export interface VersionChoice {
  version: string;
  /** Write `version` as the project's record: there was none. */
  record: boolean;
}

/**
 * Which version a project loads, from the versions this daemon can load.
 *
 * - The recorded version, when it is installed and loads here.
 * - Recorded but not loadable here (removed, or it needs a newer ue-mcp): the
 *   newest loadable one no newer than the record, else the newest. The record
 *   stays, so the project returns to its version once it loads again.
 * - No record: the version the unversioned folder holds when the project ran
 *   the extension before (it was loading that copy), else the newest. It is
 *   then recorded.
 */
export function chooseVersion(
  loadable: readonly string[],
  recorded: string | null,
  legacy: { version: string; usedBefore: boolean } | null,
): VersionChoice | null {
  if (loadable.length === 0) return null;
  const sorted = [...loadable].sort(compareVersions);
  const newest = sorted[sorted.length - 1];
  if (recorded) {
    if (sorted.includes(recorded)) return { version: recorded, record: false };
    const older = sorted.filter((v) => compareVersions(v, recorded) <= 0);
    return { version: older[older.length - 1] ?? newest, record: false };
  }
  if (legacy?.usedBefore && sorted.includes(legacy.version)) return { version: legacy.version, record: true };
  return { version: newest, record: true };
}

/** The copy to load for a version: its own folder, else the unversioned one holding it. */
export function copyFor(copies: readonly ExtensionCopy[], version: string): ExtensionCopy | null {
  return copies.find((c) => !c.legacy && c.version === version) ?? copies.find((c) => c.version === version) ?? null;
}

export function recordedVersion(projectDir: string, name: string): string | null {
  return getProjectExtensionVersion(projectDir, name);
}

export function recordVersion(projectDir: string, name: string, version: string): void {
  setProjectExtensionVersion(projectDir, name, version);
}

/**
 * Remove the versioned folders of `name` that no project records, keeping
 * `keep` and the newest. The unversioned folder is never removed here. A
 * folder still open is left for the next prune.
 */
export function pruneVersions(root: string, name: string, keep: readonly string[] = []): string[] {
  const copies = (installedCopies(root).get(name) ?? []).filter((c) => !c.legacy);
  if (copies.length === 0) return [];
  const kept = new Set([...recordedExtensionVersions(name), ...keep, copies[copies.length - 1].version]);
  const removed: string[] = [];
  for (const c of copies) {
    if (kept.has(c.version)) continue;
    try {
      fs.rmSync(c.dir, { recursive: true, force: true });
      removed.push(c.version);
    } catch {
      // In use; the next prune takes it.
    }
  }
  return removed;
}
