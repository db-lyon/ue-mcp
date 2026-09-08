#!/usr/bin/env node
/**
 * Generate first-class action declarations for Unreal's AI Toolset Registry.
 *
 *     npm run epic:generate
 *
 * Reads two checked-in inputs and writes one generated module per category:
 *
 *   tests/golden/epic-catalog.json   what the registry contains, recorded from
 *                                    a live editor by `npm run epic:record`.
 *   src/tools/epic/effects.ts        what each tool DOES, decided by a person.
 *
 * The split is the point. The catalog is Epic's, and re-recording it must never
 * silently discard a judgement somebody made; the effects file is ours, is
 * hand-edited, and is the artifact a reviewer reads. A tool in the catalog with
 * no entry in the effects file stops this script rather than being guessed at.
 *
 * What comes out is an ordinary category action record: `bp("read", "...",
 * "epic_call_tool", mapParams)` plus the zod parameters those actions accept.
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

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const CATALOG = path.join(ROOT, "tests", "golden", "epic-catalog.json");
const OUT_DIR = path.join(ROOT, "src", "tools", "epic");

/* ── routing ────────────────────────────────────────────────────────────────
 * Which ue-mcp category an Epic toolset belongs in. Kept here rather than
 * imported from src/ because this script runs before the module it generates
 * exists, and a generator that cannot run on a broken tree is no use.
 * `tests/unit/epic-generated.test.ts` asserts these stay in step with the
 * routes the server publishes.
 */
const ROUTES = [
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

/* ── descriptions ───────────────────────────────────────────────────────── */

// Epic's descriptions are Python docstrings: a sentence, then an indented
// "Args:"/"Returns:" block that repeats the schema we are about to declare
// properly. Keep the prose, drop the block, collapse the whitespace.
function prose(description) {
  const text = String(description ?? "").split(/\n\s*(?:Args|Arguments|Returns|Raises|Note):/)[0];
  return text
    .replace(/\s*—\s*/g, " - ") // em-dash-allowed: the literal is the character being stripped
    .replace(/\s+/g, " ")
    .trim();
}

/** The `Params:` clause every action on this surface is required to carry. */
function paramsClause(inputSchema) {
  const props = Object.keys(inputSchema?.properties ?? {});
  if (props.length === 0) return "Params: none";
  const required = new Set(inputSchema?.required ?? []);
  return `Params: ${props.map((n) => (required.has(n) ? n : `${n}?`)).join(", ")}`;
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
    case "assetRef": return "z.union([z.string(), z.record(z.unknown())])";
    case "string": return "z.string()";
    case "number": case "integer": return "z.number()";
    case "boolean": return "z.boolean()";
    case "array": return "z.array(z.unknown())";
    case "object": return "z.record(z.unknown())";
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
  for (const [name, prop] of Object.entries(props)) {
    out.properties[name] = isAssetRef(prop)
      ? { type: "object", properties: { refPath: {} } }
      : (prop?.type ? { type: prop.type } : {});
  }
  if (Array.isArray(input?.required) && input.required.length > 0) out.required = input.required;
  return out;
}

/* ── emit ───────────────────────────────────────────────────────────────── */

const q = (s) => JSON.stringify(s);

function main() {
  if (!fs.existsSync(CATALOG)) {
    throw new Error(`No catalog at ${path.relative(ROOT, CATALOG)}. Run \`npm run epic:record\` against a 5.8 editor first.`);
  }
  const catalog = JSON.parse(fs.readFileSync(CATALOG, "utf8"));

  const effects = loadEffects();

  // category -> { actions: [...], params: Map<name, {kinds:Set, description}> }
  const byCategory = new Map();
  const missingEffects = [];
  let total = 0;

  for (const ts of catalog.toolsets ?? []) {
    const category = routeToolset(ts.name);
    if (!byCategory.has(category)) byCategory.set(category, { actions: [], params: new Map(), keys: new Set() });
    const bucket = byCategory.get(category);

    for (const tool of ts.tools ?? []) {
      if (!tool?.name) continue;
      total++;
      const effect = effects.get(tool.name);
      if (!effect) { missingEffects.push(tool.name); continue; }

      let key = actionKey(bareToolName(tool.name));
      if (bucket.keys.has(key)) {
        // Two toolsets in one category shipping the same bare name. Qualify
        // with the toolset's own short name so both stay reachable.
        const disc = actionKey(bareToolName(ts.name)).replace(/^epic_/, "");
        key = `${key}__${disc}`;
        if (bucket.keys.has(key)) continue;
      }
      bucket.keys.add(key);

      const input = tool.inputSchema ?? {};
      for (const [name, prop] of Object.entries(input.properties ?? {})) {
        if (!bucket.params.has(name)) bucket.params.set(name, { kinds: new Set(), description: "" });
        const entry = bucket.params.get(name);
        entry.kinds.add(kindOf(prop));
        if (!entry.description && typeof prop?.description === "string") {
          entry.description = prose(prop.description);
        }
      }

      const summary = prose(tool.description) || bareToolName(tool.name);
      bucket.actions.push({
        key,
        effect,
        toolset: ts.name,
        tool: tool.name,
        description: `[Epic ${ts.name}] ${summary} ${paramsClause(input)}`.replace(/\s+/g, " ").trim(),
        input,
      });
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

  console.log(`generated ${total} actions across ${written.length} categories`);
  for (const [cat, n, p] of written) console.log(`  ${cat.padEnd(14)} ${String(n).padStart(4)} actions, ${p} parameters`);
}

function loadEffects() {
  const file = path.join(OUT_DIR, "effects.ts");
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
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog.json
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
import { z } from "zod";
import { bp, type ActionSpec } from "../../types.js";
import { epicToolCall } from "../../epic-input.js";

`;

  const schemas = [...bucket.params.entries()]
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([name, { kinds, description }]) => {
      const desc = description ? `.describe(${q(truncate(description, 160))})` : "";
      return `  ${safeKey(name)}: ${zodFor(kinds)}.optional()${desc},`;
    })
    .join("\n");

  const actions = bucket.actions
    .map((a) => {
      const schemaConst = `const ${constName(a.key)} = ${JSON.stringify(minifySchema(a.input))} as const;`;
      return { schemaConst, entry:
`  ${safeKey(a.key)}: bp(
    ${q(a.effect)},
    ${q(a.description)},
    "epic_call_tool",
    (p) => epicToolCall(${q(a.toolset)}, ${q(a.tool)}, ${constName(a.key)}, p),
  ),` };
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
