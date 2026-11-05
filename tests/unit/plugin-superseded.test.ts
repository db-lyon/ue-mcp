/** pie-studio ships in core as the `pie` tool: the loader skips it, install refuses it, the catalog hides it. */
import { describe, it, expect, beforeAll, afterAll, afterEach, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { loadPlugins } from "../../src/extensions/loader.js";
import type { ToolDef } from "../../src/core/types.js";
import { categoryTool } from "../../src/surface/category-tool.js";
import { SUPERSEDED_PLUGINS, supersededMessage } from "../../src/extensions/superseded.js";
import { fetchRegistryCatalog } from "../../src/extensions/registry-catalog.js";
import { run as runPluginCli } from "../../src/cli/plugin-cli.js";

const PIE_MANIFEST = `
actionPrefix: pie
nativeModule:
  uePluginName: PIE_Studio
  minBridgeApi: 1
  source: ue/Plugins/PIE_Studio
  category: pie
  handlers:
    record_arm:
      description: "Arm the recorder"
`;

const MESSAGE =
  "pie-studio is built into ue-mcp since 1.4.11 as the `pie` tool; this plugin was not loaded. " +
  "Remove it with `ue-mcp plugin remove pie-studio`, which also deletes Plugins/PIE_Studio from the project.";

let projectDir: string;

function writePackage(npmName: string, manifest: string): void {
  const pkgDir = path.join(projectDir, "node_modules", npmName);
  fs.mkdirSync(pkgDir, { recursive: true });
  fs.writeFileSync(path.join(pkgDir, "package.json"), JSON.stringify({ name: npmName, version: "0.6.3" }));
  fs.writeFileSync(path.join(pkgDir, "ue-mcp.plugin.yml"), manifest);
}

beforeAll(() => {
  projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-superseded-"));
  writePackage("pie-studio", PIE_MANIFEST);
  // A renamed copy that still ships the PIE_Studio native module.
  writePackage("my-pie-fork", PIE_MANIFEST);
});

afterAll(() => {
  fs.rmSync(projectDir, { recursive: true, force: true });
});

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.unstubAllEnvs();
});

// Stands in for the core `pie` tool the plugin would have collided with.
const coreTools = (): ToolDef[] => [
  categoryTool("pie", "PIE testing", {
    record_arm: { kind: "handler", effect: "read", description: "core", handler: async () => ({ core: true }) },
  }),
];

describe("superseded plugin list", () => {
  it("names pie-studio, its core tool and its native folder", () => {
    expect(SUPERSEDED_PLUGINS["pie-studio"]).toEqual({ builtIn: "pie", since: "1.4.11", uePluginName: "PIE_Studio" });
    expect(supersededMessage("pie-studio", SUPERSEDED_PLUGINS["pie-studio"])).toBe(MESSAGE);
  });
});

describe("loader", () => {
  it("skips pie-studio by package name, warns once, and injects nothing", async () => {
    const err = vi.spyOn(console, "error").mockImplementation(() => {});
    const tools = coreTools();
    const load = await loadPlugins(tools, [{ name: "pie-studio" }], projectDir, "1.4.11");

    const rec = load.records[0];
    expect(rec.status).toBe("skipped");
    expect(rec.statusReason).toBe(MESSAGE);
    expect(rec.provided).toEqual({});
    expect(rec.injected).toEqual({});
    expect(load.taskRegistrations).toEqual([]);
    expect(load.tools.filter((t) => t.name === "pie")).toHaveLength(1);
    expect(load.tools.find((t) => t.name === "pie")).toBe(tools[0]);

    const warnings = err.mock.calls.map((c) => String(c[0])).filter((l) => l.includes("pie-studio is built into"));
    expect(warnings).toHaveLength(1);
  });

  it("skips a package under another name that ships the PIE_Studio native module", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const load = await loadPlugins(coreTools(), [{ name: "my-pie-fork" }], projectDir, "1.4.11");
    expect(load.records[0].status).toBe("skipped");
    expect(load.records[0].statusReason).toBe(MESSAGE);
    expect(load.records[0].provided).toEqual({});
    expect(load.tools.filter((t) => t.name === "pie")).toHaveLength(1);
  });

  it("skips pie-studio even when it is not installed", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const empty = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-superseded-empty-"));
    try {
      const load = await loadPlugins(coreTools(), [{ name: "pie-studio" }], empty, "1.4.11");
      expect(load.records[0].statusReason).toBe(MESSAGE);
    } finally {
      fs.rmSync(empty, { recursive: true, force: true });
    }
  });
});

describe("plugin install", () => {
  it("refuses pie-studio before npm runs, with a non-zero exit", async () => {
    const errors: string[] = [];
    vi.spyOn(console, "error").mockImplementation((m: unknown) => { errors.push(String(m)); });
    vi.spyOn(console, "log").mockImplementation(() => {});
    const exit = vi.spyOn(process, "exit").mockImplementation(((code?: number) => {
      throw new Error(`exit ${code}`);
    }) as never);
    const fetchSpy = vi.fn();
    vi.stubGlobal("fetch", fetchSpy);

    await expect(runPluginCli(["install", "pie-studio"])).rejects.toThrow("exit 1");
    expect(exit).toHaveBeenCalledWith(1);
    expect(errors.join("\n")).toContain(
      "pie-studio is built into ue-mcp since 1.4.11 as the `pie` tool; this plugin was not installed.",
    );
    expect(errors.join("\n")).toContain("ue-mcp plugin remove pie-studio");
    expect(fetchSpy).not.toHaveBeenCalled();
  });
});

describe("registry catalog", () => {
  const rows = [
    { slug: "pie-studio", name: "PIE Studio", packageName: "pie-studio", status: "published" },
    { slug: "renamed", name: "Renamed", packageName: "pie-studio", status: "published" },
    { slug: "meshy", name: "Meshy", packageName: "ue-mcp-meshy", status: "published" },
  ];

  it("drops superseded plugins from the fetched list and the disk cache", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-superseded-cache-"));
    const cache = path.join(dir, "catalog.json");
    try {
      vi.stubEnv("UE_MCP_REGISTRY", "http://registry.invalid/a");
      vi.stubEnv("UE_MCP_REGISTRY_CACHE", cache);
      vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({ plugins: rows }))));

      const plugins = await fetchRegistryCatalog({ force: true });
      expect(plugins.map((p) => p.slug)).toEqual(["meshy"]);
      const written = JSON.parse(fs.readFileSync(cache, "utf-8")) as { plugins: Array<{ slug: string }> };
      expect(written.plugins.map((p) => p.slug)).toEqual(["meshy"]);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it("drops superseded plugins from a cache written before the filter existed", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-superseded-cache-"));
    const cache = path.join(dir, "catalog.json");
    try {
      vi.stubEnv("UE_MCP_REGISTRY", "http://registry.invalid/b");
      vi.stubEnv("UE_MCP_REGISTRY_CACHE", cache);
      fs.writeFileSync(cache, JSON.stringify({ fetchedAt: "x", base: "http://registry.invalid/b", plugins: rows }));
      vi.stubGlobal("fetch", vi.fn(async () => { throw new Error("offline"); }));

      const plugins = await fetchRegistryCatalog({ force: true });
      expect(plugins.map((p) => p.slug)).toEqual(["meshy"]);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});
