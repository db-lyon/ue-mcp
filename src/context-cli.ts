#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { dumpYaml } from "./yaml-dump.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";

/**
 * `ue-mcp context [full|lean|micro|status] [project]` - read or set the context
 * seeding strategy in the project's ue-mcp.yml.
 *   full  (default) advertises every action inline
 *   lean  keeps action names, serves descriptions on demand
 *   micro collapses everything behind one gateway tool
 * Restart the MCP client to apply.
 */

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const DIM = "\x1b[2m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const CYAN = "\x1b[36m";
const YELLOW = "\x1b[33m";

const ACTIONS = new Set(["full", "lean", "micro", "status"]);

function printHelp(): void {
  console.log(`
  ${BOLD}${CYAN}ue-mcp context${RESET} - control the context seeding strategy

  ${BOLD}Usage:${RESET}
    ue-mcp context                 show the current strategy
    ue-mcp context status          show the current strategy
    ue-mcp context full            every action inline (largest seed, default)
    ue-mcp context lean            action names visible, descriptions on demand
    ue-mcp context micro           one gateway tool fronts everything (smallest)

  A project path may be passed as the last argument, or an editor named with
  --editor <name-or-path>; otherwise the .uproject in the current directory is
  used. Restart your MCP client (/mcp in Claude Code) after changing the
  strategy.
`);
}

function parseArgs(): { action: string; projectArg?: string; help: boolean } {
  const argv = process.argv.slice(2);
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
  const candidates = [projectArg, process.cwd()].filter(Boolean) as string[];
  for (const c of candidates) {
    try {
      if (c.endsWith(".uproject") && fs.existsSync(c)) return path.dirname(path.resolve(c));
      if (fs.existsSync(c) && fs.statSync(c).isDirectory()) {
        if (fs.readdirSync(c).some((f) => f.endsWith(".uproject"))) return path.resolve(c);
      }
    } catch {
      // ignore and try the next candidate
    }
  }
  return null;
}

function loadYaml(configPath: string): Record<string, unknown> {
  if (!fs.existsSync(configPath)) return {};
  try {
    return (yaml.load(fs.readFileSync(configPath, "utf-8")) as Record<string, unknown>) ?? {};
  } catch (e) {
    console.log(`  ${YELLOW}Warning: ue-mcp.yml is not valid YAML, it will be rewritten${RESET}`);
    return {};
  }
}

function currentStrategy(existing: Record<string, unknown>): "full" | "lean" | "micro" {
  const block = existing["ue-mcp"] as { context?: { strategy?: string } } | undefined;
  const s = block?.context?.strategy;
  return s === "lean" ? "lean" : s === "micro" ? "micro" : "full";
}

function main(): void {
  const { action, projectArg, help } = parseArgs();
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

  const configPath = path.join(projectDir, "ue-mcp.yml");
  const existing = loadYaml(configPath);
  const before = currentStrategy(existing);
  const want = action;

  if (want === "status") {
    const color = before === "full" ? YELLOW : GREEN;
    console.log("");
    console.log(`  ${BOLD}${CYAN}Context strategy${RESET}: ${color}${before}${RESET}`);
    console.log(`  ${DIM}${configPath}${RESET}`);
    console.log(`  ${DIM}Options: full (default) | lean | micro   -> ue-mcp context <tier>${RESET}`);
    console.log("");
    return;
  }

  const block = (existing["ue-mcp"] as Record<string, unknown>) ?? {};
  if (typeof block.version !== "number") block.version = 1;
  if (want === "lean" || want === "micro") {
    block.context = { strategy: want };
  } else {
    // full is the default, so drop the key rather than persist it.
    delete block.context;
  }
  existing["ue-mcp"] = block;
  if (!("tasks" in existing)) existing.tasks = {};
  if (!("flows" in existing)) existing.flows = {};
  fs.writeFileSync(configPath, dumpYaml(existing), "utf-8");

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

main();
