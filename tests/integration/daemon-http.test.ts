/**
 * A spawned daemon's HTTP surface: who may talk to it, the event stream, flows,
 * bridge updates and plugin management, each driven over real loopback HTTP
 * with every outside service a local fake.
 */
import * as fs from "node:fs";
import * as http from "node:http";
import * as path from "node:path";
import WebSocket from "ws";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { FakeHttp, McpServer, PACKAGE_VERSION, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, discoveryFile, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";
import { placeInstalled, pluginsYml, writeFakeNpmScript, writePluginPackage } from "./plugin-fixtures.js";

const PLATFORM = process.platform === "win32" ? "Win64" : process.platform === "darwin" ? "Mac" : "Linux";

let sb: Sandbox;
let fake: FakeHttp;
let project: string;
let projectDir: string;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox();
  // The npm registry, the plugin registry and the binaries host, all one local fake.
  fake = await FakeHttp.start((req) => {
    if (req.url === "/ue-mcp") return { json: { "dist-tags": { latest: "99.0.0", beta: "99.1.0-beta.1" } } };
    if (req.url === `/v99.0.0/UE_MCP_Bridge-99.0.0-manifest.json`) {
      return { json: { assets: [{ file: `UE_MCP_Bridge-99.0.0-UE5.6-${PLATFORM}.zip`, sha256: "0".repeat(64) }] } };
    }
    if (req.url === "/api/plugins") return { json: { plugins: [{ slug: "catalogued", name: "Catalogued", packageName: "ue-mcp-catalogued" }] } };
    return { status: 404, json: { error: "not here" } };
  });

  project = sb.makeProject("Api");
  projectDir = path.dirname(project);
  fs.writeFileSync(path.join(projectDir, "flow-touch.js"), `require("fs").writeFileSync(require("path").join(__dirname, "flow-ran.txt"), "ran " + process.argv[2]);\n`);
  const flows = [
    "flows:",
    "  integ_touch:",
    "    description: Write a marker file in the project",
    "    steps:",
    "      1:",
    "        task: shell",
    "        options:",
    `          command: node flow-touch.js once`,
    `          cwd: ${projectDir.replace(/\\/g, "/")}`,
  ].join("\n");
  fs.writeFileSync(path.join(projectDir, "ue-mcp.yml"), pluginsYml(["ue-mcp-pre", "ue-mcp-missing"], flows));
  placeInstalled(projectDir, writePluginPackage(sb.path("pkgs", "pre"), { name: "ue-mcp-pre", version: "1.0.0", manifest: "actionPrefix: pre\n" }));
  writePluginPackage(sb.path("pkgs", "new"), { name: "ue-mcp-new", version: "2.0.0", manifest: "actionPrefix: newp\n" });

  // A binary bridge install at 1.0.0, and a 99.0.0 build of it on disk to update to.
  const pluginDir = path.join(projectDir, "Plugins", "UE_MCP_Bridge");
  fs.mkdirSync(path.join(pluginDir, "Binaries", PLATFORM), { recursive: true });
  fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: "1.0.0" }));
  fs.writeFileSync(path.join(pluginDir, "Binaries", PLATFORM, "old.bin"), "1.0.0");
  fs.writeFileSync(path.join(pluginDir, ".ue-mcp-install.json"), JSON.stringify({ kind: "binary", engine: "5.6", platform: PLATFORM, version: "1.0.0" }));
  const build = sb.path("build99", "UE_MCP_Bridge");
  fs.mkdirSync(path.join(build, "Binaries", PLATFORM), { recursive: true });
  fs.writeFileSync(path.join(build, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: "99.0.0" }));
  fs.writeFileSync(path.join(build, "Binaries", PLATFORM, "new.bin"), "99.0.0");

  d = await startDaemonCli(sb, project, {
    UE_MCP_NPM_REGISTRY: fake.origin,
    UE_MCP_REGISTRY: fake.origin,
    UE_MCP_BINARIES: fake.origin,
    NPM_FAKE_SCRIPT: writeFakeNpmScript(sb, { versions: { "ue-mcp-new": { "2.0.0": sb.path("pkgs", "new") } } }),
  });
});

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await fake?.stop();
  sb.cleanup();
});

