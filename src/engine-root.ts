/**
 * One resolver for "which engine does this project use" (#959, #961, #962, #974).
 *
 * Two separate discovery paths used to answer that question and they answered
 * it differently. `findUEBuildTool` probed launcher install locations, and the
 * engine-source readers in `tools/project.ts` asked `findEngineInstall` about
 * the .uproject's EngineAssociation and gave up when that came back null. A
 * source build sitting beside the project - the standard Perforce stream layout
 * of `<root>/Engine` next to `<root>/MyProject/MyProject.uproject` - was
 * invisible to both, even though `Build.bat` was right there and the editor
 * launched out of that same tree.
 *
 * Everything that needs an engine now goes through `selectEngine` here, so the
 * build tool and the engine-source readers cannot drift apart again.
 *
 * Order, most specific first:
 *
 *   1. `UE_MCP_TEST_ENGINE_ROOT`     an explicit pin for the whole process
 *   2. `UE_BUILD_TOOL_PATH`          an explicit build tool for the process
 *   3. `editor.buildToolPath`        the per-project equivalent (#817)
 *   4. `UE_EDITOR_PATH`              an explicit editor binary names its tree
 *   5. `editor.path`                 the per-project equivalent (#817)
 *   6. EngineAssociation as a path   relative or absolute, which is what a
 *                                    project beside a source build carries
 *   7. project-relative engine tree  walk up from the project directory for
 *                                    `Engine/Build/BatchFiles/<Build script>`
 *   8. EngineAssociation registered  a GUID through HKCU registered builds,
 *                                    a version string through the launcher
 *   9. last editor launch            the engine the project's own log says it
 *                                    was last opened with
 *  10. default engine install        the launcher locations probed before
 *
 * `UE_MCP_PROTECTED_ENGINE_ROOTS` outranks every one of them, including an
 * explicit pin: a root named there is never selected, the same contract
 * `scripts/build-utils.js` enforces for test builds.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { execSync } from "node:child_process";
import { debug, warn } from "./log.js";
import { readEngineRootFromLog } from "./engine-observer.js";
import { readEnv } from "./env.js";

/** Where a candidate engine came from. Printed verbatim in failures. */
export type EngineCandidateSource =
  | "UE_MCP_TEST_ENGINE_ROOT"
  | "UE_BUILD_TOOL_PATH"
  | "editor.buildToolPath"
  | "UE_EDITOR_PATH"
  | "editor.path"
  | "EngineAssociation path"
  | "project-relative engine tree"
  | "EngineAssociation"
  | "last editor launch"
  | "default engine install";

/**
 * A place an engine might be, before anything has been probed.
 *
 * `buildTool` is set only when the source names a build script directly, which
 * is the one case where a usable tool can exist without a recognisable engine
 * root above it.
 */
export interface EngineCandidate {
  source: EngineCandidateSource;
  engineRoot: string | null;
  buildTool: string | null;
  /** True when a person configured this explicitly, so a miss is their typo. */
  explicit: boolean;
}

export interface SelectedEngine {
  source: EngineCandidateSource;
  engineRoot: string | null;
  buildTool: string | null;
  editorExecutable: string | null;
}

export interface EngineLookup {
  /** Path to the .uproject. Everything project-relative needs it. */
  projectPath?: string | null;
  /** EngineAssociation, when the caller already parsed the .uproject. */
  engineAssociation?: string | null;
  /** `editor.buildToolPath` from the project's ue-mcp.yml (#817). */
  configBuildToolPath?: string | null;
  /** `editor.path` from the project's ue-mcp.yml (#817). */
  configEditorPath?: string | null;
  env?: NodeJS.ProcessEnv;
  platform?: NodeJS.Platform;
  hooks?: Partial<EngineLookupHooks>;
}

/**
 * The lookup for one project: its .uproject, its association and its
 * per-project `editor:` config, so the editor and the build tool resolve alike.
 */
export function engineLookupFor(
  projectPath: string | null | undefined,
  engineAssociation?: string | null,
  editorConfig?: { path?: string; buildToolPath?: string },
): EngineLookup {
  return {
    projectPath: projectPath ?? null,
    engineAssociation: engineAssociation ?? null,
    configBuildToolPath: editorConfig?.buildToolPath ?? null,
    configEditorPath: editorConfig?.path ?? null,
  };
}

