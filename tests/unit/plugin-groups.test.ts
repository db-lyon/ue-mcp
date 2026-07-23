import { describe, it, expect } from "vitest";
import {
  pluginSlug,
  flowGroup,
  deriveGroups,
  isGroupEnabled,
  runtimeConfigFor,
  partitionFlowsByGroup,
} from "../../src/plugin/plugin-groups.js";

describe("pluginSlug", () => {
  it("strips the conventional ue-mcp- prefix", () => {
    expect(pluginSlug("ue-mcp-recipes")).toBe("recipes");
  });
  it("leaves a non-prefixed package name intact", () => {
    expect(pluginSlug("some-plugin")).toBe("some-plugin");
  });
  it("only strips a leading prefix, not an interior match", () => {
    expect(pluginSlug("ue-mcp-ue-mcp-x")).toBe("ue-mcp-x");
  });
});

describe("flowGroup", () => {
  it("uses the name prefix before the first underscore", () => {
    expect(flowGroup("niagara_fire")).toBe("niagara");
    expect(flowGroup("pcg_scatter_surface")).toBe("pcg");
  });
  it("treats a name with no underscore as its own group", () => {
    expect(flowGroup("beacon")).toBe("beacon");
  });
  it("prefers an explicit group over the prefix", () => {
    expect(flowGroup("niagara_fire", "vfx")).toBe("vfx");
  });
  it("ignores a blank explicit group", () => {
    expect(flowGroup("niagara_fire", "  ")).toBe("niagara");
  });
});

describe("deriveGroups", () => {
  it("returns distinct, sorted groups", () => {
    const groups = deriveGroups({
      niagara_fire: {},
      niagara_smoke: {},
      pcg_scatter_surface: {},
      gas_ability: { group: "gameplay" },
    });
    expect(groups).toEqual(["gameplay", "niagara", "pcg"]);
  });
  it("is empty for no flows", () => {
    expect(deriveGroups({})).toEqual([]);
  });
});

describe("isGroupEnabled (opt-out model)", () => {
  it("enables everything when there is no config", () => {
    expect(isGroupEnabled(undefined, "gas")).toBe(true);
    expect(isGroupEnabled({}, "gas")).toBe(true);
    expect(isGroupEnabled({ groups: {} }, "gas")).toBe(true);
  });
  it("disables only groups explicitly set to false", () => {
    const cfg = { groups: { gas: false, material: true } };
    expect(isGroupEnabled(cfg, "gas")).toBe(false);
    expect(isGroupEnabled(cfg, "material")).toBe(true);
    expect(isGroupEnabled(cfg, "niagara")).toBe(true);
  });
});

describe("runtimeConfigFor", () => {
  const map = {
    recipes: { groups: { gas: false } },
    "other-plugin": { groups: { x: false } },
  };
  it("resolves by slug", () => {
    expect(runtimeConfigFor(map, "ue-mcp-recipes")).toEqual({ groups: { gas: false } });
  });
  it("tolerates a full-package-name key", () => {
    const m = { "ue-mcp-recipes": { groups: { gas: false } } };
    expect(runtimeConfigFor(m, "ue-mcp-recipes")).toEqual({ groups: { gas: false } });
  });
  it("returns undefined when absent", () => {
    expect(runtimeConfigFor(map, "ue-mcp-missing")).toBeUndefined();
    expect(runtimeConfigFor(undefined, "ue-mcp-recipes")).toBeUndefined();
  });
});

describe("partitionFlowsByGroup", () => {
  const flows = {
    niagara_fire: {},
    niagara_smoke: {},
    pcg_scatter_surface: {},
    gas_ability_scaffold: {},
  };
  it("keeps everything with no config", () => {
    const { enabled, disabled } = partitionFlowsByGroup(flows, undefined);
    expect(enabled.sort()).toEqual(Object.keys(flows).sort());
    expect(disabled).toEqual([]);
  });
  it("drops flows whose group is disabled", () => {
    const { enabled, disabled } = partitionFlowsByGroup(flows, { groups: { gas: false } });
    expect(disabled).toEqual(["gas_ability_scaffold"]);
    expect(enabled.sort()).toEqual(["niagara_fire", "niagara_smoke", "pcg_scatter_surface"]);
  });
  it("respects an explicit group override", () => {
    const withGroup = { gas_ability_scaffold: { group: "combat" } };
    const { disabled } = partitionFlowsByGroup(withGroup, { groups: { combat: false } });
    expect(disabled).toEqual(["gas_ability_scaffold"]);
  });
});
