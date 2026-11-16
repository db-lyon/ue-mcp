#!/usr/bin/env tsx
/**
 * Stamp the package version and surface counts into the files that ship them.
 *
 *   plugin/ue_mcp_bridge/*.uplugin   VersionName = package.json version, and the
 *                                    description's bridge action count
 *   package.json                     the description npmjs.org shows
 *   docs/assets/ue-mcp-overview.png  the README's overview image
 *
 * The release flow bumps package.json and runs `npm run generate:metadata`,
 * which runs this after the docs generator. `npm run stamp:package` runs it
 * alone.
 *
 * This is ONE product in ONE repo: the npm package carries the plugin source
 * and a user compiles it from that tarball, so a plugin reporting a different
 * version from the package that shipped it is telling the user their install
 * is something it is not. VersionName tracks the package exactly, prerelease
 * suffix and all, since UE treats it as a free-form display string. The integer
 * `Version` is Epic's own install-ordering field and is left alone.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { computeCounts, type Counts } from "./lib/tool-counts.js";
import { renderOverviewImage } from "./render-overview-image.js";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/**
 * The plugin descriptor, found by scanning rather than by name. The working
 * tree and the git index had disagreed on the case of this filename, which
 * Windows hides and a case-sensitive filesystem does not, so a hardcoded
 * spelling silently skipped the file on Linux and shipped a stale descriptor.
 */
function findUpluginPath(): string | null {
  const dir = path.join(repo, "plugin", "ue_mcp_bridge");
  if (!fs.existsSync(dir)) return null;
  const hit = fs.readdirSync(dir).find((f) => f.toLowerCase().endsWith(".uplugin"));
  return hit ? path.join(dir, hit) : null;
}

/** Rewrite a JSON file only when it changed, keeping its indent and trailing newline. */
function writeJsonIfChanged(file: string, raw: string, value: unknown, indent: string | number): boolean {
  const trailing = raw.endsWith("\n") ? "\n" : "";
  const next = JSON.stringify(value, null, indent) + trailing;
  if (next === raw) return false;
  fs.writeFileSync(file, next);
  return true;
}

function stampPackageDescription(counts: Counts): boolean {
  const file = path.join(repo, "package.json");
  const raw = fs.readFileSync(file, "utf8");
  const pkg = JSON.parse(raw);
  const desc = `Unreal Engine MCP server - ${counts.tools} tools, ${counts.actions}+ actions for AI-driven editor control`;
  if (pkg.description === desc) return false;
  pkg.description = desc;
  return writeJsonIfChanged(file, raw, pkg, 2);
}

function stampPluginDescriptor(counts: Counts): boolean {
  const file = findUpluginPath();
  if (!file) return false;
  const raw = fs.readFileSync(file, "utf8");
  const plugin = JSON.parse(raw);
  const desc = `C++ WebSocket bridge for UE-MCP, providing ${counts.bridgeActions}+ editor actions over the MCP protocol`;
  if (plugin.VersionName === counts.version && plugin.Description === desc) return false;
  plugin.VersionName = counts.version;
  plugin.Description = desc;
  return writeJsonIfChanged(file, raw, plugin, "\t");
}

async function main(): Promise<void> {
  const counts = computeCounts();
  if (stampPluginDescriptor(counts)) console.log(`stamped the .uplugin: VersionName ${counts.version}, description`);
  if (stampPackageDescription(counts)) console.log("stamped the package.json description");
  const image = await renderOverviewImage(counts);
  console.log(image.rendered ? `rendered ${image.png}` : `${image.png} already shows these counts`);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
