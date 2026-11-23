// @session per-session: Relays one MCP client to the daemon of the project it was started for.
/**
 * The stdio shim: what an MCP client launches as `ue-mcp <uproject>` when the
 * daemon is on. It finds the project's daemon (starting one if none is
 * running) and relays MCP messages to it over a WebSocket.
 *
 * The shim answers `initialize` itself, from instructions it composes for the
 * project, without waiting for the daemon: a cold daemon can take longer to
 * start than a client waits for a server to initialize. Every other request
 * waits for the daemon and is relayed once it is up.
 *
 * The client's connection is to the shim, so an editor restart is invisible to
 * it, and a daemon restart costs only a reconnect: on every connection the
 * shim sends the client's `initialize` to the daemon itself and swallows the
 * answer. A request in flight when the daemon went away is answered with an
 * error, never left hanging, because its outcome is unknown. A daemon that
 * cannot be started at all is replaced by a server in this process, behind the
 * same stdio connection.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawnOutsideTree } from "../core/spawn-outside-tree.js";
import WebSocket from "ws";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import { LATEST_PROTOCOL_VERSION, SUPPORTED_PROTOCOL_VERSIONS, type JSONRPCMessage, type JSONRPCRequest } from "@modelcontextprotocol/sdk/types.js";
import { info, warn, error } from "../core/log.js";
import { readEnv } from "../core/env.js";
import { packageVersion, selfCliArgs } from "../core/package-root.js";
import { compareVersions } from "../core/semver.js";
import { apiVersionsOverlap, DAEMON_API_RANGE, DAEMON_HEADERS, routePath, type DaemonDiscovery } from "../contract/daemon.js";
import { bearer } from "./client.js";
import { daemonDir, projectKey, readAnsweringDiscovery, type AnsweringDiscoveryOptions } from "./discovery.js";
import { liveStartLock } from "./start-lock.js";
import { installedBridge } from "../editor/install-marker.js";
import { NPX_AGE_GATE_OVERRIDE } from "../integrations/mcp-clients/mcp-client-config.js";

const START_TIMEOUT_MS = 120_000;
/** How long a spawned daemon has to take the start lock before it counts as dead. */
const NO_STARTER_GRACE_MS = 20_000;
/** The same through npx, which may download the version first. */
const NPX_STARTER_GRACE_MS = 90_000;
/** JSON-RPC error code for "the daemon went away while this request was out". */
const DAEMON_LOST = -32001;

/**
 * The version the project's daemon has to run. A binary bridge newer than this
 * package came from a newer release, so the daemon is that release: a client
 * launching `ue-mcp@latest` must not put a beta tester's project on the stable
 * server. A source install, or one this package is not older than, runs here.
 */
export function daemonVersionFor(projectDir: string, own = packageVersion()): string {
  const bridge = installedBridge(projectDir);
  return bridge && bridge.kind === "binary" && compareVersions(own, bridge.version) < 0 ?bridge.version : own;
}

/** npm's npx entry beside this node, which runs without a shell; null when it is not there. */
export function npxCliPath(execPath = process.execPath, platform: NodeJS.Platform = process.platform): string | null {
  const dir = path.dirname(execPath);
  const candidate = platform === "win32"
    ? path.join(dir, "node_modules", "npm", "bin", "npx-cli.js")
    : path.join(dir, "..", "lib", "node_modules", "npm", "bin", "npx-cli.js");
  return fs.existsSync(candidate) ? candidate : null;
}

/** The command line that runs the daemon: this entry, or `npx ue-mcp@<version>` for another version. */
export function daemonCommand(uproject: string, version: string, npxCli: string | null = npxCliPath()): string[] | null {
  if (version === packageVersion()) return selfCliArgs(["daemon", "run", path.resolve(uproject)]);
  if (!npxCli) return null;
  return [npxCli, "-y", NPX_AGE_GATE_OVERRIDE, `ue-mcp@${version}`, "daemon", "run", path.resolve(uproject)];
}

