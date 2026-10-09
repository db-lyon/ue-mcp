#!/usr/bin/env node
/**
 * Prebuilt bridge binaries, built on this machine and attached to a release.
 *
 *   node scripts/binaries.mjs build [--engines 5.7,5.8] [--out dist-binaries]
 *   node scripts/binaries.mjs upload [--out dist-binaries]
 *
 * build runs Unreal's own `RunUAT BuildPlugin` once per installed engine and
 * packages each result as UE_MCP_Bridge-<version>-UE<engine>-<platform>.zip,
 * the name `ue-mcp init` asks for, without Source or Intermediate. It writes
 * UE_MCP_Bridge-<version>-manifest.json with each zip's SHA-256, which the
 * installer checks before installing anything.
 *
 * upload attaches the zips and the manifest to the draft GitHub release for the
 * version in package.json. It refuses a published release: binaries ship with
 * the release that carries them, never after it.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { isSameOrUnder, protectedEngineRoots } from "./build-utils.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const PLUGIN = path.join(ROOT, "plugin", "ue_mcp_bridge", "UE_MCP_Bridge.uplugin");
const version = JSON.parse(fs.readFileSync(path.join(ROOT, "package.json"), "utf-8")).version;

function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : fallback;
}

const out = path.resolve(arg("--out", path.join(ROOT, "dist-binaries")));
const platform = process.platform === "win32" ? "Win64" : process.platform === "darwin" ? "Mac" : "Linux";

/** Engine roots by major.minor: UE_MCP_BINARY_ENGINES (5.7=C:/...;5.8=C:/...) or the launcher's default location. */
function engines() {
  const explicit = process.env.UE_MCP_BINARY_ENGINES;
  if (explicit) {
    return explicit.split(";").filter(Boolean).map((e) => {
      const [v, root] = e.split("=");
      return { version: v, root };
    });
  }
  const base = process.platform === "win32" ? "C:/Program Files/Epic Games" : "/Users/Shared/Epic Games";
  if (!fs.existsSync(base)) return [];
  return fs.readdirSync(base)
    .map((d) => /^UE_(\d+\.\d+)$/.exec(d))
    .filter(Boolean)
    .map((m) => ({ version: m[1], root: path.join(base, m[0]) }));
}

