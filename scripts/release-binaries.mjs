/**
 * The publish gate for prebuilt bridge binaries: a release must carry a bridge
 * zip for every engine it promises and the SHA-256 manifest the installer
 * checks, or every binary install's update fails with "no prebuilt bridge".
 *
 *   gh release view vX.Y.Z --json assets --jq '.assets[].name' \
 *     | node scripts/release-binaries.mjs --version X.Y.Z [--manifest <manifest.json>]
 *
 * Reads the draft's asset names, one per line, on stdin. `--manifest` names the
 * release's own manifest, whose engines are the build set. Exits 1 naming each
 * missing file and how to build and attach them.
 */
import * as fs from "node:fs";
import { pathToFileURL } from "node:url";

/**
 * Every engine a release can ship a Win64 bridge for. `always` is required on
 * every release; `when-built` only once the binaries build includes it.
 */
export const ENGINES = [
  { version: "5.7", required: "always" },
  { version: "5.8", required: "always" },
  { version: "5.9", required: "when-built" },
];
export const PLATFORM = "Win64";

/** The engines every release must carry, whatever was built. */
export const REQUIRED_ENGINES = ENGINES.filter((e) => e.required === "always").map((e) => e.version);

/** The engines a release must carry when `built` is the binaries build set. */
export function requiredEngines(built = []) {
  const have = new Set(built);
  return ENGINES.filter((e) => e.required === "always" || have.has(e.version)).map((e) => e.version);
}

/** The engine versions a manifest's assets were built for. */
export function manifestEngines(manifest, platform = PLATFORM) {
  const re = new RegExp(`-UE(\\d+\\.\\d+)-${platform}\\.zip$`);
  return (manifest?.assets ?? []).map((a) => re.exec(a.file ?? "")?.[1]).filter(Boolean);
}

/** The files a release for `version` must carry. */
export function requiredBinaries(version, engines = REQUIRED_ENGINES, platform = PLATFORM) {
  return [
    ...engines.map((e) => `UE_MCP_Bridge-${version}-UE${e}-${platform}.zip`),
    `UE_MCP_Bridge-${version}-manifest.json`,
  ];
}

/** The required files not among `assets`. */
export function missingBinaries(version, assets, engines = REQUIRED_ENGINES, platform = PLATFORM) {
  const have = new Set(assets.map((a) => a.trim()).filter(Boolean));
  return requiredBinaries(version, engines, platform).filter((f) => !have.has(f));
}

/** The failure text for a release missing `missing`. */
export function missingMessage(version, missing) {
  return [
    `Release v${version} does not carry its prebuilt bridge binaries. Missing: ${missing.join(", ")}.`,
    "Build and attach them to the draft before bumping the version:",
    "  npm run binaries:build",
    "  npm run binaries:upload",
  ].join("\n");
}

function main() {
  const arg = (name) => {
    const i = process.argv.indexOf(name);
    return i >= 0 ? process.argv[i + 1] : null;
  };
  const version = arg("--version");
  if (!version) {
    console.error("::error::usage: node scripts/release-binaries.mjs --version X.Y.Z [--manifest file] < asset-names");
    process.exit(1);
  }
  const manifestFile = arg("--manifest");
  const built = manifestFile && fs.existsSync(manifestFile)
    ? manifestEngines(JSON.parse(fs.readFileSync(manifestFile, "utf8")))
    : [];
  const engines = requiredEngines(built);
  const assets = fs.readFileSync(0, "utf8").split(/\r?\n/);
  const missing = missingBinaries(version, assets, engines);
  if (missing.length) {
    console.error(`::error::${missingMessage(version, missing).split("\n").join("%0A")}`);
    console.error(missingMessage(version, missing));
    process.exit(1);
  }
  console.log(`Release v${version} carries ${requiredBinaries(version, engines).join(", ")}.`);
}

const invokedAsScript = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
