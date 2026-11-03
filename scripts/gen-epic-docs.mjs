#!/usr/bin/env node
/**
 * Deterministically generate the "Native Unreal Tools" documentation page
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
 * Availability per engine comes from each action's `epicTool.engines` and the
 * recorded snapshots in tests/golden/epic-catalog/.
 *
 * Usage: node scripts/gen-epic-docs.mjs   (run `npx tsc` first so dist/ exists)
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS } from "../dist/tools.js";
import { loadSnapshots, mergeSnapshots } from "./lib/epic-catalog.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
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
        engines: spec.epicTool?.engines ?? [],
        description: (spec.description ?? "").replace(/\s+/g, " ").trim(),
      }));
    if (rows.length) out[t.name] = rows.sort((a, b) => a.action.localeCompare(b.action));
  }
  return out;
}

function main() {
  const snapshots = loadSnapshots();
  const engines = snapshots.map((s) => s.engine);
  const byCat = wrappedByCategory();
  const total = Object.values(byCat).reduce((n, r) => n + r.length, 0);
  const merged = mergeSnapshots(snapshots);
  const toolsetCount = merged.toolsets.length;

  const lines = [];
  lines.push("# Native Unreal Tools");
  lines.push("");
  // MkDocs admonition -> the landing converter renders this as a Fumadocs
  // <Callout>, which is the "official" badge for the whole page (no per-row
  // emoji). Body must be indented 4 spaces for both MkDocs and the converter.
  lines.push('!!! note "Official - Unreal Engine 5.8 and later"');
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
  lines.push("## Engine availability");
  lines.push("");
  lines.push(
    "The list is the union of every recorded engine's registry. A tool that is not on every engine is marked " +
    "with the engines it is on; calling it on a recorded engine without it returns `unsupported_engine_version` " +
    "naming the engines that have it.",
  );
  lines.push("");
  lines.push("| Engine | Toolsets | Tools | Only on this engine |");
  lines.push("| --- | --- | --- | --- |");
  const mergedTools = merged.toolsets.flatMap((ts) => ts.tools);
  for (const { engine, catalog } of snapshots) {
    const tools = (catalog.toolsets ?? []).reduce((n, ts) => n + (ts.tools?.length ?? 0), 0);
    const only = engines.length > 1 ? mergedTools.filter((t) => t.engines.length === 1 && t.engines[0] === engine).length : "-";
    lines.push(`| UE ${engine} | ${catalog.toolsets?.length ?? 0} | ${tools} | ${only} |`);
  }
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
      lines.push(`- \`${cat}(${r.action})\`${availability(r.engines, engines)} - ${r.description}`);
    }
    lines.push("");
  }

  fs.writeFileSync(DOC, lines.join("\n").replace(/\s+$/, "") + "\n");
  console.log(`gen-epic-docs: wrote ${DOC} - ${total} tools across ${cats.length} categories`);
  for (const cat of cats) console.log(`  ${cat}: ${byCat[cat].length}`);
}

/** " *(UE 5.9)*" for a tool not on every recorded engine, nothing otherwise. */
function availability(on, all) {
  return on.length === 0 || all.every((e) => on.includes(e)) ? "" : ` *(UE ${on.join(", ")})*`;
}

main();
