/**
 * Prebuilt binaries replace the plugin tree by staged rename and leave a
 * `binary` marker, with no Source for UnrealBuildTool to find newer.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  binaryArchiveName,
  binaryArchiveUrl,
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
