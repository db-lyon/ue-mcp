#!/usr/bin/env node
/**
 * CLI for `ue-mcp plugin <subcommand>`.
 *
 *   install <name> [--version x.y.z]   npm install + add to ue-mcp.yml plugins:
 *   uninstall <name>                   npm uninstall + remove from plugins:
 *   list                               list configured plugins and their status
 *   update [name]                      npm update + re-validate manifests
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
 * requirement — injected MCP actions only appear on next server start.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawnSync } from "node:child_process";
import yaml from "js-yaml";
import { loadManifest } from "./plugin/manifest.js";
import { satisfiesMinimum } from "./plugin/version.js";
import { findInstalledPackage } from "./plugin/resolver.js";
import { readDeployedBridgeApiVersion } from "./plugin/bridge-api.js";
import {
  deployNativeModule,
  readNativeModulesState,
  undeployNativeModule,
  writeNativeModulesState,
} from "./plugin/native-deploy.js";
import { ALL_TOOLS } from "./tools.js";

const args = process.argv.slice(2);
const sub = args.shift();

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

function findProjectDir(startDir: string): ProjectInfo {
  let dir = path.resolve(startDir);
  const root = path.parse(dir).root;
  while (true) {
    if (fs.existsSync(path.join(dir, "ue-mcp.yml"))) {
      return { projectDir: dir, configPath: path.join(dir, "ue-mcp.yml") };
    }
    if (fs.existsSync(path.join(dir, "package.json")) && fs.readdirSync(dir).some((f) => f.endsWith(".uproject"))) {
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

interface PluginEntry {
  name: string;
  version?: string;
}

function readPluginsList(configPath: string): PluginEntry[] {
  if (!fs.existsSync(configPath)) return [];
  const raw = yaml.load(fs.readFileSync(configPath, "utf-8")) as { plugins?: unknown } | null;
  if (!raw || !Array.isArray(raw.plugins)) return [];
  const out: PluginEntry[] = [];
  for (const entry of raw.plugins) {
    if (entry && typeof entry === "object" && typeof (entry as { name?: unknown }).name === "string") {
      const e = entry as { name: string; version?: unknown };
      out.push({
        name: e.name,
        version: typeof e.version === "string" ? e.version : undefined,
      });
    }
  }
  return out;
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
  const base = (process.env.UE_MCP_REGISTRY ?? "https://plugins.ue-mcp.com").replace(/\/+$/, "");
  const url = `${base}/api/resolve?name=${encodeURIComponent(name)}`;
  const script =
    `fetch(${JSON.stringify(url)}).then(async r=>{if(!r.ok)process.exit(3);` +
    `const j=await r.json();process.stdout.write(String(j.package||""));})` +
    `.catch(()=>process.exit(4));`;
  const res = spawnSync(process.execPath, ["-e", script], { encoding: "utf8", timeout: 8000 });
  const pkg = (res.stdout || "").trim();
  return res.status === 0 && pkg ? pkg : null;
}

function cmdInstall(): void {
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

  const proj = findProjectDir(process.cwd());

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
    manifest = loadManifest(pkgDir).manifest;
  } catch (e) {
    fail(`${name} has no valid ue-mcp.plugin.yml: ${(e as Error).message}`);
  }

  // minServerVersion gate at install time.
  const pkgJson = JSON.parse(fs.readFileSync(path.join(__dirnameOrCwd(), "..", "package.json"), "utf-8")) as { version: string };
  if (manifest.minServerVersion && !satisfiesMinimum(pkgJson.version, manifest.minServerVersion)) {
    fail(
      `${name} requires ue-mcp >= ${manifest.minServerVersion}; this server is ${pkgJson.version}. ` +
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
  // proceed and warn — `ue-mcp init` later deploys a current bridge.
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
    const present = uePluginEnabled(proj.projectDir, manifest.uePluginDependency);
    if (present === false) {
      note(`WARNING: ${name} requires UE plugin '${manifest.uePluginDependency}', not enabled in the .uproject. Enable it in the editor before using ${name} actions.`);
    } else if (present === undefined) {
      note(`could not determine whether UE plugin '${manifest.uePluginDependency}' is enabled — check the .uproject manually.`);
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

function cmdUninstall(): void {
  const name = args.shift();
  if (!name) fail("usage: ue-mcp plugin uninstall <name>");
  const proj = findProjectDir(process.cwd());

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

  const list = readPluginsList(proj.configPath).filter((p) => p.name !== name);
  writePluginsList(proj.configPath, list);
  runNpm(["uninstall", name], proj.projectDir);
  note(`removed ${name}`);
  note(RESTART_NOTE);
}

function cmdList(): void {
  const proj = findProjectDir(process.cwd());
  const list = readPluginsList(proj.configPath);
  if (list.length === 0) {
    note(`no plugins declared in ${proj.configPath}`);
    return;
  }
  for (const entry of list) {
    const pkgDir = findInstalledPackage(entry.name, proj.projectDir);
    if (!pkgDir) {
      console.log(`  ${entry.name}${entry.version ? `@${entry.version}` : ""} — MISSING (not in node_modules)`);
      continue;
    }
    const pj = JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8")) as { version?: string };
    let status = "ok";
    let categories: string[] = [];
    try {
      const m = loadManifest(pkgDir).manifest;
      categories = Object.keys(m.inject);
    } catch (e) {
      status = `manifest invalid: ${(e as Error).message}`;
    }
    console.log(
      `  ${entry.name}@${pj.version ?? "?"}` +
      (entry.version ? ` (pinned ${entry.version})` : "") +
      ` — ${status}` +
      (categories.length ? ` — injects: ${categories.join(", ")}` : ""),
    );
  }
}

function cmdUpdate(): void {
  const name = args.shift();
  const proj = findProjectDir(process.cwd());
  if (name) {
    runNpm(["update", name], proj.projectDir);
  } else {
    runNpm(["update"], proj.projectDir);
  }
  note(RESTART_NOTE);
}

function cmdCreate(): void {
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

function deriveDefaultPrefix(pkgName: string): string {
  const normalized = pkgName.replace(/[^a-z0-9]+/gi, "_").toLowerCase();
  const parts = normalized.split("_").filter(Boolean);
  const seed = parts.length > 0 ? parts.map((s) => s[0]).join("").slice(0, 4) : "plg";
  return /^[a-z]/.test(seed) ? seed : `p${seed}`;
}

/**
 * Scaffold a plugin that is a SUPERSET of every extension shape ue-mcp
 * supports, so an author never has to discover a capability before using it:
 *
 *   - inject:   add prefixed actions onto a built-in category
 *   - provides: own a brand-new top-level category (unprefixed actions)
 *   - flows:    chain tasks/actions into one orchestrated call
 *   - nativeModule: ship C++ handlers on the bridge - scaffolded but DORMANT
 *
 * The native C++ module is the one capability that cannot be a live default:
 * declaring `nativeModule:` makes `ue-mcp plugin install` deploy the module and
 * force a UE rebuild. So the C++ skeleton is written to disk under
 * ue/Plugins/<UePlugin>/, but its manifest block is commented out. Uncommenting
 * three lines activates it - no separate "style" flag, no compile tax until the
 * author opts in. The author deletes whatever shape they don't want.
 */
