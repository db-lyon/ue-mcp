// @session independent: Reads and writes the plugins: list of the config file it is handed.
import * as fs from "node:fs";
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

/**
 * Write the plugins: array back to ue-mcp.yml. js-yaml does not keep comments,
 * so only the `plugins:` block is replaced, as text, and every other block
 * (with its comments) is left as it was.
 */
export function writePluginsList(configPath: string, plugins: PluginEntry[]): void {
  const exists = fs.existsSync(configPath);
  const original = exists ? fs.readFileSync(configPath, "utf-8") : "";

  const rendered = plugins.length === 0
    ? "plugins: []\n"
    : "plugins:\n" + plugins.map((p) =>
        p.version
          ? `  - name: ${p.name}\n    version: ${p.version}\n`
          : `  - name: ${p.name}\n`,
      ).join("");

  if (!exists) {
    // The schema accepts a bare plugins/tasks/flows; `ue-mcp init` fills the rest later if needed.
    fs.writeFileSync(configPath, `ue-mcp:\n  version: 1\n\n${rendered}`);
    return;
  }

  // An existing `plugins:` block at column 0 (a YAML root key).
  const blockRe = /^plugins:[\t ]*(?:\r?\n(?:[ \t]+[^\r\n]*\r?\n|\r?\n)*|\[\][\t ]*\r?\n)/m;
  const match = blockRe.exec(original);
  if (match) {
    const updated = original.slice(0, match.index) + rendered + original.slice(match.index + match[0].length);
    fs.writeFileSync(configPath, updated);
    return;
  }

  const sep = original.endsWith("\n") ? (original.endsWith("\n\n") ? "" : "\n") : "\n\n";
  fs.writeFileSync(configPath, original + sep + rendered);
}
