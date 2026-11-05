/**
 * Records the bridge upgrade matrix fixtures: for every ue-mcp release whose
 * GitHub release carries a UE 5.8 Win64 bridge zip, the zip is downloaded into
 * node_modules/.cache/ue-mcp/bridge-releases (verified against the release
 * manifest) and its layout written to tests/unit/fixtures/bridge-upgrade as
 * one small JSON per version: every file's path, size and hash, plus the text
 * of the descriptor and module manifests. The unit test rebuilds each tree
 * from those, and uses the cached zips themselves when they are present.
 *
 *   node scripts/bridge-upgrade-fixtures.mjs [--engine 5.8] [--offline]
 *
 * --offline rewrites the fixtures from the zips already cached.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const args = process.argv.slice(2);
const engine = args.includes("--engine") ? args[args.indexOf("--engine") + 1] : "5.8";
const offline = args.includes("--offline");
const platform = "Win64";
const repo = "db-lyon/ue-mcp";

export const CACHE_DIR = path.join(root, "node_modules", ".cache", "ue-mcp", "bridge-releases");
export const FIXTURE_DIR = path.join(root, "tests", "unit", "fixtures", "bridge-upgrade");

/** Files whose text the fixture keeps, because the upgrade invariants read them. */
const TEXT = /\.(uplugin|modules|json|ini|txt)$/i;

const tar = process.platform === "win32"
  ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe")
  : "tar";

function token() {
  if (process.env.GITHUB_TOKEN) return process.env.GITHUB_TOKEN;
  try {
    return execFileSync("gh", ["auth", "token"], { encoding: "utf-8", stdio: ["ignore", "pipe", "ignore"] }).trim();
  } catch {
    return "";
  }
}

async function releases() {
  const auth = token();
  const headers = { accept: "application/vnd.github+json", ...(auth ? { authorization: `Bearer ${auth}` } : {}) };
  const out = [];
  for (let page = 1; ; page++) {
    const res = await fetch(`https://api.github.com/repos/${repo}/releases?per_page=100&page=${page}`, { headers });
    if (!res.ok) throw new Error(`GitHub answered ${res.status} listing releases`);
    const batch = await res.json();
    out.push(...batch.filter((r) => !r.draft));
    if (batch.length < 100) return out;
  }
}

function sha256(file) {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

async function fetchZip(release) {
  const name = `UE_MCP_Bridge-${release.tag_name.slice(1)}-UE${engine}-${platform}.zip`;
  const asset = release.assets.find((a) => a.name === name);
  if (!asset) return null;
  const dest = path.join(CACHE_DIR, name);
  const manifestAsset = release.assets.find((a) => a.name.endsWith("-manifest.json"));
  const manifest = manifestAsset ? await (await fetch(manifestAsset.browser_download_url)).json() : null;
  const expected = manifest?.assets?.find((a) => a.file === name)?.sha256?.toLowerCase() ?? null;
  if (!fs.existsSync(dest) || (expected && sha256(dest) !== expected)) {
    process.stdout.write(`downloading ${name} (${(asset.size / 1e6).toFixed(0)} MB)\n`);
    const res = await fetch(asset.browser_download_url);
    if (!res.ok) throw new Error(`${name}: ${res.status}`);
    fs.writeFileSync(`${dest}.part`, Buffer.from(await res.arrayBuffer()));
    fs.renameSync(`${dest}.part`, dest);
  }
  const actual = sha256(dest);
  if (expected && actual !== expected) throw new Error(`${name} does not match its manifest hash`);
  return { name, file: dest, sha256: actual };
}

function walk(dir, base = dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const full = path.join(dir, e.name);
    return e.isDirectory() ? walk(full, base) : [path.relative(base, full).replace(/\\/g, "/")];
  });
}

function record(version, zip) {
  const work = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-fixture-"));
  try {
    execFileSync(tar, ["-xf", zip.file, "-C", work], { stdio: ["ignore", "ignore", "pipe"] });
    const plugin = fs.existsSync(path.join(work, "UE_MCP_Bridge.uplugin")) ? work : path.join(work, "UE_MCP_Bridge");
    const files = walk(plugin).sort().map((rel) => {
      const full = path.join(plugin, rel);
      const entry = { path: rel, size: fs.statSync(full).size, sha256: sha256(full) };
      return TEXT.test(rel) ? { ...entry, text: fs.readFileSync(full, "utf-8") } : entry;
    });
    const fixture = { version, engine, platform, zip: zip.name, zipSha256: zip.sha256, nested: plugin !== work, files };
    fs.writeFileSync(path.join(FIXTURE_DIR, `${version}.json`), JSON.stringify(fixture, null, 1) + "\n");
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

async function main() {
  fs.mkdirSync(CACHE_DIR, { recursive: true });
  fs.mkdirSync(FIXTURE_DIR, { recursive: true });
  if (offline) {
    for (const name of fs.readdirSync(CACHE_DIR).filter((f) => f.endsWith(`-UE${engine}-${platform}.zip`))) {
      const version = name.slice("UE_MCP_Bridge-".length, -`-UE${engine}-${platform}.zip`.length);
      record(version, { name, file: path.join(CACHE_DIR, name), sha256: sha256(path.join(CACHE_DIR, name)) });
    }
    return;
  }
  for (const release of await releases()) {
    const zip = await fetchZip(release);
    if (!zip) continue;
    record(release.tag_name.slice(1), zip);
    process.stdout.write(`recorded ${release.tag_name}\n`);
  }
}

await main();
