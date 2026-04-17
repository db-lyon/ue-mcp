import * as http from "node:http";
import type { FlowConfig } from "./schema.js";
import type { createFlowTool } from "./flow-tool.js";
import type { ToolContext } from "../types.js";

type FlowTool = ReturnType<typeof createFlowTool>;

export interface HttpServerOptions {
  host?: string;
  port?: number;
}

// #144 — expose flow.run, flow.plan, flow.list over loopback HTTP so non-MCP
// clients (editor plugins, CI, curl) can invoke flows without speaking stdio
// MCP. Intentionally tiny: routes mirror the three flow actions, responses
// mirror the existing flow result shape.
export function startFlowHttpServer(
  flowTool: FlowTool,
  ctx: ToolContext,
  reloadConfig: () => FlowConfig,
  options: HttpServerOptions = {},
): { server: http.Server; port: number; host: string } {
  const host = options.host ?? "127.0.0.1";
  const port = options.port ?? 7723;

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

  // Silence "reloadConfig unused" — we pass it through for future /config routes.
  void reloadConfig;

  server.listen(port, host, () => {
    console.error(`[ue-mcp] Flow HTTP server listening on http://${host}:${port}`);
  });

  server.on("error", (err) => {
    console.error(`[ue-mcp] HTTP server error: ${err instanceof Error ? err.message : err}`);
  });

  return { server, port, host };
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
