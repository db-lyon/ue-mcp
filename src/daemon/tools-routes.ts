// @session per-session: Runs the category, toolset and lease actions in the editor each request names.
/**
 * The daemon's category, toolset and lease routes (#1827): the tools.* and
 * leases.* entries of DAEMON_ROUTES. Each one runs the project action of the
 * same purpose through the runtime, so an HTTP caller and an MCP client get the
 * same answer, the same refusals, and the same re-advertised surface.
 */
import type { Runtime } from "../server/runtime.js";
import type { Served } from "../server/server-dispatch.js";
import { ErrorCode } from "../core/errors.js";
import type { RouteHandlers, RouteResult } from "./router.js";

type ToolsRouteName = "tools.list" | "tools.categories" | "tools.toolsets" | "leases.list" | "tools.unsupported" | "leases.unsupported";

const STATUS_BY_CODE: Record<string, number> = {
  [ErrorCode.INVALID_PARAMS]: 400,
  [ErrorCode.NOT_FOUND]: 404,
  [ErrorCode.NOT_CONNECTED]: 409,
  [ErrorCode.ASSET_LEASED]: 409,
  [ErrorCode.REFUSED]: 409,
  [ErrorCode.TASK_FAILED]: 422,
};

/**
 * An action's result as JSON, or its failure as `{ error, code }` with a
 * status a client can branch on. A refusal (a blocking dialog) carries its
 * payload as `refusal`, whose fields say what to do about it.
 */
function actionResponse({ outcome }: Served): RouteResult {
  if (outcome.code === ErrorCode.REFUSED) {
    const refusal = (outcome.data ?? {}) as Record<string, unknown>;
    const said = typeof refusal.error === "string" ? refusal.error : typeof refusal.reason === "string" ? refusal.reason : "The call was refused.";
    return { status: 409, body: { error: said, code: outcome.code, refusal } };
  }
  if (outcome.code !== undefined) {
    return { status: STATUS_BY_CODE[outcome.code] ?? 500, body: { error: outcome.message ?? "", code: outcome.code } };
  }
  if (typeof outcome.data !== "string") return { status: 200, body: outcome.data ?? null };
  try {
    return { status: 200, body: JSON.parse(outcome.data) };
  } catch {
    return { status: 200, body: { result: outcome.data } };
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
  const run = (action: string, args?: Record<string, unknown>) => rt.callActionOutcome("project", action, args);
  return {
    "tools.list": async () => actionResponse(await run("list_categories")),
    "tools.categories": async (r) => {
      const b = await body(r);
      return actionResponse(await run("set_category_enabled", {
        category: b.category,
        enabled: b.enabled,
        ...(b.scope === undefined ? {} : { scope: b.scope }),
        ...target(b),
      }));
    },
    "tools.toolsets": async (r) => {
      const b = await body(r);
      return actionResponse(await run("set_toolset_enabled", { toolsets: b.toolsets, enabled: b.enabled, ...target(b) }));
    },
    "leases.list": async () => actionResponse(await run("list_leases")),
    "tools.unsupported": unsupported,
    "leases.unsupported": unsupported,
  };
}
