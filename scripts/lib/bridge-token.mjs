// The bridge's session token for plain-node scripts. The rules (where the file
// lives, what it must hold, how a refusal reads) are the generated wire
// contract's, shared with the shipped server; this adds only what a script
// needs to open a socket from a URL.
import {
  bridgeAuthHeaders,
  bridgeTokenDir,
  bridgeTokenPath,
  describeBridgeRefusal,
  readBridgeToken,
  TOKEN,
} from "./wire-contract.mjs";

export { bridgeAuthHeaders, bridgeTokenDir, bridgeTokenPath, readBridgeToken };

/** Response header the plugin sends on its 101 and its 401, as node lowercases it. */
export const BRIDGE_AUTH_SCHEME_HEADER = TOKEN.schemeHeader.toLowerCase();

/** The port a `ws://host:port` URL names. */
export function portOfUrl(url) {
  return Number(new URL(url).port);
}

/** `new WebSocket(url)` options carrying the token for the URL's port. */
export function bridgeSocketOptions(url, env = process.env) {
  return { headers: bridgeAuthHeaders(portOfUrl(url), env) };
}

/**
 * Report a non-101 answer to the upgrade as one readable Error through
 * `onRefused`, then terminate the socket.
 */
export function watchUpgradeRefusal(ws, url, onRefused) {
  const port = portOfUrl(url);
  const sentToken = Boolean(readBridgeTokenQuiet(port));
  ws.once("unexpected-response", (_req, res) => {
    const chunks = [];
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      const message = describeBridgeRefusal({
        port,
        status: res.statusCode ?? 0,
        body: Buffer.concat(chunks).toString("utf8"),
        sentToken,
        schemeHeader: res.headers[BRIDGE_AUTH_SCHEME_HEADER],
      });
      onRefused(new Error(message));
      ws.terminate();
    };
    res.on("data", (c) => chunks.push(c));
    res.on("end", finish);
    res.on("error", finish);
    res.on("close", finish);
  });
}

function readBridgeTokenQuiet(port) {
  try {
    return readBridgeToken(port);
  } catch {
    return null;
  }
}
