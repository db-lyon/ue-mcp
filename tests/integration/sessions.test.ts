/**
 * One server, several projects, each with its own bridge: what the `editor`
 * parameter routes, what an untargeted write is refused for, and how
 * project(add_editor, use_editor, drop_editor, list_editors) change the set.
 * Every routing assertion is made on what the OTHER bridge received, since a
 * misrouted call is invisible from the one that was meant to get it.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox, type CallResult } from "./harness.js";

interface EditorsList {
  editorCount: number;
  activeEditor: string;
  editors: Array<{ name: string; connected: boolean; active: boolean; port: number }>;
}

/** Calls a test made, without the dialog probe the guard sends on its own. */
const made = (b: FakeBridge): string[] => b.methods.filter((m) => m !== "list_dialogs");

function servingEditor(r: CallResult): string | null {
  const block = r.blocks.find((b) => b.startsWith("MACHINE_EDITOR="));
  return block ? (JSON.parse(block.slice("MACHINE_EDITOR=".length)) as { editor: string }).editor : null;
}

let sb: Sandbox;
let alpha: string;
let beta: string;
let gamma: string;
let alphaBridge: FakeBridge;
let betaBridge: FakeBridge;
let gammaBridge: FakeBridge;

beforeAll(async () => {
  sb = new Sandbox();
  alpha = sb.makeProject("Alpha");
  beta = sb.makeProject("Beta");
  gamma = sb.makeProject("Gamma");
  [alphaBridge, betaBridge, gammaBridge] = await Promise.all(
    [alpha, beta, gamma].map((p) => FakeBridge.start({ projectDir: path.dirname(p) })),
  );
});

afterAll(async () => {
  await Promise.all([alphaBridge, betaBridge, gammaBridge].map((b) => b?.stop()));
  sb.cleanup();
});

function reset(): void {
  for (const b of [alphaBridge, betaBridge, gammaBridge]) b.calls.length = 0;
}

describe("two projects", () => {
  let server: McpServer;
  const call = (category: string, method: string, args: Record<string, unknown> = {}) =>
    server.call("tools", { action: "call", category, method, args });

  beforeAll(async () => {
    server = await McpServer.start(sb, { projects: [alpha, beta], port: null });
  }, 60_000);
  afterAll(async () => {
    await server?.close();
  });

  it("runs one session per project, each attached to its own bridge", async () => {
    covers("server.multi-project");
    expect(server.instructions).toContain("MULTIPLE EDITORS");
    expect(server.instructions).toContain("Alpha");
    expect(server.instructions).toContain("Beta");
    const list = json<EditorsList>(await call("project", "list_editors"));
    expect(list.editorCount).toBe(2);
    expect(list.activeEditor).toBe("Alpha");
    const byName = Object.fromEntries(list.editors.map((e) => [e.name, e]));
    expect(byName.Alpha).toMatchObject({ connected: true, active: true, port: alphaBridge.port });
    expect(byName.Beta).toMatchObject({ connected: true, active: false, port: betaBridge.port });
  });

  it("routes a call to the named editor's bridge and no other", async () => {
    covers("sessions.targeting");
    reset();
    const toBeta = await call("asset", "list_textures", { directory: "/Game", editor: "Beta" });
    expect(toBeta.isError).toBe(false);
    expect(json(toBeta).servedBy).toBe(path.dirname(beta));
    expect(servingEditor(toBeta)).toBe("Beta");
    expect(made(betaBridge)).toEqual(["list_textures"]);
    expect(made(alphaBridge)).toEqual([]);
    // The routing key is the client's; the editor never sees it.
    expect(betaBridge.calls.find((c) => c.method === "list_textures")!.params).toEqual({ directory: "/Game" });

    reset();
    const toAlpha = await call("asset", "delete", { assetPath: "/Game/Gone", editor: "Alpha" });
    expect(toAlpha.isError).toBe(false);
    expect(servingEditor(toAlpha)).toBe("Alpha");
    expect(made(alphaBridge)).toEqual(["delete_asset"]);
    expect(made(betaBridge)).toEqual([]);

    const unknown = await call("asset", "list_textures", { directory: "/Game", editor: "Nobody" });
    expect(unknown.isError).toBe(true);
    expect(unknown.text).toContain("Nobody");
  });

  it("refuses an untargeted write and sends it nowhere, while an untargeted read runs in the active editor", async () => {
    covers("sessions.gating", "guards.write-detect");
    reset();
    const write = await call("asset", "delete", { assetPath: "/Game/Gone" });
    expect(write.isError).toBe(true);
    expect(write.text).toContain("INVALID_PARAMS");
    expect(write.text).toContain("Alpha, Beta");
    expect(write.text).toContain('editor="<name>"');
    const spawn = await call("level", "place_actor", { actorClass: "PointLight" });
    expect(spawn.isError).toBe(true);
    expect(made(alphaBridge)).toEqual([]);
    expect(made(betaBridge)).toEqual([]);

    const read = await call("asset", "list_textures", { directory: "/Game" });
    expect(read.isError).toBe(false);
    expect(servingEditor(read)).toBe("Alpha");
    expect(made(alphaBridge)).toEqual(["list_textures"]);
    expect(made(betaBridge)).toEqual([]);
  });
});

