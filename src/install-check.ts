/**
 * Whether this project can actually run the bridge, answered before the editor
 * is asked to prove it (T17).
 *
 * The backlog entry for this reads "ue-mcp requires a C++ project", and that
 * turns out to be the wrong diagnosis. UnrealBuildTool treats a project with no
 * `Source/` tree as code-based the moment a code plugin is enabled in it: it
 * writes temporary `<Project>Editor.Target.cs` and `<Project>.Build.cs` files
 * into `Intermediate/Source/` and compiles the plugin's modules against them.
 * The bridge builds into a Blueprint-only project on that path, so the project
 * kind is not the blocker.
 *
 * What IS the blocker is a C++ toolchain. A Blueprint-only project is usually
 * owned by someone who never installed one, and nothing in the install path
 * told them: `ue-mcp init` copies the plugin source in, writes the plugin into
 * the .uproject, prints "Setup complete!", and the failure surfaces much later
 * as an Unreal modal saying modules are missing. An agent cannot see that
 * modal, so the session reads as an editor that will not start.
 *
 * This module answers the question up front and names the fix, on disk, with no
 * editor running and nothing compiled yet.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { checkPluginFreshness, type PluginFreshness } from "./plugin-freshness.js";
import { trySelectEngine } from "./engine-root.js";
import { ProjectContext } from "./project.js";

/**
 * Whether the project declares native modules of its own.
 *
 * `blueprint-only` is a description, not a verdict: see the module comment.
 */
export type ProjectKind = "code" | "blueprint-only";

/** The I/O the toolchain probe does, injectable so it can be tested. */
export interface ProbeHooks {
  platform: NodeJS.Platform;
  env: NodeJS.ProcessEnv;
  exists(candidate: string): boolean;
  /** Trimmed stdout, or null when the command is absent or fails. */
  run(command: string, args: string[]): string | null;
}

export const DEFAULT_PROBE_HOOKS: ProbeHooks = {
  platform: process.platform,
  env: process.env,
  exists: (candidate) => {
    try {
      return fs.existsSync(candidate);
    } catch {
      return false;
    }
  },
  run: (command, args) => {
    try {
      return execFileSync(command, args, {
        encoding: "utf-8",
        stdio: ["ignore", "pipe", "ignore"],
        timeout: 10_000,
      }).trim();
    } catch {
      return null;
    }
  },
};

export interface ToolchainReport {
  present: boolean;
  /** What was found, named the way the installer names it. */
  name: string | null;
  detail?: string;
  /** Every location consulted, so a miss is diagnosable rather than a shrug. */
  probed: string[];
  /** The exact thing to install when nothing was found. */
  fix?: string;
}

/**
 * The C++ toolchain Unreal compiles a plugin with, on this machine.
 *
 * Deliberately does not probe .NET: every engine install ships its own dotnet
 * under `Engine/Binaries/ThirdParty/DotNet`, and UnrealBuildTool uses that one.
 */
