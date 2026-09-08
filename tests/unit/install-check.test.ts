/**
 * Installing into a project that has no C++ of its own (T17).
 *
 * The backlog says a Blueprint-only project cannot host the bridge. It can:
 * UnrealBuildTool writes temporary target and module files into
 * `Intermediate/Source/` for any project with a code plugin enabled, and
 * compiles the plugin's modules against them. What a Blueprint-only project
 * usually lacks is a C++ toolchain, and nothing used to say so: `ue-mcp init`
 * deployed the source, wrote the plugin into the .uproject, printed "Setup
 * complete!", and the failure arrived later as an Unreal modal.
 *
 * These cases pin the diagnosis, not the folklore: the project kind is
 * reported and never treated as a blocker, and a missing toolchain is.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, it, expect } from "vitest";
import {
  detectToolchain,
  inspectInstall,
  installWarning,
  readProjectShape,
  type ProbeHooks,
} from "../../src/install-check.js";

const made: string[] = [];

afterEach(() => {
  for (const dir of made.splice(0)) {
    try {
      fs.rmSync(dir, { recursive: true, force: true });
    } catch {
      // A temp directory the OS will reap anyway.
    }
  }
});

/** A throwaway .uproject on disk, with whatever descriptor is handed in. */
function makeProject(
  name: string,
  descriptor: Record<string, unknown>,
  opts: { sourceDir?: boolean; deployBridge?: boolean; compiled?: boolean } = {},
): string {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-install-"));
  made.push(root);
  const dir = path.join(root, name);
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify(descriptor, null, "\t"));
  if (opts.sourceDir) fs.mkdirSync(path.join(dir, "Source", name), { recursive: true });
  if (opts.deployBridge) {
    const pluginDir = path.join(dir, "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(pluginDir, "Source"), { recursive: true });
    fs.writeFileSync(
      path.join(pluginDir, "UE_MCP_Bridge.uplugin"),
      JSON.stringify({ FileVersion: 3, VersionName: "0.3.0", FriendlyName: "UE MCP Bridge", Modules: [] }),
    );
    fs.writeFileSync(path.join(pluginDir, "Source", "Placeholder.cpp"), "// source\n");
    if (opts.compiled) {
      const binDir = path.join(pluginDir, "Binaries", "Win64");
      fs.mkdirSync(binDir, { recursive: true });
      fs.writeFileSync(path.join(binDir, "UnrealEditor-UE_MCP_Bridge.dll"), "binary");
      // The freshness check compares mtimes, so a binary that is meant to be
      // current has to be written after the source it was built from.
      const future = new Date(Date.now() + 60_000);
      fs.utimesSync(path.join(binDir, "UnrealEditor-UE_MCP_Bridge.dll"), future, future);
    }
  }
  return uproject;
}

const BLUEPRINT_ONLY = { FileVersion: 3, EngineAssociation: "5.8", Category: "", Description: "" };

/** A probe that reports whatever toolchain the case wants, with no I/O. */
function hooksWith(present: boolean): ProbeHooks {
  return {
    platform: "win32",
    env: { ProgramFiles: "C:\\Program Files", "ProgramFiles(x86)": "C:\\Program Files (x86)" },
    exists: (candidate) => present && candidate.includes("vswhere.exe"),
    run: () => (present ? "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community" : null),
  };
}

describe("readProjectShape", () => {
  it("calls a project with no modules and no Source tree Blueprint-only", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY);
    const shape = readProjectShape(uproject);
    expect(shape.kind).toBe("blueprint-only");
    expect(shape.hasSourceDir).toBe(false);
    expect(shape.declaredModules).toEqual([]);
    expect(shape.bridgeEnabled).toBe(false);
  });

  it("calls a project with declared modules a code project", () => {
    const uproject = makeProject("Coded", {
      ...BLUEPRINT_ONLY,
      Modules: [{ Name: "Coded", Type: "Runtime" }],
    }, { sourceDir: true });
    const shape = readProjectShape(uproject);
    expect(shape.kind).toBe("code");
    expect(shape.declaredModules).toEqual(["Coded"]);
  });

  it("reads the bridge out of the enabled plugin list, and ignores a disabled one", () => {
    const enabled = readProjectShape(
      makeProject("On", { ...BLUEPRINT_ONLY, Plugins: [{ Name: "UE_MCP_Bridge", Enabled: true }] }),
    );
    expect(enabled.bridgeEnabled).toBe(true);

    const disabled = readProjectShape(
      makeProject("Off", { ...BLUEPRINT_ONLY, Plugins: [{ Name: "UE_MCP_Bridge", Enabled: false }] }),
    );
    expect(disabled.bridgeEnabled).toBe(false);
  });
});

describe("detectToolchain", () => {
  it("names the install it found", () => {
    const report = detectToolchain(hooksWith(true));
    expect(report.present).toBe(true);
    expect(report.name).toContain("Visual Studio");
    expect(report.detail).toContain("2022");
  });

  it("names what to install, and every place it looked, when there is none", () => {
    const report = detectToolchain(hooksWith(false));
    expect(report.present).toBe(false);
    expect(report.name).toBeNull();
    expect(report.fix).toContain("Desktop development with C++");
    expect(report.probed.length).toBeGreaterThan(1);
    expect(report.probed.some((p) => p.includes("vswhere"))).toBe(true);
  });

  it("looks for clang on a platform that has no Visual Studio", () => {
    const report = detectToolchain({
      platform: "linux",
      env: {},
      exists: () => false,
      run: (command) => (command === "clang++" ? "clang version 18.1.2" : null),
    });
    expect(report.present).toBe(true);
    expect(report.name).toBe("clang++");
  });
});

