#!/usr/bin/env node
import { execSync } from "node:child_process";
import * as fs from "node:fs";
import { createRequire } from "node:module";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { collectDoctor, formatDoctor } from "./doctor.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { editorOwnsProject, listEditorProcesses } from "./engine-observer.js";
import { distTagForVersion, isPrereleaseVersion, resolveUpdateTarget } from "./version-check.js";

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const DIM = "\x1b[2m";
const CYAN = "\x1b[36m";
const YELLOW = "\x1b[33m";

const ok = (msg: string) => console.log(`  ${GREEN}✓${RESET} ${msg}`);
const fail = (msg: string) => console.log(`  ${RED}✗${RESET} ${msg}`);
const step = (msg: string) => console.log(`  ${DIM}${msg}${RESET}`);

function getInstalledVersion(): string {
  const require = createRequire(import.meta.url);
  return require("../package.json").version;
}

/** The version behind the `latest` dist-tag, which is the stable line. */
function getLatestVersion(): string | null {
  try {
    return execSync("npm view ue-mcp version", { encoding: "utf-8", stdio: ["pipe", "pipe", "pipe"] }).trim();
  } catch {
    return null;
  }
}


function isGlobalInstall(): boolean {
  try {
    const globalRoot = execSync("npm root -g", { encoding: "utf-8", stdio: ["pipe", "pipe", "pipe"] }).trim();
    return import.meta.url.includes(globalRoot.replace(/\\/g, "/"));
  } catch {
    return false;
  }
}

/** The .uproject an argument names (file or directory), else the one in cwd. */
function findUProject(arg: string | undefined): string | null {
  if (arg?.endsWith(".uproject")) return path.resolve(arg);
  const dir = arg && fs.existsSync(arg) && fs.statSync(arg).isDirectory() ? arg : process.cwd();
  const found = fs.readdirSync(dir).filter((f) => f.endsWith(".uproject"));
  return found.length > 0 ? path.resolve(dir, found[0]) : null;
}

async function editorRunningFor(uproject: string): Promise<boolean> {
  try {
    return (await listEditorProcesses()).some((p) => editorOwnsProject(p, uproject));
  } catch {
    return false;
  }
}

/** Run a sibling CLI from THIS package (not npx) so a local shadow can't intercept. */
function runSelfCli(scriptBase: string, projectArg: string | undefined): boolean {
  const selfDir = path.dirname(fileURLToPath(import.meta.url));
  const script = path.join(selfDir, scriptBase);
  const argSuffix = projectArg ? ` "${projectArg}"` : "";
  try {
    execSync(`"${process.execPath}" "${script}"${argSuffix}`, { stdio: "inherit" });
    return true;
  } catch {
    return false;
  }
}