function writeScaffold(dir: string, pkgName: string, prefix: string): void {
  const helloClass = "Hello";
  const greetClass = "Greet";
  const uePlugin = deriveUePluginName(pkgName);
  const nativeCategory = `${prefix}_native`;
  // The scaffold's tasks import `ue-mcp/task`, which only exists from the
  // version that introduced it. Default the floor to the running server's
  // version so a freshly scaffolded plugin declares a dependency that can
  // actually resolve the import; an explicit env override still wins.
  const minServer = process.env.UE_MCP_PLUGIN_MIN_SERVER ?? readServerVersion();
  const year = new Date().getFullYear();

  const pkgJson = {
    name: pkgName,
    version: "0.1.0",
    description: `${pkgName} - ue-mcp plugin`,
    type: "module",
    main: "dist/index.js",
    // `ue` ships the dormant native C++ source so authors can activate it
    // without re-vendoring; `LICENSE` is included for npm publish hygiene.
    files: ["dist", "ue", "ue-mcp.plugin.yml", "knowledge", "README.md", "LICENSE"],
    keywords: ["ue-mcp-plugin", "unreal-engine"],
    author: "",
    license: "MIT",
    repository: { type: "git", url: "" },
    peerDependencies: {
      "ue-mcp": `>=${minServer}`,
    },
    devDependencies: {
      "ue-mcp": `^${minServer}`,
      "js-yaml": "^4.1.0",
      "@types/js-yaml": "^4.0.9",
      typescript: "^5.7.0",
    },
    scripts: {
      build: "tsc",
      check: "node scripts/check.mjs",
      prepublishOnly: "npm run build && npm run check",
    },
  };
  fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify(pkgJson, null, 2) + "\n");

  const tsconfig = {
    compilerOptions: {
      target: "ES2022",
      module: "Node16",
      moduleResolution: "Node16",
      outDir: "dist",
      rootDir: "src",
      strict: true,
      esModuleInterop: true,
      declaration: true,
      sourceMap: true,
      skipLibCheck: true,
    },
    include: ["src/**/*"],
  };
  fs.writeFileSync(path.join(dir, "tsconfig.json"), JSON.stringify(tsconfig, null, 2) + "\n");

  const manifestYaml =
