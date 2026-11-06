import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { registeredTimeoutMs } from "../../src/bridge/bridge-timeouts.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const sourceFile = readHandlerFile("SkeletalMeshHandlers.cpp");
const shared = readHandlerFile("SkeletalMeshVertexRead.h");

function sourceBody(): string {
  const start = sourceFile.indexOf("FSkeletalMeshHandlers::ReadSkeletalMeshVertices(");
  const end = sourceFile.indexOf("FSkeletalMeshHandlers::SetSkinWeights(", start);
  expect(start).toBeGreaterThan(-1);
  expect(end).toBeGreaterThan(start);
  return sourceFile.slice(start, end);
}

// Every selector and the page are parsed by the shared header before either
// read touches a vertex.
const SHARED_PARSERS = [
  "MCPVertexRead::ParseIncludes(Params, Include)",
  "MCPVertexRead::ParseBoneSelection(Params, RefSkeleton,",
  "MCPVertexRead::ParseBox(Params, bByBox, SelectionBox)",
  "MCPVertexRead::ParsePage(Params,",
];

describe("the shared vertex read code (#1567)", () => {
  it("refuses every bad selector and page", () => {
    for (const guard of [
      "is not one of positions, weights, triangles",
      "is not a bone of",
      "need boneNames",
      "0 < minWeight <= 1",
      "box.min must not exceed box.max",
      "outputPath needs dumpToFile=true",
      "%s must be 0 or more",
      "use dumpToFile for more",
    ]) {
      expect(shared, guard).toContain(guard);
    }
  });

  it("pages triangles by their earliest selected corner so each appears once", () => {
    expect(shared).toContain("Owner = FMath::Min(Owner, Ordinal)");
    expect(shared).toContain("if (Owner < Page.First || Owner >= Page.End) return;");
  });
});

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
    expect(registeredTimeoutMs("read_skeletal_mesh_vertices")).toBe(300_000);
  });

  it("is a read and never touches the mesh", () => {
    expect(classifyAction("asset.read_skeletal_mesh_vertices", { assetPath: "/Game/Vehicles/SK_Van" }).mutates).toBe(false);
    const body = sourceBody();
    expect(body).toContain("MCPResolveSkinWeightTarget(Params, Target)");
    for (const write of ["Modify()", "CommitMeshDescription", "SkinWeights.Set(", "MarkPackageDirty"]) {
      expect(body).not.toContain(write);
    }
  });

  it("validates every selector and the page before reading vertices", () => {
    const body = sourceBody();
    const firstVertex = body.indexOf("Description.Vertices().GetElementIDs()");
    expect(firstVertex).toBeGreaterThan(-1);
    for (const parser of SHARED_PARSERS) {
      expect(body.indexOf(parser), parser).toBeGreaterThan(-1);
      expect(body.indexOf(parser), parser).toBeLessThan(firstVertex);
    }
  });
});
