// D6: the data path and the lifecycle path have to agree about whether an
// editor exists.
//
// EditorBridge.connect() used to read port.json and validate nothing but the
// port number, so a record a crashed editor left behind cleared the #818 pin
// refusal and the client dialled a port that may since belong to something
// else. It also had no instances/<pid>.json fallback, so when one of two
// editors of a project quit and took the shared port.json with it, ordinary
// tool calls reported "not connected" against a survivor that
// editor(stop_editor) resolved perfectly well.
import { once } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { EditorBridge } from "../../src/bridge.js";
import { resolveLiveBridgeAddress } from "../../src/editor-target.js";

async function fakeEditor(name: string): Promise<{ port: number; close: () => Promise<void> }> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  server.on("connection", (socket) => {
    socket.on("message", (data) => {
      const request = JSON.parse(data.toString()) as { id: string; method: string };
      socket.send(JSON.stringify({ id: request.id, result: { editor: name } }));
    });
  });
  await once(server, "listening");
  return {
    port: (server.address() as AddressInfo).port,
    close: async () => {
      for (const client of server.clients) client.terminate();
      await new Promise<void>((resolve, reject) => server.close((e) => (e ? reject(e) : resolve())));
    },
  };
}

function makeProject(name: string): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `ue-mcp-d6-${name}-`));
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.7" }));
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  return uproject;
}

function bridgeDir(uproject: string): string {
  const dir = path.join(path.dirname(uproject), "Saved", "UE_MCP_Bridge");
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

/** A pid nothing is running under. Large, and re-checked so the test cannot
 *  accidentally name a live process on a busy machine. */
function deadPid(): number {
  for (let candidate = 999_001; candidate < 999_999; candidate++) {
    try {
      process.kill(candidate, 0);
    } catch (e) {
      if ((e as NodeJS.ErrnoException).code !== "EPERM") return candidate;
    }
  }
  throw new Error("no dead pid available");
}

describe("resolveLiveBridgeAddress", () => {
  it("takes port.json when the pid it names is alive", () => {
    const uproject = makeProject("alive");
    fs.writeFileSync(
      path.join(bridgeDir(uproject), "port.json"),
      JSON.stringify({ port: 51234, pid: process.pid }),
    );
    const live = resolveLiveBridgeAddress(path.dirname(uproject));
    expect(live).toEqual({ port: 51234, pid: process.pid, source: "port.json" });
  });

  // The auditor's case: an old plugin build publishes no pid at all, and
  // refusing it would refuse a healthy editor for having an old binary.
  it("takes a port.json that names no pid, because older plugin builds publish none", () => {
    const uproject = makeProject("nopid");
    fs.writeFileSync(path.join(bridgeDir(uproject), "port.json"), JSON.stringify({ port: 51235 }));
    const live = resolveLiveBridgeAddress(path.dirname(uproject));
    expect(live).toEqual({ port: 51235, pid: null, source: "port.json" });
  });

  it("discards a port.json whose editor has exited", () => {
    const uproject = makeProject("dead");
    fs.writeFileSync(
      path.join(bridgeDir(uproject), "port.json"),
      JSON.stringify({ port: 51236, pid: deadPid() }),
    );
    expect(resolveLiveBridgeAddress(path.dirname(uproject))).toBeNull();
  });

  // #934: two editors of one project share Saved/UE_MCP_Bridge/, and the one
  // that quits removes port.json on its way out.
  it("recovers the survivor's port from its own instance record when port.json is gone", () => {
    const uproject = makeProject("survivor");
    const dir = bridgeDir(uproject);
    fs.mkdirSync(path.join(dir, "instances"), { recursive: true });
    fs.writeFileSync(
      path.join(dir, "instances", `${process.pid}.json`),
      JSON.stringify({ port: 51237, pid: process.pid, state: "listening" }),
    );
    const live = resolveLiveBridgeAddress(path.dirname(uproject));
    expect(live?.port).toBe(51237);
    expect(live?.source).toBe("instance-record");
  });

  it("never dials a bind-failed instance record", () => {
    const uproject = makeProject("bindfail");
    const dir = bridgeDir(uproject);
    fs.mkdirSync(path.join(dir, "instances"), { recursive: true });
    fs.writeFileSync(
      path.join(dir, "instances", `${process.pid}.json`),
      JSON.stringify({ port: 51238, pid: process.pid, state: "bind-failed" }),
    );
    expect(resolveLiveBridgeAddress(path.dirname(uproject))).toBeNull();
  });
});

describe("EditorBridge.connect agrees with the lifecycle path", () => {
  let savedEnvPort: string | undefined;

  beforeEach(() => {
    savedEnvPort = process.env.UE_MCP_PORT;
    delete process.env.UE_MCP_PORT;
  });

  afterEach(() => {
    if (savedEnvPort === undefined) delete process.env.UE_MCP_PORT;
    else process.env.UE_MCP_PORT = savedEnvPort;
  });

  // Before D6 this passed: the lockfile validated only the port number, so
  // portSource became "lockfile", unverifiedPin was cleared, and the #818
  // refusal never fired even though the process named by the file is gone.
  it("keeps the pin refusal when the only lockfile names a dead editor", async () => {
    process.env.UE_MCP_PORT = "9877";
    const projectA = makeProject("A");
    const projectB = makeProject("stale");
    // The editor of B crashed. Its port.json outlived it, and the port it
    // names can since have been taken by anything.
    fs.writeFileSync(
      path.join(bridgeDir(projectB), "port.json"),
      JSON.stringify({ port: 51239, pid: deadPid() }),
    );

    const bridge = new EditorBridge();
    bridge.setProjectContext(projectA);
    const target = bridge.retargetProject(projectB);

    expect(target.verified).toBe(false);
    expect(target.port).not.toBe(51239);
    await expect(bridge.connect(200)).rejects.toThrow(/Refusing to connect/);
    expect(bridge.getTarget().portSource).not.toBe("lockfile");
  });

  // Before D6 this failed: connect() had no instance-record fallback, so the
  // survivor was unreachable to every ordinary tool call while
  // editor(stop_editor), which reads the same directory, resolved it.
  it("reaches the survivor of two editors after the other took port.json with it", async () => {
    const editor = await fakeEditor("survivor");
    const uproject = makeProject("twoeditors");
    const dir = bridgeDir(uproject);
    fs.mkdirSync(path.join(dir, "instances"), { recursive: true });
    fs.writeFileSync(
      path.join(dir, "instances", `${process.pid}.json`),
      JSON.stringify({ port: editor.port, pid: process.pid, state: "listening" }),
    );

    const bridge = new EditorBridge();
    bridge.setProjectContext(uproject);
    try {
      await expect(bridge.call("ping", {}, 1000)).resolves.toEqual({ editor: "survivor" });
      expect(bridge.port).toBe(editor.port);
    } finally {
      bridge.disconnect();
      await editor.close();
    }
  });
});
