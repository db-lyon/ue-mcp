#!/usr/bin/env node
import { execSync } from "node:child_process";
import { createRequire } from "node:module";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { collectDoctor, formatDoctor } from "./doctor.js";

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
  const args = process.argv.slice(2);
  const shouldBuild = args.includes("--build");
  const shouldDeploy = shouldBuild || args.includes("--deploy");
  const projectArg = args.find((a) => !a.startsWith("-"));

  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Update${shouldBuild ? " --build" : ""}${RESET}`);
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
  if (installed === latest) {
    ok("Package already up to date");
  } else {
    console.log(`  ${YELLOW}Updating ue-mcp ${installed} -> ${latest}...${RESET}`);
    console.log("");
    const cmd = isGlobalInstall() ? `npm install -g ue-mcp@${latest}` : `npm install ue-mcp@${latest}`;
    try {
      execSync(cmd, { stdio: "inherit" });
      console.log("");
      ok(`Updated to ${latest}`);
    } catch {
      console.log("");
      fail(`npm install failed. Try manually: ${cmd}`);
      process.exit(1);
    }
  }

  // 2. Detect a project-local node_modules/ue-mcp that would shadow the global
  //    install (npx prefers it). If it is behind, align it to latest so the
  //    server actually runs the new version. (#550)
  const preDoctor = collectDoctor(projectArg);
  if (preDoctor.localShadow && preDoctor.localShadow.version !== latest) {
    const shadowProjectRoot = path.dirname(path.dirname(preDoctor.localShadow.dir));
    console.log("");
    console.log(`  ${YELLOW}Local shadow detected: node_modules/ue-mcp@${preDoctor.localShadow.version} (npx runs this, not the global).${RESET}`);
    step(`Aligning the local copy to ${latest} in ${shadowProjectRoot}...`);
    try {
      execSync(`npm install ue-mcp@${latest}`, { stdio: "inherit", cwd: shadowProjectRoot });
      ok(`Local copy aligned to ${latest}`);
      console.log(`  ${DIM}Cleaner long-term: drop ue-mcp from this project's package.json and pin .mcp.json to \`npx -y ue-mcp@latest\`.${RESET}`);
    } catch {
      fail(`Could not update the local copy. Remove node_modules/ue-mcp manually, or pin .mcp.json to \`npx -y ue-mcp@latest\`.`);
    }
  }

  // 3. Deploy the bridge plugin sources into the project.
  if (shouldDeploy) {
    console.log("");
    step("Deploying bridge plugin...");
    console.log("");
    if (!runSelfCli("deploy-cli.js", projectArg)) {
      fail("Deploy failed. Run `ue-mcp deploy` manually.");
      process.exit(1);
    }
  } else {
    console.log("");
    step("Run `ue-mcp deploy` to copy the new plugin sources into your project (or re-run with --deploy / --build).");
  }

  // 4. Rebuild the editor (gated behind --build).
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
