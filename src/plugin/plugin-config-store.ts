/**
 * Read/write the `ue-mcp.pluginConfig.<slug>.groups` toggle map across config
 * layers. Backs `ue-mcp plugin config`.
 *
 * Writes target one layer file at a time (global by default); the server merges
 * all layers at load. Reads compose every layer so the CLI can show the
 * *effective* state and which layer decided each group.
 *
 * Layer precedence (low -> high) mirrors project.ts:
 *   ~/.ue-mcp/config.yml  (global, untracked)  <
 *   <project>/ue-mcp.yml  (project, tracked)    <
 *   ue-mcp.{env}.yml      (env overlay)         <
 *   ue-mcp.local.yml      (local, untracked)
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { dumpYaml } from "../yaml-dump.js";
import { globalConfigPath } from "../global-config.js";

export type ConfigTarget = "global" | "local" | "project";

export interface ConfigLayer {
  target: ConfigTarget | "env";
  /** Human label for menus and --list-groups provenance. */
  label: string;
  file: string;
}

/** The file a write target maps to. */
export function targetFile(projectDir: string, target: ConfigTarget): string {
  switch (target) {
    case "global":
      return globalConfigPath();
    case "local":
      return path.join(projectDir, "ue-mcp.local.yml");
    case "project":
      return path.join(projectDir, "ue-mcp.yml");
  }
}

/** All read layers in precedence order (low -> high), including the env overlay. */
export function readLayers(projectDir: string): ConfigLayer[] {
  const layers: ConfigLayer[] = [
    { target: "global", label: "~/.ue-mcp/config.yml (you, all projects)", file: globalConfigPath() },
    { target: "project", label: "ue-mcp.yml (team, tracked)", file: path.join(projectDir, "ue-mcp.yml") },
  ];
  const env = process.env.UE_MCP_ENV;
  if (env) {
    layers.push({ target: "env", label: `ue-mcp.${env}.yml (env overlay)`, file: path.join(projectDir, `ue-mcp.${env}.yml`) });
  }
  layers.push({ target: "local", label: "ue-mcp.local.yml (you, this project)", file: path.join(projectDir, "ue-mcp.local.yml") });
  return layers;
}

function loadDoc(file: string): Record<string, unknown> {
  if (!fs.existsSync(file)) return {};
  try {
    const raw = yaml.load(fs.readFileSync(file, "utf-8"));
    return raw && typeof raw === "object" && !Array.isArray(raw)
      ? (raw as Record<string, unknown>)
      : {};
  } catch {
    return {};
  }
}

/** The `groups` map a single layer file declares for one plugin slug, or {}. */
export function readLayerGroups(file: string, slug: string): Record<string, boolean> {
  const doc = loadDoc(file);
  const block = doc["ue-mcp"];
  if (!block || typeof block !== "object") return {};
  const pc = (block as Record<string, unknown>).pluginConfig;
  if (!pc || typeof pc !== "object") return {};
  const entry = (pc as Record<string, unknown>)[slug];
  if (!entry || typeof entry !== "object") return {};
  const groups = (entry as Record<string, unknown>).groups;
  if (!groups || typeof groups !== "object") return {};
  const out: Record<string, boolean> = {};
  for (const [k, v] of Object.entries(groups as Record<string, unknown>)) {
    if (typeof v === "boolean") out[k] = v;
  }
  return out;
}

/** Effective group state and the layer label that last set each group. */
export function readEffectiveGroups(
  projectDir: string,
  slug: string,
): { state: Record<string, boolean>; source: Record<string, string> } {
  const state: Record<string, boolean> = {};
  const source: Record<string, string> = {};
  for (const layer of readLayers(projectDir)) {
    const g = readLayerGroups(layer.file, slug);
    for (const [k, v] of Object.entries(g)) {
      state[k] = v;
      source[k] = layer.label;
    }
  }
  return { state, source };
}

/**
 * Update the `groups` map for one plugin slug in a single layer file, applying
 * `mutate` to whatever that layer already declares (empty {} if none). Preserves
 * every other key in the file; the doc is re-dumped, so YAML comments in a
 * hand-edited file are not preserved (call sites note this for the tracked
 * project layer).
 */
export function writeLayerGroups(
  file: string,
  slug: string,
  mutate: (existing: Record<string, boolean>) => Record<string, boolean>,
): void {
  const doc = loadDoc(file);
  const block =
    doc["ue-mcp"] && typeof doc["ue-mcp"] === "object"
      ? (doc["ue-mcp"] as Record<string, unknown>)
      : {};
  const pc =
    block.pluginConfig && typeof block.pluginConfig === "object"
      ? (block.pluginConfig as Record<string, unknown>)
      : {};
  const entry =
    pc[slug] && typeof pc[slug] === "object"
      ? (pc[slug] as Record<string, unknown>)
      : {};

  const next = mutate(readLayerGroups(file, slug));

  // Drop the groups key entirely when empty so we don't leave `groups: {}`
  // noise behind after a reset.
  if (Object.keys(next).length === 0) {
    delete entry.groups;
  } else {
    entry.groups = next;
  }
  // Prune empty containers on the way out so a cleared config leaves no husk.
  if (Object.keys(entry).length === 0) {
    delete pc[slug];
  } else {
    pc[slug] = entry;
  }
  if (Object.keys(pc).length === 0) {
    delete block.pluginConfig;
  } else {
    block.pluginConfig = pc;
  }
  if (Object.keys(block).length === 0) {
    delete doc["ue-mcp"];
  } else {
    doc["ue-mcp"] = block;
  }

  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(file, dumpYaml(doc), "utf-8");
}
