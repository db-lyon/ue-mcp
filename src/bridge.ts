import WebSocket from "ws";

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

export class EditorBridge {
  private ws: WebSocket | null = null;
  private pending = new Map<string, PendingRequest>();
  private reconnectTimer: ReturnType<typeof setInterval> | null = null;
  private idCounter = 0;

  constructor(
    public host = "localhost",
    public port = 9877,
  ) {}

  get isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  async connect(timeoutMs = 3000): Promise<void> {
    if (this.isConnected) return;

    this.ws?.terminate();

    const url = `ws://${this.host}:${this.port}`;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        ws.terminate();
        reject(new Error(`Connection to editor bridge timed out (${url})`));
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
          new Error(
            `Failed to connect to editor bridge at ${url}: ${err.message}`,
          ),
        );
      });
    });
  }

  startReconnecting(intervalMs = 15000): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setInterval(async () => {
      if (this.isConnected) return;
      try {
        await this.connect();
        console.error("[ue-mcp] Editor bridge reconnected");
      } catch {
        // silent retry
      }
    }, intervalMs);
  }

  stopReconnecting(): void {
    if (this.reconnectTimer) {
      clearInterval(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  async call(method: string, params?: Record<string, unknown>): Promise<unknown> {
    if (!this.isConnected) {
      throw new Error(
        "Not connected to editor bridge. Is Unreal Editor running with the MCP bridge plugin?",
      );
    }

    const id = String(++this.idCounter);
    const request = { id, method, params: params ?? {} };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Bridge call '${method}' timed out after 30s`));
      }, 30_000);

      this.pending.set(id, { resolve, reject, timer });
      this.ws!.send(JSON.stringify(request));
    });
  }

  async callOrThrow(method: string, params?: Record<string, unknown>): Promise<unknown> {
    const result = await this.call(method, params);
    return result;
  }

  disconnect(): void {
    this.stopReconnecting();
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error("Bridge disconnected"));
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
          pending.reject(new Error(`Bridge error: ${msg.error.message}`));
        } else {
          pending.resolve(msg.result);
        }
      } catch {
        // malformed message, ignore
      }
    });

    ws.on("close", () => {
      for (const [, pending] of this.pending) {
        clearTimeout(pending.timer);
        pending.reject(new Error("Bridge connection lost"));
      }
      this.pending.clear();
      this.ws = null;
    });

    ws.on("error", () => {
      // handled by close
    });
  }
}
