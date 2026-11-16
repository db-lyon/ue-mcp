#!/usr/bin/env node
// @session per-session: Scopes the dialog handling mode with --editor, one editor at a time.
/** `npx ue-mcp dialog mode [interactive|auto|defer|clear] [--editor <name>]`; see cli/mode-cli.ts. */
import { DIALOG_MODE } from "../config/modes.js";
import { modeCommand } from "./mode-cli.js";

/** Entry point for `ue-mcp dialog <subcommand>`. */
export const run = modeCommand(DIALOG_MODE);
