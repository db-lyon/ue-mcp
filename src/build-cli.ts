#!/usr/bin/env node
import * as path from "node:path";
import { buildProject } from "./editor-control.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { findUProject, isUProjectPath } from "./uproject-path.js";
import { RESET, BOLD, GREEN, RED, CYAN } from "./ui/ansi.js";

function targetUProject(): string | null {
  // --editor names one of the editors this server drives; it wins over the
  // positional, which in turn wins over cwd.
  let arg: string | undefined;
  try {
    const target = takeEditorTarget(process.argv.slice(2));
    arg = target.projectPath ?? target.rest[0];
  } catch (e) {
    console.log(`  ${RED}${e instanceof EditorFlagError ? e.message : String(e)}${RESET}`);
    process.exit(1);
  }
  // A named .uproject is taken as given, so a missing one is reported rather
  // than silently replaced by whatever project cwd holds.
  if (arg && isUProjectPath(arg)) return path.resolve(arg);
  return (arg && findUProject(arg)) || findUProject(process.cwd());
}

async function main() {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Build${RESET}`);
  console.log("");

  const uprojectPath = targetUProject();
  if (!uprojectPath) {
    console.log(`  ${RED}No .uproject found. Run from your project directory or pass the path.${RESET}`);
    process.exit(1);
  }

  const projectName = path.basename(uprojectPath, path.extname(uprojectPath));
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
