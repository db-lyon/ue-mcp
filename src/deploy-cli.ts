#!/usr/bin/env node
// The `ue-mcp-deploy` package bin, also run by `ue-mcp update` as a sibling
// script. The command itself is cli/deploy-cli.ts.
import { isMainModule, runCli } from "./cli/cli-main.js";
import { run } from "./cli/deploy-cli.js";

export { run };

if (isMainModule(import.meta.url)) void runCli(run, process.argv.slice(2));
