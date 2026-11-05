/**
 * The bridge's session token, client side.
 *
 * When its port is bound the plugin writes a fresh random token to
 * `<token dir>/<port>.token`, readable only by the editor's user, and refuses
 * every WebSocket upgrade that does not carry it as `Authorization: Bearer`.
 * Every in-repo client builds its headers here; scripts/lib/bridge-token.mjs is
 * the same logic for plain-node scripts, held equal by
 * tests/unit/bridge-token.test.ts. The plugin half is BridgeSessionToken.cpp.
 *
 * No file means a plugin that predates the token, so the client connects
 * without one and keeps working with it.
 */
import * as fs from "node:fs";
import type { ClientRequest, IncomingMessage } from "node:http";
import * as path from "node:path";
import type WebSocket from "ws";

import { readEnv } from "../core/env.js";
import { userDir } from "../core/user-dir.js";

/** Response header the plugin sends on its 101 and its 401. */
export const BRIDGE_AUTH_SCHEME_HEADER = "x-ue-mcp-bridge-auth";

const TOKEN_PATTERN = /^[0-9a-f]{64}$/;

/** A token file that exists but must not be used. */
export class BridgeTokenError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "BridgeTokenError";
  }
}

/** `UE_MCP_BRIDGE_TOKEN_DIR`, else `~/.ue-mcp/bridge`. */
export function bridgeTokenDir(env: NodeJS.ProcessEnv = process.env): string {
  const override = readEnv("bridgeTokenDir", env);
  return override ? override : path.join(userDir(), "bridge");
}

export function bridgeTokenPath(port: number, env: NodeJS.ProcessEnv = process.env): string {
  return path.join(bridgeTokenDir(env), `${port}.token`);
}

/**
 * The token the editor on `port` published, or null when there is no file.
 * Throws BridgeTokenError for a file that exists but is not usable: one that is
 * not a regular file, on POSIX one another user owns or others can read, or
 * one that does not hold a token.
 */
export function readBridgeToken(port: number, env: NodeJS.ProcessEnv = process.env): string | null {
  const file = bridgeTokenPath(port, env);
  let stat: fs.Stats;
  try {
    stat = fs.lstatSync(file);
  } catch (e) {
    if ((e as NodeJS.ErrnoException).code === "ENOENT") return null;
    throw new BridgeTokenError(`Could not read the bridge session token at ${file}: ${(e as Error).message}`);
  }
  if (!stat.isFile()) throw new BridgeTokenError(`The bridge session token at ${file} is not a regular file.`);
  if (process.platform !== "win32") {
    const uid = process.getuid?.();
    if (uid !== undefined && stat.uid !== uid) {
      throw new BridgeTokenError(`The bridge session token at ${file} is owned by uid ${stat.uid}, not this user (${uid}). Refusing to use it.`);
    }
    if ((stat.mode & 0o077) !== 0) {
      throw new BridgeTokenError(
        `The bridge session token at ${file} is readable by other users (mode ${(stat.mode & 0o777).toString(8)}). ` +
          `Restart the editor so it rewrites the file as 600.`,
      );
    }
  }
  const token = fs.readFileSync(file, "utf8").trim();
  if (!TOKEN_PATTERN.test(token)) throw new BridgeTokenError(`The bridge session token at ${file} does not hold a token.`);
  return token;
}

/** Headers for an upgrade to the bridge on `port`: the bearer token when one is published, else none. */
export function bridgeAuthHeaders(port: number, env: NodeJS.ProcessEnv = process.env): Record<string, string> {
  const token = readBridgeToken(port, env);
  return token ? { Authorization: `Bearer ${token}` } : {};
}

/**
 * Explain a refused upgrade in terms the reader can act on. `sentToken` is
 * whether the request carried one.
 */
export function describeBridgeRefusal(opts: {
  port: number;
  status: number;
  body: string;
  sentToken: boolean;
  schemeHeader?: string | string[];
  env?: NodeJS.ProcessEnv;
}): string {
  const { port, status, sentToken } = opts;
  const body = opts.body.trim();
  const plugin = body ? ` The editor said: ${body}` : "";
  const file = bridgeTokenPath(port, opts.env);
  if (status !== 401) {
    return `The editor bridge on port ${port} refused the connection with HTTP ${status}.${plugin}`;
  }
  if (opts.schemeHeader === undefined) {
    return `Port ${port} answered 401 and did not identify itself as a UE-MCP bridge.${plugin}`;
  }
  if (!sentToken) {
    return (
      `The editor bridge on port ${port} requires a session token, and there is none at ${file}. ` +
      `The editor writes it there when its bridge starts. Run this client as the same OS user as the editor, ` +
      `and give both the same UE_MCP_BRIDGE_TOKEN_DIR if either sets it.${plugin}`
    );
  }
  return (
    `The editor bridge on port ${port} rejected the session token in ${file}. ` +
    `The file is stale or was written by another editor on that port; restart the editor if this persists.${plugin}`
  );
}

/**
 * Turn a non-101 answer to the upgrade into one call of `onRefused` with a
 * readable message, instead of ws's bare "Unexpected server response". Once
 * the body is read the socket is terminated, so ws then emits error and close;
 * callers settle on the first of the three.
 */
export function watchUpgradeRefusal(
  ws: WebSocket,
  port: number,
  sentToken: boolean,
  onRefused: (message: string, status: number) => void,
): void {
  ws.once("unexpected-response", (_req: ClientRequest, res: IncomingMessage) => {
    const chunks: Buffer[] = [];
    let size = 0;
    let done = false;
    const finish = (): void => {
      if (done) return;
      done = true;
      const message = describeBridgeRefusal({
        port,
        status: res.statusCode ?? 0,
        body: Buffer.concat(chunks).toString("utf8"),
        sentToken,
        schemeHeader: res.headers[BRIDGE_AUTH_SCHEME_HEADER],
      });
      onRefused(message, res.statusCode ?? 0);
      ws.terminate();
    };
    res.on("data", (chunk: Buffer) => {
      if (size < 4096) chunks.push(chunk);
      size += chunk.length;
    });
    res.on("end", finish);
    res.on("error", finish);
    res.on("close", finish);
  });
}
