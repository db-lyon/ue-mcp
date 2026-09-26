import * as fs from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { levelTool } from "../../src/tools/level.js";
import { actionSchema } from "../../src/action-schema.js";
import { buildMicroGateway } from "../../src/micro-context.js";
import type { ToolContext } from "../../src/types.js";
import { editorTool } from "../../src/tools/editor.js";
import { projectTool } from "../../src/tools/project.js";

describe("spatial request contract", () => {
  it("rejects invalid capture margins: a non-number at the wire, a non-positive one in the handler", () => {
    // The C++ spec declares focusMargin a number (#1057); the range is the handler's to refuse.
    const wire = z.object(editorTool.schema);
    for (const focusMargin of [NaN, "1.5"]) {
      expect(wire.safeParse({ action: "capture_scene_png", outputPath: "capture.png", focusMargin }).success).toBe(false);
    }
    const source = fs.readFileSync(
      new URL("../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/EditorHandlers.cpp", import.meta.url),
      "utf8",
    );
    expect(source).toContain("if (!FMath::IsFinite(FocusMargin) || FocusMargin <= 0.0)");
    expect(source).toContain("focusMargin must be finite and greater than zero");
  });

  it("uses the addressed graph for project search and schemas, including an empty graph", async () => {
    for (const graph of [[levelTool], []]) {
      const ctx = { getToolGraph: () => graph } as unknown as ToolContext;
      const result = await projectTool.actions.search_tools.handler!(ctx, { query: "nudge_component" }) as any;
      expect(result.results.some((hit: any) => hit.tool === "level")).toBe(graph.length > 0);
      const schema = projectTool.actions.describe_action.handler!(ctx, { name: "level.nudge_component" });
      if (graph.length) expect(await schema).toMatchObject({ action: "nudge_component" });
      else await expect(schema).rejects.toThrow();
    }
  });
  const request = {
    action: "nudge_component", actorPath: "/Game/Map.Map:PersistentLevel.Prop",
    componentName: "Grip", frame: "actor", world: "pie", pieInstance: 2, dryRun: true,
    viewRotation: { viewFrom: "above", direction: "clockwise", degrees: 15 },
  };

  it("forwards viewpoint, preview and exact target identically through category and gateway", async () => {
    const call = vi.fn(async () => ({ success: true, operationApplied: false }));
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await levelTool.handler(ctx, z.object(levelTool.schema).parse(request));
    const categoryCall = call.mock.calls[0];
    await buildMicroGateway([levelTool]).handler(ctx, {
      action: "call", category: "level", method: "nudge_component", args: request,
    });
    expect(call.mock.calls[1]).toEqual(categoryCall);
    expect(categoryCall).toEqual(["nudge_component", expect.objectContaining({
      actorPath: request.actorPath, componentName: "Grip", viewRotation: request.viewRotation,
      dryRun: true, world: "pie", pieInstance: 2,
    }), undefined]);
  });

  it("publishes the nested viewpoint vocabulary and rejects malformed wire values", () => {
    const schema = actionSchema(levelTool, "nudge_component");
    const rotation = schema.params.find((p) => p.name === "viewRotation")!;
    // The spec publishes the vocabulary in each field's description; the handler
    // refuses anything outside it, and a degrees value that is not above zero.
    expect(rotation.properties!.viewFrom.description).toBe("front | back | right | left | above | below");
    expect(rotation.properties!.direction.description).toBe("clockwise | counterclockwise");
    const wire = z.object(levelTool.schema);
    for (const invalid of [null, { viewFrom: "above", degrees: 15 }, { ...request.viewRotation, degrees: "15" }]) {
      expect(wire.safeParse({ ...request, viewRotation: invalid }).success).toBe(false);
    }
    expect(wire.safeParse({ ...request, dryRun: "true" }).success).toBe(false);
  });
});
