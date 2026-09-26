import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { classifyWrite } from "../../src/flow/write-methods.js";

describe("asset.set_mesh_materials_batch", () => {
  it("is exposed through the asset action schema", () => {
    expect(assetTool.schema.action.safeParse("set_mesh_materials_batch").success).toBe(true);
  });

  it("accepts slot addressing by name and by index", () => {
    const assignments = [
      { assetPath: "/Game/Kit/SM_Deco_00", materialPath: "/Game/Kit/M_Facade", slotName: "Facade" },
      { assetPath: "/Game/Kit/SM_Deco_00", materialPath: "/Game/Kit/M_Trim", slotIndex: 1 },
      { assetPath: "/Game/Kit/SK_Char", materialPath: "/Game/Kit/M_Skin" },
    ];
    expect(assetTool.schema.assignments.safeParse(assignments).success).toBe(true);
  });

  it("rejects empty, oversized, and malformed batches", () => {
    expect(assetTool.schema.assignments.safeParse([]).success).toBe(false);
    expect(assetTool.schema.assignments.safeParse(
      Array.from({ length: 501 }, (_, i) => ({ assetPath: `/Game/A${i}`, materialPath: "/Game/M" })),
    ).success).toBe(false);
    expect(assetTool.schema.assignments.safeParse([{ assetPath: "/Game/A" }]).success).toBe(false);
    expect(assetTool.schema.assignments.safeParse([{ assetPath: "", materialPath: "/Game/M" }]).success).toBe(false);
  });

  it("forwards its bag as sent, as its C++ spec declares (#1057)", () => {
    const action = assetTool.actions.set_mesh_materials_batch;
    expect(action.bridge).toBe("set_mesh_materials_batch");
    expect(action.mapParams).toBeUndefined();
    expect(action.description).toContain("Params: assignments, save?, dryRun?, continueOnError?");
  });

  it("exposes continueOnError so a partial batch is reachable", () => {
    expect(assetTool.schema.continueOnError.safeParse(true).success).toBe(true);
    expect(assetTool.schema.continueOnError.safeParse("yes").success).toBe(false);
    // Omitting it must stay valid: the default is the all-or-nothing batch.
    expect(assetTool.schema.continueOnError.safeParse(undefined).success).toBe(true);
  });

  it("documents slot-name addressing and the per-item contract", () => {
    const description = assetTool.actions.set_mesh_materials_batch.description ?? "";
    expect(description).toContain("slotName");
    expect(description).toContain("continueOnError");
    expect(description).toMatch(/its own index\/ok\/status\/error/);
  });

  it("locks every mesh in the batch, and not the materials", () => {
    const c = classifyAction("asset.set_mesh_materials_batch", {
      assignments: [
        { assetPath: "/Game/Kit/SM_A", materialPath: "/Game/Kit/M_Facade", slotName: "Facade" },
        { assetPath: "/Game/Kit/SM_B", materialPath: "/Game/Kit/M_Facade", slotIndex: 0 },
      ],
    });
    expect(c.mutates).toBe(true);
    expect(c.paths.sort()).toEqual(["/Game/Kit/SM_A", "/Game/Kit/SM_B"]);
  });

  it("checks out every mesh in the batch for the source-control guard", () => {
    const r = classifyWrite("set_mesh_materials_batch", {
      assignments: [
        { assetPath: "/Game/Kit/SM_A", materialPath: "/Game/Kit/M_Facade", slotIndex: 0 },
        { assetPath: "/Game/Kit/SM_A", materialPath: "/Game/Kit/M_Trim", slotIndex: 1 },
        { assetPath: "/Game/Kit/SM_B", materialPath: "/Game/Kit/M_Facade", slotIndex: 0 },
      ],
    });
    expect(r.writes).toBe(true);
    expect(r.contentPaths).toEqual(["/Game/Kit/SM_A", "/Game/Kit/SM_B"]);
  });
});
