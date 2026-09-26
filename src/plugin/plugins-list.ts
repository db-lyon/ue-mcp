import * as fs from "node:fs";
import yaml from "js-yaml";
import { PluginEntrySchema, type PluginEntry } from "../flow/schema.js";

export type { PluginEntry } from "../flow/schema.js";

/**
 * The `plugins:` array from a project's ue-mcp.yml. Missing file or key reads
 * as empty; an entry that fails PluginEntrySchema is skipped.
 */
export function readPluginsList(configPath: string): PluginEntry[] {
  if (!fs.existsSync(configPath)) return [];
  const raw = yaml.load(fs.readFileSync(configPath, "utf-8")) as { plugins?: unknown } | null;
  if (!raw || !Array.isArray(raw.plugins)) return [];
  const out: PluginEntry[] = [];
  for (const entry of raw.plugins) {
    const parsed = PluginEntrySchema.safeParse(entry);
    if (parsed.success) out.push(parsed.data);
  }
  return out;
}
