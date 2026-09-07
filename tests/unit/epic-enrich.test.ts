import { describe, it, expect } from "vitest";
import { actionEnumValues, categoryTool, bp, type ToolDef } from "../../src/types.js";
import {
  routeToolset,
  enrichToolsWithEpicCatalog,
  type EpicCatalog,
} from "../../src/epic-enrich.js";

function fixtureTools(): ToolDef[] {
  return [
    categoryTool("gas", "GAS", { grant_ability: bp("read", "grant", "grant_ability") }, undefined, {}),
    categoryTool("niagara", "Niagara", { spawn: bp("read", "spawn", "spawn_system") }, undefined, {}),
    categoryTool("epic", "Epic gateway", { status: bp("read", "status", "epic_status") }, undefined, {}),
  ];
}

const CATALOG: EpicCatalog = {
  toolsets: [
    {
      name: "GASToolsets.AttributeSetToolset",
      tools: [
        {
          name: "GASToolsets.AttributeSetToolset.ListAttributes",
          description: "List attributes on a set.",
          inputSchema: { properties: { className: {} }, required: ["className"] },
        },
        {
          name: "GASToolsets.AttributeSetToolset.FindAttributeSetClasses",
          inputSchema: { properties: {} },
        },
      ],
    },
    {
      name: "NiagaraToolsets.NiagaraToolset_System",
      tools: [{ name: "NiagaraToolsets.NiagaraToolset_System.ListSystems" }],
    },
    {
      // Epic-only domain: ue-mcp has no native handlers, so enrichment
      // materialises the category rather than dumping it in the umbrella.
      name: "conversation_toolset.toolsets.conversation.ConversationTools",
      tools: [{ name: "conversation_toolset.toolsets.conversation.ConversationTools.ListSpeakers" }],
    },
    {
      // Registry meta-tooling: belongs to the registry itself, not an editor
      // domain, so the epic umbrella is its correct home.
      name: "ToolsetRegistry.AgentSkillToolset",
      tools: [{ name: "ToolsetRegistry.AgentSkillToolset.ListSkills" }],
    },
  ],
};

describe("routeToolset", () => {
  it("routes known toolsets to their ue-mcp category", () => {
    expect(routeToolset("GASToolsets.AttributeSetToolset")).toBe("gas");
    expect(routeToolset("NiagaraToolsets.NiagaraToolset_System")).toBe("niagara");
    expect(routeToolset("PCGToolset.PCGToolset")).toBe("pcg");
    expect(routeToolset("UMGToolSet.UMGToolSet")).toBe("widget");
    expect(routeToolset("editor_toolset.toolsets.actor.ActorTools")).toBe("level");
    expect(routeToolset("editor_toolset.toolsets.asset.AssetTools")).toBe("asset");
    expect(routeToolset("editor_toolset.toolsets.blueprint.BlueprintTools")).toBe("blueprint");
    expect(routeToolset("animation_toolset.toolsets.sequencer.SequencerTools")).toBe("animation");
  });

  it("routes the editor/engine toolsets to their canonical category, not the umbrella", () => {
    expect(routeToolset("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools")).toBe("animation");
    expect(routeToolset("editor_toolset.toolsets.static_mesh.StaticMeshTools")).toBe("asset");
    expect(routeToolset("editor_toolset.toolsets.texture.TextureTools")).toBe("asset");
    expect(routeToolset("editor_toolset.toolsets.data_table.DataTableTools")).toBe("asset");
    expect(routeToolset("editor_toolset.toolsets.string_table.StringTableTools")).toBe("asset");
    expect(routeToolset("SemanticSearchToolset.SemanticSearchToolset")).toBe("asset");
    expect(routeToolset("editor_toolset.toolsets.scene.SceneTools")).toBe("level");
    expect(routeToolset("editor_toolset.toolsets.primitive.PrimitiveTools")).toBe("level");
    expect(routeToolset("editor_toolset.toolsets.object.ObjectTools")).toBe("reflection");
    expect(routeToolset("aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools")).toBe("gameplay");
    expect(routeToolset("WorldConditionsToolset.WorldConditionTools")).toBe("gameplay");
    expect(routeToolset("SlateInspectorToolset.SlateInspectorToolset")).toBe("widget");
    expect(routeToolset("PluginToolset.PluginToolset")).toBe("plugins");
    expect(routeToolset("GameFeaturesToolset.GameFeaturesToolset")).toBe("plugins");
    expect(routeToolset("EditorToolset.EditorAppToolset")).toBe("editor");
    expect(routeToolset("EditorToolset.LogsToolset")).toBe("editor");
    expect(routeToolset("ConfigSettingsToolset.ConfigSettingsToolset")).toBe("project");
    expect(routeToolset("AutomationTestToolset.AutomationTestToolset")).toBe("project");
  });

  it("gives Epic-only editor domains their own category rather than the umbrella", () => {
    expect(routeToolset("DataflowAgent.DataflowAgentToolset")).toBe("dataflow");
    expect(routeToolset("conversation_toolset.toolsets.conversation.ConversationTools")).toBe("conversation");
  });

  it("keeps PhysicsAsset out of asset, where substring matching would strand it", () => {
    // "PhysicsAssetToolset" contains "assettools", so the generic asset rule
    // would swallow it if the physics rule were not tested first.
    expect(routeToolset("PhysicsToolsets.PhysicsAssetToolset")).toBe("gameplay");
  });

  it("returns null only for registry meta-tooling, which the umbrella owns", () => {
    expect(routeToolset("ToolsetRegistry.AgentSkillToolset")).toBeNull();
    expect(routeToolset("editor_toolset.toolsets.programmatic.ProgrammaticToolset")).toBeNull();
  });
});

