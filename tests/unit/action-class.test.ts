/**
 * What is left of the verb lexicon.
 *
 * This file used to be the drift guard over the whole surface: every action
 * had to land on a verb the lexicon knew or an entry in an override table, and
 * a new one that did neither failed CI. That was the best available answer
 * while the answer came from a name, and it was still a guess. An action
 * declares its effect now, `tests/unit/action-effects.test.ts` holds the
 * surface to it, and the lexicon's job shrank to one thing: reading a name
 * this package never declares.
 *
 * There are exactly two callers of it, both at construction time, both
 * recording what they get as `inferred`:
 *
 *   Epic enrichment, which injects wrapped engine tools read out of a live
 *   registry that can carry toolsets no release has shipped against.
 *   Plugin injection, for an action whose manifest did not say what it does.
 *
 * Nothing consults it at a gate any more. A name nobody declares is a
 * mutation, which is `action-effects.ts`'s answer and not this one's.
 */
import { describe, it, expect } from "vitest";
import { classifyActionClass, inferActionEffect, requiresExplicitEditor } from "../../src/action-class.js";

describe("inferring an effect from a name, for the actions nobody declares", () => {
  it("reads a mutate verb anywhere in the name, not only at the front", () => {
    // `metasound_add_node` adds a node; `cue_get_graph` does not.
    expect(inferActionEffect("audio", "metasound_add_node")).toBe("mutate");
    expect(inferActionEffect("audio", "metasound_get_graph")).toBe("read");
    expect(inferActionEffect("project", "live_coding_compile")).toBe("mutate");
    expect(inferActionEffect("project", "live_coding_status")).toBe("read");
  });

  it("trusts a read verb only in the leading segment", () => {
    // The case the rule was written from. `wire_rvt_sample` added a sampler
    // node to a material and read as a READ, because `sample` is a read verb
    // and the rule accepted one anywhere in the name. A trailing read verb
    // settles nothing now and falls through to `unknown`, which is gated like
    // a mutation.
    expect(classifyActionClass("material", "wire_rvt_sample")).toEqual({
      class: "unknown",
      source: "unresolved",
    });
    expect(requiresExplicitEditor("unknown")).toBe(true);
    // A leading read verb still settles it.
    expect(classifyActionClass("material", "read_runtime_virtual_texture")).toEqual({
      class: "read",
      source: "lexicon",
    });
  });

  it("defaults an unrecognised epic_* tool to read, and only that one", () => {
    // Epic's surface is overwhelmingly read-shaped and a wrapped tool that
    // writes almost always says so in its name. Defaulting the rest to a
    // change would make every unbaked Epic action a hard refusal for
    // multi-editor users, which is the trade plan 5.1 made deliberately.
    expect(classifyActionClass("gas", "epic_some_tool_nobody_baked")).toEqual({
      class: "read",
      source: "epic-default",
    });
    // A wrapped tool whose name says it writes is still a mutation.
    expect(inferActionEffect("gas", "epic_gas_toolset_create_attribute_set")).toBe("mutate");
  });

  it("answers unknown for a plugin action whose name says nothing", () => {
    // Recorded as `inferred` by the injector, so the guess is never read back
    // later as a declaration.
    expect(classifyActionClass("someplugin", "frobnicate_widget")).toEqual({
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
