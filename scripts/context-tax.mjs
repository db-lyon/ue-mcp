#!/usr/bin/env node
// Measure the context tax of the two seeding strategies: exactly what the MCP
// server injects at session start (the initialize `instructions` field + the
// full tools/list payload of names, descriptions, and inputSchemas).
//
// It talks to the real built server over stdio in each mode, so the numbers
// reflect the SDK's actual serialization rather than an approximation.
//
// Usage:
//   npx tsc                                   # build dist first
//   node scripts/context-tax.mjs [project]    # chars + ~token estimate
//   ANTHROPIC_API_KEY=sk-... node scripts/context-tax.mjs [project]   # exact tokens
//
// Run with the editor connected to include the Epic-enriched surface; run
// without it to measure the base 22-tool surface. Keep it the same for both
// modes so the comparison is apples to apples.

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { fileURLToPath } from "node:url";
import * as path from "node:path";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const PROJECT = process.argv[2] ?? path.join(ROOT, "tests", "ue_mcp", "ue_mcp.uproject");
const SERVER = path.join(ROOT, "dist", "index.js");

async function seed(strategy) {
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [SERVER, PROJECT],
    env: { ...process.env, UE_MCP_CONTEXT_STRATEGY: strategy },
    stderr: "ignore",
  });
  const client = new Client({ name: "context-tax", version: "0" });
  await client.connect(transport);

  const instructions = client.getInstructions?.() ?? "";
  const { tools } = await client.listTools();
  await client.close();

  const payload = JSON.stringify({ instructions, tools });
  return {
    toolCount: tools.length,
    instructionsChars: instructions.length,
    toolsChars: JSON.stringify(tools).length,
    totalChars: payload.length,
    payload,
  };
}

// Exact Claude tokens when a key is present; otherwise a chars/4 estimate.
async function countTokens(text) {
  const key = process.env.ANTHROPIC_API_KEY;
  if (!key) return { tokens: Math.round(text.length / 4), exact: false };
  const res = await fetch("https://api.anthropic.com/v1/messages/count_tokens", {
    method: "POST",
    headers: {
      "x-api-key": key,
      "anthropic-version": "2023-06-01",
      "content-type": "application/json",
    },
    body: JSON.stringify({
      model: process.env.ANTHROPIC_MODEL ?? "claude-opus-4-8",
      messages: [{ role: "user", content: text }],
    }),
  });
  if (!res.ok) {
    console.error(`count_tokens API error ${res.status}: ${await res.text()}`);
    return { tokens: Math.round(text.length / 4), exact: false };
  }
  const json = await res.json();
  return { tokens: json.input_tokens, exact: true };
}

const STRATEGIES = ["full", "lean", "micro"];
const rows = [];
for (const s of STRATEGIES) {
  const seeded = await seed(s);
  const t = await countTokens(seeded.payload);
  rows.push({ strategy: s, ...seeded, tokens: t.tokens, exact: t.exact });
}

const pad = (v, w) => String(v).padStart(w);
const pct = (a, b) => (a === 0 ? "0.0" : (100 * (1 - b / a)).toFixed(1));
const full = rows[0];
const exact = rows.every((r) => r.exact);

console.log("");
console.log(`  Context tax  (project: ${path.basename(PROJECT)}, tokens ${exact ? "exact via count_tokens" : "~ chars/4 estimate, set ANTHROPIC_API_KEY for exact"})`);
console.log("");
console.log(`  mode    tools   instr.chars   tools.chars   total.chars   tokens   vs full`);
for (const r of rows) {
  console.log(
    `  ${r.strategy.padEnd(6)}  ${pad(r.toolCount, 5)}   ${pad(r.instructionsChars, 11)}   ${pad(r.toolsChars, 11)}   ${pad(r.totalChars, 11)}   ${pad(r.tokens, 6)}   ${r.strategy === "full" ? "-" : `-${pct(full.tokens, r.tokens)}%`}`,
  );
}
console.log("");
console.log(`  note: lean/micro move cost to on-demand describe/call round-trips; add those for a full-session total.`);
console.log("");
