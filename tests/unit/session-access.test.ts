/**
 * Read-only session mode (#1484): every action declared `mutate` or `unknown`
 * is refused and every `read` action passes, at dispatch and at the bridge.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { createRuntime, type Runtime } from "../../src/server/runtime.js";
import { createMcpServer } from "../../src/server/mcp-server.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { buildMicroGateway } from "../../src/surface/context/micro-context.js";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { GuardRegistry, type BridgeGuard } from "../../src/flow/guard.js";
import { withCallAsker } from "../../src/flow/call-elicit.js";
import {
  SessionAccess,
  parseSessionAccessMode,
  readOnlyRefusal,
  type SessionAccessMode,
} from "../../src/flow/session-access.js";
import { bridgeMethodEffect, mayChangeState } from "../../src/surface/action-effects.js";
import type { IBridge } from "../../src/bridge/bridge.js";

const graph = [...ALL_TOOLS, buildMicroGateway(ALL_TOOLS)];
const every = graph.flatMap((t) => Object.entries(t.actions).map(([action, spec]) => ({
  task: `${t.name}.${action}`, category: t.name, action, effect: spec.effect,
})));

describe("readOnlyRefusal over every declared action", () => {
  it("covers a real surface with all three effects", () => {
    expect(every.length).toBeGreaterThan(1000);
    for (const effect of ["read", "mutate", "unknown"] as const) {
      expect(every.some((a) => a.effect === effect)).toBe(true);
    }
  });

  it("refuses every mutate and unknown action and passes every read", () => {
    const wrong = every.filter((a) => (readOnlyRefusal(a.task, graph) !== null) !== (a.effect !== "read"));
    expect(wrong.map((a) => `${a.task} (${a.effect})`)).toEqual([]);
  });

  it("refuses execute_python and execute_command, naming the mode", () => {
    for (const task of ["editor.execute_python", "editor.execute_command"]) {
      const why = readOnlyRefusal(task, graph);
      expect(why).toMatch(/^read-only session mode refuses editor\(execute_/);
      expect(why).toContain("its parameters decide what it does");
    }
  });

  it("refuses a name nothing declares", () => {
    expect(readOnlyRefusal("nowhere.made_up", graph)).toMatch(/read-only session mode refuses nowhere\(made_up\)/);
  });
});

describe("parseSessionAccessMode and SessionAccess", () => {
  it("knows the two named modes and nothing else", () => {
    expect(parseSessionAccessMode("read-only")).toBe("read-only");
    expect(parseSessionAccessMode("read-write")).toBe("read-write");
    expect(parseSessionAccessMode("readonly")).toBeUndefined();
    expect(parseSessionAccessMode(true)).toBeUndefined();
  });

  it("starts read-write and reports a change only when there is one", () => {
    const a = new SessionAccess();
    expect(a.mode).toBe("read-write");
    expect(a.set("read-only")).toBe(true);
    expect(a.set("read-only")).toBe(false);
    expect(a.mode).toBe("read-only");
  });
});

function fakeInner(): IBridge & { calls: string[] } {
  const calls: string[] = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    call: async (method) => {
      calls.push(method);
      return { ok: true };
    },
  };
}

describe("the read-only guard on the bridge", () => {
  const bridgeMethods = [...new Set(ALL_TOOLS.flatMap((t) => Object.values(t.actions))
    .flatMap((s) => (s.kind === "bridge" ? [s.bridge] : [])))];

  const asSession = <T>(mode: SessionAccessMode, fn: () => Promise<T>) => withCallAsker({ access: () => mode }, fn);

  it("refuses every bridge method that may change state and lets every read through", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, new GuardRegistry(), () => null);
    const wrong: string[] = [];
    for (const method of bridgeMethods) {
      const changes = mayChangeState(bridgeMethodEffect(method, {}).effect);
      const refused = await asSession("read-only", () => gb.call(method, {})).then(() => false, (e: Error) => {
        expect(e.message).toMatch(/^read-only session mode refuses bridge method /);
        return true;
      });
      if (refused !== changes) wrong.push(method);
    }
    expect(wrong).toEqual([]);
    expect(inner.calls.every((m) => !mayChangeState(bridgeMethodEffect(m, {}).effect))).toBe(true);
  });

  it("refuses execute_python and execute_command at the bridge", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, new GuardRegistry(), () => null);
    await expect(asSession("read-only", () => gb.call("execute_python", { code: "print(1)" }))).rejects.toThrow(/read-only session mode/);
    await expect(asSession("read-only", () => gb.call("execute_command", { command: "stat fps" }))).rejects.toThrow(/read-only session mode/);
    expect(inner.calls).toEqual([]);
  });

  it("leaves a read-write session, and a call with no session, writable", async () => {
    const inner = fakeInner();
    const gb = new GuardedBridge(inner, new GuardRegistry(), () => null);
    await asSession("read-write", () => gb.call("execute_python", { code: "print(1)" }));
    await gb.call("execute_python", { code: "print(1)" });
    expect(inner.calls).toEqual(["execute_python", "execute_python"]);
  });

  it("runs ahead of every declared guard, so none acts on a refused call", async () => {
    const acted: string[] = [];
    const early: BridgeGuard = { name: "aaa", order: -1_000_000, before: async (cc) => { acted.push(cc.method); } };
    const reg = new GuardRegistry().register(early) as GuardRegistry;
    const gb = new GuardedBridge(fakeInner(), reg, () => null);
    await expect(asSession("read-only", () => gb.call("execute_python", { code: "" }))).rejects.toThrow(/read-only/);
    expect(acted).toEqual([]);
    expect(reg.names()).toEqual(["aaa"]);
  });
});

describe("dispatch in read-only mode", () => {
  let tmp: string;
  let rt: Runtime;
  const saved: Record<string, string | undefined> = {};
  const setEnv = (key: string, value: string | undefined) => {
    if (!(key in saved)) saved[key] = process.env[key];
    if (value === undefined) delete process.env[key];
    else process.env[key] = value;
  };

  beforeAll(async () => {
    tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-session-access-"));
    const dir = path.join(tmp, "ReadOnly");
    fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
    const uproject = path.join(dir, "ReadOnly.uproject");
    fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));
    setEnv("UE_MCP_PORT", "1");
    setEnv("UE_MCP_HOST", "127.0.0.1");
    setEnv("UE_MCP_DISABLE_UPDATE_CHECK", "1");
    setEnv("UE_MCP_CONTEXT_STRATEGY", "full");
    rt = await createRuntime([uproject]);
  }, 120_000);

  afterAll(() => {
    for (const [key, value] of Object.entries(saved)) {
      if (value === undefined) delete process.env[key];
      else process.env[key] = value;
    }
    fs.rmSync(tmp, { recursive: true, force: true });
  });

  it("refuses every mutate and unknown action before it runs", async () => {
    const changing = rt.primaryLoad.surface.tools.flatMap((t) => Object.entries(t.actions)
      .filter(([, s]) => s.effect !== "read").map(([action]) => ({ category: t.name, action })));
    expect(changing.length).toBeGreaterThan(500);
    const wrong: string[] = [];
    for (const { category, action } of changing) {
      const r = await rt.callAction(category, action, {}, { access: "read-only" });
      const text = r.content.map((c) => c.text).join("\n");
      if (!r.isError || !text.startsWith(`Error [WRITE_BLOCKED]: read-only session mode refuses ${category}(${action})`)) {
        wrong.push(`${category}.${action}: ${text.slice(0, 120)}`);
      }
    }
    expect(wrong).toEqual([]);
  }, 120_000);

  it("passes a read action the gate", async () => {
    const r = await rt.callAction("project", "get_status", {}, { access: "read-only" });
    expect(r.content.map((c) => c.text).join("\n")).not.toContain("read-only session mode");
  }, 60_000);

  it("holds per MCP session: one client read-only, another on the same runtime writable", async () => {
    const connect = async (access: SessionAccess) => {
      const { server, dispose } = createMcpServer(rt, { access });
      const client = new Client({ name: "session-access-test", version: "0" });
      const [a, b] = InMemoryTransport.createLinkedPair();
      await Promise.all([server.connect(b), client.connect(a)]);
      return { client, close: async () => { dispose(); await client.close(); await server.close(); } };
    };
    const ro = new SessionAccess("read-only");
    const one = await connect(ro);
    const two = await connect(new SessionAccess());
    const text = (r: unknown) => ((r as { content: Array<{ text: string }> }).content).map((c) => c.text).join("\n");
    try {
      const args = { action: "create", args: { name: "BP_ReadOnly", path: "/Game" } };
      const refused = await one.client.callTool({ name: "blueprint", arguments: args });
      expect(text(refused)).toMatch(/^Error \[WRITE_BLOCKED\]: read-only session mode refuses blueprint\(create\)/);
      const allowed = await two.client.callTool({ name: "blueprint", arguments: args });
      expect(text(allowed)).not.toContain("read-only session mode");
      // The mode is read per call: switching back lets the next call through.
      ro.set("read-write");
      expect(text(await one.client.callTool({ name: "blueprint", arguments: args }))).not.toContain("read-only session mode");
    } finally {
      await one.close();
      await two.close();
    }
  }, 60_000);
});
