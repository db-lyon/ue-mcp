import { searchToolGraph, type ToolSearchHit } from "../../tool-search.js";
import { actionSchema, resolveActionRef, suggestActions } from "../../action-schema.js";
import { availabilityReport } from "../../offline.js";
import { getWorkarounds } from "../../workaround-tracker.js";
import { toolGraphOf, type ToolContext, type ActionSpec } from "../../types.js";

/** Surface introspection: search, describe and list what this server can serve. */
export const surfaceActions: Record<string, ActionSpec> = {
  search_tools: {
    kind: "handler",
    effect: "read",
    description: "Search every ue-mcp tool + action by keyword or task INTENT (a synonym layer maps 'screenshot'->capture_scene_png, 'tile a texture'->the texture-bomb flow, etc.) and return ranked matches (tool, action, description, score). The first step before editor(execute_python); most tasks already have a dedicated action. Params: query (space-separated keywords/intent), limit? (default 20) (#704)",
    handler: async (_ctx, p) => {
      const query = (p.query as string) ?? "";
      if (!query.trim()) throw new Error("Missing 'query'");
      const limit = (p.limit as number) ?? 20;
      const results = searchToolGraph(await toolGraphOf(_ctx), query, limit);
      return {
        query,
        resultCount: results.length,
        results,
        hint: results.length === 0 ? "No dedicated action matched. Only then consider editor(execute_python)." : undefined,
      };
    },
  },
  describe_action: {
    kind: "handler",
    effect: "read",
    description:
      "Return the live parameter schema for one action: every parameter it accepts, "
      + "with type, required/optional, description, allowed values and default, plus the "
      + "bridge method it dispatches to. search_tools finds an action by keyword and hands "
      + "back only prose; this answers what to actually pass, so the first call is the "
      + "right one. name takes 'tool.action' (asset.set_property) or a bare action name, "
      + "which reports every category providing it. A name that does not resolve comes back "
      + "with the closest spellings rather than a bare failure. Reads the graph THIS editor "
      + "advertises, so injected Epic and plugin actions are included. Each action also "
      + "reports class: read (observes), mutate (changes the editor, its project on disk, or "
      + "its process) or unknown (decided by a parameter, so gated like mutate) - MCP's own "
      + "readOnlyHint is per tool, and every tool here is a category holding both, so a "
      + "harness that gates writes reads it from here. "
      + "Params: name (required), category? (return every action of one category instead of one action)",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const graph = await toolGraphOf(ctx);

      const category = (p.category as string | undefined)?.trim();
      if (category) {
        const tool = graph.find((t) => t.name === category.toLowerCase());
        if (!tool) {
          throw new Error(
            `Unknown category '${category}'. Available: ${graph.map((t) => t.name).join(", ")}`,
          );
        }
        return {
          tool: tool.name,
          actionCount: Object.keys(tool.actions).length,
          actions: Object.keys(tool.actions).map((a) => actionSchema(tool, a)),
        };
      }

      const name = (p.name as string | undefined)?.trim();
      if (!name) throw new Error("Missing 'name'. Pass 'tool.action', a bare action name, or use category= for a whole category.");

      const matches = resolveActionRef(name, graph);
      if (matches.length === 0) {
        const suggestions = suggestActions(name, graph);
        throw new Error(
          `Unknown action '${name}'.`
          + (suggestions.length ? ` Closest: ${suggestions.join(", ")}.` : "")
          + " project(search_tools) searches by intent when the name is not known.",
        );
      }
      const schemas = matches.map(({ tool, action }) => actionSchema(tool, action));
      // One match is the common case, and returning it bare keeps the shape
      // an agent has to read as small as the question it asked.
      return schemas.length === 1
        ? schemas[0]
        : {
            name,
            matchCount: schemas.length,
            hint: `'${name}' is provided by ${schemas.length} categories. Qualify it as '<tool>.${matches[0].action}' to get one.`,
            matches: schemas,
          };
    },
  },
  list_available_actions: {
    kind: "handler",
    effect: "read",
    description:
      "Report which actions this server can serve RIGHT NOW and why the rest cannot. With no editor "
      + "attached the surface is advertised in full but most of it cannot run, and this is the line "
      + "between the two halves: an action either runs in this Node process (availability 'always') or "
      + "dispatches to a bridge method only a running editor answers (availability 'editor', with "
      + "bridgeMethod naming it). The offline half is the engine symbol index and the C++ correctness "
      + "checks, the project config, source and file readers, the surface introspection, and the "
      + "process lifecycle actions that start, stop and build. An action contributed by a plugin whose "
      + "route is undeclared reports 'unknown' and should be treated as needing an editor. "
      + "Counts come back by default, per category as well as overall; includeNames=true adds the actions "
      + "themselves, and category narrows the whole report to one. With an editor attached everything "
      + "is available and the classification still answers the question worth asking then, which is "
      + "what keeps working once the editor is stopped for a rebuild. "
      + "Params: category?, includeNames? (default false), state? (available|blocked|all, default available)",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const graph = await toolGraphOf(ctx);

      const category = (p.category as string | undefined)?.trim();
      if (category && !graph.some((t) => t.name === category.toLowerCase())) {
        throw new Error(
          `Unknown category '${category}'. Available: ${graph.map((t) => t.name).join(", ")}`,
        );
      }

      const state = (p.state as string | undefined)?.trim() ?? "available";
      if (state !== "available" && state !== "blocked" && state !== "all") {
        throw new Error(`'state' must be 'available', 'blocked' or 'all' (got '${state}').`);
      }

      const report = availabilityReport(graph, {
        editorConnected: ctx.bridge.isConnected,
        category,
        state,
        names: p.includeNames === true,
      });
      const target = ctx.bridge.getTarget();
      return {
        ...report,
        editorTarget: { projectPath: target.projectPath, port: target.port, portSource: target.portSource },
        hint: report.blocked > 0
          ? "editor(action='start_editor') launches the editor and blocks until its bridge answers."
          : undefined,
      };
    },
  },
  execute_python_report: {
    kind: "handler",
    effect: "read",
    description: "Measurement for #704: reads this session's execute_python calls and, for each, runs its taskSummary back through search_tools to flag calls that OVERLAPPED an existing dedicated action ('you used Python for X, but tool Y does X'). Returns totalCalls, overlapping[] and an overlapRate. Params: none (#704)",
    handler: async (ctx) => {
      const entries = getWorkarounds(ctx);
      const graph = await toolGraphOf(ctx);
      const overlapping: Array<{ taskSummary: string; suggestion: ToolSearchHit; codeSnippet: string }> = [];
      for (const e of entries) {
        const q = (e.taskSummary ?? "").trim();
        if (!q) continue;
        const hits = searchToolGraph(graph, q, 1);
        if (hits.length > 0 && hits[0].score >= 4) {
          overlapping.push({ taskSummary: q, suggestion: hits[0], codeSnippet: e.code.slice(0, 120) });
        }
      }
      return {
        totalCalls: entries.length,
        withTaskSummary: entries.filter((e) => (e.taskSummary ?? "").trim()).length,
        overlappingCount: overlapping.length,
        overlapRate: entries.length ? +(overlapping.length / entries.length).toFixed(2) : 0,
        overlapping,
      };
    },
  },
};