/**
 * The I/O the resolver does, injectable so the ordering can be tested against
 * synthetic layouts without a registry, a launcher manifest or an editor log.
 */
export interface EngineLookupHooks {
  exists(candidatePath: string): boolean;
  /** GUID -> registered build, version string -> launcher install. */
  associationInstall(association: string): string | null;
  /** The engine a project's own log says it was last opened with. */
  lastEngineRoot(projectPath: string): string | null;
}

const DEFAULT_HOOKS: EngineLookupHooks = {
  exists: (candidatePath) => {
    try {
      return fs.existsSync(candidatePath);
    } catch {
      return false;
    }
  },
  associationInstall: (association) => findEngineInstall(association),
  lastEngineRoot: (projectPath) => readEngineRootFromLog(projectPath),
};

/** Where the Epic launcher installs engines on Windows. */
const WINDOWS_INSTALL_BASES = [
  "C:/Program Files/Epic Games",
  "D:/Program Files/Epic Games",
  "E:/Program Files/Epic Games",
  "C:/Epic Games",
  "D:/Epic Games",
  "E:/Epic Games",
];

/** Engine versions probed in the default install locations, newest first. */
const DEFAULT_VERSIONS = ["5.8", "5.7", "5.6", "5.5", "5.4", "5.3"];

function pathImpl(platform: NodeJS.Platform): path.PlatformPath {
  return platform === "win32" ? path.win32 : path.posix;
}

/** The Build.bat / Build.sh an engine root would hold on this platform. */
export function engineBuildTool(engineRoot: string, platform: NodeJS.Platform = process.platform): string {
  const impl = pathImpl(platform);
  const batchFiles = impl.join(engineRoot, "Engine", "Build", "BatchFiles");
  if (platform === "win32") return impl.join(batchFiles, "Build.bat");
  if (platform === "darwin") return impl.join(batchFiles, "Mac", "Build.sh");
  return impl.join(batchFiles, "Linux", "Build.sh");
}

