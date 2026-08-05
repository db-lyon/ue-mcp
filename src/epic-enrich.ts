import { z } from "zod";
import { categoryTool, type ToolDef, type ActionSpec } from "./types.js";
import { McpError, ErrorCode } from "./errors.js";
import { coerceAssetPathValue } from "./asset-path.js";

/**
 * First-class surfacing of Epic's native UE 5.8 toolsets.
 *
 * The `epic` category is the discovery gateway (list/describe/call). This module
 * goes further: it takes the *live* toolset catalog and injects each Epic tool
 * as a real action into the ue-mcp category an agent would expect it in, so an
 * agent doing GAS work sees Epic's GAS tools in the `gas` tool alongside the
 * native ones. Toolsets with no natural ue-mcp home stay under `epic`.
 *
 * Enrichment mutates the category ToolDefs in place and must run BEFORE the flow
 * registry and MCP tool registration are built, since it grows each category's
 * action enum and its dispatch map.
 */

// ── Toolset → ue-mcp category routing ─────────────────────────────────────────
// Ordered keyword rules matched against the qualified toolset name (which is
// inconsistent across Epic's C++ vs Python toolsets, e.g.
// "GASToolsets.AttributeSetToolset" vs "editor_toolset.toolsets.actor.ActorTools").
// First match wins. Targets must be real ue-mcp category names; anything
// unmatched falls through to the `epic` umbrella so nothing is ever dropped.
interface Rule { test: RegExp; category: string; }
const ROUTES: Rule[] = [
  // PhysicsAsset authoring must be tested before the generic asset rule below:
  // "PhysicsAssetToolset" contains the substring "assettools", so it would
  // otherwise be swept into `asset` by accident. Physics belongs with the
  // native collision/simulation actions, which live in `gameplay`.
  { test: /physicsasset|physicstoolset/i, category: "gameplay" },
  { test: /gas|abilitysystem|gameplaycue|attributeset/i, category: "gas" },
  { test: /niagara/i, category: "niagara" },
  { test: /\bpcg\b|pcgspatial|pcgtoolset/i, category: "pcg" },
  { test: /umg|\bwidget/i, category: "widget" },
  { test: /state_?tree/i, category: "statetree" },
  { test: /controlrig|sequencer|keyframing|\banimation/i, category: "animation" },
  { test: /gameplaytags/i, category: "gameplay" },
  { test: /material/i, category: "material" },
  { test: /landscape/i, category: "landscape" },
  { test: /foliage/i, category: "foliage" },
  { test: /\.blueprint\.|blueprinttools/i, category: "blueprint" },
  { test: /\.actor\.|actortools/i, category: "level" },
  { test: /\.asset\.|assettools|data_?asset|curve_?table|dataregistry|data_?registry/i, category: "asset" },
  // Appended rules: every one of these matched nothing above, so placing them
  // last cannot change an existing route (first match wins). They exist because
  // an agent doing static-mesh or plugin work should find Epic's tools in the
  // category it is already in, not have to detour through the epic umbrella.
  { test: /skeletal_?mesh/i, category: "animation" },
  { test: /static_?mesh|\btexture|data_?table|string_?table|semanticsearch/i, category: "asset" },
  { test: /\.scene\.|scenetools|\.primitive\.|primitivetools/i, category: "level" },
  { test: /\.object\.|objecttools/i, category: "reflection" },
  { test: /behavior_?tree|worldcondition/i, category: "gameplay" },
  { test: /slateinspector/i, category: "widget" },
  { test: /plugintoolset|gamefeatures/i, category: "plugins" },
  { test: /editorapptoolset|logstoolset/i, category: "editor" },
  { test: /configsettings|automationtest/i, category: "project" },
  // Domains Unreal exposes that ue-mcp has no native handlers for. They still
  // get a real category rather than the umbrella: an agent authoring a Dataflow
  // graph should reach for `dataflow`, not go rummaging in `epic`. The category
  // is materialised on demand (see EPIC_ONLY_DOMAINS) so it exists exactly when
  // the engine actually ships the toolset.
  { test: /dataflow/i, category: "dataflow" },
  { test: /conversation/i, category: "conversation" },
];

