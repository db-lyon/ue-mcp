#!/usr/bin/env node
/**
 * CLI for `ue-mcp plugin <subcommand>`.
 *
 *   install <name> [--version x.y.z]   npm install + add to ue-mcp.yml plugins:
 *   uninstall <name>                   npm uninstall + remove from plugins:
 *   list                               list configured plugins and their status
 *   update [name]                      npm update + re-validate manifests
 *   check-skills [dir]                 check a plugin's skills/ against the
 *                                      action surface, before publishing
 *   create <name> [--dir path]         scaffold a new plugin (superset of every
 *                                      extension shape: inject, provides, flows,
 *                                      and a dormant native C++ module)
 *   publish [dir] [--private|--public] push a listing (incl. its README) to the
 *                                      registry; merges over curated fields
 *
 * Editing ue-mcp.yml: js-yaml does not preserve comments. We mitigate by
 * rewriting only the `plugins:` block via a string-level surgery when
 * possible, and falling back to a full dump when the existing file lacks the
 * block entirely. The user is told both before and after about the restart
 * requirement - injected MCP actions only appear on next server start.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawnSync } from "node:child_process";
import { loadManifest } from "./plugin/manifest.js";
import { satisfiesMinimum } from "./plugin/version.js";
import { findInstalledPackage } from "./plugin/resolver.js";
import { pluginSlug, deriveGroups, isGroupEnabled } from "./plugin/plugin-groups.js";
import {
  targetFile,
  readEffectiveGroups,
  writeLayerGroups,
  type ConfigTarget,
} from "./plugin/plugin-config-store.js";
import { checkboxSelect, singleSelect } from "./ui/select.js";
import { readDeployedBridgeApiVersion } from "./plugin/bridge-api.js";
import {
  deployNativeModule,
  readNativeModulesState,
  undeployNativeModule,
  writeNativeModulesState,
} from "./plugin/native-deploy.js";
import { ALL_TOOLS } from "./tools.js";
import { readPluginsList, type PluginEntry } from "./plugin/plugins-list.js";
import { prefixedActionName } from "./plugin/manifest.js";
import { flowCategoryForCheck } from "./flow/flow-surface.js";
import { buildMicroGateway } from "./lean-context.js";
import {
  checkSkills,
  conflictMessages,
  installSkillSet,
  installedSkillName,
  listSkills,
  removeSkillSet,
  syncPluginSkills,
} from "./skills.js";
import { resolvePublishToken } from "./registry-auth.js";
import { parseEditorFlag, resolveEditorFlag, EditorFlagError } from "./editor-flag.js";
import { packageVersion } from "./package-root.js";
import { deriveDefaultPrefix, deriveUePluginName, writeScaffold } from "./plugin-scaffold.js";
import { ProjectContext } from "./project.js";
import { registryBase } from "./registry-catalog.js";
import { findUProject, projectDirOf } from "./uproject-path.js";

const RESTART_NOTE =
  "Injected actions appear on the next server start. Restart your MCP client (or `ue-mcp restart`).";

function fail(msg: string): never {
  console.error(`[ue-mcp plugin] ERROR: ${msg}`);
  process.exit(1);
}

function note(msg: string): void {
  console.log(`[ue-mcp plugin] ${msg}`);
}

interface ProjectInfo {
  projectDir: string;
  configPath: string;
}

/**
 * Where project resolution starts: the editor named by --editor when there is
 * one, cwd otherwise. Resolved lazily so a subcommand that never touches a
 * project (login, publish) does not fail on an unresolvable name.
 */
function projectStartDir(editor: string | undefined): string {
  if (editor === undefined) return process.cwd();
  try {
    const projectPath = resolveEditorFlag(editor);
    return projectDirOf(projectPath);
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
  }
}

function findProjectDir(startDir: string): ProjectInfo {
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
  fail(
    "could not find a project. Run from a directory containing a ue-mcp.yml or a .uproject. " +
    "Run `ue-mcp init` first if this is a fresh project.",
  );
}

function runNpm(args: string[], cwd: string): void {
  // On Windows, npm is `npm.cmd`. Node's child_process with `shell: false`
  // cannot launch a `.cmd` file directly (it expects a real executable), so
  // spawnSync returns status=null and an ENOENT-style error. Setting
  // `shell: true` routes through cmd.exe which resolves the .cmd correctly.
  // On POSIX `shell: true` is also safe; `npm` is a plain binary.
  const r = spawnSync("npm", args, {
    cwd,
    stdio: "inherit",
    shell: true,
  });
  if (r.error) {
    fail(`npm ${args.join(" ")} failed to start: ${r.error.message}`);
  }
  if (r.status !== 0) {
    fail(`npm ${args.join(" ")} exited ${r.status ?? "(killed by signal)"}`);
  }
}

