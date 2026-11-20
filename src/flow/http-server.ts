// @session per-session: Resolves an editor per request and refuses an untargeted run.
import * as http from "node:http";
import { parseStepApproval, type FlowStepApprovalSpec } from "./step-approval.js";
import { randomBytes } from "node:crypto";
import { EDITOR_TARGET_PARAM } from "../surface/routing-params.js";
import { ErrorCode } from "../core/errors.js";
import type { CallOutcome } from "../dispatch/call.js";
import { info, warn, error as logError } from "../core/log.js";
import { subscribeFlowEvents, type FlowEvent } from "./events.js";
import { readEnv } from "../core/env.js";
import { hostIsAllowed, tokensMatch } from "../core/loopback-host.js";
import { FLOW_HTTP_PORT } from "../core/flow-http-port.js";
import { DAEMON_HEADERS } from "../contract/daemon.js";
import { userDir } from "../core/user-dir.js";
import { writePrivateFile } from "../core/private-file.js";
import * as path from "node:path";

/** One flow call through the server's call pipeline, as the routes hand it over. */
export type FlowRequest = (input: Record<string, unknown>, approval?: FlowStepApprovalSpec) => Promise<{ outcome: CallOutcome }>;

export interface HttpServerOptions {
  host?: string;
  port?: number;
  token?: string;
}

/** Where a generated flow HTTP token is written: `~/.ue-mcp/flow-http/<port>.token`, readable by this user only. */
export function flowHttpTokenFile(port: number): string {
  return path.join(userDir(), "flow-http", `${port}.token`);
}

function extractToken(req: http.IncomingMessage): string | null {
  const header = req.headers[DAEMON_HEADERS.flowToken];
  if (typeof header === "string" && header.length > 0) return header;
  const auth = req.headers["authorization"];
  if (typeof auth === "string" && auth.toLowerCase().startsWith("bearer ")) {
    return auth.slice(7).trim();
  }
  return null;
}

// #144 - expose flow.run, flow.plan, flow.list over loopback HTTP so non-MCP
// clients (editor plugins, CI, curl) can invoke flows without speaking stdio
// MCP. Intentionally tiny: routes mirror the three flow actions, responses
// mirror the existing flow result shape.
export function startFlowHttpServer(
  run: FlowRequest,
  options: HttpServerOptions = {},
): { server: http.Server; port: number; host: string; token: string } {
  const host = options.host ?? "127.0.0.1";
  const port = options.port ?? FLOW_HTTP_PORT;
  // Token precedence: explicit option > env override > freshly generated.
  // Env override lets CI scripts pin a known value without parsing stderr.
  const supplied = options.token ?? readEnv("httpToken");
  const generated = supplied === undefined;
  const token = supplied ?? randomBytes(32).toString("hex");

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
      if (!tokensMatch(presented, token)) {
        return send(401, { error: "Missing or invalid token" });
      }

      const handled = await handleFlowRoute(run, req, res, pathname, url);
      if (handled) return;

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
    // The token never goes into the log: MCP clients and the daemon keep it
    // on disk, and logs get pasted into issues (#1866). A supplied token is
    // already known to whoever supplied it; a generated one goes to a file
    // only this user can read.
    const how = "send it as 'Authorization: Bearer <token>' or 'X-UE-MCP-Token: <token>'";
    if (!generated) {
      info("http", `flow HTTP token: the one set in ${options.token ? "the server options" : "UE_MCP_HTTP_TOKEN"}; ${how}`);
      return;
    }
    const address = server.address();
    const file = flowHttpTokenFile(typeof address === "object" && address !== null ? address.port : port);
    writePrivateFile(file, `${token}\n`).then(
      () => info("http", `flow HTTP token written to ${file}; ${how}`),
      (e) => warn("http", `could not write the flow HTTP token to ${file}; set UE_MCP_HTTP_TOKEN to choose one`, e),
    );
  });

  return { server, port, host, token };
}

/**
 * The flow routes, without transport, auth or host checks: the caller has done
 * those. Mounted by the standalone flow server at / and by the daemon at /v1.
 * Resolves true when the request matched a route and was answered.
 */
