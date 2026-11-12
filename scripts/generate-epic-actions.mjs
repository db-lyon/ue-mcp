#!/usr/bin/env node
/**
 * Generate first-class action declarations for Unreal's AI Toolset Registry.
 *
 *     npm run epic:generate
 *
 * Reads two checked-in inputs and writes one generated module per category:
 *
 *   tests/golden/epic-catalog/*.json  what the registry contains, one snapshot
 *                                     per engine version, recorded from a live
 *                                     editor by `npm run epic:record`.
 *   src/tools/epic/effects.ts         what each tool DOES, decided by a person.
 *
 * The actions are the union across every recorded engine, merged by the rule
 * in scripts/lib/epic-catalog.mjs: newest definition, union of properties,
 * intersection of required. Each action's `epicTool.engines` names the engines
 * carrying it, and dispatch refuses a call on a recorded engine outside that
 * list with unsupported_engine_version. Action keys are claimed oldest engine
 * first, so recording a newer engine never renames an existing action.
 * `catalog-engines.ts` lists the recorded engines.
 *
 * Flags, for tests: --catalog-dir, --effects, --out-dir.
 *
 * The split is the point. The catalog is Epic's, and re-recording it must never
 * silently discard a judgement somebody made; the effects file is ours, is
 * hand-edited, and is the artifact a reviewer reads. A tool in the catalog with
 * no entry in the effects file stops this script rather than being guessed at.
 *
 * What comes out is an ordinary category action record: `bp("read", "...",
 * "epic_call_tool")` carrying the wrapped tool as `epicTool` and its input
 * schema as `epicSchema`, plus the zod parameters those actions accept.
 * Nothing about the result is special-cased downstream. It goes through the
 * same union, the same task factory, the same guards, locks, path repair,
 * `describe_action` and parameter audits as an action written by hand, because
 * it IS one - it was typed by a generator rather than a person.
 *
 * That replaces reading the live catalog at startup and inventing an effect
 * from each tool's NAME, under which 356 of the 830 rode a default nobody had
 * reviewed and none of their parameters were declared at all, so the MCP layer
 * stripped every one before dispatch.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
// The dispatcher's own parameter names, imported from the module that
// declares them. Run under tsx, so this reads the real export rather than
// parsing the file.
import { ROUTING_PARAMS } from "../src/surface/action-schema.js";
import { CATALOG_DIR, compareEngines, loadSnapshots, mergeSnapshots } from "./lib/epic-catalog.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const flag = (name, fallback) => {
  const i = process.argv.indexOf(name);
  return i >= 0 && process.argv[i + 1] ? path.resolve(process.argv[i + 1]) : fallback;
};
const CATALOG = flag("--catalog-dir", CATALOG_DIR);
const OUT_DIR = flag("--out-dir", path.join(ROOT, "src", "tools", "epic"));
const EFFECTS = flag("--effects", path.join(OUT_DIR, "effects.ts"));

/* ── routing ────────────────────────────────────────────────────────────────
 * Which ue-mcp category an Epic toolset belongs in. Kept here rather than
 * imported from src/ because this script runs before the module it generates
 * exists, and a generator that cannot run on a broken tree is no use.
 *
 * This list is the only copy. It is emitted as src/tools/epic/toolset-routes.ts,
 * which the server reads when it maps a category to the toolsets to turn off
 * (project(set_category_enabled) with scope epic), so the generator and the
 * runtime cannot place one toolset in two different categories.
 * `tests/unit/epic-generated.test.ts` fails when the emitted module is stale.
 */
