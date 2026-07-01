#!/usr/bin/env node
/**
 * Deterministically generate the "Native Unreal 5.8 Tools" documentation page
 * (docs/native-tools.md) listing every official Epic ToolsetRegistry tool that
 * ue-mcp wraps and surfaces as first-class actions.
 *
 * Source of truth: assets/epic-catalog.snapshot.json (trimmed snapshot of the
 * live ToolsetRegistry catalog, refreshed on engine bumps).
 *
 * Zero-drift: we run the SAME enrichToolsWithEpicCatalog used at runtime over
 * stub categories, then document exactly the actions/descriptions it produces,
 * so the page matches the surfaced tools by construction.
 *
 * This page is OWNED entirely by this script. It deliberately does NOT touch
 * docs/tool-reference.md (which generate-tool-metadata regenerates from the
 * native ALL_TOOLS) - the two generators no longer share a file. Content is a
 * per-category bulleted list rather than one giant table, so rich tool
 * descriptions (pipes, quotes, parens) can't break table rendering.
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
const DOC = path.join(ROOT, "docs", "native-tools.md");

const CATS = [
  "gas", "niagara", "pcg", "widget", "statetree", "animation", "gameplay",
  "material", "landscape", "foliage", "level", "asset", "blueprint", "epic",
];

/** Run the real enrichment over stubs so documented actions == surfaced actions. */
function enrichedByCategory(catalog) {
  const stubs = CATS.map((c) => categoryTool(c, "", { _seed: { bridge: "_seed" } }, undefined, {}));
  enrichToolsWithEpicCatalog(stubs, catalog);
  const out = {};
  for (const t of stubs) {
    const rows = Object.entries(t.actions)
      .filter(([k]) => k.startsWith("epic_"))
      .map(([k, spec]) => ({ action: k, description: (spec.description ?? "").replace(/\s+/g, " ").trim() }));
    if (rows.length) out[t.name] = rows.sort((a, b) => a.action.localeCompare(b.action));
  }
  return out;
}

function main() {
  const catalog = JSON.parse(fs.readFileSync(SNAPSHOT, "utf8"));
  const byCat = enrichedByCategory(catalog);
  const total = Object.values(byCat).reduce((n, r) => n + r.length, 0);
  const toolsetCount = catalog.toolsets?.length ?? 0;

  const lines = [];
  lines.push("# Native Unreal 5.8 Tools");
  lines.push("");
  // MkDocs admonition -> the landing converter renders this as a Fumadocs
  // <Callout>, which is the "official" badge for the whole page (no per-row
  // emoji). Body must be indented 4 spaces for both MkDocs and the converter.
  lines.push('!!! note "Official - Unreal Engine 5.8"');
  lines.push(
    "    The actions on this page wrap Unreal's native AI Toolset Registry (the plugin behind Unreal's own MCP " +
    "server). ue-mcp reaches the registry in-process and surfaces each official tool as a first-class action inside " +
    "the matching ue-mcp category, so you call them like any other action - passing the tool's arguments via " +
    "`input`. Requires UE 5.8+ with the `ToolsetRegistry` plugin (and the toolset plugins you want) enabled. Use " +
    "the `epic` category to discover them at runtime (`status` / `list_toolsets` / `describe_toolset` / `call_tool`).",
  );
  lines.push("");
  lines.push(
    `ue-mcp currently wraps **${total} official tools** across **${toolsetCount} toolsets**, grouped below by the ` +
    "ue-mcp category they surface in. Toolsets with no natural home appear under `epic`.",
  );
  lines.push("");

  // Sort categories by our canonical order, then any extras alphabetically.
  const cats = Object.keys(byCat).sort((a, b) => {
    const ia = CATS.indexOf(a), ib = CATS.indexOf(b);
    if (ia !== -1 && ib !== -1) return ia - ib;
    if (ia !== -1) return -1;
    if (ib !== -1) return 1;
    return a.localeCompare(b);
  });

  for (const cat of cats) {
    const rows = byCat[cat];
    lines.push(`## ${cat}`);
    lines.push("");
    lines.push(`Wraps ${rows.length} official tool${rows.length === 1 ? "" : "s"}.`);
    lines.push("");
    for (const r of rows) {
      lines.push(`- \`${cat}(${r.action})\` - ${r.description}`);
    }
    lines.push("");
  }

  fs.writeFileSync(DOC, lines.join("\n").replace(/\s+$/, "") + "\n");
  console.log(`gen-epic-docs: wrote ${DOC} - ${total} tools across ${cats.length} categories`);
  for (const cat of cats) console.log(`  ${cat}: ${byCat[cat].length}`);
}

main();
