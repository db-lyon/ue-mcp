/**
 * PluginProjector
 *
 * Projects engine and project plugins (.uplugin descriptors) into
 * /UE/Plugins/<PluginName>. No bridge call required - this is pure
 * filesystem against the resolved engine root and the project's own
 * Plugins directory.
 *
 * This projector enables the preflight 'requires' gate: actions
 * declare `requires: ["GameplayAbilities"]`, and the dispatch layer
 * checks /UE/Plugins/GameplayAbilities@enabled=1 before invoking
 * them. Agents trying to use a disabled plugin get a structured
 * error instead of a runtime blowup deep in the bridge.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import type { KantFragment, KantPoint, Projector } from "../types.js";

export interface PluginProjectorInput {
  engineRoot: string | null;
  projectDir: string | null;
}

interface UpluginDescriptor {
  FriendlyName?: string;
  Description?: string;
  Category?: string;
  CreatedBy?: string;
  EngineVersion?: string;
  VersionName?: string;
  Version?: number;
  EnabledByDefault?: boolean;
  MarketplaceURL?: string;
  Modules?: Array<{ Name?: string; Type?: string; LoadingPhase?: string }>;
}

const PLUGIN_BASE = "/UE/Plugins";

function sanitize(s: unknown, max = 300): string | undefined {
  if (typeof s !== "string") return undefined;
  if (s.length === 0) return undefined;
  return s.length > max ? `${s.slice(0, max - 3)}...` : s;
}

function findUplugins(root: string, maxDepth: number = 4): string[] {
  const out: string[] = [];
  const seen = new Set<string>();

  function walk(dir: string, depth: number): void {
    if (depth > maxDepth) return;
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (e.isFile() && e.name.endsWith(".uplugin")) {
        const full = path.join(dir, e.name);
        if (!seen.has(full)) {
          seen.add(full);
          out.push(full);
        }
      } else if (e.isDirectory()) {
        if (e.name === "Intermediate" || e.name === "Binaries" || e.name === "Content") continue;
        walk(path.join(dir, e.name), depth + 1);
      }
    }
  }

  walk(root, 0);
  return out;
}

function readUplugin(filePath: string): UpluginDescriptor | null {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf-8")) as UpluginDescriptor;
  } catch {
    return null;
  }
}

function signalBool(b: boolean | undefined, trueMarker: string, falseMarker: string) {
  const value = b === true ? 1 : 0;
  const marker = b === true ? trueMarker : falseMarker;
  return { kind: "signal" as const, value, marker };
}

function pluginPoint(filePath: string, source: "engine" | "project"): { name: string; point: KantPoint } | null {
  const descriptor = readUplugin(filePath);
  if (!descriptor) return null;
  const pluginName = path.basename(filePath, ".uplugin");

  const fields: Record<string, string | number | ReturnType<typeof signalBool>> = {
    name: pluginName,
    source,
    descriptorPath: filePath,
    enabled: signalBool(descriptor.EnabledByDefault !== false, "enabled", "disabled"),
  };
  const friendly = sanitize(descriptor.FriendlyName);
  if (friendly) fields.friendlyName = friendly;
  const engineVersion = sanitize(descriptor.EngineVersion);
  if (engineVersion) fields.engineVersion = engineVersion;
  const versionName = sanitize(descriptor.VersionName);
  if (versionName) fields.versionName = versionName;
  const category = sanitize(descriptor.Category);
  if (category) fields.category = category;

  const children: Record<string, KantPoint> = {};
  if (descriptor.Modules && descriptor.Modules.length > 0) {
    const moduleChildren: Record<string, KantPoint> = {};
    for (const mod of descriptor.Modules) {
      const modName = sanitize(mod.Name);
      if (!modName) continue;
      const modFields: Record<string, string> = { name: modName };
      const type = sanitize(mod.Type);
      if (type) modFields.type = type;
      const phase = sanitize(mod.LoadingPhase);
      if (phase) modFields.loadingPhase = phase;
      moduleChildren[modName] = {
        meaning: `Module ${modName}`,
        purpose: "C++ module provided by this plugin",
        fields: modFields,
      };
    }
    if (Object.keys(moduleChildren).length > 0) {
      children.Modules = {
        meaning: "Modules",
        purpose: "C++ modules exported by this plugin descriptor",
        children: moduleChildren,
      };
    }
  }

  return {
    name: pluginName,
    point: {
      meaning: `Plugin ${pluginName}`,
      purpose: sanitize(descriptor.Description) ?? `Plugin descriptor for ${pluginName}`,
      fields,
      children: Object.keys(children).length > 0 ? children : undefined,
    },
  };
}

export function createPluginProjector(): Projector<PluginProjectorInput> {
  return {
    name: "plugins",
    basePath: PLUGIN_BASE,
    triggerEvents: ["startup", "manual"],
    project(input: PluginProjectorInput): KantFragment {
      const pluginChildren: Record<string, KantPoint> = {};
      const roots: Array<{ dir: string; source: "engine" | "project" }> = [];

      if (input.engineRoot) {
        const enginePluginsDir = path.join(input.engineRoot, "Engine", "Plugins");
        if (fs.existsSync(enginePluginsDir)) {
          roots.push({ dir: enginePluginsDir, source: "engine" });
        }
      }
      if (input.projectDir) {
        const projectPluginsDir = path.join(input.projectDir, "Plugins");
        if (fs.existsSync(projectPluginsDir)) {
          roots.push({ dir: projectPluginsDir, source: "project" });
        }
      }

      let engineCount = 0;
      let projectCount = 0;
      for (const root of roots) {
        for (const filePath of findUplugins(root.dir)) {
          const p = pluginPoint(filePath, root.source);
          if (!p) continue;
          pluginChildren[p.name] = p.point;
          if (root.source === "engine") engineCount += 1;
          else projectCount += 1;
        }
      }

      return {
        basePath: PLUGIN_BASE,
        producer: "plugins",
        producedAt: new Date().toISOString(),
        points: {
          Catalog: {
            meaning: "Plugin Catalog",
            purpose: "Available plugins resolved from Engine/Plugins and <project>/Plugins",
            fields: {
              engineCount,
              projectCount,
              totalCount: engineCount + projectCount,
            },
            children: pluginChildren,
          },
        },
      };
    },
  };
}
