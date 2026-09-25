import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";

describe("level.set_component_skeletal_mesh (#1099)", () => {
  it("is exposed and routes only its own parameters", () => {
    expect(levelTool.schema.action.safeParse("set_component_skeletal_mesh").success).toBe(true);
    const action = levelTool.actions.set_component_skeletal_mesh;
    expect(action.bridge).toBe("set_component_skeletal_mesh");
    expect(action.mapParams?.({
      action: "set_component_skeletal_mesh",
      actorLabel: "Faction",
      componentName: "SkeletalMeshComponent0",
      skeletalMesh: "/Game/SK_Other",
      world: "editor",
      unrelated: 1,
    })).toEqual({
      actorLabel: "Faction",
      actorPath: undefined,
      componentName: "SkeletalMeshComponent0",
      skeletalMesh: "/Game/SK_Other",
      world: "editor",
      pieInstance: undefined,
    });
  });

  it("accepts null to clear the mesh", () => {
    expect(levelTool.schema.skeletalMesh.safeParse(null).success).toBe(true);
    const mapped = levelTool.actions.set_component_skeletal_mesh.mapParams?.({ actorLabel: "A", skeletalMesh: null });
    expect(mapped).toHaveProperty("skeletalMesh", null);
  });
});

describe("level.set_component_materials null entries (#1099)", () => {
  it("accepts a null entry, which the description promises clears one slot", () => {
    expect(levelTool.schema.materials.safeParse([null, "/Game/M_A", ""]).success).toBe(true);
    expect(levelTool.schema.materials.safeParse([1]).success).toBe(false);
  });
});
