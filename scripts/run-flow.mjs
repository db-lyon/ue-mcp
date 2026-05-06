#!/usr/bin/env node
/**
 * One-off harness to invoke a named flow against the live editor — same
 * code path the MCP server's flow tool uses, just driven from CLI.
 *
 * Usage: npx tsx scripts/run-flow.mjs <flowName>
 */
import { EditorBridge } from "../src/bridge.ts";
import { ProjectContext } from "../src/project.ts";
import { ALL_TOOLS } from "../src/tools.ts";
import { buildFlowRegistry } from "../src/flow/registry.ts";
import { loadFlowConfig } from "../src/flow/loader.ts";
import { createFlowTool } from "../src/flow/flow-tool.ts";
import path from "node:path";
import { fileURLToPath } from "node:url";

const flowName = process.argv[2];
if (!flowName) {
  console.error("Usage: run-flow.mjs <flowName>");
  process.exit(2);
}

const projectDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "tests", "ue_mcp");
const project = new ProjectContext();
project.setProject(projectDir);

const bridge = new EditorBridge();
await bridge.connect();

const registry = buildFlowRegistry(ALL_TOOLS);
const reload = () => loadFlowConfig(ALL_TOOLS, projectDir).config;
const flowTool = createFlowTool(registry, reload);

const ctx = { bridge, project };
const t0 = Date.now();
const result = await flowTool.handler(ctx, { action: "run", flowName });
const dt = Date.now() - t0;
console.log(JSON.stringify(result, null, 2));
console.log(`\n[done in ${dt}ms]`);
process.exit(0);
