/**
 * The verb lexicon, which guesses an effect for a plugin action whose manifest
 * declared none. Everything this package ships declares its own effect, and
 * `tests/unit/action-effects.test.ts` holds the surface to that.
 */
import { describe, it, expect } from "vitest";
import { classifyActionClass, inferActionEffect, requiresExplicitEditor } from "../../src/action-class.js";

describe("inferring an effect from a name, for the actions nobody declares", () => {
  it("reads a mutate verb anywhere in the name, not only at the front", () => {
    // `metasound_add_node` adds a node; `cue_get_graph` does not.
    expect(inferActionEffect("audio", "metasound_add_node")).toBe("mutate");
    expect(inferActionEffect("audio", "live_coding_compile")).toBe("mutate");
    expect(inferActionEffect("audio", "get_graph")).toBe("read");
  });

  it("trusts a read verb only in the leading segment", () => {
    // `wire_rvt_sample` added a sampler node and read as a READ while any read
    // verb anywhere in the name counted. A trailing read verb settles nothing.
    expect(classifyActionClass("wire_rvt_sample")).toEqual({
      class: "unknown",
      source: "unresolved",
    });
    expect(classifyActionClass("read_runtime_virtual_texture")).toEqual({
      class: "read",
      source: "lexicon",
    });
  });

  it("gives an epic_ name no special treatment", () => {
    // A plugin action named like a wrapped engine tool is still only a name.
    expect(classifyActionClass("epic_some_tool_nobody_baked")).toEqual({
      class: "unknown",
      source: "unresolved",
    });
    expect(inferActionEffect("gas", "epic_gas_toolset_create_attribute_set")).toBe("mutate");
  });

  it("answers unknown for a plugin action whose name says nothing", () => {
    expect(classifyActionClass("frobnicate_widget")).toEqual({
      class: "unknown",
      source: "unresolved",
    });
    expect(requiresExplicitEditor("unknown")).toBe(true);
  });

  it("gates every class that is not a read", () => {
    expect(requiresExplicitEditor("read")).toBe(false);
    expect(requiresExplicitEditor("mutate")).toBe(true);
    expect(requiresExplicitEditor("unknown")).toBe(true);
  });
});