/** A raw request, so the Host and Origin headers are the test's to set. */
function rawGet(port: number, route: string, headers: Record<string, string>): Promise<number> {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: "127.0.0.1", port, path: route, method: "GET", headers }, (res) => {
      res.resume();
      resolve(res.statusCode ?? 0);
    });
    req.on("error", reject);
    req.end();
  });
}

describe("discovery and auth", () => {
  it("publishes where it listens and refuses anyone without the token or from another origin", async () => {
    covers("daemon.discovery-auth");
    expect(d).toMatchObject({ version: PACKAGE_VERSION, apiVersion: 1 });
    expect(d.token).toMatch(/^[0-9a-f]{64}$/);
    if (process.platform !== "win32") expect(fs.statSync(discoveryFile(sb, project)).mode & 0o777).toBe(0o600);

    const health = await call(d, "GET", "/v1/health");
    expect(health.status).toBe(200);
    expect(health.body).toMatchObject({ ok: true, pid: d.pid, version: PACKAGE_VERSION, clients: 0 });

    expect((await fetch(`${base(d)}/v1/health`)).status).toBe(401);
    expect((await fetch(`${base(d)}/v1/health`, { headers: { authorization: `Bearer ${"f".repeat(64)}` } })).status).toBe(401);
    // Only the event stream takes the token as a query, because EventSource cannot set headers.
    expect((await fetch(`${base(d)}/v1/health?token=${d.token}`)).status).toBe(401);
    const stream = await fetch(`${base(d)}/v1/events?token=${d.token}`);
    expect(stream.status).toBe(200);
    await stream.body?.cancel();

    const auth = bearer(d);
    expect(await rawGet(d.port, "/v1/health", auth)).toBe(200);
    expect(await rawGet(d.port, "/v1/health", { ...auth, origin: `http://localhost:${d.port}` })).toBe(200);
    expect(await rawGet(d.port, "/v1/health", { ...auth, origin: "http://evil.example" })).toBe(401);
    expect(await rawGet(d.port, "/v1/health", { ...auth, host: "evil.example" })).toBe(401);
    expect(await rawGet(d.port, "/v1/health", { ...auth, host: `rebind.example:${d.port}` })).toBe(401);

    // The MCP socket takes the same token.
    const refused = await new Promise<string>((resolve) => {
      const ws = new WebSocket(`ws://127.0.0.1:${d.port}/v1/mcp`);
      ws.on("open", () => resolve("open"));
      ws.on("unexpected-response", (_req, res) => resolve(String(res.statusCode)));
      ws.on("error", () => resolve("error"));
    });
    expect(refused).toBe("401");
  });
});

describe("events", () => {
  it("streams an MCP client's session and calls, and resumes from an id", async () => {
    covers("daemon.events");
    const before = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const server = await McpServer.start(sb, { projects: [project], mode: "daemon" });
    const result = await server.call("tools", { action: "call", category: "project", method: "get_status" });
    expect(result.isError).toBe(false);
    await server.client.close();

    const events = await readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(before) }, (es) => es.some((e) => e.type === "client.disconnected"));
    const types = events.map((e) => e.type);
    expect(types).toEqual(expect.arrayContaining(["client.connected", "call.started", "call.finished", "client.disconnected"]));
    expect(events.every((e) => Number(e.id) > before)).toBe(true);
    expect(events.find((e) => e.type === "client.connected")!.data).toMatchObject({ client: "ue-mcp-integration", clients: 1 });
    const finished = events.find((e) => e.type === "call.finished")!.data;
    expect(finished).toMatchObject({ client: "ue-mcp-integration", ok: true });
    expect(typeof finished.durationMs).toBe("number");
    expect(events.find((e) => e.type === "client.disconnected")!.data).toMatchObject({ clients: 0 });
  });

  it("reports an editor attaching and going away", async () => {
    covers("daemon.events");
    const live = sb.makeProject("Live");
    const bridge = await FakeBridge.start({ projectDir: path.dirname(live) });
    let stopped = false;
    try {
      const ld = await startDaemonCli(sb, live, { UE_MCP_PORT: undefined });
      const connected = await readSse(`${base(ld)}/v1/events?since=0`, bearer(ld), (es) => es.some((e) => e.type === "editor.connected"));
      expect(connected.find((e) => e.type === "editor.connected")!.data).toMatchObject({ editor: "Live", port: bridge.port });
      await waitFor(async () => (await call(ld, "GET", "/v1/health")).body.editors[0]?.connected === true, 10_000, "health to show the editor");

      const last = (await call(ld, "GET", "/v1/health")).body.lastEventId as number;
      const gone = readSse(`${base(ld)}/v1/events`, { ...bearer(ld), "last-event-id": String(last) }, (es) => es.some((e) => e.type === "editor.disconnected"));
      await bridge.stop();
      stopped = true;
      const ev = (await gone).find((e) => e.type === "editor.disconnected")!;
      expect(ev.data).toMatchObject({ editor: "Live" });
      expect(typeof ev.data.cause).toBe("string");
      expect((await call(ld, "GET", "/v1/health")).body.editors[0]).toMatchObject({ connected: false, lastDisconnect: { cause: ev.data.cause } });
    } finally {
      if (!stopped) await bridge.stop();
    }
  });
});

