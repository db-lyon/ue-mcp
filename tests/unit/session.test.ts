/**
 * Editor sessions (#817): an arbitrary number of editors from one server.
 *
 * The properties that matter here are the ones a wrong answer corrupts a
 * project over: one session per resolved project root, a separate port and
 * lockfile per session, and a target that resolves to the editor the caller
 * named or to none at all.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { SessionRegistry, sessionKeyFor } from "../../src/session.js";
import { deriveProjectPort, DEFAULT_BRIDGE_PORT } from "../../src/port.js";
import {
  categoryTool,
  injectEditorTarget,
  removeEditorTarget,
  stripEditorTarget,
} from "../../src/types.js";
import { z } from "zod";

let root: string;

function makeProject(name: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }), "utf-8");
  return uproject;
}

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-sessions-"));
  delete process.env.UE_MCP_PORT;
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
  delete process.env.UE_MCP_PORT;
});

describe("SessionRegistry", () => {
  it("keys one session per resolved project root, whatever spelling it arrives in", () => {
    const uproject = makeProject("Alpha");
    const registry = new SessionRegistry();

    const a = registry.register({ projectPath: uproject });
    const b = registry.register({ projectPath: path.dirname(uproject) });
    const c = registry.register({ projectPath: path.dirname(uproject) + path.sep });

    expect(b).toBe(a);
    expect(c).toBe(a);
    if (process.platform === "win32") {
      // A backslash is a separator only where it is one.
      expect(registry.register({ projectPath: uproject.replace(/\//g, "\\") })).toBe(a);
    }
    expect(registry.size).toBe(1);
  });

  it("gives every session its own bridge port and its own lockfile", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const registry = new SessionRegistry();

    const a = registry.register({ projectPath: alpha });
    const b = registry.register({ projectPath: beta });

    expect(a.bridge.port).toBe(deriveProjectPort(path.dirname(alpha)));
    expect(b.bridge.port).toBe(deriveProjectPort(path.dirname(beta)));
    expect(a.bridge.port).not.toBe(b.bridge.port);
    expect(a.bridge.projectPathForLockfile).toBe(a.project.projectPath);
    expect(b.bridge.projectPathForLockfile).toBe(b.project.projectPath);
  });

  it("keeps a project-less default session on the legacy fixed port", () => {
    const registry = new SessionRegistry();
    const session = registry.register({});

    expect(registry.size).toBe(1);
    expect(session.bridge.port).toBe(DEFAULT_BRIDGE_PORT);
    expect(session.project.isLoaded).toBe(false);
    // An untargeted call still lands somewhere: this is the single-editor path.
    expect(registry.resolve()).toBe(session);
    expect(registry.resolve(undefined)).toBe(session);
  });

  it("resolves a target by session name, project name, or project path", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const registry = new SessionRegistry();
    const a = registry.register({ projectPath: alpha, name: "left" });
    const b = registry.register({ projectPath: beta });

    expect(registry.resolve("left")).toBe(a);
    expect(registry.resolve("LEFT")).toBe(a);
    expect(registry.resolve("Alpha")).toBe(a);
    expect(registry.resolve(beta)).toBe(b);
    expect(registry.resolve(path.dirname(beta))).toBe(b);
  });

  it("refuses an unknown target instead of falling back to some other editor", () => {
    const registry = new SessionRegistry();
    registry.register({ projectPath: makeProject("Alpha") });
    registry.register({ projectPath: makeProject("Beta") });

    expect(() => registry.resolve("Gamma")).toThrowError(/No editor session named 'Gamma'/);
    expect(() => registry.resolve("Gamma")).toThrowError(/Alpha, Beta/);
  });

  it("de-duplicates session names so two same-named projects stay addressable", () => {
    const registry = new SessionRegistry();
    const one = registry.register({ projectPath: makeProject("Game") });
    fs.mkdirSync(path.join(root, "worktree"), { recursive: true });
    const twoDir = path.join(root, "worktree", "Game");
    fs.mkdirSync(twoDir, { recursive: true });
    fs.writeFileSync(path.join(twoDir, "Game.uproject"), "{}", "utf-8");
    const two = registry.register({ projectPath: path.join(twoDir, "Game.uproject") });

    expect(one.name).toBe("Game");
    expect(two.name).toBe("Game-2");
    expect(registry.resolve("Game-2")).toBe(two);
  });

  it("moves the default target without touching the session set", () => {
    const registry = new SessionRegistry();
    const a = registry.register({ projectPath: makeProject("Alpha") });
    const b = registry.register({ projectPath: makeProject("Beta") });

    expect(registry.active).toBe(a);
    expect(registry.use("Beta")).toBe(b);
    expect(registry.active).toBe(b);
    expect(registry.size).toBe(2);
    expect(registry.resolve()).toBe(b);
  });

  it("drops a session, keeps the rest, and refuses to drop the last one", () => {
    const registry = new SessionRegistry();
    const a = registry.register({ projectPath: makeProject("Alpha") });
    const b = registry.register({ projectPath: makeProject("Beta") });

    const dropped = registry.drop("Alpha");
    expect(dropped.name).toBe("Alpha");
    expect(registry.size).toBe(1);
    expect(registry.active).toBe(b);
    expect(() => registry.resolve("Alpha")).toThrowError(/No editor session named 'Alpha'/);
    expect(() => registry.drop("Beta")).toThrowError(/only registered session/);
    void a;
  });

  it("reports a change once per registration and once per drop", () => {
    const registry = new SessionRegistry();
    const counts: number[] = [];
    registry.onCountChanged = (n) => counts.push(n);

    registry.register({ projectPath: makeProject("Alpha") });
    registry.register({ projectPath: makeProject("Beta") });
    registry.drop("Alpha");

    expect(counts).toEqual([1, 2, 1]);
  });

  it("re-files a session when its project moves, without growing the set", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const registry = new SessionRegistry();
    const session = registry.register({ projectPath: alpha });

    // What set_project does: switchProject moves the ProjectContext and the
    // socket together, then the registry moves the entry with them.
    session.project.setProject(beta);
    registry.rekey(session);

    expect(registry.size).toBe(1);
    expect(registry.active).toBe(session);
    expect(session.name).toBe("Beta");
    expect(registry.resolve("Beta")).toBe(session);
    expect(registry.resolve(beta)).toBe(session);
    // The project it left is no longer addressable through this server.
    expect(() => registry.resolve("Alpha")).toThrowError(/No editor session named 'Alpha'/);
  });

  it("refuses to re-file a session onto a project another session already holds", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const registry = new SessionRegistry();
    const a = registry.register({ projectPath: alpha });
    const b = registry.register({ projectPath: beta });

    a.project.setProject(beta);
    expect(() => registry.rekey(a)).toThrowError(/already registered for that project/);
    expect(registry.resolve("Beta")).toBe(b);
  });

  it("records sessions that collapse onto one port, because they cannot be told apart", () => {
    process.env.UE_MCP_PORT = "9999";
    const registry = new SessionRegistry();
    const a = registry.register({ projectPath: makeProject("Alpha") });
    const b = registry.register({ projectPath: makeProject("Beta") });

    expect(a.bridge.port).toBe(9999);
    expect(b.bridge.port).toBe(9999);
    expect(a.portSharedWith).toEqual(["Beta"]);
    expect(b.portSharedWith).toEqual(["Alpha"]);
    expect(a.info(true).portSharedWith).toEqual(["Beta"]);
  });

  it("normalizes a project path to the same key from either form", () => {
    const dir = path.join(root, "Game");
    const expected = path.resolve(dir).replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();

    expect(sessionKeyFor(path.join(dir, "Game.uproject"))).toBe(expected);
    expect(sessionKeyFor(dir)).toBe(expected);
    expect(sessionKeyFor(dir + path.sep)).toBe(expected);
    expect(sessionKeyFor(dir.toUpperCase())).toBe(expected);
  });
});

describe("per-call editor targeting", () => {
  const build = () =>
    categoryTool("demoCategory", "summary", { ping: { description: "d", bridge: "ping" } }, undefined, {
      assetPath: z.string().optional(),
    });

  it("is absent until a second editor exists, and restores the exact schema when it goes", () => {
    const tool = build();
    const before = Object.keys(tool.schema);
    expect(before).not.toContain("editor");

    injectEditorTarget(tool, ["Alpha", "Beta"]);
    expect(Object.keys(tool.schema)).toContain("editor");
    expect(tool.injectedEditorParam).toBe(true);

    removeEditorTarget(tool);
    expect(Object.keys(tool.schema)).toEqual(before);
    expect(tool.injectedEditorParam).toBe(false);
  });

  it("refuses to shadow a tool that declares its own editor parameter", () => {
    const tool = categoryTool("plugged", "summary", { ping: { bridge: "ping" } }, undefined, {
      editor: z.string().optional(),
    });
    const outcome = injectEditorTarget(tool, ["Alpha", "Beta"]);

    expect(outcome.injected).toBe(false);
    expect(outcome.reason).toMatch(/declares its own 'editor' parameter/);
    expect(tool.injectedEditorParam).toBeUndefined();
  });

  it("never forwards the routing parameter into a bridge call", async () => {
    const tool = build();
    injectEditorTarget(tool, ["Alpha", "Beta"]);
    const calls: Array<Record<string, unknown> | undefined> = [];
    const ctx = {
      bridge: {
        isConnected: true,
        connect: async () => {},
        call: async (_m: string, p?: Record<string, unknown>) => { calls.push(p); return { ok: true }; },
      },
      project: {} as never,
    };

    await tool.handler(ctx as never, { action: "ping", editor: "Beta", assetPath: "/Game/A" });

    expect(calls[0]).toEqual({ assetPath: "/Game/A" });
  });

  it("leaves an editor parameter alone on a tool that was never targeted", async () => {
    const tool = build();
    const calls: Array<Record<string, unknown> | undefined> = [];
    const ctx = {
      bridge: {
        isConnected: true,
        connect: async () => {},
        call: async (_m: string, p?: Record<string, unknown>) => { calls.push(p); return { ok: true }; },
      },
      project: {} as never,
    };

    await tool.handler(ctx as never, { action: "ping", editor: "its own meaning" });

    expect(calls[0]).toEqual({ editor: "its own meaning" });
  });

  it("strips only the routing parameter", () => {
    expect(stripEditorTarget({ editor: "B", a: 1 })).toEqual({ a: 1 });
    expect(stripEditorTarget({ a: 1 })).toEqual({ a: 1 });
  });
});
