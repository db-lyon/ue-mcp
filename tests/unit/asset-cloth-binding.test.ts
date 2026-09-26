import { describe, expect, it } from "vitest";
import { parseParams } from "../../src/surface/action-schema.js";
import { assetTool } from "../../src/tools/asset.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";

describe("asset cloth section binding (#1139)", () => {
  it("routes bind and unbind to their bridge handlers", () => {
    const bind = assetTool.actions.bind_cloth_to_section;
    const unbind = assetTool.actions.unbind_cloth_from_section;
    expect(bind.kind === "bridge" && bind.bridge).toBe("bind_cloth_to_section");
    expect(unbind.kind === "bridge" && unbind.bridge).toBe("unbind_cloth_from_section");
    expect(bind.effect).toBe("mutate");
    expect(unbind.effect).toBe("mutate");
  });

  it("documents and forwards the section address", () => {
    const params = parseParams(assetTool.actions.bind_cloth_to_section.description ?? "").params.map((p) => p.name);
    expect(params).toEqual(expect.arrayContaining(["skeletalMeshPath", "lodIndex", "sectionIndex", "clothingAsset", "assetLodIndex"]));

    // #1057: both handlers declare the section address themselves, and the
    // actions forward the bag as sent.
    expect(assetTool.actions.bind_cloth_to_section.mapParams).toBeUndefined();
    expect(assetTool.actions.unbind_cloth_from_section.mapParams).toBeUndefined();
    const required = (method: string) =>
      handlerSpecs[method].params.filter((p) => p.required).map((p) => p.name);
    expect(required("bind_cloth_to_section")).toEqual(["skeletalMeshPath", "lodIndex", "sectionIndex"]);
    expect(required("unbind_cloth_from_section")).toEqual(["skeletalMeshPath", "lodIndex", "sectionIndex"]);
  });

  it("accepts a LOD 0 section address in the schema", () => {
    expect(assetTool.schema.lodIndex.safeParse(0).success).toBe(true);
    expect(assetTool.schema.sectionIndex.safeParse(0).success).toBe(true);
    expect(assetTool.schema.assetLodIndex.safeParse(-1).success).toBe(false);
  });
});