export function detectToolchain(hooks: ProbeHooks = DEFAULT_PROBE_HOOKS): ToolchainReport {
  const probed: string[] = [];

  if (hooks.platform === "win32") {
    const programFilesX86 = hooks.env["ProgramFiles(x86)"] ?? "C:\\Program Files (x86)";
    const programFiles = hooks.env.ProgramFiles ?? "C:\\Program Files";

    const vswhere = path.join(programFilesX86, "Microsoft Visual Studio", "Installer", "vswhere.exe");
    probed.push(vswhere);
    if (hooks.exists(vswhere)) {
      const found = hooks.run(vswhere, [
        "-latest",
        "-products",
        "*",
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property",
        "installationPath",
      ]);
      const install = found?.split(/\r?\n/).find((line) => line.trim() !== "")?.trim();
      if (install) {
        return {
          present: true,
          name: "Visual Studio with the MSVC x64 build tools",
          detail: install,
          probed,
        };
      }
    }

    // vswhere is absent on a machine that only ever had the standalone Build
    // Tools, and it reports nothing when the C++ component was never selected.
    // Either way the toolset directory is the ground truth.
    for (const year of ["2022", "2019"]) {
      for (const edition of ["Community", "Professional", "Enterprise", "BuildTools"]) {
        const toolset = path.join(programFiles, "Microsoft Visual Studio", year, edition, "VC", "Tools", "MSVC");
        probed.push(toolset);
        if (hooks.exists(toolset)) {
          return {
            present: true,
            name: `Visual Studio ${year} ${edition} MSVC toolset`,
            detail: toolset,
            probed,
          };
        }
      }
    }

    return {
      present: false,
      name: null,
      probed,
      fix:
        "Install Visual Studio 2022 with the 'Desktop development with C++' workload (the free Community "
        + "edition is enough), or the standalone Build Tools with the same workload. Unreal compiles the "
        + "bridge plugin with MSVC, so nothing here works until that exists.",
    };
  }

  if (hooks.platform === "darwin") {
    const developerDir = hooks.run("xcode-select", ["-p"]);
    if (developerDir) {
      probed.push("xcode-select -p");
      return {
        present: true,
        name: developerDir.includes("CommandLineTools") ? "Xcode command line tools" : "Xcode",
        detail: developerDir,
        probed,
      };
    }
    probed.push("xcode-select -p");
    return {
      present: false,
      name: null,
      probed,
      fix: "Install Xcode from the App Store, then run 'xcode-select --install'. Unreal compiles the bridge plugin with clang from that install.",
    };
  }

  for (const compiler of ["clang++", "g++"]) {
    probed.push(compiler);
    const version = hooks.run(compiler, ["--version"]);
    if (version) {
      return {
        present: true,
        name: compiler,
        detail: version.split(/\r?\n/)[0],
        probed,
      };
    }
  }
  return {
    present: false,
    name: null,
    probed,
    fix: "Install clang (Unreal's Linux toolchain), then re-run this check.",
  };
}

/** What a .uproject declares about its own native code. */
export interface ProjectShape {
  kind: ProjectKind;
  hasSourceDir: boolean;
  /** Module names from the .uproject's Modules array. */
  declaredModules: string[];
  /** Plugin names the .uproject enables, so the bridge can be looked for. */
  enabledPlugins: string[];
  bridgeEnabled: boolean;
}

export function readProjectShape(uprojectPath: string): ProjectShape {
  const projectDir = path.dirname(uprojectPath);
  const raw = JSON.parse(fs.readFileSync(uprojectPath, "utf-8")) as {
    Modules?: Array<{ Name?: string }>;
    Plugins?: Array<{ Name?: string; Enabled?: boolean }>;
  };
  const declaredModules = (raw.Modules ?? [])
    .map((m) => m.Name)
    .filter((n): n is string => typeof n === "string");
  const enabledPlugins = (raw.Plugins ?? [])
    .filter((p) => p.Enabled !== false)
    .map((p) => p.Name)
    .filter((n): n is string => typeof n === "string");
  const hasSourceDir = fs.existsSync(path.join(projectDir, "Source"));
  return {
    kind: declaredModules.length > 0 || hasSourceDir ? "code" : "blueprint-only",
    hasSourceDir,
    declaredModules,
    enabledPlugins,
    bridgeEnabled: enabledPlugins.some((n) => n.toLowerCase() === "ue_mcp_bridge"),
  };
}

/**
 * The deployed and packaged plugin versions, read without touching anything.
 *
 * `deployer.attach()` answers the same question and ENABLES the plugin in the
 * .uproject as a side effect, which would make this report a writer and would
 * leave it contradicting the descriptor it had just changed. The descriptor is
 * only ever read here.
 *
 * The descriptor file is found by extension rather than by name: the packaged
 * copy is `ue_mcp_bridge.uplugin` and the deployed one keeps whatever casing
 * the filesystem gave it, so matching a spelling works on Windows and quietly
 * fails on Linux.
 */
function readUpluginVersion(pluginDir: string): string | null {
  let entries: string[];
  try {
    entries = fs.readdirSync(pluginDir);
  } catch {
    return null;
  }
  const descriptor = entries.find((name) => name.toLowerCase().endsWith(".uplugin"));
  if (!descriptor) return null;
  try {
    const parsed = JSON.parse(fs.readFileSync(path.join(pluginDir, descriptor), "utf-8")) as {
      VersionName?: unknown;
    };
    return typeof parsed.VersionName === "string" ? parsed.VersionName : null;
  } catch {
    return null;
  }
}