/**
 * Write the plugins: array back to ue-mcp.yml. Performs string-level surgery
 * so non-plugins blocks (with comments, etc.) are left untouched.
 */
function writePluginsList(configPath: string, plugins: PluginEntry[]): void {
  const exists = fs.existsSync(configPath);
  const original = exists ? fs.readFileSync(configPath, "utf-8") : "";

  // Render the new plugins block in YAML.
  const rendered = plugins.length === 0
    ? "plugins: []\n"
    : "plugins:\n" + plugins.map((p) =>
        p.version
          ? `  - name: ${p.name}\n    version: ${p.version}\n`
          : `  - name: ${p.name}\n`,
      ).join("");

  if (!exists) {
    // Create a minimal stub file. The schema accepts a bare plugins/tasks/flows
    // and `ue-mcp init` will fill the rest later if needed.
    fs.writeFileSync(configPath, `ue-mcp:\n  version: 1\n\n${rendered}`);
    return;
  }

  // Locate an existing `plugins:` block at column 0 (a YAML root key).
  const blockRe = /^plugins:[\t ]*(?:\r?\n(?:[ \t]+[^\r\n]*\r?\n|\r?\n)*|\[\][\t ]*\r?\n)/m;
  const match = blockRe.exec(original);
  if (match) {
    const updated = original.slice(0, match.index) + rendered + original.slice(match.index + match[0].length);
    fs.writeFileSync(configPath, updated);
    return;
  }

  // Append at end of file. Add a leading blank line for readability if the
  // file does not already end with one.
  const sep = original.endsWith("\n") ? (original.endsWith("\n\n") ? "" : "\n") : "\n\n";
  fs.writeFileSync(configPath, original + sep + rendered);
}

/**
 * Resolve a registry slug to the package to install. Queries the UE-MCP
 * registry (UE_MCP_REGISTRY, default https://plugins.ue-mcp.com) so a clean
 * name like `meshy` installs the real package (`ue-mcp-meshy`). Returns null on
 * any miss/error/timeout, so the caller installs the given name directly - the
 * registry is a convenience layer, never a hard dependency. Runs the fetch in a
 * short-lived child so this stays synchronous like the rest of the CLI.
 */
function resolveFromRegistry(name: string): string | null {
  // A scoped or path-qualified spec is already a real npm name, not a slug.
  if (name.startsWith("@") || name.includes("/")) return null;
  const base = registryBase();
  const url = `${base}/api/resolve?name=${encodeURIComponent(name)}`;
  const script =
    `fetch(${JSON.stringify(url)}).then(async r=>{if(!r.ok)process.exit(3);` +
    `const j=await r.json();process.stdout.write(String(j.package||""));})` +
    `.catch(()=>process.exit(4));`;
  const res = spawnSync(process.execPath, ["-e", script], { encoding: "utf8", timeout: 8000 });
  const pkg = (res.stdout || "").trim();
  return res.status === 0 && pkg ? pkg : null;
}

