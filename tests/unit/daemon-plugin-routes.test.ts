import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { listInstalledPlugins, pluginRoutes, type PluginOps } from "../../src/daemon/plugin-routes.js";
import { PluginOpError } from "../../src/extensions/plugin-install.js";
import { resetRegistryCatalogMemory } from "../../src/extensions/registry-catalog.js";

function project(yml: string): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-plugin-routes-"));
  fs.writeFileSync(path.join(dir, "ue-mcp.yml"), yml);
  return dir;
}

/** Fake plugin changes that record what they were asked and log one line. */
function fakeOps(calls: unknown[][], outcome: () => Promise<void> = async () => {}): PluginOps {
  return {
    install: async (_dir, name, version, log) => { calls.push(["install", name, version]); log.note("done"); await outcome(); },
    uninstall: async (_dir, name, log) => { calls.push(["uninstall", name]); log.note("done"); await outcome(); },
    update: async (_dir, name, log) => { calls.push(["update", name]); log.note("done"); await outcome(); },
  };
}

let savedCache: string | undefined;
beforeEach(() => {
  savedCache = process.env.UE_MCP_REGISTRY_CACHE;
  process.env.UE_MCP_REGISTRY_CACHE = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-catalog-")), "catalog.json");
  resetRegistryCatalogMemory();
});
afterEach(() => {
  if (savedCache === undefined) delete process.env.UE_MCP_REGISTRY_CACHE;
  else process.env.UE_MCP_REGISTRY_CACHE = savedCache;
  resetRegistryCatalogMemory();
});

describe("the daemon's plugin routes", () => {
  it("lists the declared plugins, saying which are missing or have no valid manifest", () => {
    const dir = project("plugins:\n  - name: ue-mcp-absent\n  - name: ue-mcp-broken\n    version: 1.2.0\n");
    const pkg = path.join(dir, "node_modules", "ue-mcp-broken");
    fs.mkdirSync(pkg, { recursive: true });
    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "ue-mcp-broken", version: "1.2.0" }));
    const list = listInstalledPlugins(dir);
    expect(list.map((p) => [p.name, p.status, p.version, p.pinned])).toEqual([
      ["ue-mcp-absent", "missing", null, null],
      ["ue-mcp-broken", "invalid", "1.2.0", "1.2.0"],
    ]);
    expect(list[1].problems[0]).toBeTruthy();
  });

  it("runs install, update and uninstall through the plugin library and reports that a restart is needed", async () => {
    const calls: unknown[][] = [];
    const onChanged = vi.fn();
    const routes = pluginRoutes({ projectDir: project(""), ops: fakeOps(calls), onChanged });
    expect(await routes("POST", "/install", { name: "sample-recorder", version: "^1.0.0" })).toEqual({
      status: 200, body: { ok: true, exitCode: 0, output: "[ue-mcp plugin] done", restartRequired: true },
    });
    await routes("POST", "/update", {});
    await routes("POST", "/uninstall", { name: "@scope/ue-mcp-x" });
    expect(calls).toEqual([["install", "sample-recorder", "^1.0.0"], ["update", undefined], ["uninstall", "@scope/ue-mcp-x"]]);
    expect(onChanged).toHaveBeenCalledTimes(3);
  });

  it("reports a refused change with what it printed and the refusal, and changes nothing", async () => {
    const onChanged = vi.fn();
    const routes = pluginRoutes({
      projectDir: project(""),
      ops: fakeOps([], async () => { throw new PluginOpError("no such package"); }),
      onChanged,
    });
    expect(await routes("POST", "/install", { name: "nope" })).toEqual({
      status: 422,
      body: { ok: false, exitCode: 1, output: "[ue-mcp plugin] done\n[ue-mcp plugin] ERROR: no such package", restartRequired: false },
    });
    expect(onChanged).not.toHaveBeenCalled();
  });

  it("refuses names and versions that could be read as options, and one change while another runs", async () => {
    let release!: () => void;
    const routes = pluginRoutes({ projectDir: project(""), ops: fakeOps([], () => new Promise<void>((r) => (release = r))) });
    expect((await routes("POST", "/install", { name: "--registry=evil" }))?.status).toBe(400);
    expect((await routes("POST", "/install", { name: "ok", version: "1.0.0 --force" }))?.status).toBe(400);
    const first = routes("POST", "/install", { name: "a" });
    await new Promise((r) => setTimeout(r, 0));
    expect((await routes("POST", "/install", { name: "b" }))?.status).toBe(409);
    release();
    expect((await first)?.status).toBe(200);
  });

  it("reads the registry catalog once and serves it again from memory", async () => {
    const fetchImpl = vi.fn(async () => new Response(JSON.stringify({ plugins: [{ slug: "sample-recorder", name: "Sample Recorder" }] })));
    const routes = pluginRoutes({ projectDir: project(""), fetchImpl: fetchImpl as unknown as typeof fetch });
    expect(await routes("GET", "/catalog", undefined)).toEqual({ status: 200, body: { plugins: [{ slug: "sample-recorder", name: "Sample Recorder" }] } });
    await routes("GET", "/catalog", undefined);
    expect(fetchImpl).toHaveBeenCalledTimes(1);
  });

  it("leaves a plugin that is now built into core out of the catalog", async () => {
    const listed = [{ slug: "pie-studio", name: "PIE Studio" }, { slug: "sample-recorder", name: "Sample Recorder" }];
    const fetchImpl = vi.fn(async () => new Response(JSON.stringify({ plugins: listed })));
    const routes = pluginRoutes({ projectDir: project(""), fetchImpl: fetchImpl as unknown as typeof fetch });
    expect(await routes("GET", "/catalog", undefined)).toEqual({ status: 200, body: { plugins: [{ slug: "sample-recorder", name: "Sample Recorder" }] } });
  });

  it("serves the last catalog this machine saw when the registry is down", async () => {
    const up = vi.fn(async () => new Response(JSON.stringify({ plugins: [{ slug: "sample-recorder", name: "Sample Recorder" }] })));
    await pluginRoutes({ projectDir: project(""), fetchImpl: up as unknown as typeof fetch })("GET", "/catalog", undefined);
    resetRegistryCatalogMemory();
    const down = (async () => new Response("", { status: 503 })) as unknown as typeof fetch;
    expect(await pluginRoutes({ projectDir: project(""), fetchImpl: down })("GET", "/catalog", undefined)).toEqual({
      status: 200, body: { plugins: [{ slug: "sample-recorder", name: "Sample Recorder" }] },
    });
  });

  it("answers 502 with a way forward when the registry cannot be reached and nothing is cached, and null for other paths", async () => {
    const routes = pluginRoutes({ projectDir: project(""), fetchImpl: (async () => { throw new Error("offline"); }) as unknown as typeof fetch });
    const r = await routes("GET", "/catalog", undefined);
    expect(r?.status).toBe(502);
    expect((r?.body as { error: string }).error).toMatch(/offline.*npm package name still works.*UE_MCP_REGISTRY/);
    expect(await routes("GET", "/nope", undefined)).toBeNull();
  });
});
