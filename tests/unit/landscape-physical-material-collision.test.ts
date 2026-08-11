import { describe, expect, it } from "vitest";
import { landscapeTool } from "../../src/tools/landscape.js";

describe("landscape.refresh_physical_material_collision", () => {
  it("is exposed and forwards only its bounded target parameters", () => {
    const action = landscapeTool.actions.refresh_physical_material_collision;
    expect(landscapeTool.schema.action.safeParse("refresh_physical_material_collision").success).toBe(true);
    expect(action.bridge).toBe("refresh_landscape_physical_material_collision");
    expect(action.timeoutMs).toBe(600_000);
    expect(action.description).toContain("no pending landscape edit-layer work");
    expect(action.description).toContain("every raw, complex-live, and simple-live height sample");
    expect(action.description).toContain("Persistence is deliberately unsupported");
    expect(action.mapParams?.({
      action: "refresh_physical_material_collision",
      actorLabels: ["LandscapeStreamingProxy_2_3"],
      guids: ["16A4CE244E435B01995B28A15C81A661"],
      bounds: { min: { x: 0, y: 1, z: 2 }, max: { x: 3, y: 4, z: 5 } },
      maxActors: 8,
      unrelated: "must not reach the bridge",
    })).toEqual({
      actorLabels: ["LandscapeStreamingProxy_2_3"],
      guids: ["16A4CE244E435B01995B28A15C81A661"],
      bounds: { min: { x: 0, y: 1, z: 2 }, max: { x: 3, y: 4, z: 5 } },
      maxActors: 8,
    });
  });

  it("keeps the target lists and safety cap bounded", () => {
    expect(landscapeTool.schema.actorLabels.safeParse(["Proxy_A"]).success).toBe(true);
    expect(landscapeTool.schema.actorLabels.safeParse([]).success).toBe(false);
    expect(landscapeTool.schema.actorLabels.safeParse(Array.from({ length: 257 }, (_, i) => `Proxy_${i}`)).success).toBe(false);
    expect(landscapeTool.schema.guids.safeParse(["16A4CE244E435B01995B28A15C81A661"]).success).toBe(true);
    expect(landscapeTool.schema.maxActors.safeParse(1).success).toBe(true);
    expect(landscapeTool.schema.maxActors.safeParse(1024).success).toBe(true);
    expect(landscapeTool.schema.maxActors.safeParse(0).success).toBe(false);
    expect(landscapeTool.schema.maxActors.safeParse(1025).success).toBe(false);
  });

  it("requires complete numeric bounds and does not expose package saving", () => {
    expect(landscapeTool.schema.bounds.safeParse({
      min: { x: -100, y: -100, z: -10 },
      max: { x: 100, y: 100, z: 10 },
    }).success).toBe(true);
    expect(landscapeTool.schema.bounds.safeParse({ min: { x: 0, y: 0 }, max: { x: 1, y: 1, z: 1 } }).success).toBe(false);
    expect("save" in landscapeTool.schema).toBe(false);
  });
});
