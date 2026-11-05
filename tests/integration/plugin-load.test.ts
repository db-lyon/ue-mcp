/**
 * Plugins a project declares in ue-mcp.yml, as a client sees them through the
 * real server: injected actions, provided categories, native handlers routed
 * to the bridge, flows, and the plugins the loader refuses with a reason.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox } from "./harness.js";
import { helloTask, placeInstalled, pluginsYml, writePluginPackage } from "./plugin-fixtures.js";

let sb: Sandbox;
let bridge: FakeBridge;
let server: McpServer;

const FIXTURE = "ue-mcp-fixture";
const FUTURE = "ue-mcp-future";

const fixtureManifest = `actionPrefix: fx
inject:
  project:
    hello:
      task: fx_hello
      description: Greets someone from the fixture plugin
      schema:
        who: { type: string, required: true, description: Who to greet }
    sneaky:
      task: fx_hello
      description: Declares a routing key as its own parameter
      schema:
        timeoutMs: { type: number }
provides:
  fixturecat:
    description: A category the fixture plugin owns
    actions:
      greet:
        task: fx_hello
        description: Greets from the provided category
        schema:
          who: { type: string }
nativeModule:
  uePluginName: FixtureNative
  minBridgeApi: 1
  source: ue/Plugins/FixtureNative
  category: fxnative
  categoryDescription: Native handlers of the fixture plugin
  handlers:
    fx_ping:
      description: Pings the native module
      schema:
        value: { type: string }
tasks:
  fx_hello:
    class_path: tasks/hello
flows:
  fixture_say:
    description: Greets through a flow
    steps:
      1:
        task: fx_hello
        options: { who: flow }
`;

beforeAll(async () => {
  sb = new Sandbox();
  const uproject = sb.makeProject("Game", { config: pluginsYml([FIXTURE, FUTURE]) });
  const projectDir = path.dirname(uproject);
  placeInstalled(projectDir, writePluginPackage(sb.path("src", "fixture"), {
    name: FIXTURE,
    version: "1.0.0",
    manifest: fixtureManifest,
    files: {
      "tasks/hello.js": helloTask(`${FIXTURE}@1.0.0`),
      "ue/Plugins/FixtureNative/FixtureNative.uplugin": "{}",
    },
  }));
  placeInstalled(projectDir, writePluginPackage(sb.path("src", "future"), {
    name: FUTURE,
    version: "2.0.0",
    manifest: `actionPrefix: fut\nminServerVersion: 999.0.0\ninject:\n  project:\n    later:\n      task: fut_later\ntasks:\n  fut_later:\n    class_path: tasks/hello\n`,
    files: { "tasks/hello.js": helloTask(`${FUTURE}@2.0.0`) },
  }));
  bridge = await FakeBridge.start({ projectDir, handlers: { fx_ping: (p) => ({ pong: p.value, native: true }) } });
  server = await McpServer.start(sb, { projects: [uproject], strategy: "full", port: null });
}, 60_000);

afterAll(async () => {
  await server?.close();
  await bridge?.stop();
  sb?.cleanup();
});

async function tool(name: string): Promise<{ name: string; description: string; inputSchema: unknown } | undefined> {
  return (await server.listTools()).find((t) => t.name === name);
}

async function plugin(name: string): Promise<Record<string, unknown>> {
  return json(await server.call("plugins", { action: "describe", name }));
}

describe("a loaded plugin", () => {
  it("injects its prefixed action into the target category, and the action runs its task", async () => {
    covers("plugin.load.inject");
    const project = await tool("project");
    expect(JSON.stringify(project?.inputSchema)).toContain("fx_hello");
    const r = await server.call("project", { action: "fx_hello", who: "integration" });
    expect(r.isError, r.text).toBe(false);
    expect(r.text).toContain("hello, integration");
    expect(r.text).toContain(`${FIXTURE}@1.0.0`);
    expect((await plugin(FIXTURE)).injected).toEqual({ project: ["fx_hello"] });
  });

  it("provides its own category as a tool whose actions run", async () => {
    covers("plugin.load.provides");
    const cat = await tool("fixturecat");
    expect(cat?.description).toContain("A category the fixture plugin owns");
    expect(JSON.stringify(cat?.inputSchema)).toContain("greet");
    const r = await server.call("fixturecat", { action: "greet", who: "provided" });
    expect(r.isError, r.text).toBe(false);
    expect(r.text).toContain("hello, provided");
  });

  it("surfaces native handlers as actions that reach the editor bridge", async () => {
    covers("plugin.load.native");
    const cat = await tool("fxnative");
    expect(JSON.stringify(cat?.inputSchema)).toContain("fx_ping");
    const r = await server.call("fxnative", { action: "fx_ping", value: "marco" });
    expect(r.isError, r.text).toBe(false);
    expect(r.text).toContain("marco");
    const call = bridge.calls.find((c) => c.method === "fx_ping");
    expect(call?.params).toMatchObject({ value: "marco" });
  });

  it("drops an action that declares a routing parameter, and says why", async () => {
    covers("plugin.load.reserved-params");
    expect(JSON.stringify((await tool("project"))?.inputSchema)).not.toContain("fx_sneaky");
    const r = await server.call("project", { action: "fx_sneaky" });
    expect(r.isError).toBe(true);
    const degraded = (await plugin(FIXTURE)).degraded as string[];
    expect(degraded.some((d) => d.startsWith("inject.project.sneaky") && d.includes("timeoutMs"))).toBe(true);
  });

  it("lists its flows in flow(list) and runs them", async () => {
    covers("plugin.load.flows");
    const listed = await server.call("flow", { action: "list" });
    expect(listed.text).toContain("fixture_say");
    const run = await server.call("flow", { action: "run", flowName: "fixture_say" });
    expect(run.isError, run.text).toBe(false);
    expect(run.text).toContain("hello, flow");
  });
});

describe("a plugin newer than the server", () => {
  it("is skipped, contributes nothing, and reports the version it needs", async () => {
    covers("plugin.load.version-gate");
    const list = json<{ plugins: Array<{ name: string; status: string; statusReason?: string }>; active: number }>(
      await server.call("plugins", { action: "list" }),
    );
    const future = list.plugins.find((p) => p.name === FUTURE);
    expect(future?.status).toBe("skipped");
    expect(future?.statusReason).toContain("requires server >= 999.0.0");
    expect(list.plugins.find((p) => p.name === FIXTURE)?.status).toBe("active");
    expect(JSON.stringify((await tool("project"))?.inputSchema)).not.toContain("fut_later");
  });
});
