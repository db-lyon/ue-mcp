import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  createProjectConfigProjector,
  parse,
  compose,
  select,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/index.js";

function projectAndCompose(projectPath: string | null, projectDir: string | null) {
  const proj = createProjectConfigProjector();
  const frag = proj.project({ projectPath, projectDir });
  const parsed = parse(serializeFragment(frag), "proj-config");
  return compose([{ priority: 1, fragment: parsed }]);
}

describe("ontology: ProjectConfigProjector", () => {
  let tmpDir: string;
  let projectDir: string;
  let projectPath: string;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-pc-"));
    projectDir = path.join(tmpDir, "MyProject");
    fs.mkdirSync(projectDir, { recursive: true });
    projectPath = path.join(projectDir, "MyProject.uproject");
  });

  afterEach(() => fs.rmSync(tmpDir, { recursive: true, force: true }));

  it("emits Descriptors with zero sources when no files exist", () => {
    const view = projectAndCompose(null, null);
    const desc = view.root.children!.Project.children!.Config.children!.Descriptors;
    expect(desc.fields!.sourceCount).toBe(0);
  });

  it("parses a .uproject descriptor with modules and plugin overrides", () => {
    fs.writeFileSync(
      projectPath,
      JSON.stringify({
        FileVersion: 3,
        EngineAssociation: "5.7",
        Category: "Game",
        Description: "My test project.",
        Modules: [{ Name: "MyProject", Type: "Runtime", LoadingPhase: "Default" }],
        Plugins: [
          { Name: "GameplayAbilities", Enabled: true },
          { Name: "OldPlugin", Enabled: false },
        ],
      }),
      "utf-8",
    );

    const view = projectAndCompose(projectPath, projectDir);
    const desc = view.root.children!.Project.children!.Config.children!.Descriptors;
    expect(desc.fields!.sourceCount).toBe(1);
    const up = desc.children!.UProject;
    expect(up.fields!.fileVersion).toBe(3);
    expect(up.fields!.engineAssociation).toBe("5.7");
    expect(up.children!.Modules.children!.MyProject.fields!.type).toBe("Runtime");

    const overrides = up.children!.PluginOverrides.children!;
    expect((overrides.GameplayAbilities.fields!.enabled as { marker?: string }).marker).toBe("enabled");
    expect((overrides.OldPlugin.fields!.enabled as { marker?: string }).marker).toBe("disabled");
  });

  it("includes .ue-mcp.json when present alongside the .uproject", () => {
    fs.writeFileSync(projectPath, JSON.stringify({ EngineAssociation: "5.7" }), "utf-8");
    fs.writeFileSync(
      path.join(projectDir, ".ue-mcp.json"),
      JSON.stringify({ displayName: "MyProject", ignoredCount: 12 }),
      "utf-8",
    );

    const view = projectAndCompose(projectPath, projectDir);
    const desc = view.root.children!.Project.children!.Config.children!.Descriptors;
    expect(desc.fields!.sourceCount).toBe(2);
    expect(desc.children!.McpConfig.fields!.displayName).toBe("MyProject");
    expect(desc.children!.McpConfig.fields!.ignoredCount).toBe(12);
  });

  it("selector finds disabled plugin overrides", () => {
    fs.writeFileSync(
      projectPath,
      JSON.stringify({
        Plugins: [
          { Name: "A", Enabled: true },
          { Name: "B", Enabled: false },
          { Name: "C", Enabled: false },
        ],
      }),
      "utf-8",
    );
    const view = projectAndCompose(projectPath, projectDir);
    const hits = select(
      view.root,
      "/UE/Project/Config/Descriptors/UProject/PluginOverrides/*@enabled=disabled",
    );
    expect(hits.map((h) => h.path.split("/").pop()).sort()).toEqual(["B", "C"]);
  });
});
