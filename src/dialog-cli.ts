#!/usr/bin/env node
/**
 * `npx ue-mcp dialog mode [interactive|auto|defer|default] [--editor <name>]`.
 *
 * The sibling of `npx ue-mcp feedback mode`, and it exists for the same reason:
 * the mode is a per-user, per-device preference stored in
 * `~/.ue-mcp/state.json`, and that file is written by ue-mcp commands rather
 * than edited by hand. A setting whose only documented path was "open the state
 * file and type into it" was a setting nobody could safely use.
 *
 * Argv layout after index.ts splices "dialog" out:
 *   argv[2] = subcommand (mode)
 *   argv[3] = value (interactive | auto | defer | default)
 */

import * as path from "node:path";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import {
  getDialogModeScopes,
  setDialogMode,
  getUserStatePath,
  type DialogMode,
} from "./user-state.js";
import { BOLD, CYAN, DIM, RESET, fail, info, ok, warn } from "./ui/ansi.js";

function printHelp(): void {
  console.log("");
  console.log(`  ${BOLD}${CYAN}ue-mcp dialog${RESET}`);
  console.log("");
  console.log("  How a modal dialog blocking the Unreal Editor is handled.");
  console.log("");
  console.log(`  ${BOLD}mode${RESET}              Print the current dialog handling mode`);
  console.log(`  ${BOLD}mode${RESET} <mode>       Set the mode (interactive | auto | defer)`);
  console.log(`  ${BOLD}mode default${RESET}      Clear the preference and go back to the default`);
  console.log("");
  console.log(`  ${DIM}interactive  the dialog is put to you in an MCP elicitation form; only the button you pick is pressed${RESET}`);
  console.log(`  ${DIM}auto         the dialog is handed back whole and the agent answers it; the server presses nothing${RESET}`);
  console.log(`  ${DIM}defer        nothing is pressed and nothing is asked; you answer it in the editor yourself${RESET}`);
  console.log("");
  console.log(`  ${DIM}Default: interactive when your MCP client advertises elicitation, otherwise defer. Never auto.${RESET}`);
  console.log("");
}

function isMode(value: string): value is DialogMode {
  return value === "interactive" || value === "auto" || value === "defer";
}

/** The project root a --editor value resolved to, for per-project preferences. */
function projectRootOf(projectPath?: string): string | null {
  if (!projectPath) return null;
  return projectPath.toLowerCase().endsWith(".uproject")
    ? path.dirname(path.resolve(projectPath))
    : path.resolve(projectPath);
}

/**
 * Print or set the dialog handling mode.
 *
 * With `--editor` the preference is scoped to that project, the per-session
 * equivalent of UE_MCP_DIALOG_MODE: one editor can be a long unattended run
 * while the user sits in front of another, and a single per-device answer
 * cannot describe both.
 */
function cmdMode(arg: string | undefined, projectRoot: string | null, editorLabel: string | null): void {
  const scope = editorLabel ? `editor '${editorLabel}'` : "per-user";
  const env = (process.env.UE_MCP_DIALOG_MODE ?? "").trim().toLowerCase();

  if (arg === undefined) {
    // What each scope HOLDS, not what applies. getDialogMode falls back to the
    // user preference, so printing its result as the project's own value shows
    // a value the project never set and makes "(not set)" unreachable for
    // anyone who has ever set a per-user default.
    const { project: scoped, user: pref } = getDialogModeScopes(projectRoot);

    console.log("");
    if (isMode(env)) {
      console.log(`  ${BOLD}effective:${RESET} ${env} ${DIM}(UE_MCP_DIALOG_MODE)${RESET}`);
    } else if (scoped ?? pref) {
      console.log(`  ${BOLD}effective:${RESET} ${scoped ?? pref}`);
    } else {
      console.log(`  ${BOLD}effective:${RESET} interactive when the connected MCP client advertises elicitation, otherwise defer`);
    }
    if (projectRoot) {
      console.log(`  ${DIM}${scope}: ${scoped ?? "(not set; falls back to the per-user preference)"}${RESET}`);
    }

    console.log(`  ${DIM}preference (~/.ue-mcp/state.json): ${pref ?? "(not set; the default decides)"}${RESET}`);
    if (env !== "" && !isMode(env)) {
      warn(`UE_MCP_DIALOG_MODE="${env}" names no mode and is ignored. Allowed: interactive, auto, defer.`);
    }
    console.log("");
    console.log(`  ${DIM}Set with: npx ue-mcp dialog mode <interactive|auto|defer> [--editor <name>]${RESET}`);
    console.log(`  ${DIM}Clear preference: npx ue-mcp dialog mode default${RESET}`);
    console.log("");
    return;
  }

  const value = arg.trim().toLowerCase();

  if (value === "default" || value === "clear" || value === "unset") {
    setDialogMode(undefined, projectRoot);
    ok(
      projectRoot
        ? `Cleared the mode for ${scope}. It falls back to the per-user preference.`
        : "Cleared the mode preference. The default decides again: interactive with elicitation, defer without.",
    );
    info(getUserStatePath());
    return;
  }

  if (!isMode(value)) {
    fail(`Unknown mode "${arg}". Allowed: interactive, auto, defer, default.`);
    process.exit(1);
  }

  setDialogMode(value, projectRoot);
  ok(
    projectRoot
      ? `Dialog mode for ${scope} set to "${value}" (stored per project in ~/.ue-mcp/state.json).`
      : `Dialog mode set to "${value}" (per-user, stored in ~/.ue-mcp/state.json).`,
  );
  if (value === "auto") {
    warn(
      "In auto mode a blocking dialog is handed to the agent to answer, including save prompts. " +
        "Nothing is pressed by the server, but the person at the keyboard is no longer the one deciding.",
    );
  }
  if (process.env.UE_MCP_DIALOG_MODE) {
    warn(`UE_MCP_DIALOG_MODE=${process.env.UE_MCP_DIALOG_MODE} is set in your env and will override this preference for processes started from that shell.`);
  }
}

async function main(): Promise<void> {
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(process.argv.slice(2));
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    process.exit(1);
  }
  const projectRoot = projectRootOf(target.projectPath);
  const editorLabel = target.projectPath
    ? path.basename(target.projectPath, path.extname(target.projectPath))
    : null;
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
      fail(`Unknown subcommand: dialog ${sub}`);
      printHelp();
      process.exit(1);
  }
}

main().catch((e) => {
  console.error(`[ue-mcp] dialog failed: ${e instanceof Error ? e.message : e}`);
  process.exit(1);
});
