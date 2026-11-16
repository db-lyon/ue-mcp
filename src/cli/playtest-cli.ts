#!/usr/bin/env node
// @session per-session: Scopes the playtest mode with --editor, one editor at a time.
/** `npx ue-mcp playtest mode [manual|automatic|clear] [--editor <name>]`; see cli/mode-cli.ts. */
import { PLAYTEST_MODE } from "../config/modes.js";
import { modeCommand } from "./mode-cli.js";

/** Entry point for `ue-mcp playtest <subcommand>`. */
export const run = modeCommand(PLAYTEST_MODE);
