#!/usr/bin/env node
// @session independent: The ue-mcp-update bin: hands argv to cli/update.ts.
// The `ue-mcp-update` package bin. The command itself is cli/update.ts.
import { isMainModule, runCli } from "./cli/cli-main.js";
import { run, usage } from "./cli/update.js";

export { run, usage };

if (isMainModule(import.meta.url)) void runCli(run, process.argv.slice(2), usage);
