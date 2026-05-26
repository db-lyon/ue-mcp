#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import { buildProject } from "./editor-control.js";

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const CYAN = "\x1b[36m";

function findUProject(): string | null {
  const arg = process.argv[2];
  if (arg && arg.endsWith(".uproject")) return path.resolve(arg);
  if (arg && fs.existsSync(arg) && fs.statSync(arg).isDirectory()) {
    const found = fs.readdirSync(arg).filter((f) => f.endsWith(".uproject"));
    if (found.length > 0) return path.resolve(arg, found[0]);
  }

  const cwd = process.cwd();
  const found = fs.readdirSync(cwd).filter((f) => f.endsWith(".uproject"));
  if (found.length > 0) return path.join(cwd, found[0]);

  return null;
}

async function main() {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Build${RESET}`);
  console.log("");

  const uprojectPath = findUProject();
  if (!uprojectPath) {
    console.log(`  ${RED}No .uproject found. Run from your project directory or pass the path.${RESET}`);
    process.exit(1);
  }

  const projectName = path.basename(uprojectPath, ".uproject");
  console.log(`  Project: ${GREEN}${projectName}${RESET}`);
  console.log(`  Path:    ${uprojectPath}`);
  console.log("");

  const result = await buildProject(uprojectPath);

  console.log("");
  if (result.success) {
    console.log(`  ${GREEN}${BOLD}Build succeeded${RESET}`);
  } else {
    console.log(`  ${RED}${BOLD}${result.message}${RESET}`);
    process.exit(1);
  }
}

main().catch((e) => {
  console.error(`\n  ${RED}Unexpected error: ${e instanceof Error ? e.message : String(e)}${RESET}`);
  process.exit(1);
});
