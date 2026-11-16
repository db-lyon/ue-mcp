/**
 * The pieces `ue-mcp install <name>` is built from: the name index, the
 * manifest's editor plugin fields, and the editor plugin install.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as http from "node:http";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { isInstallName, OFFICIAL_INSTALL_INDEX, resolveInstallName } from "../../src/daemon/install-names.js";
import { packageVersion } from "../../src/core/package-root.js";
import { assetNames, extensionSources } from "../../scripts/extension-assets.mjs";
import { readExtensionManifest } from "../../src/daemon/extension-install.js";
import { installEditorPlugin } from "../../src/editor/editor-plugin.js";
import { unrealPlatform } from "../../src/editor/install-marker.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const tar = process.platform === "win32" ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe") : "tar";
let dir: string;
const savedIndex = process.env.UE_MCP_INSTALL_INDEX;

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-byname-"));
});
afterEach(() => {
  if (savedIndex === undefined) delete process.env.UE_MCP_INSTALL_INDEX;
  else process.env.UE_MCP_INSTALL_INDEX = savedIndex;
  fs.rmSync(dir, { recursive: true, force: true });
});

/** Serve `routes` (path to JSON value or Buffer) on a loopback port for the length of `fn`. */
async function serving(routes: Record<string, unknown>, fn: (base: string) => Promise<void>): Promise<void> {
  const server = http.createServer((req, res) => {
    const hit = routes[req.url ?? ""];
    if (hit === undefined) res.writeHead(404).end();
    else if (Buffer.isBuffer(hit)) res.writeHead(200).end(hit);
    else res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify(hit));
  });
  await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
  try {
    await fn(`http://127.0.0.1:${(server.address() as { port: number }).port}`);
  } finally {
    await new Promise<void>((r) => server.close(() => r()));
  }
}

function project(engine: string): string {
  const p = path.join(dir, "Game", "Game.uproject");
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, JSON.stringify({ FileVersion: 3, EngineAssociation: engine }));
  return p;
}

function pluginZip(version: string, platform = unrealPlatform()): { zip: Buffer; sha256: string } {
  const root = path.join(dir, `src-${version}-${platform}`, "My_Plugin");
  fs.mkdirSync(path.join(root, "Binaries", platform), { recursive: true });
  fs.writeFileSync(path.join(root, "My_Plugin.uplugin"), JSON.stringify({ VersionName: version, FriendlyName: "My Plugin" }));
  const zip = path.join(dir, `p-${version}-${platform}.zip`);
  execFileSync(tar, ["-a", "-c", "-f", zip, "-C", path.dirname(root), "My_Plugin"]);
  const bytes = fs.readFileSync(zip);
  return { zip: bytes, sha256: crypto.createHash("sha256").update(bytes).digest("hex") };
}

describe("install names", () => {
  const repoIndex = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "..", "install-index.json"), "utf8")) as Record<string, { title: string; manifest: string }>;

  it("reads the published index from this repository's main branch, so core names no entry", () => {
    expect(OFFICIAL_INSTALL_INDEX).toBe("https://raw.githubusercontent.com/db-lyon/ue-mcp/main/install-index.json");
    const source = fs.readFileSync(path.join(__dirname, "..", "..", "src", "daemon", "install-names.ts"), "utf8");
    for (const name of Object.keys(repoIndex)) expect(source).not.toMatch(new RegExp(`\\b${name}\\b`));
  });

  it("publishes every first-party extension under its own name, at the asset the release attaches", () => {
    for (const src of extensionSources()) {
      const pkg = JSON.parse(fs.readFileSync(path.join(src, "package.json"), "utf8")) as { name: string; ueMcpExtension: { name: string } };
      const entry = repoIndex[pkg.ueMcpExtension.name];
      expect(entry, pkg.ueMcpExtension.name).toBeDefined();
      expect(entry.manifest).toBe(`https://github.com/db-lyon/ue-mcp/releases/download/v{version}/${assetNames(pkg.name, "{version}").manifest}`);
    }
    for (const entry of Object.values(repoIndex)) expect(entry.manifest).toMatch(/^https:\/\/.*\.json$/);
  });

  it("resolves a name from an index served over http, filling in this ue-mcp's version", async () => {
    await serving({ "/index.json": { ...repoIndex, probe: { manifest: "https://example.invalid/v{version}/m-{version}.json" } } }, async (base) => {
      process.env.UE_MCP_INSTALL_INDEX = `${base}/index.json`;
      expect(await resolveInstallName("studio")).toEqual(repoIndex.studio);
      expect(await resolveInstallName("probe")).toEqual({ title: "probe", manifest: `https://example.invalid/v${packageVersion()}/m-${packageVersion()}.json` });
      await expect(resolveInstallName("toString")).rejects.toThrow(/not something ue-mcp can install by name \(known: anthropic, probe, studio\)/);
      process.env.UE_MCP_INSTALL_INDEX = `${base}/gone.json`;
      await expect(resolveInstallName("studio")).rejects.toThrow(/could not be read: HTTP 404\. Install from the release manifest URL or zip instead/);
    });
  });

  it("tells a name from a path or URL", () => {
    expect(isInstallName("studio")).toBe(true);
    for (const t of ["studio.zip", "./studio", "https://x/latest.json", "C:\\a\\b.zip", "Studio"]) expect(isInstallName(t)).toBe(false);
  });

  it("reads the index UE_MCP_INSTALL_INDEX names instead, and says when it cannot", async () => {
    const file = path.join(dir, "index.json");
    fs.writeFileSync(file, JSON.stringify({ probe: { manifest: "http://127.0.0.1:1/m.json" } }));
    process.env.UE_MCP_INSTALL_INDEX = file;
    expect(await resolveInstallName("probe")).toEqual({ title: "probe", manifest: "http://127.0.0.1:1/m.json" });
    await expect(resolveInstallName("studio")).rejects.toThrow(/known: probe/);
    fs.writeFileSync(file, "{ not json");
    await expect(resolveInstallName("probe")).rejects.toThrow(/UE_MCP_INSTALL_INDEX .* is not readable JSON/);
    process.env.UE_MCP_INSTALL_INDEX = path.join(dir, "missing.json");
    await expect(resolveInstallName("probe")).rejects.toThrow(/UE_MCP_INSTALL_INDEX .* could not be read/);
  });
});

