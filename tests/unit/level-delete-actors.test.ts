import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";

describe("delete_actors class-path filters", () => {
  it("declares classPathContains and classPathContainsAny as filters of its selector choice", () => {
    const action = levelTool.actions.delete_actors;
    expect(action.mapParams).toBeUndefined();
    expect(action.kind === "bridge" && action.paramSpec?.map((p) => p.name)).toEqual([
      "labelPrefix", "labelContains", "nameContains", "className", "tag", "classPathContains", "classPathContainsAny",
      "folderPath", "folderPathPrefix", "dryRun",
    ]);
    expect(action.kind === "bridge" && action.paramChoices?.[0].branches.flat()).toContain("classPathContainsAny");
  });

  it("accepts an outliner folder as a selector on its own (#1529)", () => {
    const action = levelTool.actions.delete_actors;
    const branches = action.kind === "bridge" ? action.paramChoices?.[0].branches.flat() : [];
    expect(branches).toEqual(expect.arrayContaining(["folderPath", "folderPathPrefix"]));
  });

  it("declares the class-path filters on the shared schema", () => {
    expect(levelTool.schema.classPathContains.safeParse("/SurvivalGameKitV2/").success).toBe(true);
    expect(levelTool.schema.classPathContainsAny.safeParse(["/EasySwim/"]).success).toBe(true);
    expect(levelTool.schema.classPathContainsAny.safeParse("/EasySwim/").success).toBe(false);
  });
});
