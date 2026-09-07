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
  /** Official Unreal 5.8 ToolsetRegistry toolsets ue-mcp wraps (from the baked
   *  catalog snapshot). These surface as first-class actions at runtime but are
   *  not part of the static `actions` count. */
  nativeToolsets: number;
  /** Total official Epic tools wrapped across all native toolsets. */
  nativeToolActions: number;
  generatedAt: string;
  version: string;
}

/** Count the wrapped Epic tools from the shipped catalog snapshot. */
function computeNativeCounts(): { nativeToolsets: number; nativeToolActions: number } {
  try {
    const snap = JSON.parse(
      fs.readFileSync(path.join(repo, "assets", "epic-catalog.snapshot.json"), "utf8"),
    ) as { toolsets?: Array<{ tools?: unknown[] }> };
    const toolsets = snap.toolsets ?? [];
    const tools = toolsets.reduce((n, t) => n + (t.tools?.length ?? 0), 0);
    return { nativeToolsets: toolsets.length, nativeToolActions: tools };
  } catch {
    return { nativeToolsets: 0, nativeToolActions: 0 };
  }
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
  const native = computeNativeCounts();
  return {
    tools: ALL_TOOLS.length,
    actions: total,
    bridgeActions: bridge,
    localActions: total - bridge,
    perTool,
    nativeToolsets: native.nativeToolsets,
    nativeToolActions: native.nativeToolActions,
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

/**
 * Replace em dashes with hyphens (CLAUDE.md style rule for public artifacts).
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
      const { desc, params } = splitDescription(spec.description);
      const left = "`" + actionName + "`";
      // Escape each part exactly once. Escaping the already-escaped `merged`
      // again double-escaped pipes (`\\|`) and broke the MDX table renderer.
      const merged = params ? `${cell(desc)}. Params: \`${cell(params)}\`` : cell(desc);
      lines.push(`| ${left} | ${merged || "-"} |`);
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
    .replace(/<!--\s*count:actions\s*-->[^<]*<!--\s*\/count\s*-->/g, `<!-- count:actions -->${counts.actions}+<!-- /count -->`)
    // The actions that dispatch to the C++ bridge, which is what the smoke
    // suite exercises and therefore the number CLAUDE.md quotes for it.
    .replace(
      /<!--\s*count:bridgeActions\s*-->[^<]*<!--\s*\/count\s*-->/g,
      `<!-- count:bridgeActions -->${counts.bridgeActions}<!-- /count -->`,
    );
}

// Any doc that states a tool or action count belongs here, marker and all. A
// file left off the list has to be corrected by hand every time the surface
// grows, which is how docs/configuration.md came to say "all 22 category
// tools" twice while the server advertised twenty-four.
const MARKER_FILES = [
  "CLAUDE.md",
  "README.md",
  "docs/index.md",
  "docs/architecture.md",
  "docs/configuration.md",
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

/**
 * Keep the .uplugin in step with package.json.
 *
 * This is ONE product in ONE repo: the npm package carries the plugin source
 * and a user compiles it from that tarball. A plugin reporting a different
 * version from the package that shipped it is telling the user their install
 * is something it is not. It had drifted to 0.3.0 against a 1.3.0 package,
 * and its description still advertised 185 tools against more than a
 * thousand.
 *
 * VersionName tracks the package exactly, prerelease suffix and all, since UE
 * treats it as a free-form display string. The integer `Version` is Epic's own
 * install-ordering field and is deliberately left alone.
 */
export function findUpluginPath(): string | null {
  const dir = path.join(repo, "plugin", "ue_mcp_bridge");
  if (!fs.existsSync(dir)) return null;
  const hit = fs.readdirSync(dir).find((f) => f.toLowerCase().endsWith(".uplugin"));
  return hit ? path.join(dir, hit) : null;
}

function updatePluginDescriptor(counts: Counts): boolean {
  // Resolved by scanning rather than by name. The working tree and the git
  // index had disagreed on the case of this filename, which Windows hides and
  // a case-sensitive filesystem does not, so a hardcoded spelling silently
  // skipped the file on Linux and shipped a stale descriptor.
  const file = findUpluginPath();
  if (!file) return false;
  const raw = fs.readFileSync(file, "utf8");
  const plugin = JSON.parse(raw);
  const pkg = JSON.parse(fs.readFileSync(path.join(repo, "package.json"), "utf8"));
  const desc = `C++ WebSocket bridge for UE-MCP, providing ${counts.bridgeActions}+ editor actions over the MCP protocol`;
  if (plugin.VersionName === pkg.version && plugin.Description === desc) return false;
  plugin.VersionName = pkg.version;
  plugin.Description = desc;
  const trailing = raw.endsWith("\n") ? "\n" : "";
  fs.writeFileSync(file, JSON.stringify(plugin, null, "\t") + trailing);
  return true;
}

function main(): void {
  const counts = computeCounts();
  const json = writeCountsJson(counts);
  const ref = regenerateToolReference(counts);
  const updated = applyMarkersToFiles(counts);
  const pkgChanged = updatePackageDescription(counts);
  const upluginChanged = updatePluginDescriptor(counts);

  console.log(`tools=${counts.tools} actions=${counts.actions} (bridge=${counts.bridgeActions}, local=${counts.localActions})`);
  console.log(`wrote ${path.relative(repo, json)}`);
  console.log(`wrote ${path.relative(repo, ref)}`);
  if (updated.length) console.log(`stamped markers in: ${updated.join(", ")}`);
  if (pkgChanged) console.log(`updated package.json description`);
  if (upluginChanged) console.log(`updated UE_MCP_Bridge.uplugin version and description`);
}

main();
