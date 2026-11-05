/**
 * Installing a daemon extension from a zip: a local file or a URL, checked
 * against a SHA-256 before anything is unpacked. The archive holds the
 * extension directory itself (package.json at its root, or under one top
 * folder). It is staged beside the target and swapped in by rename, so a bad
 * download never leaves half an extension where the daemon loads it.
 *
 * Each version installs to its own `<name>@<version>` folder beside the
 * others (extension-versions.ts), so installing for one project leaves what
 * every other project loads in place.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { extensionsDir } from "./extensions.js";
import { EXTENSION_API_MIN_VERSION, EXTENSION_API_VERSION, extensionApiSupported } from "./extension-api.js";
import { installedCopies, isLeftover, migrateLegacy, pruneVersions, recordVersion, versionDir } from "./extension-versions.js";
import { clearExtensionRecords } from "../config/user-state.js";

export interface InstalledExtension {
  name: string;
  version: string;
  dir: string;
  sha256: string;
}

function extractZip(zip: string, into: string): void {
  fs.mkdirSync(into, { recursive: true });
  const tar = process.platform === "win32"
    ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe")
    : "tar";
  try {
    execFileSync(tar, ["-xf", zip, "-C", into], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
  } catch {
    execFileSync("unzip", ["-q", "-o", zip, "-d", into], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
  }
}

/** The directory holding package.json: the root, or its only folder. */
function packageRootIn(dir: string): string | null {
  if (fs.existsSync(path.join(dir, "package.json"))) return dir;
  const entries = fs.readdirSync(dir, { withFileTypes: true }).filter((e) => e.isDirectory());
  if (entries.length === 1 && fs.existsSync(path.join(dir, entries[0].name, "package.json"))) {
    return path.join(dir, entries[0].name);
  }
  return null;
}

/**
 * Install the extension in `source` (a .zip path or an http(s) URL).
 * `sha256` is required for a URL and checked for a file when given. With
 * `projectDir`, that project is recorded at the installed version. Versions
 * no project records are then removed, keeping the newest.
 */
