/**
 * applyUpdate checks the installed bridge before it reports success
 * (verifyBridgeTree) and puts the previous files back when the check fails,
 * with the editor down (whole-tree swap) and running (in-place swap). A
 * release that would not load is refused before anything is touched, and two
 * applies for one project never swap over each other.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { applyUpdate, prepareBridgeForLaunch, readPendingUpdate } from "../../src/editor/bridge-update.js";
import { treeFiles, verifyBridgeTree } from "../../src/editor/bridge-tree.js";
import { readInstallMarker, writeInstallMarker } from "../../src/editor/install-marker.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
const saved = { ...process.env };

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-rollback-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  delete process.env.UE_MCP_BINARIES;
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  fixture.cleanup();
});

/** A plugin tree of `version` with `modules`, every one with its DLL unless `missing` names it. */
function tree(dir: string, version: string, modules: string[], missing: string[] = []): void {
  const bin = path.join(dir, "Binaries", "Win64");
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(dir, "UE_MCP_Bridge.uplugin"), JSON.stringify({
    VersionName: version,
    Modules: modules.map((Name) => ({ Name, Type: "Editor" })),
  }));
  fs.writeFileSync(path.join(bin, "UnrealEditor.modules"), JSON.stringify({
    BuildId: "55116800",
    Modules: Object.fromEntries(modules.map((m) => [m, `UnrealEditor-${m}.dll`])),
  }));
  for (const m of modules) if (!missing.includes(m)) fs.writeFileSync(path.join(bin, `UnrealEditor-${m}.dll`), `${version} ${m}`);
}

function project(version: string, engine = "5.8"): { uproject: string; pluginDir: string } {
  const uproject = fixture.makeProject("Game", { engine });
  const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
  tree(pluginDir, version, ["UE_MCP_BridgeStatus", "UE_MCP_Bridge"]);
  writeInstallMarker(pluginDir, { kind: "binary", engine, platform: "Win64", version });
  return { uproject, pluginDir };
}

function release(version: string, modules = ["UE_MCP_BridgeStatus", "UE_MCP_BridgeRuntime", "UE_MCP_Bridge"], missing: string[] = []): string {
  const dir = path.join(fixture.root, `release-${version}`, "UE_MCP_Bridge");
  tree(dir, version, modules, missing);
  return dir;
}

/** Every file and its bytes, to prove a restore is exact. */
function snapshot(dir: string): Record<string, string> {
  return Object.fromEntries(treeFiles(dir).sort().map((f) => [f, fs.readFileSync(path.join(dir, f), "utf-8")]));
}

const failingCheck = () => ["module UE_MCP_BridgeRuntime has no Binaries/Win64/UnrealEditor-UE_MCP_BridgeRuntime.dll"];

describe("a post-swap check that fails", () => {
  it.each([
    ["the editor down", { editorRunning: false }],
    ["the editor running on Windows", { editorRunning: true, platform: "win32" as const }],
  ])("puts every previous file back with %s, and names what was wrong", async (_label, mode) => {
    const { uproject, pluginDir } = project("1.4.10");
    const before = snapshot(pluginDir);

    const result = await applyUpdate(uproject, { version: "1.4.11", binaries: release("1.4.11"), verifyTree: failingCheck, ...mode });

    expect(result.ok).toBe(false);
    expect(result.error).toContain("previous one was put back");
    expect(result.error).toContain("UE_MCP_BridgeRuntime has no");
    expect(snapshot(pluginDir)).toEqual(before);
    expect(readPendingUpdate(pluginDir)).toBeNull();
    expect(fs.readdirSync(path.dirname(pluginDir))).toEqual(["UE_MCP_Bridge"]);
    expect(verifyBridgeTree(pluginDir, { version: "1.4.10", platform: "Win64" })).toEqual([]);
  });

  it("reports the failure as an update.failed event", async () => {
    const { uproject } = project("1.4.10");
    const events: Array<[string, Record<string, unknown>]> = [];
    await applyUpdate(uproject, {
      version: "1.4.11", binaries: release("1.4.11"), editorRunning: false, verifyTree: failingCheck,
      onEvent: (type, data) => events.push([type, data]),
    });
    expect(events.map(([t]) => t)).toEqual(["update.failed"]);
    expect(String(events[0][1].error)).toContain("UE_MCP_BridgeRuntime");
  });
});