describe("inspectInstall", () => {
  it("reports a Blueprint-only project as installable, with the reason", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const report = inspectInstall(uproject, { hooks: hooksWith(true) });

    expect(report.project.kind).toBe("blueprint-only");
    // The whole point: the kind is described, never turned into a problem.
    expect(report.problems.map((p) => p.code)).not.toContain("no_toolchain");
    expect(report.problems.every((p) => !p.what.includes("Blueprint-only"))).toBe(true);
    expect(report.project.note).toContain("UnrealBuildTool generates temporary target");
  });

  it("names a missing toolchain as the blocker it is, with the fix", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const report = inspectInstall(uproject, { hooks: hooksWith(false) });

    const problem = report.problems.find((p) => p.code === "no_toolchain");
    expect(problem, "a machine with no compiler cannot build the bridge").toBeDefined();
    expect(problem!.fix).toContain("Visual Studio");
    expect(report.ok).toBe(false);
    expect(report.nextSteps).toContain(problem!.fix);
  });

  it("separates a plugin that was never deployed from one that was never built", () => {
    const bare = inspectInstall(makeProject("Bare", BLUEPRINT_ONLY), {
      hooks: hooksWith(true),
    });
    expect(bare.problems.map((p) => p.code)).toContain("bridge_not_deployed");
    expect(bare.bridge.deployed).toBe(false);
    expect(bare.problems.find((p) => p.code === "bridge_not_deployed")!.fix).toContain("ue-mcp init");

    const deployed = inspectInstall(
      makeProject("Deployed", BLUEPRINT_ONLY, { deployBridge: true }),
      { hooks: hooksWith(true) },
    );
    expect(deployed.bridge.deployed).toBe(true);
    expect(deployed.bridge.compiled).toBe(false);
    const never = deployed.problems.find((p) => p.code === "bridge_never_compiled");
    expect(never).toBeDefined();
    expect(never!.fix).toContain("STOPPED");
  });

  it("reports a deployed plugin the .uproject does not enable, and changes nothing", () => {
    const uproject = makeProject("Deployed", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const before = fs.readFileSync(uproject, "utf-8");

    const report = inspectInstall(uproject, { hooks: hooksWith(true) });
    expect(report.bridge.deployed).toBe(true);
    expect(report.bridge.enabledInUproject).toBe(false);
    expect(report.problems.map((p) => p.code)).toContain("bridge_not_enabled");

    // The report promises it is read-only. deployer.attach() answers the same
    // version question and enables the plugin as a side effect, which would
    // leave the report contradicting the descriptor it had just rewritten.
    expect(fs.readFileSync(uproject, "utf-8")).toBe(before);
  });

  it("reads the version out of the descriptor whatever its filename casing", () => {
    const uproject = makeProject("Versioned", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const report = inspectInstall(uproject, { hooks: hooksWith(true) });
    expect(report.bridge.installedVersion).toBe("0.3.0");
    expect(report.bridge.packagedVersion).toBeTruthy();
  });

  it("refuses a path that is not a .uproject, naming what it wanted", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY);
    expect(() => inspectInstall(path.dirname(uproject), { hooks: hooksWith(true) }))
      .toThrow(/\.uproject/);
    expect(() => inspectInstall(path.join(path.dirname(uproject), "Nope.uproject"), { hooks: hooksWith(true) }))
      .toThrow(/not found/i);
  });

  it("skips the toolchain probe when asked, without inventing a verdict", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const report = inspectInstall(uproject, { skipToolchain: true });
    expect(report.toolchain.detail).toBe("not probed");
    expect(report.problems.map((p) => p.code)).not.toContain("no_toolchain");
  });

  it("always ends on something to do", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    for (const hooks of [hooksWith(true), hooksWith(false)]) {
      const report = inspectInstall(uproject, { hooks });
      expect(report.nextSteps.length).toBeGreaterThan(0);
    }
  });
});

describe("installWarning", () => {
  it("says nothing when nothing is blocking", () => {
    const uproject = makeProject("Ready", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const report = inspectInstall(uproject, { hooks: hooksWith(true) });
    // Engine resolution depends on what is installed on the machine running
    // this, so only assert the shape when the report itself is clean.
    if (report.problems.length === 0) expect(installWarning(report)).toBeNull();
  });

  it("leads with the project kind when the machine cannot compile", () => {
    const uproject = makeProject("BpOnly", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const warning = installWarning(inspectInstall(uproject, { hooks: hooksWith(false) }));
    expect(warning).toContain("Blueprint-only");
    expect(warning).toContain("Visual Studio");
  });
});

describe("project(check_install) over the real dispatcher", () => {
  it("inspects the loaded project, and a named one, without an editor", async () => {
    const { projectTool } = await import("../../src/tools/project.js");
    const { ProjectContext } = await import("../../src/project.js");

    const uproject = makeProject("Dispatched", BLUEPRINT_ONLY, { deployBridge: true, compiled: true });
    const project = new ProjectContext();
    project.setProject(uproject);
    const ctx = {
      project,
      bridge: {
        isConnected: false,
        connect: async () => {},
        retargetProject: () => ({ projectPath: uproject, port: 1, portSource: "derived" as const, verified: true }),
        getTarget: () => ({ projectPath: uproject, port: 1, portSource: "derived" as const, verified: true }),
        call: async () => {
          throw new Error("check_install must never reach the editor");
        },
      },
    };

    const loaded = (await projectTool.handler(ctx as never, {
      action: "check_install",
      skipToolchain: true,
    })) as { project: { path: string; kind: string } };
    expect(loaded.project.kind).toBe("blueprint-only");

    const named = (await projectTool.handler(ctx as never, {
      action: "check_install",
      projectPath: uproject,
      skipToolchain: true,
    })) as { project: { path: string } };
    expect(named.project.path).toBe(path.resolve(uproject));
  });
});
