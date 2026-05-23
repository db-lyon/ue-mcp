import WebSocket from "ws";
import { McpError, ErrorCode } from "./errors.js";
import { debug, warn } from "./log.js";

const DEFAULT_BRIDGE_PORT = Number(process.env.UE_MCP_BRIDGE_PORT ?? 9877);

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

/** Minimal interface for tool handlers — enables mocking in tests. */
export interface IBridge {
  readonly isConnected: boolean;
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown>;
  connect(timeoutMs?: number): Promise<void>;
}

export class EditorBridge implements IBridge {
  private ws: WebSocket | null = null;
  private pending = new Map<string, PendingRequest>();
  private reconnectTimer: ReturnType<typeof setInterval> | null = null;
  private connectInFlight: Promise<void> | null = null;
  private idCounter = 0;

  constructor(
    public host = "localhost",
    public port = DEFAULT_BRIDGE_PORT,
  ) {}

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

  async connect(timeoutMs = 3000): Promise<void> {
    if (this.isConnected) return;

    this.ws?.terminate();

    const url = `ws://${this.host}:${this.port}`;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        ws.terminate();
        reject(new McpError(ErrorCode.BRIDGE_TIMEOUT, `Connection to editor bridge timed out (${url})`));
      }, timeoutMs);

      const ws = new WebSocket(url);

      ws.on("open", () => {
        clearTimeout(timer);
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
