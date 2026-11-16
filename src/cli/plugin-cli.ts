#!/usr/bin/env node
// @session per-session: Installs and inspects plugins for one project, chosen with --editor.
/**
 * CLI for `ue-mcp plugin <subcommand>`.
 *
 *   install <name> [--version x.y.z]   npm install + add to ue-mcp.yml plugins:
 *   uninstall <name>                   npm uninstall + remove from plugins:
 *   list                               list configured plugins and their status
 *   update [name] [--version x.y.z]    rerun install for each plugin not at its
 *                                      target (latest, or the given version)
 *   check-skills [dir]                 check a plugin's skills/ against the
 *                                      action surface, before publishing
 *   create <name> [--dir path]         scaffold a new plugin (superset of every
 *                                      extension shape: inject, provides, flows,
 *                                      and a dormant native C++ module)
 *   publish [dir] [--dry-run]          push the registry listing; derives
 *                                      provides/ueVersions, keeps the slug
 *   record-specs [dir] --project <p>   record the native module's handler
 *                                      contracts from a running editor (#1282);
 *                                      --check fails when the file differs
 *
 * install, uninstall and update are extensions/plugin-install.ts, the library
 * the daemon's /v1/plugins routes call too; this file parses their arguments
 * and prints.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { loadManifest, manifestActionNames, type PluginManifest } from "../extensions/manifest.js";
import { findInstalledPackage } from "../extensions/resolver.js";
import { pluginSlug, deriveGroups, isGroupEnabled } from "../extensions/plugin-groups.js";
import {
  targetFile,
  readEffectiveGroups,
  writeLayerGroups,
  type ConfigTarget,
} from "../extensions/plugin-config-store.js";
import { checkboxSelect, singleSelect } from "../terminal/select.js";
import { ALL_TOOLS } from "../tools.js";
import { readPluginsList } from "../extensions/plugins-list.js";
import {
  findPluginProject,
  formatPluginError,
  formatPluginNote,
  formatPluginWarn,
  installPlugin,
  refuseSuperseded,
  uninstallPlugin,
  updatePlugins,
  type PluginOpLog,
  type PluginProject,
} from "../extensions/plugin-install.js";
import { flowCategoryForCheck } from "../flow/flow-surface.js";
import { buildMicroGateway } from "../surface/context/micro-context.js";
import { actionParamNames } from "../surface/action-signature.js";
import { checkSkills, installedSkillName, listSkills } from "../extensions/skills.js";
import { pluginSkillsRoot } from "../extensions/skill-file.js";
import { resolvePublishToken } from "../extensions/registry-auth.js";
import { EditorBridge } from "../bridge/bridge.js";
import { recordedSpecsProblems, type HandlerSpecs } from "../surface/handler-spec.js";
import { parseEditorFlag, selectProject, type ProjectSelection } from "./editor-flag.js";
import { EditorFlagError, resolveEditorFlag } from "../integrations/mcp-clients/configured-editors.js";
import { firstPositional } from "./args.js";
import { formatUsage, type CliUsage } from "./cli-main.js";
import { deriveDefaultPrefix, deriveUePluginName, writeScaffold } from "../extensions/plugin-scaffold.js";
import { fetchCatalogRows, registryBase } from "../extensions/registry-catalog.js";
import { supersededByName, supersededMessage } from "../extensions/superseded.js";
import { chooseListing, derivedListingFields, listingDiff } from "../extensions/publish-listing.js";
import { projectDirOf } from "../config/uproject-path.js";

function fail(msg: string): never {
  console.error(formatPluginError(msg));
  process.exit(1);
}

function note(msg: string): void {
  console.log(formatPluginNote(msg));
}

/** Plugin changes print as they go; npm writes to the terminal itself. */
const terminalLog: PluginOpLog = {
  note,
  warn: (msg) => console.error(formatPluginWarn(msg)),
};

type ProjectInfo = PluginProject;

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
  try {
    return findPluginProject(startDir);
  } catch (e) {
    fail((e as Error).message);
  }
}

/** Run a plugin change, exiting non-zero with its message when it is refused or fails. */
async function runOp(op: () => Promise<unknown>): Promise<void> {
  try {
    await op();
  } catch (e) {
    fail(e instanceof Error ? e.message : String(e));
  }
}

async function cmdInstall(args: string[], editor: string | undefined): Promise<void> {
  const requested = args.shift();
  if (!requested) fail("usage: ue-mcp plugin install <name> [--version x.y.z]");
  let version: string | undefined;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--version") version = args[i + 1];
  }
  await runOp(async () => refuseSuperseded(requested));
  const proj = findProjectDir(projectStartDir(editor));
  await runOp(() => installPlugin(proj, requested, { version, builtIns: ALL_TOOLS, log: terminalLog }));
}

