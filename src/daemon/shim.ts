/**
 * The stdio shim: what an MCP client launches as `ue-mcp <uproject>` when the
 * daemon is on. It finds the project's daemon (starting one if none is
 * running) and relays MCP messages to it over a WebSocket.
 *
 * The client's connection is to the shim, so an editor restart is invisible to
 * it, and a daemon restart costs only a reconnect: the shim replays the
 * client's `initialize` to the new daemon itself and swallows the answer. A
 * request in flight when the daemon went away is answered with an error, never
 * left hanging, because its outcome is unknown.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawn } from "node:child_process";
import WebSocket from "ws";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import type { JSONRPCMessage } from "@modelcontextprotocol/sdk/types.js";
import { info, warn, error } from "../core/log.js";
import { packageVersion } from "../core/package-root.js";
import { DAEMON_API_VERSION, daemonDir, projectKey, readLiveDiscovery, type DaemonDiscovery } from "./discovery.js";
import { isOlder } from "./daemon.js";

const START_TIMEOUT_MS = 120_000;
/** JSON-RPC error code for "the daemon went away while this request was out". */
const DAEMON_LOST = -32001;

/** Start a detached daemon for the project and wait for it to publish itself. */
export async function ensureDaemon(uproject: string, wantVersion = packageVersion()): Promise<DaemonDiscovery> {
  const projectDir = path.dirname(path.resolve(uproject));
  const live = readLiveDiscovery(projectDir);
  if (live && live.apiVersion === DAEMON_API_VERSION && !isOlder(live.version, wantVersion)) return live;

  // The daemon runs the same entry this process was started with, so a dev
  // checkout under tsx starts a dev daemon and a published package its own.
  const logDir = daemonDir();
  fs.mkdirSync(logDir, { recursive: true });
  const log = fs.openSync(path.join(logDir, `${projectKey(projectDir)}.log`), "a");
  const child = spawn(process.execPath, [...process.execArgv, process.argv[1], "daemon", "run", path.resolve(uproject)], {
    detached: true,
    stdio: ["ignore", log, log],
    windowsHide: true,
    env: process.env,
  });
  child.unref();
  fs.closeSync(log);
  info("shim", `started daemon (pid ${child.pid}) for ${path.basename(projectDir)}`);

  const deadline = Date.now() + START_TIMEOUT_MS;
  while (Date.now() < deadline) {
    const d = readLiveDiscovery(projectDir);
    if (d && d.apiVersion === DAEMON_API_VERSION && !isOlder(d.version, wantVersion)) return d;
    await new Promise((r) => setTimeout(r, 250));
  }
  throw new Error(`daemon for ${projectDir} did not start within ${START_TIMEOUT_MS / 1000}s; see ${path.join(logDir, `${projectKey(projectDir)}.log`)}`);
}

export async function runShim(uproject: string): Promise<void> {
  const stdio = new StdioServerTransport();
  let ws: WebSocket | null = null;
  let initialize: JSONRPCMessage | null = null;
  let initialized: JSONRPCMessage | null = null;
  /** Ids of requests the client is waiting on. */
  const waiting = new Set<string | number>();
  /** Messages from the client that arrived while there was no daemon socket. */
  const queue: JSONRPCMessage[] = [];
  /** Id of a replayed initialize whose answer is the shim's, not the client's. */
  let swallow: string | number | null = null;
  let closed = false;

  const toDaemon = (m: JSONRPCMessage): void => {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(m));
    else queue.push(m);
  };

  stdio.onmessage = (m) => {
    if ("method" in m && m.method === "initialize") initialize = m;
    if ("method" in m && m.method === "notifications/initialized") initialized = m;
    if ("id" in m && "method" in m) waiting.add(m.id as string | number);
    toDaemon(m);
  };
  stdio.onclose = () => {
    closed = true;
    ws?.close();
    process.exit(0);
  };

  const connect = async (replay: boolean): Promise<void> => {
    const d = await ensureDaemon(uproject);
    const socket = new WebSocket(`ws://127.0.0.1:${d.port}/v1/mcp`, {
      headers: { authorization: `Bearer ${d.token}`, "x-ue-mcp-client": "stdio-shim" },
    });
    await new Promise<void>((resolve, reject) => {
      socket.once("open", () => resolve());
      socket.once("error", reject);
    });
    ws = socket;

    socket.on("message", (data) => {
      let m: JSONRPCMessage;
      try {
        m = JSON.parse(data.toString()) as JSONRPCMessage;
      } catch {
        return;
      }
      if ("id" in m && !("method" in m)) {
        if (m.id === swallow) {
          swallow = null;
          if (initialized) socket.send(JSON.stringify(initialized));
          flush();
          return;
        }
        waiting.delete(m.id as string | number);
      }
      void stdio.send(m);
    });
    socket.on("close", () => {
      if (closed) return;
      ws = null;
      // Whatever was out is unknowable now; say so rather than hang.
      for (const id of waiting) {
        void stdio.send({
          jsonrpc: "2.0",
          id,
          error: { code: DAEMON_LOST, message: "The ue-mcp daemon stopped while this call was in flight. Its outcome is unknown: read the state back before retrying." },
        } as JSONRPCMessage);
      }
      waiting.clear();
      warn("shim", "daemon connection lost; reconnecting");
      void reconnect();
    });

    if (replay && initialize) {
      swallow = (initialize as { id: string | number }).id;
      socket.send(JSON.stringify(initialize));
    } else {
      flush();
    }
  };

  const flush = (): void => {
    while (queue.length > 0 && ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(queue.shift()));
  };

  const reconnect = async (): Promise<void> => {
    for (let attempt = 0; !closed; attempt++) {
      try {
        await connect(true);
        info("shim", "reconnected to daemon");
        return;
      } catch (e) {
        if (attempt === 0) warn("shim", "daemon not reachable yet", e);
        await new Promise((r) => setTimeout(r, Math.min(5000, 250 * 2 ** attempt)));
      }
    }
  };

  // Read stdin from the start so nothing the client sends is lost while the daemon starts.
  await stdio.start();
  try {
    await connect(false);
  } catch (e) {
    error("shim", "could not reach or start the daemon", e);
    process.exit(1);
  }
}
