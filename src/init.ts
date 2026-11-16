#!/usr/bin/env node
// @session independent: The ue-mcp-init bin: hands argv to cli/init.ts.
// The `ue-mcp-init` package bin. The command itself is cli/init.ts.
import { isMainModule, runCli } from "./cli/cli-main.js";
import { run, usage } from "./cli/init.js";

export { run, usage };

if (isMainModule(import.meta.url)) void runCli(run, process.argv.slice(2), usage);