/** The plugin source this npm package ships. */
function packagedPluginDir(): string {
  const selfDir = path.dirname(fileURLToPath(import.meta.url));
  return path.resolve(selfDir, "..", "plugin", "ue_mcp_bridge");
}

export interface InstallProblem {
  /** Stable identifier, so a caller can branch without matching prose. */
  code:
    | "no_engine"
    | "no_toolchain"
    | "bridge_not_deployed"
    | "bridge_not_enabled"
    | "bridge_never_compiled"
    | "bridge_build_stale"
    | "bridge_version_mismatch";
  what: string;
  fix: string;
}

export interface InstallReport {
  ok: boolean;
  project: {
    path: string;
    name: string;
    kind: ProjectKind;
    hasSourceDir: boolean;
    declaredModules: string[];
    /** Why a Blueprint-only project is still installable, when it is one. */
    note?: string;
  };
  engine: {
    resolved: boolean;
    root: string | null;
    source: string | null;
    buildTool: string | null;
  };
  bridge: {
    deployed: boolean;
    enabledInUproject: boolean;
    packagedVersion: string | null;
    installedVersion: string | null;
    versionMatch: boolean | null;
    compiled: boolean;
    binaryPath?: string;
    stale: boolean;
  };
  toolchain: ToolchainReport;
  problems: InstallProblem[];
  /** The ordered calls that take this project from here to a working bridge. */
  nextSteps: string[];
}

/**
 * The reason a Blueprint-only project is reported and not refused.
 *
 * Written out in the report rather than left as folklore: the next person to
 * read "blueprint-only" should not have to re-derive that it is fine.
 */
const BLUEPRINT_ONLY_NOTE =
  "This project declares no native modules of its own. That is not a blocker: with a code plugin "
  + "enabled, UnrealBuildTool generates temporary target and module files under Intermediate/Source/ "
  + "and compiles the bridge against them, so the project stays Blueprint-only in the editor while the "
  + "plugin's C++ still builds. What it does require is a C++ toolchain on this machine, which is the "
  + "part a Blueprint-only project usually lacks.";

export interface InspectInstallOptions {
  hooks?: ProbeHooks;
  /** Skip the toolchain probe, which shells out. Used by callers that only
   *  want the on-disk half. */
  skipToolchain?: boolean;
}

/**
 * Everything standing between this .uproject and a bridge that answers.
 *
 * Reads only: it never deploys, enables, or builds anything. The steps it
 * returns are the calls that would.
 */
