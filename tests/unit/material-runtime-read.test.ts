import { describe, expect, it } from "vitest";
import { parseParams } from "../../src/surface/action-schema.js";
import { materialTool } from "../../src/tools/material.js";
import { handlerSpecs } from "../../src/tools/specs/material.generated.js";

describe("material runtime reads (#1114/#1116)", () => {
  it("forwards the component selector through read_instance as sent (#1057)", () => {
    expect(materialTool.actions.read_instance.mapParams).toBeUndefined();
    const spec = handlerSpecs.read_material_instance;
    expect(spec.params.map((p) => p.name)).toEqual(expect.arrayContaining([
      "actorLabel", "actorPath", "componentName", "slotIndex", "slotName", "world", "pieInstance",
    ]));
    expect(spec.choices?.[0].branches).toEqual([["assetPath"], ["actorLabel"], ["actorPath"]]);

    // The asset form is unchanged, including the materialPath alias.
    expect(spec.params.find((p) => p.name === "assetPath")?.aliases).toEqual(["path", "materialPath"]);
  });

  it("routes read_mpc to the collection reader and documents world", () => {
    const spec = materialTool.actions.read_mpc;
    expect(spec.kind === "bridge" && spec.bridge).toBe("read_material_parameter_collection");
    expect(spec.effect).toBe("read");
    const params = parseParams(spec.description ?? "").params.map((p) => p.name);
    expect(params).toEqual(expect.arrayContaining(["assetPath", "world"]));
    // Spec'd (#1057): the bag goes as sent, and `path` is an alias the registry resolves.
    expect(spec.mapParams).toBeUndefined();
    const assetPath = handlerSpecs.read_material_parameter_collection.params.find((p) => p.name === "assetPath");
    expect(assetPath?.aliases).toEqual(["path"]);
  });

  it("declares the selector keys so the MCP layer does not strip them", () => {
    for (const key of ["actorLabel", "actorPath", "componentName", "slotIndex", "slotName", "world", "pieInstance"]) {
      expect(materialTool.schema[key], key).toBeDefined();
    }
  });
});
