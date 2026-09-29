import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const handlerSource = readHandlerFile("SkeletalMeshHandlers.cpp");

describe("skeletal mesh bone vertex bounds", () => {
  it("takes its contract from the C++ spec", () => {
    expect(assetTool.schema.action.safeParse("read_skeletal_mesh_bone_vertex_bounds").success).toBe(true);
    const read = assetTool.actions.read_skeletal_mesh_bone_vertex_bounds;
    expect(read.mapParams).toBeUndefined();
    expect(read.description).toContain("Params: assetPath, boneNames?, minWeight?, lodIndex?, profileName?");
    expect(assetTool.schema.boneNames.safeParse(["spine_01", "head"]).success).toBe(true);
    expect(assetTool.schema.minWeight.safeParse(0.5).success).toBe(true);
  });

  it("is a read and never touches the mesh", () => {
    expect(classifyAction("asset.read_skeletal_mesh_bone_vertex_bounds", {
      assetPath: "/Game/Characters/SK_Hero",
    }).mutates).toBe(false);

    const start = handlerSource.indexOf("FSkeletalMeshHandlers::ReadBoneVertexBounds(");
    const end = handlerSource.indexOf("FSkeletalMeshHandlers::SetSkinWeights(", start);
    const body = handlerSource.slice(start, end);
    expect(start).toBeGreaterThan(-1);
    expect(body).toContain("MCPResolveSkinWeightTarget(Params, Target)");
    for (const write of ["Modify()", "CommitMeshDescription", "SkinWeights.Set(", "MarkPackageDirty"]) {
      expect(body).not.toContain(write);
    }
  });

  it("validates the filter and threshold before reading vertices", () => {
    const start = handlerSource.indexOf("FSkeletalMeshHandlers::ReadBoneVertexBounds(");
    const end = handlerSource.indexOf("FSkeletalMeshHandlers::SetSkinWeights(", start);
    const body = handlerSource.slice(start, end);
    const firstVertex = body.indexOf("Description.Vertices().GetElementIDs()");
    for (const guard of ["0 < minWeight <= 1", "is not a bone of", "duplicate bone", "must be a non-empty array"]) {
      expect(body.indexOf(guard)).toBeGreaterThan(-1);
      expect(body.indexOf(guard)).toBeLessThan(firstVertex);
    }
  });
});