const ROUTES = [
  // The Fab plugin's own toolset (module FabToolset, delivered by the launcher
  // with the "Fab UE Plugin" item, not with the engine). First, so a Fab tool
  // named for assets or materials still lands beside fab's own actions rather
  // than in asset or material. PlayFab and Fabric are not Fab.
  [/(?:^|[._])fab(?!ric)/i, "fab"],
  // UE 5.9 toolsets, ahead of the broader patterns below so a name such as
  // LevelSequenceEditor reaches animation rather than level.
  [/levelsequence|templatesequence|sequencevalidator|deformergraph|optimus/i, "animation"],
  [/pcgeditor/i, "pcg"],
  [/environmentquery|navmesh|inputtoolset/i, "gameplay"],
  [/mvvm|commonui|slatedebugger/i, "widget"],
  [/metasound|soundwave/i, "audio"],
  [/sourcecontrol/i, "source_control"],
  [/blueprint_diff/i, "blueprint"],
  [/curveatlas|userdefinedenum|userdefinedstruct|geometry_ops/i, "asset"],
  [/editortoolset\.leveltoolset|unrealcanvasspatialannotation/i, "level"],
  [/consoletoolset\.consoletoolset|cheatstoolset|python_scripting|undohistory|scenecapture|rewinddebugger|perftoolset|mediatoolset/i, "editor"],
  [/filesystemtoolset|livecoding/i, "project"],
  [/physicsasset|physicstoolset/i, "gameplay"],
  [/gas|abilitysystem|gameplaycue|attributeset/i, "gas"],
  [/niagara/i, "niagara"],
  [/\bpcg\b|pcgspatial|pcgtoolset/i, "pcg"],
  [/umg|\bwidget/i, "widget"],
  [/state_?tree/i, "statetree"],
  [/controlrig|sequencer|keyframing|\banimation/i, "animation"],
  [/gameplaytags/i, "gameplay"],
  [/material/i, "material"],
  [/landscape/i, "landscape"],
  [/foliage/i, "foliage"],
  [/\.blueprint\.|blueprinttools/i, "blueprint"],
  [/\.actor\.|actortools/i, "level"],
  [/\.asset\.|assettools|data_?asset|curve_?table|dataregistry|data_?registry/i, "asset"],
  [/skeletal_?mesh/i, "animation"],
  [/static_?mesh|\btexture|data_?table|string_?table|semanticsearch/i, "asset"],
  [/\.scene\.|scenetools|\.primitive\.|primitivetools/i, "level"],
  [/\.object\.|objecttools/i, "reflection"],
  [/behavior_?tree|worldcondition/i, "gameplay"],
  [/slateinspector/i, "widget"],
  [/plugintoolset|gamefeatures/i, "plugins"],
  [/editorapptoolset|logstoolset/i, "editor"],
  [/configsettings|automationtest/i, "project"],
  [/dataflow/i, "dataflow"],
  [/conversation/i, "conversation"],
];

function routeToolset(name) {
  for (const [re, cat] of ROUTES) if (re.test(name)) return cat;
  return "epic";
}

/* ── naming ─────────────────────────────────────────────────────────────── */

function bareToolName(qualified) {
  const i = qualified.lastIndexOf(".");
  return i >= 0 ? qualified.slice(i + 1) : qualified;
}

function actionKey(bare) {
  const snake = bare
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/[^A-Za-z0-9]+/g, "_")
    .replace(/_+/g, "_")
    .replace(/^_|_$/g, "")
    .toLowerCase();
  return `epic_${snake}`;
}

/* ── successors ─────────────────────────────────────────────────────────── */

function disjoint(a, b) {
  return !a.some((e) => b.includes(e));
}

/**
 * Which newer toolset replaced each older one: same category, no engine in
 * common, and carrying at least half of the older toolset's tool names (the
 * newer one with the most, when several do). Successors attach only within
 * such a pair, so two toolsets that both ship `create` are never crossed.
 */
