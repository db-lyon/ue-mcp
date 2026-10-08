/**
 * The install marker decides who may write the plugin tree. Source written over
 * a binary install is newer than the binaries, and the next launch becomes a
 * rebuild prompt that fails on a machine without a toolchain.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { attach, deploy } from "../../src/editor/deployer.js";
import { checkPluginFreshness, invalidatePluginFreshness } from "../../src/editor/bridge-freshness.js";
import {
  INSTALL_MARKER_NAME,
  installKindOf,
  readInstallMarker,
  resolvePluginDir,
  writeInstallMarker,
} from "../../src/editor/install-marker.js";
import { ProjectContext } from "../../src/config/project.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-marker-");
});

afterEach(() => {
  fixture.cleanup();
});

function contextFor(uproject: string): ProjectContext {
  const context = new ProjectContext();
  context.setProject(uproject);
  return context;
}

/** A prebuilt plugin: descriptor, binaries, marker, no Source. */
function placeBinaryInstall(pluginDir: string): void {
  fs.mkdirSync(path.join(pluginDir, "Binaries", "Win64"), { recursive: true });
  fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ FileVersion: 3, VersionName: "0.0.1" }));
  fs.writeFileSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), "bin");
  writeInstallMarker(pluginDir, { kind: "binary", engine: "5.6", platform: "Win64", version: "0.0.1" });
}

describe("install marker", () => {
  it("round-trips and rejects an unknown kind", () => {
    const dir = path.join(fixture.root, "p");
    writeInstallMarker(dir, { kind: "binary", engine: "5.7", platform: "Mac", version: "1.2.3" });
    expect(readInstallMarker(dir)).toEqual({ kind: "binary", engine: "5.7", platform: "Mac", version: "1.2.3" });
    fs.writeFileSync(path.join(dir, INSTALL_MARKER_NAME), JSON.stringify({ kind: "other" }));
    expect(readInstallMarker(dir)).toBeNull();
  });

  it("infers the kind of a tree that predates the marker", () => {
    const dir = path.join(fixture.root, "legacy");
    fs.mkdirSync(path.join(dir, "Source"), { recursive: true });
    expect(installKindOf(dir)).toBeNull();
    fs.writeFileSync(path.join(dir, "UE_MCP_Bridge.uplugin"), "{}");
    expect(installKindOf(dir)).toBe("source");
    fs.rmSync(path.join(dir, "Source"), { recursive: true });
    fs.mkdirSync(path.join(dir, "Binaries"));
    expect(installKindOf(dir)).toBe("binary");
  });
});

describe("deploy", () => {
  it("writes a source marker on a source install", () => {
    const uproject = fixture.makeProject("Src");
    const result = deploy(contextFor(uproject));
    expect(result.error).toBeUndefined();
    expect(result.installKind).toBe("source");
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    expect(readInstallMarker(pluginDir)?.kind).toBe("source");
    expect(fs.existsSync(path.join(pluginDir, "Source"))).toBe(true);
  });

  it("is idempotent: a second deploy writes no plugin file and keeps the marker", () => {
    const uproject = fixture.makeProject("Twice");
    deploy(contextFor(uproject));
    const before = fs.readFileSync(uproject, "utf-8");
    const second = deploy(contextFor(uproject));
    expect(second.cppPluginDeployed).toBe(false);
    expect(second.cppPluginEnabled).toBe(false);
    expect(fs.readFileSync(uproject, "utf-8")).toBe(before);
    expect(readInstallMarker(path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge"))?.kind).toBe("source");
  });

  it("never writes source over a binary install", () => {
    const uproject = fixture.makeProject("Bin");
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    placeBinaryInstall(pluginDir);

    const result = deploy(contextFor(uproject));

    expect(result.error).toBeUndefined();
    expect(result.installKind).toBe("binary");
    expect(result.cppPluginDeployed).toBe(false);
    expect(result.skipped).toMatch(/binary/);
    expect(fs.existsSync(path.join(pluginDir, "Source"))).toBe(false);
    expect(readInstallMarker(pluginDir)?.kind).toBe("binary");
    // The .uproject is still wired up.
    expect(fs.readFileSync(uproject, "utf-8")).toContain("UE_MCP_Bridge");
  });
});

describe("plugin location", () => {
  it("prefers the live editor's recorded plugin directory", () => {
    const uproject = fixture.makeProject("Rec");
    const projectDir = path.dirname(uproject);
    const moved = path.join(fixture.root, "elsewhere", "UE_MCP_Bridge");
    placeBinaryInstall(moved);
    const instances = path.join(projectDir, "Saved", "UE_MCP_Bridge", "instances");
    fs.mkdirSync(instances, { recursive: true });
    fs.writeFileSync(
      path.join(instances, "4242.json"),
      JSON.stringify({ pid: 4242, port: 50000, instanceId: "x", state: "listening", pluginDir: moved.replace(/\\/g, "/") }),
    );

    const found = resolvePluginDir(projectDir, (pid) => pid === 4242);
    expect(found).toEqual({ dir: path.resolve(moved), source: "instance-record" });
  });

  it("finds a plugin moved into a Plugins subfolder", () => {
    const uproject = fixture.makeProject("Scan");
    const nested = path.join(path.dirname(uproject), "Plugins", "Tools", "UE_MCP_Bridge");
    placeBinaryInstall(nested);
    expect(resolvePluginDir(path.dirname(uproject), () => false)).toEqual({ dir: nested, source: "scan" });

    const result = attach(contextFor(uproject));
    expect(result.cppPluginPresent).toBe(true);
    expect(result.pluginDir).toBe(nested);
    expect(result.pluginDirSource).toBe("scan");
    expect(result.installKind).toBe("binary");
  });
});

describe("freshness", () => {
  it("does not apply source timestamps to a binary install", () => {
    const uproject = fixture.makeProject("Fresh");
    placeBinaryInstall(path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge"));
    invalidatePluginFreshness(uproject);
    const verdict = checkPluginFreshness(uproject);
    expect(verdict.stale).toBe(false);
    expect(verdict.checked).toBe(false);
    expect(verdict.reason).toMatch(/binary/);
  });
});
