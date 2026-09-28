import { readConfigDoc } from "../config/ue-mcp-config.js";
import { pluginEntriesOf } from "../config/project-config.js";
import type { PluginEntry } from "../flow/schema.js";

export type { PluginEntry } from "../flow/schema.js";

/**
 * The `plugins:` array of one config file, for commands that edit that file.
 * Missing file or key reads as empty; an entry that fails PluginEntrySchema is
 * skipped. What a project loads is the merged list, ProjectConfig.plugins.
 */
export function readPluginsList(configPath: string): PluginEntry[] {
  return pluginEntriesOf(readConfigDoc(configPath));
}
