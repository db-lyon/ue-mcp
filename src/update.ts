#!/usr/bin/env node
import { execSync } from "node:child_process";
import { createRequire } from "node:module";

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const DIM = "\x1b[2m";
const CYAN = "\x1b[36m";
const YELLOW = "\x1b[33m";

const ok = (msg: string) => console.log(`  ${GREEN}✓${RESET} ${msg}`);
const fail = (msg: string) => console.log(`  ${RED}✗${RESET} ${msg}`);

function getInstalledVersion(): string {
  const require = createRequire(import.meta.url);
  const pkg = require("../package.json");
  return pkg.version;
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

async function update() {
  const args = process.argv.slice(2);
  const shouldDeploy = args.includes("--deploy");
  const filteredArgs = args.filter((a) => a !== "--deploy");

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

  if (installed === latest) {
    ok("Already up to date");
  } else {
    console.log(`  ${YELLOW}Updating ue-mcp ${installed} -> ${latest}...${RESET}`);
    console.log("");

    const global = isGlobalInstall();
    const cmd = global
      ? `npm install -g ue-mcp@${latest}`
      : `npm install ue-mcp@${latest}`;

    try {
      execSync(cmd, { stdio: "inherit" });
      console.log("");
      ok(`Updated to ${latest}`);
    } catch {
      console.log("");
      fail(`npm install failed. Try manually: ${cmd}`);
      process.exit(1);
    }

    if (shouldDeploy) {
      console.log("");
      console.log(`  ${DIM}Running deploy...${RESET}`);
      console.log("");
      const deployArgs = filteredArgs.length > 0 ? ` ${filteredArgs[0]}` : "";
      try {
        execSync(`npx ue-mcp deploy${deployArgs}`, { stdio: "inherit" });
      } catch {
        fail("Deploy failed after update. Run `ue-mcp deploy` manually.");
        process.exit(1);
      }
    } else {
      console.log("");
      console.log(`  ${DIM}Run \`ue-mcp deploy\` to copy the new plugin sources into your project.${RESET}`);
    }
  }

  console.log("");
}

update().catch((e) => {
  console.error(`\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`);
  process.exit(1);
});