describe("enrichToolsWithEpicCatalog", () => {
  it("injects Epic tools as first-class actions into mapped categories", () => {
    const tools = fixtureTools();
    const r = enrichToolsWithEpicCatalog(tools, CATALOG);

    expect(r.injected).toBe(5);
    expect(r.byCategory).toEqual({ gas: 2, niagara: 1, conversation: 1, epic: 1 });

    const gas = tools.find((t) => t.name === "gas")!;
    expect(gas.actions.epic_list_attributes).toBeDefined();
    expect(gas.actions.epic_find_attribute_set_classes).toBeDefined();
    expect(gas.actions.epic_list_attributes.bridge).toBe("epic_call_tool");
  });

  it("binds toolset + tool via mapParams and passes input through", () => {
    const tools = fixtureTools();
    enrichToolsWithEpicCatalog(tools, CATALOG);
    const gas = tools.find((t) => t.name === "gas")!;
    const mapped = gas.actions.epic_list_attributes.mapParams!({
      action: "epic_list_attributes",
      input: { className: "MyAttrs" },
    });
    expect(mapped).toEqual({
      toolset: "GASToolsets.AttributeSetToolset",
      tool: "GASToolsets.AttributeSetToolset.ListAttributes",
      input: { className: "MyAttrs" },
      inputJson: undefined,
    });
  });

  it("rebuilds the action enum and adds a shared input schema", () => {
    const tools = fixtureTools();
    enrichToolsWithEpicCatalog(tools, CATALOG);
    const gas = tools.find((t) => t.name === "gas")!;

    // The action enum must now advertise the injected keys plus the original.
    // It is advertised rather than enforced: `action` parses as a string so a
    // misspelling is refused by dispatch, with suggestions, instead of by the
    // MCP layer with the whole options array. So the assertion is on the list.
    const advertised = actionEnumValues(gas.schema.action);
    expect(advertised).toContain("epic_list_attributes");
    expect(advertised).toContain("grant_ability");
    expect(advertised).not.toContain("nope_not_real");

    expect(gas.schema.input).toBeDefined();
    expect(gas.schema.inputJson).toBeDefined();
  });

  it("routes registry meta-tooling to the epic umbrella", () => {
    const tools = fixtureTools();
    enrichToolsWithEpicCatalog(tools, CATALOG);
    const epic = tools.find((t) => t.name === "epic")!;
    expect(epic.actions.epic_list_skills).toBeDefined();
  });

  it("materialises an Epic-only category that ue-mcp does not declare", () => {
    const tools = fixtureTools();
    expect(tools.some((t) => t.name === "conversation")).toBe(false);

    const r = enrichToolsWithEpicCatalog(tools, CATALOG);
    expect(r.createdCategories).toEqual(["conversation"]);

    const conversation = tools.find((t) => t.name === "conversation")!;
    expect(conversation).toBeDefined();
    expect(conversation.actions.epic_list_speakers).toBeDefined();
    // The seed action that satisfies the non-empty enum must not survive.
    expect(Object.keys(conversation.actions)).toEqual(["epic_list_speakers"]);
    expect(conversation.schema.action.safeParse("epic_list_speakers").success).toBe(true);
    expect(conversation.schema.input).toBeDefined();
    // It must not have leaked into the umbrella as well.
    const epic = tools.find((t) => t.name === "epic")!;
    expect(epic.actions.epic_list_speakers).toBeUndefined();
  });

  it("does not create a category when the catalog has no tools for it", () => {
    const tools = fixtureTools();
    const r = enrichToolsWithEpicCatalog(tools, { toolsets: [] });
    expect(r.createdCategories).toEqual([]);
    expect(tools.some((t) => t.name === "conversation")).toBe(false);
    expect(tools.some((t) => t.name === "dataflow")).toBe(false);
  });

  it("is a no-op for an empty catalog", () => {
    const tools = fixtureTools();
    const r = enrichToolsWithEpicCatalog(tools, { toolsets: [] });
    expect(r.injected).toBe(0);
  });

  it("skips categories listed in excludeCategories", () => {
    const tools = fixtureTools();
    const r = enrichToolsWithEpicCatalog(tools, CATALOG, { excludeCategories: ["gas"] });
    expect(r.byCategory.gas).toBeUndefined();
    expect(r.byCategory.niagara).toBe(1);
    const gas = tools.find((t) => t.name === "gas")!;
    expect(Object.keys(gas.actions).some((a) => a.startsWith("epic_"))).toBe(false);
  });

  it("strips em dashes out of upstream descriptions and param hints", () => {
    // Upstream catalog text is not written to this repo's style rules, and
    // these strings are read by every connected agent and republished as
    // docs/native-tools.md, so enrichment is the single place they get fixed.
    // The two literals below are the input and the expected output of that
    // conversion, so this test has to keep the character. Leave it alone
    // during style sweeps.
    const tools = fixtureTools();
    enrichToolsWithEpicCatalog(tools, {
      toolsets: [
        {
          name: "NiagaraToolsets.NiagaraToolset_System",
          tools: [
            {
              name: "NiagaraToolsets.NiagaraToolset_System.GetEmitterData",
              description: "Returns emitter properties — fields use PascalCase.", // em-dash-allowed: fixture input for the sanitiser
              inputSchema: { properties: { system: {} }, required: ["system"] },
            },
          ],
        },
      ],
    });
    const niagara = tools.find((t) => t.name === "niagara")!;
    const desc = niagara.actions.epic_get_emitter_data.description;
    expect(desc).not.toContain("—"); // em-dash-allowed: asserts the sanitiser removed it
    expect(desc).toContain("Returns emitter properties - fields use PascalCase.");
  });
});

