/**
 * Epic's built-in Fab tools, found from the fab category.
 *
 * The Fab plugin the launcher installs beside UE 5.9 carries its own toolset
 * (module FabToolset) in Unreal's Toolset Registry, the registry Epic's Unreal
 * MCP server and AI Assistant read: it searches the Fab catalog and imports
 * listings, which fab's own actions do not. ue-mcp extends those tools rather
 * than replacing them, so an agent working in the fab category has to be able
 * to find and run them from here.
 *
 * No catalog this package was generated from recorded a Fab toolset (the test
 * project's engine has no Fab plugin), so there are no `epic_*` actions for it.
 * Instead this action asks the connected editor what it has, under the same
 * routing rule the generator and `project(set_category_enabled)` use, and
 * names the gateway that runs each tool: epic(call_tool) for a registry tool,
 * epic(call_mcp_tool) for one the Fab plugin registers with Epic's MCP plugin.
 */
import type { HandlerActionSpec, ToolContext } from "../core/types.js";
import { routeEpicToolset } from "./epic/toolset-routes.js";

/** Whether a toolset or tool name belongs to Fab, by the same rule the generator routes with. */
export function isFabToolName(name: string): boolean {
  return routeEpicToolset(name) === "fab";
}

type Body = Record<string, unknown>;

interface Asked {
  body?: Body;
  /** The editor answered with a refusal (no registry, plugin off, old engine). */
  refused?: string;
  /** The call itself failed (not connected, unknown method). */
  thrown?: unknown;
}

async function ask(ctx: ToolContext, method: string, params: Body): Promise<Asked> {
  try {
    const answer = await ctx.bridge.call(method, params);
    const body = answer && typeof answer === "object" ? (answer as Body) : {};
    if (body.success === false || body.available === false) {
      const why = body.error ?? body.reason ?? body.note;
      return { body, refused: typeof why === "string" && why ? why : `${method} is not available in this editor` };
    }
    return { body };
  } catch (e) {
    return { thrown: e };
  }
}

function message(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

function records(v: unknown): Body[] {
  return Array.isArray(v) ? v.filter((x): x is Body => !!x && typeof x === "object") : [];
}

/** A registry tool as list_toolsets gives it: a bare name, or the full object with includeSchemas. */
function toolEntry(t: unknown): Body | null {
  if (typeof t === "string") return { name: t };
  if (t && typeof t === "object" && typeof (t as Body).name === "string") return t as Body;
  return null;
}

const NONE_NOTE =
  "The connected editor's Toolset Registry and Unreal MCP plugin report no Fab tools. Epic's built-in Fab tools "
  + "(catalog search and import) come with the Fab UE Plugin that the Epic Games Launcher installs beside the engine, "
  + "as its FabToolset module (UE 5.9). fab(status) says whether that plugin is installed and loaded; epic(list_toolsets) "
  + "lists every toolset this editor has. fab's own actions still cover the signed-in account's owned library.";

export const listEpicFabTools: HandlerActionSpec = {
  kind: "handler",
  effect: "read",
  description:
    "List Epic's built-in Fab tools the connected editor has, which search the Fab catalog and import listings "
    + "(the Fab plugin's FabToolset in Unreal's Toolset Registry, UE 5.9, and any Fab tool registered with Epic's Unreal MCP plugin). "
    + "Each tool names the call that runs it: epic(call_tool) with its qualified name, or epic(call_mcp_tool). "
    + "ue-mcp does not replace these tools; use them for catalog search and free-asset import, and fab's own actions for the owned library. "
    + "includeSchemas returns each tool's input schema. available=false with a note when the editor has none. "
    + "Params: includeSchemas? (bool)",
  handler: async (ctx, p) => {
    const includeSchemas = p.includeSchemas === true;
    const [registry, mcp] = await Promise.all([
      ask(ctx, "epic_list_toolsets", { includeSchemas }),
      ask(ctx, "epic_list_mcp_tools", { includeSchemas }),
    ]);
    // Neither call reached an editor: the caller has to hear why, not "no tools".
    if (registry.thrown && mcp.thrown) throw registry.thrown;

    const toolsets = records(registry.body?.toolsets)
      .filter((ts) => typeof ts.name === "string" && isFabToolName(ts.name))
      .map((ts) => ({
        name: ts.name as string,
        ...(typeof ts.description === "string" ? { description: ts.description } : {}),
        tools: (Array.isArray(ts.tools) ? ts.tools : [])
          .map(toolEntry)
          .filter((t): t is Body => t !== null)
          .map((t) => ({ ...t, call: `epic(action="call_tool", args={tool: "${String(t.name)}", input: {...}})` })),
      }));
    const mcpTools = records(mcp.body?.tools)
      .filter((t) => typeof t.name === "string" && isFabToolName(t.name))
      .map((t) => ({ ...t, call: `epic(action="call_mcp_tool", args={tool: "${String(t.name)}", input: {...}})` }));

    const toolCount = toolsets.reduce((n, ts) => n + ts.tools.length, 0) + mcpTools.length;
    const errors = {
      ...(registry.refused || registry.thrown ? { registryError: registry.refused ?? message(registry.thrown) } : {}),
      ...(mcp.refused || mcp.thrown ? { mcpToolsError: mcp.refused ?? message(mcp.thrown) } : {}),
    };
    return {
      available: toolCount > 0,
      toolCount,
      toolsets,
      mcpTools,
      ...errors,
      ...(toolCount === 0
        ? { note: NONE_NOTE }
        : {
          hint: "Run a tool with the call it names. describe its arguments first with includeSchemas=true, "
            + "or epic(action=\"describe_toolset\", args={toolset: \"<name>\"}).",
        }),
    };
  },
};
