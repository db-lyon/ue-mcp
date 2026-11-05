/**
 * What an installed binary bridge must look like for the editor to load it
 * without offering a rebuild: every editor module the descriptor lists has its
 * DLL, the module manifest names exactly those modules under one BuildId, no
 * Source tree, and nothing left active from another release.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { BRIDGE_PLUGIN_NAME, readInstallMarker } from "./install-marker.js";

/** A file renamed aside by an in-place swap: `<name>.old-<stamp>`. */
export const ASIDE_FILE = /\.old-[^\\/]+$/;

/** Files the updater itself writes into the plugin, never part of a release. */
export const UPDATER_FILES = new Set([".ue-mcp-update-pending.json"]);

/** Module types the editor target loads. Program-only and game-only types are not. */
const EDITOR_MODULE_TYPES = new Set([
  "Runtime", "RuntimeNoCommandlet", "RuntimeAndProgram", "CookedOnly", "UncookedOnly",
  "Developer", "DeveloperTool", "Editor", "EditorNoCommandlet", "EditorAndProgram",
]);

export interface TreeExpectation {
  /** The version the install marker must name. */
  version: string;
  /** Unreal platform name, e.g. Win64. */
  platform: string;
  /** Every file the release ships, relative to the plugin root. Anything else active is stale. */
  files?: string[];
  /** The only aside files allowed, absolute. Defaults to none. */
  asideFiles?: string[];
  /** Whether the install marker must say binary at `version`. Defaults to true; an install from before the marker has none. */
  checkMarker?: boolean;
}

function rel(p: string): string {
  return p.replace(/\\/g, "/");
}

/** Every file under `dir`, relative and with forward slashes. */
export function treeFiles(dir: string, base = dir): string[] {
  const out: string[] = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...treeFiles(full, base));
    else out.push(rel(path.relative(base, full)));
  }
  return out;
}

/** The editor modules a descriptor lists for `platform`, or an error naming why it cannot be read. */
export function descriptorEditorModules(pluginDir: string, platform: string): string[] {
  const descriptor = path.join(pluginDir, `${BRIDGE_PLUGIN_NAME}.uplugin`);
  const parsed = JSON.parse(fs.readFileSync(descriptor, "utf-8")) as {
    Modules?: Array<{ Name?: string; Type?: string; PlatformAllowList?: string[]; WhitelistPlatforms?: string[] }>;
  };
  return (parsed.Modules ?? [])
    .filter((m) => typeof m.Name === "string" && EDITOR_MODULE_TYPES.has(m.Type ?? ""))
    .filter((m) => {
      const allow = m.PlatformAllowList ?? m.WhitelistPlatforms;
      return !allow || allow.includes(platform);
    })
    .map((m) => m.Name as string);
}

/**
 * Check an installed binary bridge against what its release promises. Returns
 * every problem found, empty when the tree is sound. Never throws.
 */
export function verifyBridgeTree(pluginDir: string, expect: TreeExpectation): string[] {
  const problems: string[] = [];
  let files: string[];
  try {
    files = treeFiles(pluginDir);
  } catch (e) {
    return [`cannot read ${pluginDir}: ${e instanceof Error ? e.message : String(e)}`];
  }
  const active = files.filter((f) => !ASIDE_FILE.test(f) && !UPDATER_FILES.has(f));

  const marker = readInstallMarker(pluginDir);
  if (expect.checkMarker !== false) {
    if (marker?.kind !== "binary") problems.push(`install marker says ${marker?.kind ?? "nothing"}, not binary`);
    else if (marker.version !== expect.version) problems.push(`install marker names ${marker.version ?? "no version"}, not ${expect.version}`);
  }

  if (active.some((f) => f.startsWith("Source/"))) problems.push("a Source tree is present, so the editor would offer to rebuild the bridge");

  let modules: string[] = [];
  try {
    modules = descriptorEditorModules(pluginDir, expect.platform);
  } catch (e) {
    problems.push(`cannot read ${BRIDGE_PLUGIN_NAME}.uplugin: ${e instanceof Error ? e.message : String(e)}`);
  }

  const binDir = `Binaries/${expect.platform}`;
  const manifests = active.filter((f) => f.startsWith(`${binDir}/`) && f.endsWith(".modules"));
  const main = `${binDir}/UnrealEditor.modules`;
  if (!manifests.includes(main)) {
    problems.push(`${main} is missing`);
  }
  const buildIds = new Set<string>();
  for (const m of manifests) {
    try {
      const parsed = JSON.parse(fs.readFileSync(path.join(pluginDir, m), "utf-8")) as { BuildId?: string; Modules?: Record<string, string> };
      if (!parsed.BuildId) problems.push(`${m} has no BuildId`);
      else buildIds.add(parsed.BuildId);
      if (m !== main) continue;
      const listed = Object.keys(parsed.Modules ?? {}).sort();
      const want = [...modules].sort();
      if (listed.join(",") !== want.join(",")) {
        problems.push(`${main} lists ${listed.join(", ") || "no modules"} but the descriptor has ${want.join(", ") || "none"}`);
      }
      // The manifest names each module's library, so the check holds for .dll, .so and .dylib alike.
      for (const [name, file] of Object.entries(parsed.Modules ?? {})) {
        if (!active.includes(`${binDir}/${file}`)) problems.push(`module ${name} has no ${binDir}/${file}`);
      }
    } catch (e) {
      problems.push(`cannot read ${m}: ${e instanceof Error ? e.message : String(e)}`);
    }
  }
  if (buildIds.size > 1) problems.push(`module manifests disagree on BuildId (${[...buildIds].sort().join(", ")})`);

  if (expect.files) {
    const shipped = new Set(expect.files.map(rel));
    const stale = active.filter((f) => !shipped.has(f));
    if (stale.length) problems.push(`files from another release are still active: ${stale.sort().join(", ")}`);
    const missing = [...shipped].filter((f) => !active.includes(f));
    if (missing.length) problems.push(`files of this release are missing: ${missing.sort().join(", ")}`);
  }

  const allowed = new Set((expect.asideFiles ?? []).map((f) => rel(path.resolve(f)).toLowerCase()));
  const strayAside = files
    .filter((f) => ASIDE_FILE.test(f))
    .filter((f) => !allowed.has(rel(path.resolve(pluginDir, f)).toLowerCase()));
  if (strayAside.length) problems.push(`aside files no update recorded: ${strayAside.sort().join(", ")}`);
  return problems;
}
