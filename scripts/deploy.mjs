#!/usr/bin/env node
// Run the deployer manually to sync plugin/ → tests/ue_mcp/Plugins/
// without starting the full MCP server.
import { deploy, deploySummary } from "../dist/editor/deployer.js";
import { ProjectContext } from "../dist/config/project.js";

const proj = new ProjectContext();
const target = process.argv[2] ?? "tests/ue_mcp/ue_mcp.uproject";
proj.setProject(target);
const result = deploy(proj);
console.log(deploySummary(result));
if (result.error) process.exit(1);
