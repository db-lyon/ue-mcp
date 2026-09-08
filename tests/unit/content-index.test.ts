/**
 * The one asset query that works with no editor (T16).
 *
 * Every other asset action dispatches into the editor's asset registry, so with
 * the editor down "what is in /Game/Characters" had no answer at all. This
 * reads the package files instead, through the same mount table the live path
 * uses, and is explicit about the questions a file listing cannot answer.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, it, expect } from "vitest";
import { ProjectContext } from "../../src/project.js";
import { listContent } from "../../src/content-index.js";
import { projectTool } from "../../src/tools/project.js";

const made: string[] = [];

afterEach(() => {
  for (const dir of made.splice(0)) {
    try {
      fs.rmSync(dir, { recursive: true, force: true });
    } catch {
      // A temp directory the OS will reap anyway.
    }
  }
});

/** A project with a Content tree, and optionally a plugin with its own. */
function makeProject(opts: { plugin?: boolean } = {}): ProjectContext {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-content-"));
  made.push(root);
  const dir = path.join(root, "Probe");
  const content = path.join(dir, "Content");
  fs.mkdirSync(path.join(content, "Characters", "Hero"), { recursive: true });
  fs.mkdirSync(path.join(content, "Maps"), { recursive: true });
  fs.writeFileSync(path.join(dir, "Probe.uproject"), JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8" }));
  fs.writeFileSync(path.join(content, "Characters", "SK_Hero.uasset"), "package");
  fs.writeFileSync(path.join(content, "Characters", "Hero", "M_Hero.uasset"), "package");
  fs.writeFileSync(path.join(content, "Maps", "L_Test.umap"), "map");
  // Not a package. It must not appear as one.
  fs.writeFileSync(path.join(content, "Characters", "notes.txt"), "notes");

  if (opts.plugin) {
    const pluginContent = path.join(dir, "Plugins", "Extras", "Content");
    fs.mkdirSync(pluginContent, { recursive: true });
    fs.writeFileSync(path.join(dir, "Plugins", "Extras", "Extras.uplugin"), JSON.stringify({ FileVersion: 3 }));
    fs.writeFileSync(path.join(pluginContent, "T_Extra.uasset"), "package");
  }

  const project = new ProjectContext();
  project.setProject(path.join(dir, "Probe.uproject"));
  return project;
}

describe("listContent", () => {
  it("names packages the way the editor would, and skips everything that is not one", () => {
    const listing = listContent(makeProject());
    const paths = listing.assets.map((a) => a.assetPath).sort();
    expect(paths).toEqual([
      "/Game/Characters/Hero/M_Hero",
      "/Game/Characters/SK_Hero",
      "/Game/Maps/L_Test",
    ]);
    expect(listing.assets.find((a) => a.name === "L_Test")!.kind).toBe("map");
    expect(listing.assets.find((a) => a.name === "SK_Hero")!.kind).toBe("asset");
    expect(listing.assets.every((a) => a.sizeBytes > 0 && a.modified !== "")).toBe(true);
  });

  it("says what a file listing cannot answer instead of implying it did", () => {
    const listing = listContent(makeProject());
    expect(listing.note).toContain("asset registry");
    expect(listing.note).toContain("asset(action='list')");
  });

  it("narrows to a subfolder and stops recursing when told to", () => {
    const project = makeProject();
    const nested = listContent(project, { contentPath: "/Game/Characters" });
    expect(nested.count).toBe(2);

    const flat = listContent(project, { contentPath: "/Game/Characters", recursive: false });
    expect(flat.assets.map((a) => a.name)).toEqual(["SK_Hero"]);
  });

  it("filters by name, case-insensitively", () => {
    const listing = listContent(makeProject(), { namePattern: "hero" });
    expect(listing.assets.map((a) => a.name).sort()).toEqual(["M_Hero", "SK_Hero"]);
  });

  it("stops the walk at maxResults and says so", () => {
    const listing = listContent(makeProject(), { maxResults: 2 });
    expect(listing.count).toBe(2);
    expect(listing.truncated).toBe(true);
    expect(listContent(makeProject(), { maxResults: 50 }).truncated).toBe(false);
  });

  it("resolves a plugin's own mount", () => {
    const project = makeProject({ plugin: true });
    const listing = listContent(project, { contentPath: "/Extras" });
    expect(listing.assets.map((a) => a.assetPath)).toEqual(["/Extras/T_Extra"]);
    // A plugin's content is under its own mount, never under /Game.
    expect(listContent(project).assets.some((a) => a.name === "T_Extra")).toBe(false);
  });

  it("refuses an unknown mount by naming the ones this project has", () => {
    expect(() => listContent(makeProject(), { contentPath: "/Nope" }))
      .toThrow(/Unknown mount '\/Nope'\. This project mounts: \/Game/);
  });

  it("refuses a filesystem path, which is what list_files takes", () => {
    expect(() => listContent(makeProject(), { contentPath: "Content/Characters" }))
      .toThrow(/must be a mount path/);
  });

  it("explains a folder that is absent on disk rather than returning nothing", () => {
    expect(() => listContent(makeProject(), { contentPath: "/Game/Empty" }))
      .toThrow(/does not exist/);
  });
});

describe("project(list_content_assets) over the real dispatcher", () => {
  it("answers with no editor, and never reaches one", async () => {
    const project = makeProject();
    const ctx = {
      project,
      bridge: {
        isConnected: false,
        connect: async () => {},
        retargetProject: () => ({ projectPath: null, port: 1, portSource: "derived" as const, verified: true }),
        getTarget: () => ({ projectPath: null, port: 1, portSource: "derived" as const, verified: true }),
        call: async () => {
          throw new Error("list_content_assets must never reach the editor");
        },
      },
    };
    const result = (await projectTool.handler(ctx as never, {
      action: "list_content_assets",
      contentPath: "/Game/Characters",
    })) as { count: number; assets: Array<{ assetPath: string }> };
    expect(result.count).toBe(2);
    expect(result.assets.every((a) => a.assetPath.startsWith("/Game/Characters/"))).toBe(true);
  });
});
