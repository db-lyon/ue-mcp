/**
 * Addressing, gating and the union surface against a real editor (#817, 7.3).
 *
 * The multi-editor cases need more than one session, and this machine has one
 * editor. That is not a limitation to work around: a session is registered for
 * every argv project regardless of editor state (plan item 4.4), and the
 * session COUNT is what arms targeting, gating, attribution and the union
 * refusal. So the server here drives two sessions, the live editor and a
 * throwaway project whose editor is not running, and every assertion about
 * "beyond one editor" is made with a real editor on one side of it.
 *
 * What that buys over the engine-free tier: these run through the shipped
 * entry point rather than through the routing functions directly, so a refusal
 * that is never wired in, a union that advertises what dispatch cannot serve,
 * or an attribution block that never reaches the client all fail here.
 *
 * Nothing here starts or stops an editor, and every call that reaches the live
 * editor is either a read or a change to an asset path that does not exist.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { EditorBridge } from "../../src/bridge.js";
import { SessionRegistry } from "../../src/session.js";
import { LiveServer, resultJson, servingEditor } from "./server.js";
import { closeLiveBridges, liveTarget, makeTempProject } from "./harness.js";

const target = await liveTarget();

/** The session name each project gets: its project name. */
const LIVE = "ue_mcp";
const SECOND = "LiveTierSecond";

/**
 * The second project disables a category in its own config. Disabling is the
 * one config key whose effect has to survive into a multi-editor surface: the
 * category stays advertised, because hiding it would take a working tool away
 * from the OTHER editor, and dispatch to the session that disabled it is
 * refused naming the config (plan item 4.2).
 */
const SECOND_CONFIG = ["ue-mcp:", "  version: 1", "  disable:", "    - niagara", ""].join("\n");

const second = makeTempProject(SECOND, SECOND_CONFIG);

let server: LiveServer;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject, second.uproject] });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
  second.cleanup();
});

describe("two sessions, one of them a live editor", () => {
  it("registers the project whose editor is not running, and reports both", async () => {
    const result = await server.call("project", { action: "list_editors", editor: LIVE });
    expect(result.isError).toBe(false);
    const body = resultJson<{ editors: Array<{ name: string; connected: boolean; port: number }> }>(result);
    const names = body.editors.map((e) => e.name).sort();
    expect(names).toEqual([LIVE, SECOND].sort());

    const liveEditor = body.editors.find((e) => e.name === LIVE)!;
    const stopped = body.editors.find((e) => e.name === SECOND)!;
    expect(liveEditor.port).toBe(target.port);
    expect(stopped.connected).toBe(false);
    // Two sessions on one port cannot be told apart, so a live tier that
    // silently collapsed onto one would prove nothing about routing.
    expect(stopped.port).not.toBe(liveEditor.port);
  }, 120_000);

  it("documents targeting in the instructions, which one editor does not", () => {
    expect(server.instructions).toContain(LIVE);
    expect(server.instructions).toContain(SECOND);
  });

  it("names the serving editor on a response", async () => {
    const result = await server.call("asset", {
      action: "list_textures",
      directory: "/Game",
      recursive: false,
      editor: LIVE,
    });
    expect(result.isError).toBe(false);
    expect(servingEditor(result)).toBe(LIVE);
  }, 120_000);
});

describe("gating beyond one editor", () => {
  it("refuses an untargeted change and names both editors", async () => {
    const result = await server.call("asset", {
      action: "delete",
      assetPath: "/Game/LiveTierNoSuchAsset",
    });
    expect(result.isError).toBe(true);
    expect(result.text).toContain(LIVE);
    expect(result.text).toContain(SECOND);
    expect(result.text).toContain("editor=");
  }, 120_000);

  it("refuses an untargeted lifecycle action, which is the one that closes a window", async () => {
    // Refused before anything is resolved, so no editor is asked to stop.
    const result = await server.call("editor", { action: "stop_editor" });
    expect(result.isError).toBe(true);
    expect(result.text).toContain(SECOND);
  }, 120_000);

  it("serves an untargeted read from the active session", async () => {
    const result = await server.call("asset", {
      action: "list_textures",
      directory: "/Game",
      recursive: false,
    });
    expect(result.isError).toBe(false);
    expect(servingEditor(result)).toBe(LIVE);
  }, 120_000);

  it("runs a targeted change in the editor that was named", async () => {
    // A delete of a path that does not exist: the editor answers, nothing is
    // removed, and the assertion is about where the call went.
    const result = await server.call("asset", {
      action: "delete",
      assetPath: "/Game/LiveTierNoSuchAsset",
      editor: LIVE,
    });
    expect(servingEditor(result)).toBe(LIVE);
  }, 120_000);
});