function cmdInstall(args: string[], editor: string | undefined): void {
  const requested = args.shift();
  if (!requested) fail("usage: ue-mcp plugin install <name> [--version x.y.z]");

  let version: string | undefined;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--version") version = args[i + 1];
  }

  // Clean registry slug (e.g. `meshy`) -> real package (`ue-mcp-meshy`). Unknown
  // names install verbatim, so plain npm packages keep working.
  const resolved = resolveFromRegistry(requested);
  const name = resolved ?? requested;
  if (resolved && resolved !== requested) {
    note(`resolved '${requested}' -> '${name}' via the registry`);
  }

  const proj = findProjectDir(projectStartDir(editor));

  // Ensure a package.json exists. npm install will refuse without one.
  if (!fs.existsSync(path.join(proj.projectDir, "package.json"))) {
    note(`no package.json in ${proj.projectDir}; running 'npm init -y'`);
    runNpm(["init", "-y"], proj.projectDir);
  }

  const spec = version ? `${name}@${version}` : name;
  note(`installing ${spec}`);
  runNpm(["install", "--save", spec], proj.projectDir);

  // Validate the just-installed plugin BEFORE editing ue-mcp.yml.
  const pkgDir = findInstalledPackage(name, proj.projectDir);
  if (!pkgDir) fail(`npm install succeeded but ${name} is not under node_modules`);

  let manifest;
  try {
    const parsed = loadManifest(pkgDir);
    manifest = parsed.manifest;
    // Salvaged units: the install stands, minus what failed validation. Say
    // which parts are gone here, at the one moment the user is looking, rather
    // than leaving them to notice the missing actions later.
    for (const d of parsed.dropped) {
      console.error(`[ue-mcp] warn: ${name} declares ${d.path}, which failed validation and was dropped - ${d.reason}`);
    }
  } catch (e) {
    fail(`${name} has no valid ue-mcp.plugin.yml: ${(e as Error).message}`);
  }

  // minServerVersion gate at install time.
  const serverVersion = packageVersion();
  if (manifest.minServerVersion && !satisfiesMinimum(serverVersion, manifest.minServerVersion)) {
    fail(
      `${name} requires ue-mcp >= ${manifest.minServerVersion}; this server is ${serverVersion}. ` +
      `Upgrade with \`npm install -g ue-mcp@latest\`.`,
    );
  }

  // Validate `inject` targets against the built-in category set.
  const builtIn = new Set(ALL_TOOLS.map((t) => t.name));
  for (const target of Object.keys(manifest.inject)) {
    if (!builtIn.has(target)) {
      fail(
        `${name} injects into '${target}', which is not a registered category. ` +
        `Valid: ${[...builtIn].sort().join(", ")}.`,
      );
    }
    for (const bare of Object.keys(manifest.inject[target])) {
      const tool = ALL_TOOLS.find((t) => t.name === target);
      const prefixed = `${manifest.actionPrefix}_${bare}`;
      if (tool && tool.actions[prefixed]) {
        fail(`${name}: action ${target}.${prefixed} collides with a built-in. Plugins may not override built-ins.`);
      }
    }
  }

  // `provides:` collision check: plugin-owned categories must not shadow
  // a built-in. Inter-plugin collisions are handled at server-load time
  // (first writer wins) and not gated here because we can't know what
  // other plugins claim until the loader runs.
  for (const provided of Object.keys(manifest.provides)) {
    if (builtIn.has(provided)) {
      fail(
        `${name}: provides category '${provided}' collides with a built-in. Plugins may not override built-ins.`,
      );
    }
  }

  // Native module gate: refuse to install when the deployed bridge can't
  // support the required ABI. Without a deployed bridge we let the install
  // proceed and warn - `ue-mcp init` later deploys a current bridge.
  if (manifest.nativeModule) {
    const bridgeApi = readDeployedBridgeApiVersion(proj.projectDir);
    if (bridgeApi !== null && manifest.nativeModule.minBridgeApi > bridgeApi) {
      fail(
        `${name}: nativeModule requires bridge ABI >= ${manifest.nativeModule.minBridgeApi}, but the deployed bridge is ${bridgeApi}. ` +
        `Run \`ue-mcp deploy\` to refresh the bridge, then retry install.`,
      );
    }
    if (bridgeApi === null) {
      note(`WARNING: ${name} ships a native UE module but no UE_MCP_Bridge is deployed in this project yet. Run \`ue-mcp init\` to deploy the bridge before launching the editor.`);
    }
  }

  // Optional UE plugin dependency warning.
  if (manifest.uePluginDependency) {
    const present = projectContextIn(proj.projectDir)?.isUePluginEnabled(manifest.uePluginDependency);
    if (present === false) {
      note(`WARNING: ${name} requires UE plugin '${manifest.uePluginDependency}', not enabled in the .uproject. Enable it in the editor before using ${name} actions.`);
    } else if (present === undefined) {
      note(`could not determine whether UE plugin '${manifest.uePluginDependency}' is enabled - check the .uproject manually.`);
    }
  }

  // Update ue-mcp.yml plugins:
  const list = readPluginsList(proj.configPath);
  const existing = list.findIndex((p) => p.name === name);
  if (existing >= 0) {
    list[existing] = { name, version };
  } else {
    list.push({ name, version });
  }
  writePluginsList(proj.configPath, list);

  // Deploy nativeModule (if declared) to <project>/Plugins/<uePluginName>/
  // and track every copied path so uninstall can clean up.
  if (manifest.nativeModule) {
    const native = manifest.nativeModule;
    try {
      const result = deployNativeModule(pkgDir, native.source, native.uePluginName, proj.projectDir);
      const state = readNativeModulesState(proj.projectDir);
      const pkgJson = JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8")) as { version?: string };
      state[name] = {
        uePluginName: native.uePluginName,
        pluginVersion: pkgJson.version ?? "0.0.0",
        installedAt: new Date().toISOString(),
        files: result.fileList,
      };
      writeNativeModulesState(proj.projectDir, state);
      note(`deployed native module ${native.uePluginName} (${result.filesCopied} files) to ${path.relative(proj.projectDir, result.destDir)}`);
      note(`REBUILD REQUIRED: run \`npm run build\` (or rebuild the UE project) before launching the editor so the new C++ module compiles in.`);
    } catch (e) {
      fail(`${name}: failed to deploy native module: ${(e as Error).message}`);
    }
  }

  // Copy the plugin's skills/<name>/SKILL.md into .claude/skills/.
  const skillsRoot = path.join(pkgDir, "skills");
  if (listSkills(skillsRoot).length > 0) {
    const r = installSkillSet(proj.projectDir, name, skillsRoot);
    for (const line of conflictMessages(r)) note(`WARNING: ${line}`);
    const skills = [...r.installed, ...r.unchanged];
    if (skills.length > 0) note(`skills: ${skills.join(", ")} (in .claude/skills/)`);
  }

  // Summary
  note(`installed ${name}@${manifest.minServerVersion ? `(server-min ${manifest.minServerVersion})` : ""}`);
  note(`actionPrefix: ${manifest.actionPrefix}`);
  for (const [category, actions] of Object.entries(manifest.inject)) {
    const names = Object.keys(actions).map((a) => `${manifest.actionPrefix}_${a}`).join(", ");
    note(`  ${category}: ${names}`);
  }
  for (const [category, providedSpec] of Object.entries(manifest.provides)) {
    const actionNames = Object.keys(providedSpec.actions).join(", ");
    note(`  ${category} (new category): ${actionNames}`);
  }
  if (Object.keys(manifest.flows).length > 0) {
    note(`flows: ${Object.keys(manifest.flows).join(", ")}`);
  }
  note(RESTART_NOTE);
}