/**
 * Start a detached daemon for the project and wait for it to publish itself.
 * No process is spawned while another starter holds the start lock. The daemon
 * runs the version daemonVersionFor names; when that one cannot start, this
 * package's own runs instead.
 */
export async function ensureDaemon(uproject: string, wantVersion?: string, probe: AnsweringDiscoveryOptions = {}): Promise<DaemonDiscovery> {
  const projectDir = path.dirname(path.resolve(uproject));
  const logDir = daemonDir();
  const logFile = path.join(logDir, `${projectKey(projectDir)}.log`);
  let want = wantVersion ?? daemonVersionFor(projectDir);
  const usable = (d: DaemonDiscovery | null): d is DaemonDiscovery =>
    !!d && apiVersionsOverlap({ min: d.apiVersion, max: d.apiVersion }, DAEMON_API_RANGE) && compareVersions(d.version, want) >= 0;

  let spawnedAt: number | null = null;
  let sawStarter = false;
  const deadline = Date.now() + START_TIMEOUT_MS;
  while (Date.now() < deadline) {
    // A record proven stale is dropped here, so the spawn below replaces it (#1460).
    const d = await readAnsweringDiscovery(projectDir, probe);
    if (usable(d)) return d;
    const starting = liveStartLock(projectDir) !== null;
    if (spawnedAt !== null) {
      if (starting) sawStarter = true;
      // The daemon we started is not starting any more and published nothing, or
      // never got as far as taking the lock: waiting out the timeout cannot help.
      else if (sawStarter || Date.now() - spawnedAt > (want === packageVersion() ? NO_STARTER_GRACE_MS : NPX_STARTER_GRACE_MS)) {
        if (want === packageVersion()) throw new Error(`daemon for ${projectDir} exited before it started; see ${logFile}`);
        warn("shim", `ue-mcp ${want} did not start a daemon; starting this package's ${packageVersion()} instead`);
        want = packageVersion();
        spawnedAt = null;
        sawStarter = false;
        continue;
      }
    } else if (!starting) {
      if (!spawnDaemon(uproject, want, projectDir, logDir, logFile)) want = packageVersion();
      else spawnedAt = Date.now();
    }
    await new Promise((r) => setTimeout(r, 250));
  }
  throw new Error(`daemon for ${projectDir} did not start within ${START_TIMEOUT_MS / 1000}s; see ${logFile}`);
}

function spawnDaemon(uproject: string, version: string, projectDir: string, logDir: string, logFile: string): boolean {
  const args = daemonCommand(uproject, version);
  if (!args) {
    warn("shim", `the bridge is ${version}, newer than this ue-mcp ${packageVersion()}, and npx was not found beside node to run it`);
    return false;
  }
  fs.mkdirSync(logDir, { recursive: true });
  const log = fs.openSync(logFile, "a");
  // Outside the shim's process tree: a client that ends the tree on reconnect
  // would otherwise end the daemon and the editor it launched (#1274).
  spawnOutsideTree(process.execPath, args, { output: log, env: process.env });
  fs.closeSync(log);
  info("shim", `started daemon ${version} for ${path.basename(projectDir)}`);
  return true;
}

/**
 * What `initialize` is answered with before any server is up. The servers
 * behind the shim (the daemon's, and the in-process fallback) advertise the
 * same; `listChanged` is what lets a tool list that changes after initialize
 * reach the client.
 */
export const SHIM_CAPABILITIES = {
  tools: { listChanged: true },
  resources: { listChanged: true },
  prompts: { listChanged: true },
} as const;

/** The protocol version a server answers a client's initialize with: the client's when supported, else the newest. */
export function negotiatedProtocolVersion(requested: unknown): string {
  return typeof requested === "string" && SUPPORTED_PROTOCOL_VERSIONS.includes(requested) ? requested : LATEST_PROTOCOL_VERSION;
}

