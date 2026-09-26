import { readConfigDoc } from "../config/ue-mcp-config.js";
import { PluginEntrySchema, type PluginEntry } from "../flow/schema.js";

export type { PluginEntry } from "../flow/schema.js";

/**
 * The `plugins:` array from a project's ue-mcp.yml. Missing file or key reads
 * as empty; an entry that fails PluginEntrySchema is skipped.
 */
export function readPluginsList(configPath: string): PluginEntry[] {
  const raw = readConfigDoc(configPath);
  if (!Array.isArray(raw.plugins)) return [];
  const out: PluginEntry[] = [];
  for (const entry of raw.plugins) {
    const parsed = PluginEntrySchema.safeParse(entry);
    if (parsed.success) out.push(parsed.data);
  }
  return out;
}