describe("the union surface", () => {
  it("keeps disabled categories out of lean catalog discovery", async () => {
    const lean = await LiveServer.start({ projects: [target.uproject, second.uproject], env: { UE_MCP_CONTEXT_STRATEGY: "lean" } });
    try {
      const search = resultJson<any>(await lean.call("catalog", { action: "search", query: "niagara", editor: SECOND }));
      expect(search.results.some((hit: any) => hit.category === "niagara")).toBe(false);
      const schema = resultJson<any>(await lean.call("catalog", { action: "describe", category: "niagara", editor: SECOND }));
      expect(schema.error).toContain("Unknown category");
    } finally { await lean.close(); }
  }, 240_000);

  it("discovers only actions enabled in the addressed session", async () => {
    for (const editor of [LIVE, SECOND]) {
      const search = resultJson<any>(await server.call("project", { action: "search_tools", query: "niagara", editor }));
      expect(search.results.some((hit: any) => hit.tool === "niagara")).toBe(editor === LIVE);
      const schema = await server.call("project", { action: "describe_action", category: "niagara", editor });
      expect(!!schema.isError).toBe(editor === SECOND);
      if (editor === SECOND) expect(schema.text).toContain("Unknown category");
    }
  }, 120_000);

  it("advertises the live editor's toolsets even though the other project has none", async () => {
    const tools = await server.listTools();
    const names = tools.map((t) => t.name);
    expect(names).toContain("asset");
    // Enrichment ran against the live editor, so the advertised categories are
    // at least what a project with no editor could contribute on its own.
    expect(names.length).toBeGreaterThan(10);
  }, 120_000);

  it("advertises a category one project disabled, and refuses it for that project", async () => {
    const tools = await server.listTools();
    expect(tools.map((t) => t.name)).toContain("niagara");

    const refused = await server.call("niagara", {
      action: "list",
      directory: "/Game",
      editor: SECOND,
    });
    expect(refused.isError).toBe(true);
    expect(refused.text).toContain(SECOND);
    expect(refused.text).toContain("disable");
  }, 120_000);

  it("keeps the same category working for the editor that did not disable it", async () => {
    const allowed = await server.call("niagara", {
      action: "list",
      directory: "/Game",
      editor: LIVE,
    });
    // The editor answers it: what matters is that this is not the config
    // refusal the other session gets.
    expect(allowed.text).not.toContain("under 'disable'");
    expect(servingEditor(allowed)).toBe(LIVE);
  }, 120_000);
});

describe("identity", () => {
  it("reports the project the connected editor actually has open", async () => {
    // The handshake is answered off the game thread from values snapshotted at
    // startup, so this is the running binary's own account of itself.
    expect(target.capabilities.projectName).toBe("ue_mcp");
    expect(target.capabilities.pid).toBeGreaterThan(0);
    expect(target.capabilities.port).toBe(target.port);
  });

  it("refuses to connect a session whose port was pinned for another project", async () => {
    // The shipped form of the identity refusal: a port that was chosen for a
    // different project is not evidence about this one, so the connect is
    // refused rather than landing every later mutation in an editor the caller
    // never asked for. The pin here is the live editor's real port, which is
    // exactly the dangerous case.
    const previous = process.env.UE_MCP_PORT;
    process.env.UE_MCP_PORT = String(target.port);
    try {
      const bridge = new EditorBridge();
      bridge.retargetProject(second.uproject);
      expect(bridge.getTarget().verified).toBe(false);
      await expect(bridge.connect(2000)).rejects.toThrow(/Refusing to connect/);
      bridge.disconnect();
    } finally {
      if (previous === undefined) delete process.env.UE_MCP_PORT;
      else process.env.UE_MCP_PORT = previous;
    }
  });

  it("accepts the same pin for the project it was chosen for", async () => {
    // At one editor the pin IS the target, so it connects and the live editor
    // answers. This is the "warn and proceed" side of the same rule: the
    // refusal is for a port that belongs to another project.
    const previous = process.env.UE_MCP_PORT;
    process.env.UE_MCP_PORT = String(target.port);
    try {
      const bridge = new EditorBridge();
      bridge.setProjectContext(target.uproject);
      await bridge.connect(5000);
      expect(bridge.isConnected).toBe(true);
      expect(bridge.capabilities?.projectName).toBe("ue_mcp");
      bridge.disconnect();
    } finally {
      if (previous === undefined) delete process.env.UE_MCP_PORT;
      else process.env.UE_MCP_PORT = previous;
    }
  }, 60_000);
});