/**
 * Categories that exist only because Epic ships the toolset - ue-mcp has no
 * native C++ handlers behind them. They are created during enrichment instead
 * of being declared in ALL_TOOLS, so on an engine without the toolset the
 * category simply does not appear rather than advertising an empty action list.
 */
const EPIC_ONLY_DOMAINS: Record<string, string> = {
  dataflow:
    "Dataflow graphs: node and pin authoring, variables, comment boxes, templates, " +
    "and creation of Dataflow-compatible assets (Chaos geometry and simulation graphs).",
  conversation:
    "Conversation graphs (UConversationDatabase): dialogue nodes, node connections, " +
    "sub-nodes, speakers, and entry points.",
};

/** Resolve the ue-mcp category for an Epic toolset name, or null for the umbrella. */
export function routeToolset(toolsetName: string): string | null {
  for (const r of ROUTES) {
    if (r.test.test(toolsetName)) return r.category;
  }
  return null;
}

/** Every category name a routing rule can produce. */
export function routedCategories(): string[] {
  return [...new Set(ROUTES.map((r) => r.category))];
}

// ── Catalog types (shape of epic_list_toolsets includeSchemas=true) ───────────
export interface EpicTool {
  name: string;
  description?: string;
  inputSchema?: { properties?: Record<string, unknown>; required?: string[] };
}
export interface EpicToolset {
  name: string;
  version?: string;
  description?: string;
  tools?: EpicTool[];
}
export interface EpicCatalog { toolsets?: EpicToolset[]; }

/**
 * Replace em dashes with hyphens (CLAUDE.md style rule for public artifacts).
 * Upstream tool descriptions use them freely, and these strings are surfaced
 * verbatim to every connected agent and republished as docs/native-tools.md,
 * so they are sanitised here rather than by rewriting the catalog snapshot.
 * Doing it on this path is what keeps the doc generator's zero-drift promise:
 * the page runs this same enrichment, so page and runtime cannot diverge.
 * The literal in the pattern is the character being stripped and has to stay.
 */
function deEm(s: string): string {
  return s.replace(/\s*—\s*/g, " - ");
}

// ── Naming ────────────────────────────────────────────────────────────────────
function bareToolName(qualified: string): string {
  const idx = qualified.lastIndexOf(".");
  return idx >= 0 ? qualified.slice(idx + 1) : qualified;
}

/** PascalCase/camelCase → snake_case, prefixed `epic_`. */
function actionKey(bare: string): string {
  const snake = bare
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/[^A-Za-z0-9]+/g, "_")
    .replace(/_+/g, "_")
    .replace(/^_|_$/g, "")
    .toLowerCase();
  return `epic_${snake}`;
}

function paramHint(tool: EpicTool): string {
  const props = tool.inputSchema?.properties;
  if (!props) return "";
  const names = Object.keys(props);
  if (names.length === 0) return " Params: none.";
  const req = new Set(tool.inputSchema?.required ?? []);
  const rendered = names.map((n) => (req.has(n) ? n : `${n}?`)).join(", ");
  return ` Params (pass as input): ${rendered}.`;
}

// ── Input envelope ────────────────────────────────────────────────────────────
// A wrapped engine tool takes its arguments as one nested object, while the
// surrounding ue-mcp category takes flat, canonical parameters. Passing a flat
// parameter used to reach the engine as an empty input object, which came back
// as "input params Json is empty" with no clue about what to send instead
// (#798). Build the envelope here: honour an explicit `input`, fold in the
// top-level parameters the tool's own schema names, fill an asset reference
// from the category's canonical `assetPath`, and refuse the call with the
// required shape when something is still missing.

interface SchemaProp {
  type?: string;
  properties?: Record<string, unknown>;
  required?: string[];
}

function asProp(value: unknown): SchemaProp | undefined {
  return value && typeof value === "object" ? (value as SchemaProp) : undefined;
}

/** True when a schema property is the engine's `{ refPath }` object reference. */
function isAssetRefProp(prop: SchemaProp | undefined): boolean {
  return !!prop && prop.type === "object" && !!prop.properties && "refPath" in prop.properties;
}

function parseObjectish(value: unknown): unknown {
  if (typeof value !== "string") return value;
  const trimmed = value.trim();
  if (!trimmed.startsWith("{") && !trimmed.startsWith("[")) return value;
  try {
    return JSON.parse(trimmed);
  } catch {
    return value;
  }
}