describe("deterministic collision suffixes (#875)", () => {
  /**
   * Two toolsets routed to the same category, both shipping a tool whose bare
   * name is ImportFile. Whichever is injected first keeps `epic_import_file`
   * and the other is qualified with its toolset, so the pair swap names if the
   * enumeration order swaps. Unreal does not promise that order.
   */
  const COLLIDING: EpicCatalog = {
    toolsets: [
      {
        name: "AssetToolsets.TextureToolset",
        tools: [{ name: "AssetToolsets.TextureToolset.ImportFile", description: "Import a texture." }],
      },
      {
        name: "AssetToolsets.AudioToolset",
        tools: [{ name: "AssetToolsets.AudioToolset.ImportFile", description: "Import a sound." }],
      },
      {
        name: "AssetToolsets.MeshToolset",
        tools: [{ name: "AssetToolsets.MeshToolset.ImportFile", description: "Import a mesh." }],
      },
    ],
  };

  const assetTools = (): ToolDef[] => [
    categoryTool("asset", "Assets", { list: bp("read", "list", "list_assets") }, undefined, {}),
    categoryTool("epic", "Epic gateway", { status: bp("read", "status", "epic_status") }, undefined, {}),
  ];

  /** Every permutation of the three toolsets, as injected action names. */
  function namesFor(order: number[]): string[] {
    const tools = assetTools();
    enrichToolsWithEpicCatalog(tools, { toolsets: order.map((i) => COLLIDING.toolsets[i]) });
    const asset = tools.find((t) => t.name === "asset")!;
    return Object.keys(asset.actions).filter((a) => a.startsWith("epic_")).sort();
  }

  const PERMUTATIONS = [
    [0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0],
  ];

  it("resolves the collision the same way whatever order the catalog arrives in", () => {
    const first = namesFor(PERMUTATIONS[0]);
    // Sanity: the fixture really does collide, or this proves nothing.
    expect(first).toHaveLength(3);
    expect(first).toContain("epic_import_file");
    expect(first.filter((n) => n !== "epic_import_file")).toHaveLength(2);

    for (const order of PERMUTATIONS.slice(1)) {
      expect(namesFor(order), `catalog order ${order.join(",")}`).toEqual(first);
    }
  });

  it("gives the bare name to the first toolset in name order", () => {
    // AudioToolset sorts before MeshToolset before TextureToolset, so the bare
    // name is the audio one no matter how the editor enumerated them.
    for (const order of PERMUTATIONS) {
      const tools = assetTools();
      enrichToolsWithEpicCatalog(tools, { toolsets: order.map((i) => COLLIDING.toolsets[i]) });
      const asset = tools.find((t) => t.name === "asset")!;
      expect(asset.actions.epic_import_file.description).toContain("Import a sound.");
    }
  });

  it("keeps the same description on each qualified action across orders", () => {
    const described = (order: number[]): Record<string, string> => {
      const tools = assetTools();
      enrichToolsWithEpicCatalog(tools, { toolsets: order.map((i) => COLLIDING.toolsets[i]) });
      const asset = tools.find((t) => t.name === "asset")!;
      const out: Record<string, string> = {};
      for (const [key, spec] of Object.entries(asset.actions)) {
        if (key.startsWith("epic_")) out[key] = spec.description ?? "";
      }
      return out;
    };
    const first = described(PERMUTATIONS[0]);
    for (const order of PERMUTATIONS.slice(1)) {
      expect(described(order), `catalog order ${order.join(",")}`).toEqual(first);
    }
  });

  it("does not reorder a single toolset's own tools", () => {
    // Only the toolset sequence is imposed. Within one toolset the registry's
    // own order is authored and is left alone.
    const tools = assetTools();
    enrichToolsWithEpicCatalog(tools, {
      toolsets: [
        {
          name: "AssetToolsets.TextureToolset",
          tools: [
            { name: "AssetToolsets.TextureToolset.ZResize" },
            { name: "AssetToolsets.TextureToolset.ACompress" },
          ],
        },
      ],
    });
    const asset = tools.find((t) => t.name === "asset")!;
    expect(Object.keys(asset.actions).filter((a) => a.startsWith("epic_")))
      .toEqual(["epic_zresize", "epic_acompress"]);
  });
});
