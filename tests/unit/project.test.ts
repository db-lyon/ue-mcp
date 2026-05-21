import { describe, it, expect, beforeEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import yaml from "js-yaml";
import { ProjectContext } from "../../src/project.js";

function makeTempProject(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-project-test-"));
  const uproject = path.join(dir, "Test.uproject");
  fs.writeFileSync(
    uproject,
    JSON.stringify({ FileVersion: 3, EngineAssociation: "5.7", Plugins: [] }, null, 2),
  );
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  return uproject;
}

describe("ProjectContext.resolveContentPath", () => {
  let ctx: ProjectContext;
  let uproject: string;

  beforeEach(() => {
    uproject = makeTempProject();
    ctx = new ProjectContext();
    ctx.setProject(uproject);
  });

  it("appends .uasset to game paths without an extension", () => {
    const out = ctx.resolveContentPath("/Game/MyAsset");
    expect(out.endsWith("MyAsset.uasset")).toBe(true);
  });

  it("preserves .umap extension", () => {
    const out = ctx.resolveContentPath("/Game/MyLevel.umap");
    expect(out.endsWith("MyLevel.umap")).toBe(true);
    expect(out.endsWith(".uasset")).toBe(false);
  });

  it("treats a trailing slash as a directory (no .uasset suffix)", () => {
    const out = ctx.resolveContentPath("/Game/MyFolder/");
    expect(out.endsWith(".uasset")).toBe(false);
    expect(out.endsWith("MyFolder")).toBe(true);
  });

  it("treats a trailing backslash as a directory", () => {
    const out = ctx.resolveContentPath("/Game/MyFolder\\");
    expect(out.endsWith(".uasset")).toBe(false);
  });
});

describe("ProjectContext config loading", () => {
  it("ignores a malformed ue-mcp.yml without throwing", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    fs.writeFileSync(path.join(projectDir, "ue-mcp.yml"), "this: is: not: valid yaml: at all:");

    const ctx = new ProjectContext();
    expect(() => ctx.setProject(uproject)).not.toThrow();
    expect(ctx.config).toEqual({});
  });

  it("loads a valid ue-mcp.yml", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    fs.writeFileSync(
      path.join(projectDir, "ue-mcp.yml"),
      yaml.dump({
        "ue-mcp": {
          version: 1,
          disable: ["gas"],
          http: { enabled: true, port: 7723 },
        },
      }),
    );

    const ctx = new ProjectContext();
    ctx.setProject(uproject);
    expect(ctx.config.disable).toEqual(["gas"]);
    expect(ctx.config.http?.port).toBe(7723);
  });

  it("migrates a 1.0.29-era ue-mcp.local.yml into ~/.ue-mcp/state.json", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    const userState = path.join(projectDir, "user-state.json");
    process.env.UE_MCP_USER_STATE = userState;
    try {
      fs.writeFileSync(
        path.join(projectDir, "ue-mcp.yml"),
        yaml.dump({ "ue-mcp": { version: 1, disable: ["gas"] } }),
      );
      fs.writeFileSync(
        path.join(projectDir, "ue-mcp.local.yml"),
        yaml.dump({
          "ue-mcp": {
            installedHooks: ["C:/Users/test/.claude/settings.json"],
          },
        }),
      );

      const ctx = new ProjectContext();
      ctx.setProject(uproject);
      expect(ctx.config.disable).toEqual(["gas"]);
      // ue-mcp.local.yml is gone.
      expect(fs.existsSync(path.join(projectDir, "ue-mcp.local.yml"))).toBe(false);
      // Hooks landed in user state.
      const state = JSON.parse(fs.readFileSync(userState, "utf-8")) as {
        projects: Record<string, { installedHooks: string[] }>;
      };
      expect(Object.values(state.projects)[0].installedHooks).toEqual([
        "C:/Users/test/.claude/settings.json",
      ]);
    } finally {
      delete process.env.UE_MCP_USER_STATE;
    }
  });

  it("rejects a ue-mcp.yml with wrong types in the ue-mcp: block", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    fs.writeFileSync(
      path.join(projectDir, "ue-mcp.yml"),
      yaml.dump({ "ue-mcp": { version: 1, disable: "gas" } }),
    );

    const ctx = new ProjectContext();
    ctx.setProject(uproject);
    expect(ctx.config).toEqual({});
  });

  it("migrates a pre-1.0.29 .ue-mcp.json into ue-mcp.yml + ~/.ue-mcp/state.json", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    const userState = path.join(projectDir, "user-state.json");
    process.env.UE_MCP_USER_STATE = userState;
    try {
      const jsonPath = path.join(projectDir, ".ue-mcp.json");
      fs.writeFileSync(
        jsonPath,
        JSON.stringify({
          contentRoots: ["/Game/", "/MyPlugin/"],
          disable: ["gas"],
          installedHooks: ["C:/some/settings.json"],
          feedback: { mode: "defer" },
        }),
      );

      const ctx = new ProjectContext();
      ctx.setProject(uproject);

      // Legacy JSON file is gone.
      expect(fs.existsSync(jsonPath)).toBe(false);

      // Tracked fields moved to ue-mcp.yml.
      const yml = yaml.load(
        fs.readFileSync(path.join(projectDir, "ue-mcp.yml"), "utf-8"),
      ) as { "ue-mcp": Record<string, unknown> };
      expect(yml["ue-mcp"].disable).toEqual(["gas"]);
      expect(yml["ue-mcp"].contentRoots).toEqual(["/Game/", "/MyPlugin/"]);
      expect(yml["ue-mcp"].feedback).toEqual({ mode: "defer" });
      expect(yml["ue-mcp"].installedHooks).toBeUndefined();

      // installedHooks moved to ~/.ue-mcp/state.json under this project's key.
      const state = JSON.parse(fs.readFileSync(userState, "utf-8")) as {
        projects: Record<string, { installedHooks: string[] }>;
      };
      expect(Object.values(state.projects)[0].installedHooks).toEqual([
        "C:/some/settings.json",
      ]);

      // The config that the context exposes is project-tracked only.
      // installedHooks is no longer in the project schema.
      expect(ctx.config.disable).toEqual(["gas"]);
      expect(ctx.config.feedback?.mode).toBe("defer");
      // No installedHooks field on UeMcpConfig anymore.
      expect((ctx.config as { installedHooks?: unknown }).installedHooks).toBeUndefined();
    } finally {
      delete process.env.UE_MCP_USER_STATE;
    }
  });
});
