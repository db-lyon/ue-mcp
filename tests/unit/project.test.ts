import { describe, it, expect, beforeEach, afterEach } from "vitest";
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
  let globalCfg: string;
  beforeEach(() => {
    // Isolate the user-global config layer (~/.ue-mcp/config.yml) so tests
    // never read the developer's real machine config. Points at a temp path
    // that tests can write to when they want to exercise the global layer.
    globalCfg = path.join(
      fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-global-")),
      "config.yml",
    );
    process.env.UE_MCP_GLOBAL_CONFIG = globalCfg;
  });
  afterEach(() => {
    delete process.env.UE_MCP_GLOBAL_CONFIG;
  });

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

      // Tracked fields (other than feedback.mode) moved to ue-mcp.yml.
      // feedback.mode is a per-user preference, so it goes to user state.
      const yml = yaml.load(
        fs.readFileSync(path.join(projectDir, "ue-mcp.yml"), "utf-8"),
      ) as { "ue-mcp": Record<string, unknown> };
      expect(yml["ue-mcp"].disable).toEqual(["gas"]);
      expect(yml["ue-mcp"].contentRoots).toEqual(["/Game/", "/MyPlugin/"]);
      expect(yml["ue-mcp"].feedback).toBeUndefined();
      expect(yml["ue-mcp"].installedHooks).toBeUndefined();

      // installedHooks + feedback.mode both moved to ~/.ue-mcp/state.json.
      const state = JSON.parse(fs.readFileSync(userState, "utf-8")) as {
        projects?: Record<string, { installedHooks: string[] }>;
        preferences?: { feedback?: { mode?: string } };
      };
      expect(Object.values(state.projects ?? {})[0].installedHooks).toEqual([
        "C:/some/settings.json",
      ]);
      expect(state.preferences?.feedback?.mode).toBe("defer");

      // The config that the context exposes is project-tracked only.
      // No feedback / installedHooks fields on UeMcpConfig anymore.
      expect(ctx.config.disable).toEqual(["gas"]);
      expect((ctx.config as { feedback?: unknown }).feedback).toBeUndefined();
      expect((ctx.config as { installedHooks?: unknown }).installedHooks).toBeUndefined();
    } finally {
      delete process.env.UE_MCP_USER_STATE;
    }
  });

  it("layers ue-mcp.local.yml over ue-mcp.yml and preserves the file", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    fs.writeFileSync(
      path.join(projectDir, "ue-mcp.yml"),
      yaml.dump({ "ue-mcp": { version: 1, disable: ["gas"], context: { strategy: "full" } } }),
    );
    fs.writeFileSync(
      path.join(projectDir, "ue-mcp.local.yml"),
      yaml.dump({ "ue-mcp": { context: { strategy: "micro" } } }),
    );

    const ctx = new ProjectContext();
    ctx.setProject(uproject);

    // Local layer wins for the key it sets; a project key it doesn't touch survives.
    expect(ctx.config.context?.strategy).toBe("micro");
    expect(ctx.config.disable).toEqual(["gas"]);
    // The override file is a live layer, not a legacy artifact - never deleted.
    expect(fs.existsSync(path.join(projectDir, "ue-mcp.local.yml"))).toBe(true);
  });

  it("strips installedHooks from ue-mcp.local.yml but keeps real overrides", () => {
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
            context: { strategy: "lean" },
          },
        }),
      );

      const ctx = new ProjectContext();
      ctx.setProject(uproject);

      // Machine-state key moved out to user state...
      const state = JSON.parse(fs.readFileSync(userState, "utf-8")) as {
        projects: Record<string, { installedHooks: string[] }>;
      };
      expect(Object.values(state.projects)[0].installedHooks).toEqual([
        "C:/Users/test/.claude/settings.json",
      ]);
      // ...but the file survives because it still holds a real override...
      expect(fs.existsSync(path.join(projectDir, "ue-mcp.local.yml"))).toBe(true);
      const local = yaml.load(
        fs.readFileSync(path.join(projectDir, "ue-mcp.local.yml"), "utf-8"),
      ) as { "ue-mcp": Record<string, unknown> };
      expect((local["ue-mcp"] as { installedHooks?: unknown }).installedHooks).toBeUndefined();
      // ...and that override still applies to the merged config.
      expect(ctx.config.context?.strategy).toBe("lean");
    } finally {
      delete process.env.UE_MCP_USER_STATE;
    }
  });

  it("applies ~/.ue-mcp/config.yml as a user-global layer under the project file", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    // Global config sets a personal default and a key the project omits.
    fs.writeFileSync(
      globalCfg,
      yaml.dump({ "ue-mcp": { context: { strategy: "micro" }, nativeTools: { enabled: false } } }),
    );
    fs.writeFileSync(
      path.join(projectDir, "ue-mcp.yml"),
      yaml.dump({ "ue-mcp": { version: 1, context: { strategy: "full" } } }),
    );

    const ctx = new ProjectContext();
    ctx.setProject(uproject);

    // Project overrides the global default...
    expect(ctx.config.context?.strategy).toBe("full");
    // ...but a key only the global layer sets still shows through.
    expect(ctx.config.nativeTools?.enabled).toBe(false);
  });
});
