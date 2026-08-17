import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";

describe("delete_actors class-path filters", () => {
  it("forwards classPathContains and classPathContainsAny with the existing filters", () => {
    const mapped = levelTool.actions.delete_actors.mapParams!({
      action: "delete_actors",
      labelPrefix: "BP_",
      className: "LootChest",
      tag: "Vendor",
      classPathContains: "/SurvivalGameKitV2/",
      classPathContainsAny: ["/EasySwim/", "/SmartAI/"],
      dryRun: true,
    });

    expect(mapped).toEqual({
      labelPrefix: "BP_",
      className: "LootChest",
      tag: "Vendor",
      classPathContains: "/SurvivalGameKitV2/",
      classPathContainsAny: ["/EasySwim/", "/SmartAI/"],
      dryRun: true,
    });
  });

  it("declares the class-path filters on the shared schema", () => {
    expect(levelTool.schema.classPathContains.safeParse("/SurvivalGameKitV2/").success).toBe(true);
    expect(levelTool.schema.classPathContainsAny.safeParse(["/EasySwim/"]).success).toBe(true);
    expect(levelTool.schema.classPathContainsAny.safeParse("/EasySwim/").success).toBe(false);
  });
});
