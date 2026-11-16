#!/usr/bin/env node
// @session per-session: Builds one project, chosen with --editor.
import * as path from "node:path";
import { buildProject } from "../editor/editor-build.js";
import { selectProjectOrExit } from "./editor-flag.js";
import { findUProject, isUProjectPath } from "../config/uproject-path.js";
import { RESET, BOLD, GREEN, RED, CYAN } from "../terminal/ansi.js";
import type { CliUsage } from "./cli-main.js";

function targetUProject(argv: string[]): string | null {
  const arg = selectProjectOrExit(argv, { positional: true }).projectPath;
  // A named .uproject is taken as given, so a missing one is reported rather
  // than silently replaced by whatever project cwd holds.
  if (arg && isUProjectPath(arg)) return path.resolve(arg);
  return (arg && findUProject(arg)) || findUProject(process.cwd());
}

async function main(argv: string[]) {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Build${RESET}`);
  console.log("");

  const uprojectPath = targetUProject(argv);
  if (!uprojectPath) {
    console.log(`  ${RED}No .uproject found. Run from your project directory or pass the path.${RESET}`);
    process.exit(1);
  }

  const projectName = path.basename(uprojectPath, path.extname(uprojectPath));
  console.log(`  Project: ${GREEN}${projectName}${RESET}`);
  console.log(`  Path:    ${uprojectPath}`);
  console.log("");

  const result = await buildProject(uprojectPath, { onOutput: (text) => process.stdout.write(text) });

  console.log("");
  if (result.success) {
    console.log(`  ${GREEN}${BOLD}Build succeeded${RESET}`);
  } else {
    console.log(`  ${RED}${BOLD}${result.message}${RESET}`);
    process.exit(1);
  }
}

export const usage: CliUsage = {
  usage: "ue-mcp build [project] [flags]",
  summary: "Build the project's editor target.",
  strict: true,
  flags: [{ name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives" }],
};

/** Entry point for `ue-mcp build [project] [--editor <name-or-path>]`. */
export async function run(argv: string[]): Promise<number | void> {
  try {
    await main(argv);
  } catch (e) {
    console.error(`\n  ${RED}Unexpected error: ${e instanceof Error ? e.message : String(e)}${RESET}`);
    return 1;
  }
}
