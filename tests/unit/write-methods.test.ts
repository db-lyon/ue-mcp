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

  it("handles the bulk-rename descriptor shape", () => {
    const r = classifyWrite("bulk_rename_assets", {
      renames: [
        { sourcePath: "/Game/A", destinationPath: "/Game/A2" },
        { assetPath: "/Game/B", newName: "B2" },
      ],
    });
    expect(r.contentPaths.sort()).toEqual(["/Game/A", "/Game/B"]);
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
});
