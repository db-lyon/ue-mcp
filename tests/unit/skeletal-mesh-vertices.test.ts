import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const handlerSource = readHandlerFile("SkeletalMeshHandlers.cpp");

function handlerBody(): string {
  const start = handlerSource.indexOf("FSkeletalMeshHandlers::ReadSkeletalMeshVertices(");
  const end = handlerSource.indexOf("FSkeletalMeshHandlers::SetSkinWeights(", start);
  expect(start).toBeGreaterThan(-1);
  expect(end).toBeGreaterThan(start);
  return handlerSource.slice(start, end);
}

describe("read_skeletal_mesh_vertices (#1567)", () => {
  it("takes its contract from the C++ spec", () => {
    expect(assetTool.schema.action.safeParse("read_skeletal_mesh_vertices").success).toBe(true);
    const read = assetTool.actions.read_skeletal_mesh_vertices;
    expect(read.mapParams).toBeUndefined();
    expect(read.description).toContain(
      "Params: assetPath, boneNames?, includeChildBones?, minWeight?, box?, include?, offset?, limit?, dumpToFile?, outputPath?, lodIndex?, profileName?",
    );
    expect(assetTool.schema.box.safeParse({ min: { x: -1, y: -1, z: -1 }, max: { x: 1, y: 1, z: 1 } }).success).toBe(true);
    expect(assetTool.schema.include.safeParse(["positions", "weights", "triangles"]).success).toBe(true);
  });

  it("is a read and never touches the mesh", () => {
    expect(classifyAction("asset.read_skeletal_mesh_vertices", { assetPath: "/Game/Vehicles/SK_Van" }).mutates).toBe(false);
    const body = handlerBody();
    expect(body).toContain("MCPResolveSkinWeightTarget(Params, Target)");
    for (const write of ["Modify()", "CommitMeshDescription", "SkinWeights.Set(", "MarkPackageDirty"]) {
      expect(body).not.toContain(write);
    }
  });

  it("validates every selector and the page before reading vertices", () => {
    const body = handlerBody();
    const firstVertex = body.indexOf("Description.Vertices().GetElementIDs()");
    expect(firstVertex).toBeGreaterThan(-1);
    for (const guard of [
      "is not one of positions, weights, triangles",
      "is not a bone of",
      "need boneNames",
      "0 < minWeight <= 1",
      "box.min must not exceed box.max",
      "outputPath needs dumpToFile=true",
      "offset must be 0 or more",
      "use dumpToFile for more",
    ]) {
      expect(body.indexOf(guard), guard).toBeGreaterThan(-1);
      expect(body.indexOf(guard), guard).toBeLessThan(firstVertex);
    }
  });

  it("pages triangles by their earliest selected corner so each appears once", () => {
    const body = handlerBody();
    expect(body).toContain("Owner = FMath::Min(Owner, Ordinal)");
    expect(body).toContain("if (Owner < First || Owner >= End) continue;");
  });
});
