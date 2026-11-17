// @session independent: Reads one request's Host header and compares tokens. Pure logic.
import type * as http from "node:http";
import { timingSafeEqual } from "node:crypto";

/** Whether a presented token is the expected one, compared in constant time. */
export function tokensMatch(presented: string | null | undefined, expected: string): boolean {
  if (typeof presented !== "string") return false;
  const a = Buffer.from(presented, "utf8");
  const b = Buffer.from(expected, "utf8");
  return a.length === b.length && timingSafeEqual(a, b);
}

const ALLOWED_HOSTS = new Set([
  "localhost",
  "127.0.0.1",
  "[::1]",
]);

/**
 * Whether a request names a loopback host. A page on another origin that
 * resolves its own name to 127.0.0.1 (DNS rebinding) still sends its own name
 * here, so the loopback servers refuse it.
 */
export function hostIsAllowed(req: http.IncomingMessage): boolean {
  const host = req.headers.host;
  if (typeof host !== "string" || host.length === 0) return false;
  // strip optional :port (handle bracketed IPv6 too)
  const bare = host.startsWith("[")
    ? host.slice(0, host.indexOf("]") + 1)
    : host.replace(/:\d+$/, "");
  return ALLOWED_HOSTS.has(bare.toLowerCase());
}
