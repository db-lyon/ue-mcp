import { describe, expect, it } from "vitest";
import { searchToolGraph } from "../../src/surface/context/tool-search.js";
import { ALL_TOOLS } from "../../src/tools.js";

const ids = (query: string, limit = 20) =>
  searchToolGraph(ALL_TOOLS, query, limit).map((hit) => `${hit.tool}.${hit.action}`);
const firstParty = (query: string) => ids(query).filter((id) => !id.split(".")[1].startsWith("epic_"));

describe("searchToolGraph", () => {
  it("keeps matching Epic gateway actions distinct", () => {
    const hits = searchToolGraph([{
      name: "animation",
      actions: {
        epic_import_animation: {
          bridge: "epic_call_tool",
          description: "Import an animation asset.",
        },
        epic_import_pose: {
          bridge: "epic_call_tool",
          description: "Import a pose asset.",
        },
      },
    }], "import");

    expect(hits.map((hit) => hit.action)).toEqual(["epic_import_animation", "epic_import_pose"]);
  });

  it("collapses ordinary bridge aliases to the best-scoring name", () => {
    const hits = searchToolGraph([{
      name: "asset",
      actions: {
        import_asset: {
          bridge: "import_content",
          description: "Import an asset.",
        },
        import_texture: {
          bridge: "import_content",
          description: "Import texture asset.",
        },
      },
    }], "import texture");

    expect(hits.map((hit) => hit.action)).toEqual(["import_texture"]);
  });

  it("ranks a first-party action ahead of an Epic wrapper on a tied score", () => {
    const hits = searchToolGraph([{
      name: "widget",
      actions: {
        epic_make_widget: { bridge: "epic_call_tool", description: "Make a widget." },
        make_widget: { bridge: "make_widget", description: "Make a widget." },
      },
    }], "widget");

    expect(hits.map((hit) => hit.action)).toEqual(["make_widget", "epic_make_widget"]);
  });

  it("ranks Epic wrappers past the per-tool cap after every other hit, not out of the results", () => {
    const hits = searchToolGraph([{
      name: "animation",
      actions: {
        epic_import_a: { bridge: "epic_call_tool", description: "Import one." },
        epic_import_b: { bridge: "epic_call_tool", description: "Import two." },
        epic_import_c: { bridge: "epic_call_tool", description: "Import three." },
        load_clip: { bridge: "load_clip", description: "Load a clip." },
      },
    }], "import");

    expect(hits.map((hit) => hit.action)).toEqual(["epic_import_a", "epic_import_b", "load_clip", "epic_import_c"]);
  });

  describe("ranking signals", () => {
    const bp = (actions: Record<string, { description: string; effect: string }>) => [{ name: "blueprint", actions }];

    it("matches whole words, so 'movement' is not 'move' and 'count' is not 'account'", () => {
      const graph = bp({
        reorder_enum_values: { description: "Reorder enum values.", effect: "mutate" },
        manage_account: { description: "Manage the account.", effect: "mutate" },
      });
      expect(searchToolGraph(graph, "movement component")).toEqual([]);
      expect(searchToolGraph(graph, "count")).toEqual([]);
    });

    it("demotes mutating actions when the ask only reads", () => {
      const graph = bp({
        edit_widget_tree: { description: "Edit it.", effect: "mutate" },
        show_widget_tree: { description: "Show it.", effect: "read" },
      });
      expect(searchToolGraph(graph, "check widget tree").map((h) => h.action)).toEqual(["show_widget_tree", "edit_widget_tree"]);
      // An ask that also writes keeps both on equal footing, in declaration order.
      expect(searchToolGraph(graph, "check then add widget tree").map((h) => h.action)).toEqual(["edit_widget_tree", "show_widget_tree"]);
    });
  });

  describe("gate candidates for reading a component off a Blueprint", () => {
    // What the execute_python gate lists: the top five with a score of at least 4.
    const gateCandidates = (summary: string) =>
      searchToolGraph(ALL_TOOLS, summary, 5).filter((h) => h.score >= 4).map((h) => `${h.tool}.${h.action}`);

    const ask = {
      exact:
        "Read each vehicle Blueprint CDO's movement component class, automatic gears, wheel count and NetworkPhysicsSettings component",
      movement: "read the movement component on a Blueprint",
      inherited: "read inherited component properties on several Blueprints",
      structField: "check a component's struct field on a Blueprint class default",
    };

    it.each(Object.entries(ask))("lists read_component_properties for: %s", (_name, summary) => {
      expect(gateCandidates(summary)).toContain("blueprint.read_component_properties");
    });

    it("does not let incidental words pull in enum/struct reorder or mass config", () => {
      const listed = gateCandidates(ask.exact);
      expect(listed).not.toContain("blueprint.reorder_enum_values");
      expect(listed).not.toContain("blueprint.reorder_struct_fields");
      expect(listed).not.toContain("gameplay.read_mass_entity_config");
    });

    it("does not surface read_component_properties for unrelated tasks", () => {
      const reorder = gateCandidates("reorder the values of a user defined enum");
      expect(reorder).toContain("blueprint.reorder_enum_values");
      expect(reorder).not.toContain("blueprint.read_component_properties");
      expect(gateCandidates("capture a screenshot of the viewport")).not.toContain("blueprint.read_component_properties");
    });

    it("says which components the two readers cover, and that struct fields come back", () => {
      const actions = ALL_TOOLS.find((t) => t.name === "blueprint")!.actions;
      for (const name of ["read_component_properties", "get_component_property"]) {
        const text = actions[name].description!;
        expect(text, name).toMatch(/inherited/);
        expect(text, name).toMatch(/native C\+\+ default-subobject/);
        expect(text, name).toMatch(/struct field/);
      }
    });
  });

  describe("against the shipped graph", () => {
    it("keeps first-party actions on the default page for broad queries", () => {
      expect(firstParty("create widget").length).toBeGreaterThanOrEqual(15);
      expect(firstParty("material parameter").length).toBeGreaterThanOrEqual(15);
      expect(ids("create widget", 5)).toContain("widget.create");
      expect(ids("material parameter", 5)).toEqual(expect.arrayContaining(["material.set_parameter", "material.list_parameters"]));
    });

    it("finds set_sequence_keyframes in the top five for a sequencer keyframe query", () => {
      expect(ids("add keyframe sequencer", 5)).toContain("editor.set_sequence_keyframes");
    });

    it("still surfaces distinct Epic actions when they are the best match", () => {
      expect(ids("replace widget with template", 2)).toEqual([
        "widget.epic_replace_widget_with_template",
        "widget.epic_replace_widget_with_child",
      ]);
      // "frame" is in the capture synonym group, so editor.render_sequence_frames
      // legitimately ranks here too; both Epic actions must still surface.
      expect(ids("marked frame", 3)).toEqual(expect.arrayContaining([
        "animation.epic_add_marked_frame",
        "animation.epic_delete_all_marked_frames",
      ]));
    });
  });
});
