import { describe, it, expect } from "vitest";
import { classifyWrite } from "../../src/flow/write-methods.js";

describe("classifyWrite", () => {
  it("classifies a save as a write and extracts assetPath", () => {
    const r = classifyWrite("save_asset", { assetPath: "/Game/Foo" });
    expect(r.writes).toBe(true);
    expect(r.contentPaths).toEqual(["/Game/Foo"]);
  });

  it("does not classify read verbs as writes", () => {
    for (const m of ["read_asset", "list_assets", "get_asset_properties", "search_assets", "find_references"]) {
      expect(classifyWrite(m, { assetPath: "/Game/Foo" }).writes).toBe(false);
    }
  });

  it("extracts source and destination for a move", () => {
    const r = classifyWrite("move_asset", { sourcePath: "/Game/A", destinationPath: "/Game/B" });
    expect(r.contentPaths.sort()).toEqual(["/Game/A", "/Game/B"]);
  });

  it("handles the batch-delete explicit shape", () => {
    const r = classifyWrite("delete_asset_batch", { assetPaths: ["/Game/A", "/Game/B"] });
    expect(r.writes).toBe(true);
    expect(r.contentPaths).toEqual(["/Game/A", "/Game/B"]);
  });

  it("extracts every asset from bulk property descriptors", () => {
    const r = classifyWrite("bulk_set_asset_properties", {
      items: [
        { assetPath: "/Game/A", properties: { Enabled: true } },
        { assetPath: "/Game/B", properties: { "Config.Weight": 2 } },
        { assetPath: "/Game/A", properties: { Count: 3 } },
      ],
    });
    expect(r.writes).toBe(true);
    expect(r.contentPaths).toEqual(["/Game/A", "/Game/B"]);
  });

  it("handles the bulk-rename descriptor shape", () => {
    const r = classifyWrite("bulk_rename_assets", {
      renames: [
        { sourcePath: "/Game/A", destinationPath: "/Game/A2" },
        { assetPath: "/Game/B", newName: "B2" },
      ],
    });
    expect(r.contentPaths.sort()).toEqual(["/Game/A", "/Game/B"]);
  });

  it("assembles bulk-upsert targets from packagePath + name", () => {
    const r = classifyWrite("bulk_upsert_data_assets", {
      items: [
        { name: "DA_A", packagePath: "/Game/Data/Items", className: "/Script/X.Y" },
        { name: "DA_B", packagePath: "/Game/Data/Items/", className: "/Script/X.Y" },
      ],
    });
    expect(r.writes).toBe(true);
    expect(r.contentPaths).toEqual(["/Game/Data/Items/DA_A", "/Game/Data/Items/DA_B"]);
  });

  it("treats a bulk-upsert dry run as a non-write", () => {
    const r = classifyWrite("bulk_upsert_data_assets", {
      dryRun: true,
      items: [{ name: "DA_A", packagePath: "/Game/Data/Items", className: "/Script/X.Y" }],
    });
    expect(r.writes).toBe(false);
    expect(r.contentPaths).toEqual([]);
  });

  it("is a no-op write when a write verb carries no path param", () => {
    const r = classifyWrite("save_all_dirty", { saveMapPackages: true });
    expect(r.writes).toBe(false);
    expect(r.contentPaths).toEqual([]);
  });

  it("dedupes repeated paths", () => {
    const r = classifyWrite("set_mesh_material", { assetPath: "/Game/M", path: "/Game/M" });
    expect(r.contentPaths).toEqual(["/Game/M"]);
  });

  it("ignores non-string path params", () => {
    const r = classifyWrite("save_asset", { assetPath: 123 });
    expect(r.writes).toBe(false);
  });

  it("extracts the actual Control Rig edit outputs", () => {
    expect(classifyWrite("begin_control_rig_edit", { sequencePath: "/Game/Edit/LS_A" }).contentPaths)
      .toEqual(["/Game/Edit/LS_A"]);
    expect(classifyWrite("apply_control_rig_edits", { sequencePath: "/Game/Edit/LS_A" }).contentPaths)
      .toEqual(["/Game/Edit/LS_A"]);
    expect(classifyWrite("bake_control_rig_edit", {
      sequencePath: "/Game/Edit/LS_A",
      outputAssetPath: "/Game/Edit/A_Result",
    }).contentPaths).toEqual(["/Game/Edit/A_Result"]);
  });

  it("extracts the edited IK or retargeter asset, not referenced inputs", () => {
    const cases: Array<[string, Record<string, unknown>, string]> = [
      ["configure_ik_rig", {
        rigPath: "/Game/Rigs/IK_A",
        skeletalMeshPath: "/Game/Characters/SK_A",
      }, "/Game/Rigs/IK_A"],
      ["configure_ik_retargeter", {
        retargeterPath: "/Game/Rigs/RTG_A",
        sourceRig: "/Game/Rigs/IK_Source",
        targetRig: "/Game/Rigs/IK_Target",
      }, "/Game/Rigs/RTG_A"],
      ["set_ik_rig_mesh", {
        rigPath: "/Game/Rigs/IK_A",
        meshPath: "/Game/Characters/SK_A",
      }, "/Game/Rigs/IK_A"],
      ["set_ik_retargeter_rig", {
        retargeterPath: "/Game/Rigs/RTG_A",
        rigPath: "/Game/Rigs/IK_Target",
      }, "/Game/Rigs/RTG_A"],
      ["auto_align_retarget_pose", {
        retargeterPath: "/Game/Rigs/RTG_A",
      }, "/Game/Rigs/RTG_A"],
      ["reset_retarget_pose", {
        retargeterPath: "/Game/Rigs/RTG_A",
      }, "/Game/Rigs/RTG_A"],
    ];

    for (const [method, params, expectedPath] of cases) {
      expect(classifyWrite(method, params), method).toEqual({
        writes: true,
        contentPaths: [expectedPath],
      });
    }
  });
});
