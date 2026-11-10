/**
 * Turning categories on and off in a running server (#1827): the surface each client is advertised,
 * the tools/list_changed it is sent, the refusal a turned-off action gets, and
 * the files the editor-down path writes.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import yaml from "js-yaml";
import { afterAll, afterEach, beforeAll, describe, expect, it } from "vitest";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { ToolListChangedNotificationSchema } from "@modelcontextprotocol/sdk/types.js";
import { createRuntime, type Runtime } from "../../src/server/runtime.js";
import { createMcpServer } from "../../src/server/mcp-server.js";
import { categoryDeps } from "../../src/tools/project/categories.js";
import { editorPlatformDir } from "../../src/integrations/epic/unreal-mcp-server.js";

const saved: Record<string, string | undefined> = {};
function setEnv(key: string, value: string | undefined): void {
  if (!(key in saved)) saved[key] = process.env[key];
  if (value === undefined) delete process.env[key];
  else process.env[key] = value;
}

function makeProject(tmp: string, name: string, engine: string, config?: Record<string, unknown>): string {
  const dir = path.join(tmp, name);
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: engine, Modules: [] }));
  if (config) fs.writeFileSync(path.join(dir, "ue-mcp.yml"), yaml.dump({ "ue-mcp": config }));
  return uproject;
}

interface Connected {
  client: Client;
  changes: () => number;
  call: (name: string, args: Record<string, unknown>) => Promise<{ isError: boolean; text: string }>;
  names: () => Promise<string[]>;
  close: () => Promise<void>;
}

async function connect(rt: Runtime, sessionId?: string): Promise<Connected> {
  const { server, dispose } = createMcpServer(rt, sessionId ? { sessionId } : {});
  const client = new Client({ name: `client-${sessionId ?? "solo"}`, version: "0" });
  let changes = 0;
  client.setNotificationHandler(ToolListChangedNotificationSchema, async () => {
    changes++;
  });
  const [a, b] = InMemoryTransport.createLinkedPair();
  await Promise.all([server.connect(b), client.connect(a)]);
  return {
    client,
    changes: () => changes,
    call: async (name, args) => {
      const r = await client.callTool({ name, arguments: args });
      const text = (r.content as Array<{ text?: string }>).map((c) => c.text ?? "").join("\n");
      return { isError: r.isError === true, text };
    },
    names: async () => (await client.listTools()).tools.map((t) => t.name),
    close: async () => {
      dispose();
      await client.close();
      await server.close();
    },
  };
}

const flush = () => new Promise((r) => setTimeout(r, 50));
let tmp: string;
const realProbe = categoryDeps.editorHoldsProject;

beforeAll(() => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-category-toggle-"));
  setEnv("UE_MCP_PORT", "1");
  setEnv("UE_MCP_HOST", "127.0.0.1");
  setEnv("UE_MCP_DISABLE_UPDATE_CHECK", "1");
});

afterEach(() => {
  categoryDeps.editorHoldsProject = realProbe;
});

afterAll(() => {
  for (const [key, value] of Object.entries(saved)) {
    if (value === undefined) delete process.env[key];
    else process.env[key] = value;
  }
  fs.rmSync(tmp, { recursive: true, force: true });
});

describe("set_category_enabled on the full surface, editor down", () => {
  let rt: Runtime;
  let uproject: string;
  let c: Connected;

  beforeAll(async () => {
    setEnv("UE_MCP_CONTEXT_STRATEGY", "full");
    uproject = makeProject(tmp, "Toggle", "5.8", { nativeTools: { exclude: ["niagara"] } });
    rt = await createRuntime([uproject]);
    c = await connect(rt);
  }, 120_000);

  afterAll(async () => {
    await c.close();
  });

  it("takes a whole category off the list, writes disable and the per-user setting file, and says so", async () => {
    categoryDeps.editorHoldsProject = async () => false;
    expect(await c.names()).toContain("gas");
    const before = c.changes();
    const r = await c.call("project", { action: "set_category_enabled", args: { category: "gas", enabled: false } });
    expect(r.isError, r.text).toBe(false);
    const body = JSON.parse(r.text);
    expect(body.engine.route).toBe("settings-file");
    expect(body.engine.fileScope).toBe("user");
    await flush();
    expect(c.changes()).toBeGreaterThan(before);
    expect(await c.names()).not.toContain("gas");

    const dir = path.dirname(uproject);
    const doc = yaml.load(fs.readFileSync(path.join(dir, "ue-mcp.yml"), "utf-8")) as { "ue-mcp": { disable: string[]; nativeTools?: unknown } };
    // The deprecated exclude moved into disable on the first write.
    expect(doc["ue-mcp"].disable).toEqual(["niagara:epic", "gas"]);
    expect(doc["ue-mcp"].nativeTools).toBeUndefined();
    const ini = fs.readFileSync(path.join(dir, "Saved", "Config", editorPlatformDir(), "EditorPerProjectUserSettings.ini"), "utf-8");
    expect(ini).toContain("[/Script/ToolsetRegistry.ToolsetRegistrySettings]");
    expect(ini).toMatch(/\+BlockedNames=\S*[Gg]as|\+BlockedNames=\S*Ability/);
  });

  it("refuses a call to the turned-off category through dispatch, naming the command that turns it back on", async () => {
    const r = await rt.callAction("gas", Object.keys(rt.primaryLoad.surface.tools.find((t) => t.name === "gas")!.actions)[0], {});
    const text = r.content.map((b) => (b.type === "text" ? b.text : "")).join("\n");
    expect(r.isError).toBe(true);
    expect(text).toMatch(/^Error \[CATEGORY_DISABLED\]/);
    expect(text).toContain("project(action='set_category_enabled', category='gas', enabled=true)");
    expect(text).toContain("ue-mcp tools enable gas");
  });

  it("puts the category back, and the per-user setting file loses its block", async () => {
    categoryDeps.editorHoldsProject = async () => false;
    const r = await c.call("project", { action: "set_category_enabled", args: { category: "gas", enabled: true } });
    expect(r.isError, r.text).toBe(false);
    await flush();
    expect(await c.names()).toContain("gas");
    const ini = fs.readFileSync(path.join(path.dirname(uproject), "Saved", "Config", editorPlatformDir(), "EditorPerProjectUserSettings.ini"), "utf-8");
    expect(ini).not.toMatch(/BlockedNames=\S*[Gg]as/);
  });

  it("takes only the Epic actions out with scope epic, and the action enum follows", async () => {
    categoryDeps.editorHoldsProject = async () => false;
    const before = (await c.client.listTools()).tools.find((t) => t.name === "animation")!;
    expect(JSON.stringify(before.inputSchema)).toContain("epic_");
    const r = await c.call("project", { action: "set_category_enabled", args: { category: "animation", enabled: false, scope: "epic" } });
    expect(r.isError, r.text).toBe(false);
    await flush();
    const after = (await c.client.listTools()).tools.find((t) => t.name === "animation")!;
    expect(after.description).not.toMatch(/\bepic_[a-z]/);
    const listed = await c.call("project", { action: "list_categories", args: {} });
    const animation = JSON.parse(listed.text).categories.find((x: { name: string }) => x.name === "animation");
    expect(animation).toMatchObject({ enabled: true, native: { enabled: true }, epic: { enabled: false } });
    expect(animation.epic.toolsets.length).toBeGreaterThan(0);
  });

  it("refuses to turn off project or epic, and an unknown category, without writing anything", async () => {
    const file = path.join(path.dirname(uproject), "ue-mcp.yml");
    const before = fs.readFileSync(file, "utf-8");
    for (const [category, scope] of [["project", "all"], ["epic", "native"]]) {
      const r = await c.call("project", { action: "set_category_enabled", args: { category, enabled: false, scope } });
      expect(r.isError).toBe(true);
      expect(r.text).toContain("cannot be turned off");
    }
    const unknown = await c.call("project", { action: "set_category_enabled", args: { category: "nope", enabled: false } });
    expect(unknown.text).toContain("Unknown category 'nope'");
    expect(fs.readFileSync(file, "utf-8")).toBe(before);
  });

  it("refuses an engine change while an editor holds the project but does not answer", async () => {
    categoryDeps.editorHoldsProject = async () => true;
    const file = path.join(path.dirname(uproject), "ue-mcp.yml");
    const before = fs.readFileSync(file, "utf-8");
    const r = await c.call("project", { action: "set_category_enabled", args: { category: "pcg", enabled: false } });
    expect(r.isError).toBe(true);
    expect(r.text).toContain("bridge is not answering");
    expect(fs.readFileSync(file, "utf-8")).toBe(before);
    const toolsets = await c.call("project", { action: "set_toolset_enabled", args: { toolsets: ["X.Y"], enabled: false } });
    expect(toolsets.isError).toBe(true);
    expect(toolsets.text).toContain("bridge is not answering");
    // A native-only change touches no toolset, so it does not need the editor.
    const native = await c.call("project", { action: "set_category_enabled", args: { category: "pcg", enabled: false, scope: "native" } });
    expect(native.isError, native.text).toBe(false);
  });
});

describe("set_category_enabled on the micro gateway", () => {
  it("drops the category from the gateway's list and refuses a call to it", async () => {
    setEnv("UE_MCP_CONTEXT_STRATEGY", "micro");
    categoryDeps.editorHoldsProject = async () => false;
    const uproject = makeProject(tmp, "Micro", "5.9");
    const rt = await createRuntime([uproject]);
    const c = await connect(rt);
    try {
      const categories = async () => (JSON.parse((await c.call("tools", { action: "list_categories" })).text).categories as Array<{ category: string }>).map((x) => x.category);
      expect(await categories()).toContain("pcg");
      const r = await c.call("tools", { action: "call", category: "project", method: "set_category_enabled", args: { category: "pcg", enabled: false } });
      expect(r.isError, r.text).toBe(false);
      expect(JSON.parse(r.text).engine).toMatchObject({ route: "settings-file", fileScope: "project" });
      expect(fs.readFileSync(path.join(path.dirname(uproject), "Config", "DefaultGame.ini"), "utf-8")).toContain("+BlockedNames=");
      expect(await categories()).not.toContain("pcg");
      const refused = await c.call("tools", { action: "call", category: "pcg", method: "list_graphs", args: {} });
      expect(refused.isError).toBe(true);
      expect(refused.text).toContain("ue-mcp tools enable pcg");
    } finally {
      await c.close();
    }
  }, 120_000);
});

