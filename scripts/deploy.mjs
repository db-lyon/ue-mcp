#!/usr/bin/env node
// Run the deployer manually to sync plugin/ → tests/ue_mcp/Plugins/
// without starting the full MCP server.
// Loads the TS source through tsx, so a stale or missing dist/ cannot deploy
// with old deployer code.
import { tsImport } from "tsx/esm/api";

const { deploy, deploySummary } = await tsImport("../src/editor/deployer.ts", import.meta.url);
const { ProjectContext } = await tsImport("../src/config/project.ts", import.meta.url);

const proj = new ProjectContext();
const target = process.argv[2] ?? "tests/ue_mcp/ue_mcp.uproject";
proj.setProject(target);
const result = deploy(proj);
console.log(deploySummary(result));
if (result.error) process.exit(1);
