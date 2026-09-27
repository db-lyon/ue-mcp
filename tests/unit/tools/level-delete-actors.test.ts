import { describe, expect, it } from "vitest";
import { levelTool } from "../../../src/tools/level.js";

describe("delete_actors class-path filters", () => {
  it("declares classPathContains and classPathContainsAny as filters of its selector choice", () => {
    const action = levelTool.actions.delete_actors;
    expect(action.mapParams).toBeUndefined();
    expect(action.kind === "bridge" && action.paramSpec?.map((p) => p.name)).toEqual([
      "labelPrefix", "labelContains", "nameContains", "className", "tag", "classPathContains", "classPathContainsAny", "dryRun",
    ]);
    expect(action.kind === "bridge" && action.paramChoices?.[0].branches.flat()).toContain("classPathContainsAny");
  });

  it("declares the class-path filters on the shared schema", () => {
    expect(levelTool.schema.classPathContains.safeParse("/SurvivalGameKitV2/").success).toBe(true);
    expect(levelTool.schema.classPathContainsAny.safeParse(["/EasySwim/"]).success).toBe(true);
    expect(levelTool.schema.classPathContainsAny.safeParse("/EasySwim/").success).toBe(false);
  });
});