async function update() {
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(process.argv.slice(2));
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    process.exit(1);
  }
  const args = target.rest;
  // The package and the editor plugin are one product, so a bare update carries
  // both. Stopping at npm left editors on an old plugin that answered
  // "Unknown method" for actions the new server advertised.
  const uproject = findUProject(target.projectPath ?? args.find((a) => !a.startsWith("-")));
  const projectArg = uproject ?? undefined;
  const shouldDeploy = !!uproject && !args.includes("--no-deploy");
  // --build and --deploy are the old opt-ins, now the default; still accepted.
  let shouldBuild = shouldDeploy && !args.includes("--no-build");

  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Update${RESET}`);
  console.log("");

  const installed = getInstalledVersion();
  console.log(`  Installed: ${BOLD}${installed}${RESET}`);

  const latest = getLatestVersion();
  if (!latest) {
    fail("Could not reach npm registry. Check your network connection.");
    process.exit(1);
  }
  console.log(`  Latest:    ${BOLD}${latest}${RESET}`);
  console.log("");

  // 1. Update the package this CLI lives in (global or local).
  const wanted = resolveUpdateTarget(installed, latest);
  // Everything below aligns to the version this install ends on, not to the
  // stable line, so a prerelease tester's copies stay on the version they run.
  const aligned = wanted ?? installed;
  if (!wanted) {
    ok("Package already up to date");
    if (isPrereleaseVersion(installed)) {
      const tag = distTagForVersion(installed);
      step(`You are on the ${tag} prerelease channel, ahead of the stable line (${latest}).`);
      step(`Newer prereleases: npm install -g ue-mcp@${tag}. Back to stable: npm install -g ue-mcp@latest.`);
    }
  } else {
    console.log(`  ${YELLOW}Updating ue-mcp ${installed} -> ${wanted}...${RESET}`);
    console.log("");
    const cmd = isGlobalInstall() ? `npm install -g ue-mcp@${wanted}` : `npm install ue-mcp@${wanted}`;
    try {
      execSync(cmd, { stdio: "inherit" });
      console.log("");
      ok(`Updated to ${wanted}`);
    } catch {
      console.log("");
      fail(`npm install failed. Try manually: ${cmd}`);
      process.exit(1);
    }
  }

  // 2. Detect a project-local node_modules/ue-mcp that would shadow the global
  //    install (npx prefers it). If it differs, align it to the version this
  //    install ended on so the server actually runs that version. (#550)
  const preDoctor = collectDoctor(projectArg);
  if (preDoctor.localShadow && preDoctor.localShadow.version !== aligned) {
    const shadowProjectRoot = path.dirname(path.dirname(preDoctor.localShadow.dir));
    console.log("");
    console.log(`  ${YELLOW}Local shadow detected: node_modules/ue-mcp@${preDoctor.localShadow.version} (npx runs this, not the global).${RESET}`);
    step(`Aligning the local copy to ${aligned} in ${shadowProjectRoot}...`);
    try {
      execSync(`npm install ue-mcp@${aligned}`, { stdio: "inherit", cwd: shadowProjectRoot });
      ok(`Local copy aligned to ${aligned}`);
      console.log(`  ${DIM}Cleaner long-term: drop ue-mcp from this project's package.json and pin .mcp.json to \`npx -y ue-mcp@latest\`.${RESET}`);
    } catch {
      fail(`Could not update the local copy. Remove node_modules/ue-mcp manually, or pin .mcp.json to \`npx -y ue-mcp@latest\`.`);
    }
  }

  // 3. Deploy the bridge plugin sources into the project.
  if (!uproject) {
    console.log("");
    console.log(`  ${YELLOW}No .uproject here, so the editor plugin was NOT updated.${RESET}`);
    step("Run `ue-mcp update` from your project directory, or pass the .uproject path.");
  } else if (shouldDeploy) {
    console.log("");
    step("Deploying bridge plugin...");
    console.log("");
    if (!runSelfCli("deploy-cli.js", projectArg)) {
      fail("Deploy failed. Run `ue-mcp deploy` manually.");
      process.exit(1);
    }
  } else {
    console.log("");
    console.log(`  ${YELLOW}Skipped the plugin (--no-deploy). The editor still runs the old one until \`ue-mcp deploy\` and \`ue-mcp build\`.${RESET}`);
  }

  // 4. Rebuild the editor. A running editor holds the plugin DLL, so the build
  //    would fail on a locked file; skip it and say what to run instead.
  if (shouldBuild && uproject && (await editorRunningFor(uproject))) {
    shouldBuild = false;
    console.log("");
    console.log(`  ${YELLOW}The editor is running, so the plugin was deployed but not rebuilt.${RESET}`);
    step("Close the editor, then run `ue-mcp build`.");
  } else if (shouldDeploy && !shouldBuild) {
    console.log("");
    step("Plugin deployed but not rebuilt (--no-build). Run `ue-mcp build` before the next editor launch.");
  }
  if (shouldBuild) {
    console.log("");
    step("Rebuilding the editor (this can take a few minutes)...");
    console.log("");
    if (!runSelfCli("build-cli.js", projectArg)) {
      fail("Build failed. Run `ue-mcp build` manually and check the output.");
      process.exit(1);
    }
  }

  // 5. Show the version table so alignment is visible.
  console.log(formatDoctor(collectDoctor(projectArg)));

  // 6. Remind to relaunch - an update launched through the MCP client cannot
  //    restart the server it was spawned by.
  console.log(`  ${BOLD}Next:${RESET} quit your MCP client and relaunch it so it spawns the updated server.`);
  console.log("");
}

update().catch((e) => {
  console.error(`\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`);
  process.exit(1);
});
