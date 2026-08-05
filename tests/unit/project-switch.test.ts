// #818: after a project switch, path resolution and the editor connection
// must describe the same project, or bridge actions mutate the project the
// caller just switched away from.
//
// Each test drives the real ProjectContext and the real EditorBridge against
// stand-in editors on loopback, so "which editor answered" is observed rather
// than asserted about a mock.
import { once } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import yaml from "js-yaml";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { EditorBridge } from "../../src/bridge.js";
import { ProjectContext } from "../../src/project.js";
import { switchProject, isTargetDiverged } from "../../src/project-switch.js";

async function fakeEditor(name: string): Promise<{
  port: number;
  received: string[];
  hang: boolean;
  close: () => Promise<void>;
}> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  const state = {
    port: 0,
    received: [] as string[],
    hang: false,
    close: async () => {
      for (const client of server.clients) client.terminate();
      await new Promise<void>((resolve, reject) => {
        server.close((err) => (err ? reject(err) : resolve()));
      });
    },
  };
  server.on("connection", (socket) => {
    socket.on("message", (data) => {
      const request = JSON.parse(data.toString()) as { id: string; method: string };
      state.received.push(request.method);
      if (state.hang) return;
      socket.send(JSON.stringify({ id: request.id, result: { editor: name } }));
    });
  });
  await once(server, "listening");
  state.port = (server.address() as AddressInfo).port;
  return state;
}

function makeProject(name: string, opts?: { lockfilePort?: number; configPort?: number }): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `ue-mcp-switch-${name}-`));
  const uproject = path.join(dir, `${name}.uproject`);
  // No EngineAssociation: nothing in these tests should touch a real engine.
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3 }));
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  if (opts?.lockfilePort !== undefined) {
    const lockDir = path.join(dir, "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(lockDir, { recursive: true });
    fs.writeFileSync(path.join(lockDir, "port.json"), JSON.stringify({ port: opts.lockfilePort, pid: 7 }));
  }
  if (opts?.configPort !== undefined) {
    fs.writeFileSync(
      path.join(dir, "ue-mcp.yml"),
      yaml.dump({ "ue-mcp": { version: 1, bridge: { port: opts.configPort } } }),
    );
  }
  return uproject;
}

/** The invariant, checked as one statement. */
function expectPairAgrees(project: ProjectContext, bridge: EditorBridge, uproject: string): void {
  expect(project.projectPath).toBe(path.resolve(uproject));
  expect(bridge.getTarget().projectPath).toBe(path.resolve(uproject));
  expect(isTargetDiverged(project, bridge.getTarget())).toBe(false);
}