/** The editor binaries an engine root would hold, best first. */
export function engineEditorBinaries(engineRoot: string, platform: NodeJS.Platform = process.platform): string[] {
  const impl = pathImpl(platform);
  const binaries = impl.join(engineRoot, "Engine", "Binaries");
  if (platform === "win32") return [impl.join(binaries, "Win64", "UnrealEditor.exe")];
  if (platform === "darwin") {
    return [
      impl.join(binaries, "Mac", "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
      impl.join(binaries, "Mac", "UnrealEditor"),
    ];
  }
  return [impl.join(binaries, "Linux", "UnrealEditor")];
}

/**
 * Walk up from a path inside an engine tree to the root that contains `Engine`.
 * Covers build scripts and editor binaries on every platform.
 */
export function engineRootFromEnginePath(
  enginePath: string,
  platform: NodeJS.Platform = process.platform,
): string | null {
  const impl = pathImpl(platform);
  let directory = impl.dirname(enginePath);
  while (directory && directory !== impl.dirname(directory)) {
    if (impl.basename(directory).toLowerCase() === "engine") return impl.dirname(directory);
    directory = impl.dirname(directory);
  }
  return null;
}

function comparisonPath(value: string, platform: NodeJS.Platform): string {
  const normalized = pathImpl(platform).normalize(value).replace(/[\\/]+$/, "");
  return platform === "win32" ? normalized.toLowerCase() : normalized;
}

/**
 * True when `candidate` is `parent` itself or lives beneath it. The trailing
 * separator on the parent is what stops `/ue/engine-test` from counting as a
 * child of `/ue/engine`.
 */
function isSameOrUnder(candidate: string, parent: string, platform: NodeJS.Platform): boolean {
  const normalizedCandidate = comparisonPath(candidate, platform);
  const normalizedParent = comparisonPath(parent, platform);
  return (
    normalizedCandidate === normalizedParent ||
    normalizedCandidate.startsWith(`${normalizedParent}${pathImpl(platform).sep}`)
  );
}

/**
 * Roots this server must never touch, from `UE_MCP_PROTECTED_ENGINE_ROOTS`.
 *
 * A relative entry is rejected rather than resolved against the working
 * directory: turning a mistyped deny entry into a path that can never match
 * would leave the caller believing an engine is protected when it is not. Same
 * rule as `scripts/build-utils.js`.
 */
/** Split a deny-list value into entries.
 *
 *  `path.delimiter` is ":" on POSIX, which cuts a Windows path in half at its
 *  drive letter: "D:\protected" becomes "D" and "\protected". That matters
 *  because this list is a SAFETY deny list, and a mangled entry does not fail
 *  loudly, it silently protects nothing. A value authored on Windows and read
 *  anywhere else has to survive.
 *
 *  Semicolons always separate. A colon separates only when it is not the colon
 *  of a drive letter. */
function splitDenyList(value: string): string[] {
  const out: string[] = [];
  let current = "";
  for (let i = 0; i < value.length; i++) {
    const ch = value[i];
    if (ch === ";") {
      out.push(current);
      current = "";
      continue;
    }
    if (ch === ":") {
      // Compare on the trimmed value: a list may be written with spaces
      // around its separators, and " C" is still a drive letter.
      const head = current.trim();
      const isDriveColon = head.length === 1 && /[A-Za-z]/.test(head);
      if (!isDriveColon) {
        out.push(current);
        current = "";
        continue;
      }
    }
    current += ch;
  }
  out.push(current);
  return out;
}

/** Absolute in the POSIX sense, or a Windows drive-letter or UNC root, whatever
 *  platform is doing the parsing. A deny list written on one machine is often
 *  read on another. */
function isAbsoluteAnyPlatform(entry: string): boolean {
  const driveRooted = /^[A-Za-z]:[\\/]/.test(entry);
  const uncRooted = /^\\\\/.test(entry);
  return path.isAbsolute(entry) || driveRooted || uncRooted;
}

export function protectedEngineRoots(env: NodeJS.ProcessEnv = process.env): string[] {
  return splitDenyList(readEnv("protectedEngineRoots", env) || "")
    .map((entry) => entry.trim())
    .filter(Boolean)
    .map((entry) => {
      if (!isAbsoluteAnyPlatform(entry)) {
        throw new Error(
          `UE_MCP_PROTECTED_ENGINE_ROOTS entry '${entry}' is not an absolute path, so it would protect nothing.`,
        );
      }
      return entry;
    });
}

/** Thrown when nothing usable was found. `tried` is every path probed. */
export class EngineResolutionError extends Error {
  readonly tried: string[];
  constructor(message: string, tried: string[]) {
    super(message);
    this.name = "EngineResolutionError";
    this.tried = tried;
  }
}

function trimmed(value: string | null | undefined): string | null {
  if (typeof value !== "string") return null;
  const text = value.trim();
  return text === "" ? null : text;
}

const GUID_RE = /^\{?[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}?$/i;
const VERSION_RE = /^\d+(\.\d+)*$/;

/**
 * An EngineAssociation that is neither a GUID nor a version number is a path,
 * which is how a project beside a source build names its engine. UE writes it
 * relative to the project directory, and an absolute one is legal too.
 */
function associationAsPath(
  association: string,
  projectDir: string | null,
  platform: NodeJS.Platform,
): string | null {
  if (GUID_RE.test(association) || VERSION_RE.test(association)) return null;
  const impl = pathImpl(platform);
  // `../` normalizes with a trailing separator, which would then read as a
  // different root from the same directory written without one.
  const tidy = (value: string): string => impl.normalize(value).replace(/(?<=[^\\/:])[\\/]+$/, "");
  if (impl.isAbsolute(association)) return tidy(association);
  if (!projectDir) return null;
  return tidy(impl.join(projectDir, association));
}

/**
 * Every ancestor of the project directory (and the directory itself) that holds
 * an engine tree, nearest first. This is the sibling-engine layout: the project
 * at `<root>/MyProject` and the engine at `<root>/Engine`.
 */
function projectRelativeEngineRoots(
  projectDir: string,
  platform: NodeJS.Platform,
  exists: (p: string) => boolean,
  probed: string[],
): string[] {
  const impl = pathImpl(platform);
  const roots: string[] = [];
  let directory = impl.resolve(projectDir);
  for (;;) {
    const buildTool = engineBuildTool(directory, platform);
    if (exists(buildTool)) roots.push(directory);
    else probed.push(`${buildTool} (walking up from the project)`);
    const parent = impl.dirname(directory);
    if (parent === directory) break;
    directory = parent;
  }
  return roots;
}

/**
 * Default install locations, filtered to the base directories that exist. A
 * machine with one Epic install should not have thirty five dead paths listed
 * back at it when resolution fails.
 */
function defaultInstallRoots(
  platform: NodeJS.Platform,
  env: NodeJS.ProcessEnv,
  exists: (p: string) => boolean,
  probed: string[],
): string[] {
  const impl = pathImpl(platform);
  const bases =
    platform === "win32"
      ? WINDOWS_INSTALL_BASES
      : platform === "darwin"
        ? ["/Users/Shared/Epic Games"]
        : ["/opt/UnrealEngine"];

  const roots: string[] = [];
  for (const base of bases) {
    if (!exists(base)) {
      probed.push(`${base} (no engine install directory there)`);
      continue;
    }
    for (const version of DEFAULT_VERSIONS) roots.push(impl.join(base, `UE_${version}`));
  }

  // Linux and macOS source builds live in the home directory with no version
  // suffix, which is the layout the engine's own Setup.sh produces.
  if (platform !== "win32") {
    const home = trimmed(env.HOME);
    if (home) roots.push(impl.join(home, "UnrealEngine"));
  }

  return roots;
}

/**
 * Every place this project's engine might be, in order, before probing.
 *
 * Exported so a caller can report the search without triggering it, and so the
 * ordering can be asserted directly in tests.
 */
export function engineCandidates(lookup: EngineLookup = {}, probed: string[] = []): EngineCandidate[] {
  const platform = lookup.platform ?? process.platform;
  const env = lookup.env ?? process.env;
  const hooks = { ...DEFAULT_HOOKS, ...lookup.hooks };
  const impl = pathImpl(platform);

  const projectPath = trimmed(lookup.projectPath);
  const projectDir = projectPath ? impl.dirname(impl.resolve(projectPath)) : null;
  const association = trimmed(lookup.engineAssociation)?.replace(/^\{|\}$/g, "") ?? null;

  const candidates: EngineCandidate[] = [];
  const seenRoots = new Set<string>();

  const pushRoot = (source: EngineCandidateSource, engineRoot: string | null, explicit = false): void => {
    if (!engineRoot) return;
    const key = comparisonPath(engineRoot, platform);
    if (seenRoots.has(key)) return;
    seenRoots.add(key);
    candidates.push({ source, engineRoot, buildTool: null, explicit });
  };

  const pushTool = (source: EngineCandidateSource, buildTool: string | null): void => {
    if (!buildTool) return;
    const engineRoot = engineRootFromEnginePath(buildTool, platform);
    if (engineRoot) seenRoots.add(comparisonPath(engineRoot, platform));
    candidates.push({ source, engineRoot, buildTool, explicit: true });
  };

  // 1-5: everything a person configured on purpose.
  pushRoot("UE_MCP_TEST_ENGINE_ROOT", trimmed(readEnv("testEngineRoot", env)), true);
  pushTool("UE_BUILD_TOOL_PATH", trimmed(env.UE_BUILD_TOOL_PATH));
  pushTool("editor.buildToolPath", trimmed(lookup.configBuildToolPath));
  // An editor binary names its tree, but it is a statement about the editor,
  // not about the build tool: a tree that cannot build is not the user's typo,
  // so these are not treated as explicit build pins.
  pushRoot("UE_EDITOR_PATH", engineRootFromEnginePath(trimmed(env.UE_EDITOR_PATH) ?? "", platform));
  pushRoot("editor.path", engineRootFromEnginePath(trimmed(lookup.configEditorPath) ?? "", platform));

  // 6-7: what the project itself says, which is the half that was missing.
  if (association) pushRoot("EngineAssociation path", associationAsPath(association, projectDir, platform));
  if (projectDir) {
    for (const root of projectRelativeEngineRoots(projectDir, platform, hooks.exists, probed)) {
      pushRoot("project-relative engine tree", root);
    }
  }

  // 8: the registered build or launcher install the association names.
  if (association) pushRoot("EngineAssociation", hooks.associationInstall(association));

  // 9: whatever the project was last actually opened with.
  if (projectPath) pushRoot("last editor launch", hooks.lastEngineRoot(projectPath));

  // 10: the launcher locations that were the only thing probed before.
  for (const root of defaultInstallRoots(platform, env, hooks.exists, probed)) {
    pushRoot("default engine install", root);
  }

  return candidates;
}

/**
 * What a selection needs: a usable build script, a launchable editor, or a
 * readable engine source tree. They are asked for separately because an engine
 * can satisfy one and not another - a binary-only distribution launches but
 * cannot answer engine-source questions.
 */
export type EngineNeed = "buildTool" | "editor" | "engineRoot";

function candidatePaths(candidate: EngineCandidate, platform: NodeJS.Platform, need: EngineNeed): string[] {
  if (need === "buildTool") {
    if (candidate.buildTool) return [candidate.buildTool];
    return candidate.engineRoot ? [engineBuildTool(candidate.engineRoot, platform)] : [];
  }
  if (!candidate.engineRoot) return [];
  if (need === "editor") return engineEditorBinaries(candidate.engineRoot, platform);
  return [pathImpl(platform).join(candidate.engineRoot, "Engine", "Source")];
}

/**
 * The path a junction, symlink or short name actually points at, or the path
 * itself when it cannot be resolved. Used only for the deny-list comparison, so
 * a protected engine cannot be reached under a second name.
 */
function canonicalIfPossible(candidatePath: string): string {
  try {
    return fs.realpathSync.native(candidatePath);
  } catch {
    return candidatePath;
  }
}

function assertNotProtected(
  engineRoot: string,
  roots: string[],
  platform: NodeJS.Platform,
  source: EngineCandidateSource,
): void {
  const forms = [engineRoot, canonicalIfPossible(engineRoot)];
  const match = roots.find((root) =>
    [root, canonicalIfPossible(root)].some((denied) => forms.some((form) => isSameOrUnder(form, denied, platform))),
  );
  if (match) {
    throw new EngineResolutionError(
      `Engine root '${engineRoot}' (from ${source}) is listed in UE_MCP_PROTECTED_ENGINE_ROOTS ('${match}'). ` +
        "UE-MCP refuses to use a protected engine.",
      [],
    );
  }
}

function failureMessage(need: EngineNeed, lookup: EngineLookup, tried: string[]): string {
  const subject =
    need === "buildTool"
      ? "Unreal Engine build tool not found"
      : need === "editor"
        ? "Unreal Editor executable not found"
        : "Unreal Engine source tree not found";
  const projectPath = trimmed(lookup.projectPath);
  const forProject = projectPath ? ` for ${projectPath}` : "";
  const remedy =
    need === "editor"
      ? "Set UE_EDITOR_PATH (or editor.path in the project's ue-mcp.yml) to the editor binary (on macOS that is inside " +
        "UnrealEditor.app/Contents/MacOS/), put the engine beside the project as Engine/, point EngineAssociation at it, " +
        "or install UE 5.3+ to a default location."
      : need === "buildTool"
      ? "Set UE_BUILD_TOOL_PATH (or editor.buildToolPath in the project's ue-mcp.yml) to a Build.bat / Build.sh, " +
        "put the engine beside the project as Engine/, point EngineAssociation at it, or install UE 5.3+ to a default location."
      : "Set UE_MCP_TEST_ENGINE_ROOT (or editor.path in the project's ue-mcp.yml) to an engine root, " +
        "put the engine beside the project as Engine/, point EngineAssociation at it, or install UE 5.3+ to a default location. " +
        "A launcher install without the Source/ tree cannot answer engine-source questions.";
  return `${subject}${forProject}. Tried:\n  ${tried.join("\n  ") || "(no candidate locations)"}\n${remedy}`;
}

/**
 * Pick the engine for one project, or throw an error naming every path probed.
 *
 * An explicitly configured location that does not exist is reported on its own
 * rather than quietly falling through to a default install nobody asked for.
 */
export function selectEngine(lookup: EngineLookup = {}, need: EngineNeed = "buildTool"): SelectedEngine {
  const platform = lookup.platform ?? process.platform;
  const env = lookup.env ?? process.env;
  const hooks = { ...DEFAULT_HOOKS, ...lookup.hooks };
  const denied = protectedEngineRoots(env);

  // Locations ruled out while the candidate list was being built (the walk up
  // from the project, the default install directories that are not there) are
  // part of the answer to "why did this not find my engine", so they are
  // reported alongside the candidates that were probed and missed.
  const tried: string[] = [];

  for (const candidate of engineCandidates(lookup, tried)) {
    const probes = candidatePaths(candidate, platform, need);
    const hit = probes.find((probe) => hooks.exists(probe));

    if (!hit) {
      // A candidate with nothing to probe cannot answer this need at all. That
      // is not a misconfiguration, so it is recorded and stepped over.
      if (probes.length === 0) {
        tried.push(`(${candidate.source} names no ${need})`);
        continue;
      }
      for (const probe of probes) tried.push(`${probe} (from ${candidate.source})`);
      if (candidate.explicit) {
        throw new EngineResolutionError(
          `${candidate.source} does not point at a usable Unreal engine: ${probes[0]} is missing.`,
          tried,
        );
      }
      continue;
    }

    assertNotProtected(candidate.engineRoot ?? hit, denied, platform, candidate.source);

    const buildTool =
      candidate.buildTool ?? (candidate.engineRoot ? engineBuildTool(candidate.engineRoot, platform) : null);
    const editorExecutable = candidate.engineRoot
      ? (engineEditorBinaries(candidate.engineRoot, platform).find((binary) => hooks.exists(binary)) ?? null)
      : null;

    return {
      source: candidate.source,
      engineRoot: candidate.engineRoot,
      buildTool: buildTool && hooks.exists(buildTool) ? buildTool : null,
      editorExecutable,
    };
  }

  throw new EngineResolutionError(failureMessage(need, lookup, tried), tried);
}

/** `selectEngine` without the throw, for callers that have their own fallback. */
export function trySelectEngine(lookup: EngineLookup = {}, need: EngineNeed = "buildTool"): SelectedEngine | null {
  try {
    return selectEngine(lookup, need);
  } catch {
    return null;
  }
}

/**
 * The engine root for reading engine source, or null.
 *
 * Callers that used `findEngineInstall(association)` want this: it still
 * honours the association, it just no longer stops there.
 */
export function resolveEngineRoot(lookup: EngineLookup = {}): string | null {
  return trySelectEngine(lookup, "engineRoot")?.engineRoot ?? null;
}

/**
 * The engine a launcher association names: a source build's registry GUID,
 * or a launcher version found in LauncherInstalled.dat or the default bases.
 */
function findEngineInstall(
  engineAssociation: string | null,
): string | null {
  if (!engineAssociation) return null;
  const normalizedAssociation = engineAssociation.replace(/^\{|\}$/g, "");

  const guidRegex =
    /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  if (guidRegex.test(normalizedAssociation)) {
    return findEngineByGuid(normalizedAssociation);
  }

  return findLauncherEngine(normalizedAssociation);
}

function findEngineByGuid(guid: string): string | null {
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(guid)) {
    debug("engine-root", `refusing registry lookup for non-GUID engine association '${guid}'`);
    return null;
  }
  try {
    const output = execSync(
      `reg query "HKCU\\SOFTWARE\\Epic Games\\Unreal Engine\\Builds" /v "${guid}"`,
      { stdio: "pipe", encoding: "utf-8" },
    );
    const match = output.match(/REG_SZ\s+(.+)/);
    if (match) {
      const p = match[1].trim();
      if (fs.existsSync(p)) return p;
    }
  } catch (e) {
    debug("engine-root", `no registry entry for GUID ${guid}`, e);
  }
  return null;
}

function findLauncherEngine(association: string): string | null {
  const launcherDat = path.join(
    process.env.PROGRAMDATA || "C:\\ProgramData",
    "Epic",
    "UnrealEngineLauncher",
    "LauncherInstalled.dat",
  );

  if (fs.existsSync(launcherDat)) {
    try {
      const data = JSON.parse(fs.readFileSync(launcherDat, "utf-8"));
      for (const entry of data.InstallationList ?? []) {
        if (
          entry.AppName?.toLowerCase() ===
          `ue_${association}`.toLowerCase()
        ) {
          if (fs.existsSync(entry.InstallLocation)) {
            return entry.InstallLocation;
          }
        }
      }
    } catch (e) {
      warn("engine-root", `LauncherInstalled.dat at ${launcherDat} could not be parsed - falling back to drive-letter scan`, e);
    }
  }

  for (const root of WINDOWS_INSTALL_BASES) {
    const candidate = path.join(root, `UE_${association}`);
    if (fs.existsSync(candidate)) return candidate;
  }

  return null;
}
