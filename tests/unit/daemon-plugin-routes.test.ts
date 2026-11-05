import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, expect, it, vi } from "vitest";
import { listInstalledPlugins, pluginRoutes, type PluginCommandRunner } from "../../src/daemon/plugin-routes.js";

function project(yml: string): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-plugin-routes-"));
  fs.writeFileSync(path.join(dir, "ue-mcp.yml"), yml);
  return dir;
}

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

  it("runs install, update and uninstall through the CLI and reports that a restart is needed", async () => {
    const calls: string[][] = [];
    const run: PluginCommandRunner = async (args) => {
      calls.push(args);
      return { exitCode: 0, output: "done\n" };
    };
    const onChanged = vi.fn();
    const routes = pluginRoutes({ projectDir: project(""), run, onChanged });
    expect(await routes("POST", "/install", { name: "sample-recorder", version: "^1.0.0" })).toEqual({
      status: 200, body: { ok: true, exitCode: 0, output: "done", restartRequired: true },
    });
    await routes("POST", "/update", {});
    await routes("POST", "/uninstall", { name: "@scope/ue-mcp-x" });
    expect(calls).toEqual([["install", "sample-recorder", "--version", "^1.0.0"], ["update"], ["uninstall", "@scope/ue-mcp-x"]]);
    expect(onChanged).toHaveBeenCalledTimes(3);
  });

  it("reports a failed command with its output, and changes nothing", async () => {
    const onChanged = vi.fn();
    const routes = pluginRoutes({ projectDir: project(""), run: async () => ({ exitCode: 1, output: "no such package" }), onChanged });
    expect(await routes("POST", "/install", { name: "nope" })).toEqual({
      status: 422, body: { ok: false, exitCode: 1, output: "no such package", restartRequired: false },
    });
    expect(onChanged).not.toHaveBeenCalled();
  });

  it("refuses names and versions that could be read as options, and one change while another runs", async () => {
    let release!: () => void;
    const run: PluginCommandRunner = () => new Promise((r) => (release = () => r({ exitCode: 0, output: "" })));
    const routes = pluginRoutes({ projectDir: project(""), run });
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

  it("answers 502 when the registry cannot be reached, and null for other paths", async () => {
    const routes = pluginRoutes({ projectDir: project(""), fetchImpl: (async () => { throw new Error("offline"); }) as unknown as typeof fetch });
    expect((await routes("GET", "/catalog", undefined))?.status).toBe(502);
    expect(await routes("GET", "/nope", undefined)).toBeNull();
  });
});
