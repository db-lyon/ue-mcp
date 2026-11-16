// @session per-session: Installs, updates and removes the ue-mcp plugins of the one project directory it is handed.
/**
 * Installing, updating and removing a project's ue-mcp plugins: the one
 * library `ue-mcp plugin install|uninstall|update` and the daemon's
 * /v1/plugins routes both call, so a terminal and a client panel take the
 * same path and get the same refusals.
 *
 * Nothing here exits the process or writes to the terminal on its own: every
 * line goes through the PluginOpLog the caller hands in, and a refusal is a
 * PluginOpError whose message says what to do next. npm runs as a child
 * process without blocking the caller, so the daemon keeps answering while a
 * plugin installs.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { UnsafeArgumentError, assertPackageSpec, npmInvocation, spawnInvocation } from "../core/safe-spawn.js";
import { packageVersion } from "../core/package-root.js";
import { UE_PLUGIN_NAME_RE } from "./package-paths.js";
import { loadManifest, pluginVersionRefusal, type PluginManifest } from "./manifest.js";
import { findInstalledPackage } from "./resolver.js";
import { pluginSlug } from "./plugin-groups.js";
import { readDeployedBridgeApiVersion } from "./bridge-api.js";
import {
  deployNativeModule,
  pruneStaleNativeFiles,
  readNativeModulesState,
  undeployNativeModule,
  writeNativeModulesState,
} from "./native-deploy.js";
import { readPluginsList, writePluginsList, type PluginEntry } from "./plugins-list.js";
import { conflictMessages, installSkillSet, listSkills, removeSkillSet, syncPluginSkills } from "./skills.js";
import { PROJECT_SKILLS_DIR, pluginSkillsRoot } from "./skill-file.js";
import { registryBase } from "./registry-catalog.js";
import { isSuperseded, supersededByName, supersededMessage } from "./superseded.js";
import { ProjectContext } from "../config/project.js";
import { findUProject } from "../config/uproject-path.js";
import { removeUProjectPluginEntry, uprojectListsPlugin } from "../config/uproject-plugins.js";
import { editorsHoldingProject } from "../editor/project-holders.js";

/** A plugin change that was refused or failed; the message says what to do next. */
export class PluginOpError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "PluginOpError";
  }
}

/** Where a plugin change reports what it does. */
export interface PluginOpLog {
  /** A progress line. */
  note(msg: string): void;
  /** Something that went wrong without stopping the change. */
  warn(msg: string): void;
  /** npm's own output. Without it npm writes straight to this process's terminal. */
  npmOutput?: (text: string) => void;
}

/** How the CLI prints each kind of line, and the daemon records them, so both read the same. */
export const formatPluginNote = (msg: string): string => `[ue-mcp plugin] ${msg}`;
export const formatPluginWarn = (msg: string): string => `[ue-mcp] warn: ${msg}`;
export const formatPluginError = (msg: string): string => `[ue-mcp plugin] ERROR: ${msg}`;

/**
 * A log that keeps every line, npm's output included, for a caller with no
 * terminal. `error` adds a refusal the way the CLI prints it.
 */
export function captureLog(): { log: PluginOpLog; error: (msg: string) => void; text: () => string } {
  const lines: string[] = [];
  return {
    log: {
      note: (m) => lines.push(formatPluginNote(m)),
      warn: (m) => lines.push(formatPluginWarn(m)),
      npmOutput: (t) => lines.push(t.replace(/\s+$/, "")),
    },
    error: (m) => lines.push(formatPluginError(m)),
    text: () => lines.filter((l) => l !== "").join("\n"),
  };
}

export const RESTART_NOTE =
  "Injected actions appear on the next server start. Restart your MCP client (or `ue-mcp restart`).";

export const REBUILD_NOTE =
  "REBUILD REQUIRED: run `ue-mcp build` (or rebuild the UE project) before launching the editor so the new C++ module compiles in.";

/** The built-in categories with their actions, which a plugin may not shadow. */
export type BuiltInCategories = ReadonlyArray<{ name: string; actions: Record<string, unknown> }>;

export interface PluginProject {
  projectDir: string;
  configPath: string;
}