function cmdUninstall(args: string[], editor: string | undefined): void {
  const name = args.shift();
  if (!name) fail("usage: ue-mcp plugin uninstall <name>");
  const proj = findProjectDir(projectStartDir(editor));

  // Remove any deployed native module BEFORE npm uninstall so we can still
  // read the manifest (for diagnostics) and so the state file stays
  // consistent if anything fails midway.
  const nativeState = readNativeModulesState(proj.projectDir);
  if (nativeState[name]) {
    try {
      const removed = undeployNativeModule(proj.projectDir, name);
      note(`removed ${removed} native module file(s) for ${name}`);
    } catch (e) {
      note(`WARNING: could not fully clean up native module for ${name}: ${(e as Error).message}. ` +
        `Close the editor if it's running and remove leftover files under Plugins/${nativeState[name].uePluginName}/ manually.`);
    }
  }

  const removedSkills = removeSkillSet(proj.projectDir, name);
  if (removedSkills.length > 0) note(`removed skills: ${removedSkills.join(", ")}`);

  const list = readPluginsList(proj.configPath).filter((p) => p.name !== name);
  writePluginsList(proj.configPath, list);
  runNpm(["uninstall", name], proj.projectDir);
  note(`removed ${name}`);
  note(RESTART_NOTE);
}

function cmdList(editor: string | undefined): void {
  const proj = findProjectDir(projectStartDir(editor));
  const list = readPluginsList(proj.configPath);
  if (list.length === 0) {
    note(`no plugins declared in ${proj.configPath}`);
    return;
  }
  for (const entry of list) {
    const pkgDir = findInstalledPackage(entry.name, proj.projectDir);
    if (!pkgDir) {
      console.log(`  ${entry.name}${entry.version ? `@${entry.version}` : ""} - MISSING (not in node_modules)`);
      continue;
    }
    const pj = JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8")) as { version?: string };
    let status = "ok";
    let categories: string[] = [];
    let dropped: Array<{ path: string; reason: string }> = [];
    try {
      const parsed = loadManifest(pkgDir);
      categories = Object.keys(parsed.manifest.inject);
      dropped = parsed.dropped;
      if (dropped.length > 0) status = `ok (${dropped.length} part(s) dropped)`;
    } catch (e) {
      status = `manifest invalid: ${(e as Error).message}`;
    }
    console.log(
      `  ${entry.name}@${pj.version ?? "?"}` +
      (entry.version ? ` (pinned ${entry.version})` : "") +
      ` - ${status}` +
      (categories.length ? ` - injects: ${categories.join(", ")}` : ""),
    );
    for (const d of dropped) console.log(`      dropped ${d.path} - ${d.reason}`);
  }
}

