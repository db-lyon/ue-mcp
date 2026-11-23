/**
 * Prebuilt bridge binaries and the engine build they load in (#2072). Unreal
 * skips a module whose manifest BuildId differs from the engine's, so a zip
 * built on one preview or hotfix asks to rebuild on every launch of another.
 * The install refuses it, falls back to a source build where a toolchain
 * exists, swaps whole folders so a failure leaves the old tree, writes the
 * marker last from the tree on disk, and the install check reports a tree that
 * would not load as bridge_engine_mismatch with a rebuild action.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { spawn } from "node:child_process";
import {
  EngineBuildMismatchError,
  buildIdsDiffer,
  engineBuildId,
  inspectEngineMatch,
  pluginBuildId,
  projectEngineBuildId,
  readBuildId,
} from "../../src/editor/engine-build-id.js";
import { installBinaryPlugin, stageBinaryPlugin, swapInStaged, writeMarkerFromTree } from "../../src/editor/bridge-binaries.js";
import { applyUpdate, prepareBridgeForLaunch, sourceForMismatchedBinary } from "../../src/editor/bridge-update.js";
import { readInstallMarker, writeInstallMarker } from "../../src/editor/install-marker.js";
import { inspectInstall } from "../../src/editor/install-check.js";
import { readPendingBuild } from "../../src/editor/pending-build.js";
import { packageVersion } from "../../src/core/package-root.js";
import { ProjectFixture } from "../helpers/project-fixture.js";
import { fakeEngine } from "../helpers/engine-fixture.js";
import { DllHolder, stubModule } from "../helpers/dll-holder.js";

const ENGINE_BUILD = "60351404";
const OTHER_BUILD = "60738873";

let fixture: ProjectFixture;
const saved = { ...process.env };

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-buildid-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  delete process.env.UE_MCP_BINARIES;
  process.env.UE_MCP_TEST_ENGINE_ROOT = fakeEngine(path.join(fixture.root, "UE_5.9"), "5.9", ENGINE_BUILD);
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  fixture.cleanup();
});

/** A prebuilt bridge tree of `version` whose modules are built for `buildId`. */
function prebuilt(dir: string, version: string, buildId: string, dll: string | Buffer = "bin"): string {
  const root = path.join(dir, "UE_MCP_Bridge");
  const bin = path.join(root, "Binaries", "Win64");
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(root, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: version, Modules: [{ Name: "UE_MCP_Bridge", Type: "Editor" }] }));
  fs.writeFileSync(path.join(bin, "UnrealEditor-UE_MCP_Bridge.dll"), dll);
  fs.writeFileSync(path.join(bin, "UnrealEditor.modules"), JSON.stringify({ BuildId: buildId, Modules: { UE_MCP_Bridge: "UnrealEditor-UE_MCP_Bridge.dll" } }));
  return dir;
}

/** A project on UE 5.9 with a binary bridge of `version` built for `buildId` already installed. */
async function binaryProject(version: string, buildId: string): Promise<{ uproject: string; pluginDir: string }> {
  const uproject = fixture.makeProject("Game", { engine: "5.9" });
  const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
  await installBinaryPlugin(pluginDir, { version, engine: "5.9", platform: "Win64" }, { kind: "dir", path: prebuilt(path.join(fixture.root, `old-${version}`), version, buildId) });
  return { uproject, pluginDir };
}

describe("reading BuildIds", () => {
  it("reads the engine's from UnrealEditor.modules, then UnrealEditor.version", () => {
    const root = path.join(fixture.root, "E");
    const bin = path.join(root, "Engine", "Binaries", "Win64");
    fs.mkdirSync(bin, { recursive: true });
    expect(engineBuildId(root, "Win64")).toBeNull();
    fs.writeFileSync(path.join(bin, "UnrealEditor.version"), JSON.stringify({ MajorVersion: 5, MinorVersion: 9, BuildId: "v-id" }));
    expect(engineBuildId(root, "Win64")).toBe("v-id");
    fs.writeFileSync(path.join(bin, "UnrealEditor.modules"), "﻿" + JSON.stringify({ BuildId: "m-id", Modules: {} }));
    expect(engineBuildId(root, "Win64")).toBe("m-id");
    expect(readBuildId(path.join(root, "missing.modules"))).toBeNull();
  });

  it("never calls an unknown side a mismatch", () => {
    expect(buildIdsDiffer("a", "b")).toBe(true);
    expect(buildIdsDiffer("a", "a")).toBe(false);
    expect(buildIdsDiffer(null, "b")).toBe(false);
    expect(buildIdsDiffer("a", undefined)).toBe(false);
  });

  it("reads the BuildId of the engine the project launches with, and not an engine of another version", () => {
    const uproject = fixture.makeProject("Game", { engine: "5.9" });
    expect(projectEngineBuildId(uproject, "Win64")).toBe(ENGINE_BUILD);
    const older = fixture.makeProject("Older", { engine: "5.7" });
    expect(projectEngineBuildId(older, "Win64")).toBeNull();
  });
});