function successorToolsets(toolsets) {
  const keysOf = (ts) => new Set(ts.tools.map((t) => actionKey(bareToolName(t.name))));
  const out = new Map();
  for (const old of toolsets) {
    const oldKeys = keysOf(old);
    let best = null;
    let bestShare = 0.5;
    for (const next of toolsets) {
      if (next === old || routeToolset(next.name) !== routeToolset(old.name)) continue;
      if (!disjoint(old.engines, next.engines) || compareEngines(next.engines[0], old.engines[0]) <= 0) continue;
      const nextKeys = keysOf(next);
      const share = [...oldKeys].filter((k) => nextKeys.has(k)).length / Math.max(1, oldKeys.size);
      if (share >= bestShare && (!best || share > bestShare)) { best = next.name; bestShare = share; }
    }
    if (best) out.set(old.name, best);
  }
  return out;
}

// An argument name with case, underscores and a bool's leading `b` folded away:
// `component_type` and `componentType`, `worldspace` and `bWorldSpace`.
function foldName(name, prop) {
  const flat = String(name).replace(/[^A-Za-z0-9]/g, "").toLowerCase();
  return prop?.type === "boolean" && /^b[A-Z]/.test(name) ? flat.slice(1) : flat;
}

/** Older argument names a successor spells differently, matched by their folded form. */
function renameMap(oldInput, newInput) {
  const fresh = new Map(Object.entries(newInput?.properties ?? {}).map(([n, p]) => [foldName(n, p), n]));
  const out = {};
  for (const [name, prop] of Object.entries(oldInput?.properties ?? {})) {
    const to = fresh.get(foldName(name, prop)) ?? fresh.get(foldName(name, { type: "boolean" }));
    if (to && to !== name) out[name] = to;
  }
  return Object.keys(out).length > 0 ? out : undefined;
}

// The stronger claim wins when one action reaches two tools: a read on one
// engine and a write on another is a write, and unknown gates hardest.
function strongerEffect(a, b) {
  const rank = { read: 0, mutate: 1, unknown: 2 };
  return rank[b] > rank[a] ? b : a;
}

/** Declare a tool's arguments on its category, with every engine's type for each. */
function addParams(bucket, tool, input) {
  if (reservedArgs(input).length > 0) bucket.needsInput = true;
  for (const [name, prop] of Object.entries(input.properties ?? {})) {
    // Never declared: the category tool owns the name.
    if (RESERVED_TOP_LEVEL.has(name)) continue;
    if (!bucket.params.has(name)) bucket.params.set(name, { kinds: new Set(), description: "" });
    const entry = bucket.params.get(name);
    // Every engine's type for it, so a type that changed accepts either.
    for (const s of tool.inputSchemas ?? [input]) if (s.properties?.[name]) entry.kinds.add(kindOf(s.properties[name]));
    entry.kinds.add(kindOf(prop));
    if (!entry.description && typeof prop?.description === "string") {
      entry.description = prose(prop.description);
    }
  }
}

/* ── descriptions ───────────────────────────────────────────────────────── */

// Epic's descriptions are Python docstrings: a sentence, then an indented
// "Args:"/"Returns:" block that repeats the schema we are about to declare
// properly. Keep the prose, drop the block, collapse the whitespace.
function prose(description) {
  const text = String(description ?? "").split(/\n\s*(?:Args|Arguments|Returns|Raises|Note):/)[0];
  return text
    .replace(/\s*—\s*/g, " - ") // em-dash-allowed: the literal is the character being stripped
    // Epic's prose lands in tracked files under this repository's prose rules,
    // which ban the word for a level ("quality tier" reads "quality level"). lint-prose-allow: tier
    .replace(/\btier(s?)\b/gi, (_m, s) => `level${s}`) // lint-prose-allow: tier
    .replace(/\s+/g, " ")
    .trim();
}

/**
 * Names the dispatcher consumes before a call reaches the bridge (#1078).
 * Declaring one here would overwrite the category's own parameter, so such an
 * argument goes inside `input` instead.
 */
const RESERVED_TOP_LEVEL = ROUTING_PARAMS;

