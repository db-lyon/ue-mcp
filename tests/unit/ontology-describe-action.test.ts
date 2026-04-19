import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import { OntologyRegistry, createHandlerRegistryProjector } from "../../src/ontology/index.js";
import { createOntologyTool } from "../../src/tools/ontology.js";
import type { ToolContext } from "../../src/types.js";

function buildTools(): ToolDef[] {
  return [
    categoryTool(
      "gas",
      "GAS.",
      {
        create_ability: bp("Create ability", "create_ability"),
      },
      undefined,
      undefined,
      { requires: ["GameplayAbilities"] },
    ),
    categoryTool("editor", "Editor.", {
      execute_python: {
        bridge: "execute_python",
        classification: "destructive",
        approval: "explicit",
        risk: "catastrophic",
        requires: ["PythonScriptPlugin"],
      },
    }),
  ];
}

describe("ontology tool: describe_action", () => {
  let tmpDir: string;
  let registry: OntologyRegistry;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-describe-"));
    const projectedDir = path.join(tmpDir, "projected");
    fs.mkdirSync(projectedDir, { recursive: true });
    registry = new OntologyRegistry(
      () => projectedDir,
      () => ({
        kernel: { priority: 0, paths: [] },
        projected: { priority: 1, paths: [projectedDir] },
        repoLocal: { priority: 2, paths: [] },
      }),
    );
    registry.register(createHandlerRegistryProjector(buildTools()), () => undefined);
    registry.projectAll();
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("returns declared metadata for an explicitly-declared action", async () => {
    const tool = createOntologyTool(registry);
    const result = (await tool.actions.describe_action.handler!(
      {} as ToolContext,
      { action: "describe_action", tool: "editor", actionName: "execute_python" },
    )) as Record<string, unknown>;

    expect(result.path).toBe("/UE/Mediation/Registry/Tools/editor/Actions/execute_python");
    expect((result.classification as { marker?: string }).marker).toBe("destructive");
    expect((result.approval as { marker?: string }).marker).toBe("explicit");
    expect((result.risk as { marker?: string }).marker).toBe("catastrophic");
    expect(result.metadataSource).toBe("declared");
    expect(result.requires).toEqual(["PythonScriptPlugin"]);
  });

  it("returns category-level requires for an action that inherited them", async () => {
    const tool = createOntologyTool(registry);
    const result = (await tool.actions.describe_action.handler!(
      {} as ToolContext,
      { action: "describe_action", tool: "gas", actionName: "create_ability" },
    )) as Record<string, unknown>;

    expect(result.requires).toEqual(["GameplayAbilities"]);
    expect((result.classification as { marker?: string }).marker).toBe("create");
    expect(result.metadataSource).toBe("heuristic");
  });

  it("throws NOT_FOUND for an unknown action", async () => {
    const tool = createOntologyTool(registry);
    await expect(
      tool.actions.describe_action.handler!(
        {} as ToolContext,
        { action: "describe_action", tool: "gas", actionName: "does_not_exist" },
      ),
    ).rejects.toThrow(/No ontology point/);
  });

  it("throws INVALID_PARAMS when tool or actionName is missing", async () => {
    const tool = createOntologyTool(registry);
    await expect(
      tool.actions.describe_action.handler!({} as ToolContext, { action: "describe_action", tool: "gas" }),
    ).rejects.toThrow(/Missing required parameters/);
  });
});