function sha256(file) {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

function zipName(engine) {
  return `UE_MCP_Bridge-${version}-UE${engine}-${platform}.zip`;
}

function manifestName() {
  return `UE_MCP_Bridge-${version}-manifest.json`;
}

/** bsdtar writes zip from the extension; on Windows it is named by path because Git Bash puts GNU tar first. */
function zipDir(parent, folder, zipPath) {
  const tar = process.platform === "win32" ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe") : "tar";
  fs.rmSync(zipPath, { force: true });
  execFileSync(tar, ["-a", "-c", "-f", zipPath, "-C", parent, folder], { stdio: "inherit" });
}

function build() {
  // Binaries carry the descriptor they were built from; it must already name this version.
  const descriptor = JSON.parse(fs.readFileSync(PLUGIN, "utf-8")).VersionName;
  if (descriptor !== version) {
    throw new Error(`the plugin descriptor says ${descriptor}, package.json says ${version}; run npm run generate:metadata first`);
  }
  const wanted = arg("--engines", null)?.split(",");
  // UE_MCP_PROTECTED_ENGINE_ROOTS outranks everything, as it does for the test builds.
  const denied = protectedEngineRoots();
  const found = engines()
    .filter((e) => !wanted || wanted.includes(e.version))
    .filter((e) => {
      const blocked = denied.some((root) => isSameOrUnder(e.root, root));
      if (blocked) console.warn(`skip UE ${e.version}: ${e.root} is in UE_MCP_PROTECTED_ENGINE_ROOTS`);
      return !blocked;
    });
  if (found.length === 0) throw new Error("no engines to build against; set UE_MCP_BINARY_ENGINES or install one");
  fs.mkdirSync(out, { recursive: true });

  const assets = [];
  for (const engine of found) {
    const uat = path.join(engine.root, "Engine", "Build", "BatchFiles", process.platform === "win32" ? "RunUAT.bat" : "RunUAT.sh");
    if (!fs.existsSync(uat)) {
      console.warn(`skip UE ${engine.version}: no ${uat}`);
      continue;
    }
    // Staged under a short root: BuildPlugin nests a host project inside the package, and UBT
    // refuses action paths over Windows' 260 characters.
    const stage = path.join(arg("--stage", path.join(os.tmpdir(), "uemcpb")), `UE${engine.version}`);
    const pkg = path.join(stage, "UE_MCP_Bridge");
    fs.rmSync(stage, { recursive: true, force: true });
    console.log(`\n== UE ${engine.version}: BuildPlugin -> ${pkg}`);
    const started = Date.now();
    const hadFilter = fs.existsSync(path.join(path.dirname(PLUGIN), "Config"));
    const uatArgs = ["BuildPlugin", `-Plugin=${PLUGIN}`, `-Package=${pkg}`, `-TargetPlatforms=${platform}`, "-Rocket"];
    // A .bat runs under cmd, and the engine path has spaces: quote every piece and pass the line verbatim.
    const r = process.platform === "win32"
      ? spawnSync(process.env.ComSpec ?? "cmd.exe", ["/d", "/s", "/c", `"${[uat, ...uatArgs].map((x) => `"${x}"`).join(" ")}"`], {
          stdio: "inherit",
          windowsVerbatimArguments: true,
        })
      : spawnSync(uat, uatArgs, { stdio: "inherit" });
    // BuildPlugin writes a template Config/FilterPlugin.ini into the source plugin when it has none.
    if (!hadFilter) fs.rmSync(path.join(path.dirname(PLUGIN), "Config"), { recursive: true, force: true });
    if (r.status !== 0) throw new Error(`BuildPlugin failed for UE ${engine.version} (exit ${r.status})`);

    // The installer strips these too; leaving them out keeps the download small.
    for (const dir of ["Source", "Intermediate", "HostProject"]) fs.rmSync(path.join(pkg, dir), { recursive: true, force: true });
    if (!fs.existsSync(path.join(pkg, "Binaries", platform))) throw new Error(`UE ${engine.version}: no Binaries/${platform} in the package`);

    const zip = path.join(out, zipName(engine.version));
    zipDir(stage, "UE_MCP_Bridge", zip);
    fs.rmSync(stage, { recursive: true, force: true });
    assets.push({ engine: engine.version, platform, file: zipName(engine.version), sha256: sha256(zip), size: fs.statSync(zip).size });
    console.log(`== UE ${engine.version}: ${zipName(engine.version)} in ${Math.round((Date.now() - started) / 60000)} min`);
  }

  // Merge with a manifest from an earlier run, so engines can be built one at a time.
  const manifestPath = path.join(out, manifestName());
  const previous = fs.existsSync(manifestPath) ? JSON.parse(fs.readFileSync(manifestPath, "utf-8")).assets : [];
  const merged = [...previous.filter((a) => !assets.some((b) => b.file === a.file)), ...assets].sort((a, b) => a.file.localeCompare(b.file));
  fs.writeFileSync(manifestPath, JSON.stringify({ version, assets: merged }, null, 2) + "\n");
  console.log(`\nmanifest: ${manifestPath} (${merged.length} asset(s))`);
}

function upload() {
  const manifestPath = path.join(out, manifestName());
  if (!fs.existsSync(manifestPath)) throw new Error(`no ${manifestPath}; run build first`);
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf-8"));
  if (manifest.version !== version) throw new Error(`manifest is for ${manifest.version}, package.json says ${version}`);
  for (const a of manifest.assets) {
    const file = path.join(out, a.file);
    if (!fs.existsSync(file) || sha256(file) !== a.sha256) throw new Error(`${a.file} is missing or does not match the manifest`);
  }

  const tag = `v${version}`;
  const view = spawnSync("gh", ["release", "view", tag, "--json", "isDraft"], { encoding: "utf-8" });
  if (view.status !== 0) throw new Error(`no release ${tag}; create the draft first (gh release create ${tag} --draft --notes-file ...)`);
  if (!JSON.parse(view.stdout).isDraft) throw new Error(`${tag} is already published; binaries go on the draft before the version bump`);

  const files = [...manifest.assets.map((a) => path.join(out, a.file)), manifestPath];
  const r = spawnSync("gh", ["release", "upload", tag, ...files, "--clobber"], { stdio: "inherit" });
  if (r.status !== 0) throw new Error("gh release upload failed");
  console.log(`uploaded ${files.length} file(s) to draft ${tag}`);
}

const command = process.argv[2];
try {
  if (command === "build") build();
  else if (command === "upload") upload();
  else {
    console.error("usage: node scripts/binaries.mjs <build|upload> [--engines 5.7,5.8] [--out dir]");
    process.exit(2);
  }
} catch (e) {
  console.error(`binaries: ${e.message}`);
  process.exit(1);
}