describe("changing the session set", () => {
  let server: McpServer;
  const call = (category: string, method: string, args: Record<string, unknown> = {}) =>
    server.call("tools", { action: "call", category, method, args });
  const editors = async () => json<EditorsList>(await call("project", "list_editors"));

  beforeAll(async () => {
    server = await McpServer.start(sb, { projects: [alpha], port: null });
  }, 60_000);
  afterAll(async () => {
    await server?.close();
  });

  it("adds, switches, drops and lists editors, and routing follows", async () => {
    covers("sessions.editor-management");
    expect((await editors()).editors.map((e) => e.name)).toEqual(["Alpha"]);

    const added = json<{ editor: string; editorConnected: boolean; editorCount: number; bridgePort: number }>(
      await call("project", "add_editor", { projectPath: gamma.split(path.sep).join("/") }),
    );
    expect(added).toMatchObject({ editor: "Gamma", editorConnected: true, editorCount: 2, bridgePort: gammaBridge.port });
    let list = await editors();
    expect(list.editors.map((e) => e.name).sort()).toEqual(["Alpha", "Gamma"]);
    expect(list.activeEditor).toBe("Alpha");

    // Two editors now, so an untargeted write is refused.
    reset();
    expect((await call("asset", "delete", { assetPath: "/Game/Gone" })).isError).toBe(true);
    expect(made(gammaBridge)).toEqual([]);

    const used = await call("project", "use_editor", { editorTarget: "Gamma" });
    expect(used.isError).toBe(false);
    list = await editors();
    expect(list.activeEditor).toBe("Gamma");
    reset();
    await call("asset", "list_textures", { directory: "/Game" });
    expect(made(gammaBridge)).toEqual(["list_textures"]);
    expect(made(alphaBridge)).toEqual([]);

    const dropped = await call("project", "drop_editor", { editorTarget: "Alpha" });
    expect(dropped.isError).toBe(false);
    list = await editors();
    expect(list.editors.map((e) => e.name)).toEqual(["Gamma"]);
    expect(list.activeEditor).toBe("Gamma");

    // One editor again: an untargeted write runs, in the one that is left.
    reset();
    const write = await call("asset", "delete", { assetPath: "/Game/Gone" });
    expect(write.isError).toBe(false);
    expect(made(gammaBridge)).toEqual(["delete_asset"]);
    expect(made(alphaBridge)).toEqual([]);

    const missing = await call("project", "use_editor", { editorTarget: "Alpha" });
    expect(missing.isError).toBe(true);
  });
});
