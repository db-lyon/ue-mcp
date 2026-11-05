/**
 * A source bridge update builds the bridge (#1593). Unreal rebuilds a module on
 * start only when its DLL is missing, never when its source is newer, so an
 * update that deployed source and stopped left every restart on the old DLL.
 *
 * With the editor down the update builds before it reports success and reports
 * a failed build. With it running the build is recorded as owed, and the next
 * start through ue-mcp (prepareBridgeForLaunch) builds first.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { applyUpdate, prepareBridgeForLaunch, type BridgeBuild } from "../../src/editor/bridge-update.js";
import { pendingBuildPath, readPendingBuild, recordPendingBuild } from "../../src/editor/pending-build.js";
import { inspectInstall } from "../../src/editor/install-check.js";
import { invalidatePluginFreshness } from "../../src/editor/bridge-freshness.js";
import { packageVersion } from "../../src/core/package-root.js";
import { deploy } from "../../src/editor/deployer.js";
import { ProjectContext } from "../../src/config/project.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
const saved = { ...process.env };

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-srcbuild-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  invalidatePluginFreshness();
  fixture.cleanup();
});

const VERSION = packageVersion();

function okBuild(): BridgeBuild & ReturnType<typeof vi.fn> {
  return vi.fn(async () => ({ success: true, exitCode: 0, message: "Build succeeded (GameEditor Win64 Development)" }));
}

function failedBuild(): BridgeBuild & ReturnType<typeof vi.fn> {
  return vi.fn(async () => ({
    success: false,
    exitCode: 6,
    message: "Build failed with exit code 6. Result: Failed (OtherCompilationError)",
    resultLine: "Result: Failed (OtherCompilationError)",
    errors: ["Foo.cpp(12,3): error C2065: 'Bar': undeclared identifier"],
  }));
}

/** A project with this package's bridge source deployed, never built. */
function deployedProject(name: string): string {
  const uproject = fixture.makeProject(name, { engine: "5.7" });
  const project = new ProjectContext();
  project.setProject(uproject);
  const r = deploy(project);
  expect(r.error).toBeFalsy();
  return uproject;
}

