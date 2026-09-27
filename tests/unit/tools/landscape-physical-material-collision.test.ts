import { describe, expect, it } from "vitest";
import { landscapeTool } from "../../../src/tools/landscape.js";

describe("landscape.refresh_physical_material_collision", () => {
  it("is exposed and takes its target parameters from its spec", () => {
    const action = landscapeTool.actions.refresh_physical_material_collision;
    expect(landscapeTool.schema.action.safeParse("refresh_physical_material_collision").success).toBe(true);
    expect(action.bridge).toBe("refresh_landscape_physical_material_collision");
    expect(action.timeoutMs).toBe(600_000);
    expect(action.description).toContain("no pending landscape edit-layer work");
    expect(action.description).toContain("every raw, complex-live, and simple-live height sample");
    expect(action.description).toContain("Persistence is deliberately unsupported");
    expect(action.mapParams).toBeUndefined();
    expect(action.kind === "bridge" && action.paramSpec?.map((p) => p.name)).toEqual(
      ["actorLabels", "guids", "bounds", "maxActors", "save"],
    );
  });

  it("types the target lists and safety cap; the handler enforces their ranges", () => {
    expect(landscapeTool.schema.actorLabels.safeParse(["Proxy_A"]).success).toBe(true);
    expect(landscapeTool.schema.actorLabels.safeParse([1]).success).toBe(false);
    expect(landscapeTool.schema.guids.safeParse(["16A4CE244E435B01995B28A15C81A661"]).success).toBe(true);
    expect(landscapeTool.schema.maxActors.safeParse(1024).success).toBe(true);
    expect(landscapeTool.schema.maxActors.safeParse(1.5).success).toBe(false);
  });

  it("requires complete numeric bounds and accepts only save=false", () => {
    expect(landscapeTool.schema.bounds.safeParse({
      min: { x: -100, y: -100, z: -10 },
      max: { x: 100, y: 100, z: 10 },
    }).success).toBe(true);
    expect(landscapeTool.schema.bounds.safeParse({ min: { x: 0, y: 0 }, max: { x: 1, y: 1, z: 1 } }).success).toBe(false);
    expect(landscapeTool.schema.save.safeParse(false).success).toBe(true);
    expect(landscapeTool.schema.save.safeParse(true).success).toBe(false);
  });
});