function cmdUpdate(args: string[], editor: string | undefined): void {
  const name = args.shift();
  const proj = findProjectDir(projectStartDir(editor));
  if (name) {
    runNpm(["update", name], proj.projectDir);
  } else {
    runNpm(["update"], proj.projectDir);
  }
  const skills = syncPluginSkills(proj.projectDir, proj.configPath);
  for (const [plugin, r] of Object.entries(skills.plugins)) {
    for (const line of conflictMessages(r)) note(`WARNING: ${line}`);
    if (r.installed.length > 0) note(`${plugin}: updated skills ${r.installed.join(", ")}`);
    if (r.pruned.length > 0) note(`${plugin}: removed skills ${r.pruned.join(", ")}`);
  }
  note(RESTART_NOTE);
}

/**
 * Check a plugin package's skills/ before publishing: every SKILL.md needs a
 * description, a name matching its directory, and only actions that exist,
 * counting the ones this plugin adds. Another plugin's category cannot be
 * checked here and is listed as unverified rather than failed.
 */
function cmdCheckSkills(args: string[]): void {
  const dir = path.resolve(args.shift() ?? process.cwd());
  const root = path.join(dir, "skills");
  if (listSkills(root).length === 0) fail(`no skills/<name>/SKILL.md under ${dir}`);

  const known = new Set<string>();
  // The micro gateway is advertised too (the default strategy), so a skill may teach it.
  for (const tool of [...ALL_TOOLS, flowCategoryForCheck(), buildMicroGateway(ALL_TOOLS)]) {
    for (const action of Object.keys(tool.actions)) known.add(`${tool.name}.${action}`);
  }
  try {
    const { manifest } = loadManifest(dir);
    for (const [category, actions] of Object.entries(manifest.inject)) {
      for (const a of Object.keys(actions)) known.add(`${category}.${prefixedActionName(manifest.actionPrefix, a)}`);
    }
    for (const [category, spec] of Object.entries(manifest.provides)) {
      for (const a of Object.keys(spec.actions)) known.add(`${category}.${a}`);
    }
  } catch (e) {
    note(`no usable ue-mcp.plugin.yml (${(e as Error).message}); checking against core actions only`);
  }

  // The installer sets a plugin skill's name, so the directory need not match it.
  const result = checkSkills(root, known, { requireNameMatch: false });
  for (const p of result.problems) {
    console.log(`  ${p.skill}: ${p.detail}${p.didYouMean?.length ? ` Closest: ${p.didYouMean.join(", ")}` : ""}`);
  }
  for (const ref of result.unverified) console.log(`  unverified (unknown category): ${ref}`);
  if (result.problems.length > 0) fail(`${result.problems.length} problem(s) in ${result.checked.length} skill(s)`);
  let pkgName = path.basename(dir);
  try {
    pkgName = (JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf-8")) as { name?: string }).name ?? pkgName;
  } catch {
    // No package.json: name the prefix after the directory.
  }
  note(`${result.checked.length} skill(s) ok. Installed as:`);
  for (const skill of result.checked) console.log(`  ${skill} -> .claude/skills/${installedSkillName(pkgName, skill)}`);
}

function cmdCreate(args: string[]): void {
  const name = args.shift();
  if (!name) fail("usage: ue-mcp plugin create <name> [--dir path]");
  let targetDir = path.resolve(process.cwd(), name);
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--dir") targetDir = path.resolve(args[i + 1]);
  }

  if (fs.existsSync(targetDir) && fs.readdirSync(targetDir).length > 0) {
    fail(`target directory ${targetDir} exists and is not empty`);
  }
  fs.mkdirSync(targetDir, { recursive: true });

  // Derive a default actionPrefix from the package name suffix.
  const prefix = deriveDefaultPrefix(name);

  writeScaffold(targetDir, name, prefix);
  note(`scaffolded ${name} at ${targetDir}`);
  note(`superset scaffold: inject + provides + flows live; native C++ module dormant under ue/Plugins/${deriveUePluginName(name)}/.`);
  note(`keep the shapes you want, delete the rest. To activate native handlers, uncomment the nativeModule: block in ue-mcp.plugin.yml.`);
  note(`next steps:`);
  console.log(`  cd ${path.relative(process.cwd(), targetDir) || "."}`);
  console.log(`  npm install`);
  console.log(`  npm run build`);
  console.log(`  npm run check      # validate manifest + task wiring`);
  console.log(`  npm publish        # when ready`);
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


/* ------------------------------------------------------------------ */
/* publish - push a plugin listing (incl. its README) to the registry  */
/* ------------------------------------------------------------------ */

interface RegistryRow {
  slug: string;
  name: string;
  packageName?: string;
  repoUrl?: string;
  repoPrivate?: boolean;
  [k: string]: unknown;
}

/** Normalise package.json's `repository` (string or {url}) to a clean https URL. */
function repoUrlFromPkg(pkg: { repository?: unknown }): string | undefined {
  const r = pkg.repository;
  const raw = typeof r === "string" ? r : (r as { url?: string } | undefined)?.url;
  if (!raw) return undefined;
  return raw.replace(/^git\+/, "").replace(/\.git$/, "").replace(/^git@github\.com:/, "https://github.com/");
}

/** Fetch the current published catalog so publish can merge over curated fields. */
async function fetchCatalog(base: string): Promise<RegistryRow[]> {
  try {
    const res = await fetch(`${base}/api/plugins`);
    if (!res.ok) return [];
    const j = (await res.json()) as { plugins?: RegistryRow[] };
    return j.plugins ?? [];
  } catch {
    return [];
  }
}

/**
 * `ue-mcp plugin publish [dir] [--slug s] [--private|--public] [--token t] [--dry-run]`
 *
 * Reads the package at [dir] (default cwd): its README.md becomes the listing's
 * README (npm model - the docs travel with the package), and package.json fills
 * packageName / repoUrl / author. Curated marketplace fields (category, pricing,
 * tagline, tags, featured) are preserved by merging over the existing registry
 * row, so a re-publish only refreshes what the package owns.
 */
async function cmdPublish(args: string[]): Promise<void> {
  let dir = process.cwd();
  let slugFlag: string | undefined;
  let repoFlag: string | undefined;
  let tokenFlag: string | undefined;
  let privacy: boolean | undefined;
  let dryRun = false;
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "--slug") slugFlag = args[++i];
    else if (a === "--repo") repoFlag = args[++i];
    else if (a === "--token") tokenFlag = args[++i];
    else if (a === "--private") privacy = true;
    else if (a === "--public") privacy = false;
    else if (a === "--dry-run" || a === "--dry") dryRun = true;
    else if (!a.startsWith("--")) dir = path.resolve(a);
  }

  const pkgPath = path.join(dir, "package.json");
  if (!fs.existsSync(pkgPath)) fail(`no package.json in ${dir}`);
  const pkg = JSON.parse(fs.readFileSync(pkgPath, "utf-8")) as {
    name?: string;
    description?: string;
    author?: unknown;
    keywords?: string[];
    repository?: unknown;
  };
  if (!pkg.name) fail(`package.json in ${dir} has no "name"`);

  // README is optional but the whole point - warn loudly if missing.
  const readmePath = ["README.md", "readme.md", "Readme.md"]
    .map((f) => path.join(dir, f))
    .find((f) => fs.existsSync(f));
  const readme = readmePath ? fs.readFileSync(readmePath, "utf-8").trim() : "";
  if (!readme) note(`WARNING: no README.md in ${dir}; publishing with an empty README.`);

  const base = registryBase();
  // Precedence: --token, then env (CI), then the token cached by `ue-mcp login`.
  const token = await resolvePublishToken(tokenFlag);
  if (!token && !dryRun) {
    fail(
      "not logged in. Run `ue-mcp login` - it authorizes this machine with GitHub " +
      "and mints your own publish token, no site secret needed. " +
      "For CI, set UE_MCP_PUBLISH_TOKEN (mint one at " + base + "/account).",
    );
  }

  // Default slug: the package name minus the conventional `ue-mcp-` prefix.
  const slug = slugFlag ?? pkg.name.replace(/^ue-mcp-/, "");

  const catalog = await fetchCatalog(base);
  const existing = catalog.find((r) => r.slug === slug || r.packageName === pkg.name);

  const authorName =
    typeof pkg.author === "string"
      ? pkg.author.replace(/\s*<[^>]*>.*/, "").trim()
      : (pkg.author as { name?: string } | undefined)?.name;

  // Merge: existing curated fields first, then the package-owned overrides.
  const manifest: Record<string, unknown> = { ...(existing ?? {}) };
  delete manifest.status;
  delete manifest.rating;
  delete manifest.ratingCount;

  manifest.slug = slug;
  manifest.packageName = pkg.name;
  manifest.readme = readme;
  if (!manifest.name) manifest.name = pkg.name;
  if (!manifest.author && authorName) manifest.author = authorName;
  // The package owns where its source lives, so package.json's `repository`
  // wins over whatever the registry row happens to hold (which can be stale or
  // simply wrong). --repo overrides both.
  manifest.repoUrl = repoFlag ?? repoUrlFromPkg(pkg) ?? manifest.repoUrl;
  if (privacy !== undefined) manifest.repoPrivate = privacy;

  if (!existing) {
    // New listing: fill the required marketplace fields with sane defaults so
    // the publish validates; the owner can refine category/pricing on the site.
    if (!manifest.tagline) manifest.tagline = (pkg.description ?? pkg.name).slice(0, 140);
    if (!manifest.description) manifest.description = pkg.description ?? "";
    if (!manifest.category) manifest.category = "other";
    if (!manifest.pricing) manifest.pricing = "free";
    if (!manifest.author) manifest.author = authorName ?? pkg.name;
    if (Array.isArray(pkg.keywords) && !manifest.tags) {
      manifest.tags = pkg.keywords.filter((k) => k !== "ue-mcp-plugin").slice(0, 12);
    }
    note(`no existing listing for '${slug}'; creating a new one with default category/pricing.`);
  }

  if (dryRun) {
    note(`dry run - would POST to ${base}/api/publish:`);
    const preview = { ...manifest, readme: `<${readme.length} chars>` };
    console.log(JSON.stringify(preview, null, 2));
    return;
  }

  const res = await fetch(`${base}/api/publish`, {
    method: "POST",
    headers: { "content-type": "application/json", authorization: `Bearer ${token}` },
    body: JSON.stringify(manifest),
  });
  const text = await res.text();
  if (!res.ok) fail(`publish failed (HTTP ${res.status}): ${text}`);
  note(`published '${slug}' to ${base} (README ${readme.length} chars${privacy !== undefined ? `, repoPrivate=${privacy}` : ""}).`);
}