/** Render one required property as the JSON a caller should send for it. */
function exampleFor(name: string, prop: SchemaProp | undefined): string {
  if (isAssetRefProp(prop)) return `"${name}": { "refPath": "/Game/UI/WBP_Example" }`;
  if (prop?.type === "number" || prop?.type === "integer") return `"${name}": 0`;
  if (prop?.type === "boolean") return `"${name}": false`;
  if (prop?.type === "array") return `"${name}": []`;
  if (prop?.type === "object") return `"${name}": {}`;
  return `"${name}": "..."`;
}

/**
 * Build the `input` object for one wrapped engine tool call.
 * Returns the arguments to hand the bridge; throws INVALID_PARAMS when the
 * tool's required arguments cannot be assembled from what the caller sent.
 */
export function resolveEpicToolInput(
  tool: EpicTool,
  params: Record<string, unknown>,
): { input?: unknown; inputJson?: unknown } {
  // An explicit raw JSON string is the caller taking full control.
  if (typeof params.inputJson === "string" && params.inputJson.trim() !== "") {
    return { inputJson: params.inputJson };
  }

  const schema = tool.inputSchema;
  const props = schema?.properties;
  if (!props) return { input: params.input, inputJson: params.inputJson };

  const given = parseObjectish(params.input);
  const input: Record<string, unknown> =
    given && typeof given === "object" && !Array.isArray(given)
      ? { ...(given as Record<string, unknown>) }
      : {};

  const propNames = Object.keys(props);
  const required = schema?.required ?? [];

  // 1. Top-level parameters the tool's own schema names.
  for (const name of propNames) {
    if (input[name] !== undefined) continue;
    const flat = params[name];
    if (flat === undefined || flat === null || flat === "") continue;
    const prop = asProp(props[name]);
    let value = parseObjectish(flat);
    if (isAssetRefProp(prop) && typeof value === "string") {
      value = { refPath: value };
    }
    input[name] = value;
  }

  // 2. The category's canonical asset path fills the tool's asset reference,
  //    but only when exactly one is still unresolved, so nothing is guessed.
  const unresolvedRefs = propNames.filter((n) => input[n] === undefined && isAssetRefProp(asProp(props[n])));
  const canonicalAsset = coerceAssetPathValue(params.assetPath) ?? coerceAssetPathValue(params.path);
  if (unresolvedRefs.length === 1 && canonicalAsset) {
    input[unresolvedRefs[0]] = { refPath: canonicalAsset };
  }

  // 3. Refuse rather than dispatch a call the engine will reject anyway.
  const missing = required.filter((n) => input[n] === undefined);
  if (missing.length > 0) {
    const known = new Set([...propNames, "action", "input", "inputJson"]);
    const ignored = Object.keys(params).filter(
      (k) => !known.has(k) && params[k] !== undefined && params[k] !== null,
    );
    const shape = missing.map((n) => exampleFor(n, asProp(props[n]))).join(", ");
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      `${tool.name} is missing required argument(s): ${missing.join(", ")}. ` +
      `Pass them in 'input', for example {"input": {${shape}}}.` +
      (ignored.length > 0
        ? ` These top-level parameters are not arguments of this tool and were not sent: ${ignored.join(", ")}.`
        : ""),
    );
  }

  return { input };
}

// ── Enrichment ─────────────────────────────────────────────────────────────────
export interface EnrichResult {
  injected: number;
  byCategory: Record<string, number>;
  /** Categories created during this run because only Epic supplies them. */
  createdCategories: string[];
}

/**
 * Inject the Epic catalog's tools into the matching category ToolDefs.
 * `tools` is the live category list (mutated in place). `epicFallback` is the
 * `epic` category ToolDef used for toolsets with no natural home. Returns a
 * summary for logging.
 */
