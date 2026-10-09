/**
 * MCP's Transport over one WebSocket: one JSON-RPC message per text frame.
 * The daemon gives every shim connection its own MCP server on one of these.
 */
import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import type { JSONRPCMessage } from "@modelcontextprotocol/sdk/types.js";
import WebSocket from "ws";

export class WebSocketServerTransport implements Transport {
  onclose?: () => void;
  onerror?: (error: Error) => void;
  onmessage?: (message: JSONRPCMessage) => void;

  /** Requests received and not yet answered. */
  readonly open = new Set<string | number>();

  /** Sees every message in each direction, for the activity feed. Never alters one. */
  observe?: (direction: "in" | "out", message: JSONRPCMessage) => void;

  constructor(private readonly ws: WebSocket) {}

  async start(): Promise<void> {
    this.ws.on("message", (data) => {
      let message: JSONRPCMessage;
      try {
        message = JSON.parse(data.toString()) as JSONRPCMessage;
      } catch (e) {
        this.onerror?.(e instanceof Error ? e : new Error(String(e)));
        return;
      }
      if ("id" in message && "method" in message) this.open.add(message.id as string | number);
      this.observe?.("in", message);
      this.onmessage?.(message);
    });
    this.ws.on("close", () => this.onclose?.());
    this.ws.on("error", (e) => this.onerror?.(e));
  }

  async send(message: JSONRPCMessage): Promise<void> {
    if ("id" in message && ("result" in message || "error" in message)) this.open.delete(message.id as string | number);
    this.observe?.("out", message);
    if (this.ws.readyState !== WebSocket.OPEN) return;
    await new Promise<void>((resolve, reject) => this.ws.send(JSON.stringify(message), (e) => (e ? reject(e) : resolve())));
  }

  async close(): Promise<void> {
    this.ws.close();
  }
}
