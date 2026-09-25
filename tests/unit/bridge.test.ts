import { once } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import { describe, expect, it, vi } from "vitest";

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

type RequestHandler = (request: Record<string, unknown>, socket: import("ws").WebSocket) => void;

/** Hold a real loopback handshake until the test chooses its outcome. */
function holdHandshake() {
  let receive!: (value: { request: Record<string, unknown>; socket: import("ws").WebSocket }) => void;
  const received = new Promise<Parameters<typeof receive>[0]>((resolve) => { receive = resolve; });
  const handle: RequestHandler = (request, socket) => receive({ request, socket });
  return { received, handle };
}

async function withBridgeServer(
  onRequest: RequestHandler,
  /** null makes the server answer the handshake the way a pre-handshake plugin does. */
  capabilities: Record<string, unknown> | RequestHandler | null = DEFAULT_CAPABILITIES,
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
        if (typeof capabilities === "function") {
          capabilities(request, socket);
          return;
        }
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

  it("keeps the connection after a call times out", async () => {
    const server = await withBridgeServer((request, socket) => {
      if (request.method === "hang") return;
      socket.send(JSON.stringify({ id: request.id, result: "still here" }));
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await expect(bridge.call("hang", {}, 50)).rejects.toThrow("timed out");
      // A slow editor is not a broken connection: tearing the socket down would
      // take every concurrent call with it and lose the late reply (#799).
      expect(bridge.isConnected).toBe(true);

      await expect(bridge.call("ping", {}, 1000)).resolves.toBe("still here");
      expect(server.connectionCount()).toBe(1);
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("reports a timed-out call as an unknown outcome, not a failure", async () => {
    const server = await withBridgeServer(() => {});

    const { EditorBridge } = await import("../../src/bridge.js");
    const { McpError, ErrorCode } = await import("../../src/errors.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      const error = await bridge.call("add_widget", {}, 50).catch((e: unknown) => e);

      expect(error).toBeInstanceOf(McpError);
      const mcpError = error as InstanceType<typeof McpError>;
      expect(mcpError.code).toBe(ErrorCode.BRIDGE_TIMEOUT);
      expect(mcpError.details?.outcome).toBe("unknown");
      expect(mcpError.details?.method).toBe("add_widget");
      expect(mcpError.details?.operationId).toBeTruthy();
      expect(mcpError.message).toContain("may have already applied");
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

  it("reconciles a reply that arrives after the client gave up", async () => {
    let held: { id: unknown; socket: import("ws").WebSocket } | null = null;
    const server = await withBridgeServer((request, socket) => {
      held = { id: request.id, socket };
    });

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await expect(bridge.call("add_widget", {}, 50)).rejects.toThrow("timed out");

      const pendingCall = held as unknown as { id: unknown; socket: import("ws").WebSocket };
      pendingCall.socket.send(JSON.stringify({ id: pendingCall.id, result: { created: true } }));
      await new Promise((resolve) => setTimeout(resolve, 50));

      const [abandoned] = bridge.abandonedCalls;
      expect(abandoned.method).toBe("add_widget");
      expect(abandoned.answeredAt).toBeTypeOf("number");
      expect(abandoned.result).toEqual({ created: true });
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });
});

describe("bridge state records", () => {
  it("carries the identity and version fields the bridge now publishes", async () => {
    const dir = makeProjectDir();
    const uproject = path.join(dir, "Sample.uproject");
    const { readBridgeLockfile } = await import("../../src/bridge.js");

    writeBridgeRecord(dir, "port.json", {
      port: 51234,
      pid: process.pid,
      instanceId: "9f1c0e2a-0000-4000-8000-000000000001",
      status: "listening",
      handlerApiVersion: 1,
    });
    const record = readBridgeLockfile(uproject);
    expect(record?.port).toBe(51234);
    expect(record?.instanceId).toBe("9f1c0e2a-0000-4000-8000-000000000001");
    expect(record?.status).toBe("listening");

    writeBridgeRecord(dir, "port.json", { port: 0, pid: process.pid });
    expect(readBridgeLockfile(uproject)).toBeNull();
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
  it.each(["disconnect", "remote close"])("clears capabilities on %s", async (event) => {
    const server = await withBridgeServer(() => {});
    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await bridge.connect(1000);
      expect(bridge.capabilities?.builtAt).toBe(DEFAULT_CAPABILITIES.builtAt);
      if (event === "disconnect") {
        bridge.disconnect();
      } else {
        const closed = once(bridge["ws"]!, "close");
        await server.close();
        await closed;
      }
      expect(bridge.isConnected).toBe(false);
      expect(bridge.capabilities).toBeNull();
    } finally {
      bridge.disconnect();
      if (event === "disconnect") await server.close();
    }
  });

  it("keeps A's metadata absent until B answers its handshake", async () => {
    const held = holdHandshake();
    const serverA = await withBridgeServer(() => {});
    const serverB = await withBridgeServer(() => {}, held.handle);
    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", serverA.port);
    let connecting: Promise<void> | undefined;

    try {
      await bridge.connect(1000);
      expect(bridge.capabilities?.builtAt).toBe(DEFAULT_CAPABILITIES.builtAt);
      bridge.disconnect();
      bridge.port = serverB.port;
      connecting = bridge.connect(1000);
      const { request, socket } = await held.received;
      expect(bridge.capabilities).toBeNull();
      socket.send(JSON.stringify({ id: request.id, result: { ...DEFAULT_CAPABILITIES, projectName: "B" } }));
      await connecting;
      expect(bridge.capabilities?.projectName).toBe("B");
    } finally {
      bridge.disconnect();
      await connecting;
      await serverA.close();
      await serverB.close();
    }
  });

  it.each(["close", "response", "timeout"])("ignores A's delayed %s after B completes its handshake", async (event) => {
    const held = holdHandshake();
    const serverA = await withBridgeServer(() => {}, held.handle);
    const serverB = await withBridgeServer(() => {}, { ...DEFAULT_CAPABILITIES, projectName: "B" });
    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", serverA.port);
    const warnings = vi.spyOn(await import("../../src/log.js"), "warn").mockImplementation(() => {});
    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
    const connectingA = bridge.connect(5000);
    let socketA: import("ws").WebSocket | undefined;

    try {
      const { request, socket } = await held.received;
      socketA = bridge["ws"]!;
      // Keep A's transport alive to deliver each obsolete completion after B.
      // This controls event ordering without depending on network timing.
      const terminate = vi.spyOn(socketA, "terminate").mockImplementation(() => {});
      bridge.retargetProject(path.join(makeProjectDir(), "B.uproject"), serverB.port);
      terminate.mockRestore();
      await bridge.connect(1000);
      const capabilitiesB = bridge.capabilities;
      expect(capabilitiesB?.projectName).toBe("B");

      if (event === "close") socketA.emit("close", 1000, Buffer.alloc(0));
      else if (event === "response") {
        socket.send(JSON.stringify({ id: request.id, result: { protocolVersion: 1, projectName: "A" } }));
      } else {
        await vi.advanceTimersByTimeAsync(5000);
      }
      await connectingA;

      expect(bridge.capabilities).toBe(capabilitiesB);
      expect(bridge.isConnected).toBe(true);
      expect(warnings).not.toHaveBeenCalled();
      expect(socketA.listenerCount("message")).toBe(1);
      expect(socketA.listenerCount("close")).toBe(1);
      expect(vi.getTimerCount()).toBe(0);
      // Repeated events from A must stay inert after its handshake settles.
      socketA.emit("message", Buffer.from(JSON.stringify({ id: request.id, result: { protocolVersion: 1 } })));
      socketA.emit("close", 1000, Buffer.alloc(0));
      await vi.advanceTimersByTimeAsync(5000);
      expect(bridge.capabilities).toBe(capabilitiesB);
      expect(warnings).not.toHaveBeenCalled();
    } finally {
      socketA?.terminate();
      bridge.disconnect();
      await connectingA;
      vi.useRealTimers();
      warnings.mockRestore();
      await serverA.close();
      await serverB.close();
    }
  });

  it.each(["legacy timeout", "remote close", "closing timeout"])("settles the current handshake on %s and cleans up", async (event) => {
    const held = holdHandshake();
    const server = await withBridgeServer(() => {}, held.handle);
    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);
    const warnings = vi.spyOn(await import("../../src/log.js"), "warn").mockImplementation(() => {});
    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
    const connecting = bridge.connect(1000);

    try {
      const { socket: remote } = await held.received;
      const socket = bridge["ws"]!;
      expect(bridge.capabilities).toBeNull();
      if (event === "legacy timeout") {
        await vi.advanceTimersByTimeAsync(1000);
      } else {
        const closed = once(socket, "close");
        const remoteClosed = once(remote, "close");
        if (event === "remote close") remote.terminate();
        else {
          socket.close();
          // Fire the handshake deadline while the transport is CLOSING,
          // before its close event can remove the current socket.
          vi.advanceTimersByTime(1000);
          await connecting;
          expect(bridge.capabilities).toBeNull();
        }
        await Promise.all([closed, remoteClosed]);
      }
      await connecting;
      if (event === "legacy timeout") {
        expect(bridge.capabilities).toEqual({ protocolVersion: 1, legacy: true });
        expect(bridge.isConnected).toBe(true);
        expect(warnings).toHaveBeenCalledExactlyOnceWith("bridge", expect.stringContaining("protocol version 1"));
      } else {
        expect(bridge.capabilities).toBeNull();
        expect(bridge.isConnected).toBe(false);
        expect(warnings).not.toHaveBeenCalled();
      }
      expect(socket.listenerCount("message")).toBe(1);
      expect(socket.listenerCount("close")).toBe(1);
      expect(vi.getTimerCount()).toBe(0);
    } finally {
      bridge.disconnect();
      await connecting;
      vi.useRealTimers();
      warnings.mockRestore();
      await server.close();
    }
  });

  it("clears capabilities immediately when a call cannot be sent", async () => {
    const server = await withBridgeServer(() => {});
    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", server.port);

    try {
      await bridge.connect(1000);
      const socket = bridge["ws"]!;
      const send = vi.spyOn(socket, "send").mockImplementation((_data, callback) => {
        (callback as (err: Error) => void)(new Error("send failed"));
      });
      await expect(bridge.call("ping", {}, 1000)).rejects.toThrow("send failed");
      send.mockRestore();
      expect(bridge.isConnected).toBe(false);
      expect(bridge.capabilities).toBeNull();
    } finally {
      bridge.disconnect();
      await server.close();
    }
  });

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
      expect(failure).toContain("ue-mcp update --build");
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

  it("settles at once when the socket dies before the handshake is answered", async () => {
    // A bridge that accepts the upgrade and then drops the socket without
    // answering. Waiting out the whole connect budget for a reply that cannot
    // arrive puts that delay in front of the caller's first call, and calling
    // it a legacy plugin would blame the wrong thing for a network fault.
    const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
    server.on("connection", (socket) => socket.terminate());
    await once(server, "listening");
    const { port } = server.address() as AddressInfo;

    const { EditorBridge } = await import("../../src/bridge.js");
    const bridge = new EditorBridge("127.0.0.1", port);

    try {
      const startedAt = Date.now();
      await bridge.connect(5000).catch(() => undefined);
      expect(Date.now() - startedAt).toBeLessThan(2000);
      expect(bridge.capabilities).toBeNull();
      expect(bridge.isConnected).toBe(false);
    } finally {
      bridge.disconnect();
      await new Promise<void>((resolve, reject) => {
        server.close((err) => (err ? reject(err) : resolve()));
      });
    }
  });
});