/* ------------------------------------------------------------------ */
/* config - toggle a plugin's flow groups per user, per layer          */
/* ------------------------------------------------------------------ */

const CONFIG_RESTART_NOTE =
  "Plugin flows load at server start. Restart your MCP client (or `ue-mcp restart`) for group changes to take effect.";

interface ResolvedInstalled {
  slug: string;
  pkgName: string;
  pkgDir: string;
}

/**
 * Resolve a `config <name>` argument to an installed plugin. Accepts the slug
 * (`recipes`), the full package name (`ue-mcp-recipes`), or a bare name that
 * takes the conventional prefix. Prefers a match in the declared plugins: list.
 */
function resolveInstalledPlugin(proj: ProjectInfo, nameArg: string): ResolvedInstalled {
  const list = readPluginsList(proj.configPath);
  const declared = list.find((p) => p.name === nameArg || pluginSlug(p.name) === nameArg);
  const candidates = declared
    ? [declared.name]
    : [nameArg, `ue-mcp-${nameArg}`];
  for (const pkgName of candidates) {
    const pkgDir = findInstalledPackage(pkgName, proj.projectDir);
    if (pkgDir) return { slug: pluginSlug(pkgName), pkgName, pkgDir };
  }
  fail(
    `plugin '${nameArg}' is not installed in ${proj.projectDir}. ` +
    `Install it with \`ue-mcp plugin install ${nameArg}\` first, or run \`ue-mcp plugin list\`.`,
  );
}

