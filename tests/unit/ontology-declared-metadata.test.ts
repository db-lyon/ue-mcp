import { describe, it, expect } from "vitest";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import {
  createHandlerRegistryProjector,
  parseKant,
  compose,
  select,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/emit.js";

function buildProjectedView(tools: ToolDef[]) {
  const proj = createHandlerRegistryProjector(tools);
  const text = serializeFragment(proj.project(undefined as never));
  const frag = parseKant(text, "projected");
  return compose([{ priority: 1, fragment: frag }]);
}

describe("ontology: declared metadata overrides heuristic", () => {
  it("explicit classification wins over name-prefix heuristic", () => {
    const editor = categoryTool("editor", "Editor control.", {
      // Starts with 'get' → heuristic says 'read'. We declare otherwise.
      get_status: { bridge: "editor_get_status", classification: "mutate", approval: "required", risk: "severe" },
      // No declaration → heuristic wins.
      delete_actor: bp("Delete", "delete_actor"),
    });

    const view = buildProjectedView([editor]);
    const actions = view.root.children!.Mediation.children!.Registry.children!.Tools.children!.editor.children!.Actions.children!;

    const status = actions.get_status.fields!;
    expect((status.classification as { marker?: string }).marker).toBe("mutate");
    expect((status.approval as { marker?: string }).marker).toBe("required");
    expect((status.risk as { marker?: string }).marker).toBe("severe");
    expect(status.metadataSource).toBe("declared");

    const del = actions.delete_actor.fields!;
    expect((del.classification as { marker?: string }).marker).toBe("destructive");
    expect(del.metadataSource).toBe("heuristic");
  });

  it("category-level defaults.requires applies to every action that lacks its own", () => {
    const niagara = categoryTool(
      "niagara",
      "Niagara VFX.",
      {
        spawn: bp("Spawn", "spawn_niagara"),
        list: bp("List", "list_niagara"),
        custom: { bridge: "custom", requires: ["Niagara", "NiagaraExtras"] },
      },
      undefined,
      undefined,
      { requires: ["Niagara"] },
    );

    const view = buildProjectedView([niagara]);
    const actions = view.root.children!.Mediation.children!.Registry.children!.Tools.children!.niagara.children!.Actions.children!;

    // Inherited from category default.
    expect(Object.keys(actions.spawn.children!.requires.children!)).toEqual(["Niagara"]);
    expect(Object.keys(actions.list.children!.requires.children!)).toEqual(["Niagara"]);
    // Per-action declaration wins - extra plugin present.
    expect(Object.keys(actions.custom.children!.requires.children!).sort()).toEqual(["Niagara", "NiagaraExtras"]);
  });

  it("requires subtree is queryable via the path selector", () => {
    const gas = categoryTool(
      "gas",
      "GAS.",
      { create_ability: bp("Create", "create_ability") },
      undefined,
      undefined,
      { requires: ["GameplayAbilities"] },
    );

    const view = buildProjectedView([gas]);
    const hits = select(
      view.root,
      "/UE/Mediation/Registry/Tools/gas/Actions/create_ability/requires/*",
    );
    expect(hits.map((h) => h.path.split("/").pop()).sort()).toEqual(["GameplayAbilities"]);
  });

  it("predicate-filters actions declared as destructive+explicit", () => {
    const editor = categoryTool("editor", "Editor control.", {
      execute_python: {
        bridge: "execute_python",
        classification: "destructive",
        approval: "explicit",
        risk: "catastrophic",
      },
      get_status: bp("Read", "editor_get_status"),
    });
    const view = buildProjectedView([editor]);
    const hits = select(
      view.root,
      "/UE/Mediation/Registry/Tools/*/Actions/*@classification=destructive",
    );
    expect(hits.map((h) => h.path.split("/").pop())).toEqual(["execute_python"]);

    const explicit = select(
      view.root,
      "/UE/Mediation/Registry/Tools/*/Actions/*@approval=explicit",
    );
    expect(explicit.map((h) => h.path.split("/").pop())).toEqual(["execute_python"]);
  });
});
