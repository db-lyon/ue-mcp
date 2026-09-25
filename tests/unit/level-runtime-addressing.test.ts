import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";
import { editorTool } from "../../src/tools/editor.js";

describe("runtime addressing by label and component name (#1113)", () => {
  it("get_actors_by_class forwards labelPrefix", () => {
    const mapped = levelTool.actions.get_actors_by_class.mapParams?.({ labelPrefix: "Stash_", world: "pie" });
    expect(mapped).toHaveProperty("labelPrefix", "Stash_");
    expect(mapped).toHaveProperty("world", "pie");
  });

  it("get_component_tree forwards componentName", () => {
    const mapped = levelTool.actions.get_component_tree.mapParams?.({ actorLabel: "GM", componentName: "StashWorld" });
    expect(mapped).toHaveProperty("componentName", "StashWorld");
  });

  it("get_runtime_values forwards componentName", () => {
    const mapped = editorTool.actions.get_runtime_values.mapParams?.({
      componentName: "StashWorld",
      paths: ["SnapAnchors"],
    });
    expect(mapped).toHaveProperty("componentName", "StashWorld");
    expect(mapped).toHaveProperty("paths", ["SnapAnchors"]);
  });
});