describe("switchProject", () => {
  let savedEnvPort: string | undefined;
  let globalCfg: string;

  beforeEach(() => {
    savedEnvPort = process.env.UE_MCP_PORT;
    delete process.env.UE_MCP_PORT;
    // Keep the developer's ~/.ue-mcp/config.yml out of these tests.
    globalCfg = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-global-")), "config.yml");
    process.env.UE_MCP_GLOBAL_CONFIG = globalCfg;
  });

  afterEach(() => {
    delete process.env.UE_MCP_GLOBAL_CONFIG;
    if (savedEnvPort === undefined) delete process.env.UE_MCP_PORT;
    else process.env.UE_MCP_PORT = savedEnvPort;
  });

  it("moves path resolution and the live connection to the new project together", async () => {
    const editorA = await fakeEditor("A");
    const editorB = await fakeEditor("B");
    const projectA = makeProject("A", { lockfilePort: editorA.port });
    const projectB = makeProject("B", { lockfilePort: editorB.port });

    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(projectA);
    bridge.setProjectContext(project.projectPath);

    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "A" });
      expectPairAgrees(project, bridge, projectA);

      const result = await switchProject(project, bridge, projectB, { connectTimeoutMs: 1000 });

      expect(result.connected).toBe(true);
      expect(result.previousProjectPath).toBe(path.resolve(projectA));
      expectPairAgrees(project, bridge, projectB);
      // Content paths and bridge calls now name the same project.
      expect(project.resolveContentPath("/Game/Thing")).toContain(path.dirname(projectB));
      await expect(bridge.call("place_actor", {}, 1000)).resolves.toEqual({ editor: "B" });
      expect(editorA.received).toEqual(["ping"]);
      expect(editorB.received).toEqual(["place_actor"]);
    } finally {
      bridge.disconnect();
      await editorA.close();
      await editorB.close();
    }
  });

  it("leaves nothing reachable in the old editor when the new one is not running", async () => {
    const editorA = await fakeEditor("A");
    const projectA = makeProject("A", { lockfilePort: editorA.port });
    // No lockfile and no editor: project B is simply not open.
    const projectB = makeProject("B");

    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(projectA);
    bridge.setProjectContext(project.projectPath);

    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "A" });

      const result = await switchProject(project, bridge, projectB, { connectTimeoutMs: 300 });

      expect(result.connected).toBe(false);
      expect(result.connectError).toBeTruthy();
      expectPairAgrees(project, bridge, projectB);
      expect(bridge.isConnected).toBe(false);

      // This is the bug: before the fix, this call executed in project A.
      await expect(bridge.call("place_actor", {}, 300)).rejects.toThrow();
      expect(editorA.received).toEqual(["ping"]);
    } finally {
      bridge.disconnect();
      await editorA.close();
    }
  });

  it("fails a call that was in flight to the old editor rather than letting it land", async () => {
    const editorA = await fakeEditor("A");
    const editorB = await fakeEditor("B");
    editorA.hang = true;
    const projectA = makeProject("A", { lockfilePort: editorA.port });
    const projectB = makeProject("B", { lockfilePort: editorB.port });

    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(projectA);
    bridge.setProjectContext(project.projectPath);

    try {
      await bridge.connect(1000);
      // Assert on the rejection before the switch runs, so the failure is
      // observed the moment it happens rather than a tick later.
      const inFlight = expect(bridge.call("slow_write", {}, 5000)).rejects.toThrow(/retargeted/);

      await switchProject(project, bridge, projectB, { connectTimeoutMs: 1000 });

      await inFlight;
      expectPairAgrees(project, bridge, projectB);
    } finally {
      bridge.disconnect();
      await editorA.close();
      await editorB.close();
    }
  });

  it("refuses a bad target and leaves both halves on the current project", async () => {
    const editorA = await fakeEditor("A");
    const projectA = makeProject("A", { lockfilePort: editorA.port });

    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(projectA);
    bridge.setProjectContext(project.projectPath);

    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "A" });

      const missingDir = path.join(os.tmpdir(), "ue-mcp-switch-does-not-exist");
      await expect(switchProject(project, bridge, missingDir)).rejects.toThrow(/not found/i);

      // Nothing moved: not the path resolver, not the socket.
      expectPairAgrees(project, bridge, projectA);
      expect(bridge.isConnected).toBe(true);
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "A" });

      const emptyDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-switch-empty-"));
      await expect(switchProject(project, bridge, emptyDir)).rejects.toThrow(/No .uproject/);
      expectPairAgrees(project, bridge, projectA);
      expect(bridge.isConnected).toBe(true);
    } finally {
      bridge.disconnect();
      await editorA.close();
    }
  });

  it("takes the bridge port from the new project's config, not the old one's", async () => {
    const editorB = await fakeEditor("B");
    // A pins a port; B pins its own. Neither has published a lockfile.
    const projectA = makeProject("A", { configPort: 9911 });
    const projectB = makeProject("B", { configPort: editorB.port });

    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(projectA);
    bridge.setConfigPort(project.config.bridge?.port);
    bridge.setProjectContext(project.projectPath);
    expect(bridge.port).toBe(9911);

    try {
      const result = await switchProject(project, bridge, projectB, { connectTimeoutMs: 1000 });

      expect(result.target.port).toBe(editorB.port);
      expect(result.connected).toBe(true);
      expectPairAgrees(project, bridge, projectB);
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "B" });
    } finally {
      bridge.disconnect();
      await editorB.close();
    }
  });

  it("completes the switch but blocks the connection when only an inherited pin names the port", async () => {
    const editorA = await fakeEditor("A");
    const projectA = makeProject("A");
    const projectB = makeProject("B");

    // A pin from the environment: chosen for whatever project the process
    // started on, and no evidence at all about project B.
    process.env.UE_MCP_PORT = String(editorA.port);
    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(projectA);
    bridge.setProjectContext(project.projectPath);

    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "A" });

      const result = await switchProject(project, bridge, projectB, { connectTimeoutMs: 300 });

      expect(result.connected).toBe(false);
      expect(result.connectError).toMatch(/Refusing to connect/);
      expect(result.target.verified).toBe(false);
      expectPairAgrees(project, bridge, projectB);

      // The pinned port still has editor A listening on it. Nothing reaches it.
      await expect(bridge.call("place_actor", {}, 300)).rejects.toThrow(/Refusing to connect/);
      expect(editorA.received).toEqual(["ping"]);
    } finally {
      bridge.disconnect();
      await editorA.close();
    }
  });
});
