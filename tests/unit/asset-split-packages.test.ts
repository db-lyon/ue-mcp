import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";

describe("asset moves over multi-object packages (#1137)", () => {
  it("documents the refusal on both batch move actions", () => {
    for (const name of ["move_folder", "bulk_rename"] as const) {
      const description = assetTool.actions[name].description ?? "";
      expect(description).toContain("package_objects_left_behind");
    }
    expect(assetTool.actions.bulk_rename.description).toContain("splitPackages");
  });
});