/** The project a plugin change applies to: the nearest folder up from `startDir` with a ue-mcp.yml, or a package.json beside a .uproject. */
export function findPluginProject(startDir: string): PluginProject {
  let dir = path.resolve(startDir);
  const root = path.parse(dir).root;
  while (true) {
    if (fs.existsSync(path.join(dir, "ue-mcp.yml"))) {
      return { projectDir: dir, configPath: path.join(dir, "ue-mcp.yml") };
    }
    if (fs.existsSync(path.join(dir, "package.json")) && findUProject(dir)) {
      return { projectDir: dir, configPath: path.join(dir, "ue-mcp.yml") };
    }
    if (dir === root) break;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  throw new PluginOpError(
    "could not find a project. Run from a directory containing a ue-mcp.yml or a .uproject. " +
    "Run `ue-mcp init` first if this is a fresh project.",
  );
}

/** Run npm without a shell and without blocking; throws PluginOpError when it fails. */
function runNpm(args: string[], cwd: string, log: PluginOpLog): Promise<void> {
  return new Promise((resolve, reject) => {
    const capture = log.npmOutput;
    const child = spawnInvocation(npmInvocation(args), { cwd, stdio: capture ? ["ignore", "pipe", "pipe"] : "inherit" });
    if (capture) {
      child.stdout?.on("data", (b: Buffer) => capture(b.toString()));
      child.stderr?.on("data", (b: Buffer) => capture(b.toString()));
    }
    child.on("error", (e) => reject(new PluginOpError(`npm ${args.join(" ")} failed to start: ${e.message}`)));
    child.on("close", (code) => {
      if (code === 0) resolve();
      else reject(new PluginOpError(`npm ${args.join(" ")} exited ${code ?? "(killed by signal)"}`));
    });
  });
}

/** npm's stdout for a read-only command, or null when it did not run cleanly. */
function npmText(args: string[], cwd: string): Promise<string | null> {
  return new Promise((resolve) => {
    const child = spawnInvocation(npmInvocation(args), { cwd, stdio: ["ignore", "pipe", "ignore"] });
    let out = "";
    child.stdout?.on("data", (b: Buffer) => (out += b.toString()));
    child.on("error", () => resolve(null));
    child.on("close", (code) => resolve(code === 0 ? out : null));
  });
}

/** Throw a clear message when a name or spec cannot be handed to npm. */
function checkSpec(spec: string): void {
  try {
    assertPackageSpec(spec);
  } catch (e) {
    throw new PluginOpError(e instanceof UnsafeArgumentError ? e.message : String(e));
  }
}

/** Refuse before npm runs, or anything else is looked up, when the package now ships in core. */
export function refuseSuperseded(name: string): void {
  const hit = supersededByName(name);
  if (hit) throw new PluginOpError(supersededMessage(hit[0], hit[1], "installed"));
}

/** A spec npm resolves from the registry by name, not a folder, tarball or URL. */
function isRegistryName(spec: string): boolean {
  return /^(@[a-z0-9-~][a-z0-9-._~]*\/)?[a-z0-9-~][a-z0-9-._~]*$/i.test(spec) && !/\.(tgz|tar|tar\.gz)$/i.test(spec);
}

/**
 * Resolve a registry slug to the package to install, so a clean name like
 * `meshy` installs the real package (`ue-mcp-meshy`). Null on any miss, error
 * or timeout, so the caller installs the given name directly: the registry is
 * a convenience layer, never a hard dependency.
 */
async function resolveFromRegistry(name: string, fetchImpl: typeof fetch = fetch): Promise<string | null> {
  // A scoped name is already a real npm name, and a path or tarball is no name at all.
  if (name.startsWith("@") || !isRegistryName(name)) return null;
  try {
    const res = await fetchImpl(`${registryBase()}/api/resolve?name=${encodeURIComponent(name)}`, { signal: AbortSignal.timeout(8000) });
    if (!res.ok) return null;
    const pkg = String(((await res.json()) as { package?: unknown }).package ?? "").trim();
    return pkg || null;
  } catch {
    return null;
  }
}

function readDependencies(projectDir: string): Record<string, string> {
  try {
    return (JSON.parse(fs.readFileSync(path.join(projectDir, "package.json"), "utf-8")) as { dependencies?: Record<string, string> }).dependencies ?? {};
  } catch {
    return {};
  }
}

/**
 * The package `npm install <spec>` put in node_modules. A registry name is its
 * own answer. For a folder, tarball or URL it is the dependency the install
 * added or changed, else a folder's own package.json name.
 */
function installedPackageName(spec: string, projectDir: string, before: Record<string, string>): string | null {
  if (isRegistryName(spec)) return spec;
  const after = readDependencies(projectDir);
  const changed = Object.keys(after).filter((k) => after[k] !== before[k]);
  if (changed.length === 1) return changed[0];
  try {
    return (JSON.parse(fs.readFileSync(path.join(path.resolve(projectDir, spec), "package.json"), "utf-8")) as { name?: string }).name ?? null;
  } catch {
    return null;
  }
}

/** The version in an installed package's package.json. */
export function installedVersion(pkgDir: string): string | undefined {
  try {
    return (JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8")) as { version?: string }).version;
  } catch {
    return undefined;
  }
}

/** The project in `projectDir`, or undefined when it holds no .uproject. */
function projectContextIn(projectDir: string): ProjectContext | undefined {
  const ctx = new ProjectContext();
  try {
    ctx.setProject(projectDir);
    return ctx;
  } catch {
    return undefined;
  }
}

/** What one run of the install path did. */
export interface InstalledPlugin {
  /** The package name npm installed, which is what ue-mcp.yml records. */
  name: string;
  manifest: PluginManifest;
  version: string;
  /** The native module was copied into the project, so the UE project needs a rebuild. */
  nativeDeployed: boolean;
}

/**
 * The install path `install` and `update` share: npm (which writes
 * package.json), validation, the ue-mcp.yml entry pinned to `pin`, the native
 * module with stale files pruned, and skills. A check that fails after npm
 * ran undoes a fresh install, or says how to put the previous version back,
 * so a half-installed plugin is never reported as done.
 */
async function installPackage(
  proj: PluginProject,
  requested: string,
  version: string | undefined,
  pin: string | undefined,
  builtIns: BuiltInCategories,
  log: PluginOpLog,
): Promise<InstalledPlugin> {
  let nativeDeployed = false;
  // npm install refuses without a package.json.
  if (!fs.existsSync(path.join(proj.projectDir, "package.json"))) {
    log.note(`no package.json in ${proj.projectDir}; running 'npm init -y'`);
    await runNpm(["init", "-y"], proj.projectDir, log);
  }

  const spec = version ? `${requested}@${version}` : requested;
  checkSpec(spec);
  log.note(`installing ${spec}`);
  const before = readDependencies(proj.projectDir);
  await runNpm(["install", "--save", spec], proj.projectDir, log);
  const identified = installedPackageName(requested, proj.projectDir, before);
  if (!identified) {
    throw new PluginOpError(
      `npm install ${spec} succeeded but the package it installed could not be identified. ` +
      `Check package.json and remove it with \`npm uninstall <name>\` if it is not wanted.`,
    );
  }
  const name = identified;

  /** Refuse after npm ran: undo a fresh install, or say how to restore the previous version. */
  const refuse = async (msg: string): Promise<never> => {
    const prior = before[name];
    if (prior !== undefined) {
      throw new PluginOpError(`${msg} npm has already changed ${name} in package.json (it was ${prior}); \`npm install ${name}@${prior}\` puts it back.`);
    }
    try {
      await runNpm(["uninstall", name], proj.projectDir, log);
    } catch {
      throw new PluginOpError(`${msg} ${name} is still in node_modules and package.json; remove it with \`npm uninstall ${name}\`.`);
    }
    throw new PluginOpError(`${msg} ${name} was uninstalled again, so the project is as it was.`);
  };

  // Validate the just-installed plugin BEFORE editing ue-mcp.yml.
  const pkgDir = findInstalledPackage(name, proj.projectDir);
  if (!pkgDir) return refuse(`npm install succeeded but ${name} is not under node_modules.`);

  let manifest: PluginManifest;
  try {
    const parsed = loadManifest(pkgDir);
    manifest = parsed.manifest;
    // Salvaged units: the install stands, minus what failed validation. Say
    // which parts are gone here, at the one moment the user is looking.
    for (const d of parsed.dropped) {
      log.warn(`${name} declares ${d.path}, which failed validation and was dropped - ${d.reason}`);
    }
  } catch (e) {
    return refuse(`${name} has no valid ue-mcp.plugin.yml: ${(e as Error).message}.`);
  }

  const bridgeApi = readDeployedBridgeApiVersion(proj.projectDir);
  const versionRefusal = pluginVersionRefusal(name, manifest, packageVersion(), bridgeApi);
  if (versionRefusal) return refuse(`${name}: ${versionRefusal}`);

  const builtInNames = new Set(builtIns.map((t) => t.name));
  const noOverride =
    "Plugins may not override built-ins, and this cannot be overridden: install another version with `--version`, " +
    "or ask the plugin's author to change its actionPrefix or category name.";
  for (const target of Object.keys(manifest.inject)) {
    if (!builtInNames.has(target)) {
      return refuse(`${name} injects into '${target}', which is not a registered category. Valid: ${[...builtInNames].sort().join(", ")}.`);
    }
    const tool = builtIns.find((t) => t.name === target);
    for (const bare of Object.keys(manifest.inject[target])) {
      const prefixed = `${manifest.actionPrefix}_${bare}`;
      if (tool && tool.actions[prefixed]) return refuse(`${name}: action ${target}.${prefixed} collides with a built-in. ${noOverride}`);
    }
  }
  // Inter-plugin `provides:` collisions are settled at load (first writer
  // wins); only a built-in can be known to collide here.
  for (const provided of Object.keys(manifest.provides)) {
    if (builtInNames.has(provided)) return refuse(`${name}: provides category '${provided}' collides with a built-in. ${noOverride}`);
  }

  if (manifest.nativeModule && bridgeApi === null) {
    log.note(`WARNING: ${name} ships a native UE module but no UE_MCP_Bridge is deployed in this project yet. Run \`ue-mcp init\` to deploy the bridge before launching the editor.`);
  }

  if (manifest.uePluginDependency) {
    const present = projectContextIn(proj.projectDir)?.isUePluginEnabled(manifest.uePluginDependency);
    if (present === false) {
      log.note(`WARNING: ${name} requires UE plugin '${manifest.uePluginDependency}', not enabled in the .uproject. Enable it in the editor before using ${name} actions.`);
    } else if (present === undefined) {
      log.note(`could not determine whether UE plugin '${manifest.uePluginDependency}' is enabled - check the .uproject manually.`);
    }
  }

  const list = readPluginsList(proj.configPath);
  const existing = list.findIndex((p) => p.name === name);
  if (existing >= 0) list[existing] = { name, version: pin };
  else list.push({ name, version: pin });
  writePluginsList(proj.configPath, list);

  // Deploy nativeModule to <project>/Plugins/<uePluginName>/ and track every
  // copied path so uninstall can clean up.
  if (manifest.nativeModule) {
    const native = manifest.nativeModule;
    try {
      const result = deployNativeModule(pkgDir, native.source, native.uePluginName, proj.projectDir);
      const state = readNativeModulesState(proj.projectDir);
      const pruned = pruneStaleNativeFiles(proj.projectDir, state[name]?.files ?? [], result.fileList);
      if (pruned > 0) {
        log.note(`removed ${pruned} file(s) the previous ${native.uePluginName} version deployed and this one no longer ships`);
      }
      state[name] = {
        uePluginName: native.uePluginName,
        pluginVersion: installedVersion(pkgDir) ?? "0.0.0",
        installedAt: new Date().toISOString(),
        files: result.fileList,
      };
      writeNativeModulesState(proj.projectDir, state);
      log.note(`deployed native module ${native.uePluginName} (${result.filesCopied} files) to ${path.relative(proj.projectDir, result.destDir)}`);
      nativeDeployed = true;
    } catch (e) {
      throw new PluginOpError(
        `${name}: failed to deploy native module: ${(e as Error).message}. ` +
        `The package is installed and listed in ue-mcp.yml; fix the cause and run \`ue-mcp plugin install ${name}\` again, or remove it with \`ue-mcp plugin uninstall ${name}\`.`,
      );
    }
  }

  const skillsRoot = pluginSkillsRoot(pkgDir);
  if (listSkills(skillsRoot).length > 0) {
    const r = installSkillSet(proj.projectDir, name, skillsRoot);
    for (const line of conflictMessages(r)) log.note(`WARNING: ${line}`);
    const skills = [...r.installed, ...r.unchanged];
    if (skills.length > 0) log.note(`skills: ${skills.join(", ")} (in ${PROJECT_SKILLS_DIR}/)`);
  }
  return { name, manifest, version: installedVersion(pkgDir) ?? version ?? "?", nativeDeployed };
}

function logInstallSummary(name: string, manifest: PluginManifest, log: PluginOpLog): void {
  log.note(`installed ${name}@${manifest.minServerVersion ? `(server-min ${manifest.minServerVersion})` : ""}`);
  log.note(`actionPrefix: ${manifest.actionPrefix}`);
  for (const [category, actions] of Object.entries(manifest.inject)) {
    const names = Object.keys(actions).map((a) => `${manifest.actionPrefix}_${a}`).join(", ");
    log.note(`  ${category}: ${names}`);
  }
  for (const [category, providedSpec] of Object.entries(manifest.provides)) {
    log.note(`  ${category} (new category): ${Object.keys(providedSpec.actions).join(", ")}`);
  }
  if (Object.keys(manifest.flows).length > 0) log.note(`flows: ${Object.keys(manifest.flows).join(", ")}`);
  log.note(RESTART_NOTE);
}

export interface InstallOptions {
  /** The version or range to install, which ue-mcp.yml then pins. */
  version?: string;
  builtIns: BuiltInCategories;
  log: PluginOpLog;
  fetchImpl?: typeof fetch;
}

/** `ue-mcp plugin install <requested> [--version v]`: a registry slug or an npm spec. */
export async function installPlugin(proj: PluginProject, requested: string, opts: InstallOptions): Promise<InstalledPlugin> {
  refuseSuperseded(requested);
  // Clean registry slug (e.g. `meshy`) -> real package (`ue-mcp-meshy`). Unknown
  // names install verbatim, so plain npm packages keep working.
  const resolved = await resolveFromRegistry(requested, opts.fetchImpl);
  const name = resolved ?? requested;
  refuseSuperseded(name);
  if (resolved && resolved !== requested) opts.log.note(`resolved '${requested}' -> '${name}' via the registry`);
  const installed = await installPackage(proj, name, opts.version, opts.version, opts.builtIns, opts.log);
  if (installed.nativeDeployed) opts.log.note(REBUILD_NOTE);
  logInstallSummary(installed.name, installed.manifest, opts.log);
  return installed;
}

/** `ue-mcp plugin uninstall <name>`: the package, its ue-mcp.yml entry, its native module, .uproject entry and skills. */
export async function uninstallPlugin(proj: PluginProject, name: string, opts: { log: PluginOpLog }): Promise<{ removed: string[] }> {
  const { log } = opts;
  // The UE plugin the package deployed: from the install record, or, for a
  // superseded plugin installed before the record existed, from its entry.
  const nativeState = readNativeModulesState(proj.projectDir);
  const recorded = nativeState[name]?.uePluginName ?? supersededByName(name)?.[1].uePluginName;
  // The record is a file in the project, so a name that is not an Unreal name never becomes a folder to delete.
  if (recorded !== undefined && !UE_PLUGIN_NAME_RE.test(recorded)) {
    throw new PluginOpError(
      `the install record names ${JSON.stringify(recorded)} as the Unreal plugin, which is not a plugin name. Nothing was removed. ` +
      `Fix or delete the ${name} entry in .ue-mcp/native-modules.json and run \`ue-mcp plugin remove ${name}\` again.`,
    );
  }
  const uePluginName = recorded;
  const uproject = findUProject(proj.projectDir);
  const pluginDir = uePluginName ? path.join(proj.projectDir, "Plugins", uePluginName) : null;
  const pluginDirExists = pluginDir !== null && fs.existsSync(pluginDir);
  const uprojectText = uproject && uePluginName ? fs.readFileSync(uproject, "utf-8") : null;
  const listed = uprojectText !== null && uePluginName ? uprojectListsPlugin(uprojectText, uePluginName) : false;

  // An open editor holds the plugin's DLLs and rewrites the .uproject on save,
  // so nothing is touched while one has the project open.
  if (uproject && (pluginDirExists || listed)) {
    const holders = await editorsHoldingProject(proj.projectDir, uproject);
    if (holders.pids.length > 0) {
      throw new PluginOpError(
        `an editor has ${path.basename(uproject)} open (pid ${holders.pids.join(", ")}). ` +
        `Close it, then run \`ue-mcp plugin remove ${name}\` again. Nothing was removed.`,
      );
    }
  }

  // npm goes first: when it fails, the project is left exactly as it was.
  checkSpec(name);
  await runNpm(["uninstall", name], proj.projectDir, log);
  const removed: string[] = [`package ${name}`];

  if (nativeState[name]) undeployNativeModule(proj.projectDir, name);
  if (pluginDir && pluginDirExists) {
    const rel = path.relative(proj.projectDir, pluginDir).split(path.sep).join("/");
    try {
      fs.rmSync(pluginDir, { recursive: true, force: true, maxRetries: 3 });
    } catch (e) {
      throw new PluginOpError(`could not delete ${rel}: ${(e as Error).message}. Close anything holding files in it and run \`ue-mcp plugin remove ${name}\` again.`);
    }
    removed.push(`${rel}/ (source, Binaries and Intermediate)`);
  }
  if (uproject && uprojectText !== null && uePluginName && listed) {
    const edited = removeUProjectPluginEntry(uprojectText, uePluginName);
    if (edited.removed > 0) {
      fs.writeFileSync(uproject, edited.text);
      removed.push(`the ${uePluginName} entry in ${path.basename(uproject)}`);
    }
  }

  const removedSkills = removeSkillSet(proj.projectDir, name);
  if (removedSkills.length > 0) removed.push(`skills ${removedSkills.join(", ")}`);

  const list = readPluginsList(proj.configPath);
  if (list.some((p) => p.name === name)) {
    writePluginsList(proj.configPath, list.filter((p) => p.name !== name));
    removed.push(`the ${name} entry in ${path.basename(proj.configPath)}`);
  }
  log.note(`removed ${name}:`);
  for (const r of removed) log.note(`  ${r}`);
  log.note(RESTART_NOTE);
  return { removed };
}

/** One plugin `plugin update` will move, and why. */
export interface PluginUpdateStep {
  name: string;
  /** The version to install. */
  to: string;
  /** The ue-mcp.yml pin to write: the new version when the entry was pinned, else none. */
  pin?: string;
  reason: string;
}

/** What `plugin update` reads about each declared plugin. */
export interface PluginUpdateFacts {
  installed(name: string): string | undefined;
  /** The version whose native module is deployed in the project, if one is. */
  deployedNative(name: string): string | undefined;
  /** The newest published version, or null when npm could not say. */
  latest(name: string): string | null;
}

/** The declared entries `update [requested]` considers. */
function updateCandidates(entries: readonly PluginEntry[], requested: string | undefined): PluginEntry[] {
  return requested ? entries.filter((e) => e.name === requested || pluginSlug(e.name) === requested) : [...entries];
}

/**
 * Decide what `plugin update [name] [--version v]` installs. A plugin moves
 * when its package, its ue-mcp.yml pin or its deployed native module is not
 * at the target, which is the given version or the newest published one, so
 * a 0.x minor is crossed rather than held back by a caret range.
 */
export function planPluginUpdates(
  entries: readonly PluginEntry[],
  requested: string | undefined,
  version: string | undefined,
  facts: PluginUpdateFacts,
): { steps: PluginUpdateStep[]; current: string[]; errors: string[] } {
  const errors: string[] = [];
  if (version && !requested) return { steps: [], current: [], errors: ["--version needs a plugin name"] };
  const chosen = updateCandidates(entries, requested);
  if (requested && chosen.length === 0) {
    errors.push(`${requested} is not in ue-mcp.yml plugins:; install it with \`ue-mcp plugin install ${requested}\``);
  }
  const steps: PluginUpdateStep[] = [];
  const current: string[] = [];
  for (const entry of chosen) {
    const to = version ?? facts.latest(entry.name);
    if (!to) {
      errors.push(`could not read the latest version of ${entry.name} from npm; check the npm registry is reachable, or name the version with \`ue-mcp plugin update ${entry.name} --version <x.y.z>\``);
      continue;
    }
    const installed = facts.installed(entry.name);
    const native = facts.deployedNative(entry.name);
    const pin = entry.version ? to : undefined;
    const reason = installed !== to
      ? `${installed ?? "not installed"} -> ${to}`
      : entry.version && entry.version !== to
        ? `ue-mcp.yml pins ${entry.version}`
        : native !== undefined && native !== to
          ? `native module deployed from ${native}`
          : undefined;
    if (reason) steps.push({ name: entry.name, to, pin, reason });
    else current.push(`${entry.name}@${to}`);
  }
  return { steps, current, errors };
}

/** The version behind a package's `latest` dist-tag, or null when npm cannot answer. */
async function npmLatest(name: string, cwd: string): Promise<string | null> {
  try {
    assertPackageSpec(name);
  } catch {
    return null;
  }
  const out = await npmText(["view", name, "version"], cwd);
  const v = out?.trim().split(/\r?\n/).pop()?.trim();
  return v ? v : null;
}

export interface UpdateOptions {
  /** The version to move `requested` to; needs `requested`. */
  version?: string;
  builtIns: BuiltInCategories;
  log: PluginOpLog;
}

/** `ue-mcp plugin update [requested] [--version v]`: every declared plugin, or the one named. */
export async function updatePlugins(proj: PluginProject, requested: string | undefined, opts: UpdateOptions): Promise<{ steps: PluginUpdateStep[]; rebuild: boolean }> {
  const { log, version } = opts;
  if (requested) refuseSuperseded(requested);
  const nativeState = readNativeModulesState(proj.projectDir);
  const declared = readPluginsList(proj.configPath);
  // A plugin that ships in core is never reinstalled; removing it is the only step left.
  for (const e of declared) {
    const hit = supersededByName(e.name);
    if (hit) log.note(`WARNING: ${supersededMessage(hit[0], hit[1])}`);
  }
  const live = declared.filter((e) => !isSuperseded(e.name));
  const latest = new Map<string, string | null>();
  if (!version) {
    for (const e of updateCandidates(live, requested)) latest.set(e.name, await npmLatest(e.name, proj.projectDir));
  }
  const plan = planPluginUpdates(live, requested, version, {
    installed: (name) => {
      const dir = findInstalledPackage(name, proj.projectDir);
      return dir ? installedVersion(dir) : undefined;
    },
    deployedNative: (name) => nativeState[name]?.pluginVersion,
    latest: (name) => latest.get(name) ?? null,
  });
  if (plan.errors.length > 0) throw new PluginOpError(plan.errors.join("; "));
  for (const c of plan.current) log.note(`${c} is current`);

  let rebuild = false;
  for (const step of plan.steps) {
    log.note(`updating ${step.name} (${step.reason})`);
    const done = await installPackage(proj, step.name, step.to, step.pin, opts.builtIns, log);
    rebuild ||= done.nativeDeployed;
    log.note(`${step.name} is at ${done.version}${step.pin ? `, pinned in ue-mcp.yml` : ""}`);
  }

  // Skills of plugins that did not move are refreshed too, so a deleted skill comes back.
  const skills = syncPluginSkills(proj.projectDir, proj.configPath);
  for (const [plugin, r] of Object.entries(skills.plugins)) {
    for (const line of conflictMessages(r)) log.note(`WARNING: ${line}`);
    if (r.installed.length > 0) log.note(`${plugin}: updated skills ${r.installed.join(", ")}`);
    if (r.pruned.length > 0) log.note(`${plugin}: removed skills ${r.pruned.join(", ")}`);
  }

  if (plan.steps.length === 0) {
    log.note("nothing to update. Rebuild needed: no. MCP client restart needed: no.");
    return { steps: [], rebuild: false };
  }
  if (rebuild) log.note(REBUILD_NOTE);
  log.note(`updated ${plan.steps.length} plugin(s). Rebuild needed: ${rebuild ? "yes" : "no"}. MCP client restart needed: yes.`);
  log.note(RESTART_NOTE);
  return { steps: plan.steps, rebuild };
}
