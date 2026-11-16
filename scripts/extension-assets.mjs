/**
 * Packages the first-party daemon extensions under extensions/ as release
 * assets: for each, `<package>-<version>.zip` and `<package>-<version>-manifest.json`
 * (the release manifest `ue-mcp install` reads: the zip's URL, relative to the
 * manifest, and its SHA-256). The version is the ue-mcp version, stamped into
 * the zipped package.json, so a release carries the extension built from the
 * same commit. install-index.json points each name at its manifest.
 *
 *   node scripts/extension-assets.mjs [--version X.Y.Z] [--out dir]
 *
 * The publish job attaches what it writes to the draft release.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
export const EXTENSIONS_DIR = path.join(root, "extensions");

/** The extension source folders: each holds a package.json with a ueMcpExtension field. */
export function extensionSources(dir = EXTENSIONS_DIR) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir, { withFileTypes: true })
    .filter((e) => e.isDirectory() && fs.existsSync(path.join(dir, e.name, "package.json")))
    .map((e) => path.join(dir, e.name))
    .filter((d) => JSON.parse(fs.readFileSync(path.join(d, "package.json"), "utf8")).ueMcpExtension?.name);
}

/** The asset names a release of `version` carries for the package `pkgName`. */
export function assetNames(pkgName, version) {
  return { zip: `${pkgName}-${version}.zip`, manifest: `${pkgName}-${version}-manifest.json` };
}

function zip(dir, zipPath) {
  fs.rmSync(zipPath, { force: true });
  if (process.platform === "win32") {
    const tar = path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe");
    execFileSync(tar, ["-a", "-c", "-f", zipPath, "-C", dir, "."], { windowsHide: true, stdio: "inherit" });
  } else {
    execFileSync("zip", ["-q", "-r", "-X", zipPath, "."], { cwd: dir, stdio: "inherit" });
  }
}

/** Write the zip and manifest of every extension for `version` into `out`. Returns the files written. */
export function packageExtensions(version, out, dir = EXTENSIONS_DIR) {
  fs.mkdirSync(out, { recursive: true });
  const written = [];
  for (const src of extensionSources(dir)) {
    const pkg = JSON.parse(fs.readFileSync(path.join(src, "package.json"), "utf8"));
    const names = assetNames(pkg.name, version);
    const stage = fs.mkdtempSync(path.join(out, ".stage-"));
    try {
      fs.cpSync(src, stage, { recursive: true });
      fs.writeFileSync(path.join(stage, "package.json"), JSON.stringify({ ...pkg, version }, null, 2) + "\n");
      const zipPath = path.join(out, names.zip);
      zip(stage, zipPath);
      const sha256 = crypto.createHash("sha256").update(fs.readFileSync(zipPath)).digest("hex");
      const manifestPath = path.join(out, names.manifest);
      fs.writeFileSync(manifestPath, JSON.stringify({ url: names.zip, sha256, version }, null, 2) + "\n");
      written.push(zipPath, manifestPath);
    } finally {
      fs.rmSync(stage, { recursive: true, force: true });
    }
  }
  return written;
}

function main() {
  const arg = (name) => {
    const i = process.argv.indexOf(name);
    return i >= 0 ? process.argv[i + 1] : null;
  };
  const version = arg("--version") ?? JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8")).version;
  const out = path.resolve(arg("--out") ?? path.join(root, "dist-extensions"));
  for (const f of packageExtensions(version, out)) console.log(f);
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? "").href) main();
