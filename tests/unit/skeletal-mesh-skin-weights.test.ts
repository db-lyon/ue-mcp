import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { classifyAction } from "../../src/locking.js";
import { classifyWrite } from "../../src/flow/write-methods.js";

const handlerSource = readFileSync(
  new URL(
    "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/SkeletalMeshHandlers.cpp",
    import.meta.url,
  ),
  "utf8",
);

describe("skeletal mesh skin-weight actions", () => {
  it("publishes bounded selected-vertex payloads", () => {
    expect(assetTool.schema.action.safeParse("read_skeletal_mesh_skin_weights").success).toBe(true);
    expect(assetTool.schema.action.safeParse("set_skeletal_mesh_skin_weights").success).toBe(true);

    expect(assetTool.schema.vertexIndices.safeParse([0, 17, 42]).success).toBe(true);
    expect(assetTool.schema.vertexIndices.safeParse([]).success).toBe(false);
    expect(assetTool.schema.vertexIndices.safeParse(Array.from({ length: 257 }, (_, i) => i)).success).toBe(false);

    expect(assetTool.schema.edits.safeParse([{
      vertexIndex: 17,
      influences: [
        { boneName: "upperarm_l", weight: 0.75 },
        { boneName: "clavicle_l", weight: 0.25 },
      ],
    }]).success).toBe(true);
    expect(assetTool.schema.edits.safeParse([{
      vertexIndex: 17,
      influences: [{ boneName: "upperarm_l", weight: -0.1 }],
    }]).success).toBe(false);
    expect(assetTool.schema.edits.safeParse([{
      vertexIndex: 17,
      influences: [{ boneName: "upperarm_l", rawWeight: 65535 }],
    }]).success).toBe(true);
  });

  it("routes the exact read and edit contracts", () => {
    expect(assetTool.actions.read_skeletal_mesh_skin_weights.mapParams?.({
      action: "read_skeletal_mesh_skin_weights",
      assetPath: "/Game/Characters/SK_Hero",
      vertexIndices: [2, 3],
      lodIndex: 1,
      profileName: "Default",
      unrelated: true,
    })).toEqual({
      assetPath: "/Game/Characters/SK_Hero",
      vertexIndices: [2, 3],
      lodIndex: 1,
      profileName: "Default",
    });

    const edits = [{ vertexIndex: 2, influences: [{ boneName: "root", weight: 1 }] }];
    expect(assetTool.actions.set_skeletal_mesh_skin_weights.mapParams?.({
      action: "set_skeletal_mesh_skin_weights",
      assetPath: "/Game/Characters/SK_Hero",
      edits,
      lodIndex: 0,
      restoreRawWeights: true,
      unrelated: true,
    })).toEqual({
      assetPath: "/Game/Characters/SK_Hero",
      edits,
      lodIndex: 0,
      profileName: undefined,
      restoreRawWeights: true,
    });
  });

  it("preflights the whole edit batch before the first write and only sets listed vertices", () => {
    const setterStart = handlerSource.indexOf("FSkeletalMeshHandlers::SetSkinWeights");
    const setterEnd = handlerSource.indexOf("FSkeletalMeshHandlers::ReadBuildSettings", setterStart);
    const setter = handlerSource.slice(setterStart, setterEnd);

    expect(setterStart).toBeGreaterThan(-1);
    expect(setter).toContain("MCPParseSkinWeightEdits(Params, Target, Edits)");
    expect(setter.indexOf("MCPParseSkinWeightEdits(Params, Target, Edits)")).toBeLessThan(
      setter.indexOf("Target.Mesh->Modify()"),
    );
    expect(setter).toContain("for (FMCPSkinWeightEdit& Edit : Edits)");
    expect(setter).toContain("SkinWeights.Set(FVertexID(Edit.VertexIndex), Edit.Desired)");
    expect(setter).not.toContain("Target.Description->Vertices().GetElementIDs()");

    // The parser rejects the failure modes that could otherwise half-apply a
    // valid prefix, and commit is constrained to the selected LOD/profile.
    const parserStart = handlerSource.indexOf("MCPParseSkinWeightEdits(");
    const parserEnd = handlerSource.indexOf("MCPSerializeSkinWeightEdit(", parserStart);
    const parser = handlerSource.slice(parserStart, parserEnd);
    for (const guard of [
      "duplicate vertex ID",
      "duplicate bone",
      "unknown or unsupported bone",
      "must be finite and between 0 and 1",
      "cannot be all zero",
      "quantize to all zero",
    ]) {
      expect(parser).toContain(guard);
    }
    expect(setter).toContain("MCPAssetWriteBlockedError(");
    expect(setter.indexOf("MCPAssetWriteBlockedError(")).toBeLessThan(
      setter.indexOf("Target.Mesh->Modify()"),
    );
    expect(setter).toContain("CommitParams.bUpdateSkinWeightProfiles = !Target.ProfileName.IsNone()");
    expect(setter).toContain("CommitMeshDescription(Target.LodIndex, CommitParams)");
  });

  it("uses an exact validated raw-weight rollback that bypasses only the render influence limit", () => {
    const rollbackStart = handlerSource.indexOf("MCPMakeSkinWeightRollbackPayload(");
    const rollbackEnd = handlerSource.indexOf("void FSkeletalMeshHandlers::RegisterHandlers", rollbackStart);
    const rollback = handlerSource.slice(rollbackStart, rollbackEnd);
    const parserStart = handlerSource.indexOf("MCPParseSkinWeightEdits(");
    const parserEnd = handlerSource.indexOf("MCPSerializeSkinWeightEdit(", parserStart);
    const parser = handlerSource.slice(parserStart, parserEnd);

    expect(rollback).toContain('SetNumberField(TEXT("rawWeight"), Weight.GetRawWeight())');
    expect(rollback).toContain('SetBoolField(TEXT("restoreRawWeights"), true)');
    expect(parser).toContain("restoreRawWeights must be a boolean");
    expect(parser).toContain("rawWeight values must sum to");
    expect(parser).toContain("EBoneWeightNormalizeType::None");
    expect(parser).toContain("SetMaxWeightCount(RequestedWeights.Num())");
    expect(parser).toContain("MCPReadExactSkinWeights(");
  });

  it("fails closed instead of normalizing, pruning, or losing a source snapshot", () => {
    const readStart = handlerSource.indexOf("FSkeletalMeshHandlers::ReadSkinWeights");
    const readEnd = handlerSource.indexOf("FSkeletalMeshHandlers::SetSkinWeights", readStart);
    const read = handlerSource.slice(readStart, readEnd);

    expect(handlerSource).toContain("TSharedPtr<FJsonValue> MCPReadExactSkinWeights(");
    expect(handlerSource).toContain("source rawWeight values sum to");
    expect(handlerSource).toContain("duplicate source bone index");
    expect(handlerSource).toContain("zero source rawWeight");
    expect(handlerSource).toContain("ExactSettings.SetWeightThreshold(0.0f)");
    expect(handlerSource).toContain("ExactSettings.SetMaxWeightCount(SourceCount)");
    expect(handlerSource).toContain("ExactSettings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::None)");
    expect(read).toContain("MCPReadExactSkinWeights(");
  });

  it("reports a changed-but-unsaved result with a recovery payload on an unexpected save failure", () => {
    const setterStart = handlerSource.indexOf("FSkeletalMeshHandlers::SetSkinWeights");
    const setterEnd = handlerSource.indexOf("FSkeletalMeshHandlers::ReadBuildSettings", setterStart);
    const setter = handlerSource.slice(setterStart, setterEnd);

    expect(setter).toContain('SetBoolField(TEXT("changedButUnsaved"), true)');
    expect(setter).toContain('SetBoolField(TEXT("saved"), false)');
    expect(setter).toContain('MCPSetRollback(Result, TEXT("set_skeletal_mesh_skin_weights"), MCPMakeSkinWeightRollbackPayload(Target, Edits))');
  });

  it("classifies only the setter as a write against the named mesh", () => {
    expect(classifyAction("asset.read_skeletal_mesh_skin_weights", {
      assetPath: "/Game/Characters/SK_Hero",
    }).mutates).toBe(false);
    expect(classifyAction("asset.set_skeletal_mesh_skin_weights", {
      assetPath: "/Game/Characters/SK_Hero",
    })).toMatchObject({
      mutates: true,
      paths: ["/Game/Characters/SK_Hero"],
    });
    expect(classifyWrite("set_skeletal_mesh_skin_weights", {
      assetPath: "/Game/Characters/SK_Hero",
    })).toMatchObject({
      writes: true,
      contentPaths: ["/Game/Characters/SK_Hero"],
    });
  });
});
