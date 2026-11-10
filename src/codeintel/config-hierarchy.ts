/**
 * Unreal's config hierarchy, read the way the engine reads it: one category
 * (Engine, Game, Input, ...) is the stack of ini files from the engine's Base
 * up to the per-user Saved copy, each applying its lines on top of the last.
 * read_config shows one file; this answers what the editor actually ends up
 * with, which file set it, and what the project changes from the engine.
 *
 * Line operations, as FConfigFile applies them:
 *   Key=Value    replace every value of the key with this one
 *   +Key=Value   add the value unless the key already holds it
 *   .Key=Value   add the value even when it is already there
 *   -Key=Value   remove that exact value
 *   !Key=...     clear the key
 * A line starting with `;` (or `#`) is a comment.
 */
import * as fs from "node:fs";
import * as path from "node:path";

export type LayerScope = "engine" | "project" | "user";

export interface ConfigLayer {
  /** Short name: EngineBase, EngineBaseCategory, EnginePlatform, ProjectDefault, ProjectPlatform, UserSaved. */
  name: string;
  scope: LayerScope;
  file: string;
}

export type ConfigOp = "set" | "add" | "addDuplicate" | "remove" | "clear";

export interface ConfigLine {
  section: string;
  key: string;
  op: ConfigOp;
  value: string;
  line: number;
}

const OPS: Record<string, ConfigOp> = { "+": "add", ".": "addDuplicate", "-": "remove", "!": "clear" };

/** A config category name such as Engine or Game, from Engine, DefaultEngine or DefaultEngine.ini. */
export function configCategory(name: string): string {
  if (typeof name !== "string" || name.trim() === "") throw new Error("Missing 'configName' (e.g. 'Engine', 'Game', 'Input')");
  let category = name.trim().replace(/\.ini$/i, "");
  if (/^Default(?=[A-Z])/.test(category)) category = category.slice("Default".length);
  if (/^Base(?=[A-Z])/.test(category)) category = category.slice("Base".length);
  if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(category)) {
    throw new Error(`'${name}' is not a config category: pass a name such as Engine, Game, Input or DeviceProfiles`);
  }
  return category;
}

/** The platform name Unreal uses for a host OS: Windows, Mac or Linux. */
export function hostPlatform(platform: NodeJS.Platform = process.platform): string {
  return platform === "win32" ? "Windows" : platform === "darwin" ? "Mac" : "Linux";
}

export function checkPlatform(name: string | undefined): string {
  const platform = name?.trim() || hostPlatform();
  if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(platform)) throw new Error(`'${name}' is not a platform name (e.g. Windows, Mac, Linux, Android, IOS)`);
  return platform;
}

/**
 * The files one category is read from, lowest first. A platform's files live
 * under Config/<Platform>/ or, since UE5, Platforms/<Platform>/Config/; both
 * are listed and whichever exists is read.
 */
export function configLayers(opts: {
  engineRoot: string | null;
  projectDir: string;
  category: string;
  platform: string;
}): ConfigLayer[] {
  const { engineRoot, projectDir, category: c, platform: p } = opts;
  const layers: ConfigLayer[] = [];
  if (engineRoot) {
    const cfg = path.join(engineRoot, "Engine", "Config");
    layers.push({ name: "EngineBase", scope: "engine", file: path.join(cfg, "Base.ini") });
    layers.push({ name: "EngineBaseCategory", scope: "engine", file: path.join(cfg, `Base${c}.ini`) });
    layers.push({ name: "EnginePlatform", scope: "engine", file: path.join(cfg, p, `Base${p}${c}.ini`) });
    layers.push({ name: "EnginePlatform", scope: "engine", file: path.join(engineRoot, "Engine", "Platforms", p, "Config", `Base${p}${c}.ini`) });
  }
  const proj = path.join(projectDir, "Config");
  layers.push({ name: "ProjectDefault", scope: "project", file: path.join(proj, `Default${c}.ini`) });
  layers.push({ name: "ProjectPlatform", scope: "project", file: path.join(proj, p, `${p}${c}.ini`) });
  layers.push({ name: "ProjectPlatform", scope: "project", file: path.join(projectDir, "Platforms", p, "Config", `${p}${c}.ini`) });
  layers.push({ name: "UserSaved", scope: "user", file: path.join(projectDir, "Saved", "Config", `${p}Editor`, `${c}.ini`) });
  return layers;
}