describe("two editors of one project", () => {
  it("registers one session for the project however the path is spelled", () => {
    // One session per project path is a decision, not an accident: every
    // address channel in the codebase is project-keyed, so a second session
    // for one project would be two handles competing for one editor.
    const sessions = new SessionRegistry();
    const first = sessions.register({ projectPath: target.uproject });
    const again = sessions.register({ projectPath: target.projectDir });
    const upper = sessions.register({ projectPath: target.uproject.toUpperCase() });
    expect(sessions.size).toBe(1);
    expect(again).toBe(first);
    expect(upper).toBe(first);
    first.bridge.disconnect();
  });

  it("keeps a per-process instance record next to the port lockfile", () => {
    // The bridge writes Saved/UE_MCP_Bridge/instances/<pid>.json, one file per
    // process, which is what makes two editors of one project describable at
    // all: `port.json` names one port for the whole directory, so the second
    // editor to boot used to overwrite the first one's address.
    const dir = path.join(target.projectDir, "Saved", "UE_MCP_Bridge", "instances");
    expect(fs.existsSync(dir)).toBe(true);

    const records = fs
      .readdirSync(dir)
      .filter((f) => f.endsWith(".json"))
      .map((f) => ({ file: f, body: JSON.parse(fs.readFileSync(path.join(dir, f), "utf-8")) as Record<string, unknown> }));

    const mine = records.find((r) => Number(r.body.pid) === target.capabilities.pid);
    expect(mine, `no instance record for the connected editor (pid ${target.capabilities.pid})`).toBeTruthy();
    expect(mine!.file).toBe(`${target.capabilities.pid}.json`);
    expect(mine!.body.port).toBe(target.port);
    expect(String(mine!.body.state)).toBe("listening");
    expect(String(mine!.body.projectRoot).toLowerCase()).toContain("ue_mcp");
  });

  it("stamps the port lockfile with the process that owns it", () => {
    // Owner-checked writes and deletes (plan item 0.3): the record carries the
    // pid and the instance id of the process that published it, which is what
    // lets a quitting editor tell its own record from a live one's.
    const lockfile = path.join(target.projectDir, "Saved", "UE_MCP_Bridge", "port.json");
    const body = JSON.parse(fs.readFileSync(lockfile, "utf-8")) as Record<string, unknown>;
    expect(body.port).toBe(target.port);
    expect(body.pid).toBe(target.capabilities.pid);
    expect(String(body.instanceId)).toBe(String(target.capabilities.instanceId));
  });
});

describe("the no-project default bridge", () => {
  it("dispatches bridge actions with no project argument at all", async () => {
    // The documented "attach to whatever answers the port" mode: one
    // session-less default bridge, every bridge-backed action working against
    // whatever is there. Here that is the live editor.
    const bare = await LiveServer.start({ projects: [], port: target.port });
    try {
      const result = await bare.call("asset", {
        action: "list_textures",
        directory: "/Game",
        recursive: false,
      });
      expect(result.isError).toBe(false);
      // One session, so nothing is attributed and nothing is gated: the
      // single-editor shape is exactly the shape it always was.
      expect(servingEditor(result)).toBeNull();

      const status = await bare.call("project", { action: "get_status" });
      expect(status.isError).toBe(false);
      const body = resultJson<{ mode: string; editorConnected: boolean }>(status);
      expect(body.editorConnected).toBe(true);
      expect(body.mode).toBe("live");
    } finally {
      await bare.close();
    }
  }, 240_000);
});