/** Tool argument names this generator must not declare at the top level. */
function reservedArgs(inputSchema) {
  return Object.keys(inputSchema?.properties ?? {}).filter((n) => RESERVED_TOP_LEVEL.has(n));
}

/** The `Params:` clause every action on this surface is required to carry.
 *  A reserved argument is named in a parenthetical, which the parser reads as
 *  prose rather than as a promised key. */
function paramsClause(inputSchema) {
  const props = Object.keys(inputSchema?.properties ?? {});
  if (props.length === 0) return "Params: none";
  const required = new Set(inputSchema?.required ?? []);
  const reserved = reservedArgs(inputSchema);
  const plain = props
    .filter((n) => !RESERVED_TOP_LEVEL.has(n))
    .map((n) => (required.has(n) ? n : `${n}?`));
  if (reserved.length > 0) {
    plain.push(`input? (carries ${reserved.join(", ")}, which cannot be sent at the top level)`);
  }
  return `Params: ${plain.join(", ")}`;
}

/* ── parameter types ────────────────────────────────────────────────────── */

function isAssetRef(prop) {
  return !!prop && prop.type === "object" && !!prop.properties && "refPath" in prop.properties;
}

/** The zod expression for one JSON Schema property, as source text. */
function zodFor(kinds) {
  if (kinds.size > 1) return "z.unknown()";
  const [kind] = kinds;
  switch (kind) {
    // An asset reference is accepted as a plain content path and wrapped into
    // the engine's `{refPath}` shape on the way out, which is what makes an
    // Epic action take a path the same way a native one does.
    case "assetRef": return "z.union([z.string(), z.record(z.string(), z.unknown())])";
    case "string": return "z.string()";
    case "number": case "integer": return "z.number()";
    case "boolean": return "z.boolean()";
    case "array": return "z.array(z.unknown())";
    case "object": return "z.record(z.string(), z.unknown())";
    default: return "z.unknown()";
  }
}

function kindOf(prop) {
  if (isAssetRef(prop)) return "assetRef";
  return prop?.type ?? "unknown";
}

/**
 * The part of a tool's JSON Schema that dispatch actually reads.
 *
 * `resolveEpicToolInput` needs three things: which properties exist, which of
 * them are the engine's `{refPath}` object reference, and which are required.
 * Everything else in the recorded schema - titles, prose, nested field
 * descriptions, and in one case an entire C++ doc comment pasted into a
 * `default` - is documentation that already reached the caller through the
 * action's description and its declared parameters. Emitting it again would
 * put most of a megabyte of dead weight in the repository and in the bundle.
 *
 * The full schema stays in the recorded catalog, which is where it belongs.
 */
function minifySchema(input) {
  const props = input?.properties ?? {};
  const out = { properties: {} };
  for (const [name, prop] of Object.entries(props)) out.properties[name] = minifyProp(prop, 0);
  if (Array.isArray(input?.required) && input.required.length > 0) out.required = input.required;
  return out;
}

/**
 * One property, keeping what a caller needs to build a valid value: its type,
 * its enum, and the item and field shapes of an array or struct (#1281).
 */
function minifyProp(prop, depth) {
  if (isAssetRef(prop)) return { type: "object", properties: { refPath: {} } };
  if (!prop || typeof prop !== "object" || !prop.type) return {};
  const out = { type: prop.type };
  if (Array.isArray(prop.enum) && prop.enum.length > 0) out.enum = prop.enum;
  if (depth >= 4) return out;
  if (prop.type === "array" && prop.items && typeof prop.items === "object") {
    const items = minifyProp(prop.items, depth + 1);
    if (Object.keys(items).length > 0) out.items = items;
  }
  if (prop.type === "object" && prop.properties && typeof prop.properties === "object") {
    const fields = Object.entries(prop.properties).map(([k, v]) => [k, minifyProp(v, depth + 1)]);
    if (fields.length > 0) out.properties = Object.fromEntries(fields);
  }
  return out;
}