export function inspectInstall(uprojectPath: string, options: InspectInstallOptions = {}): InstallReport {
  const resolved = path.resolve(uprojectPath);
  if (!fs.existsSync(resolved)) {
    throw new Error(`Project file not found: ${resolved}. Pass the path to a .uproject.`);
  }
  if (!resolved.toLowerCase().endsWith(".uproject")) {
    throw new Error(`Not a .uproject file: ${resolved}. Pass the project descriptor, not its directory.`);
  }

  const shape = readProjectShape(resolved);
  const projectName = path.basename(resolved, ".uproject");

  const context = new ProjectContext();
  context.setProject(resolved);

  const deployedPluginDir = path.join(path.dirname(resolved), "Plugins", "UE_MCP_Bridge");
  const deployed = fs.existsSync(deployedPluginDir);
  const installedVersion = deployed ? readUpluginVersion(deployedPluginDir) : null;
  const packagedVersion = readUpluginVersion(packagedPluginDir());
  const versionMatch =
    installedVersion && packagedVersion ? installedVersion === packagedVersion : null;

  const freshness: PluginFreshness = checkPluginFreshness(resolved);

  const engine = trySelectEngine(
    {
      projectPath: resolved,
      engineAssociation: context.engineAssociation,
      configBuildToolPath: context.config.editor?.buildToolPath ?? null,
      configEditorPath: context.config.editor?.path ?? null,
    },
    "buildTool",
  );

  const toolchain = options.skipToolchain
    ? { present: true, name: null, probed: [], detail: "not probed" }
    : detectToolchain(options.hooks ?? DEFAULT_PROBE_HOOKS);

  // "Never compiled" and "compiled but out of date" are different problems with
  // different fixes, and checkPluginFreshness already separates them: it
  // reports stale with no binaryPath for the first and with one for the second.
  const compiled = Boolean(freshness.binaryPath);

  const problems: InstallProblem[] = [];

  if (!engine?.buildTool) {
    problems.push({
      code: "no_engine",
      what: `No Unreal install could be resolved for EngineAssociation '${context.engineAssociation ?? "(unset)"}'.`,
      fix: "Install the engine version this project names, or point at it with editor.buildToolPath in ue-mcp.yml.",
    });
  }

  if (!toolchain.present) {
    problems.push({
      code: "no_toolchain",
      what: "No C++ toolchain was found on this machine, so the bridge plugin cannot be compiled here.",
      fix: toolchain.fix ?? "Install the C++ toolchain Unreal uses on this platform.",
    });
  }

  if (!deployed) {
    problems.push({
      code: "bridge_not_deployed",
      what: `The bridge plugin is not in ${projectName}/Plugins/UE_MCP_Bridge/.`,
      fix: `Run 'npx ue-mcp init ${resolved}' (or 'npx ue-mcp deploy ${resolved}') to copy the plugin source in.`,
    });
  } else {
    if (!shape.bridgeEnabled) {
      problems.push({
        code: "bridge_not_enabled",
        what: "The plugin is on disk but the .uproject does not enable it, so the editor will not load it.",
        fix: `Run 'npx ue-mcp deploy ${resolved}', which adds UE_MCP_Bridge to the .uproject's Plugins array.`,
      });
    }
    if (versionMatch === false) {
      problems.push({
        code: "bridge_version_mismatch",
        what: `The installed plugin is v${installedVersion} and this package ships v${packagedVersion}.`,
        fix: `Run 'npx ue-mcp deploy ${resolved}' to sync the source, then rebuild.`,
      });
    }
    if (!compiled) {
      problems.push({
        code: "bridge_never_compiled",
        what: "The plugin source is deployed and has never been compiled, so there is no module for the editor to load.",
        fix: "Build it with the editor STOPPED: project(action='build'), or open the project in Unreal and accept its rebuild prompt.",
      });
    } else if (freshness.stale) {
      problems.push({
        code: "bridge_build_stale",
        what: "The compiled plugin is older than its source, so handlers added since the last build answer 'Unknown method'.",
        fix: "Rebuild with the editor STOPPED: project(action='build').",
      });
    }
  }

  const nextSteps: string[] = [];
  for (const problem of problems) nextSteps.push(problem.fix);
  if (problems.length === 0) {
    nextSteps.push("Nothing to do. Start the editor with editor(action='start_editor') and call project(action='get_status').");
  } else if (!problems.some((p) => p.code === "no_toolchain" || p.code === "no_engine")) {
    nextSteps.push("Then start the editor with editor(action='start_editor').");
  }

  return {
    ok: problems.length === 0,
    project: {
      path: resolved,
      name: projectName,
      kind: shape.kind,
      hasSourceDir: shape.hasSourceDir,
      declaredModules: shape.declaredModules,
      note: shape.kind === "blueprint-only" ? BLUEPRINT_ONLY_NOTE : undefined,
    },
    engine: {
      resolved: Boolean(engine?.buildTool),
      root: engine?.engineRoot ?? null,
      source: engine?.source ?? null,
      buildTool: engine?.buildTool ?? null,
    },
    bridge: {
      deployed,
      enabledInUproject: shape.bridgeEnabled,
      packagedVersion,
      installedVersion,
      versionMatch,
      compiled,
      binaryPath: freshness.binaryPath,
      stale: freshness.stale,
    },
    toolchain,
    problems,
    nextSteps,
  };
}

/**
 * The one-paragraph version, for a CLI that has just finished deploying.
 *
 * Returns null when there is nothing to warn about, so a healthy install still
 * ends on "Setup complete!" and nothing else.
 */
export function installWarning(report: InstallReport): string | null {
  const blocking = report.problems.filter(
    (p) => p.code === "no_toolchain" || p.code === "no_engine" || p.code === "bridge_never_compiled",
  );
  if (blocking.length === 0) return null;
  const lines = [
    report.project.kind === "blueprint-only"
      ? `${report.project.name} is a Blueprint-only project and the bridge plugin is C++.`
      : `${report.project.name} still needs one thing before the bridge can answer.`,
  ];
  for (const problem of blocking) lines.push(`${problem.what} ${problem.fix}`);
  return lines.join(" ");
}
