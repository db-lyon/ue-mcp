import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { levelTool } from "../../src/tools/level.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { registeredTimeoutMs } from "../../src/bridge/bridge-timeouts.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const sourceFile = readHandlerFile("SkeletalMeshHandlers.cpp");
const posedFile = readHandlerFile("SkeletalMeshHandlers_Posed.cpp");
const shared = readHandlerFile("SkeletalMeshVertexRead.h");

function sourceBody(): string {
  const start = sourceFile.indexOf("FSkeletalMeshHandlers::ReadSkeletalMeshVertices(");
  const end = sourceFile.indexOf("FSkeletalMeshHandlers::SetSkinWeights(", start);
  expect(start).toBeGreaterThan(-1);
  expect(end).toBeGreaterThan(start);
  return sourceFile.slice(start, end);
}

function posedBody(): string {
  const start = posedFile.indexOf("FSkeletalMeshHandlers::GetSkinnedVertices(");
  expect(start).toBeGreaterThan(-1);
  return posedFile.slice(start);
}

// Every selector and the page are parsed by the shared header before either
// read touches a vertex.
const SHARED_PARSERS = [
  "MCPVertexRead::ParseIncludes(Params, Include)",
  "MCPVertexRead::ParseBoneSelection(Params, RefSkeleton,",
  "MCPVertexRead::ParseBox(Params, bByBox, SelectionBox)",
  "MCPVertexRead::ParsePage(Params,",
];

describe("the shared vertex read code (#1567, #1452)", () => {
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

  it("echoes the page key each read was asked with", () => {
    expect(sourceBody()).toContain('MCPVertexRead::ParsePage(Params, TEXT("offset"), Page)');
    expect(posedBody()).toContain('MCPVertexRead::ParsePage(Params, TEXT("startIndex"), Page)');
    expect(shared).toContain('NextKey = TEXT("next") + NextKey.Left(1).ToUpper() + NextKey.Mid(1);');
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

describe("get_skinned_vertices (#1452)", () => {
  it("takes its contract from the C++ spec, on the level tool", () => {
    expect(levelTool.schema.action.safeParse("get_skinned_vertices").success).toBe(true);
    const read = levelTool.actions.get_skinned_vertices;
    expect(read.mapParams).toBeUndefined();
    expect(read.description).toContain(
      "Params: at least one of actorLabel/actorPath, componentName?, world?, pieInstance?, lodIndex?, space?, boneNames?, includeChildBones?, minWeight?, box?, nearPoint?, radius?, include?, startIndex?, limit?, dumpToFile?, outputPath?",
    );
    expect(levelTool.schema.space.safeParse("world").success).toBe(true);
    expect(levelTool.schema.space.safeParse("bone").success).toBe(false);
    expect(levelTool.schema.nearPoint.safeParse({ x: 1, y: 2, z: 3 }).success).toBe(true);
    expect(registeredTimeoutMs("get_skinned_vertices")).toBe(300_000);
    expect(sourceFile).toMatch(
      /FCategoryScope LevelScope\(Registry, TEXT\("level"\)\);\s*Registry\.RegisterHandlerWithTimeout\(TEXT\("get_skinned_vertices"\)/,
    );
  });

  it("is a read and never touches the component or mesh", () => {
    expect(classifyAction("level.get_skinned_vertices", { actorLabel: "Driver" }).mutates).toBe(false);
    const body = posedBody();
    for (const write of ["Modify()", "MarkPackageDirty", "MarkRenderStateDirty", "SetSkinnedAsset", "FScopedTransaction"]) {
      expect(body).not.toContain(write);
    }
  });

  it("skins with the matrices the renderer uses, so a follower reads through its leader", () => {
    const body = posedBody();
    // GetCurrentRefToLocalMatrices maps a follower's bones to its leader's pose;
    // CacheRefToLocalMatrices indexes the leader by the follower's bone index.
    expect(body).toContain("Comp->GetCurrentRefToLocalMatrices(RefToLocals, LodIndex);");
    expect(body).not.toContain("CacheRefToLocalMatrices");
    expect(body).toContain("LODData.RenderSections[SectionIndex].BoneMap");
    expect(body).toContain("Comp->GetSkinWeightBuffer(LodIndex)");
    expect(body).toContain("Comp->ActiveMorphTargets");
    expect(body).toContain("leaderPoseComponent");
  });

  it("validates every selector and the page before skinning a vertex", () => {
    const body = posedBody();
    const firstVertex = body.indexOf("GetCurrentRefToLocalMatrices");
    expect(firstVertex).toBeGreaterThan(-1);
    for (const guard of [
      ...SHARED_PARSERS,
      "is not one of component, world",
      "lodIndex %d is out of range",
      "nearPoint must be {x,y,z}",
      "radius must be a finite number above 0",
      "keeps no CPU copy of its vertex positions",
      "keeps no CPU copy of its skin weights",
    ]) {
      expect(body.indexOf(guard), guard).toBeGreaterThan(-1);
      expect(body.indexOf(guard), guard).toBeLessThan(firstVertex);
    }
  });
});