async function cmdConfig(args: string[], editor: string | undefined): Promise<void> {
  const nameArg = args.shift();
  if (!nameArg) {
    fail(
      "usage: ue-mcp plugin config <name> [--enable a,b] [--disable c,d] [--list-groups] [--local|--project]\n" +
      "  no --enable/--disable => interactive menu. Default write target is ~/.ue-mcp/config.yml (you, all projects).",
    );
  }

  const enable = new Set<string>();
  const disable = new Set<string>();
  let listGroups = false;
  let target: ConfigTarget = "global";
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "--enable") (args[++i] ?? "").split(",").map((s) => s.trim()).filter(Boolean).forEach((g) => enable.add(g));
    else if (a === "--disable") (args[++i] ?? "").split(",").map((s) => s.trim()).filter(Boolean).forEach((g) => disable.add(g));
    else if (a === "--list-groups" || a === "--list") listGroups = true;
    else if (a === "--local") target = "local";
    else if (a === "--project" || a === "--tracked") target = "project";
    else if (a === "--global") target = "global";
  }

  const proj = findProjectDir(projectStartDir(editor));
  const resolved = resolveInstalledPlugin(proj, nameArg);
  const manifest = loadManifest(resolved.pkgDir).manifest;
  const groups = deriveGroups(manifest.flows);
  if (groups.length === 0) {
    fail(`${resolved.slug} declares no flows, so there are no groups to configure.`);
  }

  const effective = readEffectiveGroups(proj.projectDir, resolved.slug);

  if (listGroups) {
    note(`groups for ${resolved.slug} (${Object.keys(manifest.flows).length} flow(s)):`);
    for (const g of groups) {
      const on = isGroupEnabled({ groups: effective.state }, g);
      const src = effective.source[g] ? ` [set in ${effective.source[g]}]` : " [default]";
      console.log(`  ${on ? "on " : "off"}  ${g}${on ? "" : src}`);
    }
    return;
  }

  // Validate any group names the user named against what the plugin declares.
  for (const g of [...enable, ...disable]) {
    if (!groups.includes(g)) {
      fail(`'${g}' is not a group of ${resolved.slug}. Groups: ${groups.join(", ")}.`);
    }
  }

  // Scriptable path: apply deltas to the chosen layer only.
  if (enable.size > 0 || disable.size > 0) {
    writeLayerGroups(targetFile(proj.projectDir, target), resolved.slug, (existing) => {
      const next = { ...existing };
      for (const g of enable) next[g] = true;
      for (const g of disable) next[g] = false;
      return next;
    });
    reportConfigWrite(resolved.slug, target, proj);
    return;
  }

  // Interactive path: choose group states, then the write target.
  await cmdConfigInteractive(resolved.slug, groups, effective.state, proj);
}