export async function handleFlowRoute(
  run: FlowRequest,
  req: http.IncomingMessage,
  res: http.ServerResponse,
  pathname: string,
  url: URL,
): Promise<boolean> {
  const method = req.method ?? "GET";
  const send = (status: number, body: unknown): true => {
    const text = typeof body === "string" ? body : JSON.stringify(body, null, 2);
    res.statusCode = status;
    res.setHeader("Content-Type", "application/json; charset=utf-8");
    res.end(text);
    return true;
  };

  /**
   * A flow call through the same pipeline as an MCP call: routing, the
   * untargeted refusal beyond one editor (#817), the dialog guard, read-only,
   * the journal. Untargeted calls follow the active session. Steps are refused
   * at the guarded bridge; the dialog gate covers a run STARTED while a modal
   * is up, and list/plan, which never touch the bridge. Nobody is on an HTTP
   * request, so interactive degrades to reporting the dialog, as defer does.
   */
  const answer = async (input: Record<string, unknown>, approval?: FlowStepApprovalSpec): Promise<true> => {
    const { outcome } = await run(input, approval);
    if (outcome.code === undefined) return send(200, outcome.data);
    if (outcome.code === ErrorCode.REFUSED) return send(409, outcome.data);
    const status = outcome.code === ErrorCode.INVALID_PARAMS ? 400 : outcome.code === ErrorCode.NOT_FOUND ? 404 : 500;
    return send(status, { error: outcome.message });
  };

  if (method === "GET" && (pathname === "/" || pathname === "/flows")) {
    return answer({ action: "list" });
  }

  const planMatch = pathname.match(/^\/flows\/([^/]+)\/plan$/);
  if (method === "GET" && planMatch) {
    return answer({ action: "plan", flowName: decodeURIComponent(planMatch[1]) });
  }

  const runMatch = pathname.match(/^\/flows\/([^/]+)\/run$/);
  if (method === "POST" && runMatch) {
    const input: Record<string, unknown> = { action: "run", flowName: decodeURIComponent(runMatch[1]) };
    const body = await readJsonBody(req);
    let approval: FlowStepApprovalSpec | null = null;
    if (body && typeof body === "object") {
      const b = body as Record<string, unknown>;
      if (b.params !== undefined) input.params = b.params;
      if (b.skip !== undefined) input.skip = b.skip;
      if (b.rollback_on_failure !== undefined) input.rollback_on_failure = b.rollback_on_failure;
      // Each step asks a registered extension before it runs (flow/step-approval.ts).
      try {
        approval = parseStepApproval(b.stepApproval);
      } catch (e) {
        return send(400, { error: e instanceof Error ? e.message : String(e) });
      }
      // The editor is addressed the same way the MCP surface addresses one;
      // the pipeline takes it off before the flow sees its options.
      if (typeof b.editor === "string" && b.editor) input[EDITOR_TARGET_PARAM] = b.editor;
    }
    return answer(input, approval ?? undefined);
  }


  // Server-Sent Events: live per-step + per-run events from the flow
  // runner. Optional `?runId=...` query filter for clients that only
  // care about a specific run (the runId is returned in the flow.run
  // response). Otherwise every event from every concurrent run flows
  // through.
  if (method === "GET" && pathname === "/flows/events") {
    const runIdFilter = url.searchParams.get("runId");
    res.statusCode = 200;
    res.setHeader("Content-Type", "text/event-stream");
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Connection", "keep-alive");
    // Disable proxy buffering (nginx, etc.) so events flush immediately.
    res.setHeader("X-Accel-Buffering", "no");

    // Tell the client what id (if any) we're filtering on, then a
    // ready marker so curl --no-buffer users see something land
    // even before any flow fires.
    res.write(`: connected runIdFilter=${runIdFilter ?? "*"}\n\n`);

    const unsubscribe = subscribeFlowEvents((event: FlowEvent) => {
      if (runIdFilter && event.runId !== runIdFilter) return;
      try {
        res.write(`event: ${event.type}\n`);
        res.write(`data: ${JSON.stringify(event)}\n\n`);
      } catch {
        // Write failed: connection is dead. Drop the subscription
        // so we don't keep buffering events for a gone client.
        unsubscribe();
      }
    });

    // Periodic comment lines keep proxies and load balancers from
    // killing the connection during quiet periods.
    const keepalive = setInterval(() => {
      try {
        res.write(`: keepalive ${Date.now()}\n\n`);
      } catch {
        clearInterval(keepalive);
        unsubscribe();
      }
    }, 30_000);

    req.on("close", () => {
      clearInterval(keepalive);
      unsubscribe();
    });
    return true;
  }

  return false;
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
