#!/usr/bin/env node
/**
 * CLI for `ue-mcp plugin <subcommand>`.
 *
 *   install <name> [--version x.y.z]   npm install + add to ue-mcp.yml plugins:
 *   uninstall <name>                   npm uninstall + remove from plugins:
 *   list                               list configured plugins and their status
 *   update [name]                      npm update + re-validate manifests
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

function cmdInstall(): void {
  const name = args.shift();
  if (!name) fail("usage: ue-mcp plugin install <name> [--version x.y.z]");

  let version: string | undefined;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--version") version = args[i + 1];
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
  note(`next steps:`);
  console.log(`  cd ${path.relative(process.cwd(), targetDir) || "."}`);
  console.log(`  npm install`);
  console.log(`  npm run build`);
  console.log(`  npm publish        # when ready`);
}

function deriveDefaultPrefix(pkgName: string): string {
  const normalized = pkgName.replace(/[^a-z0-9]+/gi, "_").toLowerCase();
  const parts = normalized.split("_").filter(Boolean);
  const seed = parts.length > 0 ? parts.map((s) => s[0]).join("").slice(0, 4) : "plg";
  return /^[a-z]/.test(seed) ? seed : `p${seed}`;
}

function writeScaffold(dir: string, pkgName: string, prefix: string): void {
  const className = "Hello";
  // The scaffold's task imports `ue-mcp/task`, which only exists from the
  // version that introduced it. Default the floor to the running server's
  // version so a freshly scaffolded plugin declares a dependency that can
  // actually resolve the import; an explicit env override still wins.
  const minServer = process.env.UE_MCP_PLUGIN_MIN_SERVER ?? readServerVersion();
  const pkgJson = {
    name: pkgName,
    version: "0.1.0",
    description: `${pkgName} - ue-mcp plugin`,
    type: "module",
    main: "dist/index.js",
    files: ["dist", "ue-mcp.plugin.yml", "knowledge", "README.md"],
    keywords: ["unreal-engine"],
    peerDependencies: {
      "ue-mcp": `>=${minServer}`,
    },
    devDependencies: {
      "ue-mcp": `^${minServer}`,
      typescript: "^5.7.0",
    },
    scripts: {
      build: "tsc",
      prepublishOnly: "npm run build",
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

inject:
  project:
    hello:
      task: ${prefix}.hello
      description: "Replace me - a stand-in action this scaffold ships with"
      schema:
        name: { type: string }

tasks:
  ${prefix}.hello:
    class_path: tasks/${className}

flows: {}
`;
  fs.writeFileSync(path.join(dir, "ue-mcp.plugin.yml"), manifestYaml);

  const helloTs =
`import { UeMcpTask, type TaskResult } from "ue-mcp/task";

interface Options {
  name?: string;
}

export default class ${className} extends UeMcpTask<Options> {
  get taskName() { return "${prefix}.hello"; }

  async execute(): Promise<TaskResult> {
    const who = this.options.name ?? "world";
    return { success: true, data: { greeting: \`hello, \${who}\` } };
  }
}
`;
  fs.mkdirSync(path.join(dir, "src", "tasks"), { recursive: true });
  fs.writeFileSync(path.join(dir, "src", "tasks", `${className}.ts`), helloTs);

  fs.mkdirSync(path.join(dir, "knowledge"), { recursive: true });
  fs.writeFileSync(
    path.join(dir, "knowledge", "project.md"),
    `# ${pkgName} - project actions\n\nDescribe in one screen what your plugin contributes to the project category.\n`,
  );

  const readme =
`# ${pkgName}

A ue-mcp plugin. Install with:

\`\`\`bash
ue-mcp plugin install ${pkgName}
\`\`\`

After a server restart, the plugin's actions appear inside the built-in
category they target. The default scaffold injects \`project(action="${prefix}_hello")\`.

## Develop

\`\`\`bash
npm install
npm run build
\`\`\`

See https://db-lyon.github.io/ue-mcp/plugins/ for the full author contract.
`;
  fs.writeFileSync(path.join(dir, "README.md"), readme);

  fs.writeFileSync(
    path.join(dir, ".gitignore"),
    `node_modules/\ndist/\n*.tgz\n.DS_Store\n`,
  );
}

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
  default:
    console.error(
      "Usage:\n" +
      "  ue-mcp plugin install <name> [--version x.y.z]\n" +
      "  ue-mcp plugin uninstall <name>\n" +
      "  ue-mcp plugin list\n" +
      "  ue-mcp plugin update [name]\n" +
      "  ue-mcp plugin create <name> [--dir path]",
    );
    process.exit(1);
}
