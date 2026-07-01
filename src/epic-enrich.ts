import { z } from "zod";
import type { ToolDef, ActionSpec } from "./types.js";

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
];

/** Resolve the ue-mcp category for an Epic toolset name, or null for the umbrella. */
export function routeToolset(toolsetName: string): string | null {
  for (const r of ROUTES) {
    if (r.test.test(toolsetName)) return r.category;
  }
  return null;
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

// ── Enrichment ─────────────────────────────────────────────────────────────────
export interface EnrichResult {
  injected: number;
  byCategory: Record<string, number>;
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
  const result: EnrichResult = { injected: 0, byCategory: {} };
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

  for (const ts of catalog.toolsets) {
    if (!ts?.name || !ts.tools?.length) continue;
    const targetCat = routeToolset(ts.name) ?? epicName;
    // Excluded categories are not enriched (tools stay reachable via the epic
    // gateway's call_tool). Excluding the epic umbrella drops unrouted tools.
    if (excluded.has(targetCat)) continue;
    const target = byName.get(targetCat) ?? epicTool;
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
      const desc =
        `[Epic ${toolsetName}] ${tool.description ?? bare}`.replace(/\s+/g, " ").trim() +
        paramHint(tool);

      const spec: ActionSpec = {
        description: desc,
        bridge: "epic_call_tool",
        mapParams: (p: Record<string, unknown>) => ({
          toolset: toolsetName,
          tool: qualifiedTool,
          input: p.input,
          inputJson: p.inputJson,
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
    t.description += `\n\nEpic 5.8 toolset actions (${added}): the epic_* actions above wrap Unreal's native ToolsetRegistry tools for this domain. Pass tool arguments via 'input'.`;
  }

  return result;
}
