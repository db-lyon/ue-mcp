/**
 * Turning categories and Unreal toolsets on and off (#1827), from every place
 * a user reaches it: the `ue-mcp tools` CLI with no editor, an MCP client on a
 * running server, and the daemon's HTTP routes with an editor attached. Plus
 * the deprecated `nativeTools.exclude`, still read as `<category>:epic`.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { ToolListChangedNotificationSchema } from "@modelcontextprotocol/sdk/types.js";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, plain, runCli, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const SECTION = "[/Script/ToolsetRegistry.ToolsetRegistrySettings]";

let sb: Sandbox;
beforeAll(() => {
  sb = new Sandbox();
});
afterAll(() => sb.cleanup());

const disableOf = (uproject: string): string[] | undefined =>
  (yaml.load(fs.readFileSync(path.join(path.dirname(uproject), "ue-mcp.yml"), "utf-8")) as { "ue-mcp"?: { disable?: string[] } })["ue-mcp"]?.disable;
const defaultGame = (uproject: string): string => {
  const f = path.join(path.dirname(uproject), "Config", "DefaultGame.ini");
  return fs.existsSync(f) ? fs.readFileSync(f, "utf-8") : "";
};
const tools = (args: string[]) => runCli(sb, ["tools", ...args], { env: { UE_MCP_PORT: "1" }, timeoutMs: 120_000 });

describe("ue-mcp tools with no editor", () => {
  let uproject: string;
  beforeAll(() => {
    uproject = sb.makeProject("CliToggle", { engine: "5.9", content: true, config: "ue-mcp:\n  contentRoots: [/Game/]\n" });
  });

  it("lists every category with what is on in it and the toolsets it maps to", async () => {
    covers("cli.tools.list");
    const r = await tools(["list", "--project", uproject, "--json"]);
    expect(r.code, r.stderr).toBe(0);
    const body = JSON.parse(r.stdout);
    const gas = body.categories.find((c: { name: string }) => c.name === "gas");
    expect(gas).toMatchObject({ enabled: true, native: { enabled: true }, epic: { enabled: true } });
    expect(gas.epic.toolsets.length).toBeGreaterThan(0);
    expect(body.engine).toMatchObject({ connected: false, settingsFile: path.join(path.dirname(uproject), "Config", "DefaultGame.ini"), blocked: [] });
    const text = await tools(["list", "--project", uproject]);
    expect(text.code).toBe(0);
    expect(plain(text.stdout)).toMatch(/gas\s+on/);
  });

  it("turns a category off and on again in ue-mcp.yml and in the project's setting file", async () => {
    covers("cli.tools.toggle");
    const off = await tools(["disable", "gas", "--project", uproject]);
    expect(off.code, off.stderr + off.stdout).toBe(0);
    expect(plain(off.stdout)).toContain("gas: off");
    expect(disableOf(uproject)).toEqual(["gas"]);
    expect(defaultGame(uproject)).toContain(SECTION);
    expect(defaultGame(uproject)).toMatch(/\+BlockedNames=.*(Gas|Ability)/i);

    const on = await tools(["enable", "gas", "--project", uproject]);
    expect(on.code, on.stderr).toBe(0);
    expect(disableOf(uproject)).toBeUndefined();
    expect(defaultGame(uproject)).not.toMatch(/BlockedNames=.*(Gas|Ability)/i);
  });

  it("switches one scope of a category, and a toolset by name in the setting file only", async () => {
    covers("cli.tools.toggle");
    const r = await tools(["disable", "animation", "--epic", "--project", uproject]);
    expect(r.code, r.stderr).toBe(0);
    expect(disableOf(uproject)).toEqual(["animation:epic"]);
    const ts = await tools(["disable", "EditorToolset.MaterialToolset", "--project", uproject, "--json"]);
    expect(ts.code, ts.stderr).toBe(0);
    expect(JSON.parse(ts.stdout).results[0]).toMatchObject({ ok: true, route: "settings-file", categories: ["material"] });
    expect(defaultGame(uproject)).toContain("+BlockedNames=EditorToolset.MaterialToolset");
    expect(disableOf(uproject)).toEqual(["animation:epic"]);
  });

  it("refuses a name that is neither, and --epic on a toolset, before changing anything", async () => {
    covers("cli.tools.toggle");
    const before = fs.readFileSync(path.join(path.dirname(uproject), "ue-mcp.yml"), "utf-8");
    const bad = await tools(["disable", "notacategory", "--project", uproject]);
    expect(bad.code).toBe(2);
    expect(bad.stderr).toContain("is not a category or a toolset");
    const mixed = await tools(["disable", "X.Y", "--epic", "--project", uproject]);
    expect(mixed.code).toBe(2);
    const protectedCat = await tools(["disable", "project", "--project", uproject]);
    expect(protectedCat.code).toBe(1);
    expect(protectedCat.stderr).toContain("cannot be turned off");
    expect(fs.readFileSync(path.join(path.dirname(uproject), "ue-mcp.yml"), "utf-8")).toBe(before);
  });

  it("refuses an engine change while an editor holds the project without answering", async () => {
    covers("cli.tools.toggle");
    const dir = path.dirname(uproject);
    const instances = path.join(dir, "Saved", "UE_MCP_Bridge", "instances");
    fs.mkdirSync(instances, { recursive: true });
    // A live editor's record, on a port nothing listens on: this test process stands in for its pid.
    const record = path.join(instances, `${process.pid}.json`);
    fs.writeFileSync(record, JSON.stringify({ port: 1, pid: process.pid, state: "listening", startedAt: new Date().toISOString() }));
    try {
      const iniBefore = defaultGame(uproject);
      const r = await tools(["disable", "pcg", "--project", uproject]);
      expect(r.code).toBe(1);
      expect(r.stderr).toContain("bridge is not answering");
      expect(defaultGame(uproject)).toBe(iniBefore);
      expect(disableOf(uproject)).not.toContain("pcg");
    } finally {
      fs.rmSync(record, { force: true });
    }
  });
});

describe("a running server", () => {
  it("rebuilds the surface live, sends tools/list_changed, and refuses a turned-off action by name", async () => {
    covers("server.category-toggle");
    const uproject = sb.makeProject("ServerToggle", { engine: "5.9", content: true });
    const server = await McpServer.start(sb, { projects: [uproject], strategy: "full" });
    let changed = 0;
    server.client.setNotificationHandler(ToolListChangedNotificationSchema, async () => {
      changed++;
    });
    try {
      expect(await server.toolNames()).toContain("pcg");
      const off = await server.call("project", { action: "set_category_enabled", args: { category: "pcg", enabled: false } });
      expect(off.isError, off.text).toBe(false);
      await waitFor(() => changed > 0, 10_000, "tools/list_changed");
      expect(await server.toolNames()).not.toContain("pcg");

      const native = await server.call("project", { action: "set_category_enabled", args: { category: "level", enabled: false, scope: "native" } });
      expect(native.isError, native.text).toBe(false);
      const refused = await server.call("level", { action: "get_outliner", args: {} });
      expect(refused.isError).toBe(true);
      expect(refused.text).toMatch(/^Error \[CATEGORY_DISABLED\]/);
      expect(refused.text).toContain("ue-mcp tools enable level --native");

      const back = await server.call("project", { action: "set_category_enabled", args: { category: "pcg", enabled: true } });
      expect(back.isError, back.text).toBe(false);
      await waitFor(async () => (await server.toolNames()).includes("pcg"), 10_000, "pcg to come back");
    } finally {
      await server.close();
    }
  });

  it("reads the deprecated nativeTools.exclude as <category>:epic, and says so", async () => {
    covers("config.native-tools-exclude");
    const uproject = sb.makeProject("Exclude", { engine: "5.9", content: true, config: "ue-mcp:\n  nativeTools:\n    exclude: [gas]\n" });
    const server = await McpServer.start(sb, { projects: [uproject], strategy: "full", env: { UE_MCP_LOG_LEVEL: "warn" } });
    try {
      const gas = (await server.listTools()).find((t) => t.name === "gas")!;
      expect(gas.description).not.toMatch(/\bepic_[a-z]/);
      expect(server.log).toContain("nativeTools.exclude is deprecated and is read as disable: [gas:epic]");
      const listed = json<{ categories: Array<{ name: string; epic: { enabled: boolean } }>; deprecated: string }>(
        await server.call("project", { action: "list_categories", args: {} }),
      );
      expect(listed.categories.find((c) => c.name === "gas")!.epic.enabled).toBe(false);
      expect(listed.deprecated).toContain("gas:epic");
    } finally {
      await server.close();
    }
  });
});

describe("the daemon with an editor attached", () => {
  let bridge: FakeBridge;
  let d: Discovery;
  let uproject: string;
  const blocked = new Set<string>();

  beforeAll(async () => {
    uproject = sb.makeProject("DaemonToggle", { engine: "5.9", content: true });
    bridge = await FakeBridge.start({
      projectDir: path.dirname(uproject),
      handlers: {
        epic_set_toolset_enabled: (p) => {
          for (const t of p.toolsets as string[]) {
            if (p.enabled) blocked.delete(t);
            else blocked.add(t);
          }
          return { success: true, toolsets: p.toolsets, enabled: p.enabled };
        },
        epic_get_toolset_states: () => ({
          toolsets: ["GasToolset.Abilities", "EditorToolset.MaterialToolset"].map((name) => ({ name, enabled: !blocked.has(name) })),
        }),
      },
    });
    d = await startDaemonCli(sb, uproject, { UE_MCP_PORT: undefined });
    await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
  }, 60_000);

  afterAll(async () => {
    await stopSandboxDaemons(sb);
    await bridge?.stop();
  });

  it("lists categories with the editor's toolset states and switches one live through the bridge", async () => {
    covers("daemon.tools");
    expect((await call(d, "GET", "/v1/health")).body.features).toEqual(expect.arrayContaining(["tools.categories", "leases"]));
    const listed = await call(d, "GET", "/v1/tools");
    expect(listed.status, listed.text).toBe(200);
    expect(listed.body.engine.connected).toBe(true);
    const gas = listed.body.categories.find((c: { name: string }) => c.name === "gas");
    expect(gas.epic.toolsets).toEqual(expect.arrayContaining([expect.objectContaining({ name: "GasToolset.Abilities", present: true, enabled: true })]));

    const off = await call(d, "POST", "/v1/tools/categories", { category: "gas", enabled: false });
    expect(off.status, off.text).toBe(200);
    expect(off.body.engine.route).toBe("editor");
    const sent = bridge.calls.filter((c) => c.method === "epic_set_toolset_enabled");
    expect(sent.at(-1)!.params.enabled).toBe(false);
    expect(sent.at(-1)!.params.toolsets).toContain("GasToolset.Abilities");
    expect(disableOf(uproject)).toEqual(["gas"]);
    // Live, so nothing goes to the setting file.
    expect(defaultGame(uproject)).toBe("");

    const bad = await call(d, "POST", "/v1/tools/categories", { category: "project", enabled: false });
    expect(bad.status).toBe(422);
    expect(bad.body.error).toContain("cannot be turned off");
  });

  it("switches toolsets by name, and the CLI goes through the daemon when one serves the project", async () => {
    covers("daemon.tools");
    covers("cli.tools.toggle");
    const ts = await call(d, "POST", "/v1/tools/toolsets", { toolsets: ["EditorToolset.MaterialToolset"], enabled: false });
    expect(ts.status, ts.text).toBe(200);
    expect(ts.body).toMatchObject({ route: "editor", categories: ["material"] });
    expect(blocked.has("EditorToolset.MaterialToolset")).toBe(true);

    const r = await runCli(sb, ["tools", "enable", "gas", "--project", uproject, "--json"], { timeoutMs: 120_000 });
    expect(r.code, r.stderr).toBe(0);
    expect(JSON.parse(r.stdout).results[0]).toMatchObject({ ok: true, engine: { route: "editor" } });
    expect(bridge.calls.filter((c) => c.method === "epic_set_toolset_enabled").at(-1)!.params.enabled).toBe(true);
    expect(disableOf(uproject)).toBeUndefined();
    expect((await call(d, "GET", "/v1/tools/nope")).status).toBe(405);
  });
});
