/** #1271: a scaffolded plugin must install against prerelease servers. */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import yaml from "js-yaml";
import { writeScaffold } from "../../src/extensions/plugin-scaffold.js";
import { loadManifest } from "../../src/extensions/manifest.js";
import { initialize, isServerSpecifier, resolve } from "../../src/extensions/self-resolve-hooks.js";
import { packageVersion } from "../../src/core/package-root.js";

describe("the scaffolded package.json", () => {
  it("declares no peer range on ue-mcp, and gates the server with minServerVersion", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-scaffold-pkg-"));
    try {
      writeScaffold(dir, "ue-mcp-probe", "probe");
      const pkg = JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf8")) as Record<string, Record<string, string> | undefined>;
      expect(pkg.peerDependencies).toBeUndefined();
      expect(pkg.dependencies?.["ue-mcp"]).toBeUndefined();
      expect(pkg.devDependencies?.["ue-mcp"]).toBe(`^${packageVersion()}`);
      const manifest = yaml.load(fs.readFileSync(path.join(dir, "ue-mcp.plugin.yml"), "utf8")) as { minServerVersion?: string };
      expect(String(manifest.minServerVersion)).toBe(packageVersion());
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe("the server answers a plugin's ue-mcp imports", () => {
  it("matches ue-mcp and its subpaths only", () => {
    expect(isServerSpecifier("ue-mcp")).toBe(true);
    expect(isServerSpecifier("ue-mcp/task")).toBe(true);
    expect(isServerSpecifier("ue-mcp-other")).toBe(false);
    expect(isServerSpecifier("@x/ue-mcp")).toBe(false);
  });

  it("resolves ue-mcp from the server package and leaves everything else alone", async () => {
    const seen: Array<string | undefined> = [];
    const next = async (_s: string, ctx?: { parentURL?: string }) => {
      seen.push(ctx?.parentURL);
      return {};
    };
    initialize({ selfUrl: "file:///srv/ue-mcp/package.json" });
    await resolve("ue-mcp/task", { parentURL: "file:///proj/node_modules/plug/dist/t.js" }, next);
    await resolve("js-yaml", { parentURL: "file:///proj/node_modules/plug/dist/t.js" }, next);
    expect(seen).toEqual(["file:///srv/ue-mcp/package.json", "file:///proj/node_modules/plug/dist/t.js"]);
    initialize(undefined);
  });
});

describe("the scaffolded knowledge (#1631)", () => {
  it("declares every file under knowledge/ in the manifest, so the loader reads it", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-scaffold-knowledge-"));
    try {
      writeScaffold(dir, "ue-mcp-probe", "probe");
      const { manifest, dropped } = loadManifest(dir);
      expect(dropped).toEqual([]);
      const written = fs.readdirSync(path.join(dir, "knowledge")).map((f) => `knowledge/${f}`).sort();
      expect(Object.values(manifest.knowledge).sort()).toEqual(written);
      expect(manifest.knowledge.project).toBe("knowledge/project.md");
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});
