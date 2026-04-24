#!/usr/bin/env node
// Counts unique MCP handler names registered in the C++ plugin.
// Used by CI / docs to keep action counts honest.
//
// Run: node scripts/count-handlers.mjs
// Exits 0 and prints "<unique> unique (<total> registrations, <aliases> aliases)".

import { readFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const handlersDir = join(here, "..", "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers");

const re = /Registry\.RegisterHandler(?:WithTimeout)?\(\s*TEXT\("([^"]+)"\)/g;

const names = [];
for (const entry of readdirSync(handlersDir)) {
  if (!entry.endsWith(".cpp")) continue;
  const body = readFileSync(join(handlersDir, entry), "utf8");
  for (const m of body.matchAll(re)) names.push(m[1]);
}

const unique = new Set(names);
console.log(`${unique.size} unique (${names.length} registrations, ${names.length - unique.size} aliases)`);