/** A deployed project whose update ran with the editor up, so it owes a build. */
async function sourceProject(name: string): Promise<{ uproject: string; pluginDir: string }> {
  const uproject = deployedProject(name);
  const first = await applyUpdate(uproject, { editorRunning: true });
  expect(first.ok, first.error).toBe(true);
  return { uproject, pluginDir: path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge") };
}

describe("a source bridge update with the editor down", () => {
  it("builds before reporting success, and owes nothing afterwards", async () => {
    const uproject = deployedProject("DownGame");
    const build = okBuild();

    const result = await applyUpdate(uproject, { editorRunning: false, build });

    expect(result.ok, result.error).toBe(true);
    expect(result.state).toBe("built");
    expect(result.build?.success).toBe(true);
    expect(build).toHaveBeenCalledTimes(1);
    expect(build).toHaveBeenCalledWith(uproject);
    expect(readPendingBuild(uproject)).toBeNull();
  });

  it("reports a failed build with the compiler's errors, and keeps the build owed", async () => {
    const uproject = deployedProject("FailGame");

    const result = await applyUpdate(uproject, { editorRunning: false, build: failedBuild() });

    expect(result.ok).toBe(false);
    expect(result.state).toBe("rebuild");
    expect(result.error).toContain(`the bridge ${VERSION} source is deployed, but building it failed`);
    expect(result.error).toContain("OtherCompilationError");
    expect(result.build?.errors).toEqual(["Foo.cpp(12,3): error C2065: 'Bar': undeclared identifier"]);
    expect(readPendingBuild(uproject)).toMatchObject({ version: VERSION, failed: { message: expect.stringContaining("exit code 6") } });
  });

  it("finishes an earlier update that deployed without building when run again", async () => {
    const { uproject } = await sourceProject("AgainGame");
    expect(readPendingBuild(uproject)).not.toBeNull();
    const build = okBuild();

    // The source is already current, so the deploy changes nothing; the owed build still runs.
    const again = await applyUpdate(uproject, { editorRunning: false, build });

    expect(again.ok, again.error).toBe(true);
    expect(again.state).toBe("built");
    expect(build).toHaveBeenCalledTimes(1);
    expect(readPendingBuild(uproject)).toBeNull();
  });
});

describe("a source bridge update with the editor running", () => {
  it("deploys, builds nothing, and records the build as owed", async () => {
    const uproject = deployedProject("UpGame");
    const build = okBuild();
    const events: string[] = [];

    const result = await applyUpdate(uproject, { editorRunning: true, build, onEvent: (t) => events.push(t) });

    expect(result.ok, result.error).toBe(true);
    expect(result.state).toBe("rebuild");
    expect(build).not.toHaveBeenCalled();
    expect(events).toEqual(["update.applied"]);
    const owed = readPendingBuild(uproject);
    expect(owed).toMatchObject({ version: VERSION, reason: "editor running" });
    // Under Intermediate/, which a redeploy keeps.
    expect(pendingBuildPath(path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge"))).toContain(`${path.sep}Intermediate${path.sep}`);
  });

  it("keeps the owed build across a second deploy", async () => {
    const { uproject } = await sourceProject("TwiceGame");
    const second = await applyUpdate(uproject, { editorRunning: true, build: okBuild() });
    expect(second.ok, second.error).toBe(true);
    expect(readPendingBuild(uproject)).not.toBeNull();
  });
});

describe("the next start after a source update", () => {
  it("builds the owed bridge before the launch and clears it", async () => {
    const { uproject } = await sourceProject("StartGame");
    const build = okBuild();

    const prepared = await prepareBridgeForLaunch(uproject, { build });

    expect(prepared.ok).toBe(true);
    expect(prepared.built).toEqual({ version: VERSION, message: "Build succeeded (GameEditor Win64 Development)" });
    expect(build).toHaveBeenCalledTimes(1);
    expect(readPendingBuild(uproject)).toBeNull();

    // Nothing is owed now, so the following start builds nothing.
    const next = await prepareBridgeForLaunch(uproject, { build });
    expect(next).toEqual({ ok: true });
    expect(build).toHaveBeenCalledTimes(1);
  });

  it("refuses the launch when the build fails, naming the error, and keeps it owed", async () => {
    const { uproject } = await sourceProject("RefuseGame");

    const prepared = await prepareBridgeForLaunch(uproject, { build: failedBuild() });

    expect(prepared.ok).toBe(false);
    expect(prepared.message).toContain(`the bridge ${VERSION} source was deployed and not built yet, and building it failed`);
    expect(prepared.message).toContain("OtherCompilationError");
    expect(readPendingBuild(uproject)?.failed?.message).toContain("OtherCompilationError");
  });

  it("opens the previous build on the start after a failed one, saying so, without building again", async () => {
    const { uproject } = await sourceProject("AfterFailGame");
    await prepareBridgeForLaunch(uproject, { build: failedBuild() });
    const build = okBuild();

    const next = await prepareBridgeForLaunch(uproject, { build });

    expect(next.ok).toBe(true);
    expect(next.buildFailed?.version).toBe(VERSION);
    expect(next.buildFailed?.message).toContain("OtherCompilationError");
    expect(build).not.toHaveBeenCalled();

    // A new update resets it, so the start after that builds again.
    expect((await applyUpdate(uproject, { editorRunning: true })).ok).toBe(true);
    expect(readPendingBuild(uproject)?.failed).toBeUndefined();
    expect((await prepareBridgeForLaunch(uproject, { build })).built?.version).toBe(VERSION);
  });

  it("builds nothing for a source bridge that owes no build", async () => {
    const uproject = fixture.makeProject("NothingOwed", { engine: "5.7" });
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(pluginDir, { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "{}");
    const build = okBuild();
    expect(await prepareBridgeForLaunch(uproject, { build })).toEqual({ ok: true });
    expect(build).not.toHaveBeenCalled();
  });
});

describe("the install report", () => {
  it("names an owed build under plugin.pendingBuild and says how it is paid", async () => {
    const { uproject, pluginDir } = await sourceProject("ReportGame");
    // A compiled binary older than the source, as the editor left it.
    const bin = path.join(pluginDir, "Binaries", "Win64");
    fs.mkdirSync(bin, { recursive: true });
    const dll = path.join(bin, "UnrealEditor-UE_MCP_Bridge.dll");
    fs.writeFileSync(dll, "old");
    const past = new Date(Date.now() - 86_400_000);
    fs.utimesSync(dll, past, past);
    invalidatePluginFreshness(uproject);

    const report = inspectInstall(uproject, { skipToolchain: true });

    expect(report.bridge.pendingBuild).toMatchObject({ version: VERSION, reason: "editor running" });
    const stale = report.problems.find((p) => p.code === "bridge_build_stale");
    expect(stale?.fix).toContain("restart_editor");
  });

  it("reports no owed build once a build cleared it", async () => {
    const { uproject } = await sourceProject("ClearedGame");
    recordPendingBuild(uproject, VERSION, "editor running");
    await prepareBridgeForLaunch(uproject, { build: okBuild() });
    expect(inspectInstall(uproject, { skipToolchain: true }).bridge.pendingBuild).toBeNull();
  });
});
