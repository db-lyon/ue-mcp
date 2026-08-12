/**
 * Classification drift guard (#817, plan 5.1).
 *
 * Modelled on `drift.test.ts`: an action nobody classified fails CI rather than
 * surfacing later as a call that either edited the wrong editor or refused to
 * run in the right one. A new action lands with a verb the lexicon knows, or
 * with a line in the override table saying what it does.
 */
import { describe, it, expect } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { classifyActionClass, classifyTaskClass, requiresExplicitEditor } from "../../src/action-class.js";
import { classifyAction } from "../../src/locking.js";

/** Every `category.action` this package declares. */
function nativeActions(): Array<{ tool: string; action: string }> {
  const out: Array<{ tool: string; action: string }> = [];
  for (const t of ALL_TOOLS) {
    for (const action of Object.keys(t.actions)) out.push({ tool: t.name, action });
  }
  return out;
}

describe("action classification", () => {
  it("classifies every native action", () => {
    const unresolved = nativeActions().filter(
      (a) => classifyActionClass(a.tool, a.action).source === "unresolved",
    );

    if (unresolved.length > 0) {
      throw new Error(
        `${unresolved.length} action(s) have no read/mutate classification:\n` +
          unresolved.map((a) => `  ${a.tool}.${a.action}`).join("\n") +
          `\n\nAdd a verb to the lexicon in src/action-class.ts, or an entry in its ` +
          `override table with the reason. Untargeted dispatch beyond one editor ` +
          `depends on this answer.`,
      );
    }
    expect(unresolved).toEqual([]);
  });

  it("covers the whole surface and finds real mutations in it", () => {
    const all = nativeActions();
    expect(all.length).toBeGreaterThan(700);
    const mutating = all.filter((a) => classifyActionClass(a.tool, a.action).class === "mutate");
    // The surface is majority write-shaped. A classifier that suddenly calls
    // most of it `read` has broken, not improved.
    expect(mutating.length).toBeGreaterThan(all.length / 2);
  });

  it("never contradicts the locking classifier", () => {
    // The two answer different questions, but they cannot disagree about
    // whether something writes. The only exceptions are the session-registry
    // actions, which address the server rather than any editor.
    const REGISTRY_ACTIONS = new Set(["project.add_editor"]);
    const contradictions = nativeActions()
      .map((a) => ({ ...a, key: `${a.tool}.${a.action}` }))
      .filter((a) => !REGISTRY_ACTIONS.has(a.key))
      .filter((a) => classifyAction(a.key, {}).mutates)
      .filter((a) => classifyActionClass(a.tool, a.action).class !== "mutate");

    expect(
      contradictions.map((c) => c.key),
      "locking treats these as writes but the routing gate does not",
    ).toEqual([]);
  });

  it("gates the lifecycle actions, which are the ones that close a window", () => {
    for (const action of ["start_editor", "stop_editor", "restart_editor", "build_project"]) {
      const cls = classifyActionClass("editor", action).class;
      expect(cls, `editor.${action}`).toBe("mutate");
      expect(requiresExplicitEditor(cls)).toBe(true);
    }
  });

  it("treats an arbitrary payload as unknown, and gates it like a mutation", () => {
    for (const key of ["epic.call_tool", "editor.invoke_object_function", "animation.analyze_animation"]) {
      const cls = classifyTaskClass(key);
      expect(cls.class, key).toBe("unknown");
      expect(requiresExplicitEditor(cls.class)).toBe(true);
    }
    // `unknown` is a declared answer, not a fall-through.
    expect(classifyTaskClass("epic.call_tool").source).toBe("override");
  });

  it("lets plain reads through", () => {
    for (const key of ["project.get_status", "asset.list", "level.get_outliner", "reflection.reflect_class"]) {
      expect(classifyTaskClass(key).class, key).toBe("read");
      expect(requiresExplicitEditor("read")).toBe(false);
    }
  });

  it("reads a mutate verb anywhere in the name, not only at the front", () => {
    expect(classifyActionClass("audio", "metasound_add_node").class).toBe("mutate");
    expect(classifyActionClass("audio", "metasound_get_graph").class).toBe("read");
    expect(classifyActionClass("project", "live_coding_compile").class).toBe("mutate");
    expect(classifyActionClass("project", "live_coding_status").class).toBe("read");
  });

  it("defaults an unrecognised epic_* action to read, and only that", () => {
    const epicDefault = classifyActionClass("gas", "epic_some_tool_nobody_baked");
    expect(epicDefault).toEqual({ class: "read", source: "epic-default" });
    // A wrapped tool whose name says it writes is still a mutation.
    expect(classifyActionClass("gas", "epic_gas_toolset_create_attribute_set").class).toBe("mutate");
    // A non-epic action nobody classified stays unknown and stays gated.
    const stranger = classifyActionClass("someplugin", "frobnicate_widget");
    expect(stranger).toEqual({ class: "unknown", source: "unresolved" });
    expect(requiresExplicitEditor(stranger.class)).toBe(true);
  });

  it("does not require a target for the session-registry actions", () => {
    // These never reach a bridge: they name their subject in their own
    // parameters, so no routing decision can send them to the wrong editor.
    for (const key of ["project.list_editors", "project.use_editor", "project.add_editor", "project.drop_editor"]) {
      expect(classifyTaskClass(key).class, key).toBe("read");
    }
  });
});
