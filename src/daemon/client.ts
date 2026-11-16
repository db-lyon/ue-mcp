// @session per-session: Talks to the one daemon the discovery record it is handed names.
/**
 * The client side of the daemon's HTTP API, for ue-mcp's own commands: the
 * address and bearer token come from a discovery record, the paths from the
 * contract's route table.
 */
import * as http from "node:http";
import { isPidAlive } from "../bridge/editor-target.js";
import { routePath, type DaemonDiscovery, type DaemonRouteName } from "../contract/daemon.js";

/** The Authorization header a daemon accepts. */
export function bearer(d: Pick<DaemonDiscovery, "token">): { authorization: string } {
  return { authorization: `Bearer ${d.token}` };
}

/** The daemon's address for a path, such as routePath("health"). */
export function daemonUrl(d: Pick<DaemonDiscovery, "port">, pathAndQuery: string): string {
  return `http://127.0.0.1:${d.port}${pathAndQuery}`;
}

/** The client UI's login URL, which sets the cookie the page authenticates with. */
export function daemonUiUrl(d: Pick<DaemonDiscovery, "port" | "token">): string {
  return daemonUrl(d, `/ui/?token=${d.token}`);
}

export interface DaemonResponse {
  status: number;
  /** The body parsed as JSON, or undefined when it is empty or not JSON. */
  json: unknown;
  text: string;
}

/**
 * One request to a route. Rejects when the daemon cannot be reached; any
 * answer, error statuses included, resolves.
 */
export function daemonRequest(
  d: Pick<DaemonDiscovery, "port" | "token">,
  route: DaemonRouteName,
  opts: { method?: string; params?: Record<string, string>; body?: unknown } = {},
): Promise<DaemonResponse> {
  return new Promise((resolve, reject) => {
    const payload = opts.body === undefined ? undefined : JSON.stringify(opts.body);
    const req = http.request({
      host: "127.0.0.1",
      port: d.port,
      path: routePath(route, opts.params),
      method: opts.method ?? "GET",
      headers: { ...bearer(d), ...(payload ? { "content-type": "application/json" } : {}) },
    }, (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c: Buffer) => chunks.push(c));
      res.on("end", () => {
        const text = Buffer.concat(chunks).toString("utf-8");
        let json: unknown;
        try {
          json = text ? JSON.parse(text) : undefined;
        } catch {
          json = undefined;
        }
        resolve({ status: res.statusCode ?? 500, json, text });
      });
      res.on("error", reject);
    });
    req.on("error", reject);
    if (payload) req.write(payload);
    req.end();
  });
}

/** Wait for a process to end, up to `ms`. True when it did. */
export async function waitForExit(pid: number, ms: number, isAlive: (pid: number) => boolean = isPidAlive, sleep = (t: number) => new Promise((r) => setTimeout(r, t))): Promise<boolean> {
  const deadline = Date.now() + ms;
  while (isAlive(pid)) {
    if (Date.now() >= deadline) return false;
    await sleep(200);
  }
  return true;
}

/**
 * Ask the daemon to exit and wait for its process to end, so a daemon started
 * right after never runs beside it. True when it ended within `waitMs`.
 */
export async function stopDaemon(d: Pick<DaemonDiscovery, "pid" | "port" | "token">, waitMs: number): Promise<boolean> {
  await daemonRequest(d, "shutdown", { method: "POST" }).catch(() => undefined);
  return waitForExit(d.pid, waitMs);
}
