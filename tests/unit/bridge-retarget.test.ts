// #818: the connection target and the resolved project must move together.
// These cover the bridge half of that guarantee: retargeting drops the socket
// to the previous editor, re-decides the port from the new project alone, and
// refuses to connect when the port cannot be attributed to the new project.
import { once } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { EditorBridge } from "../../src/bridge.js";
import { deriveProjectPort } from "../../src/port.js";

/** A stand-in editor that answers every call with its own name. */
async function fakeEditor(name: string): Promise<{
  port: number;
  received: string[];
  close: () => Promise<void>;
}> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  const received: string[] = [];
  server.on("connection", (socket) => {
    socket.on("message", (data) => {
      const request = JSON.parse(data.toString()) as { id: string; method: string };
      received.push(request.method);
      socket.send(JSON.stringify({ id: request.id, result: { editor: name } }));
    });
  });
  await once(server, "listening");
  return {
    port: (server.address() as AddressInfo).port,
    received,
    close: async () => {
      for (const client of server.clients) client.terminate();
      await new Promise<void>((resolve, reject) => {
        server.close((err) => (err ? reject(err) : resolve()));
      });
    },
  };
}

/** Temp .uproject, optionally with the lockfile its editor would publish. */
function makeProject(name: string, lockfilePort?: number): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `ue-mcp-818-${name}-`));
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.7" }));
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  if (lockfilePort !== undefined) {
    const lockDir = path.join(dir, "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(lockDir, { recursive: true });
    fs.writeFileSync(
      path.join(lockDir, "port.json"),
      JSON.stringify({ port: lockfilePort, pid: 4242 }),
    );
  }
  return uproject;
}

describe("EditorBridge.retargetProject", () => {
  let savedEnvPort: string | undefined;

  beforeEach(() => {
    savedEnvPort = process.env.UE_MCP_PORT;
    delete process.env.UE_MCP_PORT;
  });

  afterEach(() => {
    if (savedEnvPort === undefined) delete process.env.UE_MCP_PORT;
    else process.env.UE_MCP_PORT = savedEnvPort;
  });

  it("moves a live connection off the previous editor and onto the new project's", async () => {
    const editorA = await fakeEditor("A");
    const editorB = await fakeEditor("B");
    const projectA = makeProject("A", editorA.port);
    const projectB = makeProject("B", editorB.port);

    const bridge = new EditorBridge("127.0.0.1", editorA.port);
    bridge.setProjectContext(projectA);

    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "A" });

      const target = bridge.retargetProject(projectB);
      // The socket to A is gone the moment the retarget returns, before any
      // await gives another handler a chance to run.
      expect(bridge.isConnected).toBe(false);
      expect(target.projectPath).toBe(path.resolve(projectB));
      expect(target.port).toBe(editorB.port);
      expect(target.portSource).toBe("lockfile");
      expect(target.verified).toBe(true);

      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "B" });
      expect(editorA.received).toEqual(["ping"]);
      expect(editorB.received).toEqual(["ping"]);
    } finally {
      bridge.disconnect();
      await editorA.close();
      await editorB.close();
    }
  });

  it("re-derives the port from the new project root when it publishes no lockfile", () => {
    const projectA = makeProject("A");
    const projectB = makeProject("B");

    const bridge = new EditorBridge();
    bridge.setProjectContext(projectA);
    expect(bridge.port).toBe(deriveProjectPort(path.dirname(projectA)));

    const target = bridge.retargetProject(projectB);
    expect(target.port).toBe(deriveProjectPort(path.dirname(projectB)));
    expect(target.port).not.toBe(deriveProjectPort(path.dirname(projectA)));
    expect(target.portSource).toBe("derived");
    expect(target.verified).toBe(true);
  });

  it("prefers the new project's configured port over the previous project's", () => {
    const bridge = new EditorBridge();
    bridge.setProjectContext(makeProject("A"));

    const target = bridge.retargetProject(makeProject("B"), 9999);
    expect(target.port).toBe(9999);
    expect(target.portSource).toBe("config");
    expect(target.verified).toBe(true);
  });

  it("refuses to connect when only an inherited pin names the port", async () => {
    process.env.UE_MCP_PORT = "9877";
    const bridge = new EditorBridge();
    bridge.setProjectContext(makeProject("A"));

    const projectB = makeProject("B");
    const target = bridge.retargetProject(projectB);
    expect(target.verified).toBe(false);
    expect(target.port).toBe(9877);

    await expect(bridge.connect(200)).rejects.toThrow(/Refusing to connect/);
    await expect(bridge.call("ping", {}, 200)).rejects.toThrow(/UE_MCP_PORT/);
    expect(bridge.isConnected).toBe(false);
  });

  it("accepts the pinned port again once the new project's editor publishes a lockfile", async () => {
    process.env.UE_MCP_PORT = "9877";
    const editorB = await fakeEditor("B");
    const bridge = new EditorBridge();
    bridge.setProjectContext(makeProject("A"));

    const projectB = makeProject("B");
    expect(bridge.retargetProject(projectB).verified).toBe(false);

    // The editor starts and publishes the port it actually bound.
    const lockDir = path.join(path.dirname(projectB), "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(lockDir, { recursive: true });
    fs.writeFileSync(path.join(lockDir, "port.json"), JSON.stringify({ port: editorB.port, pid: 1 }));

    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "B" });
      expect(bridge.getTarget().verified).toBe(true);
      expect(bridge.getTarget().portSource).toBe("lockfile");
    } finally {
      bridge.disconnect();
      await editorB.close();
    }
  });

  it("abandons a handshake that was in flight when the target moved", async () => {
    const editorA = await fakeEditor("A");
    const editorB = await fakeEditor("B");
    const projectA = makeProject("A", editorA.port);
    const projectB = makeProject("B", editorB.port);

    const bridge = new EditorBridge();
    bridge.setProjectContext(projectA);

    try {
      const connecting = bridge.connect(1000);
      // Same tick: the handshake with A has not completed yet.
      bridge.retargetProject(projectB);

      await expect(connecting).rejects.toThrow(/retargeted while connecting/);
      expect(bridge.isConnected).toBe(false);

      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "B" });
      expect(editorA.received).toEqual([]);
    } finally {
      bridge.disconnect();
      await editorA.close();
      await editorB.close();
    }
  });
});