`actionPrefix: ${prefix}
minServerVersion: ${minServer}

# This scaffold is a SUPERSET: it shows every way a plugin can extend ue-mcp.
# Keep the shapes you need and delete the rest.
#   1) inject   - add actions onto a built-in category (names are prefixed)
#   2) provides - own a brand-new top-level category (names are NOT prefixed)
#   3) flows    - chain tasks/actions into one orchestrated call
#   4) nativeModule (commented, at the bottom) - ship C++ handlers on the bridge

# 1) inject: '${prefix}_hello' lands on the built-in 'project' tool, reached as
#    project(action="${prefix}_hello").
inject:
  project:
    hello:
      task: ${prefix}.hello
      description: "Greet by name - a stand-in inject action this scaffold ships with"
      schema:
        name: { type: string, description: "Who to greet (default: world)" }

# 2) provides: a category the plugin owns. Actions are NOT prefixed - the
#    category name is the namespace, e.g. ${prefix}(action="greet").
provides:
  ${prefix}:
    description: "${pkgName} - example plugin-owned category"
    actions:
      greet:
        task: ${prefix}.greet
        description: "Greet by name from a plugin-owned category"
        schema:
          name: { type: string, description: "Who to greet (default: world)" }

tasks:
  ${prefix}.hello:
    class_path: tasks/${helloClass}
  ${prefix}.greet:
    class_path: tasks/${greetClass}

# 3) flows: chain tasks/actions into one call. This one just runs the hello
#    task; expand 'steps' to orchestrate real multi-step work.
flows:
  ${prefix}_demo:
    description: "Example flow: run the hello task"
    steps:
      1:
        task: ${prefix}.hello
        options:
          name: world

# 4) nativeModule: ship C++ handlers that register on the bridge. DORMANT by
#    default - the C++ skeleton already lives under ue/Plugins/${uePlugin}/, but
#    it is not deployed or compiled until you UNCOMMENT this block. Activating it
#    makes 'ue-mcp plugin install' deploy the module and require a UE rebuild.
#    The handlers then surface unprefixed under a '${nativeCategory}' category,
#    e.g. ${nativeCategory}(action="echo").
# nativeModule:
#   uePluginName: ${uePlugin}
#   minBridgeApi: 1
#   source: ue/Plugins/${uePlugin}
#   category: ${nativeCategory}
#   categoryDescription: "${pkgName} native C++ handlers"
#   handlers:
#     echo:
#       description: "Echo a name back from native C++ (required: name)"
#       schema:
#         name: { type: string, description: "Name to echo" }
`;
  fs.writeFileSync(path.join(dir, "ue-mcp.plugin.yml"), manifestYaml);

  // ── TypeScript: index stub + the two example tasks ──────────────────────
  fs.mkdirSync(path.join(dir, "src", "tasks"), { recursive: true });

  const indexTs =
