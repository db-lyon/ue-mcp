#!/usr/bin/env node
// The `ue-mcp-update` package bin. The command itself is cli/update.ts.
import { isMainModule, runCli } from "./cli/cli-main.js";
import { run } from "./cli/update.js";

export { run };

if (isMainModule(import.meta.url)) void runCli(run, process.argv.slice(2));