describe("flows", () => {
  it("lists, plans and runs a project flow, streaming its progress", async () => {
    covers("daemon.flows");
    const listed = await call(d, "GET", "/v1/flows");
    expect(listed.status, listed.text).toBe(200);
    expect(listed.text).toContain("integ_touch");

    const plan = await call(d, "GET", "/v1/flows/integ_touch/plan");
    expect(plan.status, plan.text).toBe(200);
    expect(plan.text).toContain("shell");
    expect(fs.existsSync(path.join(projectDir, "flow-ran.txt"))).toBe(false);

    const progress = readSse(`${base(d)}/v1/flows/events`, bearer(d), (es) => es.some((e) => /completed|finished|failed/.test(e.type) && e.data?.flowName === "integ_touch" && !e.data?.stepId && !e.data?.step));
    await new Promise((r) => setTimeout(r, 300));
    const run = await call(d, "POST", "/v1/flows/integ_touch/run", {});
    expect(run.status, run.text).toBe(200);
    expect(run.text).not.toMatch(/"success":\s*false/);
    expect(fs.readFileSync(path.join(projectDir, "flow-ran.txt"), "utf8")).toBe("ran once");
    const seen = await progress;
    expect(seen.length).toBeGreaterThan(0);
    expect(seen.every((e) => e.data?.flowName === "integ_touch")).toBe(true);

    const unknown = await call(d, "POST", "/v1/flows/no_such_flow/run", {});
    expect(unknown.text).toMatch(/no_such_flow/);
    expect(unknown.text).not.toMatch(/"success":\s*true/);
  });
});

describe("bridge updates", () => {
  it("reports the newest release for the install's channel from the registry", async () => {
    covers("daemon.update");
    const check = await call(d, "GET", "/v1/update");
    expect(check.status).toBe(200);
    expect(check.body).toMatchObject({
      installed: { kind: "binary", version: "1.0.0", engine: "5.6", platform: PLATFORM },
      channel: "latest",
      latest: "99.0.0",
      target: "99.0.0",
      binaries: { available: true, file: `UE_MCP_Bridge-99.0.0-UE5.6-${PLATFORM}.zip` },
    });
    expect(check.body.error).toBeUndefined();
    expect(fake.requests.map((r) => r.url)).toEqual(expect.arrayContaining(["/ue-mcp", "/v99.0.0/UE_MCP_Bridge-99.0.0-manifest.json"]));
  });

  it("follows the channel the caller names, and refuses one that is not a dist-tag", async () => {
    covers("daemon.update");
    const beta = await call(d, "GET", "/v1/update?channel=beta");
    expect(beta.status).toBe(200);
    expect(beta.body).toMatchObject({ channel: "beta", latest: "99.1.0-beta.1", target: "99.1.0-beta.1" });
    expect((await call(d, "GET", "/v1/update?channel=..%2Fx")).status).toBe(400);
  });

  it("applies an update from given binaries while no editor runs, and says so on the stream", async () => {
    covers("daemon.update");
    const last = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const applied = await call(d, "POST", "/v1/update/apply", { version: "99.0.0", binaries: sb.path("build99") });
    expect(applied.status, applied.text).toBe(200);
    expect(applied.body).toMatchObject({ ok: true, kind: "binary", version: "99.0.0", state: "applied" });
    const pluginDir = path.join(projectDir, "Plugins", "UE_MCP_Bridge");
    expect(fs.readFileSync(path.join(pluginDir, "Binaries", PLATFORM, "new.bin"), "utf8")).toBe("99.0.0");
    expect(fs.existsSync(path.join(pluginDir, "Binaries", PLATFORM, "old.bin"))).toBe(false);
    expect(JSON.parse(fs.readFileSync(path.join(pluginDir, ".ue-mcp-install.json"), "utf8"))).toMatchObject({ kind: "binary", version: "99.0.0" });
    const events = await readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(last) }, (es) => es.some((e) => e.type === "update.applied"));
    expect(events.find((e) => e.type === "update.applied")!.data).toMatchObject({ version: "99.0.0", state: "applied" });

    const bad = await call(d, "POST", "/v1/update/apply", { version: "100.0.0", binaries: sb.path("nothing-here") });
    expect(bad.status).toBe(500);
    expect(bad.body).toMatchObject({ ok: false });
    expect(fs.readFileSync(path.join(pluginDir, "Binaries", PLATFORM, "new.bin"), "utf8")).toBe("99.0.0");
  });
});

