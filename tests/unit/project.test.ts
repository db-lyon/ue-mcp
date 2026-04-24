import { describe, it, expect, beforeEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
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
  it("ignores a malformed .ue-mcp.json without throwing", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    fs.writeFileSync(path.join(projectDir, ".ue-mcp.json"), "{not valid json");

    const ctx = new ProjectContext();
    expect(() => ctx.setProject(uproject)).not.toThrow();
    expect(ctx.config).toEqual({});
  });

  it("loads a valid .ue-mcp.json", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    fs.writeFileSync(
      path.join(projectDir, ".ue-mcp.json"),
      JSON.stringify({ disable: ["gas"], http: { enabled: true, port: 7723 } }),
    );

    const ctx = new ProjectContext();
    ctx.setProject(uproject);
    expect(ctx.config.disable).toEqual(["gas"]);
    expect(ctx.config.http?.port).toBe(7723);
  });

  it("rejects a .ue-mcp.json with wrong types", () => {
    const uproject = makeTempProject();
    const projectDir = path.dirname(uproject);
    // disable should be an array; passing a string should fall back to defaults
    fs.writeFileSync(
      path.join(projectDir, ".ue-mcp.json"),
      JSON.stringify({ disable: "gas" }),
    );

    const ctx = new ProjectContext();
    ctx.setProject(uproject);
    expect(ctx.config).toEqual({});
  });
});
