import WebSocket from "ws";
import * as fs from "node:fs";
import * as path from "node:path";
import { createRequire } from "node:module";
import { McpError, ErrorCode } from "./errors.js";
import { debug, warn } from "./log.js";
import { DEFAULT_BRIDGE_PORT, deriveProjectPort } from "./port.js";
import { isPidAlive } from "./editor-target.js";

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

// #492: per-project port lockfile published by the bridge plugin. When the
// default port (9877) is taken by another editor, the plugin walks up and
// publishes the actual bound port here. The client reads this before
// falling back to the default port so a second editor finds the right one.
export function readBridgeLockfile(uprojectPath: string | null): BridgeLockfile | null {
  if (!uprojectPath) return null;
  const parsed = readJsonFile<BridgeLockfile>(
    path.join(path.dirname(uprojectPath), "Saved", "UE_MCP_Bridge", "port.json"),
  );
  if (!parsed || typeof parsed.port !== "number" || parsed.port <= 0 || parsed.port >= 65536) {
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
  if (typeof parsed.pid === "number" && parsed.pid > 0 && !isPidAlive(parsed.pid)) return null;
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

/**
 * A call the client stopped waiting for, and the reply that turned up after
 * (#799). The editor applies and saves a mutation before it answers, so a
 * timeout says nothing about whether the change landed. Keeping the record
 * lets the late reply be reconciled and logged instead of dropped on the floor
 * as an unrecognised message.
 */
export interface AbandonedCall {
  operationId: string;
  method: string;
  /** Epoch ms the client gave up waiting. */
  abandonedAt: number;
  /** Epoch ms the editor's reply arrived, if it ever did. */
  answeredAt?: number;
  result?: unknown;
  error?: string;
}

/** How many abandoned calls to remember. Oldest are dropped first. */
const ABANDONED_HISTORY = 32;

/**
 * How the port the bridge is about to use was chosen.
 *
 * The first three are attributable to the targeted project: the lockfile is
 * written by that project's own editor, the config port comes from that
 * project's ue-mcp.yml, and the derived port is a hash of that project's root
 * that the C++ side computes identically. The last two are pins that say
 * nothing about which project answers on that port.
 */
export type BridgePortSource = "lockfile" | "config" | "derived" | "explicit" | "env" | "default";

/** Which editor the bridge is pointed at, and how sure it is. */
export interface BridgeTarget {
  /** Absolute .uproject path whose editor this connection belongs to. */
  projectPath: string | null;
  /** Port the next connect will use, before any lockfile re-read. */
  port: number;
  portSource: BridgePortSource;
  /**
   * True when the port is attributable to `projectPath`. False means the port
   * is a pin inherited from the environment or an earlier target, so
   * connecting could land on some other project's editor. Connects are
   * refused in that state (see connect()).
   */
  verified: boolean;
}

/** Minimal interface for tool handlers - enables mocking in tests. */
export interface IBridge {
  readonly isConnected: boolean;
  /** #821: what the connected bridge reported at handshake, when there is one. */
  readonly capabilities?: BridgeCapabilities | null;
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown>;
  connect(timeoutMs?: number): Promise<void>;
  /**
   * Move the connection to another project's editor (#818). Drops the current
   * socket before returning, so the caller can re-point path resolution in the
   * same synchronous step and never expose a state where the two disagree.
   */
  retargetProject(uprojectPath: string, configPort?: number): BridgeTarget;
  /** Snapshot of the current target, for reporting and for tests. */
  getTarget(): BridgeTarget;
}

export class EditorBridge implements IBridge {
  private ws: WebSocket | null = null;
  private pending = new Map<string, PendingRequest>();
  private abandoned = new Map<string, AbandonedCall>();
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
  private portSource: BridgePortSource = "default";

  /**
   * Set once the bridge has been retargeted at a specific project (#818).
   * A pinned port (constructor arg or UE_MCP_PORT) was chosen for whatever
   * project the process started on, so after a switch it is not evidence about
   * the new target. While this is true and no lockfile confirms the port,
   * connect() refuses rather than risk answering as the wrong editor.
   */
  private unverifiedPin = false;

  /**
   * Bumped on every retarget and every socket teardown. A connect that was
   * already in flight when the target moved must not install its socket, or a
   * switch would silently reconnect to the project we just left.
   */
  private targetGeneration = 0;

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

  /**
   * Point the bridge at another project's editor (#818).
   *
   * The socket is dropped before anything else, so from the moment this
   * returns the only editor reachable is the one belonging to `uprojectPath`.
   * The port is re-decided from that project alone: its lockfile first (the
   * port its editor actually bound), then its own `bridge.port` config, then
   * the port derived from its root path. A port pinned by the constructor or
   * UE_MCP_PORT survives only as a last resort and is flagged unverified,
   * because it was chosen for a different project.
   *
   * Callers must re-point path resolution in the same synchronous step. That
   * is what keeps the pair honest: a handler can never observe the resolved
   * project and the connected editor referring to different projects.
   */
  retargetProject(uprojectPath: string, configPort?: number): BridgeTarget {
    const resolved = path.resolve(uprojectPath);
    const previous = this.projectPathForLockfile;
    this.closeSocket(`Bridge retargeted to ${resolved}`);
    this.projectPathForLockfile = resolved;
    this.unverifiedPin = false;

    const lockfile = readBridgeLockfile(resolved);
    if (lockfile) {
      this.port = lockfile.port;
      this.portSource = "lockfile";
    } else if (typeof configPort === "number" && configPort > 0) {
      this.port = configPort;
      this.portSource = "config";
    } else if (this.portSource === "explicit" || this.portSource === "env") {
      this.unverifiedPin = true;
    } else {
      this.port = deriveProjectPort(path.dirname(resolved));
      this.portSource = "derived";
    }

    debug(
      "bridge",
      `retargeted from ${previous ?? "(no project)"} to ${resolved}: port ${this.port} (${this.portSource})`,
    );
    return this.getTarget();
  }

  getTarget(): BridgeTarget {
    return {
      projectPath: this.projectPathForLockfile,
      port: this.port,
      portSource: this.portSource,
      verified: !this.unverifiedPin,
    };
  }

  async connect(timeoutMs = 3000): Promise<void> {
    if (this.isConnected) return;

    this.closeSocket("Bridge reconnecting");

    // #492: if a per-project lockfile exists for this .uproject, prefer the
    // port it advertises over the default. Lets multiple editors run side-
    // by-side without their npm clients colliding on 9877.
    const lockfile = readBridgeLockfile(this.projectPathForLockfile);
    if (lockfile) {
      if (lockfile.port !== this.port) {
        debug("bridge", `lockfile points at port ${lockfile.port}, using it instead of default ${this.port}`);
        this.port = lockfile.port;
      }
      // The target project's own editor published this port, which is the
      // proof a pin could not give.
      this.portSource = "lockfile";
      this.unverifiedPin = false;
    }

    // #818: refuse rather than guess. The alternative is connecting to a port
    // chosen for a different project and executing every subsequent mutation
    // in an editor the caller never asked for.
    if (this.unverifiedPin) {
      throw new McpError(
        ErrorCode.NOT_CONNECTED,
        `Refusing to connect: the bridge is targeted at ${this.projectPathForLockfile}, ` +
          `but port ${this.port} is pinned (${this.portSource === "env" ? "UE_MCP_PORT" : "explicit port argument"}) ` +
          `and was not chosen for this project, so it may belong to another editor. ` +
          `Start this project's editor (it publishes Saved/UE_MCP_Bridge/port.json), ` +
          `or clear the pin so the per-project port is derived from the project path.`,
      );
    }

    const url = `ws://${this.host}:${this.port}`;
    const generation = this.targetGeneration;

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
        // The target moved while this handshake was in flight. Installing the
        // socket now would put the bridge back on the project we just left.
        if (this.targetGeneration !== generation) {
          ws.terminate();
          reject(
            new McpError(
              ErrorCode.NOT_CONNECTED,
              `Abandoned the connection to ${url}: the bridge was retargeted while connecting.`,
            ),
          );
          return;
        }
        this.ws = ws;
        this.setupListeners(ws);
        // #821: ask what we are talking to before anything else does. The
        // answer is cheap, it never touches the game thread, and without it a
        // client running against an older plugin can only report the symptom.
        // Bounded by the caller's own connect budget: a bridge that will not
        // answer this is not one worth waiting past that for.
        this.handshake(ws, timeoutMs).then(() => resolve(), () => resolve());
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
        // #799: the request was sent, so the editor may have run it to
        // completion (mutating handlers apply and save the asset before they
        // answer). The connection is not at fault and is left open, both so
        // concurrent calls survive and so the late reply can be reconciled.
        this.rememberAbandoned(id, method);
        reject(new McpError(
          ErrorCode.BRIDGE_TIMEOUT,
          `Bridge call '${method}' timed out after ${Math.round(timeout / 1000)}s. `
          + `Outcome is unknown: the editor may have already applied and saved this call `
          + `(operation ${id}). Read the current state back before retrying, and prefer `
          + `an idempotent retry (pass the same names) so a call that did land is not repeated.`,
          { outcome: "unknown", operationId: id, method },
        ));
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
    this.closeSocket("Bridge disconnected", { graceful: true });
  }

  /**
   * Drop the socket and fail everything riding on it. Retargeting terminates
   * instead of closing: a close handshake leaves the socket usable for another
   * round trip, and the caller is switching projects precisely because nothing
   * more should reach this editor.
   */
  private closeSocket(reason: string, opts?: { graceful?: boolean }): void {
    this.targetGeneration += 1;
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new McpError(ErrorCode.CONNECTION_LOST, reason));
    }
    this.pending.clear();
    const ws = this.ws;
    this.ws = null;
    if (!ws) return;
    if (opts?.graceful) ws.close();
    else ws.terminate();
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

      const detach = (): void => {
        clearTimeout(timer);
        ws.off("message", onMessage);
        ws.off("close", onClose);
      };

      // The socket went away before the bridge answered. Settle now rather
      // than sitting out the rest of the connect budget on a dead socket, and
      // record nothing: a dropped connection says nothing about which protocol
      // version the plugin speaks, so claiming it is a legacy one here would
      // put a "rebuild your plugin" warning in front of a network fault.
      const onClose = (): void => {
        if (settled) return;
        settled = true;
        detach();
        this.capabilities = null;
        resolve({ ...LEGACY_CAPABILITIES });
      };

      const finish = (capabilities: BridgeCapabilities): void => {
        if (settled) return;
        settled = true;
        detach();
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
          finish({ ...LEGACY_CAPABILITIES });
          return;
        }
        const result = msg.result as Partial<BridgeCapabilities>;
        finish({
          ...result,
          protocolVersion: typeof result.protocolVersion === "number" ? result.protocolVersion : 1,
          legacy: false,
        });
      };

      const timer = setTimeout(() => finish({ ...LEGACY_CAPABILITIES }), timeoutMs);

      ws.on("message", onMessage);
      ws.on("close", onClose);
      ws.send(JSON.stringify({ id, method: "get_bridge_capabilities", params: {} }), (err) => {
        if (err) onClose();
      });
    });
  }

  /** Record a call the client gave up on, capping the history. */
  private rememberAbandoned(id: string, method: string): void {
    this.abandoned.set(id, { operationId: id, method, abandonedAt: Date.now() });
    while (this.abandoned.size > ABANDONED_HISTORY) {
      const oldest = this.abandoned.keys().next();
      if (oldest.done) break;
      this.abandoned.delete(oldest.value);
    }
  }

  /**
   * Calls this client stopped waiting for, newest last. An entry with
   * `answeredAt` set is one the editor finished after the timeout, which is
   * proof the mutation ran (#799).
   */
  get abandonedCalls(): AbandonedCall[] {
    return [...this.abandoned.values()];
  }

  private setupListeners(ws: WebSocket): void {
    ws.on("message", (data) => {
      try {
        const msg = JSON.parse(data.toString()) as BridgeResponse;
        const pending = this.pending.get(msg.id);
        if (!pending) {
          // A reply to a call that already timed out on this side. Nobody is
          // waiting for it, but it settles whether that call ran (#799).
          const abandonedCall = this.abandoned.get(msg.id);
          if (abandonedCall) {
            abandonedCall.answeredAt = Date.now();
            if (msg.error) abandonedCall.error = msg.error.message;
            else abandonedCall.result = msg.result;
            warn(
              "bridge",
              `editor finished '${abandonedCall.method}' (operation ${msg.id}) `
              + `${Date.now() - abandonedCall.abandonedAt}ms after the client timed out; `
              + `the call ${msg.error ? "failed" : "completed"}`,
            );
          }
          return;
        }

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
      // A socket we already replaced (retarget, reconnect) closes after its
      // successor is live. Without this guard its late close event would null
      // out the new connection and fail the calls riding on it.
      if (this.ws !== ws) return;

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