describe("a binary install for another engine build", () => {
  it("is refused before anything is laid down, naming both builds and how to proceed", async () => {
    const { pluginDir } = await binaryProject("1.0.0", ENGINE_BUILD);
    const before = fs.readFileSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), "utf-8");
    const source = { kind: "dir" as const, path: prebuilt(path.join(fixture.root, "new"), "1.1.0", OTHER_BUILD, "new") };

    const err = await installBinaryPlugin(pluginDir, { version: "1.1.0", engine: "5.9", platform: "Win64", engineBuildId: ENGINE_BUILD }, source).catch((e) => e);

    expect(err).toBeInstanceOf(EngineBuildMismatchError);
    expect(err).toMatchObject({ code: "bridge_engine_mismatch", engineBuildId: ENGINE_BUILD, binaryBuildId: OTHER_BUILD });
    expect(err.message).toMatch(new RegExp(`build ${OTHER_BUILD}.*build ${ENGINE_BUILD}`));
    expect(err.message).toMatch(/C\+\+ toolchain/);
    expect(fs.readFileSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), "utf-8")).toBe(before);
    expect(readInstallMarker(pluginDir)?.version).toBe("1.0.0");
  });

  it("installs when the BuildIds match, or when the engine's cannot be read", async () => {
    const pluginDir = path.join(fixture.makeProject("Game", { engine: "5.9" }), "..", "Plugins", "UE_MCP_Bridge");
    const src = prebuilt(path.join(fixture.root, "same"), "1.1.0", ENGINE_BUILD);
    await installBinaryPlugin(pluginDir, { version: "1.1.0", engine: "5.9", platform: "Win64", engineBuildId: ENGINE_BUILD }, { kind: "dir", path: src });
    expect(pluginBuildId(pluginDir, "Win64")).toBe(ENGINE_BUILD);
    const other = prebuilt(path.join(fixture.root, "unknown"), "1.1.0", OTHER_BUILD);
    await installBinaryPlugin(pluginDir, { version: "1.1.0", engine: "5.9", platform: "Win64", engineBuildId: null }, { kind: "dir", path: other });
    expect(pluginBuildId(pluginDir, "Win64")).toBe(OTHER_BUILD);
  });

  it("falls back to a source install with its build owed when a toolchain exists", async () => {
    const { uproject, pluginDir } = await binaryProject(packageVersion(), OTHER_BUILD);
    const source = prebuilt(path.join(fixture.root, "rel"), packageVersion(), OTHER_BUILD);

    const result = await applyUpdate(uproject, { version: packageVersion(), editorRunning: false, binaries: source, hasToolchain: () => true });

    expect(result).toMatchObject({ ok: true, kind: "source", state: "rebuild", sourceFallback: { engineBuildId: ENGINE_BUILD, binaryBuildId: OTHER_BUILD } });
    expect(readInstallMarker(pluginDir)).toMatchObject({ kind: "source", version: packageVersion() });
    expect(fs.existsSync(path.join(pluginDir, "Source"))).toBe(true);
    expect(fs.existsSync(path.join(pluginDir, "Binaries"))).toBe(false);
    expect(readPendingBuild(uproject)?.reason).toMatch(new RegExp(`${OTHER_BUILD}.*${ENGINE_BUILD}`));
  });

  it("fails without a toolchain and leaves the installed bridge as it was", async () => {
    const { uproject, pluginDir } = await binaryProject("1.0.0", ENGINE_BUILD);
    const source = prebuilt(path.join(fixture.root, "rel"), "1.1.0", OTHER_BUILD);

    const result = await applyUpdate(uproject, { version: "1.1.0", editorRunning: false, binaries: source, hasToolchain: () => false });

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(new RegExp(`build ${OTHER_BUILD}.*this engine is build ${ENGINE_BUILD}`));
    expect(readInstallMarker(pluginDir)).toMatchObject({ kind: "binary", version: "1.0.0" });
    expect(pluginBuildId(pluginDir, "Win64")).toBe(ENGINE_BUILD);
  });

  it("is rebuilt from source before a launch, which builds it", async () => {
    const { uproject, pluginDir } = await binaryProject(packageVersion(), OTHER_BUILD);
    const source = prebuilt(path.join(fixture.root, "rel"), packageVersion(), OTHER_BUILD);
    let builds = 0;

    const ready = await prepareBridgeForLaunch(uproject, {
      binaries: source,
      hasToolchain: () => true,
      build: async () => {
        builds++;
        return { success: true, exitCode: 0, message: "built" };
      },
    });

    expect(ready.ok).toBe(true);
    expect(ready.repaired?.join()).toMatch(/engine build/);
    expect(ready.built?.version).toBe(packageVersion());
    expect(builds).toBe(1);
    expect(readInstallMarker(pluginDir)?.kind).toBe("source");
    expect(readPendingBuild(uproject)).toBeNull();
  });

  it("is replaced by source ahead of a project build, the rebuild the problem offers", async () => {
    const { uproject, pluginDir } = await binaryProject("1.0.0", OTHER_BUILD);
    expect(sourceForMismatchedBinary(uproject, { hasToolchain: () => false })).toMatchObject({ switched: false, error: /toolchain/ });
    expect(readInstallMarker(pluginDir)?.kind).toBe("binary");

    const switched = sourceForMismatchedBinary(uproject, { hasToolchain: () => true });
    expect(switched.switched).toBe(true);
    expect(readInstallMarker(pluginDir)?.kind).toBe("source");
    expect(sourceForMismatchedBinary(uproject, { hasToolchain: () => true }).switched).toBe(false);
  });
});