`// Entry point for the npm package. ue-mcp loads tasks by their manifest
// class_path, not through this file, so it can stay empty - but package.json's
// "main" points here, so the build must emit it.
export {};
`;
  fs.writeFileSync(path.join(dir, "src", "index.ts"), indexTs);

  const helloTs =
`import { UeMcpTask, type TaskResult } from "ue-mcp/task";

interface Options {
  name?: string;
}

/** Backs project(action="${prefix}_hello") and the ${prefix}_demo flow. */
export default class ${helloClass} extends UeMcpTask<Options> {
  get taskName() { return "${prefix}.hello"; }

  async execute(): Promise<TaskResult> {
    const who = this.options.name ?? "world";
    return { success: true, data: { greeting: \`hello, \${who}\` } };
  }
}
`;
  fs.writeFileSync(path.join(dir, "src", "tasks", `${helloClass}.ts`), helloTs);

  const greetTs =
`import { UeMcpTask, type TaskResult } from "ue-mcp/task";

interface Options {
  name?: string;
}

/**
 * Backs ${prefix}(action="greet") - the plugin-owned category. Swap the body
 * for real work; compose built-ins with \`this.call("level.get_outliner", {})\`.
 */
export default class ${greetClass} extends UeMcpTask<Options> {
  get taskName() { return "${prefix}.greet"; }

  async execute(): Promise<TaskResult> {
    const who = this.options.name ?? "world";
    return { success: true, data: { greeting: \`greetings, \${who}\` } };
  }
}
`;
  fs.writeFileSync(path.join(dir, "src", "tasks", `${greetClass}.ts`), greetTs);

  // ── Pre-publish sanity check ────────────────────────────────────────────
  fs.mkdirSync(path.join(dir, "scripts"), { recursive: true });
  fs.writeFileSync(path.join(dir, "scripts", "check.mjs"), CHECK_SCRIPT);

  // ── Knowledge ───────────────────────────────────────────────────────────
  fs.mkdirSync(path.join(dir, "knowledge"), { recursive: true });
  fs.writeFileSync(
    path.join(dir, "knowledge", "project.md"),
    `# ${pkgName} - project actions\n\nDescribe in one screen what your plugin contributes to the project category.\n`,
  );

  // ── Dormant native C++ module ────────────────────────────────────────────
  writeNativeSkeleton(dir, uePlugin, pkgName);

  // ── README / LICENSE / .gitignore ────────────────────────────────────────
  const readme =
