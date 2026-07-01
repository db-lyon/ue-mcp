import { describe, it, expect } from "vitest";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import {
  routeToolset,
  enrichToolsWithEpicCatalog,
  type EpicCatalog,
} from "../../src/epic-enrich.js";

function fixtureTools(): ToolDef[] {
  return [
    categoryTool("gas", "GAS", { grant_ability: bp("grant", "grant_ability") }, undefined, {}),
    categoryTool("niagara", "Niagara", { spawn: bp("spawn", "spawn_system") }, undefined, {}),
    categoryTool("epic", "Epic gateway", { status: bp("status", "epic_status") }, undefined, {}),
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
      // No natural ue-mcp home -> falls to the epic umbrella.
      name: "ConfigSettingsToolset.ConfigSettingsToolset",
      tools: [{ name: "ConfigSettingsToolset.ConfigSettingsToolset.GetSetting" }],
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

  it("returns null for toolsets with no natural home", () => {
    expect(routeToolset("ConfigSettingsToolset.ConfigSettingsToolset")).toBeNull();
    expect(routeToolset("SemanticSearchToolset.SemanticSearchToolset")).toBeNull();
  });
});

describe("enrichToolsWithEpicCatalog", () => {
  it("injects Epic tools as first-class actions into mapped categories", () => {
    const tools = fixtureTools();
    const r = enrichToolsWithEpicCatalog(tools, CATALOG);

    expect(r.injected).toBe(4);
    expect(r.byCategory).toEqual({ gas: 2, niagara: 1, epic: 1 });

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

    // The action enum must now accept the injected keys plus the original.
    expect(gas.schema.action.safeParse("epic_list_attributes").success).toBe(true);
    expect(gas.schema.action.safeParse("grant_ability").success).toBe(true);
    expect(gas.schema.action.safeParse("nope_not_real").success).toBe(false);

    expect(gas.schema.input).toBeDefined();
    expect(gas.schema.inputJson).toBeDefined();
  });

  it("routes unmapped toolsets to the epic umbrella", () => {
    const tools = fixtureTools();
    enrichToolsWithEpicCatalog(tools, CATALOG);
    const epic = tools.find((t) => t.name === "epic")!;
    expect(epic.actions.epic_get_setting).toBeDefined();
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
});
