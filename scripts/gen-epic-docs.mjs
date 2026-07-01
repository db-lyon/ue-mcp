#!/usr/bin/env node
/**
 * Deterministically generate documentation for the official Epic 5.8 toolset
 * tools that ue-mcp wraps, and inject it into docs/tool-reference.md.
 *
 * Source of truth: scripts/data/epic-catalog.snapshot.json (a trimmed snapshot
 * of the live ToolsetRegistry catalog, refreshed on engine bumps).
 *
 * Zero-drift guarantee: we run the SAME enrichToolsWithEpicCatalog used at
 * runtime over stub categories, then document exactly the actions/descriptions
 * it produces. So the docs match the surfaced tools by construction.
 *
 * Each generated block is fenced with <!-- EPIC-GEN:<cat>:START/END --> markers
 * and is idempotent: re-running replaces the block in place.
 *
 * Usage: node scripts/gen-epic-docs.mjs   (run `npx tsc` first so dist/ exists)
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { enrichToolsWithEpicCatalog } from "../dist/epic-enrich.js";
import { categoryTool } from "../dist/types.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SNAPSHOT = path.join(ROOT, "assets", "epic-catalog.snapshot.json");
const DOC = path.join(ROOT, "docs", "tool-reference.md");
const BADGE = "🧩"; // Official Epic 5.8 tool marker (legend added near the top).

function loadCatalog() {
  return JSON.parse(fs.readFileSync(SNAPSHOT, "utf8"));
}

/** Run the real enrichment over stubs so documented actions == surfaced actions. */
function enrichedByCategory(catalog) {
  // A stub for every category the docs know about, plus epic. Enrichment routes
  // each toolset to its category (or the epic umbrella) exactly as at runtime.
  const CATS = [
    "gas", "niagara", "pcg", "widget", "statetree", "animation", "gameplay",
    "material", "landscape", "foliage", "level", "asset", "blueprint", "epic",
  ];
  const stubs = CATS.map((c) => categoryTool(c, "", { _seed: { bridge: "_seed" } }, undefined, {}));
  enrichToolsWithEpicCatalog(stubs, catalog);
  const out = {};
  for (const t of stubs) {
    const rows = Object.entries(t.actions)
      .filter(([k]) => k.startsWith("epic_"))
      .map(([k, spec]) => ({ action: k, description: spec.description ?? "" }));
    if (rows.length) out[t.name] = rows.sort((a, b) => a.action.localeCompare(b.action));
  }
  return out;
}

function esc(s) {
  return String(s).replace(/\|/g, "\\|").replace(/\r?\n/g, " ").trim();
}

function blockFor(cat, rows) {
  const start = `<!-- EPIC-GEN:${cat}:START -->`;
  const end = `<!-- EPIC-GEN:${cat}:END -->`;
  const lines = [
    start,
    "",
    `> ${BADGE} **Official Epic 5.8 toolsets** — the ${rows.length} actions below wrap Unreal's native ToolsetRegistry tools (surfaced in-process by ue-mcp). Pass tool arguments via \`input\`.`,
    "",
    "| Action | Description |",
    "| --- | --- |",
    ...rows.map((r) => `| ${BADGE} \`${cat}(${r.action})\` | ${esc(r.description)} |`),
    "",
    end,
  ];
  return lines.join("\n");
}

/** Insert or replace a category's EPIC-GEN block at the end of its `## cat` section. */
function upsertBlock(doc, cat, block) {
  const startMark = `<!-- EPIC-GEN:${cat}:START -->`;
  const endMark = `<!-- EPIC-GEN:${cat}:END -->`;
  const sIdx = doc.indexOf(startMark);
  if (sIdx !== -1) {
    const eIdx = doc.indexOf(endMark, sIdx);
    return doc.slice(0, sIdx) + block + doc.slice(eIdx + endMark.length);
  }
  // No existing block: find the `## cat` heading and insert before the next `## ` (or EOF).
  const headingRe = new RegExp(`^## ${cat}\\b`, "m");
  const hMatch = headingRe.exec(doc);
  if (!hMatch) return null; // caller decides how to handle a missing section
  const afterHeading = hMatch.index + hMatch[0].length;
  const nextH = doc.indexOf("\n## ", afterHeading);
  const insertAt = nextH === -1 ? doc.length : nextH;
  return doc.slice(0, insertAt) + "\n\n" + block + "\n" + doc.slice(insertAt);
}

function ensureLegend(doc) {
  const legend = `> ${BADGE} = **Official Epic 5.8 tool**: a native Unreal ToolsetRegistry tool that ue-mcp wraps and surfaces in-process. Requires UE 5.8+ with the ToolsetRegistry plugin enabled. See the \`epic\` category for discovery (list/describe/call).`;
  if (doc.includes(`${BADGE} = **Official Epic 5.8 tool**`)) return doc;
  // Insert the legend right after the first H1 line.
  const nl = doc.indexOf("\n", doc.indexOf("# Tool Reference"));
  return doc.slice(0, nl + 1) + "\n" + legend + "\n" + doc.slice(nl + 1);
}

function ensureEpicSection(doc, gatewayIntro) {
  if (/^## epic\b/m.test(doc)) return doc;
  // Append a new `## epic` section at EOF for the gateway + umbrella tools.
  return doc.replace(/\s*$/, "\n") + `\n## epic\n\n${gatewayIntro}\n`;
}

function main() {
  const catalog = loadCatalog();
  const byCat = enrichedByCategory(catalog);
  let doc = fs.readFileSync(DOC, "utf8");

  doc = ensureLegend(doc);

  const gatewayIntro = [
    "Discovery gateway for Unreal 5.8's native AI Toolset Registry. Every official toolset is also surfaced as first-class actions inside the matching category above (badged " + BADGE + ").",
    "",
    "| Action | Description |",
    "| --- | --- |",
    "| `status` | Report registry availability and toolset count. Never errors. |",
    "| `list_toolsets` | List registered toolsets (name, version, description, tool names). Params: `nameFilter?, includeSchemas?` |",
    "| `describe_toolset` | Full schema for one toolset (tools + input/output schemas). Params: `toolset` |",
    "| `call_tool` | Execute any registered Epic tool. Params: `toolset, tool, input?` or `inputJson?` |",
  ].join("\n");
  doc = ensureEpicSection(doc, gatewayIntro);

  const cats = Object.keys(byCat).sort();
  const missing = [];
  let updated = 0;
  for (const cat of cats) {
    const block = blockFor(cat, byCat[cat]);
    const next = upsertBlock(doc, cat, block);
    if (next === null) { missing.push(cat); continue; }
    doc = next;
    updated++;
  }

  fs.writeFileSync(DOC, doc);
  const total = Object.values(byCat).reduce((n, r) => n + r.length, 0);
  console.log(`gen-epic-docs: documented ${total} official tools across ${updated} categories`);
  for (const cat of cats) console.log(`  ${cat}: ${byCat[cat].length}`);
  if (missing.length) console.log(`  WARNING: no '## ' heading found for: ${missing.join(", ")}`);
}

main();
