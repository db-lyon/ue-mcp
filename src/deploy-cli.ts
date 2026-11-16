#!/usr/bin/env node
// @session independent: The ue-mcp-deploy bin: hands argv to cli/deploy-cli.ts.
// The `ue-mcp-deploy` package bin, also run by `ue-mcp update` as a sibling
// script. The command itself is cli/deploy-cli.ts.
import { isMainModule, runCli } from "./cli/cli-main.js";
import { run, usage } from "./cli/deploy-cli.js";

export { run, usage };

if (isMainModule(import.meta.url)) void runCli(run, process.argv.slice(2), usage);
