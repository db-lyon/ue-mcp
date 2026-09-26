import { z } from "zod";
import type { ActionSpec, ToolDef } from "./core/types.js";
import { actionEnum, categoryTool } from "./category-tool.js";
import { actionSchema } from "./action-schema.js";
import { searchToolGraph } from "./tool-search.js";
import { actionSignature } from "./action-signature.js";
import { readEnv } from "./core/env.js";

/**
 * Lean context strategy, and what every strategy shares: choosing one, and
 * the signature pages and search results discovery answers with. The micro
 * gateway lives in micro-context.ts.
 *
 * Full mode advertises every action inline: each category tool's description
 * carries an "Actions:\n- ..." catalog and SERVER_INSTRUCTIONS lists all 600+
 * actions. That is great for discoverability but expensive on the MCP
 * initialize handshake for token-constrained clients.
 *
 * Lean mode keeps the exact same 24 typed category tools and their validated
 * `action` enums, but:
 *   - trims each tool description to its one-line summary + a discovery pointer,
 *   - trims the server instructions (see SERVER_INSTRUCTIONS_LEAN),
 *   - adds a per-category `describe` action that returns that category's action
 *     list on demand,
 *   - prepends a `catalog` discovery tool (search / describe / list_categories)
 *     so an agent can find any action across every category by keyword.
 *
 * The typed enum is deliberately retained (unlike a free-form string surface)
 * so unknown actions are still rejected up front. Silent param drift is the
 * failure mode this repo works hardest to avoid.
 */

export type ContextStrategy = "full" | "lean" | "micro";

/** The strategy a project gets when it names none (#1172). */
export const DEFAULT_CONTEXT_STRATEGY: ContextStrategy = "micro";

/**
 * Resolve the active strategy. Env var wins over config so a user can flip it
 * per-session without editing ue-mcp.yml. Anything other than "full"/"lean"
 * (case insensitive) resolves to "micro", the default since #1172: the full
 * surface cost most of a context window before the first call.
 */
export function resolveContextStrategy(configStrategy?: string): ContextStrategy {
  const raw = (readEnv("contextStrategy") ?? configStrategy ?? DEFAULT_CONTEXT_STRATEGY).trim().toLowerCase();
  return raw === "full" ? "full" : raw === "lean" ? "lean" : "micro";
}

const ACTIONS_MARKER = "\n\nActions:\n";

/** Split a categoryTool() description into its summary and the generated catalog. */
export function splitDescription(description: string): { summary: string; catalog: string } {
  const i = description.indexOf(ACTIONS_MARKER);
  if (i === -1) return { summary: description.trim(), catalog: "" };
  return {
    summary: description.slice(0, i).trim(),
    catalog: description.slice(i + ACTIONS_MARKER.length).trim(),
  };
}

/**
 * The full-mode description of a category tool (#1172): its summary, how to
 * call it, and one signature line per action. No per-action prose: a first
 * sentence per native action cost about 9k tokens and pushed the seed to its
 * 60k budget, and describe_action carries every description on demand.
 */
export function fullSurfaceDescription(tool: ToolDef): string {
  const { summary } = splitDescription(tool.description);
  const lines = Object.keys(tool.actions).map((name) => actionSignature(tool, name));
  return `${summary}\n\nCall ${tool.name}(action, args={...}). Actions:\n${lines.join("\n")}`;
}

/** Characters of signatures one describe page holds, about 1.5k tokens. */
export const DESCRIBE_PAGE_CHARS = 5600;

/** Longest signature a search hit carries; optional params past it become +N. */
export const SEARCH_SIGNATURE_CHARS = 140;

/**
 * One page of a category's signatures. A category can hold hundreds of
 * actions (animation wraps 341 engine tools), so the page is bounded by size
 * and `nextOffset` names where the next one starts.
 */
export function describeCategory(tool: ToolDef, offset = 0): Record<string, unknown> {
  const names = Object.keys(tool.actions);
  const start = Math.max(0, Math.min(Math.floor(offset), names.length));
  const signatures: string[] = [];
  let used = 0;
  let i = start;
  for (; i < names.length; i++) {
    const line = actionSignature(tool, names[i]);
    if (signatures.length > 0 && used + line.length > DESCRIBE_PAGE_CHARS) break;
    signatures.push(line);
    used += line.length;
  }
  return {
    category: tool.name,
    count: names.length,
    ...(start > 0 ? { offset: start } : {}),
    signatures,
    ...(i < names.length ? { nextOffset: i } : {}),
  };
}