describe("atomic install", () => {
  it("writes the marker last, from the descriptor on disk, and never for another release", async () => {
    const pluginDir = path.join(path.dirname(fixture.makeProject("Game", { engine: "5.9" })), "Plugins", "UE_MCP_Bridge");
    const t = { version: "1.1.0", engine: "5.9", platform: "Win64" };
    const staged = await stageBinaryPlugin(pluginDir, t, { kind: "dir", path: prebuilt(path.join(fixture.root, "s"), "1.1.0", ENGINE_BUILD) });
    expect(readInstallMarker(staged.staging)).toBeNull();

    const marker = swapInStaged(staged, pluginDir, t);
    expect(marker).toEqual({ kind: "binary", engine: "5.9", platform: "Win64", version: "1.1.0" });
    expect(readInstallMarker(pluginDir)).toEqual(marker);

    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: "1.0.9" }));
    expect(() => writeMarkerFromTree(pluginDir, t)).toThrow(/holds bridge 1\.0\.9, not 1\.1\.0/);
    await expect(
      installBinaryPlugin(pluginDir, { ...t, version: "2.0.0" }, { kind: "dir", path: prebuilt(path.join(fixture.root, "w"), "1.1.0", ENGINE_BUILD) }),
    ).rejects.toThrow(/holds bridge 1\.1\.0, not 2\.0\.0/);
  });

  it.runIf(process.platform === "win32")("moves a folder whose DLLs an editor has loaded out of sight in one piece", async () => {
    const uproject = fixture.makeProject("Game", { engine: "5.9" });
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    await installBinaryPlugin(pluginDir, { version: "1.0.0", engine: "5.9", platform: "Win64" }, { kind: "dir", path: prebuilt(path.join(fixture.root, "a"), "1.0.0", ENGINE_BUILD, stubModule("old")) });
    const holder = new DllHolder();
    await holder.hold([path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll")]);
    try {
      await installBinaryPlugin(pluginDir, { version: "1.1.0", engine: "5.9", platform: "Win64" }, { kind: "dir", path: prebuilt(path.join(fixture.root, "b"), "1.1.0", ENGINE_BUILD, stubModule("new")) });
      expect(readInstallMarker(pluginDir)?.version).toBe("1.1.0");
      expect(JSON.parse(fs.readFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "utf-8")).VersionName).toBe("1.1.0");
      expect(fs.readdirSync(path.dirname(pluginDir))).toEqual(["UE_MCP_Bridge"]);
    } finally {
      await holder.release();
    }
  }, 60_000);

  it.runIf(process.platform === "win32")("leaves the previous tree in place and says why when a file in it is locked", async () => {
    const uproject = fixture.makeProject("Game", { engine: "5.9" });
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    await installBinaryPlugin(pluginDir, { version: "1.0.0", engine: "5.9", platform: "Win64" }, { kind: "dir", path: prebuilt(path.join(fixture.root, "a"), "1.0.0", ENGINE_BUILD) });
    // Opened with no sharing at all, as some tools and scanners do, which forbids renaming the folder around it.
    const locked = path.join(pluginDir, "UE_MCP_Bridge.uplugin").replace(/'/g, "''");
    const lock = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command",
      `$f = [System.IO.File]::Open('${locked}', 'Open', 'Read', 'None'); Write-Output held; Start-Sleep -Seconds 120`], { stdio: ["ignore", "pipe", "inherit"], windowsHide: true });
    await new Promise<void>((resolve, reject) => {
      lock.stdout!.on("data", (d: Buffer) => d.toString().includes("held") && resolve());
      lock.on("exit", (code) => reject(new Error(`lock holder exited ${code}`)));
    });
    try {
      const err = await installBinaryPlugin(pluginDir, { version: "1.1.0", engine: "5.9", platform: "Win64" }, { kind: "dir", path: prebuilt(path.join(fixture.root, "b"), "1.1.0", ENGINE_BUILD, "new") }).catch((e) => e);
      expect(err).toBeInstanceOf(Error);
      expect(err.message).toMatch(/could not replace .*previous bridge is unchanged/);
      expect(readInstallMarker(pluginDir)?.version).toBe("1.0.0");
      expect(fs.readFileSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), "utf-8")).toBe("bin");
    } finally {
      // The holder this test started; nothing else is stopped.
      const exited = new Promise<void>((r) => lock.once("exit", () => r()));
      lock.kill();
      await exited;
    }
  }, 60_000);
});

