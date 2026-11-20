#!/usr/bin/env tsx
/**
 * Generate the docs metadata from `ALL_TOOLS`.
 *
 * Outputs:
 *   1. `dist/tool-counts.json` - consumed by the landing site to render
 *      counts dynamically. Shipped inside the npm tarball so it is also
 *      reachable at `https://unpkg.com/ue-mcp@latest/dist/tool-counts.json`.
 *   2. `docs/tool-reference.md` - fully regenerated. Top-of-file intro
 *      (everything before the first `## `) is preserved from the existing
 *      file; every tool section is rebuilt from source so undocumented
 *      actions can never accumulate.
 *   3. Every `<!-- count:<name> -->...<!-- /count -->` marker in the tracked
 *      Markdown files, found by scanning (scripts/lib/tool-counts.ts).
 *
 * The package version, the npm and plugin descriptions and the overview image
 * are stamped by scripts/stamp-package.ts. `npm run generate:metadata` runs
 * both; `npm run generate` runs every generator in order.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { ALL_TOOLS } from "../src/tools.js";
import { actionSignature } from "../src/surface/action-signature.js";
import { applyCountMarkers, computeCounts, markerFiles, type Counts } from "./lib/tool-counts.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, "..");

function writeCountsJson(counts: Counts): string {
  const out = path.join(repo, "dist", "tool-counts.json");
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(counts, null, 2) + "\n");
  return out;
}

/**
 * Replace em dashes with hyphens (agent guide style rule for public artifacts).
 * The literal in the pattern is the character being stripped, so it is the one
 * place in this repo that has to keep it. Leave it alone during style sweeps.
 */
function deEm(s: string): string {
  return s.replace(/\s*—\s*/g, " - "); // em-dash-allowed: the literal is the character being stripped
}

/** Escape pipes in a markdown table cell. */
function cell(s: string): string {
  return deEm(s).replace(/\|/g, "\\|");
}

function regenerateToolReference(counts: Counts): string {
  const file = path.join(repo, "docs", "tool-reference.md");
  const existing = fs.readFileSync(file, "utf8");
  const firstSection = existing.search(/^## /m);
  const intro = firstSection >= 0 ? existing.slice(0, firstSection) : existing;

  const lines: string[] = [];
  lines.push(deEm(intro).replace(/\s+$/, ""));
  lines.push("");

  for (const t of ALL_TOOLS) {
    const summary = deEm(t.description.split("\n")[0].replace(/\.$/, "").trim());
    // Heading uses just the tool name so the auto-generated anchor is the
    // tool name (`#project`, `#asset`, ...). The descriptive summary lives
    // in a paragraph below the heading.
    lines.push(`## ${t.name}`);
    lines.push("");
    lines.push(`*${summary}.*`);
    lines.push("");
    lines.push("| Action | Description |");
    lines.push("|--------|-------------|");
    for (const [actionName, spec] of Object.entries(t.actions)) {
      // The signature is the one the server advertises (#1172), built from the
      // action's declaration; the row's other cell is its description.
      const left = "`" + cell(actionSignature(t, actionName)) + "`";
      const desc = cell((spec.description ?? "").trim().replace(/\.$/, ""));
      lines.push(`| ${left} | ${desc || "-"} |`);
    }
    lines.push("");
    lines.push("---");
    lines.push("");
  }

  // Drop the trailing `---` separator after the last tool.
  while (lines.length && (lines[lines.length - 1] === "" || lines[lines.length - 1] === "---")) {
    lines.pop();
  }
  lines.push("");

  const stamped = applyCountMarkers(lines.join("\n"), counts);
  fs.writeFileSync(file, stamped);
  return file;
}

function applyMarkersToFiles(counts: Counts): string[] {
  const updated: string[] = [];
  for (const rel of markerFiles()) {
    const file = path.join(repo, rel);
    const before = fs.readFileSync(file, "utf8");
    const after = applyCountMarkers(before, counts);
    if (after !== before) {
      fs.writeFileSync(file, after);
      updated.push(rel);
    }
  }
  return updated;
}

function main(): void {
  const counts = computeCounts();
  const json = writeCountsJson(counts);
  const ref = regenerateToolReference(counts);
  const updated = applyMarkersToFiles(counts);

  console.log(`tools=${counts.tools} actions=${counts.actions} (bridge=${counts.bridgeActions}, local=${counts.localActions})`);
  console.log(
    `  of those: own=${counts.ownActions}, wrapped Epic=${counts.nativeToolActions} ` +
      `across ${counts.nativeToolsets} toolsets`,
  );
  console.log(`wrote ${path.relative(repo, json)}`);
  console.log(`wrote ${path.relative(repo, ref)}`);
  if (updated.length) console.log(`stamped markers in: ${updated.join(", ")}`);
}

main();