/* ── emit ───────────────────────────────────────────────────────────────── */

const q = (s) => JSON.stringify(s);

function main() {
  const snapshots = loadSnapshots(CATALOG);
  const merged = mergeSnapshots(snapshots);
  const effects = loadEffects();

  // Oldest engine first, so a tool an older engine already had keeps its key
  // when a newer engine adds a same-named tool to the category.
  const entries = merged.toolsets
    .flatMap((ts) => ts.tools.map((tool) => ({ ts, tool })))
    .sort((a, b) => compareEngines(a.tool.engines[0], b.tool.engines[0]));

  const successorToolset = successorToolsets(merged.toolsets);

  // category -> { actions: [...], params: Map<name, {kinds:Set, description}> }
  const byCategory = new Map();
  const missingEffects = [];
  let total = 0;

  for (const { ts, tool } of entries) {
    const category = routeToolset(ts.name);
    if (!byCategory.has(category)) byCategory.set(category, { actions: [], params: new Map(), keys: new Set(), needsInput: false });
    const bucket = byCategory.get(category);
    total++;
    const effect = effects.get(tool.name);
    if (!effect) { missingEffects.push(tool.name); continue; }

    let key = actionKey(bareToolName(tool.name));
    // The same tool under a newer engine's toolset, where the older one is gone:
    // a successor of the existing action rather than a second, suffixed one, so
    // the action keeps its name and reaches whichever tool the editor carries.
    const prior = bucket.actions.find((a) => (a.key === key || a.key.startsWith(`${key}__`))
      && successorToolset.get(a.toolset) === ts.name
      && disjoint(a.engines.concat(a.successors.flatMap((s) => s.engines)), tool.engines));
    if (prior) {
      addParams(bucket, tool, tool.inputSchema ?? {});
      prior.successors.push({
        toolset: ts.name,
        tool: tool.name,
        engines: tool.engines,
        input: tool.inputSchema ?? {},
        rename: renameMap(prior.input, tool.inputSchema ?? {}),
      });
      prior.effect = strongerEffect(prior.effect, effect);
      continue;
    }
    if (bucket.keys.has(key)) {
      // Two toolsets in one category shipping the same bare name. Qualify
      // with the toolset's own short name so both stay reachable.
      const disc = actionKey(bareToolName(ts.name)).replace(/^epic_/, "");
      key = `${key}__${disc}`;
      if (bucket.keys.has(key)) continue;
    }
    bucket.keys.add(key);

    const input = tool.inputSchema ?? {};
    addParams(bucket, tool, input);

    bucket.actions.push({
      key,
      effect,
      toolset: ts.name,
      tool: tool.name,
      engines: tool.engines,
      summary: prose(tool.description) || bareToolName(tool.name),
      input,
      successors: [],
    });
  }

  // Descriptions last, once every successor is known: availability is the union
  // of the engines the action reaches, and each successor names its own params.
  for (const bucket of byCategory.values()) {
    for (const a of bucket.actions) {
      const reach = [...a.engines, ...a.successors.flatMap((s) => s.engines)];
      const later = a.successors
        .map((s) => `On UE ${s.engines.join(", ")} it calls ${s.tool} (${paramsClause(s.input)}).`)
        .join(" ");
      a.description = `[Epic ${a.toolset}] ${a.summary} ${availabilityClause(reach, merged.engines)}${paramsClause(a.input)}${later ? `. ${later}` : ""}`
        .replace(/\s+/g, " ").trim();
    }
  }

  if (missingEffects.length > 0) {
    throw new Error(
      `${missingEffects.length} tool(s) in the catalog have no entry in src/tools/epic/effects.ts:\n`
      + missingEffects.slice(0, 20).map((n) => `  ${n}`).join("\n")
      + (missingEffects.length > 20 ? `\n  ...and ${missingEffects.length - 20} more` : "")
      + "\n\nEvery action states what it does to the editor. Add each one with the effect it has, "
      + "rather than letting this generator invent one from the name.",
    );
  }

  fs.mkdirSync(OUT_DIR, { recursive: true });
  const written = [];
  for (const [category, bucket] of [...byCategory].sort((a, b) => a[0].localeCompare(b[0]))) {
    bucket.actions.sort((a, b) => a.key.localeCompare(b.key));
    const file = path.join(OUT_DIR, `${category}.generated.ts`);
    fs.writeFileSync(file, emitCategory(category, bucket));
    written.push([category, bucket.actions.length, bucket.params.size]);
  }

  const index = [...byCategory.keys()].sort();
  fs.writeFileSync(path.join(OUT_DIR, "index.ts"), emitIndex(index));
  fs.writeFileSync(path.join(OUT_DIR, "catalog-engines.ts"), emitEngines(merged.engines));
  fs.writeFileSync(path.join(OUT_DIR, "toolset-routes.ts"), emitRoutes(merged.toolsets));

  console.log(`generated ${total} actions across ${written.length} categories from UE ${merged.engines.join(", ")}`);
  for (const [cat, n, p] of written) console.log(`  ${cat.padEnd(14)} ${String(n).padStart(4)} actions, ${p} parameters`);
}

