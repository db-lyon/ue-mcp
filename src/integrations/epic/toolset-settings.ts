/**
 * The Toolset Registry's own on/off switch, written to its settings file while
 * no editor is running.
 *
 * UToolsetRegistrySettings carries BlockedNames and AllowedNames pattern lists
 * (a substring, or a /regex/; block wins), read when the editor starts. Where
 * they are saved moved between engines:
 *
 *   UE 5.9  the project's Config/DefaultGame.ini, shared by everyone on it
 *   UE 5.8  per user, Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini
 *
 * both under [/Script/ToolsetRegistry.ToolsetRegistrySettings]. A running
 * registry does not re-read the file, which is why a live editor is changed
 * through the bridge instead (epic_set_toolset_enabled) and this module is only
 * the editor-down path.
 *
 * Entries are written as `+BlockedNames=<name>`, which adds to whatever a
 * lower layer listed. Unblocking removes the lines this file holds for the
 * name; a block that comes from a lower layer (the engine's own Base ini) is
 * left alone and reported, since only that layer can take it out.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { editorPlatformDir, engineAtLeast } from "./unreal-mcp-server.js";

export const TOOLSET_SETTINGS_SECTION = "/Script/ToolsetRegistry.ToolsetRegistrySettings";
const KEY = "BlockedNames";

export interface ToolsetSettingsFile {
  file: string;
  /** `project` is shared through source control; `user` is this machine's. */
  scope: "project" | "user";
  engineVersion: string;
}

/**
 * The settings file a project's engine reads the registry's lists from, or a
 * reason there is none: an engine older than 5.8 has no Toolset Registry, and
 * an unknown version cannot be told apart.
 */
export function toolsetSettingsFile(
  projectDir: string,
  engineVersion: string | null,
  platform: NodeJS.Platform = process.platform,
): ToolsetSettingsFile | { reason: string } {
  const at59 = engineAtLeast(engineVersion, 5, 9);
  const at58 = engineAtLeast(engineVersion, 5, 8);
  if (at59 === null || at58 === null || !engineVersion) {
    return { reason: `the project's engine version could not be read (EngineAssociation '${engineVersion ?? "none"}'), so it is not known which file the Toolset Registry reads` };
  }
  if (!at58) return { reason: `UE ${engineVersion} has no Toolset Registry; it arrived in UE 5.8` };
  if (at59) return { file: path.join(projectDir, "Config", "DefaultGame.ini"), scope: "project", engineVersion };
  return {
    file: path.join(projectDir, "Saved", "Config", editorPlatformDir(platform), "EditorPerProjectUserSettings.ini"),
    scope: "user",
    engineVersion,
  };
}

interface Parsed {
  bom: string;
  eol: string;
  lines: string[];
  /** Header line index of the section, or -1. */
  start: number;
  /** Index one past the section's last line. */
  end: number;
}

function parse(file: string): Parsed {
  const before = fs.existsSync(file) ? fs.readFileSync(file, "utf-8") : "";
  const bom = before.startsWith("﻿") ? "﻿" : "";
  const body = bom ? before.slice(1) : before;
  const eol = body.includes("\r\n") ? "\r\n" : "\n";
  const lines = body.length > 0 ? body.split(/\r?\n/) : [];
  if (lines.length > 0 && lines[lines.length - 1] === "") lines.pop();
  const start = lines.findIndex((l) => l.trim().toLowerCase() === `[${TOOLSET_SETTINGS_SECTION.toLowerCase()}]`);
  let end = lines.length;
  if (start >= 0) {
    const next = lines.findIndex((l, i) => i > start && /^\s*\[.*\]\s*$/.test(l));
    if (next >= 0) end = next;
  }
  return { bom, eol, lines, start, end };
}

/** One BlockedNames line: its operator and value, or null for anything else. */
function entryOf(line: string): { op: "" | "+" | "." | "-" | "!"; value: string } | null {
  const m = /^\s*([+.\-!]?)BlockedNames\s*=(.*)$/i.exec(line);
  if (!m) return null;
  let value = m[2].trim();
  if (value.length >= 2 && value.startsWith("\"") && value.endsWith("\"")) value = value.slice(1, -1);
  return { op: m[1] as "" | "+" | "." | "-" | "!", value };
}

/** The names this one file blocks, in the order it lists them. */
export function readBlockedNames(file: string): string[] {
  const p = parse(file);
  if (p.start < 0) return [];
  let names: string[] = [];
  for (let i = p.start + 1; i < p.end; i++) {
    const e = entryOf(p.lines[i]);
    if (!e) continue;
    if (e.op === "!") names = [];
    else if (e.op === "-") names = names.filter((n) => n.toLowerCase() !== e.value.toLowerCase());
    else if (e.value && !names.some((n) => n.toLowerCase() === e.value.toLowerCase())) names.push(e.value);
  }
  return names;
}

export interface BlockedNamesWrite {
  file: string;
  changed: boolean;
  /** What the file blocks after the write. */
  blocked: string[];
}

/**
 * Block or unblock names in one settings file, keeping every other line, the
 * line endings and a byte order mark. Writes only when something changes.
 */
export function setBlockedNames(file: string, names: readonly string[], blocked: boolean): BlockedNamesWrite {
  const p = parse(file);
  const lines = [...p.lines];
  let start = p.start;
  let end = p.end;
  const lower = new Set(names.map((n) => n.toLowerCase()));
  if (blocked) {
    const present = new Set(readBlockedNames(file).map((n) => n.toLowerCase()));
    const missing = [...new Set(names)].filter((n) => !present.has(n.toLowerCase()));
    if (missing.length > 0) {
      if (start < 0) {
        if (lines.length > 0 && lines[lines.length - 1].trim() !== "") lines.push("");
        lines.push(`[${TOOLSET_SETTINGS_SECTION}]`);
        start = lines.length - 1;
        end = lines.length;
      }
      let insert = end;
      while (insert - 1 > start && lines[insert - 1].trim() === "") insert--;
      lines.splice(insert, 0, ...missing.map((n) => `+${KEY}=${n}`));
    }
  } else if (start >= 0) {
    for (let i = end - 1; i > start; i--) {
      const e = entryOf(lines[i]);
      if (e && e.op !== "-" && e.op !== "!" && lower.has(e.value.toLowerCase())) lines.splice(i, 1);
    }
  }
  const before = fs.existsSync(file) ? fs.readFileSync(file, "utf-8") : "";
  const after = lines.length > 0 ? p.bom + lines.join(p.eol) + p.eol : before;
  if (after !== before) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, after);
  }
  return { file, changed: after !== before, blocked: readBlockedNames(file) };
}