`# ${pkgName}

A ue-mcp plugin. Install with:

\`\`\`bash
ue-mcp plugin install ${pkgName}
\`\`\`

## What this scaffold ships

It is a **superset** - every way a plugin can extend ue-mcp, wired and working.
Keep what you want, delete the rest.

- **inject** -> \`project(action="${prefix}_hello")\` (an action added onto a built-in category)
- **provides** -> \`${prefix}(action="greet")\` (a new top-level category this plugin owns)
- **flows** -> \`${prefix}_demo\` (a chained, one-call orchestration)
- **nativeModule** -> a C++ handler skeleton under \`ue/Plugins/${uePlugin}/\`, **dormant** until you activate it

## Activate the native C++ module

The C++ source is already on disk but inert. To turn it on:

1. Uncomment the \`nativeModule:\` block at the bottom of \`ue-mcp.plugin.yml\`.
2. Reinstall (\`ue-mcp plugin install ${pkgName}\`) - this deploys the module into
   your project's \`Plugins/\` and requires a UE rebuild before the editor starts.

The handler then surfaces as \`${prefix}_native(action="echo")\`.

## Develop

\`\`\`bash
npm install
npm run build
npm run check      # validate the manifest + task wiring
\`\`\`

See https://ue-mcp.com/docs/plugins/ for the full author contract.
`;
  fs.writeFileSync(path.join(dir, "README.md"), readme);

  fs.writeFileSync(
    path.join(dir, "LICENSE"),
    `MIT License

Copyright (c) ${year} ${pkgName} authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
`,
  );

  fs.writeFileSync(
    path.join(dir, ".gitignore"),
    `node_modules/\ndist/\n*.tgz\n.DS_Store\n`,
  );
}

/**
 * Derive a UE-legal module/plugin name (PascalCase, leading letter) from the
 * npm package name. "voxel-plugin-tools" -> "VoxelPluginTools".
 */
function deriveUePluginName(pkgName: string): string {
  const parts = pkgName.replace(/[^a-z0-9]+/gi, " ").trim().split(/\s+/).filter(Boolean);
  const pascal = parts.map((p) => p.charAt(0).toUpperCase() + p.slice(1)).join("");
  return /^[A-Za-z]/.test(pascal) ? pascal : `Plugin${pascal}`;
}

/**
 * Write a minimal, compile-ready UE C++ module under ue/Plugins/<uePlugin>/.
 * It registers one example handler ("echo") on the bridge via the public
 * UEMCP::RegisterExternalHandler API. Inert until the manifest's nativeModule:
 * block is uncommented.
 */