/** Every key line of an ini file, in order, with its operation. */
export function parseConfigLines(content: string): ConfigLine[] {
  const out: ConfigLine[] = [];
  let section = "";
  const lines = content.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const trimmed = lines[i].trim();
    if (!trimmed || trimmed.startsWith(";") || trimmed.startsWith("#")) continue;
    if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
      section = trimmed.slice(1, -1).trim();
      continue;
    }
    if (!section) continue;
    let body = trimmed;
    let op: ConfigOp = "set";
    const marker = OPS[body[0]];
    if (marker) {
      op = marker;
      body = body.slice(1);
    }
    const eq = body.indexOf("=");
    const key = (eq < 0 ? body : body.slice(0, eq)).trim();
    if (!key) continue;
    if (eq < 0 && op !== "clear") continue;
    out.push({ section, key, op, value: eq < 0 ? "" : body.slice(eq + 1).trim(), line: i + 1 });
  }
  return out;
}

/** Apply one line to a key's values. */
export function applyOp(values: string[], line: Pick<ConfigLine, "op" | "value">): string[] {
  switch (line.op) {
    case "set": return [line.value];
    case "add": return values.includes(line.value) ? values : [...values, line.value];
    case "addDuplicate": return [...values, line.value];
    case "remove": return values.filter((v) => v !== line.value);
    case "clear": return [];
  }
}

/** A key's values as one answer: the string for one, the list for several, null for none. */
export function displayValue(values: readonly string[]): string | string[] | null {
  return values.length === 0 ? null : values.length === 1 ? values[0] : [...values];
}

/** Section names compare as Unreal compares them, without case. */
const norm = (s: string): string => s.toLowerCase();

export interface LoadedLayer extends ConfigLayer {
  exists: boolean;
  lines: ConfigLine[];
}

/** Read every layer that exists; one that does not is kept with exists=false. */
export function loadLayers(layers: readonly ConfigLayer[]): LoadedLayer[] {
  return layers.map((layer) => {
    if (!fs.existsSync(layer.file)) return { ...layer, exists: false, lines: [] };
    return { ...layer, exists: true, lines: parseConfigLines(fs.readFileSync(layer.file, "utf8")) };
  });
}

export interface LayerStep {
  layer: string;
  scope: LayerScope;
  file: string;
  lines: Array<{ line: number; op: ConfigOp; value: string; text: string }>;
  valueAfter: string | string[] | null;
}

/** Render a line back the way it reads in the file. */
function lineText(l: ConfigLine): string {
  const prefix = { set: "", add: "+", addDuplicate: ".", remove: "-", clear: "!" }[l.op];
  return l.op === "clear" && l.value === "" ? `!${l.key}` : `${prefix}${l.key}=${l.value}`;
}

/** Every layer that touches section/key, in order, with the value after each. */
export function explainKey(loaded: readonly LoadedLayer[], section: string, key: string): { steps: LayerStep[]; values: string[] } {
  let values: string[] = [];
  const steps: LayerStep[] = [];
  for (const layer of loaded) {
    const touching = layer.lines.filter((l) => norm(l.section) === norm(section) && l.key === key);
    if (touching.length === 0) continue;
    for (const l of touching) values = applyOp(values, l);
    steps.push({
      layer: layer.name,
      scope: layer.scope,
      file: layer.file,
      lines: touching.map((l) => ({ line: l.line, op: l.op, value: l.value, text: lineText(l) })),
      valueAfter: displayValue(values),
    });
  }
  return { steps, values };
}

/** Final values of every key, keyed `section\0key`, from the given layers. */
export function resolveAll(loaded: readonly LoadedLayer[]): Map<string, { section: string; key: string; values: string[]; setBy?: LoadedLayer }> {
  const out = new Map<string, { section: string; key: string; values: string[]; setBy?: LoadedLayer }>();
  for (const layer of loaded) {
    for (const l of layer.lines) {
      const id = `${norm(l.section)}\0${l.key}`;
      const entry = out.get(id) ?? { section: l.section, key: l.key, values: [] };
      const next = applyOp(entry.values, l);
      entry.values = next;
      entry.setBy = layer;
      out.set(id, entry);
    }
  }
  return out;
}

/** The layers that were not found, for a note on the answer. */
export function layerReport(loaded: readonly LoadedLayer[]): Array<{ layer: string; file: string; exists: boolean }> {
  return loaded.map((l) => ({ layer: l.name, file: l.file, exists: l.exists }));
}
