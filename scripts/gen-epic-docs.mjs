#!/usr/bin/env node
/**
 * Deterministically generate the "Native Unreal 5.8 Tools" documentation page
 * (docs/native-tools.md) listing every official Epic ToolsetRegistry tool that
 * ue-mcp wraps and surfaces as first-class actions.
 *
 * Source of truth: ALL_TOOLS itself. The wrapped engine tools are declared
 * actions now, generated into src/tools/epic/*.generated.ts from a recorded
 * catalog and a reviewed effect for each one, so the page is read off the
 * surface the server actually advertises rather than reproduced from a
 * snapshot alongside it.
 *
 * Zero-drift by construction, and now literally: the rows ARE the actions.
 * Nothing here re-derives a description, a parameter list or an effect, so
 * there is no second implementation to fall out of step.
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
import { ALL_TOOLS } from "../dist/tools.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const CATALOG = path.join(ROOT, "tests", "golden", "epic-catalog.json");
const DOC = path.join(ROOT, "docs", "native-tools.md");

// The advertised order, so the page reads in the same order as the surface.
const CATS = ALL_TOOLS.map((t) => t.name);

/**
 * The wrapped engine tools, straight off the advertised surface.
 *
 * Nothing is re-derived here. The rows ARE the declared actions, with the
 * description and effect they carry, so the page cannot drift from what a
 * client is handed. The `epic` category is excluded: its four actions are
 * ue-mcp's own gateway into the registry, not wrapped tools.
 */
function wrappedByCategory() {
  const out = {};
  for (const t of ALL_TOOLS) {
    if (t.name === "epic") continue;
    const rows = Object.entries(t.actions)
      .filter(([k]) => k.startsWith("epic_"))
      .map(([k, spec]) => ({
        action: k,
        effect: spec.effect,
        description: (spec.description ?? "").replace(/\s+/g, " ").trim(),
      }));
    if (rows.length) out[t.name] = rows.sort((a, b) => a.action.localeCompare(b.action));
  }
  return out;
}

function main() {
  const catalog = JSON.parse(fs.readFileSync(CATALOG, "utf8"));
  const byCat = wrappedByCategory();
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
    "server). Each one is a declared ue-mcp action inside the matching category: it states what it does to the " +
    "editor, declares its own parameters, and goes through the same guards, locks and dispatch as every other " +
    "action here. Pass a tool's arguments as ordinary top-level parameters, or as `input` when you prefer. " +
    "Requires UE 5.8+ with the `ToolsetRegistry` plugin (and the toolset plugins you want) enabled. The `epic` " +
    "category discovers the live registry (`status` / `list_toolsets` / `describe_toolset` / `call_tool`).",
  );
  lines.push("");
  lines.push(
    `ue-mcp currently wraps **${total} official tools** across **${toolsetCount} toolsets**, grouped below by the ` +
    "ue-mcp category they surface in. Domains ue-mcp has no native handlers for still get their own " +
    "category (`dataflow`, `conversation`); only the registry's own meta-tooling lives under `epic`.",
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
