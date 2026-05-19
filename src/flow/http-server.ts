import * as http from "node:http";
import { randomBytes, timingSafeEqual } from "node:crypto";
import type { createFlowTool } from "./flow-tool.js";
import type { ToolContext } from "../types.js";
import { info, warn, error as logError } from "../log.js";

type FlowTool = ReturnType<typeof createFlowTool>;

export interface HttpServerOptions {
  host?: string;
  port?: number;
  token?: string;
}

const ALLOWED_HOSTS = new Set([
  "localhost",
  "127.0.0.1",
  "[::1]",
]);

function constantTimeEq(a: string, b: string): boolean {
  const ab = Buffer.from(a, "utf8");
  const bb = Buffer.from(b, "utf8");
  if (ab.length !== bb.length) return false;
  return timingSafeEqual(ab, bb);
}

function extractToken(req: http.IncomingMessage): string | null {
  const header = req.headers["x-ue-mcp-token"];
  if (typeof header === "string" && header.length > 0) return header;
  const auth = req.headers["authorization"];
  if (typeof auth === "string" && auth.toLowerCase().startsWith("bearer ")) {
    return auth.slice(7).trim();
  }
  return null;
}

function hostIsAllowed(req: http.IncomingMessage): boolean {
  const host = req.headers.host;
  if (typeof host !== "string" || host.length === 0) return false;
  // strip optional :port (handle bracketed IPv6 too)
  const bare = host.startsWith("[")
    ? host.slice(0, host.indexOf("]") + 1)
    : host.replace(/:\d+$/, "");
  return ALLOWED_HOSTS.has(bare.toLowerCase());
}

// #144 — expose flow.run, flow.plan, flow.list over loopback HTTP so non-MCP
// clients (editor plugins, CI, curl) can invoke flows without speaking stdio
// MCP. Intentionally tiny: routes mirror the three flow actions, responses
// mirror the existing flow result shape.
export function startFlowHttpServer(
  flowTool: FlowTool,
  ctx: ToolContext,
  options: HttpServerOptions = {},
): { server: http.Server; port: number; host: string; token: string } {
  const host = options.host ?? "127.0.0.1";
  const port = options.port ?? 7723;
  // Token precedence: explicit option > env override > freshly generated.
  // Env override lets CI scripts pin a known value without parsing stderr.
  const token =
    options.token ??
    process.env.UE_MCP_HTTP_TOKEN ??
    randomBytes(32).toString("hex");

  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url ?? "/", `http://${host}:${port}`);
      const method = req.method ?? "GET";
      const pathname = url.pathname.replace(/\/+$/, "") || "/";

      const send = (status: number, body: unknown) => {
        const text = typeof body === "string" ? body : JSON.stringify(body, null, 2);
        res.statusCode = status;
        res.setHeader("Content-Type", "application/json; charset=utf-8");
        res.end(text);
      };

      // Defeat DNS rebinding: a malicious page can resolve attacker.example
      // to 127.0.0.1, but the browser still sends the original Host header.
      if (!hostIsAllowed(req)) {
        warn("http", `rejected request with disallowed Host: ${req.headers.host ?? "<none>"}`);
        return send(403, { error: "Host not allowed" });
      }

      // Bearer token: every route requires it, including /health (otherwise
      // any local process can probe whether the server is up).
      const presented = extractToken(req);
      if (presented === null || !constantTimeEq(presented, token)) {
        return send(401, { error: "Missing or invalid token" });
      }

      if (method === "GET" && (pathname === "/" || pathname === "/flows")) {
        const result = await flowTool.handler(ctx, { action: "list" });
        return send(200, result);
      }

      const planMatch = pathname.match(/^\/flows\/([^/]+)\/plan$/);
      if (method === "GET" && planMatch) {
        const flowName = decodeURIComponent(planMatch[1]);
        const result = await flowTool.handler(ctx, { action: "plan", flowName });
        return send(200, result);
      }

      const runMatch = pathname.match(/^\/flows\/([^/]+)\/run$/);
      if (method === "POST" && runMatch) {
        const flowName = decodeURIComponent(runMatch[1]);
        const body = await readJsonBody(req);
        const params: Record<string, unknown> = {
          action: "run",
          flowName,
        };
        if (body && typeof body === "object") {
          const b = body as Record<string, unknown>;
          if (b.params !== undefined) params.params = b.params;
          if (b.skip !== undefined) params.skip = b.skip;
          if (b.rollback_on_failure !== undefined) params.rollback_on_failure = b.rollback_on_failure;
        }
        const result = await flowTool.handler(ctx, params);
        return send(200, result);
      }

      if (method === "GET" && pathname === "/health") {
        return send(200, { ok: true, port, host });
      }

      send(404, { error: `No route for ${method} ${pathname}` });
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      res.statusCode = 500;
      res.setHeader("Content-Type", "application/json; charset=utf-8");
      res.end(JSON.stringify({ error: msg }));
    }
  });

  // Attach the error handler BEFORE listen so that synchronous bind failures
  // (EADDRINUSE / EACCES) surface via the handler rather than throwing out of
  // the caller's try/catch after the fact.
  server.on("error", (err) => {
    logError("http", `flow HTTP server error on ${host}:${port}`, err);
  });

  server.listen(port, host, () => {
    info("http", `flow HTTP server listening on http://${host}:${port}`);
    // Print the token to stderr (where MCP servers' diagnostic output lives)
    // so the operator can copy it without parsing tool output. Env-supplied
    // tokens are still echoed; the value is already known to whoever set it.
    info("http", `flow HTTP token: ${token} (send as 'Authorization: Bearer ...' or 'X-UE-MCP-Token: ...')`);
  });

  return { server, port, host, token };
}

async function readJsonBody(req: http.IncomingMessage): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", () => {
      const raw = Buffer.concat(chunks).toString("utf-8").trim();
      if (!raw) return resolve({});
      try {
        resolve(JSON.parse(raw));
      } catch (e) {
        reject(new Error(`Invalid JSON body: ${e instanceof Error ? e.message : e}`));
      }
    });
    req.on("error", reject);
  });
}
