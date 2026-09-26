// Measure the context tax: exactly what the MCP server injects at session
// start (the initialize `instructions` field + the full tools/list payload of
// names, descriptions and inputSchemas) for each strategy, plus the two
// discovery answers micro leans on: a category `describe` of the largest
// category and a default `search` (#1172).
//
// It talks to the real server over stdio, so the numbers reflect the SDK's
// actual serialization rather than an approximation. The run is hermetic by
// default: a throwaway project, no editor (UE_MCP_PORT=1), no inherited
// UE_MCP_* setting and no user config. The Epic catalog is declared in the
// package, so it is present either way.
//
// Usage:
//   node scripts/context-tax.mjs            table, chars/4 token estimate
//   node scripts/context-tax.mjs --json     the same as JSON
//   node scripts/context-tax.mjs --check    exit 1 when a number is over budget
//   node scripts/context-tax.mjs --stamp    rewrite the <!-- tax:* --> markers in the docs
//   ANTHROPIC_API_KEY=sk-... node scripts/context-tax.mjs   exact tokens via count_tokens
//
// The server is run from src/ through tsx, so no build is needed and the
// measurement can never come from a stale dist/.

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { fileURLToPath } from "node:url";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** Token budgets from #1172. A number over its budget fails the release gate. */
export const BUDGETS = {
  micro: 5_000,
  lean: 30_000,
  full: 60_000,
  describe: 2_000,
  search: 1_000,
};

export const STRATEGIES = ["full", "lean", "micro"];

/** Queries the default-search budget is measured over; the largest answer counts. */
export const SEARCH_QUERIES = ["spawn actor", "blueprint variable", "material parameter", "rotate clockwise", "screenshot"];

/** Files carrying `<!-- tax:<key> -->...<!-- /tax -->` markers. */
export const TAX_MARKER_FILES = ["README.md", "docs/configuration.md", "docs/config-file.md", "docs/architecture.md"];

/** How far a stamped number may sit from the measurement before it is stale. */
export const STAMP_TOLERANCE = 0.1;

function sandboxProject() {
  const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-context-tax-"));
  const dir = path.join(sandbox, "TaxProject");
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, "TaxProject.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6", Category: "", Description: "" }, null, 2));
  return { sandbox, uproject };
}

function hermeticEnv(sandbox, strategy) {
  const env = {};
  for (const [key, value] of Object.entries(process.env)) {
    if (!key.startsWith("UE_MCP_") && value !== undefined) env[key] = value;
  }
  Object.assign(env, {
    HOME: sandbox,
    USERPROFILE: sandbox,
    UE_MCP_HOST: "127.0.0.1",
    UE_MCP_PORT: "1",
    UE_MCP_GLOBAL_CONFIG: path.join(sandbox, "global-config.yml"),
    UE_MCP_USER_STATE: path.join(sandbox, "state.json"),
    UE_MCP_AUTH_DIR: path.join(sandbox, "auth"),
    UE_MCP_DISABLE_UPDATE_CHECK: "1",
    UE_MCP_LOG_LEVEL: "error",
    UE_MCP_CONTEXT_STRATEGY: strategy,
  });
  return env;
}

/** The text a client is handed for one tool call. */
function callText(result) {
  return (result.content ?? []).map((block) => block.text ?? "").join("\n");
}

async function withServer(strategy, fn) {
  const { sandbox, uproject } = sandboxProject();
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: ["--import", "tsx", path.join(ROOT, "src", "index.ts"), uproject],
    cwd: ROOT,
    env: hermeticEnv(sandbox, strategy),
    stderr: "ignore",
  });
  const client = new Client({ name: "context-tax", version: "0" });
  try {
    await client.connect(transport);
    return await fn(client);
  } finally {
    await client.close().catch(() => undefined);
    await transport.close().catch(() => undefined);
    fs.rmSync(sandbox, { recursive: true, force: true });
  }
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

/**
 * Seed every strategy, and in micro ask the two discovery questions.
 * Returns one row per measured thing, keyed like BUDGETS.
 */
export async function measureContextTax() {
  /** @type {Record<string, { what: string, chars: number, tokens: number, exact: boolean, [extra: string]: unknown }>} */
  const rows = {};
  for (const strategy of STRATEGIES) {
    const seeded = await withServer(strategy, async (client) => {
      const instructions = client.getInstructions?.() ?? "";
      const { tools } = await client.listTools();
      const out = {
        toolCount: tools.length,
        instructionsChars: instructions.length,
        toolsChars: JSON.stringify(tools).length,
        payload: JSON.stringify({ instructions, tools }),
      };
      if (strategy === "micro") {
        const listed = JSON.parse(callText(await client.callTool({ name: "tools", arguments: { action: "list_categories" } })));
        // Largest by action count, asked of the server rather than assumed.
        let largest = { category: "", count: -1, text: "" };
        for (const { category } of listed.categories) {
          const text = callText(await client.callTool({ name: "tools", arguments: { action: "describe", category } }));
          const count = JSON.parse(text).count ?? 0;
          if (count > largest.count) largest = { category, count, text };
        }
        out.describe = largest;
        let widest = { query: "", text: "" };
        for (const query of SEARCH_QUERIES) {
          const text = callText(await client.callTool({ name: "tools", arguments: { action: "search", query } }));
          if (text.length > widest.text.length) widest = { query, text };
        }
        out.search = widest;
      }
      return out;
    });
    const t = await countTokens(seeded.payload);
    rows[strategy] = {
      what: `${strategy} seed`,
      toolCount: seeded.toolCount,
      instructionsChars: seeded.instructionsChars,
      toolsChars: seeded.toolsChars,
      chars: seeded.payload.length,
      tokens: t.tokens,
      exact: t.exact,
    };
    if (seeded.describe) {
      const d = await countTokens(seeded.describe.text);
      rows.describe = { what: `micro describe ${seeded.describe.category} (${seeded.describe.count} actions)`, chars: seeded.describe.text.length, tokens: d.tokens, exact: d.exact };
    }
    if (seeded.search) {
      const s = await countTokens(seeded.search.text);
      rows.search = { what: `micro search "${seeded.search.query}"`, chars: seeded.search.text.length, tokens: s.tokens, exact: s.exact };
    }
  }
  return rows;
}

