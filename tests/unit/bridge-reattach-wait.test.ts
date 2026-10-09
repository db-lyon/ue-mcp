/**
 * A call made while the editor is away waits for it when reattachWaitMs is set
 * (the daemon sets it), and otherwise fails at once naming the last cause.
 */
import { afterEach, describe, expect, it } from "vitest";
import * as net from "node:net";
import { WebSocketServer, type WebSocket } from "ws";
import { EditorBridge } from "../../src/bridge/bridge.js";

let wss: WebSocketServer | null = null;

afterEach(async () => {
  for (const c of wss?.clients ?? []) c.terminate();
  await new Promise<void>((r) => (wss ? wss.close(() => r()) : r()));
  wss = null;
});

async function freePort(): Promise<number> {
  return new Promise((resolve) => {
    const s = net.createServer().listen(0, "127.0.0.1", () => {
      const port = (s.address() as net.AddressInfo).port;
      s.close(() => resolve(port));
    });
  });
}

function serve(port: number): void {
  wss = new WebSocketServer({ host: "127.0.0.1", port });
  wss.on("connection", (ws: WebSocket) => {
    ws.on("message", (raw) => {
      const req = JSON.parse(String(raw)) as { id?: unknown; method?: string };
      const result = req.method === "get_bridge_capabilities" ? { protocolVersion: 2 } : { success: true, method: req.method };
      ws.send(JSON.stringify({ jsonrpc: "2.0", id: req.id ?? null, result }));
    });
  });
}

describe("waiting for the editor", () => {
  it("fails at once by default, naming why the editor last went away", async () => {
    const bridge = new EditorBridge("127.0.0.1", await freePort());
    bridge.lastDisconnectCause = "crashed";
    await expect(bridge.call("get_world_outliner")).rejects.toThrow(/The editor last went away: crashed/);
  });

  it("waits for an editor that comes back within reattachWaitMs", async () => {
    const port = await freePort();
    const bridge = new EditorBridge("127.0.0.1", port);
    bridge.reattachWaitMs = 5000;
    setTimeout(() => serve(port), 300);
    await expect(bridge.call("get_world_outliner")).resolves.toMatchObject({ success: true });
    bridge.disconnect();
  }, 15_000);

  it("gives up after reattachWaitMs and says how long it waited", async () => {
    const bridge = new EditorBridge("127.0.0.1", await freePort());
    bridge.reattachWaitMs = 1200;
    bridge.lastDisconnectCause = "closed";
    await expect(bridge.call("get_world_outliner")).rejects.toThrow(/last went away: closed\. Waited 1s/);
    bridge.disconnect();
  }, 15_000);
});
