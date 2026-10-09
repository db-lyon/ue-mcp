/**
 * The install order matrix (plans/in-editor-client.md section 8), CLI rows.
 * Every sequence must land on the same end state, and none may write source
 * over a binary install. The installer is stood in for by what it leaves on
 * disk: a binary plugin tree with its marker.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { apply, plan, type InitChoices } from "../../src/cli/init-core.js";
import { installBinaryPlugin } from "../../src/editor/bridge-binaries.js";
import { readInstallMarker, unrealPlatform } from "../../src/editor/install-marker.js";
import { packageVersion } from "../../src/core/package-root.js";
import type { ProbeHooks } from "../../src/editor/install-check.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

const WITH_TOOLCHAIN: ProbeHooks = { platform: "linux", env: {}, exists: () => true, run: () => "clang version 18" };
const NO_TOOLCHAIN: ProbeHooks = { platform: "linux", env: {}, exists: () => false, run: () => null };

let fixture: ProjectFixture;
let savedEnv: NodeJS.ProcessEnv;
let binaries: string;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-order-");
  savedEnv = { ...process.env };
  // detectMcpClients and the hook registry read the user's home; keep them in the fixture.
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  process.env.UE_MCP_USER_STATE = path.join(fixture.root, "state.json");
  delete process.env.UE_MCP_BINARIES;
  binaries = prebuiltPlugin(path.join(fixture.root, "prebuilt"));
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in savedEnv)) delete process.env[k];
  Object.assign(process.env, savedEnv);
  fixture.cleanup();
});

function prebuiltPlugin(dir: string): string {
  const bin = path.join(dir, "Binaries", unrealPlatform());
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(dir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ FileVersion: 3, VersionName: packageVersion() }));
  fs.writeFileSync(path.join(bin, "UnrealEditor-UE_MCP_Bridge.bin"), "bin");
  return dir;
}

function choices(uproject: string, over: Partial<InitChoices> = {}): InitChoices {
  return { project: uproject, clients: ["claude-code"], pin: true, binaries, ...over };
}

async function init(uproject: string, hooks: ProbeHooks, over: Partial<InitChoices> = {}) {
  const result = await apply(plan(choices(uproject, over), hooks));
  expect(result.error).toBeUndefined();
  expect(result.ok).toBe(true);
  return result;
}

/** The parts of the end state every install order must agree on. */
function endState(uproject: string) {
  const dir = path.dirname(uproject);
  const pluginDir = path.join(dir, "Plugins", "UE_MCP_Bridge");
  const descriptor = JSON.parse(fs.readFileSync(uproject, "utf-8")) as { Plugins: Array<{ Name: string; Enabled: boolean }> };
  const names = descriptor.Plugins.map((p) => p.Name);
  const mcp = JSON.parse(fs.readFileSync(path.join(dir, ".mcp.json"), "utf-8"));
  return {
    marker: readInstallMarker(pluginDir),
    hasSource: fs.existsSync(path.join(pluginDir, "Source")),
    plugins: names,
    duplicatePlugins: names.length !== new Set(names.map((n) => n.toLowerCase())).size,
    config: fs.existsSync(path.join(dir, "ue-mcp.yml")),
    launch: mcp.mcpServers["ue-mcp"].args[1],
  };
}

function expectCommonEndState(s: ReturnType<typeof endState>): void {
  expect(s.plugins).toEqual(expect.arrayContaining(["UE_MCP_Bridge", "PythonScriptPlugin"]));
  expect(s.duplicatePlugins).toBe(false);
  expect(s.config).toBe(true);
  expect(s.launch).toBe(`ue-mcp@${packageVersion()}`);
}