describe("plugins", () => {
  it("lists the project's plugins and the registry catalog", async () => {
    covers("daemon.plugin-routes");
    const listed = await call(d, "GET", "/v1/plugins");
    expect(listed.status).toBe(200);
    const byName = Object.fromEntries((listed.body.plugins as Array<{ name: string }>).map((p) => [p.name, p]));
    expect(byName["ue-mcp-pre"]).toMatchObject({ version: "1.0.0", status: "ok" });
    expect(byName["ue-mcp-missing"]).toMatchObject({ version: null, status: "missing" });

    const catalog = await call(d, "GET", "/v1/plugins/catalog");
    expect(catalog.status).toBe(200);
    expect(catalog.body.plugins).toEqual([{ slug: "catalogued", name: "Catalogued", packageName: "ue-mcp-catalogued" }]);
  });

  it("installs and uninstalls a plugin through the CLI path and announces the change", async () => {
    covers("daemon.plugin-routes");
    expect((await call(d, "POST", "/v1/plugins/install", { name: "../escape" })).status).toBe(400);
    expect((await call(d, "POST", "/v1/plugins/install", { name: "ue-mcp-new", version: "1; rm -rf /" })).status).toBe(400);

    const last = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const installed = await call(d, "POST", "/v1/plugins/install", { name: "ue-mcp-new" });
    expect(installed.status, installed.text).toBe(200);
    expect(installed.body).toMatchObject({ ok: true, exitCode: 0, restartRequired: true });
    expect(fs.readFileSync(path.join(projectDir, "ue-mcp.yml"), "utf8")).toContain("ue-mcp-new");
    expect(fs.existsSync(path.join(projectDir, "node_modules", "ue-mcp-new", "package.json"))).toBe(true);
    expect(sb.npmCalls().some((c) => c.args[0] === "install" && c.args.includes("ue-mcp-new"))).toBe(true);
    const listed = await call(d, "GET", "/v1/plugins");
    expect((listed.body.plugins as Array<{ name: string; status: string }>).find((p) => p.name === "ue-mcp-new")).toMatchObject({ status: "ok", version: "2.0.0" });

    const removed = await call(d, "POST", "/v1/plugins/uninstall", { name: "ue-mcp-new" });
    expect(removed.status, removed.text).toBe(200);
    expect(fs.readFileSync(path.join(projectDir, "ue-mcp.yml"), "utf8")).not.toContain("ue-mcp-new");
    expect(fs.existsSync(path.join(projectDir, "node_modules", "ue-mcp-new"))).toBe(false);

    const events = await readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(last) }, (es) => es.filter((e) => e.type === "plugins.changed").length >= 2);
    expect(events.filter((e) => e.type === "plugins.changed")).toHaveLength(2);
  });
});
