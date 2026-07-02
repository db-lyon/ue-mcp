#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { dumpYaml } from "./yaml-dump.js";

/**
 * `ue-mcp context [lean|full|status] [project]` - read or set the context
 * seeding strategy in the project's ue-mcp.yml. `lean` trims the MCP init
 * payload and serves the action catalog on demand; `full` (default) advertises
 * every action inline. Restart the MCP client to apply.
 */

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const DIM = "\x1b[2m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const CYAN = "\x1b[36m";
const YELLOW = "\x1b[33m";

const ACTIONS = new Set(["lean", "full", "status", "on", "off"]);

function printHelp(): void {
  console.log(`
  ${BOLD}${CYAN}ue-mcp context${RESET} - control the context seeding strategy

  ${BOLD}Usage:${RESET}
    ue-mcp context                 show the current strategy
    ue-mcp context status          show the current strategy
    ue-mcp context lean            switch to lean (catalog served on demand)
    ue-mcp context full            switch to full (every action inline, default)

  A project path may be passed as the last argument; otherwise the .uproject in
  the current directory is used. Restart your MCP client (/mcp in Claude Code)
  after changing the strategy.
`);
}

function parseArgs(): { action: string; projectArg?: string; help: boolean } {
  const args = process.argv.slice(2);
  if (args.some((a) => a === "-h" || a === "--help" || a === "help")) {
    return { action: "status", help: true };
  }
  const positional = args.filter((a) => !a.startsWith("-"));
  let action: string | undefined;
  let projectArg: string | undefined;
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

function currentStrategy(existing: Record<string, unknown>): "full" | "lean" {
  const block = existing["ue-mcp"] as { context?: { strategy?: string } } | undefined;
  return block?.context?.strategy === "lean" ? "lean" : "full";
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

  // Normalise on/off aliases.
  const want = action === "on" ? "lean" : action === "off" ? "full" : action;

  if (want === "status") {
    const color = before === "lean" ? GREEN : YELLOW;
    console.log("");
    console.log(`  ${BOLD}${CYAN}Context strategy${RESET}: ${color}${before}${RESET}`);
    console.log(`  ${DIM}${configPath}${RESET}`);
    console.log(`  ${DIM}Switch with: ue-mcp context ${before === "lean" ? "full" : "lean"}${RESET}`);
    console.log("");
    return;
  }

  const block = (existing["ue-mcp"] as Record<string, unknown>) ?? {};
  if (typeof block.version !== "number") block.version = 1;
  if (want === "lean") {
    block.context = { strategy: "lean" };
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
