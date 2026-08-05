import WebSocket from "ws";
import * as fs from "node:fs";
import * as path from "node:path";
import { createRequire } from "node:module";
import { McpError, ErrorCode } from "./errors.js";
import { debug, warn } from "./log.js";
import { DEFAULT_BRIDGE_PORT, deriveProjectPort } from "./port.js";

/**
 * The wire protocol this client speaks. Must match
 * UEMCP_BRIDGE_PROTOCOL_VERSION in the plugin's MCPHandlerRegistration.h.
 *
 * The plugin is compiled by the user, and `attach` is deliberately
 * non-destructive, so an npm upgrade routinely leaves a new client talking to
 * an arbitrarily old binary. Comparing these two numbers is how that gets
 * named instead of surfacing as an unexplained "Unknown method".
 */
export const CLIENT_PROTOCOL_VERSION = 2;

/** Answer to get_bridge_capabilities, or what we infer when there is none. */
export interface BridgeCapabilities {
  protocolVersion: number;
  handlerApiVersion?: number;
  /** Compile timestamp of the loaded plugin binary. The stale-DLL tell. */
  builtAt?: string;
  engineVersion?: string;
  projectName?: string;
  instanceId?: string;
  pid?: number;
  port?: number;
  startedAt?: string;
  features?: string[];
  actions?: string[];
  actionCount?: number;
  /** True when the bridge did not answer the handshake at all. */
  legacy: boolean;
}

/** What a bridge that predates the handshake looks like. */
const LEGACY_CAPABILITIES: BridgeCapabilities = { protocolVersion: 1, legacy: true };

let cachedClientVersion: string | null = null;
function clientPackageVersion(): string {
  if (cachedClientVersion) return cachedClientVersion;
  try {
    const require = createRequire(import.meta.url);
    cachedClientVersion = (require("../package.json") as { version: string }).version;
  } catch {
    cachedClientVersion = "unknown";
  }
  return cachedClientVersion;
}

/**
 * Describe a client/plugin protocol mismatch in terms the reader can act on,
 * naming both versions. Returns null when the two agree.
 */
export function describeProtocolMismatch(
  capabilities: BridgeCapabilities | null,
  method?: string,
): string | null {
  if (!capabilities) return null;
  const theirs = capabilities.protocolVersion;
  const ours = CLIENT_PROTOCOL_VERSION;
  if (theirs === ours) return null;

  const built = capabilities.builtAt ? ` The loaded plugin binary was built ${capabilities.builtAt}.` : "";
  const missing =
    method && capabilities.actions && !capabilities.actions.includes(method)
      ? ` '${method}' is not among the ${capabilities.actionCount ?? capabilities.actions.length} actions the running plugin registered.`
      : "";

  if (theirs < ours) {
    return (
      `The Unreal bridge plugin speaks protocol version ${theirs}, this ue-mcp client (v${clientPackageVersion()}) speaks version ${ours}.` +
      `${missing}${built}` +
      ` Rebuild the plugin against the current package: run 'npx ue-mcp update' in the project, then rebuild the editor binaries.`
    );
  }
  return (
    `The Unreal bridge plugin speaks protocol version ${theirs}, this ue-mcp client (v${clientPackageVersion()}) speaks only version ${ours}.` +
    `${built}` +
    ` Update the npm package to match the plugin: 'npm install ue-mcp@latest'.`
  );
}

/** The record the running bridge publishes for this project. */
export interface BridgeLockfile {
  port: number;
  pid?: number;
  startedAt?: string;
  /** Identifies the server object that wrote this, across pid recycling. */
  instanceId?: string;
  status?: string;
  apiVersion?: number;
  handlerApiVersion?: number;
}

/** What the bridge leaves behind when the editor started but the bridge did not. */
export interface BridgeErrorRecord {
  status?: string;
  pid?: number;
  failedAt?: string;
  firstPortTried?: number;
  lastPortTried?: number;
  errorCode?: number;
  detail?: string;
}

function readJsonFile<T>(file: string): T | null {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8")) as T;
  } catch {
    return null;
  }
}

/**
 * True when a process with this id exists. Signal 0 performs the permission
 * check without delivering anything; EPERM means it exists but belongs to
 * someone else, which still counts as alive.
 */
function isProcessAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (e) {
    return (e as NodeJS.ErrnoException).code === "EPERM";
  }
}

