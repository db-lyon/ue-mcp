#!/usr/bin/env tsx
/**
 * Generate the default ue-mcp.yml the package ships as a reference.
 *
 * It is `buildDefaults(ALL_TOOLS)`, the same object the flow loader starts
 * from at runtime, dumped as YAML: every category's tasks and the built-in
 * flows, in the form a project's ue-mcp.yml uses today.
 *
 * Run: tsx scripts/generate-default-config.ts
 * Output: dist/ue-mcp.default.yml
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { ALL_TOOLS } from "../src/tools.js";
import { buildDefaults } from "../src/flow/loader.js";
import { dumpYaml } from "../src/yaml-dump.js";

const HEADER = [
  "# Auto-generated - do not edit by hand.",
  "# Source: scripts/generate-default-config.ts",
  "",
].join("\n");

/** The default config as YAML text. */
export function generateDefaultConfig(): string {
  return HEADER + dumpYaml({ "ue-mcp": { version: 1 }, ...buildDefaults(ALL_TOOLS) });
}

const isMain = process.argv[1] !== undefined
  && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);

if (isMain) {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
  const outDir = path.join(root, "dist");
  fs.mkdirSync(outDir, { recursive: true });
  const text = generateDefaultConfig();
  const outPath = path.join(outDir, "ue-mcp.default.yml");
  fs.writeFileSync(outPath, text, "utf-8");
  const taskCount = (text.match(/^ {2}[\w-]+\.\w+:/gm) ?? []).length;
  console.log(`[generate] ${outPath} - ${taskCount} tasks`);
}
