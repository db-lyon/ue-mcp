// @session per-session: Runs the category, toolset and lease actions in the editor each request names.
/**
 * The daemon's category, toolset and lease routes (#1827): the tools.* and
 * leases.* entries of DAEMON_ROUTES. Each one runs the project action of the
 * same purpose through the runtime, so an HTTP caller and an MCP client get the
 * same answer, the same refusals, and the same re-advertised surface.
 */
import type { Runtime } from "../server/runtime.js";
import type { ToolResult } from "../server/server-dispatch.js";
import type { RouteHandlers, RouteResult } from "./router.js";

type ToolsRouteName = "tools.list" | "tools.categories" | "tools.toolsets" | "leases.list" | "tools.unsupported" | "leases.unsupported";

/** An action's result as JSON, or its error as `{ error, code }` with a status a client can branch on. */
function actionResponse(result: ToolResult): RouteResult {
  const text = result.content.map((c) => (c.type === "text" ? c.text : "")).join("\n");
  if (result.isError) {
    const m = /^Error \[([A-Z_]+)\]: ([\s\S]*)$/.exec(text);
    const code = m?.[1] ?? "UNKNOWN";
    const status = code === "INVALID_PARAMS" ? 400 : code === "NOT_FOUND" ? 404 : code === "NOT_CONNECTED" || code === "ASSET_LEASED" ? 409 : code === "TASK_FAILED" ? 422 : 500;
    return { status, body: { error: m?.[2] ?? text, code } };
  }
  const first = result.content[0];
  try {
    return { status: 200, body: JSON.parse(first?.type === "text" ? first.text : "null") };
  } catch {
    return { status: 200, body: { result: text } };
  }
}

/** The editor a change is aimed at, when the daemon drives more than one. */
function target(b: Record<string, unknown>): Record<string, unknown> {
  return typeof b.editor === "string" && b.editor ? { editor: b.editor } : {};
}

export function toolsRoutes(rt: Runtime): Pick<RouteHandlers, ToolsRouteName> {
  const body = async (r: { body(): Promise<unknown> }): Promise<Record<string, unknown>> => {
    const b = await r.body();
    return (b && typeof b === "object" ? b : {}) as Record<string, unknown>;
  };
  const unsupported: RouteHandlers["tools.unsupported"] = ({ method, path }) =>
    ({ status: 405, body: { error: `${method} is not served on ${path}` } });
  return {
    "tools.list": async () => actionResponse(await rt.callAction("project", "list_categories")),
    "tools.categories": async (r) => {
      const b = await body(r);
      return actionResponse(await rt.callAction("project", "set_category_enabled", {
        category: b.category,
        enabled: b.enabled,
        ...(b.scope === undefined ? {} : { scope: b.scope }),
        ...target(b),
      }));
    },
    "tools.toolsets": async (r) => {
      const b = await body(r);
      return actionResponse(await rt.callAction("project", "set_toolset_enabled", { toolsets: b.toolsets, enabled: b.enabled, ...target(b) }));
    },
    "leases.list": async () => actionResponse(await rt.callAction("project", "list_leases")),
    "tools.unsupported": unsupported,
    "leases.unsupported": unsupported,
  };
}
