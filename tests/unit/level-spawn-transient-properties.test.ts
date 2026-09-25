import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";

describe("level.spawn_transient_actor properties (#1103)", () => {
  it("forwards properties to the bridge instead of dropping them", () => {
    const properties = {
      SpawnAnchorTag: "District.Dockworks",
      AnchorTags: ["District.Dockworks", "District.Harbour"],
    };
    const mapped = levelTool.actions.spawn_transient_actor.mapParams?.({
      actorClass: "NodeEntranceActor",
      initialize: "construction",
      properties,
    });
    expect(mapped).toHaveProperty("properties", properties);
  });

  it("documents the per-property report", () => {
    const description = levelTool.actions.spawn_transient_actor.description ?? "";
    expect(description).toContain("properties?");
    expect(description).toContain("properties[]");
  });
});