export function readOffset(p: Record<string, unknown>): number {
  return typeof p.offset === "number" && p.offset > 0 ? p.offset : 0;
}

/** Produce the lean variant of a single category tool (non-mutating). */
function leanTool(tool: ToolDef): ToolDef {
  const { summary } = splitDescription(tool.description);

  // Preserve any pre-existing describe action rather than clobber it.
  const actions: Record<string, ActionSpec> = { ...tool.actions };
  if (!actions.describe) {
    actions.describe = {
      kind: "handler",
      effect: "read",
      description: `Signatures of the ${tool.name} category's actions, a page at a time. Params: offset?`,
      handler: async (_ctx, p) => describeCategory(tool, readOffset(p)),
    };
  }

  const actionNames = Object.keys(actions) as [string, ...string[]];
  const description =
    `${summary}\n\nCall ${tool.name}(action, args={...}). ` +
    `${tool.name}(action="describe") lists signatures; ` +
    `catalog(action="search", query="...") searches every category.`;

  return {
    ...tool,
    description,
    actions,
    schema: {
      ...tool.schema,
      action: actionEnum(actionNames),
    },
  };
}

/** Search hits as `category.signature` lines, the whole of what search returns (#1172). */
export function discoveryResults(tools: ToolDef[], query: string, limit: number): string[] {
  const byName = new Map(tools.map((t) => [t.name, t] as const));
  return searchToolGraph(tools, query, limit).map(({ tool, action }) => {
    const def = byName.get(tool);
    return `${tool}.${def ? actionSignature(def, action, { maxLength: SEARCH_SIGNATURE_CHARS }) : `${action}()`}`;
  });
}

/**
 * Build the `catalog` discovery tool from the pre-lean tools, so its search
 * index and describe output carry the full action descriptions even though the
 * leaned tools hide them.
 */
export function buildCatalogTool(tools: ToolDef[]): ToolDef {
  const summaries = tools.map((t) => ({ category: t.name, summary: splitDescription(t.description).summary }));
  const byName = new Map(tools.map((t) => [t.name, t] as const));

  const actions: Record<string, ActionSpec> = {
    search: {
      kind: "handler",
      effect: "read",
      description: "Rank actions across every category by keyword; returns signatures. Params: query, limit? (default 20)",
      handler: async (_ctx, p) => {
        const query = typeof p.query === "string" ? p.query : "";
        const limit = typeof p.limit === "number" && p.limit > 0 ? Math.min(p.limit, 100) : 20;
        if (!query.trim()) {
          return { error: 'Provide a "query" string, e.g. catalog(action="search", query="spawn actor").' };
        }
        const results = discoveryResults(tools, query, limit);
        return { query, count: results.length, results };
      },
    },
    describe: {
      kind: "handler",
      effect: "read",
      description: "A category's action signatures a page at a time, or one action's parameter schema. Params: category, method?, offset?",
      handler: async (_ctx, p) => {
        const category = typeof p.category === "string" ? p.category : "";
        const tool = byName.get(category);
        if (!tool) {
          return { error: `Unknown category "${category}". Use catalog(action="list_categories").`, categories: summaries.map((s) => s.category) };
        }
        if (typeof p.method === "string" && p.method) return actionSchema(tool, p.method);
        return describeCategory(tool, readOffset(p));
      },
    },
    list_categories: {
      kind: "handler",
      effect: "read",
      description: "List all category tools with their one-line summaries.",
      handler: async () => ({ count: summaries.length, categories: summaries }),
    },
  };

  return categoryTool(
    "catalog",
    "Discovery for lean context mode: search and describe the full action catalog on demand.",
    actions,
    {
      query: z.string().optional().describe("Keyword query for action=search"),
      category: z.string().optional().describe("Category name for action=describe"),
      method: z.string().optional().describe("describe: return only this action's parameter schema"),
      offset: z.number().int().min(0).optional().describe("describe: first signature of the page (nextOffset)"),
      limit: z.number().int().min(1).max(100).optional().describe("Max results for action=search (default 20)"),
    },
    { flatSurface: true },
  );
}

/**
 * Apply the lean strategy to a set of category tools. Returns a new array
 * (catalog tool first); the input tools are not mutated. When two categories
 * already contain a `catalog` tool (they never do today) the caller-supplied
 * one wins; we skip prepending a duplicate.
 */
export function applyLeanContext(tools: ToolDef[]): ToolDef[] {
  const leaned = tools.map(leanTool);
  if (tools.some((t) => t.name === "catalog")) return leaned;
  return [buildCatalogTool(tools), ...leaned];
}
