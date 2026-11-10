/**
 * The daemon's category and toolset routes (#1827). Each one runs the
 * project action of the same purpose through the runtime, so an HTTP caller
 * and an MCP client get the same answer, the same refusals, and the same
 * re-advertised surface:
 *
 *   GET  /v1/tools               project(list_categories)
 *   POST /v1/tools/categories    project(set_category_enabled)   { category, enabled, scope?, editor? }
 *   POST /v1/tools/toolsets      project(set_toolset_enabled)    { toolsets, enabled, editor? }
 *
 * Additive under DAEMON_API_VERSION 1, announced by the `tools.categories`
 * feature in /v1/health.
 */
import type { Runtime } from "../server/runtime.js";
import type { ToolResult } from "../dispatch/server-dispatch.js";

export interface RouteResult {
  status: number;
  body: unknown;
}

/** An action's result as JSON, or its error as `{ error, code }` with a status a client can branch on. */
export function actionResponse(result: ToolResult): RouteResult {
  const text = result.content.map((c) => (c.type === "text" ? c.text : "")).join("\n");
  if (result.isError) {
    const m = /^Error \[([A-Z_]+)\]: ([\s\S]*)$/.exec(text);
    const code = m?.[1] ?? "UNKNOWN";
    const status = code === "INVALID_PARAMS" ? 400 : code === "NOT_FOUND" ? 404 : code === "NOT_CONNECTED" ? 409 : code === "TASK_FAILED" ? 422 : 500;
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

export async function toolsRoute(rt: Runtime, method: string, path: string, body: unknown): Promise<RouteResult | null> {
  const b = (body && typeof body === "object" ? body : {}) as Record<string, unknown>;
  if (path === "/v1/tools" && method === "GET") {
    return actionResponse(await rt.callAction("project", "list_categories"));
  }
  if (path === "/v1/tools/categories" && method === "POST") {
    return actionResponse(await rt.callAction("project", "set_category_enabled", {
      category: b.category,
      enabled: b.enabled,
      ...(b.scope === undefined ? {} : { scope: b.scope }),
      ...target(b),
    }));
  }
  if (path === "/v1/tools/toolsets" && method === "POST") {
    return actionResponse(await rt.callAction("project", "set_toolset_enabled", { toolsets: b.toolsets, enabled: b.enabled, ...target(b) }));
  }
  if (path === "/v1/tools" || path.startsWith("/v1/tools/")) {
    return { status: 405, body: { error: `${method} is not served on ${path}` } };
  }
  return null;
}
