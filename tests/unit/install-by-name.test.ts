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
import { isInstallName, resolveInstallName } from "../../src/daemon/install-names.js";
import { readExtensionManifest } from "../../src/daemon/extension-install.js";
import { installEditorPlugin } from "../../src/editor/editor-plugin.js";
import { unrealPlatform } from "../../src/editor/install-marker.js";

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

function pluginZip(version: string, platform = unrealPlatform(), withSource = false): { zip: Buffer; sha256: string } {
  const root = path.join(dir, `src-${version}-${platform}${withSource ? "-src" : ""}`, "My_Plugin");
  fs.mkdirSync(path.join(root, "Binaries", platform), { recursive: true });
  if (withSource) {
    fs.mkdirSync(path.join(root, "Source", "My_Plugin"), { recursive: true });
    fs.writeFileSync(path.join(root, "Source", "My_Plugin", "My_Plugin.Build.cs"), "// module rules");
  }
  fs.writeFileSync(path.join(root, "My_Plugin.uplugin"), JSON.stringify({ VersionName: version, FriendlyName: "My Plugin" }));
  const zip = path.join(dir, `p-${version}-${platform}${withSource ? "-src" : ""}.zip`);
  execFileSync(tar, ["-a", "-c", "-f", zip, "-C", path.dirname(root), "My_Plugin"]);
  const bytes = fs.readFileSync(zip);
  return { zip: bytes, sha256: crypto.createHash("sha256").update(bytes).digest("hex") };
}

describe("install names", () => {
  it("resolves studio to its release manifest and refuses an unknown name", () => {
    delete process.env.UE_MCP_INSTALL_INDEX;
    expect(resolveInstallName("studio")).toEqual({
      title: "Studio",
      manifest: "https://github.com/db-lyon/ue-mcp-studio/releases/latest/download/latest.json",
    });
    expect(() => resolveInstallName("toString")).toThrow(/not something ue-mcp can install by name \(known: studio\)/);
  });

  it("tells a name from a path or URL", () => {
    expect(isInstallName("studio")).toBe(true);
    for (const t of ["studio.zip", "./studio", "https://x/latest.json", "C:\\a\\b.zip", "Studio"]) expect(isInstallName(t)).toBe(false);
  });

  it("reads the index UE_MCP_INSTALL_INDEX names instead, and says when it cannot", () => {
    const file = path.join(dir, "index.json");
    fs.writeFileSync(file, JSON.stringify({ probe: { manifest: "http://127.0.0.1:1/m.json" } }));
    process.env.UE_MCP_INSTALL_INDEX = file;
    expect(resolveInstallName("probe")).toEqual({ title: "probe", manifest: "http://127.0.0.1:1/m.json" });
    expect(() => resolveInstallName("studio")).toThrow(/known: probe/);
    process.env.UE_MCP_INSTALL_INDEX = path.join(dir, "missing.json");
    expect(() => resolveInstallName("probe")).toThrow(/UE_MCP_INSTALL_INDEX .* is not readable JSON/);
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

  it("installs another platform's build that carries its source, for Unreal to compile in the project", async () => {
    const uproject = project("5.7");
    const other = pluginZip("0.2.0", unrealPlatform() === "Win64" ? "Linux" : "Win64", true);
    await serving({
      "/plugin.json": { version: "0.2.0", files: { "5.7": { url: "o.zip", sha256: other.sha256 } } },
      "/o.zip": other.zip,
    }, async (base) => {
      const p = await installEditorPlugin(`${base}/plugin.json`, uproject);
      expect(p.compiles).toBe(true);
      expect(fs.existsSync(path.join(p.dir, "Source", "My_Plugin", "My_Plugin.Build.cs"))).toBe(true);
    });
  });
});
