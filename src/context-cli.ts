#!/usr/bin/env node
import * as path from "node:path";
import { projectConfigPath, readConfigDoc, writeConfigDoc } from "./config/ue-mcp-config.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { findUProject } from "./config/uproject-path.js";
import { RESET, BOLD, DIM, GREEN, RED, CYAN, YELLOW } from "./ui/ansi.js";

/**
 * `ue-mcp context [full|lean|micro|status] [project]` - read or set the context
 * seeding strategy in the project's ue-mcp.yml.
 *   micro (default) collapses everything behind one gateway tool
 *   lean  keeps the category tools and action names, signatures on demand
 *   full  advertises every action's signature inline
 * Restart the MCP client to apply.
 */

const ACTIONS = new Set(["full", "lean", "micro", "status"]);

function printHelp(): void {
  console.log(`
  ${BOLD}${CYAN}ue-mcp context${RESET} - control the context seeding strategy

  ${BOLD}Usage:${RESET}
    ue-mcp context                 show the current strategy
    ue-mcp context status          show the current strategy
    ue-mcp context micro           one gateway tool fronts everything (smallest, default)
    ue-mcp context lean            action names visible, signatures on demand
    ue-mcp context full            every action's signature inline (largest seed)

  A project path may be passed as the last argument, or an editor named with
  --editor <name-or-path>; otherwise the .uproject in the current directory is
  used. Restart your MCP client (/mcp in Claude Code) after changing the
  strategy.
`);
}

function parseArgs(argv: string[]): { action: string; projectArg?: string; help: boolean } {
  if (argv.some((a) => a === "-h" || a === "--help" || a === "help")) {
    return { action: "status", help: true };
  }
  // Flags were dropped wholesale here, so --editor would have been silently
  // discarded and the command would have retargeted cwd. Take it first, then
  // keep the original positional-only behaviour for everything else.
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(argv);
  } catch (e) {
    console.log(`
  ${RED}${e instanceof EditorFlagError ? e.message : String(e)}${RESET}
`);
    process.exit(1);
  }
  const positional = target.rest.filter((a) => !a.startsWith("-"));
  let action: string | undefined;
  let projectArg: string | undefined = target.projectPath;
  for (const a of positional) {
    if (!action && ACTIONS.has(a.toLowerCase())) action = a.toLowerCase();
    else if (!projectArg) projectArg = a;
  }
  return { action: action ?? "status", projectArg, help: false };
}

function findProjectDir(projectArg?: string): string | null {
  const uproject = (projectArg && findUProject(projectArg)) || findUProject(process.cwd());
  return uproject ? path.dirname(uproject) : null;
}

function loadYaml(configPath: string): Record<string, unknown> {
  return readConfigDoc(configPath, () => {
    console.log(`  ${YELLOW}Warning: ue-mcp.yml is not valid YAML, it will be rewritten${RESET}`);
  });
}

function currentStrategy(existing: Record<string, unknown>): "full" | "lean" | "micro" {
  const block = existing["ue-mcp"] as { context?: { strategy?: string } } | undefined;
  const s = block?.context?.strategy;
  return s === "lean" ? "lean" : s === "full" ? "full" : "micro";
}

function main(argv: string[]): void {
  const { action, projectArg, help } = parseArgs(argv);
  if (help) {
    printHelp();
    return;
  }

  const projectDir = findProjectDir(projectArg);
  if (!projectDir) {
    console.log(`\n  ${RED}No .uproject found.${RESET} Run from your project directory or pass the path:`);
    console.log(`  ${DIM}ue-mcp context ${action === "status" ? "" : action + " "}<path-to-project>${RESET}\n`);
    process.exit(1);
  }

  const configPath = projectConfigPath(projectDir);
  const existing = loadYaml(configPath);
  const before = currentStrategy(existing);
  const want = action;

  if (want === "status") {
    const color = before === "micro" ? YELLOW : GREEN;
    console.log("");
    console.log(`  ${BOLD}${CYAN}Context strategy${RESET}: ${color}${before}${RESET}`);
    console.log(`  ${DIM}${configPath}${RESET}`);
    console.log(`  ${DIM}Options: micro (default) | lean | full   -> ue-mcp context <strategy>${RESET}`);
    console.log("");
    return;
  }

  const block = (existing["ue-mcp"] as Record<string, unknown>) ?? {};
  if (typeof block.version !== "number") block.version = 1;
  if (want === "lean" || want === "full") {
    block.context = { strategy: want };
  } else {
    // micro is the default, so drop the key rather than persist it.
    delete block.context;
  }
  existing["ue-mcp"] = block;
  if (!("tasks" in existing)) existing.tasks = {};
  if (!("flows" in existing)) existing.flows = {};
  writeConfigDoc(configPath, existing);

  console.log("");
  if (before === want) {
    console.log(`  ${GREEN}Context strategy already ${BOLD}${want}${RESET}`);
  } else {
    console.log(`  ${GREEN}${BOLD}Context strategy: ${before} -> ${want}${RESET}`);
  }
  console.log(`  ${DIM}${configPath}${RESET}`);
  console.log(`  ${DIM}Restart your MCP client (/mcp in Claude Code) to apply.${RESET}`);
  console.log("");
}

/** Entry point for `ue-mcp context [full|lean|micro|status] [project]`. */
export async function run(argv: string[]): Promise<number | void> {
  main(argv);
}
