/**
 * ProjectConfigProjector
 *
 * Projects the user's project-level config into /UE/Project/Config/**.
 * Pure filesystem: reads .uproject (engine association, plugin
 * overrides, modules) and .ue-mcp.json (our own per-project settings).
 * Lets repo-local policy layers reference project config by path.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import type { EmittedFragment, Point, Projector } from "@db-lyon/cairn";

export interface ProjectConfigProjectorInput {
  projectDir: string | null;
  projectPath: string | null;
}

const CONFIG_BASE = "/UE/Project/Config";

interface UProjectDescriptor {
  FileVersion?: number;
  EngineAssociation?: string;
  Category?: string;
  Description?: string;
  Modules?: Array<{ Name?: string; Type?: string; LoadingPhase?: string }>;
  Plugins?: Array<{ Name?: string; Enabled?: boolean; MarketplaceURL?: string }>;
}

function sanitize(s: unknown, max = 200): string | undefined {
  if (typeof s !== "string" || s.length === 0) return undefined;
  return s.length > max ? `${s.slice(0, max - 3)}...` : s;
}

function signalBool(b: boolean | undefined, t: string, f: string) {
  return { kind: "signal" as const, value: b === true ? 1 : 0, marker: b === true ? t : f };
}

function uprojectPoint(projectPath: string): Point | null {
  let descriptor: UProjectDescriptor;
  try {
    descriptor = JSON.parse(fs.readFileSync(projectPath, "utf-8")) as UProjectDescriptor;
  } catch {
    return null;
  }

  const fields: Record<string, string | number | ReturnType<typeof signalBool>> = {
    descriptorPath: projectPath,
  };
  if (typeof descriptor.FileVersion === "number") fields.fileVersion = descriptor.FileVersion;
  const engine = sanitize(descriptor.EngineAssociation);
  if (engine) fields.engineAssociation = engine;
  const cat = sanitize(descriptor.Category);
  if (cat) fields.category = cat;
  const desc = sanitize(descriptor.Description);
  if (desc) fields.description = desc;

  const children: Record<string, Point> = {};

  if (descriptor.Modules && descriptor.Modules.length > 0) {
    const modChildren: Record<string, Point> = {};
    for (const mod of descriptor.Modules) {
      const name = sanitize(mod.Name);
      if (!name) continue;
      const modFields: Record<string, string> = { name };
      const type = sanitize(mod.Type);
      if (type) modFields.type = type;
      const phase = sanitize(mod.LoadingPhase);
      if (phase) modFields.loadingPhase = phase;
      modChildren[name] = {
        meaning: `Project Module ${name}`,
        purpose: "Project-level C++ module declared by the .uproject",
        fields: modFields,
      };
    }
    if (Object.keys(modChildren).length > 0) {
      children.Modules = {
        meaning: "Modules",
        purpose: "C++ modules declared by the project descriptor",
        children: modChildren,
      };
    }
  }

  if (descriptor.Plugins && descriptor.Plugins.length > 0) {
    const plugChildren: Record<string, Point> = {};
    for (const plug of descriptor.Plugins) {
      const name = sanitize(plug.Name);
      if (!name) continue;
      plugChildren[name] = {
        meaning: `Plugin Override ${name}`,
        purpose: "Per-project enable/disable override for a plugin",
        fields: {
          name,
          enabled: signalBool(plug.Enabled !== false, "enabled", "disabled"),
        },
      };
    }
    if (Object.keys(plugChildren).length > 0) {
      children.PluginOverrides = {
        meaning: "Plugin Overrides",
        purpose: "Project-scoped plugin enable/disable declarations",
        children: plugChildren,
      };
    }
  }

  return {
    meaning: "UProject Descriptor",
    purpose: "Parsed fields of the .uproject file for this project",
    fields,
    children: Object.keys(children).length > 0 ? children : undefined,
  };
}

function mcpConfigPoint(projectDir: string): Point | null {
  const configPath = path.join(projectDir, ".ue-mcp.json");
  if (!fs.existsSync(configPath)) return null;
  let parsed: Record<string, unknown>;
  try {
    parsed = JSON.parse(fs.readFileSync(configPath, "utf-8")) as Record<string, unknown>;
  } catch {
    return null;
  }

  const fields: Record<string, string | number> = { configPath };
  for (const [k, v] of Object.entries(parsed)) {
    if (typeof v === "string") fields[k] = v;
    else if (typeof v === "number") fields[k] = v;
    // Arrays/objects/booleans skipped - repo-local .cairn layers can
    // express richer overrides if needed.
  }
  return {
    meaning: "ue-mcp Config",
    purpose: "Per-project .ue-mcp.json settings",
    fields,
  };
}

export function createProjectConfigProjector(): Projector<ProjectConfigProjectorInput> {
  return {
    name: "project-config",
    basePath: CONFIG_BASE,
    triggerEvents: ["startup", "manual"],
    project(input: ProjectConfigProjectorInput): EmittedFragment {
      const children: Record<string, Point> = {};
      if (input.projectPath && fs.existsSync(input.projectPath)) {
        const p = uprojectPoint(input.projectPath);
        if (p) children.UProject = p;
      }
      if (input.projectDir) {
        const p = mcpConfigPoint(input.projectDir);
        if (p) children.McpConfig = p;
      }

      return {
        basePath: CONFIG_BASE,
        producer: "project-config",
        producedAt: new Date().toISOString(),
        points: {
          Descriptors: {
            meaning: "Project Descriptors",
            purpose: "Parsed project-level config sources",
            fields: {
              sourceCount: Object.keys(children).length,
            },
            children,
          },
        },
      };
    },
  };
}
