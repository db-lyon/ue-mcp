/**
 * The recorded AI Toolset Registry catalogs, one snapshot per engine version.
 *
 *   tests/golden/epic-catalog/<major.minor>.json
 *
 * Each file is exactly what `npm run epic:record` read from one editor. The
 * generator, the seeder, the metadata counts and the docs all read them through
 * `mergeSnapshots`, so every consumer sees the same union.
 *
 * Merge rule, for a tool recorded on more than one engine:
 *   - identity is the qualified tool name; `engines` lists every version carrying it.
 *   - description, toolset metadata and each property's definition come from the
 *     newest version that has them, so the action documents the current engine.
 *   - properties are the union: an argument any engine accepts is forwarded.
 *   - required is the intersection: an argument is required only when every
 *     engine carrying the tool requires it, so no engine's call is refused for
 *     omitting what that engine does not need. The engine still enforces its own.
 *   - a property whose type differs between engines is declared as accepting
 *     either (`inputSchemas` keeps every engine's schema for that); dispatch
 *     shapes it by the newest definition.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
export const CATALOG_DIR = path.join(ROOT, "tests", "golden", "epic-catalog");

/** "5.8" out of "5.8", "5.8.1" or "5.8.0-12345+++UE5+Release-5.8"; null when there is none. */
export function engineKey(version) {
  const m = /^(\d+)\.(\d+)/.exec(String(version ?? "").trim());
  return m ? `${Number(m[1])}.${Number(m[2])}` : null;
}

/** Numeric order on major.minor keys, so 5.10 sorts after 5.9. */
export function compareEngines(a, b) {
  const [am, an] = a.split(".").map(Number);
  const [bm, bn] = b.split(".").map(Number);
  return am - bm || an - bn;
}

/** The snapshot file for one engine. */
export function snapshotPath(engine, dir = CATALOG_DIR) {
  return path.join(dir, `${engine}.json`);
}

/** Every recorded snapshot, oldest engine first. Throws when there is none. */
export function loadSnapshots(dir = CATALOG_DIR) {
  if (!fs.existsSync(dir)) throw new Error(`No catalog directory at ${dir}. Run \`npm run epic:record\` against a UE 5.8+ editor.`);
  const snapshots = fs.readdirSync(dir)
    .filter((f) => /^\d+\.\d+\.json$/.test(f))
    .map((f) => {
      const engine = f.replace(/\.json$/, "");
      const catalog = JSON.parse(fs.readFileSync(path.join(dir, f), "utf8"));
      if (engineKey(catalog.engineAssociation) !== engine) {
        throw new Error(`${f} records engine ${catalog.engineAssociation}; a snapshot is named for the engine it came from.`);
      }
      return { engine, catalog };
    })
    .sort((a, b) => compareEngines(a.engine, b.engine));
  if (snapshots.length === 0) throw new Error(`No snapshots in ${dir}. Run \`npm run epic:record\` against a UE 5.8+ editor.`);
  return snapshots;
}

/** The input schemas of one tool on several engines, oldest first, as one schema. */
export function mergeSchemas(schemas) {
  const present = schemas.filter((s) => s && typeof s === "object");
  if (present.length === 0) return {};
  const newest = present[present.length - 1];
  const properties = {};
  // Oldest first, so a newer definition overwrites an older one.
  for (const s of present) Object.assign(properties, s.properties ?? {});
  const requiredBy = present.map((s) => new Set(Array.isArray(s.required) ? s.required : []));
  const required = [...requiredBy[requiredBy.length - 1]].filter((n) => requiredBy.every((r) => r.has(n)));
  const merged = { ...newest, properties };
  if (required.length > 0) merged.required = required;
  else delete merged.required;
  return merged;
}

/**
 * The union of every snapshot: toolsets and tools sorted by name, each carrying
 * the engines that have it, with the newest definition and merged input schema.
 */
export function mergeSnapshots(snapshots) {
  const engines = snapshots.map((s) => s.engine);
  const toolsets = new Map();
  for (const { engine, catalog } of snapshots) {
    for (const ts of catalog.toolsets ?? []) {
      if (!toolsets.has(ts.name)) toolsets.set(ts.name, { engines: [], meta: ts, tools: new Map() });
      const entry = toolsets.get(ts.name);
      entry.engines.push(engine);
      entry.meta = ts;
      for (const tool of ts.tools ?? []) {
        if (!tool?.name) continue;
        if (!entry.tools.has(tool.name)) entry.tools.set(tool.name, { engines: [], defs: [] });
        const t = entry.tools.get(tool.name);
        t.engines.push(engine);
        t.defs.push(tool);
      }
    }
  }
  const byName = (a, b) => a.name.localeCompare(b.name);
  return {
    engines,
    toolsets: [...toolsets].map(([name, entry]) => {
      const { tools: _tools, toolCount: _count, ...meta } = entry.meta;
      return {
        ...meta,
        name,
        engines: entry.engines,
        tools: [...entry.tools].map(([toolName, t]) => ({
          ...t.defs[t.defs.length - 1],
          name: toolName,
          inputSchema: mergeSchemas(t.defs.map((d) => d.inputSchema)),
          inputSchemas: t.defs.map((d) => d.inputSchema ?? {}),
          engines: t.engines,
        })).sort(byName),
      };
    }).sort(byName),
  };
}

/** Every recorded snapshot merged; the shortcut every consumer uses. */
export function loadMergedCatalog(dir = CATALOG_DIR) {
  return mergeSnapshots(loadSnapshots(dir));
}
