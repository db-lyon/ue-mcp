// The bridge's session token for plain-node scripts. Same rules as
// src/bridge/bridge-token.ts, which the shipped server uses; the two are held
// equal by tests/unit/bridge-token.test.ts. Read that module's header for the
// contract.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

export const BRIDGE_AUTH_SCHEME_HEADER = "x-ue-mcp-bridge-auth";
const TOKEN_PATTERN = /^[0-9a-f]{64}$/;

/** `UE_MCP_BRIDGE_TOKEN_DIR`, else `~/.ue-mcp/bridge`. */
export function bridgeTokenDir(env = process.env) {
  const override = env.UE_MCP_BRIDGE_TOKEN_DIR;
  return override ? override : path.join(os.homedir(), ".ue-mcp", "bridge");
}

export function bridgeTokenPath(port, env = process.env) {
  return path.join(bridgeTokenDir(env), `${port}.token`);
}

/** The published token for `port`, null without a file; throws for a file that must not be used. */
export function readBridgeToken(port, env = process.env) {
  const file = bridgeTokenPath(port, env);
  let stat;
  try {
    stat = fs.lstatSync(file);
  } catch (e) {
    if (e && e.code === "ENOENT") return null;
    throw new Error(`Could not read the bridge session token at ${file}: ${e instanceof Error ? e.message : String(e)}`);
  }
  if (!stat.isFile()) throw new Error(`The bridge session token at ${file} is not a regular file.`);
  if (process.platform !== "win32") {
    const uid = process.getuid?.();
    if (uid !== undefined && stat.uid !== uid) throw new Error(`The bridge session token at ${file} is owned by uid ${stat.uid}, not this user (${uid}).`);
    if ((stat.mode & 0o077) !== 0) throw new Error(`The bridge session token at ${file} is readable by other users; restart the editor so it rewrites it.`);
  }
  const token = fs.readFileSync(file, "utf8").trim();
  if (!TOKEN_PATTERN.test(token)) throw new Error(`The bridge session token at ${file} does not hold a token.`);
  return token;
}

export function bridgeAuthHeaders(port, env = process.env) {
  const token = readBridgeToken(port, env);
  return token ? { Authorization: `Bearer ${token}` } : {};
}

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
      const body = Buffer.concat(chunks).toString("utf8").trim();
      const said = body ? ` The editor said: ${body}` : "";
      const file = bridgeTokenPath(port);
      let message;
      if (res.statusCode !== 401) message = `The editor bridge on port ${port} refused the connection with HTTP ${res.statusCode}.${said}`;
      else if (res.headers[BRIDGE_AUTH_SCHEME_HEADER] === undefined) message = `Port ${port} answered 401 and did not identify itself as a UE-MCP bridge.${said}`;
      else if (!sentToken) message = `The editor bridge on port ${port} requires a session token, and there is none at ${file}. Run as the same OS user as the editor.${said}`;
      else message = `The editor bridge on port ${port} rejected the session token in ${file}; restart the editor if this persists.${said}`;
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