// #492: per-project port lockfile published by the bridge plugin. When the
// default port (9877) is taken by another editor, the plugin walks up and
// publishes the actual bound port here. The client reads this before
// falling back to the default port so a second editor finds the right one.
export function readBridgeLockfile(uprojectPath: string | null): BridgeLockfile | null {
  if (!uprojectPath) return null;
  return readBridgeLockfileForDir(path.dirname(uprojectPath));
}

/** Same record, for callers that hold the project directory rather than the .uproject. */
export function readBridgeLockfileForDir(projectDir: string | null | undefined): BridgeLockfile | null {
  if (!projectDir) return null;
  const parsed = readJsonFile<BridgeLockfile>(path.join(projectDir, "Saved", "UE_MCP_Bridge", "port.json"));
  if (!parsed || typeof parsed.port !== "number" || parsed.port <= 0 || parsed.port >= 65536) {
    return null;
  }

  // #821: the pid was written and never read. A record left by an editor that
  // crashed sent the client at a port nothing is listening on, and the
  // resulting failure named the wrong problem. A recycled pid can still read
  // as alive, which the connect attempt then settles.
  if (typeof parsed.pid === "number" && parsed.pid > 0 && !isProcessAlive(parsed.pid)) {
    debug("bridge", `ignoring stale bridge lockfile: process ${parsed.pid} is gone`);
    return null;
  }
  return parsed;
}

/**
 * #821: read the record the bridge writes when the editor came up but the
 * bridge could not bind. Without it, "editor alive, bridge dead" was
 * indistinguishable from "no editor", and the client said the wrong thing.
 */
export function readBridgeErrorRecord(uprojectPath: string | null): BridgeErrorRecord | null {
  if (!uprojectPath) return null;
  const parsed = readJsonFile<BridgeErrorRecord>(
    path.join(path.dirname(uprojectPath), "Saved", "UE_MCP_Bridge", "bridge-error.json"),
  );
  if (!parsed || parsed.status !== "bind-failed") return null;
  // A record from an editor that has since exited describes nothing current.
  if (typeof parsed.pid === "number" && parsed.pid > 0 && !isProcessAlive(parsed.pid)) return null;
  return parsed;
}

export interface BridgeResponse {
  id: string;
  result?: unknown;
  error?: { code: number; message: string };
}

interface PendingRequest {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  timer: ReturnType<typeof setTimeout>;
  /** Kept so an "unknown method" answer can name the method it was about. */
  method: string;
}

/** Minimal interface for tool handlers — enables mocking in tests. */
export interface IBridge {
  readonly isConnected: boolean;
  /** #821: what the connected bridge reported at handshake, when there is one. */
  readonly capabilities?: BridgeCapabilities | null;
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown>;
  connect(timeoutMs?: number): Promise<void>;
}

export class EditorBridge implements IBridge {
  private ws: WebSocket | null = null;
  private pending = new Map<string, PendingRequest>();
  private reconnectTimer: ReturnType<typeof setInterval> | null = null;
  private connectInFlight: Promise<void> | null = null;
  private idCounter = 0;

  /**
   * #821: what the bridge said it was, answered on connect. Null before the
   * first connection. `legacy` marks a plugin old enough not to answer at all.
   */
  public capabilities: BridgeCapabilities | null = null;

  // How this.port was decided. Precedence for the *preferred* port (the one
  // used when no editor lockfile is present) is explicit > env > config >
  // derived > default; the lockfile always overrides at connect time because
  // it is the port the editor actually bound. Deriving only kicks in while the
  // source is still "default", so an explicit/env/config pin is never
  // clobbered by the path hash.
  private portSource: "explicit" | "env" | "config" | "derived" | "default" = "default";

  constructor(host?: string, port?: number) {
    // #497: default to 127.0.0.1 so the client picks the loopback IPv4 the
    // plugin actually binds to. "localhost" can resolve to ::1 on systems
    // where the IPv6 stack wins DNS, leaving the client stuck connecting to
    // an empty IPv6 socket while the plugin owns 127.0.0.1:9877.
    // UE_MCP_HOST overrides the default for non-standard topologies.
    this.host = host ?? process.env.UE_MCP_HOST ?? "127.0.0.1";

    const envPort = Number.parseInt(process.env.UE_MCP_PORT ?? "", 10);
    if (typeof port === "number" && port > 0) {
      this.port = port;
      this.portSource = "explicit";
    } else if (Number.isFinite(envPort) && envPort > 0) {
      this.port = envPort;
      this.portSource = "env";
    } else {
      this.port = DEFAULT_BRIDGE_PORT;
      this.portSource = "default";
    }
  }

  public host: string;
  public port: number;