describe("a release that would not load", () => {
  it("is refused before the install is touched", async () => {
    const { uproject, pluginDir } = project("1.4.10");
    const before = snapshot(pluginDir);

    const result = await applyUpdate(uproject, {
      version: "1.4.11", editorRunning: true, platform: "win32",
      binaries: release("1.4.11", undefined, ["UE_MCP_BridgeRuntime"]),
    });

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/would not load: module UE_MCP_BridgeRuntime has no/);
    expect(snapshot(pluginDir)).toEqual(before);
    expect(fs.existsSync(path.join(path.dirname(uproject), "Intermediate", "UE_MCP_Update"))).toBe(false);
  });

  it("is refused when its module manifest disagrees with its descriptor", async () => {
    const { uproject, pluginDir } = project("1.4.10");
    const dir = release("1.4.11");
    const modules = path.join(dir, "Binaries", "Win64", "UnrealEditor.modules");
    fs.writeFileSync(modules, JSON.stringify({ BuildId: "55116800", Modules: { UE_MCP_Bridge: "UnrealEditor-UE_MCP_Bridge.dll" } }));

    const result = await applyUpdate(uproject, { version: "1.4.11", editorRunning: false, binaries: dir });

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/UnrealEditor\.modules lists UE_MCP_Bridge but the descriptor has/);
    expect(readInstallMarker(pluginDir)?.version).toBe("1.4.10");
  });
});

describe("applies for one project", () => {
  it("run one after another, so the second finds the first's result", async () => {
    const { uproject, pluginDir } = project("1.4.10");
    const binaries = release("1.4.11");

    const [a, b] = await Promise.all([
      applyUpdate(uproject, { version: "1.4.11", binaries, editorRunning: true, platform: "win32" }),
      applyUpdate(uproject, { version: "1.4.11", binaries, editorRunning: true, platform: "win32" }),
    ]);

    expect(a).toMatchObject({ ok: true, state: "applied" });
    expect(b).toMatchObject({ ok: true, state: "unchanged" });
    // Nothing maps the aside files here, so the second apply has already cleared them.
    expect(verifyBridgeTree(pluginDir, { version: "1.4.11", platform: "Win64", asideFiles: readPendingUpdate(pluginDir)?.oldFiles ?? [] })).toEqual([]);
  });
});

describe("the engine binaries are chosen for", () => {
  it("is the project's engine, not the one the old marker recorded", async () => {
    const { uproject, pluginDir } = project("1.4.10", "5.7");
    const descriptor = JSON.parse(fs.readFileSync(uproject, "utf-8"));
    fs.writeFileSync(uproject, JSON.stringify({ ...descriptor, EngineAssociation: "5.8" }));

    const result = await applyUpdate(uproject, { version: "1.4.11", binaries: release("1.4.11"), editorRunning: false });

    expect(result, result.error).toMatchObject({ ok: true, state: "applied" });
    expect(readInstallMarker(pluginDir)).toMatchObject({ engine: "5.8", version: "1.4.11" });
  });
});

describe("before the editor launches", () => {
  it("reinstalls a binary bridge that would not load, at its own version", async () => {
    const { uproject, pluginDir } = project("1.4.11");
    // A mixed tree: the new descriptor, one module's DLL missing.
    tree(pluginDir, "1.4.11", ["UE_MCP_BridgeStatus", "UE_MCP_BridgeRuntime", "UE_MCP_Bridge"], ["UE_MCP_BridgeRuntime"]);
    fs.rmSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_BridgeRuntime.dll"), { force: true });

    const prepared = await prepareBridgeForLaunch(uproject, { binaries: release("1.4.11") });

    expect(prepared.ok, prepared.message).toBe(true);
    expect(prepared.repaired?.join(" ")).toContain("UE_MCP_BridgeRuntime has no");
    expect(verifyBridgeTree(pluginDir, { version: "1.4.11", platform: "Win64" })).toEqual([]);
  });

  it("refuses the launch, naming what is wrong, when the reinstall fails too", async () => {
    const { uproject, pluginDir } = project("1.4.11");
    fs.rmSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"));

    const prepared = await prepareBridgeForLaunch(uproject, { binaries: path.join(fixture.root, "nowhere") });

    expect(prepared.ok).toBe(false);
    expect(prepared.message).toContain("module UE_MCP_Bridge has no Binaries/Win64/UnrealEditor-UE_MCP_Bridge.dll");
    expect(prepared.message).toContain("npx ue-mcp@1.4.11 update --apply-only");
  });

  it("leaves a sound install alone and clears spare copies under Plugins/", async () => {
    const { uproject, pluginDir } = project("1.4.10");
    const spare = path.join(path.dirname(pluginDir), "UE_MCP_Bridge.old-777");
    tree(spare, "1.4.9", ["UE_MCP_Bridge"]);
    const before = snapshot(pluginDir);

    const prepared = await prepareBridgeForLaunch(uproject, { binaries: path.join(fixture.root, "nowhere") });

    expect(prepared).toEqual({ ok: true });
    expect(snapshot(pluginDir)).toEqual(before);
    expect(fs.readdirSync(path.dirname(pluginDir))).toEqual(["UE_MCP_Bridge"]);
  });

  it("does not touch a source install", async () => {
    const uproject = fixture.makeProject("Src", { engine: "5.8" });
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(pluginDir, "Source"), { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "{}");
    writeInstallMarker(pluginDir, { kind: "source", engine: "5.8", platform: "Win64", version: "1.4.11" });

    expect(await prepareBridgeForLaunch(uproject)).toEqual({ ok: true });
  });
});
