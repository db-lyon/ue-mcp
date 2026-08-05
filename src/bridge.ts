import WebSocket from "ws";
import * as fs from "node:fs";
import * as path from "node:path";
import { McpError, ErrorCode } from "./errors.js";
import { debug, warn } from "./log.js";
import { DEFAULT_BRIDGE_PORT, deriveProjectPort } from "./port.js";

// #492: per-project port lockfile published by the bridge plugin. When the
// default port (9877) is taken by another editor, the plugin walks up and
// publishes the actual bound port here. The client reads this before
// falling back to the default port so a second editor finds the right one.
export function readBridgeLockfile(
  uprojectPath: string | null,
): { port: number; pid: number; startedAt?: string; apiVersion?: number } | null {
  if (!uprojectPath) return null;
  const lockfile = path.join(
    path.dirname(uprojectPath),
    "Saved",
    "UE_MCP_Bridge",
    "port.json",
  );
  try {
    const raw = fs.readFileSync(lockfile, "utf8");
    const parsed = JSON.parse(raw);
    if (typeof parsed?.port === "number" && parsed.port > 0 && parsed.port < 65536) {
      return parsed;
    }
  } catch {
    // Missing or unreadable - fall back to default.
  }
  return null;
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
}

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

/** Minimal interface for tool handlers — enables mocking in tests. */
export interface IBridge {
  readonly isConnected: boolean;
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
  private reconnectTimer: ReturnType<typeof setInterval> | null = null;
  private connectInFlight: Promise<void> | null = null;
  private idCounter = 0;

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

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        ws.terminate();
        reject(new McpError(ErrorCode.BRIDGE_TIMEOUT, `Connection to editor bridge timed out (${url})`));
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
        resolve();
      });

      ws.on("error", (err) => {
        clearTimeout(timer);
        reject(
          new McpError(
            ErrorCode.NOT_CONNECTED,
            `Failed to connect to editor bridge at ${url}: ${err.message}`,
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

      this.pending.set(id, { resolve, reject, timer });
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

  private setupListeners(ws: WebSocket): void {
    ws.on("message", (data) => {
      try {
        const msg = JSON.parse(data.toString()) as BridgeResponse;
        const pending = this.pending.get(msg.id);
        if (!pending) return;

        this.pending.delete(msg.id);
        clearTimeout(pending.timer);

        if (msg.error) {
          pending.reject(new McpError(ErrorCode.BRIDGE_ERROR, `Bridge error: ${msg.error.message}`));
        } else {
          pending.resolve(msg.result);
        }
      } catch (e) {
        warn("bridge", "dropped malformed message from editor", e);
      }
    });

    ws.on("close", () => {
      // A socket we already replaced (retarget, reconnect) closes after its
      // successor is live. Without this guard its late close event would null
      // out the new connection and fail the calls riding on it.
      if (this.ws !== ws) return;
      for (const [, pending] of this.pending) {
        clearTimeout(pending.timer);
        pending.reject(new McpError(ErrorCode.CONNECTION_LOST, "Bridge connection lost"));
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
