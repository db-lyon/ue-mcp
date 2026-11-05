#!/usr/bin/env node
/**
 * `npx ue-mcp playtest mode [manual|automatic|clear] [--editor <name>]`.
 *
 * The sibling of `ue-mcp dialog mode`: who starts playtest runs, stored per
 * user, or per project with --editor, in ~/.ue-mcp/state.json.
 *
 * Argv layout after index.ts splices "playtest" out:
 *   argv[2] = subcommand (mode)
 *   argv[3] = value (manual | automatic | clear)
 */
import * as path from "node:path";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { getPlaytestModeScopes, getUserStatePath, setPlaytestMode, type PlaytestMode } from "../config/user-state.js";
import { BOLD, CYAN, DIM, RESET, fail, info, ok } from "./ui/ansi.js";
import { projectDirOf } from "../config/uproject-path.js";

function printHelp(): void {
  console.log("");
  console.log(`  ${BOLD}${CYAN}ue-mcp playtest${RESET}`);
  console.log("");
  console.log("  Who starts playtest runs (pie script_run and script_replay).");
  console.log("");
  console.log(`  ${BOLD}mode${RESET}              Print the current playtest mode`);
  console.log(`  ${BOLD}mode${RESET} <mode>       Set the mode (manual | automatic)`);
  console.log(`  ${BOLD}mode clear${RESET}        Clear it and leave the choice to each client`);
  console.log("");
  console.log(`  ${DIM}manual     a person starts playtests; an MCP agent's script_run and script_replay are refused${RESET}`);
  console.log(`  ${DIM}automatic  an agent may start them${RESET}`);
  console.log("");
  console.log(`  ${DIM}Unset, each client decides. --editor <name> sets it for one project only.${RESET}`);
  console.log("");
}

function isMode(value: string): value is PlaytestMode {
  return value === "manual" || value === "automatic";
}

function cmdMode(arg: string | undefined, projectRoot: string | null, editorLabel: string | null): void {
  const scope = editorLabel ? `editor '${editorLabel}'` : "per-user";

  if (arg === undefined) {
    const { project, user } = getPlaytestModeScopes(projectRoot);
    console.log("");
    console.log(`  ${BOLD}effective:${RESET} ${project ?? user ?? "not set (each client decides)"}`);
    if (projectRoot) {
      console.log(`  ${DIM}${scope}: ${project ?? "(not set; falls back to the per-user preference)"}${RESET}`);
    }
    console.log(`  ${DIM}preference (~/.ue-mcp/state.json): ${user ?? "(not set)"}${RESET}`);
    console.log("");
    console.log(`  ${DIM}Set with: npx ue-mcp playtest mode <manual|automatic> [--editor <name>]${RESET}`);
    console.log(`  ${DIM}Clear: npx ue-mcp playtest mode clear [--editor <name>]${RESET}`);
    console.log("");
    return;
  }

  const value = arg.trim().toLowerCase();
  if (value === "clear" || value === "default" || value === "unset") {
    setPlaytestMode(undefined, projectRoot);
    ok(
      projectRoot
        ? `Cleared the playtest mode for ${scope}. It falls back to the per-user preference.`
        : "Cleared the playtest mode preference. Each client decides again.",
    );
    info(getUserStatePath());
    return;
  }
  if (!isMode(value)) {
    fail(`Unknown mode "${arg}". Allowed: manual, automatic, clear.`);
    process.exit(1);
  }
  setPlaytestMode(value, projectRoot);
  ok(
    projectRoot
      ? `Playtest mode for ${scope} set to "${value}" (stored per project in ~/.ue-mcp/state.json).`
      : `Playtest mode set to "${value}" (per-user, stored in ~/.ue-mcp/state.json).`,
  );
}

async function main(argv: string[]): Promise<void> {
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(argv);
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    process.exit(1);
  }
  const projectRoot = target.projectPath ? projectDirOf(target.projectPath) : null;
  const editorLabel = target.projectPath ? path.basename(target.projectPath, path.extname(target.projectPath)) : null;
  const sub = target.rest[0];
  switch (sub) {
    case "mode":
      cmdMode(target.rest[1], projectRoot, editorLabel);
      return;
    case undefined:
    case "help":
    case "--help":
    case "-h":
      printHelp();
      return;
    default:
      fail(`Unknown subcommand: playtest ${sub}`);
      printHelp();
      process.exit(1);
  }
}

/** Entry point for `ue-mcp playtest <subcommand>`. */
export async function run(argv: string[]): Promise<number | void> {
  try {
    await main(argv);
  } catch (e) {
    console.error(`[ue-mcp] playtest failed: ${e instanceof Error ? e.message : e}`);
    return 1;
  }
}
