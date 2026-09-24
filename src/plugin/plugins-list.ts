import * as fs from "node:fs";
import yaml from "js-yaml";

export interface PluginEntry {
  name: string;
  version?: string;
}

/** The `plugins:` array from a project's ue-mcp.yml. Missing file or key reads as empty. */
export function readPluginsList(configPath: string): PluginEntry[] {
  if (!fs.existsSync(configPath)) return [];
  const raw = yaml.load(fs.readFileSync(configPath, "utf-8")) as { plugins?: unknown } | null;
  if (!raw || !Array.isArray(raw.plugins)) return [];
  const out: PluginEntry[] = [];
  for (const entry of raw.plugins) {
    if (entry && typeof entry === "object" && typeof (entry as { name?: unknown }).name === "string") {
      const e = entry as { name: string; version?: unknown };
      out.push({
        name: e.name,
        version: typeof e.version === "string" ? e.version : undefined,
      });
    }
  }
  return out;
}
