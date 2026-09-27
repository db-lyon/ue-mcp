import { describe, expect, it } from "vitest";
import { levelTool } from "../../../src/tools/level.js";
import { editorTool } from "../../../src/tools/editor.js";
import { RECORDED_HANDLER_SPECS } from "../../../src/tools/specs/index.js";

// A spec'd action has no mapper and forwards its bag as sent (#1057).
const forwarded = (action: string, params: Record<string, unknown>) =>
  levelTool.actions[action].mapParams?.(params) ?? params;

describe("runtime addressing by label and component name (#1113)", () => {
  it("get_actors_by_class forwards labelPrefix", () => {
    const mapped = forwarded("get_actors_by_class", { labelPrefix: "Stash_", world: "pie" });
    expect(mapped).toHaveProperty("labelPrefix", "Stash_");
    expect(mapped).toHaveProperty("world", "pie");
  });

  it("get_component_tree forwards componentName", () => {
    const mapped = forwarded("get_component_tree", { actorLabel: "GM", componentName: "StashWorld" });
    expect(mapped).toHaveProperty("componentName", "StashWorld");
  });

  it("get_runtime_values declares componentName and forwards the bag as sent", () => {
    // #1057: a spec'd action has no mapParams, so nothing it declares can be
    // dropped on the way to the bridge.
    expect(editorTool.actions.get_runtime_values.mapParams).toBeUndefined();
    const declared = RECORDED_HANDLER_SPECS.get_runtime_values.params.map((p) => p.name);
    expect(declared).toContain("componentName");
    expect(declared).toContain("paths");
  });
});
