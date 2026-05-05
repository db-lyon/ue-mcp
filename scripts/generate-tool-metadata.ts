#!/usr/bin/env tsx
/**
 * Generate tool metadata from `ALL_TOOLS` (the single source of truth).
 *
 * Outputs:
 *   1. `dist/tool-counts.json` - consumed by the landing site to render
 *      counts dynamically. Shipped inside the npm tarball so it is also
 *      reachable at `https://unpkg.com/ue-mcp@latest/dist/tool-counts.json`.
 *   2. `docs/tool-reference.md` - fully regenerated. Top-of-file intro
 *      (everything before the first `## `) is preserved from the existing
 *      file; every tool section is rebuilt from source so undocumented
 *      actions can never accumulate.
 *   3. Marker substitution - any file containing
 *      `<!-- count:tools -->...<!-- /count -->` or
 *      `<!-- count:actions -->...<!-- /count -->` has the inner span
 *      rewritten with the current numbers. Lets README / other docs stay
 *      authoritative without templating the whole file.
 *
 * Run: tsx scripts/generate-tool-metadata.ts
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { ALL_TOOLS, enumerateBridgeActions } from "../src/tools.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, "..");

interface Counts {
  tools: number;
  actions: number;
  bridgeActions: number;
  localActions: number;
  perTool: Record<string, number>;
  generatedAt: string;
  version: string;
}

function computeCounts(): Counts {
  const perTool: Record<string, number> = {};
  let total = 0;
  for (const t of ALL_TOOLS) {
    const n = Object.keys(t.actions).length;
    perTool[t.name] = n;
    total += n;
  }
  const bridge = enumerateBridgeActions().length;
  const pkg = JSON.parse(fs.readFileSync(path.join(repo, "package.json"), "utf8"));
  return {
    tools: ALL_TOOLS.length,
    actions: total,
    bridgeActions: bridge,
    localActions: total - bridge,
    perTool,
    generatedAt: new Date().toISOString(),
    version: pkg.version,
  };
}

function writeCountsJson(counts: Counts): string {
  const out = path.join(repo, "dist", "tool-counts.json");
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(counts, null, 2) + "\n");
  return out;
}

/** Take the source description and split it into (description, keyParams).
 *  Source descriptions follow the convention "<sentence>. Params: <list>"
 *  (with optional trailing prose after the params). Returns the prefix as
 *  description and the params text up to a sensible stop. */
function splitDescription(raw: string | undefined): { desc: string; params: string } {
  if (!raw) return { desc: "", params: "" };
  const marker = ". Params: ";
  const idx = raw.indexOf(marker);
  if (idx < 0) return { desc: raw.trim().replace(/\.$/, ""), params: "" };
  const desc = raw.slice(0, idx).trim();
  const rest = raw.slice(idx + marker.length).trim();
  // Trim trailing prose after the params list. Heuristic: end at the first
  // ". " that occurs at depth 0 in (), [], {}.
  let depth = 0;
  let end = rest.length;
  for (let i = 0; i < rest.length - 1; i++) {
    const ch = rest[i];
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth--;
    else if (ch === "." && rest[i + 1] === " " && depth === 0) {
      end = i;
      break;
    }
  }
  return { desc, params: rest.slice(0, end).trim().replace(/\.$/, "") };
}

/** Replace em dashes with hyphens (CLAUDE.md style rule for public artifacts). */
function deEm(s: string): string {
  return s.replace(/\s*—\s*/g, " - ");
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
      const { desc, params } = splitDescription(spec.description);
      const left = "`" + actionName + "`";
      const merged = params ? `${desc}. Params: \`${cell(params)}\`` : desc;
      lines.push(`| ${left} | ${cell(merged) || "-"} |`);
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

  // Update the count marker inside the regenerated intro before writing.
  const body = lines.join("\n");
  const stamped = applyCountMarkers(body, counts);
  fs.writeFileSync(file, stamped);
  return file;
}

function applyCountMarkers(text: string, counts: Counts): string {
  return text
    .replace(/<!--\s*count:tools\s*-->[^<]*<!--\s*\/count\s*-->/g, `<!-- count:tools -->${counts.tools}<!-- /count -->`)
    .replace(/<!--\s*count:actions\s*-->[^<]*<!--\s*\/count\s*-->/g, `<!-- count:actions -->${counts.actions}+<!-- /count -->`);
}

const MARKER_FILES = [
  "README.md",
  "docs/index.md",
  "docs/architecture.md",
  "docs/development.md",
  "docs/flows.md",
  "docs/tool-reference.md",
];

function applyMarkersToFiles(counts: Counts): string[] {
  const updated: string[] = [];
  for (const rel of MARKER_FILES) {
    const file = path.join(repo, rel);
    if (!fs.existsSync(file)) continue;
    const before = fs.readFileSync(file, "utf8");
    const after = applyCountMarkers(before, counts);
    if (after !== before) {
      fs.writeFileSync(file, after);
      updated.push(rel);
    }
  }
  return updated;
}

/** Sync the npm package description so npmjs.org shows the right number. */
function updatePackageDescription(counts: Counts): boolean {
  const file = path.join(repo, "package.json");
  const raw = fs.readFileSync(file, "utf8");
  const pkg = JSON.parse(raw);
  const desc = `Unreal Engine MCP server - ${counts.tools} tools, ${counts.actions}+ actions for AI-driven editor control`;
  if (pkg.description === desc) return false;
  pkg.description = desc;
  // Preserve trailing newline if present.
  const trailing = raw.endsWith("\n") ? "\n" : "";
  fs.writeFileSync(file, JSON.stringify(pkg, null, 2) + trailing);
  return true;
}

function main(): void {
  const counts = computeCounts();
  const json = writeCountsJson(counts);
  const ref = regenerateToolReference(counts);
  const updated = applyMarkersToFiles(counts);
  const pkgChanged = updatePackageDescription(counts);

  console.log(`tools=${counts.tools} actions=${counts.actions} (bridge=${counts.bridgeActions}, local=${counts.localActions})`);
  console.log(`wrote ${path.relative(repo, json)}`);
  console.log(`wrote ${path.relative(repo, ref)}`);
  if (updated.length) console.log(`stamped markers in: ${updated.join(", ")}`);
  if (pkgChanged) console.log(`updated package.json description`);
}

main();
