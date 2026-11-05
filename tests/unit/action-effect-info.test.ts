/**
 * The effect and destructive flag a client reads per action: the effect from
 * the dispatched graph, `destructive` from the spec, else from the leading
 * verb for anything that is not a read.
 */
import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { actionDestructive, actionEffectInfo } from "../../src/surface/action-effects.js";
import { DESTRUCTIVE_VERBS, leadingVerb } from "../../src/surface/action-verbs.js";
import type { ToolDef } from "../../src/core/types.js";

const info = actionEffectInfo(ALL_TOOLS, ALL_TOOLS);
const of = (category: string, action: string) => info.find((r) => r.category === category && r.action === action)!;

describe("leadingVerb", () => {
  it("reads the first segment past a qualifier", () => {
    expect(leadingVerb("delete_actor")).toBe("delete");
    expect(leadingVerb("epic_remove_node")).toBe("remove");
    expect(leadingVerb("bulk_delete")).toBe("delete");
    expect(leadingVerb("metasound_remove_node")).toBe("metasound");
    expect(leadingVerb("")).toBe("");
  });

  it("keeps the destructive list to the verbs that always destroy", () => {
    expect([...DESTRUCTIVE_VERBS].sort()).toEqual([
      "clear", "delete", "destroy", "discard", "drop", "overwrite", "purge", "remove", "replace", "reset", "restore", "revert", "uninstall", "wipe",
    ]);
  });
});

describe("actionEffectInfo", () => {
  it("lists every action with the effect its spec declares", () => {
    expect(info.length).toBe(ALL_TOOLS.reduce((n, t) => n + Object.keys(t.actions).length, 0));
    for (const tool of ALL_TOOLS) {
      for (const [action, spec] of Object.entries(tool.actions)) {
        expect(of(tool.name, action)).toMatchObject({ effect: spec.effect, effectSource: spec.effectSource ?? "declared" });
      }
    }
  });

  it("never calls a read destructive", () => {
    for (const r of info.filter((x) => x.effect === "read")) {
      expect(r.destructive, `${r.category}.${r.action}`).toBe(false);
    }
  });

  it("uses the lexicon only for what a spec leaves unsaid", () => {
    expect(of("asset", "delete")).toMatchObject({ destructive: true, destructiveSource: "lexicon" });
    expect(of("level", "place_actor")).toMatchObject({ destructive: false, destructiveSource: "lexicon" });
    expect(of("level", "get_outliner")).toMatchObject({ destructive: false, destructiveSource: "none" });
    // Declared where the leading verb is wrong, in both directions.
    expect(of("project", "drop_editor")).toMatchObject({ destructive: false, destructiveSource: "declared" });
    expect(of("level", "restore_component_transform")).toMatchObject({ destructive: false, destructiveSource: "declared" });
    expect(of("audio", "metasound_remove_node")).toMatchObject({ destructive: true, destructiveSource: "declared" });
    expect(of("pie", "script_delete")).toMatchObject({ destructive: true, destructiveSource: "declared" });
    expect(of("source_control", "mark_for_delete")).toMatchObject({ destructive: true, destructiveSource: "declared" });
  });

  it("narrows to categories and refuses an unknown one", () => {
    const some = actionEffectInfo(ALL_TOOLS, ALL_TOOLS, ["fab"]);
    expect(new Set(some.map((r) => r.category))).toEqual(new Set(["fab"]));
    expect(() => actionEffectInfo(ALL_TOOLS, ALL_TOOLS, ["fab", "nope"])).toThrow(/no such category: nope/);
  });

  it("reads a plugin action's inferred effect from the dispatched graph", () => {
    const plugin: ToolDef = {
      name: "plug",
      description: "x",
      schema: {},
      handler: async () => ({}),
      actions: {
        wipe_cache: { kind: "handler", effect: "mutate", effectSource: "inferred", handler: async () => ({}) },
        keep_all: { kind: "handler", effect: "unknown", effectSource: "inferred", handler: async () => ({}) },
      },
    } as unknown as ToolDef;
    const rows = actionEffectInfo([plugin], [plugin]);
    expect(rows).toEqual([
      { category: "plug", action: "wipe_cache", effect: "mutate", effectSource: "inferred", destructive: true, destructiveSource: "lexicon" },
      { category: "plug", action: "keep_all", effect: "unknown", effectSource: "inferred", destructive: false, destructiveSource: "lexicon" },
    ]);
    // Asked of a graph that does not carry it, the action is undeclared and treated as a change.
    expect(actionEffectInfo([plugin], [])[0]).toMatchObject({ effect: "mutate", effectSource: "undeclared" });
  });

  it("lets a declaration outrank the effect", () => {
    expect(actionDestructive("delete_x", { destructive: false }, "mutate")).toEqual({ destructive: false, source: "declared" });
    expect(actionDestructive("get_x", { destructive: true }, "read")).toEqual({ destructive: true, source: "declared" });
    expect(actionDestructive("remove_x", {}, "unknown")).toEqual({ destructive: true, source: "lexicon" });
  });
});