async function cmdUninstall(args: string[], editor: string | undefined): Promise<void> {
  const name = args.shift();
  if (!name) fail("usage: ue-mcp plugin uninstall <name>");
  const proj = findProjectDir(projectStartDir(editor));
  await runOp(() => uninstallPlugin(proj, name, { log: terminalLog }));
}

function cmdList(editor: string | undefined): void {
  const proj = findProjectDir(projectStartDir(editor));
  const list = readPluginsList(proj.configPath);
  if (list.length === 0) {
    note(`no plugins declared in ${proj.configPath}`);
    return;
  }
  for (const entry of list) {
    const superseded = supersededByName(entry.name);
    if (superseded) {
      console.log(`  ${entry.name} - SKIPPED: ${supersededMessage(superseded[0], superseded[1])}`);
      continue;
    }
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

async function cmdUpdate(args: string[], editor: string | undefined): Promise<void> {
  let requested: string | undefined;
  let version: string | undefined;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--version") version = args[++i];
    else if (!args[i].startsWith("-")) requested ??= args[i];
  }
  const named = requested;
  if (named) await runOp(async () => refuseSuperseded(named));
  const proj = findProjectDir(projectStartDir(editor));
  await runOp(() => updatePlugins(proj, requested, { version, builtIns: ALL_TOOLS, log: terminalLog }));
}

/**
 * Check a plugin package's skills/ before publishing: every SKILL.md needs a
 * description, a name matching its directory, and only actions that exist,
 * counting the ones this plugin adds. Another plugin's category cannot be
 * checked here and is listed as unverified rather than failed.
 */
function cmdCheckSkills(args: string[]): void {
  const dir = path.resolve(args.shift() ?? process.cwd());
  const root = pluginSkillsRoot(dir);
  if (listSkills(root).length === 0) fail(`no skills/<name>/SKILL.md under ${dir}`);

  const known = new Set<string>();
  const coreParams = new Map<string, string[]>();
  // The micro gateway is advertised too (the default strategy), so a skill may teach it.
  for (const tool of [...ALL_TOOLS, flowCategoryForCheck(), buildMicroGateway(ALL_TOOLS)]) {
    for (const action of Object.keys(tool.actions)) {
      known.add(`${tool.name}.${action}`);
      coreParams.set(`${tool.name}.${action}`, actionParamNames(tool, action));
    }
  }
  try {
    const { manifest } = loadManifest(dir);
    for (const name of manifestActionNames(manifest, new Set(ALL_TOOLS.map((t) => t.name)))) known.add(name);
  } catch (e) {
    note(`no usable ue-mcp.plugin.yml (${(e as Error).message}); checking against core actions only`);
  }

  // The installer sets a plugin skill's name, so the directory need not match it.
  const result = checkSkills(root, known, { requireNameMatch: false, paramsOf: (ref) => coreParams.get(ref) });
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

/** The current catalog, every row, so publish can merge over curated fields; [] when the registry cannot say. */
async function fetchCatalog(): Promise<RegistryRow[]> {
  return fetchCatalogRows<RegistryRow>({ timeoutMs: 15_000 }).catch(() => []);
}

/**
 * Record a native module's handler contracts from the editor that has it
 * loaded (#1282). The module registers each handler with its C++ spec; the
 * bridge publishes them in get_bridge_capabilities.pluginHandlerSpecs, and
 * this writes the ones nativeModule.handlers declares to nativeModule.specs.
 * Every declared handler must have one: a handler without a contract would be
 * dropped from the surface at load.
 */
async function cmdRecordSpecs(args: string[]): Promise<void> {
  const check = args.includes("--check");
  let target: ProjectSelection;
  try {
    target = selectProject(args, { projectFlag: true });
  } catch (e) {
    fail(e instanceof Error ? e.message : String(e));
  }
  const project = target.projectPath;
  const dir = path.resolve(firstPositional(target.rest) ?? process.cwd());
  if (!project || !project.endsWith(".uproject") || !fs.existsSync(project)) {
    fail("record-specs needs --project <path to the .uproject of a running editor that loads this plugin's native module>");
  }
  const { manifest } = loadManifest(dir);
  const native = manifest.nativeModule;
  if (!native?.specs) fail("the manifest declares no nativeModule.specs; add one (for example specs: handler-specs.json)");

  const bridge = new EditorBridge();
  bridge.setProjectContext(path.resolve(project));
  try {
    await bridge.ensureConnected(10000);
  } catch (e) {
    fail(`no editor answered for ${project}: ${(e as Error).message}`);
  }
  const capabilities = bridge.capabilities;
  const live = capabilities?.pluginHandlerSpecs as HandlerSpecs | undefined;
  bridge.disconnect();
  if (!capabilities || capabilities.legacy) {
    fail("the editor did not answer the capabilities handshake; wait until it finishes loading and run again");
  }
  if (!live) fail("the connected bridge publishes no pluginHandlerSpecs; it predates bridge ABI 2. Run ue-mcp deploy and rebuild.");

  const declared = Object.keys(native.handlers).sort();
  const missing = declared.filter((m) => !live[m]);
  if (missing.length > 0) {
    fail(`${missing.length} declared handler(s) registered no contract: ${missing.join(", ")}. Register each with UEMCP::RegisterExternalHandler(name, fn, params).`);
  }
  const handlers = Object.fromEntries(declared.map((m) => [m, live[m]]));
  const snapshot = { handlerCount: declared.length, handlers };
  const problems = recordedSpecsProblems(snapshot);
  if (problems.length > 0) fail(`the live contracts cannot generate a surface:\n  ${problems.join("\n  ")}`);

  const out = path.join(dir, native.specs);
  const text = `${JSON.stringify(snapshot, null, 2)}\n`;
  const current = fs.existsSync(out) ? fs.readFileSync(out, "utf-8").replace(/\r\n/g, "\n") : null;
  if (check) {
    if (current !== text) fail(`${native.specs} differs from the contracts the running module registered; run ue-mcp plugin record-specs`);
    note(`${native.specs} matches the running module (${declared.length} handlers)`);
    return;
  }
  fs.writeFileSync(out, text);
  note(`recorded ${declared.length} handler contracts to ${native.specs}${current === text ? " (unchanged)" : ""}`);
}

/**
 * `ue-mcp plugin publish [dir]`: push the package's registry listing.
 *
 * A row already listing this package keeps its slug unless --slug and
 * --rename say otherwise. Facts the package declares (provides, ueVersions)
 * are derived on every publish; curated text comes from the manifest's
 * `registry:` block or flags, and anything else is kept from the row.
 * --dry-run prints the field-by-field change instead of posting.
 */
async function cmdPublish(args: string[]): Promise<void> {
  let dir = process.cwd();
  let slugFlag: string | undefined;
  let repoFlag: string | undefined;
  let tokenFlag: string | undefined;
  let privacy: boolean | undefined;
  let dryRun = false;
  let rename = false;
  let readmeMode: "snapshot" | "live" = "snapshot";
  let tagsFromKeywords = false;
  const curated: { tagline?: string; description?: string; category?: string } = {};
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "--slug") slugFlag = args[++i];
    else if (a === "--rename") rename = true;
    else if (a === "--repo") repoFlag = args[++i];
    else if (a === "--token") tokenFlag = args[++i];
    else if (a === "--private") privacy = true;
    else if (a === "--public") privacy = false;
    else if (a === "--dry-run" || a === "--dry") dryRun = true;
    else if (a === "--tagline") curated.tagline = args[++i];
    else if (a === "--description") curated.description = args[++i];
    else if (a === "--category") curated.category = args[++i];
    else if (a === "--tags-from-keywords") tagsFromKeywords = true;
    else if (a === "--readme") {
      const mode = args[++i];
      if (mode !== "snapshot" && mode !== "live") fail(`--readme must be snapshot or live, not '${mode}'`);
      readmeMode = mode;
    } else if (a.startsWith("-")) fail(`unknown flag ${a} for plugin publish`);
    else dir = path.resolve(a);
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
  // An empty stored readme is rendered live from the public repo; a snapshot
  // is what the listing shows until the next publish.
  const readme = readmeMode === "live" ? "" : readmePath ? fs.readFileSync(readmePath, "utf-8").trim() : "";
  if (readmeMode === "snapshot" && !readme) {
    note(`WARNING: no README.md in ${dir}; the listing falls back to the repo's README, if the repo is public.`);
  }

  // The listing fields the package itself declares.
  let pluginManifest: PluginManifest | undefined;
  try {
    pluginManifest = loadManifest(dir).manifest;
  } catch (e) {
    note(`WARNING: no usable ue-mcp.plugin.yml (${(e as Error).message}); provides is taken from skills/ only.`);
  }

  const base = registryBase();
  // Precedence: --token, then env (CI), then the token cached by `ue-mcp login`.
  const token = await resolvePublishToken(tokenFlag);
  if (!token && !dryRun) {
    fail(
      "not logged in. Run `ue-mcp login` - it signs in with an email code or GitHub " +
      "and mints your own publish token, no site secret needed. " +
      "For CI, set UE_MCP_PUBLISH_TOKEN (mint one at " + base + "/account).",
    );
  }

  const catalog = await fetchCatalog();
  const choice = chooseListing(catalog, pkg.name, slugFlag, rename);
  if (!choice.ok) fail(choice.error);
  const { slug, existing } = choice;
  if (choice.renamed) note(`renaming listing '${choice.renamed}' -> '${slug}'`);

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

  // Facts the package declares replace hand-curated copies on every publish.
  const derived = derivedListingFields(
    pluginManifest,
    listSkills(pluginSkillsRoot(dir)).length > 0,
    Array.isArray(existing?.provides) ? (existing.provides as string[]) : [],
  );
  manifest.provides = derived.provides;
  if (derived.ueVersions) manifest.ueVersions = derived.ueVersions;
  // Curated fields: ue-mcp.plugin.yml `registry:`, then flags.
  const own = { ...(pluginManifest?.registry ?? {}), ...Object.fromEntries(Object.entries(curated).filter(([, v]) => v !== undefined)) };
  for (const [k, v] of Object.entries(own)) if (v !== undefined) manifest[k] = v;
  if (tagsFromKeywords && Array.isArray(pkg.keywords)) {
    manifest.tags = pkg.keywords.filter((k) => k !== "ue-mcp-plugin").slice(0, 12);
  }

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
    const changes = listingDiff(existing, manifest);
    note(`dry run - nothing posted. ${existing ? `Changes to listing '${existing.slug}'` : `New listing '${slug}'`} at ${base}:`);
    console.log(changes.length > 0 ? changes.join("\n") : "  (no changes)");
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
  const states = await checkboxSelect(`${slug}: enable flow groups`, items, "Pass --enable <groups> or --disable <groups>.");
  const chosen: Record<string, boolean> = {};
  groups.forEach((g, i) => (chosen[g] = states[i]));

  const targetLabels: Array<{ label: string; target: ConfigTarget }> = [
    { label: "you, all projects  (~/.ue-mcp/config.yml)", target: "global" },
    { label: "you, this project  (ue-mcp.local.yml, untracked)", target: "local" },
    { label: "the team, tracked  (ue-mcp.yml)", target: "project" },
  ];
  const idx = await singleSelect("Save preference to", targetLabels.map((t) => t.label), "Pass --global, --local or --project with --enable or --disable.");
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

export const usage: CliUsage = {
  usage: [
    "ue-mcp plugin <subcommand>",
    "  ue-mcp plugin install <name> [--version x.y.z]",
    "  ue-mcp plugin uninstall <name>",
    "  ue-mcp plugin list",
    "  ue-mcp plugin update [name] [--version x.y.z]",
    "  ue-mcp plugin check-skills [dir]",
    "  ue-mcp plugin config <name> [--enable a,b] [--disable c,d] [--list-groups] [--local|--project]",
    "  ue-mcp plugin create <name> [--dir path]",
    "  ue-mcp plugin publish [dir] [--slug s [--rename]] [--private|--public] [--readme snapshot|live]",
    "                        [--tagline t] [--description d] [--category c] [--tags-from-keywords] [--dry-run]",
    "  ue-mcp plugin record-specs [dir] --project <path.uproject> [--check]",
  ].join("\n"),
  summary: "Install, create and publish plugins.",
  flags: [{ name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives" }],
};

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
    case "install": await cmdInstall(args, editor); break;
    case "uninstall":
    case "remove": await cmdUninstall(args, editor); break;
    case "list":
    case "ls": cmdList(editor); break;
    case "update":
    case "upgrade": await cmdUpdate(args, editor); break;
    case "check-skills": cmdCheckSkills(args); break;
    case "create":
    case "new":
    case "init": cmdCreate(args); break;
    case "publish": await cmdPublish(args).catch((e) => fail(e instanceof Error ? e.message : String(e))); break;
    case "config": await cmdConfig(args, editor).catch((e) => fail(e instanceof Error ? e.message : String(e))); break;
    case "record-specs": await cmdRecordSpecs(args).catch((e) => fail(e instanceof Error ? e.message : String(e))); break;
    default:
      console.error(formatUsage(usage));
      return 1;
  }
}