/** One line per number over its budget. */
export function budgetProblems(rows, budgets = BUDGETS) {
  const problems = [];
  for (const [key, budget] of Object.entries(budgets)) {
    const row = rows[key];
    if (!row) problems.push(`${key}: not measured`);
    else if (row.tokens > budget) problems.push(`${row.what}: ${row.tokens} tokens, over its ${budget} budget`);
  }
  return problems;
}

/** A token count as the docs print it: ~51k, ~2.1k, ~560. */
export function formatTokens(tokens) {
  if (tokens >= 10_000) return `~${Math.round(tokens / 1000)}k`;
  if (tokens >= 1_000) return `~${(tokens / 1000).toFixed(1)}k`;
  return `~${Math.round(tokens / 10) * 10}`;
}

/** Read a stamped value back into tokens. */
export function parseStamp(text) {
  const m = /^~?\s*([\d.]+)\s*(k?)$/i.exec(text.trim());
  if (!m) return null;
  return Number(m[1]) * (m[2] ? 1000 : 1);
}

const MARKER = /<!--\s*tax:(\w+)\s*-->([^<]*)<!--\s*\/tax\s*-->/g;

/** Rewrite every tax marker in one text. */
export function applyTaxMarkers(text, rows) {
  return text.replace(MARKER, (whole, key) => {
    const row = rows[key];
    const budget = key.endsWith("Budget") ? BUDGETS[key.slice(0, -"Budget".length)] : undefined;
    if (budget !== undefined) return `<!-- tax:${key} -->${formatTokens(budget)}<!-- /tax -->`;
    return row ? `<!-- tax:${key} -->${formatTokens(row.tokens)}<!-- /tax -->` : whole;
  });
}

/** Stamped numbers further than STAMP_TOLERANCE from the measurement. */
export function staleStamps(text, rows, file) {
  const out = [];
  for (const m of text.matchAll(MARKER)) {
    const [, key, value] = m;
    if (key.endsWith("Budget")) {
      const budget = BUDGETS[key.slice(0, -"Budget".length)];
      if (budget !== undefined && parseStamp(value) !== budget) out.push(`${file}: tax:${key} says ${value}, the budget is ${budget}`);
      continue;
    }
    const row = rows[key];
    if (!row) {
      out.push(`${file}: tax:${key} names nothing context-tax measures`);
      continue;
    }
    const stamped = parseStamp(value);
    if (stamped === null || Math.abs(stamped - row.tokens) > row.tokens * STAMP_TOLERANCE) {
      out.push(`${file}: tax:${key} says ${value.trim()}, measured ${row.tokens}`);
    }
  }
  return out;
}

function render(rows) {
  const exact = Object.values(rows).every((r) => r.exact);
  const pad = (v, w) => String(v).padStart(w);
  const lines = [
    "",
    `  Context tax  (hermetic, Epic catalog included; tokens ${exact ? "exact via count_tokens" : "~ chars/4 estimate, set ANTHROPIC_API_KEY for exact"})`,
    "",
    "  measured                                     tools   instr.chars   tools.chars   total.chars   tokens   budget",
  ];
  for (const [key, r] of Object.entries(rows)) {
    const seed = r.toolCount !== undefined;
    lines.push(
      `  ${r.what.padEnd(44)} ${seed ? pad(r.toolCount, 5) : pad("", 5)}   ${seed ? pad(r.instructionsChars, 11) : pad("", 11)}   ${seed ? pad(r.toolsChars, 11) : pad("", 11)}   ${pad(r.chars, 11)}   ${pad(r.tokens, 6)}   ${pad(BUDGETS[key] ?? "", 6)}`,
    );
  }
  lines.push("", "  note: lean/micro move cost to on-demand describe/call round-trips; add those for a full-session total.", "");
  return lines.join("\n");
}

async function main(argv) {
  const rows = await measureContextTax();
  if (argv.includes("--json")) console.log(JSON.stringify(rows, null, 2));
  else console.log(render(rows));

  if (argv.includes("--stamp")) {
    for (const rel of TAX_MARKER_FILES) {
      const file = path.join(ROOT, rel);
      if (!fs.existsSync(file)) continue;
      const before = fs.readFileSync(file, "utf8");
      const after = applyTaxMarkers(before, rows);
      if (after !== before) {
        fs.writeFileSync(file, after);
        console.log(`  stamped ${rel}`);
      }
    }
  }

  if (argv.includes("--check")) {
    const problems = budgetProblems(rows);
    if (problems.length > 0) {
      for (const p of problems) console.error(`  over budget: ${p}`);
      return 1;
    }
  }
  return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(await main(process.argv.slice(2)));
}
