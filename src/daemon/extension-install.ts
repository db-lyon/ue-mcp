/**
 * Installing a daemon extension from a zip: a local file or a URL, checked
 * against a SHA-256 before anything is unpacked. The archive holds the
 * extension directory itself (package.json at its root, or under one top
 * folder). It is staged beside the target and swapped in by rename, so a bad
 * download never leaves half an extension where the daemon loads it.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { extensionsDir } from "./extensions.js";
import { EXTENSION_API_VERSION } from "./extension-api.js";

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
    execFileSync(tar, ["-xf", zip, "-C", into], { stdio: ["ignore", "ignore", "pipe"] });
  } catch {
    execFileSync("unzip", ["-q", "-o", zip, "-d", into], { stdio: ["ignore", "ignore", "pipe"] });
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
 * `sha256` is required for a URL and checked for a file when given.
 */
export async function installExtension(
  source: string,
  opts: { sha256?: string | null; root?: string } = {},
): Promise<InstalledExtension> {
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
    if (EXTENSION_API_VERSION < min || EXTENSION_API_VERSION > max) {
      throw new Error(`${name} needs extension API ${min}..${max}; this ue-mcp has ${EXTENSION_API_VERSION}`);
    }

    const root = opts.root ?? extensionsDir();
    const target = path.join(root, name);
    const staging = `${target}.staging-${process.pid}`;
    fs.mkdirSync(root, { recursive: true });
    fs.rmSync(staging, { recursive: true, force: true });
    fs.cpSync(pkgDir, staging, { recursive: true });
    const old = `${target}.old-${process.pid}`;
    const had = fs.existsSync(target);
    if (had) fs.renameSync(target, old);
    try {
      fs.renameSync(staging, target);
    } catch (e) {
      if (had) fs.renameSync(old, target);
      throw e;
    }
    if (had) fs.rmSync(old, { recursive: true, force: true });
    return { name, version: pkg.version ?? "0.0.0", dir: target, sha256 };
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

/** Remove an installed extension. True when there was one. */
export function removeExtension(name: string, root: string = extensionsDir()): boolean {
  const dir = path.join(root, name);
  if (!fs.existsSync(dir)) return false;
  fs.rmSync(dir, { recursive: true, force: true });
  return true;
}