export function enrichToolsWithEpicCatalog(
  tools: ToolDef[],
  catalog: EpicCatalog,
  opts: { epicCategoryName?: string; excludeCategories?: string[] } = {},
): EnrichResult {
  const epicName = opts.epicCategoryName ?? "epic";
  const excluded = new Set(opts.excludeCategories ?? []);
  const byName = new Map(tools.map((t) => [t.name, t]));
  const epicTool = byName.get(epicName);
  const result: EnrichResult = { injected: 0, byCategory: {}, createdCategories: [] };
  if (!catalog?.toolsets?.length) return result;

  // Track added action keys per category to dedupe.
  const usedKeys = new Map<string, Set<string>>();
  const keysFor = (cat: string): Set<string> => {
    let s = usedKeys.get(cat);
    if (!s) {
      const t = byName.get(cat);
      s = new Set(t ? Object.keys(t.actions) : []);
      usedKeys.set(cat, s);
    }
    return s;
  };

  const touched = new Set<string>();

  // A routed category that ue-mcp does not declare natively is materialised
  // here, on first use, rather than shipped as an empty stub in ALL_TOOLS. The
  // seed action only exists to satisfy the non-empty action enum that
  // categoryTool builds; it is removed immediately and the enum is rebuilt
  // below once the category's real actions are in.
  const SEED = "__epic_seed__";
  const ensureCategory = (cat: string): ToolDef | undefined => {
    const existing = byName.get(cat);
    if (existing) return existing;
    const summary = EPIC_ONLY_DOMAINS[cat];
    if (summary === undefined) return undefined;
    const created = categoryTool(cat, summary, { [SEED]: { bridge: SEED } });
    delete created.actions[SEED];
    byName.set(cat, created);
    tools.push(created);
    result.createdCategories.push(cat);
    return created;
  };

  for (const ts of catalog.toolsets) {
    if (!ts?.name || !ts.tools?.length) continue;
    const targetCat = routeToolset(ts.name) ?? epicName;
    // Excluded categories are not enriched (tools stay reachable via the epic
    // gateway's call_tool). Excluding the epic umbrella drops unrouted tools.
    if (excluded.has(targetCat)) continue;
    const target = ensureCategory(targetCat) ?? epicTool;
    if (!target) continue;

    const keys = keysFor(target.name);
    for (const tool of ts.tools) {
      if (!tool?.name) continue;
      const bare = bareToolName(tool.name);
      let key = actionKey(bare);
      if (keys.has(key)) {
        // Collision across toolsets in the same category: qualify with the
        // toolset's short name so both remain reachable.
        const disc = actionKey(bareToolName(ts.name)).replace(/^epic_/, "");
        key = `${key}__${disc}`;
        if (keys.has(key)) continue; // give up on a double collision
      }
      keys.add(key);

      const qualifiedTool = tool.name;
      const toolsetName = ts.name;
      const desc = deEm(
        `[Epic ${toolsetName}] ${tool.description ?? bare}`.replace(/\s+/g, " ").trim() +
          paramHint(tool),
      );

      const spec: ActionSpec = {
        description: desc,
        bridge: "epic_call_tool",
        mapParams: (p: Record<string, unknown>) => ({
          toolset: toolsetName,
          tool: qualifiedTool,
          ...resolveEpicToolInput(tool, p),
        }),
      };
      target.actions[key] = spec;
      result.injected++;
      result.byCategory[target.name] = (result.byCategory[target.name] ?? 0) + 1;
      touched.add(target.name);
    }
  }

  // Rebuild the action enum + shared input schema + description for every
  // category that gained actions, so MCP advertises the new actions.
  for (const catName of touched) {
    const t = byName.get(catName);
    if (!t) continue;
    const actionNames = Object.keys(t.actions) as [string, ...string[]];
    t.schema.action = z.enum(actionNames).describe("Action to perform");
    if (!t.schema.input) {
      t.schema.input = z.record(z.unknown()).optional()
        .describe("Epic tool arguments as a JSON object (for epic_* actions)");
    }
    if (!t.schema.inputJson) {
      t.schema.inputJson = z.string().optional()
        .describe("Epic tool arguments as a raw JSON string (alternative to input)");
    }
    const added = result.byCategory[catName] ?? 0;
    t.description += `\n\nEpic 5.8 toolset actions (${added}): the epic_* actions above wrap Unreal's native ToolsetRegistry tools for this domain. Pass tool arguments via 'input'. A top-level parameter named by the wrapped tool's own schema is folded into 'input' for you, as is this category's canonical asset path when the tool takes a single asset reference. A call that is still missing a required argument is refused with the exact shape to send, instead of being dispatched (#798).`;
  }

  return result;
}