function writeNativeSkeleton(dir: string, uePlugin: string, pkgName: string): void {
  const base = path.join(dir, "ue", "Plugins", uePlugin);
  const moduleDir = path.join(base, "Source", uePlugin);
  const publicDir = path.join(moduleDir, "Public");
  const privateDir = path.join(moduleDir, "Private");
  const handlersDir = path.join(privateDir, "Handlers");
  fs.mkdirSync(handlersDir, { recursive: true });
  fs.mkdirSync(publicDir, { recursive: true });

  const uplugin = {
    FileVersion: 3,
    Version: 1,
    VersionName: "0.1.0",
    FriendlyName: uePlugin,
    Description: `Native C++ handlers for the ${pkgName} ue-mcp plugin`,
    Category: "Editor",
    CreatedBy: "",
    EnabledByDefault: true,
    CanContainContent: false,
    Modules: [
      { Name: uePlugin, Type: "Editor", LoadingPhase: "PostEngineInit" },
    ],
    Plugins: [
      { Name: "UE_MCP_Bridge", Enabled: true },
    ],
  };
  fs.writeFileSync(path.join(base, `${uePlugin}.uplugin`), JSON.stringify(uplugin, null, 2) + "\n");

  fs.writeFileSync(
    path.join(moduleDir, `${uePlugin}.Build.cs`),
`using UnrealBuildTool;

public class ${uePlugin} : ModuleRules
{
\tpublic ${uePlugin}(ReadOnlyTargetRules Target) : base(Target)
\t{
\t\tPCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

\t\tPublicDependencyModuleNames.AddRange(new string[]
\t\t{
\t\t\t"Core",
\t\t\t"CoreUObject",
\t\t\t"Engine",
\t\t\t"Json",
\t\t\t"JsonUtilities",
\t\t});

\t\t// UE_MCP_Bridge exposes UEMCP::RegisterExternalHandler (MCPHandlerRegistration.h).
\t\tPrivateDependencyModuleNames.AddRange(new string[]
\t\t{
\t\t\t"UE_MCP_Bridge",
\t\t});
\t}
}
`,
  );

  fs.writeFileSync(
    path.join(publicDir, `${uePlugin}Module.h`),
`#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(Log${uePlugin}, Log, All);

class F${uePlugin}Module : public IModuleInterface
{
public:
\tvirtual void StartupModule() override;
\tvirtual void ShutdownModule() override;
};
`,
  );

  fs.writeFileSync(
    path.join(privateDir, `${uePlugin}Module.cpp`),
`#include "${uePlugin}Module.h"
#include "Modules/ModuleManager.h"
#include "MCPHandlerRegistration.h"
#include "Handlers/ExampleHandlers.h"

DEFINE_LOG_CATEGORY(Log${uePlugin});
IMPLEMENT_MODULE(F${uePlugin}Module, ${uePlugin})

void F${uePlugin}Module::StartupModule()
{
\t// Bare method names. When this module is surfaced via nativeModule.category
\t// in ue-mcp.plugin.yml, ue-mcp routes <category>(action="echo") to "echo".
\tUEMCP::RegisterExternalHandler(TEXT("echo"), &FExampleHandlers::Echo);

\tUE_LOG(Log${uePlugin}, Log, TEXT("[${uePlugin}] Registered 1 handler"));
}

void F${uePlugin}Module::ShutdownModule()
{
\tUEMCP::UnregisterExternalHandler(TEXT("echo"));
}
`,
  );

  fs.writeFileSync(
    path.join(handlersDir, "ExampleHandlers.h"),
`#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** Example external handler registered on the UE-MCP bridge. */
class FExampleHandlers
{
public:
\t/** Echo a 'name' param back. Bridge method: "echo". */
\tstatic TSharedPtr<FJsonValue> Echo(const TSharedPtr<FJsonObject>& Params);
};
`,
  );

  fs.writeFileSync(
    path.join(handlersDir, "ExampleHandlers.cpp"),
`#include "Handlers/ExampleHandlers.h"

TSharedPtr<FJsonValue> FExampleHandlers::Echo(const TSharedPtr<FJsonObject>& Params)
{
\tFString Name = TEXT("world");
\tif (Params.IsValid())
\t{
\t\tParams->TryGetStringField(TEXT("name"), Name);
\t}

\tTSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
\tResult->SetBoolField(TEXT("success"), true);
\tResult->SetStringField(TEXT("echo"), FString::Printf(TEXT("hello, %s"), *Name));
\treturn MakeShared<FJsonValueObject>(Result);
}
`,
  );
}

/**
 * Standalone pre-publish check shipped into scaffolded plugins as
 * scripts/check.mjs. Verifies the manifest parses, every action's task:
 * resolves to a tasks: entry, and every tasks: class_path points at a real
 * source/built file. No build step required - runs on plain node.
 */
const CHECK_SCRIPT =
`#!/usr/bin/env node
// Sanity-check a ue-mcp plugin: manifest parses, task references resolve, and
// every class_path points at a real source or built file.
import fs from "node:fs";
import path from "node:path";
import yaml from "js-yaml";

const root = process.cwd();
const manifestPath = path.join(root, "ue-mcp.plugin.yml");
let errors = 0;
const fail = (m) => { console.error("  x " + m); errors++; };

if (!fs.existsSync(manifestPath)) {
  console.error("ue-mcp.plugin.yml not found");
  process.exit(1);
}
const m = yaml.load(fs.readFileSync(manifestPath, "utf8")) ?? {};
const tasks = m.tasks ?? {};

// Collect every task reference from inject + provides + flows.
const refs = [];
for (const [cat, actions] of Object.entries(m.inject ?? {}))
  for (const [a, def] of Object.entries(actions))
    if (def?.task) refs.push(["inject " + cat + "." + a, def.task]);
for (const [cat, spec] of Object.entries(m.provides ?? {}))
  for (const [a, def] of Object.entries(spec?.actions ?? {}))
    if (def?.task) refs.push(["provides " + cat + "." + a, def.task]);
for (const [fname, fdef] of Object.entries(m.flows ?? {}))
  for (const [s, step] of Object.entries(fdef?.steps ?? {}))
    if (step?.task) refs.push(["flow " + fname + "." + s, step.task]);

for (const [where, task] of refs)
  if (!tasks[task]) fail(where + " references task '" + task + "' with no tasks: entry");

for (const [name, def] of Object.entries(tasks)) {
  const seg = String(def.class_path).replace(/\\./g, "/");
  const candidates = [
    path.join(root, "src", seg + ".ts"),
    path.join(root, "dist", seg + ".js"),
  ];
  if (!candidates.some((c) => fs.existsSync(c)))
    fail("task '" + name + "' class_path '" + def.class_path + "' resolves to no src/*.ts or dist/*.js");
}

if (m.nativeModule?.source && !fs.existsSync(path.join(root, m.nativeModule.source)))
  fail("nativeModule.source '" + m.nativeModule.source + "' does not exist");

if (errors) {
  console.error("\\n" + errors + " problem(s) found.");
  process.exit(1);
}
console.log("ue-mcp plugin check: OK");
`;