  /**
   * Apply an explicit `bridge.port` from ue-mcp.yml. Ignored when an
   * explicit constructor arg or UE_MCP_PORT already pinned the port.
   */
  setConfigPort(port?: number): void {
    if (typeof port === "number" && port > 0 && this.portSource !== "explicit" && this.portSource !== "env") {
      this.port = port;
      this.portSource = "config";
    }
  }

  get isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  async ensureConnected(timeoutMs = 5000): Promise<void> {
    if (this.isConnected) return;

    if (!this.connectInFlight) {
      this.connectInFlight = this.connect(timeoutMs).finally(() => {
        this.connectInFlight = null;
      });
    }

    await this.connectInFlight;
  }

  /**
   * #492: project context for resolving the per-project port lockfile. Set
   * by index.ts (via setProjectContext) after the user's .uproject is loaded.
   * Leaving this null keeps the default-port-only behaviour for callers that
   * don't have a project context (CLI tools, tests).
   */
  public projectPathForLockfile: string | null = null;

  /**
   * Record the loaded .uproject and, when no port was explicitly pinned,
   * derive this project's stable per-worktree bridge port from its root path.
   * The C++ bridge derives the same value; the lockfile reconciles the actual
   * bound port at connect time either way.
   */
  setProjectContext(uprojectPath: string | null): void {
    this.projectPathForLockfile = uprojectPath;
    if (uprojectPath && this.portSource === "default") {
      this.port = deriveProjectPort(path.dirname(uprojectPath));
      this.portSource = "derived";
      debug("bridge", `derived per-project bridge port ${this.port} from ${path.dirname(uprojectPath)}`);
    }
  }

