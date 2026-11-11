import { once } from "node:events";
import type { AddressInfo } from "node:net";
import { WebSocketServer, type WebSocket } from "ws";
import { describe, expect, it } from "vitest";
import { EditorBridge } from "../../src/bridge/bridge.js";
import { ErrorCode } from "../../src/core/errors.js";
import type { ProgressUpdate } from "../../src/core/types.js";

type Frame = Record<string, unknown>;

/** A loopback bridge that answers the handshake and hands every other frame to the test. */
async function startBridge(onRequest: (frame: Frame, socket: WebSocket) => void) {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  server.on("connection", (socket) => {
    socket.on("message", (data) => {
      const frame = JSON.parse(data.toString()) as Frame;
      if (frame.method === "get_bridge_capabilities") {
        socket.send(JSON.stringify({ id: frame.id, result: { protocolVersion: 2, actions: ["slow"] } }));
        return;
      }
      onRequest(frame, socket);
    });
  });
  await once(server, "listening");
  return {
    port: (server.address() as AddressInfo).port,
    close: () => new Promise<void>((resolve) => {
      for (const c of server.clients) c.terminate();
      server.close(() => resolve());
    }),
  };
}

describe("EditorBridge deferred answers", () => {
  it("routes the plugin's progress frames to the caller and asks for them in the request", async () => {
    let sent: Frame | null = null;
    const server = await startBridge((frame, socket) => {
      sent = frame;
      for (const n of [1, 2]) {
        socket.send(JSON.stringify({ jsonrpc: "2.0", method: "notifications/progress", params: { id: frame.id, progress: n, message: `tick ${n}` } }));
      }
      socket.send(JSON.stringify({ id: frame.id, result: { success: true } }));
    });
    const bridge = new EditorBridge("127.0.0.1", server.port);
    const seen: ProgressUpdate[] = [];
    try {
      await expect(bridge.call("slow", {}, 5000, { onProgress: (u) => seen.push(u) })).resolves.toEqual({ success: true });
      expect(sent!.progress).toBe(true);
      expect(sent!.cancelToken).toBeUndefined();
      expect(seen).toEqual([{ progress: 1, message: "tick 1" }, { progress: 2, message: "tick 2" }]);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("sends cancel_request with the request's token on a socket of its own and stops waiting", async () => {
    let request: Frame | null = null;
    let requestSocket: WebSocket | null = null;
    const cancels: Array<{ frame: Frame; socket: WebSocket }> = [];
    let cancelled!: () => void;
    const cancelArrived = new Promise<void>((resolve) => { cancelled = resolve; });
    const server = await startBridge((frame, socket) => {
      if (frame.method === "cancel_request") {
        cancels.push({ frame, socket });
        socket.send(JSON.stringify({ id: frame.id, result: { success: true, state: "deferred" } }));
        cancelled();
        return;
      }
      request = frame;
      requestSocket = socket;
    });
    const bridge = new EditorBridge("127.0.0.1", server.port);
    const abort = new AbortController();
    try {
      const pending = bridge.call("slow", {}, 5000, { signal: abort.signal });
      await expect.poll(() => request).not.toBeNull();
      abort.abort();
      await expect(pending).rejects.toMatchObject({ code: ErrorCode.CANCELLED });
      await cancelArrived;
      expect(typeof request!.cancelToken).toBe("string");
      expect(cancels[0].frame.params).toEqual({ cancelToken: request!.cancelToken });
      expect(cancels[0].socket).not.toBe(requestSocket);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("refuses a call whose signal is already aborted without sending it", async () => {
    let calls = 0;
    const server = await startBridge(() => { calls += 1; });
    const bridge = new EditorBridge("127.0.0.1", server.port);
    const abort = new AbortController();
    abort.abort();
    try {
      await expect(bridge.call("slow", {}, 1000, { signal: abort.signal })).rejects.toMatchObject({ code: ErrorCode.CANCELLED });
      expect(calls).toBe(0);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });
});