function uePluginEnabled(projectDir: string, name: string): boolean | undefined {
  const files = fs.readdirSync(projectDir).filter((f) => f.endsWith(".uproject"));
  if (files.length === 0) return undefined;
  try {
    const raw = JSON.parse(fs.readFileSync(path.join(projectDir, files[0]), "utf-8")) as {
      Plugins?: Array<{ Name?: string; Enabled?: boolean }>;
    };
    if (!raw.Plugins) return false;
    const entry = raw.Plugins.find((p) => p.Name === name);
    if (!entry) return false;
    return entry.Enabled !== false;
  } catch {
    return undefined;
  }
}

/** Read the running ue-mcp server's version from its own package.json. */
function readServerVersion(): string {
  try {
    const pkgJson = JSON.parse(
      fs.readFileSync(path.join(__dirnameOrCwd(), "..", "package.json"), "utf-8"),
    ) as { version?: string };
    return pkgJson.version ?? "1.0.0";
  } catch {
    return "1.0.0";
  }
}

/**
 * `__dirname` is not available in ESM. Fall back to the cwd so the relative
 * path-to-package.json lookup still works when the CLI is invoked directly.
 */
function __dirnameOrCwd(): string {
  try {
    return path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1"));
  } catch {
    return process.cwd();
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
async function cmdPublish(): Promise<void> {
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

  const base = (process.env.UE_MCP_REGISTRY ?? "https://plugins.ue-mcp.com").replace(/\/+$/, "");
  const token = tokenFlag ?? process.env.UE_MCP_PUBLISH_TOKEN ?? process.env.REGISTRY_PUBLISH_TOKEN;
  if (!token && !dryRun) {
    fail(
      "no publish token. Set UE_MCP_PUBLISH_TOKEN (or pass --token). " +
      "Get it from the registry owner / Render dashboard (REGISTRY_PUBLISH_TOKEN).",
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

switch (sub) {
  case "install": cmdInstall(); break;
  case "uninstall":
  case "remove": cmdUninstall(); break;
  case "list":
  case "ls": cmdList(); break;
  case "update":
  case "upgrade": cmdUpdate(); break;
  case "create":
  case "new":
  case "init": cmdCreate(); break;
  case "publish": cmdPublish().catch((e) => fail(e instanceof Error ? e.message : String(e))); break;
  default:
    console.error(
      "Usage:\n" +
      "  ue-mcp plugin install <name> [--version x.y.z]\n" +
      "  ue-mcp plugin uninstall <name>\n" +
      "  ue-mcp plugin list\n" +
      "  ue-mcp plugin update [name]\n" +
      "  ue-mcp plugin create <name> [--dir path]\n" +
      "  ue-mcp plugin publish [dir] [--slug s] [--private|--public] [--dry-run]",
    );
    process.exit(1);
}
