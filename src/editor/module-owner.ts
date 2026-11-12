import * as fs from "node:fs";
import * as path from "node:path";

/** The plugin a module is declared by, and whether it lives in the engine tree. */
export interface ModuleOwner {
  module: string;
  plugin: string;
  upluginPath: string;
  scope: "engine" | "project";
}

/** "UnrealEditor-FabToolset.dll" and "FabToolset" both name the module FabToolset. */
function bareModuleName(name: string): string {
  return name.replace(/\.(dll|so|dylib)$/i, "").replace(/^UnrealEditor-/i, "");
}

function declaredModules(upluginPath: string): string[] {
  try {
    const text = fs.readFileSync(upluginPath, "utf-8").replace(/^﻿/, "");
    const modules = (JSON.parse(text) as { Modules?: Array<{ Name?: unknown }> }).Modules;
    if (!Array.isArray(modules)) return [];
    return modules.map((m) => m?.Name).filter((n): n is string => typeof n === "string");
  } catch {
    return [];
  }
}

/** Every .uplugin under `root`, without descending into a plugin's own folder. */
function findUplugins(root: string): string[] {
  const found: string[] = [];
  const walk = (dir: string): void => {
    let entries: fs.Dirent[];
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    const uplugin = entries.find((e) => e.isFile() && e.name.toLowerCase().endsWith(".uplugin"));
    if (uplugin) {
      found.push(path.join(dir, uplugin.name));
      return;
    }
    for (const entry of entries) {
      if (entry.isDirectory()) walk(path.join(dir, entry.name));
    }
  };
  walk(root);
  return found;
}

/**
 * Which plugin declares each missing module (#1627). Project plugins are read
 * first, since a project plugin of the same name shadows an engine one.
 * A module no plugin declares (a project module) has no entry.
 */
export function findModuleOwners(
  modules: string[],
  roots: { projectDir?: string | null; engineRoot?: string | null },
): Map<string, ModuleOwner> {
  const owners = new Map<string, ModuleOwner>();
  const wanted = new Map(modules.map((m) => [bareModuleName(m).toLowerCase(), m]));
  if (wanted.size === 0) return owners;

  const searches: Array<{ dir: string; scope: ModuleOwner["scope"] }> = [];
  if (roots.projectDir) searches.push({ dir: path.join(roots.projectDir, "Plugins"), scope: "project" });
  if (roots.engineRoot) searches.push({ dir: path.join(roots.engineRoot, "Engine", "Plugins"), scope: "engine" });

  for (const { dir, scope } of searches) {
    for (const upluginPath of findUplugins(dir)) {
      for (const declared of declaredModules(upluginPath)) {
        const original = wanted.get(declared.toLowerCase());
        if (original === undefined || owners.has(original)) continue;
        owners.set(original, {
          module: original,
          plugin: path.basename(upluginPath).replace(/\.uplugin$/i, ""),
          upluginPath,
          scope,
        });
      }
    }
    if (owners.size === wanted.size) break;
  }
  return owners;
}
