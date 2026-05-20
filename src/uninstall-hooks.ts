#!/usr/bin/env node
/**
 * `npx ue-mcp uninstall-hooks` — manual escape hatch.
 *
 * Reads `installedHooks[]` from .ue-mcp.json in cwd (or a path passed as
 * argv[2]), removes the ue-mcp PostToolUse matcher from each, and clears the
 * registry.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { uninstallAllRegisteredHooks } from "./hook-installer.js";
import { BOLD, CYAN, DIM, GREEN, RED, RESET, fail, info, ok, warn } from "./ui/ansi.js";

function resolveProjectDir(): string | null {
  // index.ts splices the "uninstall-hooks" arg out before this module loads,
  // so a user-supplied project dir lands at argv[2].
  const arg = process.argv[2];
  if (arg) {
    if (!fs.existsSync(arg)) {
      fail(`Path does not exist: ${arg}`);
      return null;
    }
    return path.resolve(arg);
  }
  // Walk up from cwd to find a .ue-mcp.json.
  let dir = process.cwd();
  for (let i = 0; i < 32; i++) {
    if (fs.existsSync(path.join(dir, ".ue-mcp.json"))) return dir;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

function main(): void {
  console.log("");
  console.log(`  ${BOLD}${CYAN}ue-mcp uninstall-hooks${RESET}`);
  console.log("");

  const projectDir = resolveProjectDir();
  if (!projectDir) {
    fail("Could not locate a .ue-mcp.json. Pass the project directory as an argument:");
    console.log(`    ${DIM}npx ue-mcp uninstall-hooks <path-to-ue-project-dir>${RESET}`);
    process.exit(1);
  }

  const result = uninstallAllRegisteredHooks(projectDir);

  if (result.removed.length === 0 && result.skipped.length === 0) {
    info("No installed hooks found in .ue-mcp.json. Nothing to remove.");
  }
  for (const p of result.removed) {
    ok(`Removed hook from ${p}`);
  }
  for (const p of result.skipped) {
    warn(`Already absent: ${p}`);
  }

  console.log("");
  console.log(`  ${BOLD}${GREEN}Done.${RESET}`);
  console.log("");
}

try {
  main();
} catch (e) {
  console.error(
    `\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`,
  );
  process.exit(1);
}