/**
 * Where a tool exists, stated in its description only when that is not every
 * recorded engine, so a single-engine catalog adds nothing to the surface.
 */
function availabilityClause(engines, all) {
  const absent = all.filter((e) => !engines.includes(e));
  if (absent.length === 0) return "";
  return `Available on UE ${engines.join(", ")}; absent from UE ${absent.join(", ")}. `;
}

function loadEffects() {
  const file = EFFECTS;
  if (!fs.existsSync(file)) return new Map();
  // Read the declarations out of the source rather than importing it: this
  // script runs against a tree whose generated modules may not compile yet.
  const src = fs.readFileSync(file, "utf8");
  const map = new Map();
  for (const m of src.matchAll(/^\s*(?:"([^"]+)"|'([^']+)'):\s*"(read|mutate|unknown)"/gm)) {
    map.set(m[1] ?? m[2], m[3]);
  }
  return map;
}

function emitCategory(category, bucket) {
  const header =
`// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

`;

  // The nested escape, declared only where a wrapped tool needs it.
  if (bucket.needsInput) {
    if (!bucket.params.has("input")) {
      bucket.params.set("input", {
        kinds: new Set(["object"]),
        description: "The wrapped tool's arguments as an object. Use it for an argument this category cannot take at the top level because its dispatcher owns that name.",
      });
    }
    if (!bucket.params.has("inputJson")) {
      bucket.params.set("inputJson", {
        kinds: new Set(["string"]),
        description: "The wrapped tool's arguments as a raw JSON string. Takes full control: nothing is merged in when it is set.",
      });
    }
  }

  const schemas = [...bucket.params.entries()]
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([name, { kinds, description }]) => {
      const desc = description ? `.describe(${q(truncate(description, 160))})` : "";
      return `  ${safeKey(name)}: ${zodFor(kinds)}.optional()${desc},`;
    })
    .join("\n");

  const actions = bucket.actions
    .map((a) => {
      const consts = [`const ${constName(a.key)} = ${JSON.stringify(minifySchema(a.input))} as const;`];
      const successors = a.successors.map((s, i) => {
        const name = `${constName(a.key)}__${i + 1}`;
        consts.push(`const ${name} = ${JSON.stringify(minifySchema(s.input))} as const;`);
        const rename = s.rename ? `, rename: ${JSON.stringify(s.rename)}` : "";
        return `{ toolset: ${q(s.toolset)}, name: ${q(s.tool)}, engines: ${q(s.engines)}, schema: ${name}${rename} }`;
      });
      const succ = successors.length > 0 ? `, successors: [${successors.join(", ")}]` : "";
      return { schemaConst: consts.join("\n"), entry:
`  ${safeKey(a.key)}: {
    epicSchema: ${constName(a.key)},
    epicTool: { toolset: ${q(a.toolset)}, name: ${q(a.tool)}, engines: ${q(a.engines)}${succ} },
    ...bp(${q(a.effect)}, ${q(a.description)}, "epic_call_tool"),
  },` };
    });

  return header
    + actions.map((a) => a.schemaConst).join("\n")
    + `\n\n/** ${bucket.actions.length} wrapped engine tools routed to the \`${category}\` category. */\n`
    + `export const actions: Record<string, ActionSpec> = {\n`
    + actions.map((a) => a.entry).join("\n")
    + `\n};\n\n`
    + `/** The parameters those actions accept, declared so the MCP layer stops stripping them. */\n`
    + `export const schema: Record<string, z.ZodType> = {\n${schemas}\n};\n`;
}

