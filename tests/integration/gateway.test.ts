/**
 * What a client does through the micro gateway, against a real server with
 * no editor running: discover the surface, call actions that answer from
 * this process, and get a prompt not-connected answer from the rest. Then a
 * server started with no project, which serves whatever bridge UE_MCP_PORT
 * names.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox } from "./harness.js";

/** A call that needs an editor must answer well inside this with none running. */
const NOT_CONNECTED_BOUND_MS = 10_000;

let sb: Sandbox;
let project: string;
let server: McpServer;

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Game", { content: true });
  server = await McpServer.start(sb, { projects: [project] });
}, 60_000);

afterAll(async () => {
  await server?.close();
  sb.cleanup();
});

const gateway = (args: Record<string, unknown>) => server.call("tools", args);
const call = (category: string, method: string, args: Record<string, unknown> = {}) =>
  gateway({ action: "call", category, method, args });

describe("discovery", () => {
  it("lists every category with a summary", async () => {
    covers("gateway.list_categories");
    const r = await gateway({ action: "list_categories" });
    expect(r.isError).toBe(false);
    const body = json<{ count: number; categories: Array<{ category: string; summary: string }> }>(r);
    expect(body.count).toBe(body.categories.length);
    const names = body.categories.map((c) => c.category);
    for (const c of ["project", "asset", "blueprint", "level", "material", "niagara", "plugins"]) expect(names).toContain(c);
    for (const c of body.categories) expect(c.summary.length).toBeGreaterThan(10);
  });

  it("ranks action signatures for a query, best match first", async () => {
    covers("gateway.search");
    const r = await gateway({ action: "search", query: "create blueprint" });
    expect(r.isError).toBe(false);
    const body = json<{ query: string; count: number; results: string[] }>(r);
    expect(body.query).toBe("create blueprint");
    expect(body.results.length).toBe(body.count);
    expect(body.results[0]).toMatch(/^blueprint\.create\(/);
    expect(body.results.every((s) => /^[a-z_]+\.[a-z_0-9]+\(/.test(s))).toBe(true);
  });

  it("pages a category's signatures and describes one action's parameters", async () => {
    covers("gateway.describe");
    const page = json<{ category: string; count: number; signatures: string[]; nextOffset?: number }>(
      await gateway({ action: "describe", category: "blueprint" }),
    );
    expect(page.category).toBe("blueprint");
    expect(page.signatures.some((s) => s.startsWith("create("))).toBe(true);
    if (page.nextOffset !== undefined) {
      const next = json<{ signatures: string[] }>(await gateway({ action: "describe", category: "blueprint", offset: page.nextOffset }));
      expect(next.signatures.length).toBeGreaterThan(0);
      expect(next.signatures[0]).not.toBe(page.signatures[0]);
    }

    const one = json<{ tool: string; action: string; bridge: string; params: Array<{ name: string; required: boolean }> }>(
      await gateway({ action: "describe", category: "level", method: "nudge_component" }),
    );
    expect(one.tool).toBe("level");
    expect(one.action).toBe("nudge_component");
    expect(one.bridge).toBe("nudge_component");
    expect(one.params.find((p) => p.name === "componentName")?.required).toBe(true);

    const unknown = await gateway({ action: "describe", category: "no_such_category" });
    expect(unknown.isError).toBe(true);
    expect(unknown.text).toContain('Unknown category "no_such_category"');
    expect(unknown.text).toContain("blueprint");
  });
});

describe("calls", () => {
  it("dispatches a category action and refuses args the action does not take", async () => {
    covers("gateway.call");
    const info = await call("project", "get_info");
    expect(info.isError).toBe(false);
    expect(json(info).projectName).toBe("Game");

    const stray = await call("blueprint", "create", { bogus: 1 });
    expect(stray.isError).toBe(true);
    expect(stray.text).toContain("INVALID_PARAMS");
    expect(stray.text).toContain("bogus");

    const nested = await call("blueprint", "create", { action: "x" });
    expect(nested.isError).toBe(true);
    expect(nested.text).toContain("args.action");

    const missing = await call("blueprint", "no_such_method");
    expect(missing.isError).toBe(true);
  });

  it("answers server-side actions with no editor", async () => {
    covers("actions.local");
    const status = json<{ editorConnected: boolean; mode: string; project: { name: string; engineAssociation: string } }>(
      await call("project", "get_status"),
    );
    expect(status.editorConnected).toBe(false);
    expect(status.mode).toBe("disconnected");
    expect(status.project.name).toBe("Game");
    expect(status.project.engineAssociation).toBe("5.6");

    const info = json<{ projectName: string; uprojectContents: { FileVersion: number } }>(await call("project", "get_info"));
    expect(info.uprojectContents.FileVersion).toBe(3);

    const editors = json<{ editorCount: number; editors: Array<{ name: string; connected: boolean }> }>(
      await call("project", "list_editors"),
    );
    expect(editors.editorCount).toBe(1);
    expect(editors.editors[0]).toMatchObject({ name: "Game", connected: false });

    const plugins = json<{ pluginCount: number; plugins: unknown[] }>(await call("plugins", "list"));
    expect(plugins.pluginCount).toBe(0);
    expect(plugins.plugins).toEqual([]);
  });

  it("answers an editor action promptly with a not-connected error naming the way out", async () => {
    covers("server.editor-down");
    const started = Date.now();
    const r = await call("blueprint", "create", { path: "/Game/BP_X", parentClass: "Actor" });
    const elapsed = Date.now() - started;
    expect(elapsed).toBeLessThan(NOT_CONNECTED_BOUND_MS);
    expect(r.isError).toBe(true);
    expect(r.text).toContain("needs a running editor");
    expect(r.text).toContain("start_editor");
    const machine = r.blocks.find((b) => b.startsWith("MACHINE_ERROR="));
    expect(machine).toBeDefined();
    expect(JSON.parse(machine!.slice("MACHINE_ERROR=".length))).toMatchObject({
      code: "NOT_CONNECTED",
      method: "create_blueprint",
      editorConnected: false,
    });
  });

  it("answers the same way through the daemon, the default server mode", async () => {
    covers("server.editor-down");
    const viaDaemon = await McpServer.start(sb, { projects: [project], mode: "daemon" });
    try {
      const started = Date.now();
      const r = await viaDaemon.call("tools", { action: "call", category: "level", method: "place_actor", args: { actorClass: "PointLight" } });
      expect(Date.now() - started).toBeLessThan(NOT_CONNECTED_BOUND_MS);
      expect(r.isError).toBe(true);
      expect(r.text).toContain("NOT_CONNECTED");
    } finally {
      await viaDaemon.close();
    }
  }, 60_000);
});

describe("no project", () => {
  it("serves the bridge UE_MCP_PORT names", async () => {
    covers("server.no-project");
    const bridge = await FakeBridge.start({ projectDir: sb.path("bare-bridge") });
    const bare = await McpServer.start(sb, { projects: [], port: bridge.port });
    try {
      const status = json<{ editorConnected: boolean; project: unknown; editorTarget: { port: number; projectPath: string | null } }>(
        await bare.call("tools", { action: "call", category: "project", method: "get_status" }),
      );
      expect(status.project).toBeNull();
      expect(status.editorConnected).toBe(true);
      expect(status.editorTarget).toMatchObject({ port: bridge.port, projectPath: null });

      const r = await bare.call("tools", { action: "call", category: "asset", method: "list_textures", args: { directory: "/Game" } });
      expect(r.isError).toBe(false);
      expect(json(r).servedBy).toBe(path.join(sb.root, "bare-bridge"));
      expect(bridge.calls.find((c) => c.method === "list_textures")?.params).toEqual({ directory: "/Game" });
    } finally {
      await bare.close();
      await bridge.stop();
    }
  }, 60_000);
});
