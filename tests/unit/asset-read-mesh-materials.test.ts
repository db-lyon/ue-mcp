import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import { parseParams } from "../../src/surface/action-schema.js";

describe("mesh material inspection surface", () => {
  it("publishes a read with an optional integer LOD filter", () => {
    const action = assetTool.actions.read_mesh_materials;
    expect(action.effect).toBe("read");
    expect(action.bridge).toBe("read_mesh_materials");
    expect(parseParams(action.description ?? "").params.map((p) => p.name)).toEqual(["assetPath", "lodIndex"]);
    expect(handlerSpecs.read_mesh_materials.params).toEqual([
      expect.objectContaining({ name: "assetPath", required: true, type: "string" }),
      expect.objectContaining({ name: "lodIndex", required: false, type: "integer" }),
    ]);
    expect(assetTool.schema.lodIndex.safeParse(1.5).success).toBe(false);
    expect(assetTool.schema.lodIndex.safeParse(2).success).toBe(true);
  });
});