/** The id the shim gives its own `initialize` to a server, never one a client uses. */
const SHIM_INITIALIZE_ID = "ue-mcp-shim-initialize";

/** A server in this process, served when no daemon can be started. */
export interface InProcessServer {
  /** The client end of a transport the server is connected to. */
  transport: Transport;
  /** Keep the process alive after the MCP client goes, for the flow HTTP surface. */
  outlivesClient: boolean;
}

export interface ShimOptions {
  /** The instructions `initialize` is answered with. A failure answers with none. */
  instructions: () => Promise<string>;
  /** Start a server in this process; called when the daemon cannot be started. */
  inProcess: () => Promise<InProcessServer>;
}

/** One server the shim relays to: the daemon over its socket, or a server in this process. */
interface Backend {
  send(m: JSONRPCMessage): void;
  close(): void;
}

export async function runShim(uproject: string, opts: ShimOptions): Promise<void> {
  const stdio = new StdioServerTransport();
  let backend: Backend | null = null;
  /** True once the current backend has answered the shim's initialize. */
  let ready = false;
  let everReady = false;
  let initialize: JSONRPCRequest | null = null;
  let resolveInitialize!: () => void;
  const initializeArrived = new Promise<void>((resolve) => (resolveInitialize = resolve));
  /** Ids of client requests sent to the current backend and not answered yet. */
  const waiting = new Set<string | number>();
  /** Client messages held until a backend is ready. */
  const queue: JSONRPCMessage[] = [];
  /** Whether the client asked for the tool list before any backend was ready, and may have given up on it. */
  let listedEarly = false;
  let closed = false;
  let outlivesClient = false;
  /** The instructions this client was given, compared with the backend's own. */
  let answeredInstructions: string | null = null;

  // Composed from the start, so the answer is ready by the time initialize arrives.
  const instructions = opts.instructions().catch((e: unknown) => {
    warn("shim", "could not compose the initialize instructions; answering without them", e);
    return "";
  });

  /** The MCP client's own name once it has sent initialize, so the daemon's activity feed names it. */
  const clientName = (): string =>
    ((initialize as { params?: { clientInfo?: { name?: string } } } | null)?.params?.clientInfo?.name) ?? "stdio-shim";

  const sendToBackend = (m: JSONRPCMessage): void => {
    if ("id" in m && "method" in m) waiting.add(m.id as string | number);
    backend!.send(m);
  };

  const answerInitialize = async (m: JSONRPCRequest): Promise<void> => {
    const text = await instructions;
    answeredInstructions = text;
    await stdio.send({
      jsonrpc: "2.0",
      id: m.id,
      result: {
        protocolVersion: negotiatedProtocolVersion((m.params as { protocolVersion?: unknown } | undefined)?.protocolVersion),
        capabilities: SHIM_CAPABILITIES,
        serverInfo: { name: "ue-mcp", version: packageVersion() },
        ...(text ? { instructions: text } : {}),
      },
    } as JSONRPCMessage);
  };

  stdio.onmessage = (m) => {
    if ("method" in m && m.method === "initialize" && "id" in m) {
      initialize = m as JSONRPCRequest;
      resolveInitialize();
      void answerInitialize(m as JSONRPCRequest);
      return;
    }
    // The shim sends its own initialized to each backend after the handshake.
    if ("method" in m && m.method === "notifications/initialized") return;
    if ("method" in m && m.method === "ping" && "id" in m) {
      void stdio.send({ jsonrpc: "2.0", id: m.id, result: {} } as JSONRPCMessage);
      return;
    }
    if (ready) {
      sendToBackend(m);
      return;
    }
    if (!everReady && "method" in m && m.method === "tools/list") listedEarly = true;
    queue.push(m);
  };
  stdio.onclose = () => {
    closed = true;
    backend?.close();
    if (!outlivesClient) process.exit(0);
  };
  process.stdin.once("end", () => void stdio.close());

  /** Handle one message from the current backend. */
  const fromBackend = (m: JSONRPCMessage): void => {
    if ("id" in m && !("method" in m)) {
      if (m.id === SHIM_INITIALIZE_ID) {
        handshakeDone(m);
        return;
      }
      waiting.delete(m.id as string | number);
    }
    void stdio.send(m);
  };

  const handshakeDone = (m: JSONRPCMessage): void => {
    // A refusal is logged and the calls still go through, so the client hears
    // the server's own errors rather than waiting on a queue.
    if ("error" in m) error("shim", "the server refused initialize", m.error);
    const theirs = (m as { result?: { instructions?: string } }).result?.instructions ?? "";
    if ("result" in m && answeredInstructions !== null && theirs !== answeredInstructions) {
      warn("shim", "the server's initialize instructions differ from the ones this client was given; the client keeps its own until it reconnects");
    }
    backend!.send({ jsonrpc: "2.0", method: "notifications/initialized" } as JSONRPCMessage);
    ready = true;
    const first = !everReady;
    everReady = true;
    while (queue.length > 0 && ready) sendToBackend(queue.shift()!);
    // A client that gave up waiting for its first tool list fetches it again on this.
    if (first && listedEarly) void stdio.send({ jsonrpc: "2.0", method: "notifications/tools/list_changed" } as JSONRPCMessage);
  };

  /** Make `next` the backend and introduce the client to it. */
  const attach = (next: Backend): void => {
    backend = next;
    ready = false;
    next.send({ ...initialize!, id: SHIM_INITIALIZE_ID } as JSONRPCMessage);
  };

  const connectDaemon = async (): Promise<void> => {
    const d = await ensureDaemon(uproject);
    // The socket carries the client's name, which only its initialize says.
    await initializeArrived;
    const socket = new WebSocket(`ws://127.0.0.1:${d.port}${routePath("mcp")}`, {
      headers: {
        ...bearer(d),
        [DAEMON_HEADERS.client]: clientName(),
        // A subagent with its own connection names its primary (#2075).
        ...(readEnv("parentSession") ? { [DAEMON_HEADERS.parent]: readEnv("parentSession")! } : {}),
      },
    });
    await new Promise<void>((resolve, reject) => {
      socket.once("open", () => resolve());
      socket.once("error", reject);
    });
    const self: Backend = { send: (m) => socket.send(JSON.stringify(m)), close: () => socket.close() };
    socket.on("message", (data) => {
      let m: JSONRPCMessage;
      try {
        m = JSON.parse(data.toString()) as JSONRPCMessage;
      } catch {
        return;
      }
      fromBackend(m);
    });
    socket.on("close", () => {
      if (closed || backend !== self) return;
      backend = null;
      ready = false;
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
    attach(self);
  };

  const reconnect = async (): Promise<void> => {
    for (let attempt = 0; !closed; attempt++) {
      try {
        await connectDaemon();
        info("shim", "reconnected to daemon");
        return;
      } catch (e) {
        if (attempt === 0) warn("shim", "daemon not reachable yet", e);
        await new Promise((r) => setTimeout(r, Math.min(5000, 250 * 2 ** attempt)));
      }
    }
  };

  // Read stdin from the start: initialize is answered here, and nothing the
  // client sends while the daemon starts is lost.
  await stdio.start();
  try {
    await connectDaemon();
    return;
  } catch (e) {
    warn("shim", "daemon unavailable; serving in process", e);
  }
  try {
    const local = await opts.inProcess();
    outlivesClient = local.outlivesClient;
    await initializeArrived;
    local.transport.onmessage = fromBackend;
    await local.transport.start();
    attach({ send: (m) => void local.transport.send(m), close: () => void local.transport.close() });
  } catch (e) {
    error("shim", "could not start a server in process either", e);
    process.exit(1);
  }
}