async function cmdConfigInteractive(
  slug: string,
  groups: string[],
  effectiveState: Record<string, boolean>,
  proj: ProjectInfo,
): Promise<void> {
  const items = groups.map((g) => ({
    label: g,
    checked: isGroupEnabled({ groups: effectiveState }, g),
  }));
  const states = await checkboxSelect(`${slug}: enable flow groups`, items);
  const chosen: Record<string, boolean> = {};
  groups.forEach((g, i) => (chosen[g] = states[i]));

  const targetLabels: Array<{ label: string; target: ConfigTarget }> = [
    { label: "you, all projects  (~/.ue-mcp/config.yml)", target: "global" },
    { label: "you, this project  (ue-mcp.local.yml, untracked)", target: "local" },
    { label: "the team, tracked  (ue-mcp.yml)", target: "project" },
  ];
  const idx = await singleSelect("Save preference to", targetLabels.map((t) => t.label));
  const target = targetLabels[idx].target;

  writeLayerGroups(targetFile(proj.projectDir, target), slug, () => chosen);
  reportConfigWrite(slug, target, proj);
}

function reportConfigWrite(slug: string, target: ConfigTarget, proj: ProjectInfo): void {
  const file = targetFile(proj.projectDir, target);
  const shown = target === "global" ? file : path.relative(proj.projectDir, file);
  note(`updated ${slug} group config in ${shown}`);
  if (target === "project") {
    note("NOTE: ue-mcp.yml is source-tracked - this sets a team-wide default and was re-dumped (YAML comments not preserved).");
  }
  note(CONFIG_RESTART_NOTE);
}

/** Entry point for `ue-mcp plugin <subcommand>`. */
export async function run(argv: string[]): Promise<number | void> {
  // --editor names one of the editors this server drives; every project lookup
  // starts from it instead of cwd. Taken before the subcommand is shifted off
  // so it can appear anywhere on the line.
  const parsed = parseEditorFlag(argv);
  const args = parsed.rest;
  const editor = parsed.editor;
  const sub = args.shift();
  switch (sub) {
    case "install": cmdInstall(args, editor); break;
    case "uninstall":
    case "remove": cmdUninstall(args, editor); break;
    case "list":
    case "ls": cmdList(editor); break;
    case "update":
    case "upgrade": cmdUpdate(args, editor); break;
    case "check-skills": cmdCheckSkills(args); break;
    case "create":
    case "new":
    case "init": cmdCreate(args); break;
    case "publish": await cmdPublish(args).catch((e) => fail(e instanceof Error ? e.message : String(e))); break;
    case "config": await cmdConfig(args, editor).catch((e) => fail(e instanceof Error ? e.message : String(e))); break;
    default:
      console.error(
        "Usage:\n" +
        "  ue-mcp plugin install <name> [--version x.y.z]\n" +
        "  ue-mcp plugin uninstall <name>\n" +
        "  ue-mcp plugin list\n" +
        "  ue-mcp plugin update [name]\n" +
        "  ue-mcp plugin check-skills [dir]\n" +
        "  ue-mcp plugin config <name> [--enable a,b] [--disable c,d] [--list-groups] [--local|--project]\n" +
        "  ue-mcp plugin create <name> [--dir path]\n" +
        "  ue-mcp plugin publish [dir] [--slug s] [--private|--public] [--dry-run]",
      );
      return 1;
  }
}
