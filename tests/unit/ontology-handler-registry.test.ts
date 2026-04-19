import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import { OntologyRegistry, createHandlerRegistryProjector } from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/index.js";

function buildFakeTools(): ToolDef[] {
  const blueprintTool = categoryTool(
    "blueprint",
    "Blueprint authoring.",
    {
      list_variables: bp("List variables on a BP", "bp_list_variables"),
      create_variable: bp("Create a new variable", "bp_create_variable"),
      delete_variable: bp("Delete a variable", "bp_delete_variable"),
    },
  );
  const editorTool = categoryTool(
    "editor",
    "Editor control.",
    {
      get_status: bp("Read editor status", "editor_get_status"),
      validate_scene: bp("Validate the scene", "editor_validate_scene"),
    },
  );
  return [blueprintTool, editorTool];
}

describe("ontology: HandlerRegistryProjector", () => {
  let tmpDir: string;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ontology-"));
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("emits a fragment with one point per tool and per action", () => {
    const tools = buildFakeTools();
    const projector = createHandlerRegistryProjector(tools);
    const fragment = projector.project(undefined as never);

    expect(fragment.basePath).toBe("/UE/Mediation/Registry");
    expect(fragment.producer).toBe("handler-registry");
    const toolsPoint = fragment.points.Tools;
    expect(toolsPoint).toBeDefined();
    expect(Object.keys(toolsPoint.children ?? {}).sort()).toEqual(["blueprint", "editor"]);

    const bpActions = toolsPoint.children!.blueprint.children!.Actions.children!;
    expect(Object.keys(bpActions).sort()).toEqual(["create_variable", "delete_variable", "list_variables"]);
  });

  it("assigns classification/approval/risk signals by action-name heuristic", () => {
    const tools = buildFakeTools();
    const projector = createHandlerRegistryProjector(tools);
    const fragment = projector.project(undefined as never);
    const actions = fragment.points.Tools.children!.blueprint.children!.Actions.children!;

    const list = actions.list_variables.fields!;
    const create = actions.create_variable.fields!;
    const del = actions.delete_variable.fields!;

    expect((list.classification as { marker?: string }).marker).toBe("read");
    expect((create.classification as { marker?: string }).marker).toBe("create");
    expect((del.classification as { marker?: string }).marker).toBe("destructive");

    expect((list.approval as { marker?: string }).marker).toBe("auto");
    expect((del.approval as { marker?: string }).marker).toBe("explicit");

    expect((list.risk as { marker?: string }).marker).toBe("trivial");
    expect((del.risk as { marker?: string }).marker).toBe("severe");
  });

  it("serializes fragments with Meaning anchors and a context tree", () => {
    const tools = buildFakeTools();
    const projector = createHandlerRegistryProjector(tools);
    const fragment = projector.project(undefined as never);
    const text = serializeFragment(fragment);

    expect(text).toContain("⛩️:");
    expect(text).toContain("Meaning@Tools:");
    expect(text).toContain("Meaning@blueprint:");
    expect(text).toContain("Category: /UE/Mediation/Registry/Tools/blueprint");
    expect(text).toContain("Meaning@delete_variable:");
    expect(text).toContain("bridge: \"bp_delete_variable\"");
    expect(text).not.toContain("true");
    expect(text).not.toContain("false");
    expect(text).not.toMatch(/:\s*\[/); // no JSON arrays
  });

  it("writes fragments to the resolved output dir and reports via listLayers", () => {
    const registry = new OntologyRegistry(() => tmpDir);
    registry.register(createHandlerRegistryProjector(buildFakeTools()), () => undefined);

    const results = registry.projectAll();
    expect(results).toHaveLength(1);
    expect(results[0].projector).toBe("handler-registry");
    expect(results[0].pointCount).toBeGreaterThan(5);
    expect(fs.existsSync(results[0].outputPath)).toBe(true);

    const layers = registry.listLayers();
    expect(layers.map((l) => l.file)).toContain("handler-registry.cairn");
  });

  it("projectByEvent filters projectors by their triggerEvents", () => {
    const registry = new OntologyRegistry(() => tmpDir);
    registry.register(createHandlerRegistryProjector(buildFakeTools()), () => undefined);

    expect(registry.projectByEvent("startup")).toHaveLength(1);
    expect(registry.projectByEvent("flow-completed")).toHaveLength(0);
  });
});
