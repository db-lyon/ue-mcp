import { once } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import { describe, expect, it } from "vitest";

/** A throwaway project directory with a Saved/UE_MCP_Bridge folder. */
function makeProjectDir(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-bridge-"));
  fs.mkdirSync(path.join(dir, "Saved", "UE_MCP_Bridge"), { recursive: true });
  return dir;
}

function writeBridgeRecord(dir: string, name: string, body: unknown): void {
  fs.writeFileSync(path.join(dir, "Saved", "UE_MCP_Bridge", name), JSON.stringify(body));
}

/** What a current bridge answers the capability handshake with. */
const DEFAULT_CAPABILITIES = {
  protocolVersion: 2,
  handlerApiVersion: 1,
  builtAt: "Aug  5 2026 09 14 22",
  actionCount: 3,
  actions: ["first", "second", "ping"],
};

async function withBridgeServer(
  onRequest: (request: Record<string, unknown>, socket: import("ws").WebSocket) => void,
  /** null makes the server answer the handshake the way a pre-handshake plugin does. */
  capabilities: Record<string, unknown> | null = DEFAULT_CAPABILITIES,
): Promise<{
  close: () => Promise<void>;
  connectionCount: () => number;
  port: number;
}> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  let connections = 0;

  server.on("connection", (socket) => {
    connections += 1;
    socket.on("message", (data) => {
      const request = JSON.parse(data.toString()) as Record<string, unknown>;
      if (request.method === "get_bridge_capabilities") {
        socket.send(
          JSON.stringify(
            capabilities
              ? { id: request.id, result: capabilities }
              : { id: request.id, error: { code: -32601, message: "Unknown method: get_bridge_capabilities" } },
          ),
        );
        return;
      }
      onRequest(request, socket);
    });
  });

  await once(server, "listening");
  const address = server.address() as AddressInfo;
  return {
    port: address.port,
    connectionCount: () => connections,
    close: async () => {
      for (const client of server.clients) {
        client.terminate();
      }
      await new Promise<void>((resolve, reject) => {
        server.close((err) => (err ? reject(err) : resolve()));
      });
    },
  };
}

describe("EditorBridge connection handling", () => {
  it("connects on the first bridge call when the editor bridge is reachable", async () => {
    const server = await withBridgeServer((request, socket) => {
      socket.send(JSON.stringify({ id: request.id, result: { method: request.method, params: request.params } }));
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      const result = await bridge.call("ping", { ok: true }, 1000);

      expect(result).toEqual({ method: "ping", params: { ok: true } });
      expect(bridge.isConnected).toBe(true);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("shares one in-flight connection for concurrent calls", async () => {
    const server = await withBridgeServer((request, socket) => {
      socket.send(JSON.stringify({ id: request.id, result: request.method }));
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await expect(Promise.all([
        bridge.call("first", {}, 1000),
        bridge.call("second", {}, 1000),
      ])).resolves.toEqual(["first", "second"]);
      expect(server.connectionCount()).toBe(1);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("repeats the close code and reason when the bridge refuses a message", async () => {
    const server = await withBridgeServer((_request, socket) => {
      // What the bridge does when a message exceeds its size bound.
      socket.close(1009, "message of 70000000 bytes exceeds the 67108864 byte bridge limit");
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await expect(bridge.call("oversized", {}, 2000)).rejects.toThrow(
        /code 1009.*exceeds the 67108864 byte bridge limit/,
      );
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("terminates a timed-out socket so the next call can reconnect", async () => {
    const server = await withBridgeServer((request, socket) => {
      if (request.method === "hang") return;
      socket.send(JSON.stringify({ id: request.id, result: "reconnected" }));
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await expect(bridge.call("hang", {}, 50)).rejects.toThrow("timed out");
      expect(bridge.isConnected).toBe(false);

      await expect(bridge.call("ping", {}, 1000)).resolves.toBe("reconnected");
      expect(server.connectionCount()).toBe(2);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });
});

describe("bridge state records", () => {
  it("ignores a port lockfile whose editor process is gone", async () => {
    const dir = makeProjectDir();
    const { readBridgeLockfileForDir } = await import("../../src/bridge.js");

    writeBridgeRecord(dir, "port.json", { port: 51234, pid: 0x7ffffffe, instanceId: "dead-instance" });
    expect(readBridgeLockfileForDir(dir)).toBeNull();

    writeBridgeRecord(dir, "port.json", { port: 51234, pid: process.pid, instanceId: "live-instance" });
    expect(readBridgeLockfileForDir(dir)?.port).toBe(51234);
    expect(readBridgeLockfileForDir(dir)?.instanceId).toBe("live-instance");
  });

  it("reports a bridge that failed to bind while its editor is still running", async () => {
    const dir = makeProjectDir();
    const uproject = path.join(dir, "Sample.uproject");
    const { readBridgeErrorRecord } = await import("../../src/bridge.js");

    writeBridgeRecord(dir, "bridge-error.json", {
      status: "bind-failed",
      pid: process.pid,
      firstPortTried: 49200,
      lastPortTried: 49250,
      detail: "The editor is running but its MCP bridge could not bind a port in [49200, 49250].",
    });
    expect(readBridgeErrorRecord(uproject)?.detail).toContain("could not bind a port");

    writeBridgeRecord(dir, "bridge-error.json", { status: "bind-failed", pid: 0x7ffffffe });
    expect(readBridgeErrorRecord(uproject)).toBeNull();
  });
});

describe("bridge capability handshake", () => {
  it("records what the bridge says it is on connect", async () => {
    const server = await withBridgeServer((request, socket) => {
      socket.send(JSON.stringify({ id: request.id, result: "ok" }));
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await bridge.call("ping", {}, 1000);
      expect(bridge.capabilities?.protocolVersion).toBe(2);
      expect(bridge.capabilities?.legacy).toBe(false);
      expect(bridge.capabilities?.builtAt).toBe("Aug  5 2026 09 14 22");
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("names both versions when an older plugin does not know a method", async () => {
    const server = await withBridgeServer(
      (request, socket) => {
        socket.send(JSON.stringify({ id: request.id, error: { code: -32601, message: "Unknown method: set_water_body_property" } }));
      },
      null, // a plugin built before the handshake existed
    );

    const { EditorBridge, CLIENT_PROTOCOL_VERSION } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      const failure = await bridge.call("set_water_body_property", {}, 1000).catch((e: Error) => e.message);
      expect(bridge.capabilities?.legacy).toBe(true);
      expect(failure).toContain("Unknown method: set_water_body_property");
      expect(failure).toContain("protocol version 1");
      expect(failure).toContain(`version ${CLIENT_PROTOCOL_VERSION}`);
      expect(failure).toContain("npx ue-mcp update");
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("tells the user to update the package when the plugin is newer", async () => {
    const { describeProtocolMismatch, CLIENT_PROTOCOL_VERSION } = await import("../../src/bridge.js");
    const message = describeProtocolMismatch({
      protocolVersion: CLIENT_PROTOCOL_VERSION + 1,
      legacy: false,
      builtAt: "Sep 1 2026 12 00 00",
    });
    expect(message).toContain(`protocol version ${CLIENT_PROTOCOL_VERSION + 1}`);
    expect(message).toContain("ue-mcp@latest");
  });

  it("says nothing when the versions agree", async () => {
    const { describeProtocolMismatch, CLIENT_PROTOCOL_VERSION } = await import("../../src/bridge.js");
    expect(describeProtocolMismatch({ protocolVersion: CLIENT_PROTOCOL_VERSION, legacy: false })).toBeNull();
  });
});
