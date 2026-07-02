import { z } from "zod";
import { categoryTool, type ActionSpec, type ToolDef } from "./types.js";

/**
 * Lean context strategy (Claireon-inspired).
 *
 * Full mode advertises every action inline: each category tool's description
 * carries an "Actions:\n- ..." catalog and SERVER_INSTRUCTIONS lists all 600+
 * actions. That is great for discoverability but expensive on the MCP
 * initialize handshake for token-constrained clients.
 *
 * Lean mode keeps the exact same 22 typed category tools and their validated
 * `action` enums, but:
 *   - trims each tool description to its one-line summary + a discovery pointer,
 *   - trims the server instructions (see SERVER_INSTRUCTIONS_LEAN),
 *   - adds a per-category `describe` action that returns that category's action
 *     list on demand,
 *   - prepends a `catalog` discovery tool (search / describe / list_categories)
 *     so an agent can find any action across every category by keyword.
 *
 * The typed enum is deliberately retained (unlike a free-form string surface)
 * so unknown actions are still rejected up front — silent param drift is the
 * failure mode this repo works hardest to avoid.
 */

export type ContextStrategy = "full" | "lean";

/**
 * Resolve the active strategy. Env var wins over config so a user can flip it
 * per-session without editing ue-mcp.yml. Anything other than "lean" (case
 * insensitive) resolves to "full" — the safe, unchanged default.
 */
export function resolveContextStrategy(configStrategy?: string): ContextStrategy {
  const raw = (process.env.UE_MCP_CONTEXT_STRATEGY ?? configStrategy ?? "full").trim().toLowerCase();
  return raw === "lean" ? "lean" : "full";
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

/** One "- action: description" line per action in a tool. */
function actionLines(tool: ToolDef): string[] {
  return Object.entries(tool.actions).map(([name, spec]) =>
    spec.description ? `- ${name}: ${spec.description}` : `- ${name}`,
  );
}

/** Produce the lean variant of a single category tool (non-mutating). */
function leanTool(tool: ToolDef): ToolDef {
  const { summary } = splitDescription(tool.description);

  // Preserve any pre-existing describe action rather than clobber it.
  const actions: Record<string, ActionSpec> = { ...tool.actions };
  if (!actions.describe) {
    const lines = actionLines(tool);
    actions.describe = {
      description: `List every action in the ${tool.name} category with its description (lean-mode discovery).`,
      handler: async () => ({ category: tool.name, count: lines.length, actions: lines }),
    };
  }

  const actionNames = Object.keys(actions) as [string, ...string[]];
  const description =
    `${summary}\n\nLean mode: actions are hidden to save context. ` +
    `Call ${tool.name}(action="describe") to list this category's actions, or ` +
    `catalog(action="search", query="...") to find actions across all categories.`;

  return {
    ...tool,
    description,
    actions,
    schema: {
      ...tool.schema,
      action: z.enum(actionNames).describe("Action to perform"),
    },
  };
}

interface CatalogEntry {
  category: string;
  action: string;
  description: string;
  haystack: string;
}

/** Flatten every action across every tool into a searchable index. */
function buildIndex(tools: ToolDef[]): CatalogEntry[] {
  return tools.flatMap((t) =>
    Object.entries(t.actions).map(([action, spec]) => ({
      category: t.name,
      action,
      description: spec.description ?? "",
      haystack: `${t.name} ${action} ${spec.description ?? ""}`.toLowerCase(),
    })),
  );
}

/**
 * Lightweight keyword ranking — no embedding model, no native deps. Scores each
 * action against the query tokens: an exact category/action token match weighs
 * more than a substring hit in the description. Deterministic and cheap.
 */
function rank(index: CatalogEntry[], query: string, limit: number): Array<Omit<CatalogEntry, "haystack">> {
  const tokens = query.toLowerCase().split(/\s+/).filter(Boolean);
  if (tokens.length === 0) return [];
  const scored = index
    .map((e) => {
      let score = 0;
      for (const tok of tokens) {
        if (e.category === tok || e.action === tok) score += 5;
        else if (e.action.includes(tok)) score += 3;
        else if (e.category.includes(tok)) score += 2;
        else if (e.description.toLowerCase().includes(tok)) score += 1;
      }
      return { e, score };
    })
    .filter((s) => s.score > 0)
    .sort((a, b) => b.score - a.score)
    .slice(0, limit);
  return scored.map(({ e }) => ({ category: e.category, action: e.action, description: e.description }));
}

/**
 * Build the `catalog` discovery tool from the pre-lean tools, so its search
 * index and describe output carry the full action descriptions even though the
 * leaned tools hide them.
 */
export function buildCatalogTool(tools: ToolDef[]): ToolDef {
  const index = buildIndex(tools);
  const summaries = tools.map((t) => ({ category: t.name, summary: splitDescription(t.description).summary }));
  const byName = new Map(tools.map((t) => [t.name, t] as const));

  const actions: Record<string, ActionSpec> = {
    search: {
      description: 'Rank actions across every category by keyword. Params: query (string), limit (default 20).',
      handler: async (_ctx, p) => {
        const query = typeof p.query === "string" ? p.query : "";
        const limit = typeof p.limit === "number" && p.limit > 0 ? Math.min(p.limit, 100) : 20;
        if (!query.trim()) {
          return { error: 'Provide a "query" string, e.g. catalog(action="search", query="spawn actor").' };
        }
        const results = rank(index, query, limit);
        return { query, count: results.length, results };
      },
    },
    describe: {
      description: "List every action in one category. Params: category (string).",
      handler: async (_ctx, p) => {
        const category = typeof p.category === "string" ? p.category : "";
        const tool = byName.get(category);
        if (!tool) {
          return { error: `Unknown category "${category}". Use catalog(action="list_categories").`, categories: summaries.map((s) => s.category) };
        }
        const lines = actionLines(tool);
        return { category, count: lines.length, actions: lines };
      },
    },
    list_categories: {
      description: "List all category tools with their one-line summaries.",
      handler: async () => ({ count: summaries.length, categories: summaries }),
    },
  };

  return categoryTool(
    "catalog",
    "Discovery for lean context mode: search and describe the full action catalog on demand.",
    actions,
    undefined,
    {
      query: z.string().optional().describe("Keyword query for action=search"),
      category: z.string().optional().describe("Category name for action=describe"),
      limit: z.number().int().min(1).max(100).optional().describe("Max results for action=search (default 20)"),
    },
  );
}

/**
 * Apply the lean strategy to a set of category tools. Returns a new array
 * (catalog tool first) — the input tools are not mutated. When two categories
 * already contain a `catalog` tool (they never do today) the caller-supplied
 * one wins; we skip prepending a duplicate.
 */
export function applyLeanContext(tools: ToolDef[]): ToolDef[] {
  const leaned = tools.map(leanTool);
  if (tools.some((t) => t.name === "catalog")) return leaned;
  return [buildCatalogTool(tools), ...leaned];
}
