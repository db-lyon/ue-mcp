import { describe, expect, it } from "vitest";
import { parseParamsClause } from "../../src/action-schema.js";
import { assetTool } from "../../src/tools/asset.js";

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
    const params = parseParamsClause(assetTool.actions.bind_cloth_to_section.description ?? "").map((p) => p.name);
    expect(params).toEqual(expect.arrayContaining(["skeletalMeshPath", "lodIndex", "sectionIndex", "clothingAsset", "assetLodIndex"]));

    expect(assetTool.actions.bind_cloth_to_section.mapParams?.({
      skeletalMeshPath: "/Game/SK_Hair",
      lodIndex: 1,
      sectionIndex: 0,
      clothingAsset: "SK_Hair_Clothing_0",
      assetLodIndex: 0,
      properties: { ignored: true },
    })).toEqual({
      skeletalMeshPath: "/Game/SK_Hair",
      lodIndex: 1,
      sectionIndex: 0,
      clothingAsset: "SK_Hair_Clothing_0",
      assetLodIndex: 0,
    });

    expect(assetTool.actions.unbind_cloth_from_section.mapParams?.({
      skeletalMeshPath: "/Game/SK_Hair",
      lodIndex: 0,
      sectionIndex: 2,
    })).toMatchObject({ skeletalMeshPath: "/Game/SK_Hair", lodIndex: 0, sectionIndex: 2 });
  });

  it("accepts a LOD 0 section address in the schema", () => {
    expect(assetTool.schema.lodIndex.safeParse(0).success).toBe(true);
    expect(assetTool.schema.sectionIndex.safeParse(0).success).toBe(true);
    expect(assetTool.schema.assetLodIndex.safeParse(-1).success).toBe(false);
  });
});