describe("install order matrix", () => {
  it("CLI init only, with a toolchain: source install", async () => {
    const uproject = fixture.makeProject("Src");
    await init(uproject, WITH_TOOLCHAIN);
    const s = endState(uproject);
    expectCommonEndState(s);
    expect(s.marker?.kind).toBe("source");
    expect(s.hasSource).toBe(true);
  });

  it("CLI init only, without a toolchain: binary install", async () => {
    const uproject = fixture.makeProject("NoCompiler");
    const planned = plan(choices(uproject), NO_TOOLCHAIN);
    expect(planned.install.kind).toBe("binary");
    expect(planned.install.reason).toMatch(/no C\+\+ toolchain/);
    await init(uproject, NO_TOOLCHAIN);
    const s = endState(uproject);
    expectCommonEndState(s);
    expect(s.marker).toMatchObject({ kind: "binary", version: packageVersion(), platform: unrealPlatform() });
    expect(s.hasSource).toBe(false);
  });

  it("run twice: idempotent, nothing changes the second time", async () => {
    const uproject = fixture.makeProject("Twice");
    await init(uproject, WITH_TOOLCHAIN);
    const before = fs.readFileSync(uproject, "utf-8");
    const mcpBefore = fs.readFileSync(path.join(path.dirname(uproject), ".mcp.json"), "utf-8");

    const second = await init(uproject, WITH_TOOLCHAIN, { clients: undefined });

    expect(second.steps.filter((s) => s.status !== "unchanged")).toEqual([]);
    expect(fs.readFileSync(uproject, "utf-8")).toBe(before);
    expect(fs.readFileSync(path.join(path.dirname(uproject), ".mcp.json"), "utf-8")).toBe(mcpBefore);
    expect(endState(uproject).duplicatePlugins).toBe(false);
  });

  it("binary path run twice: the second run fetches nothing", async () => {
    const uproject = fixture.makeProject("BinTwice");
    await init(uproject, NO_TOOLCHAIN);
    const planned = plan(choices(uproject), NO_TOOLCHAIN);
    expect(planned.install.writes).toBe(false);
    const second = await apply(planned);
    expect(second.steps.find((s) => s.id === "plugin")?.status).toBe("unchanged");
  });

  it("installer then CLI init: init adopts the binaries and never writes source", async () => {
    const uproject = fixture.makeProject("Adopt");
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    await installBinaryPlugin(pluginDir, { version: packageVersion(), engine: "5.6", platform: unrealPlatform() }, { kind: "dir", path: binaries });

    // Even with a compiler present, what is installed wins.
    const planned = plan(choices(uproject), WITH_TOOLCHAIN);
    expect(planned.install).toMatchObject({ kind: "binary", existing: "binary", writes: false });
    await apply(planned);

    const s = endState(uproject);
    expectCommonEndState(s);
    expect(s.marker?.kind).toBe("binary");
    expect(s.hasSource).toBe(false);
  });

  it("installer then init --install=source is refused", () => {
    const uproject = fixture.makeProject("Refuse");
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    return installBinaryPlugin(pluginDir, { version: packageVersion(), engine: "5.6", platform: unrealPlatform() }, { kind: "dir", path: binaries })
      .then(async () => {
        const planned = plan(choices(uproject, { install: "source" }), WITH_TOOLCHAIN);
        expect(planned.blockers.join()).toMatch(/source over an existing binary install/);
        const result = await apply(planned);
        expect(result.ok).toBe(false);
        expect(fs.existsSync(path.join(pluginDir, "Source"))).toBe(false);
      });
  });

  it("installer then update: an older binary install gets binaries, not source", async () => {
    const uproject = fixture.makeProject("Update");
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    await installBinaryPlugin(pluginDir, { version: "0.0.1", engine: "5.6", platform: unrealPlatform() }, { kind: "dir", path: binaries });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: "0.0.1" }));

    // What `ue-mcp update` runs for a binary install.
    const planned = plan(choices(uproject, { install: "binary" }), WITH_TOOLCHAIN);
    expect(planned.install.writes).toBe(true);
    await apply(planned);

    expect(readInstallMarker(pluginDir)).toMatchObject({ kind: "binary", version: packageVersion() });
    expect(fs.existsSync(path.join(pluginDir, "Source"))).toBe(false);
  });

  it("editor open during init: an edit made after plan() survives apply()", async () => {
    const uproject = fixture.makeProject("Open");
    const planned = plan(choices(uproject), WITH_TOOLCHAIN);

    // The editor writes the descriptor between plan and apply.
    const edited = JSON.parse(fs.readFileSync(uproject, "utf-8"));
    edited.Plugins = [{ Name: "ModelingToolsEditorMode", Enabled: true }];
    edited.Description = "set in the editor";
    fs.writeFileSync(uproject, JSON.stringify(edited, null, "\t"));

    await apply(planned);

    const after = JSON.parse(fs.readFileSync(uproject, "utf-8"));
    expect(after.Description).toBe("set in the editor");
    expect(after.Plugins.map((p: { Name: string }) => p.Name)).toEqual(
      expect.arrayContaining(["ModelingToolsEditorMode", "UE_MCP_Bridge", "PythonScriptPlugin"]),
    );
  });
});