function emitEngines(engines) {
  return `// GENERATED FILE - do not edit. See scripts/generate-epic-actions.mjs.

/** Engine versions with a recorded toolset catalog, oldest first. */
export const EPIC_CATALOG_ENGINES: readonly string[] = ${q(engines)};
`;
}

/**
 * The routing table as a runtime module: the patterns themselves, so a toolset
 * an editor registers that no catalog recorded still routes, and the recorded
 * toolsets per category, so a category maps to its toolsets with no editor up.
 */
function emitRoutes(toolsets) {
  const byCategory = new Map();
  for (const ts of toolsets) {
    const category = routeToolset(ts.name);
    if (!byCategory.has(category)) byCategory.set(category, []);
    byCategory.get(category).push(ts.name);
  }
  const rows = ROUTES.map(([re, cat]) => `  [${re.toString()}, ${q(cat)}],`).join("\n");
  const cats = [...byCategory]
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([cat, names]) => `  ${safeKey(cat)}: ${q([...names].sort())},`)
    .join("\n");
  return `// GENERATED FILE - do not edit. See scripts/generate-epic-actions.mjs.
//
// The routing the generator uses to place each Epic toolset in a ue-mcp
// category, emitted so the server applies the same rule at runtime.

/** Pattern to category, first match wins. A toolset matching none belongs to \`epic\`. */
export const EPIC_TOOLSET_ROUTES: ReadonlyArray<readonly [RegExp, string]> = [
${rows}
];

/** The ue-mcp category an Epic toolset belongs in. */
export function routeEpicToolset(name: string): string {
  for (const [re, category] of EPIC_TOOLSET_ROUTES) if (re.test(name)) return category;
  return "epic";
}

/** Every recorded toolset, by the category it routes to. */
export const EPIC_TOOLSETS_BY_CATEGORY: Readonly<Record<string, readonly string[]>> = {
${cats}
};
`;
}

function emitIndex(categories) {
  return `// GENERATED FILE - do not edit. See scripts/generate-epic-actions.mjs.
${categories.map((c) => `import * as ${ident(c)} from "./${c}.generated.js";`).join("\n")}

/** Every category that Unreal's toolset registry contributes actions to. */
export const EPIC_CATEGORIES = {
${categories.map((c) => `  ${c}: ${ident(c)},`).join("\n")}
} as const;

export type EpicCategoryName = keyof typeof EPIC_CATEGORIES;
`;
}

const ident = (s) => s.replace(/[^A-Za-z0-9_]/g, "_");
const constName = (key) => `S_${key.replace(/[^A-Za-z0-9_]/g, "_")}`;
const safeKey = (name) => (/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(name) ? name : q(name));
const truncate = (s, n) => (s.length <= n ? s : `${s.slice(0, n - 1).trimEnd()}…`);

try {
  main();
} catch (e) {
  console.error(String(e?.message ?? e));
  process.exit(1);
}