describe("readExtensionManifest", () => {
  it("resolves the archive and the editor plugin channel against the manifest's URL", async () => {
    await serving({ "/rel/latest.json": { url: "s.zip", sha256: "ab", version: "1.0.0", pluginChannel: "../p/plugin.json", pluginMin: "0.1.0" } }, async (base) => {
      expect(await readExtensionManifest(`${base}/rel/latest.json`)).toEqual({
        url: `${base}/rel/s.zip`, sha256: "ab", version: "1.0.0", pluginChannel: `${base}/p/plugin.json`, pluginMin: "0.1.0",
      });
    });
  });
});

describe("installEditorPlugin", () => {
  it("installs the build for the project's engine into Plugins/, and leaves the same version in place", async () => {
    const uproject = project("5.7");
    const p = pluginZip("0.2.0");
    await serving({ "/plugin.json": { version: "0.2.0", files: { "5.7": { url: "p.zip", sha256: p.sha256 } } }, "/p.zip": p.zip }, async (base) => {
      const first = await installEditorPlugin(`${base}/plugin.json`, uproject);
      expect(first).toMatchObject({ name: "My_Plugin", title: "My Plugin", version: "0.2.0", changed: true });
      expect(first.dir).toBe(path.join(dir, "Game", "Plugins", "My_Plugin"));
      expect(fs.existsSync(path.join(first.dir, "Binaries", unrealPlatform()))).toBe(true);
      expect(fs.readdirSync(path.join(dir, "Game", "Plugins"))).toEqual(["My_Plugin"]);
      expect((await installEditorPlugin(`${base}/plugin.json`, uproject)).changed).toBe(false);
    });
  });

  it("replaces a copy moved into a subfolder of Plugins/ where it is", async () => {
    const uproject = project("5.7");
    const moved = path.join(dir, "Game", "Plugins", "Vendor", "My_Plugin");
    fs.mkdirSync(moved, { recursive: true });
    fs.writeFileSync(path.join(moved, "My_Plugin.uplugin"), JSON.stringify({ VersionName: "0.1.0" }));
    const p = pluginZip("0.2.0");
    await serving({ "/plugin.json": { version: "0.2.0", files: { "5.7": { url: "p.zip", sha256: p.sha256 } } }, "/p.zip": p.zip }, async (base) => {
      expect((await installEditorPlugin(`${base}/plugin.json`, uproject)).dir).toBe(moved);
      expect(JSON.parse(fs.readFileSync(path.join(moved, "My_Plugin.uplugin"), "utf8")).VersionName).toBe("0.2.0");
    });
  });

  it("refuses a channel older than the minimum, an engine with no build, a bad digest and another platform's build", async () => {
    const uproject = project("5.7");
    const p = pluginZip("0.2.0");
    const other = pluginZip("0.2.0", unrealPlatform() === "Win64" ? "Linux" : "Win64");
    await serving({
      "/plugin.json": { version: "0.2.0", files: { "5.7": { url: "p.zip", sha256: p.sha256 } } },
      "/bad.json": { version: "0.2.0", files: { "5.7": { url: "p.zip", sha256: "0".repeat(64) } } },
      "/other.json": { version: "0.2.0", files: { "5.7": { url: "o.zip", sha256: other.sha256 } } },
      "/p.zip": p.zip,
      "/o.zip": other.zip,
    }, async (base) => {
      await expect(installEditorPlugin(`${base}/plugin.json`, uproject, "0.3.0")).rejects.toThrow(/offers 0\.2\.0, older than the 0\.3\.0/);
      await expect(installEditorPlugin(`${base}/plugin.json`, project("5.6"))).rejects.toThrow(/no editor plugin build for UE 5\.6 \(builds exist for 5\.7\)/);
      await expect(installEditorPlugin(`${base}/bad.json`, project("5.7"))).rejects.toThrow(/does not match the expected SHA-256/);
      await expect(installEditorPlugin(`${base}/other.json`, project("5.7"))).rejects.toThrow(new RegExp(`has no build for ${unrealPlatform()}`));
    });
    expect(fs.existsSync(path.join(dir, "Game", "Plugins", "My_Plugin"))).toBe(false);
  });
});