  async connect(timeoutMs = 3000): Promise<void> {
    if (this.isConnected) return;

    this.ws?.terminate();

    // #492: if a per-project lockfile exists for this .uproject, prefer the
    // port it advertises over the default. Lets multiple editors run side-
    // by-side without their npm clients colliding on 9877.
    const lockfile = readBridgeLockfile(this.projectPathForLockfile);
    if (lockfile && lockfile.port !== this.port) {
      debug("bridge", `lockfile points at port ${lockfile.port}, using it instead of default ${this.port}`);
      this.port = lockfile.port;
    }

    const url = `ws://${this.host}:${this.port}`;

    // #821: an editor whose bridge failed to bind is running and unreachable at
    // the same time. It leaves a record saying so, and quoting it here is the
    // difference between "start the editor" and "the editor is up, its bridge
    // is not".
    const explainFailure = (base: string): string => {
      const failed = readBridgeErrorRecord(this.projectPathForLockfile);
      return failed?.detail ? `${base}. ${failed.detail}` : base;
    };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        ws.terminate();
        reject(new McpError(ErrorCode.BRIDGE_TIMEOUT, explainFailure(`Connection to editor bridge timed out (${url})`)));
      }, timeoutMs);

      const ws = new WebSocket(url);

      ws.on("open", () => {
        clearTimeout(timer);
        this.ws = ws;
        this.setupListeners(ws);
        // #821: ask what we are talking to before anything else does. The
        // answer is cheap, it never touches the game thread, and without it a
        // client running against an older plugin can only report the symptom.
        this.handshake(ws).then(() => resolve(), () => resolve());
      });

      ws.on("error", (err) => {
        clearTimeout(timer);
        reject(
          new McpError(
            ErrorCode.NOT_CONNECTED,
            explainFailure(`Failed to connect to editor bridge at ${url}: ${err.message}`),
          ),
        );
      });
    });
  }

  startReconnecting(intervalMs = 15000): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setInterval(() => {
      if (this.isConnected) return;
      this.connect().then(
        () => { warn("bridge", "editor bridge reconnected"); },
        (e) => { debug("bridge", "reconnect attempt failed (will retry)", e); },
      );
    }, intervalMs);
  }

  stopReconnecting(): void {
    if (this.reconnectTimer) {
      clearInterval(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  async call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown> {
    if (!this.isConnected) {
      await this.ensureConnected();
    }

    const id = String(++this.idCounter);
    const request = { id, method, params: params ?? {} };
    const timeout = timeoutMs ?? 30_000;
    const ws = this.ws;
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      throw new McpError(
        ErrorCode.NOT_CONNECTED,
        "Not connected to editor bridge. Is Unreal Editor running with the MCP bridge plugin?",
      );
    }

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        if (this.ws === ws) {
          ws.terminate();
          this.ws = null;
        }
        reject(new McpError(ErrorCode.BRIDGE_TIMEOUT, `Bridge call '${method}' timed out after ${Math.round(timeout / 1000)}s`));
      }, timeout);

      this.pending.set(id, { resolve, reject, timer, method });
      ws.send(JSON.stringify(request), (err) => {
        if (!err) return;

        clearTimeout(timer);
        this.pending.delete(id);
        if (this.ws === ws) {
          ws.terminate();
          this.ws = null;
        }
        reject(new McpError(ErrorCode.CONNECTION_LOST, `Failed to send bridge call '${method}': ${err.message}`));
      });
    });
  }

  disconnect(): void {
    this.stopReconnecting();
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new McpError(ErrorCode.CONNECTION_LOST, "Bridge disconnected"));
    }
    this.pending.clear();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }

  /**
   * Ask the bridge what it is. Deliberately not routed through call(): a slow
   * or absent answer here must not terminate the socket the way a timed-out
   * call does, and a bridge old enough to have no answer is a normal outcome
   * rather than a failure.
   */
  private handshake(ws: WebSocket, timeoutMs = 5000): Promise<BridgeCapabilities> {
    return new Promise((resolve) => {
      const id = `cap-${++this.idCounter}`;
      let settled = false;

      const finish = (capabilities: BridgeCapabilities): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        ws.off("message", onMessage);
        this.capabilities = capabilities;

        const mismatch = describeProtocolMismatch(capabilities);
        if (mismatch) {
          warn("bridge", mismatch);
        } else {
          debug("bridge", `bridge protocol ${capabilities.protocolVersion}, ${capabilities.actionCount ?? "?"} actions, built ${capabilities.builtAt ?? "unknown"}`);
        }
        resolve(capabilities);
      };

      const onMessage = (data: WebSocket.RawData): void => {
        let msg: BridgeResponse;
        try {
          msg = JSON.parse(data.toString()) as BridgeResponse;
        } catch {
          return;
        }
        if (msg.id !== id) return;
        if (msg.error || !msg.result) {
          // -32601 from a bridge that has never heard of the handshake. That
          // silence is itself the answer: it predates the protocol version.
          finish(LEGACY_CAPABILITIES);
          return;
        }
        const result = msg.result as Partial<BridgeCapabilities>;
        finish({
          ...result,
          protocolVersion: typeof result.protocolVersion === "number" ? result.protocolVersion : 1,
          legacy: false,
        });
      };

      const timer = setTimeout(() => finish(LEGACY_CAPABILITIES), timeoutMs);

      ws.on("message", onMessage);
      ws.send(JSON.stringify({ id, method: "get_bridge_capabilities", params: {} }), (err) => {
        if (err) finish(LEGACY_CAPABILITIES);
      });
    });
  }

  private setupListeners(ws: WebSocket): void {
    ws.on("message", (data) => {
      try {
        const msg = JSON.parse(data.toString()) as BridgeResponse;
        const pending = this.pending.get(msg.id);
        if (!pending) return;

        this.pending.delete(msg.id);
        clearTimeout(pending.timer);

        if (msg.error) {
          // #821: -32601 against an older plugin used to read as a typo. Say
          // which side is behind, and name both versions.
          const mismatch =
            msg.error.code === -32601 ? describeProtocolMismatch(this.capabilities, pending.method) : null;
          pending.reject(
            new McpError(
              ErrorCode.BRIDGE_ERROR,
              mismatch ? `Bridge error: ${msg.error.message}. ${mismatch}` : `Bridge error: ${msg.error.message}`,
            ),
          );
        } else {
          pending.resolve(msg.result);
        }
      } catch (e) {
        warn("bridge", "dropped malformed message from editor", e);
      }
    });

    ws.on("close", (code: number, reasonRaw: Buffer) => {
      // The bridge closes with a status code and a reason when it refuses a
      // frame: 1009 for a message over its size bound, 1002 for a frame stream
      // that stopped making sense. Repeating both verbatim is the difference
      // between "something broke" and a caller knowing it sent too much.
      const reason = reasonRaw?.toString("utf8") ?? "";
      const detail = reason
        ? `Bridge connection closed by the editor (code ${code}): ${reason}`
        : `Bridge connection lost (close code ${code})`;
      for (const [, pending] of this.pending) {
        clearTimeout(pending.timer);
        pending.reject(new McpError(ErrorCode.CONNECTION_LOST, detail));
      }
      this.pending.clear();
      this.ws = null;
    });

    ws.on("error", (err) => {
      // `close` fires next and is where we reject pending calls; log here so
      // the underlying socket error (ECONNRESET, etc.) is not invisible.
      debug("bridge", "websocket error (close will follow)", err);
    });
  }
}
