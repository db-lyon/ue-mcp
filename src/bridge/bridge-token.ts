/**
 * The bridge's session token, client side.
 *
 * When its port is bound the plugin writes a fresh random token to
 * `<token dir>/<port>.token`, readable only by the editor's user, and refuses
 * every WebSocket upgrade that does not carry it as `Authorization: Bearer`.
 * Where the file lives, what it must hold and how a refusal reads are the
 * generated wire contract's (wire-contract.ts), which the plain-node scripts
 * share; this module adds the socket half. The plugin half is
 * BridgeSessionToken.cpp.
 *
 * No file means a plugin that predates the token, so the client connects
 * without one and keeps working with it.
 */
import type { ClientRequest, IncomingMessage } from "node:http";
import type WebSocket from "ws";

import { describeBridgeRefusal, TOKEN } from "./wire-contract.js";

export {
  BridgeTokenError,
  bridgeAuthHeaders,
  bridgeTokenDir,
  bridgeTokenPath,
  describeBridgeRefusal,
  readBridgeToken,
} from "./wire-contract.js";

/** Response header the plugin sends on its 101 and its 401, as node lowercases it. */
export const BRIDGE_AUTH_SCHEME_HEADER = TOKEN.schemeHeader.toLowerCase();

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
