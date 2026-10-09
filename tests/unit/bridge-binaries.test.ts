/**
 * Prebuilt binaries replace the plugin tree by staged rename and leave a
 * `binary` marker, with no Source for UnrealBuildTool to find newer.
 */
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as http from "node:http";
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  binaryArchiveName,
  binaryArchiveUrl,
  binaryManifestName,
  installBinaryPlugin,
  resolveBinarySource,
} from "../../src/editor/bridge-binaries.js";
import { readInstallMarker } from "../../src/editor/install-marker.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

const target = { version: "1.2.3", engine: "5.7", platform: "Win64" };
let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-bin-");
});

afterEach(() => {
  fixture.cleanup();
});

function prebuilt(dir: string, platform = "Win64"): string {
  const root = path.join(dir, "UE_MCP_Bridge");
  fs.mkdirSync(path.join(root, "Binaries", platform), { recursive: true });
  fs.mkdirSync(path.join(root, "Source"), { recursive: true });
  fs.writeFileSync(path.join(root, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: "1.2.3" }));
  fs.writeFileSync(path.join(root, "Binaries", platform, "UnrealEditor-UE_MCP_Bridge.dll"), "bin");
  fs.writeFileSync(path.join(root, "Source", "Stray.cpp"), "//");
  return dir;
}

describe("binary source", () => {
  it("names one archive per version, engine and platform", () => {
    expect(binaryArchiveName(target)).toBe("UE_MCP_Bridge-1.2.3-UE5.7-Win64.zip");
    expect(binaryArchiveUrl(target, "https://cdn.example/b/")).toBe(
      "https://cdn.example/b/v1.2.3/UE_MCP_Bridge-1.2.3-UE5.7-Win64.zip",
    );
  });

  it("defaults to the GitHub release and honours an explicit directory", () => {
    expect(resolveBinarySource(target, null)).toEqual({ kind: "url", url: binaryArchiveUrl(target) });
    const dir = prebuilt(path.join(fixture.root, "src"));
    expect(resolveBinarySource(target, dir)).toEqual({ kind: "dir", path: path.resolve(dir) });
  });
});

describe("installBinaryPlugin", () => {
  it("replaces a source tree and leaves no Source behind", async () => {
    const pluginDir = path.join(fixture.root, "Proj", "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(pluginDir, "Source"), { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "{}");

    const result = await installBinaryPlugin(pluginDir, target, { kind: "dir", path: prebuilt(path.join(fixture.root, "src")) });

    expect(result.marker.kind).toBe("binary");
    expect(readInstallMarker(pluginDir)).toEqual({ kind: "binary", engine: "5.7", platform: "Win64", version: "1.2.3" });
    expect(fs.existsSync(path.join(pluginDir, "Source"))).toBe(false);
    expect(fs.existsSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"))).toBe(true);
    expect(fs.readdirSync(path.dirname(pluginDir))).toEqual(["UE_MCP_Bridge"]);
  });

  it("refuses binaries built for another platform and leaves the target alone", async () => {
    const pluginDir = path.join(fixture.root, "Proj", "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(pluginDir, { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "keep");

    await expect(
      installBinaryPlugin(pluginDir, target, { kind: "dir", path: prebuilt(path.join(fixture.root, "mac"), "Mac") }),
    ).rejects.toThrow(/Binaries\/Win64/);
    expect(fs.readFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "utf-8")).toBe("keep");
  });
});

describe("downloads are checked against the release manifest", () => {
  let server: http.Server;
  let base: string;
  const files = new Map<string, Buffer>();

  beforeAll(async () => {
    server = http.createServer((req, res) => {
      const body = files.get((req.url ?? "").split("/").pop() ?? "");
      if (!body) return void res.writeHead(404).end();
      res.writeHead(200).end(body);
    });
    await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
    base = `http://127.0.0.1:${(server.address() as { port: number }).port}/v${target.version}`;
  });
  afterAll(async () => {
    await new Promise<void>((r) => server.close(() => r()));
  });

  /** A real zip of a prebuilt plugin, published with or without a matching manifest entry. */
  function publish(opts: { tamper?: boolean; listed?: boolean } = {}): void {
    files.clear();
    const src = prebuilt(path.join(fixture.root, "zipsrc"));
    const zip = path.join(fixture.root, binaryArchiveName(target));
    const tar = process.platform === "win32" ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe") : "tar";
    execFileSync(tar, ["-a", "-c", "-f", zip, "-C", src, "UE_MCP_Bridge"]);
    const bytes = fs.readFileSync(zip);
    const sha = crypto.createHash("sha256").update(bytes).digest("hex");
    files.set(binaryArchiveName(target), opts.tamper ? Buffer.concat([bytes, Buffer.from("x")]) : bytes);
    const assets = opts.listed === false ? [] : [{ engine: target.engine, platform: target.platform, file: binaryArchiveName(target), sha256: sha, size: bytes.length }];
    files.set(binaryManifestName(target.version), Buffer.from(JSON.stringify({ version: target.version, assets })));
  }

  const install = () => installBinaryPlugin(
    path.join(fixture.root, "Proj", "Plugins", "UE_MCP_Bridge"),
    target,
    { kind: "url", url: `${base}/${binaryArchiveName(target)}` },
  );

  it("installs an archive that matches its manifest", async () => {
    publish();
    const result = await install();
    expect(result.marker.kind).toBe("binary");
  });

  it("refuses an archive whose bytes differ from the manifest", async () => {
    publish({ tamper: true });
    await expect(install()).rejects.toThrow(/does not match its manifest hash/);
  });

  it("refuses an archive the manifest does not list", async () => {
    publish({ listed: false });
    await expect(install()).rejects.toThrow(/not in .*refusing/);
  });

  it("says plainly when a release has no build for this engine", async () => {
    files.clear();
    await expect(install()).rejects.toThrow(/no prebuilt bridge for UE 5\.7 Win64 in ue-mcp 1\.2\.3/);
  });
});