export async function installExtension(
  source: string,
  opts: { sha256?: string | null; root?: string; projectDir?: string } = {},
): Promise<InstalledExtension> {
  // A release manifest names the archive and its hash: { "url": "...", "sha256": "..." }.
  if (/^https?:\/\/.*\.json(\?.*)?$/i.test(source)) {
    const res = await fetch(source);
    if (!res.ok) throw new Error(`manifest download failed (${res.status} ${res.statusText}): ${source}`);
    const manifest = (await res.json()) as { url?: unknown; sha256?: unknown };
    if (typeof manifest.url !== "string" || typeof manifest.sha256 !== "string") {
      throw new Error(`${source} is not an extension manifest (needs url and sha256)`);
    }
    return installExtension(new URL(manifest.url, source).href, { ...opts, sha256: manifest.sha256 });
  }
  const isUrl = /^https?:\/\//i.test(source);
  if (isUrl && !opts.sha256) throw new Error("a URL needs --sha256: an extension runs inside the daemon, so it is never installed unverified");
  const work = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ext-"));
  try {
    const zip = isUrl ? path.join(work, "extension.zip") : path.resolve(source);
    if (isUrl) {
      const res = await fetch(source);
      if (!res.ok) throw new Error(`download failed (${res.status} ${res.statusText}): ${source}`);
      fs.writeFileSync(zip, Buffer.from(await res.arrayBuffer()));
    }
    if (!fs.existsSync(zip)) throw new Error(`no such file: ${zip}`);
    const sha256 = crypto.createHash("sha256").update(fs.readFileSync(zip)).digest("hex");
    if (opts.sha256 && sha256 !== opts.sha256.toLowerCase()) {
      throw new Error(`${source} does not match the expected SHA-256 (expected ${opts.sha256}, got ${sha256})`);
    }

    const unpacked = path.join(work, "x");
    extractZip(zip, unpacked);
    const pkgDir = packageRootIn(unpacked);
    if (!pkgDir) throw new Error(`${source} has no package.json at its root`);
    const pkg = JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8")) as {
      version?: string; main?: string; ueMcpExtension?: { name?: string; api?: { min?: number; max?: number } };
    };
    const name = pkg.ueMcpExtension?.name;
    if (!name || !/^[a-z0-9][a-z0-9-]*$/.test(name)) throw new Error(`${source} is not a ue-mcp extension (package.json has no valid ueMcpExtension.name)`);
    if (!pkg.main || !fs.existsSync(path.join(pkgDir, pkg.main))) throw new Error(`${name}: main ${pkg.main ?? "(unset)"} is missing from the archive`);
    const min = pkg.ueMcpExtension?.api?.min ?? 1;
    const max = pkg.ueMcpExtension?.api?.max ?? min;
    if (!extensionApiSupported(min, max)) {
      throw new Error(`${name} needs extension API ${min}..${max}; this ue-mcp serves ${EXTENSION_API_MIN_VERSION}..${EXTENSION_API_VERSION}`);
    }

    const version = pkg.version ?? "0.0.0";
    const root = opts.root ?? extensionsDir();
    const target = versionDir(root, name, version);
    const staging = `${target}.staging-${process.pid}`;
    fs.mkdirSync(root, { recursive: true });
    migrateLegacy(root);
    sweepLeftovers(root, name);
    fs.cpSync(pkgDir, staging, { recursive: true });
    const old = `${target}.old-${process.pid}`;
    const had = fs.existsSync(target);
    try {
      if (had) fs.renameSync(target, old);
      try {
        fs.renameSync(staging, target);
      } catch (e) {
        if (had) fs.renameSync(old, target);
        throw e;
      }
    } finally {
      // The new copy is in place or the old one is back; either way the spare goes, and
      // one that cannot go yet (a file still open) is swept by the next install.
      removeQuietly(staging);
      if (had) removeQuietly(old);
    }
    if (opts.projectDir) recordVersion(opts.projectDir, name, version);
    pruneVersions(root, name, [version]);
    return { name, version, dir: target, sha256 };
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

function removeQuietly(dir: string): void {
  try {
    fs.rmSync(dir, { recursive: true, force: true });
  } catch {
    // Still open somewhere; sweepLeftovers takes it next time.
  }
}

/** The copies earlier installs of `name` left beside it: <folder>.staging-<pid> and <folder>.old-<pid>. */
export function sweepLeftovers(root: string, name: string): void {
  if (!fs.existsSync(root)) return;
  for (const e of fs.readdirSync(root, { withFileTypes: true })) {
    if (!e.isDirectory() || !isLeftover(e.name)) continue;
    const folder = e.name.replace(/\.(staging|old)-\d+$/, "");
    if (folder === name || folder.startsWith(`${name}@`)) removeQuietly(path.join(root, e.name));
  }
}

/**
 * Record `projectDir` at an installed version of `name`, the way `install`
 * does for a version already on the machine. Throws when it is not installed.
 */
export function useExtensionVersion(name: string, version: string, projectDir: string, root: string = extensionsDir()): { name: string; version: string; dir: string } {
  migrateLegacy(root);
  const copies = installedCopies(root).get(name) ?? [];
  const copy = copies.find((c) => c.version === version && !c.legacy) ?? copies.find((c) => c.version === version);
  if (!copy) {
    const have = [...new Set(copies.map((c) => c.version))];
    throw new Error(have.length ? `${name} ${version} is not installed; installed: ${have.join(", ")}` : `${name} is not installed`);
  }
  recordVersion(projectDir, name, version);
  return { name, version, dir: copy.dir };
}

/**
 * Remove every installed version of an extension, and every project's record
 * of it. True when there was one. The name must be one folder name directly
 * under the extensions directory, never a path out of it.
 */
export function removeExtension(name: string, root: string = extensionsDir()): boolean {
  if (!name || name === "." || name === ".." || /[\\/@]/.test(name)) {
    throw new Error(`${JSON.stringify(name)} is not an extension name`);
  }
  const dirs = new Set((installedCopies(root).get(name) ?? []).map((c) => c.dir));
  const plain = path.join(root, name);
  if (fs.existsSync(plain)) dirs.add(plain);
  if (dirs.size === 0) return false;
  for (const dir of dirs) fs.rmSync(dir, { recursive: true, force: true });
  clearExtensionRecords(name);
  return true;
}
