// @session per-session: Serves the routes of the one daemon that built it; each request names its editor session.
/**
 * The daemon's HTTP router: it walks DAEMON_ROUTES in order and hands the first
 * entry whose method and path match to that route's handler. Every handler
 * has the one shape below, so a route is added in the contract and its handler
 * here, and the type of RouteHandlers fails until both are there.
 */
import * as fs from "node:fs";
import type * as http from "node:http";
import { DAEMON_ROUTES, routePath, type DaemonRoute, type DaemonRouteName } from "../contract/daemon.js";
import { warn } from "../core/log.js";
import { StatusError } from "../core/status-error.js";
import type { EditorSession, SessionRegistry } from "../sessions/session.js";

/** Every route served over HTTP; the WebSocket one is taken at the upgrade. */
export type HttpRouteName = { [K in DaemonRouteName]: (typeof DAEMON_ROUTES)[K]["method"] extends "WS" ? never : K }[DaemonRouteName];

export interface RouteRequest {
  method: string;
  /** The path with trailing slashes dropped. */
  path: string;
  /** The URL as it was sent. */
  url: URL;
  /** The `:param` segments of the route, decoded. */
  params: Record<string, string>;
  query: URLSearchParams;
  /** The JSON body, or undefined when there is none. Rejects on a body that is not JSON. */
  body(): Promise<unknown>;
  req: http.IncomingMessage;
  res: http.ServerResponse;
}

/** The handler wrote the response itself (a stream). */
export const HANDLED = Symbol("handled");

/**
 * What a handler answers: JSON (`body`, or no body at all when it is
 * undefined), raw bytes with their headers, a file streamed with its headers,
 * or HANDLED.
 */
export type RouteResult =
  | { status: number; body?: unknown }
  | { status: number; headers: Record<string, string>; raw: string | Buffer }
  | { status: number; headers: Record<string, string>; file: string }
  | typeof HANDLED;

/**
 * One handler. A StatusError it throws is answered with its status and
 * `{ error }`; anything else it throws, with 500.
 */
export type RouteHandler = (r: RouteRequest) => RouteResult | Promise<RouteResult>;

export type RouteHandlers = { [K in HttpRouteName]: RouteHandler };

export const ok = (body: unknown): RouteResult => ({ status: 200, body });

/**
 * The editor session a request names in `editor`, as /v1/health lists them;
 * omitted or empty is the active one.
 */
export function resolveEditorSession(sessions: SessionRegistry, editor: unknown): EditorSession {
  if (editor === undefined || editor === null || editor === "") return sessions.active;
  if (typeof editor !== "string") throw new StatusError(400, "editor is a session name");
  const session = sessions.find(editor);
  if (!session) throw new StatusError(404, `No editor session '${editor}'. ${routePath("health")} lists them.`);
  return session;
}

/** A JSON object body, or {} for none or any other JSON value. */
export function objectBody(body: unknown): Record<string, unknown> {
  return (body && typeof body === "object" && !Array.isArray(body) ? body : {}) as Record<string, unknown>;
}

/** The params of `path` under a route pattern, or null when it does not match. */
export function matchRoutePath(pattern: string, path: string): Record<string, string> | null {
  const want = pattern.split("/");
  const got = path.split("/");
  const prefix = want[want.length - 1] === "*";
  if (prefix) want.pop();
  if (prefix ? got.length < want.length : got.length !== want.length) return null;
  const params: Record<string, string> = {};
  for (let i = 0; i < want.length; i++) {
    if (want[i].startsWith(":")) {
      if (!got[i]) return null;
      try {
        params[want[i].slice(1)] = decodeURIComponent(got[i]);
      } catch {
        return null;
      }
    } else if (want[i] !== got[i]) return null;
  }
  return params;
}

/** The first route that serves `method path`, or null. */
export function findRoute(method: string, path: string): { name: HttpRouteName; params: Record<string, string> } | null {
  for (const [name, route] of Object.entries(DAEMON_ROUTES) as Array<[DaemonRouteName, DaemonRoute]>) {
    if (route.method === "WS") continue;
    if (route.method !== "ANY" && route.method !== method) continue;
    const params = matchRoutePath(route.path, path);
    if (params) return { name: name as HttpRouteName, params };
  }
  return null;
}

/** Pipe a file to a response; a file gone mid-read ends the response instead of the daemon. */
export function streamFile(file: string, res: http.ServerResponse): void {
  fs.createReadStream(file)
    .on("error", (e) => {
      warn("daemon", `could not read ${file}`, e);
      res.destroy();
    })
    .pipe(res);
}

function readBody(req: http.IncomingMessage): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", () => {
      const raw = Buffer.concat(chunks).toString("utf-8").trim();
      if (!raw) return resolve(undefined);
      try {
        resolve(JSON.parse(raw));
      } catch (e) {
        reject(e);
      }
    });
    req.on("error", reject);
  });
}

function send(res: http.ServerResponse, out: RouteResult): void {
  if (out === HANDLED) return;
  if ("file" in out) {
    res.writeHead(out.status, out.headers);
    streamFile(out.file, res);
  } else if ("raw" in out) {
    res.writeHead(out.status, out.headers).end(out.raw);
  } else if (out.body === undefined) {
    res.writeHead(out.status).end();
  } else {
    res.writeHead(out.status, { "content-type": "application/json" }).end(JSON.stringify(out.body));
  }
}

/** Answer one authorized request: the first matching route's handler, or 404 with no body. */
export function createRouter(handlers: RouteHandlers): (req: http.IncomingMessage, res: http.ServerResponse) => void {
  return (req, res) => {
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    const method = req.method ?? "GET";
    const path = url.pathname.replace(/\/+$/, "") || "/";
    const hit = findRoute(method, path);
    if (!hit) {
      res.writeHead(404).end();
      return;
    }
    let body: Promise<unknown> | null = null;
    const request: RouteRequest = {
      method,
      path,
      url,
      params: hit.params,
      query: url.searchParams,
      body: () => (body ??= readBody(req)),
      req,
      res,
    };
    Promise.resolve()
      .then(() => handlers[hit.name](request))
      .then(
        (out) => send(res, out),
        (e: unknown) => send(res, {
          status: e instanceof StatusError ? e.status : 500,
          body: { error: e instanceof Error ? e.message : String(e) },
        }),
      );
  };
}
