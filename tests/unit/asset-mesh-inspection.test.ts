import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { contractViolation } from "../../src/surface/handler-spec.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import type { ToolContext } from "../../src/core/types.js";

describe("static mesh inspection surface", () => {
  const accepts = (args: Record<string, unknown>) => contractViolation({ ...handlerSpecs.get_static_mesh_geometry, strict: true }, args) === undefined;
  const base = { assetPath: "/Game/Fixture/SM_TwoTriangles", geometrySource: "sourceModel" };

  it("requires an explicit representation and exposes all three tables", () => {
    expect(accepts({ assetPath: base.assetPath })).toBe(false);
    for (const geometrySource of ["sourceModel", "renderData"]) {
      for (const elementType of ["vertices", "corners", "triangles"]) {
        expect(accepts({ ...base, geometrySource, elementType })).toBe(true);
      }
    }
    expect(accepts({ ...base, geometrySource: "auto" })).toBe(false);
    expect(accepts({ ...base, elementType: "normals" })).toBe(false);
  });

  it("bounds page allocations and refuses fractional offsets", () => {
    for (const pageSize of [1, 256, 512]) expect(accepts({ ...base, pageSize })).toBe(true);
    for (const pageSize of [0, -1, 513, 1.5, "512"]) expect(accepts({ ...base, pageSize })).toBe(false);
    for (const elementOffset of [-1, 0.5, 1000001]) expect(accepts({ ...base, elementOffset })).toBe(false);
    expect(accepts({ ...base, elementOffset: 1000000 })).toBe(true);
  });

  it("forwards the snapshot identity and untouched native results", async () => {
    const params = { ...base, snapshotId: "capture-a", elementType: "corners", elementOffset: 512, pageSize: 512, lodIndex: 0 };
    const result = {
      success: true, snapshotId: "capture-a", immutableSnapshot: true, currentAssetStateChecked: false,
      contentRevision: "revision", capturedUtc: "2026-01-01T00:00:00Z", hasMore: false, nextOffset: null,
      elements: [{ id: 700, vertexId: 42, normal: { x: 0, y: 1, z: 0 }, tangent: { x: 1, y: 0, z: 0 },
        binormalSign: -1, color: { r: 0.25, g: 0.5, b: 0.75, a: 0.125 }, uvs: [{ u: 0, v: 1 }, { u: 3, v: -2 }] }],
    };
    const call = vi.fn().mockResolvedValue(result);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    expect(await assetTool.handler(ctx, { action: "get_static_mesh_geometry", ...params })).toEqual(result);
    expect(call).toHaveBeenCalledWith("get_static_mesh_geometry", params, 300_000);
    expect(classifyAction("asset.get_static_mesh_geometry", params).mutates).toBe(false);
  });

  it("forwards full export in one call with only its receipt returned", async () => {
    const params = { ...base, snapshotId: "capture-a", dumpToFile: true, outputPath: "Evidence/mesh.json" };
    expect(accepts(params)).toBe(true);
    expect(accepts({ ...params, dumpToFile: "true" })).toBe(false);
    expect(accepts({ ...params, outputPath: 1 })).toBe(false);
    const receipt = { success: true, dumpedToFile: true, exportComplete: true, snapshotId: "capture-a", contentRevision: "revision",
      currentAssetStateChecked: false, outputPath: "Saved/Evidence/mesh.json", bytes: 12345, vertexCount: 4, cornerCount: 7, triangleCount: 2 };
    const call = vi.fn().mockResolvedValue(receipt);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    expect(await assetTool.handler(ctx, { action: "get_static_mesh_geometry", ...params })).toEqual(receipt);
    expect(call).toHaveBeenCalledExactlyOnceWith("get_static_mesh_geometry", params, 300_000);
    expect(assetTool.actions.get_static_mesh_geometry.description).toContain("existing destinations are refused");
  });

  it("preserves explicit stale errors instead of retrying a new capture", async () => {
    const result = { success: false, reason: "snapshot_unavailable", error: "Restart the capture; do not combine pages." };
    const call = vi.fn().mockResolvedValue(result);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    expect(await assetTool.handler(ctx, { action: "get_static_mesh_geometry", ...base, snapshotId: "old", elementOffset: 1 })).toEqual(result);
    expect(call).toHaveBeenCalledTimes(1);
    expect(assetTool.actions.get_static_mesh_geometry.description).toContain("not current asset state");
    expect(assetTool.actions.get_static_mesh_geometry.description).toContain("fallback LOD");
  });
});