describe("bridge_engine_mismatch", () => {
  const studioManifest = (projectDir: string, buildId: string): void => {
    const studio = path.join(projectDir, "Plugins", "UE_MCP_Studio");
    fs.mkdirSync(path.join(studio, "Binaries", "Win64"), { recursive: true });
    fs.writeFileSync(path.join(studio, "UE_MCP_Studio.uplugin"), JSON.stringify({ VersionName: "1" }));
    fs.writeFileSync(path.join(studio, "Binaries", "Win64", "UnrealEditor.modules"), JSON.stringify({ BuildId: buildId, Modules: {} }));
  };

  it("names nothing for a sound tree", async () => {
    const { pluginDir } = await binaryProject("1.0.0", ENGINE_BUILD);
    expect(inspectEngineMatch({ engineBuildId: ENGINE_BUILD, bridgeDir: pluginDir, platform: "Win64", markerVersion: "1.0.0", descriptorVersion: "1.0.0" })).toBeNull();
  });

  it("reports the bridge, Studio, manifests that disagree and a marker for another release", async () => {
    const { uproject, pluginDir } = await binaryProject("1.0.0", OTHER_BUILD);
    const projectDir = path.dirname(uproject);
    studioManifest(projectDir, "999");
    const m = inspectEngineMatch({
      engineBuildId: ENGINE_BUILD,
      bridgeDir: pluginDir,
      studioDir: path.join(projectDir, "Plugins", "UE_MCP_Studio"),
      platform: "Win64",
      markerVersion: "1.4.16-beta.1",
      descriptorVersion: "1.4.14",
    });
    expect(m).toMatchObject({ engineBuildId: ENGINE_BUILD, bridgeBuildId: OTHER_BUILD, studioBuildId: "999", manifestBuildIds: [OTHER_BUILD, "999"].sort() });
    expect(m?.reasons).toHaveLength(4);
    expect(m?.reasons.join(" ")).toMatch(/install marker says 1\.4\.16-beta\.1 and its descriptor is 1\.4\.14/);
  });

  it("is reported by the install check with both BuildIds and a rebuild action", async () => {
    const { uproject } = await binaryProject("1.0.0", OTHER_BUILD);
    const report = inspectInstall(uproject, { skipToolchain: true });
    const problem = report.problems.find((p) => p.code === "bridge_engine_mismatch");
    expect(problem).toMatchObject({ action: "rebuild", engineMismatch: { engineBuildId: ENGINE_BUILD, bridgeBuildId: OTHER_BUILD } });
    expect(problem?.what).toMatch(new RegExp(`${OTHER_BUILD}.*${ENGINE_BUILD}`));
    expect(problem?.fix).toMatch(/project\(action='build'\)/);
  });

  it("is reported for a marker that names another release than the tree holds", async () => {
    const { uproject, pluginDir } = await binaryProject("1.0.0", ENGINE_BUILD);
    writeInstallMarker(pluginDir, { kind: "binary", engine: "5.9", platform: "Win64", version: "1.4.16-beta.1" });
    const problem = inspectInstall(uproject, { skipToolchain: true }).problems.find((p) => p.code === "bridge_engine_mismatch");
    expect(problem?.engineMismatch).toMatchObject({ markerVersion: "1.4.16-beta.1", descriptorVersion: "1.0.0" });
  });
});
