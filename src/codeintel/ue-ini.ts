import * as fs from "node:fs";
import * as path from "node:path";

/** Resolve a config file name to an absolute path, preferring DefaultEngine.ini-style names. */
export function resolveConfigPath(configDir: string, configName: string): string {
  if (path.isAbsolute(configName)) return configName;
  if (!configName.endsWith(".ini")) configName += ".ini";
  if (!configName.startsWith("Default")) {
    const defaultPath = path.join(configDir, `Default${configName}`);
    if (fs.existsSync(defaultPath)) return defaultPath;
  }
  return path.join(configDir, configName);
}

/** Recursively collect every .ini file under `dir`. */
export function findIniFiles(dir: string): string[] {
  const results: string[] = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) results.push(...findIniFiles(full));
    else if (entry.name.endsWith(".ini")) results.push(full);
  }
  return results;
}

/** Minimal INI parser. Strips the leading +/-/. modifiers UE uses for array
 *  merge syntax. Lines outside any section land under "Global". */
export function parseIni(content: string): Record<string, Record<string, string>> {
  const sections: Record<string, Record<string, string>> = {};
  let current = "Global";
  for (const line of content.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith(";") || trimmed.startsWith("#")) continue;
    if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
      current = trimmed.slice(1, -1);
      if (!sections[current]) sections[current] = {};
      continue;
    }
    if (!sections[current]) sections[current] = {};
    const eq = trimmed.indexOf("=");
    if (eq > 0) {
      const key = trimmed.slice(0, eq).replace(/^[+\-.]/, "");
      sections[current][key] = trimmed.slice(eq + 1);
    }
  }
  return sections;
}

/** Group dotted gameplay-tag identifiers into a nested tree. */
export function buildTagTree(tags: string[]): Record<string, unknown> {
  const tree: Record<string, unknown> = {};
  for (const tag of tags) {
    let current = tree;
    for (const part of tag.split(".")) {
      if (!current[part]) current[part] = {};
      current = current[part] as Record<string, unknown>;
    }
  }
  return tree;
}
