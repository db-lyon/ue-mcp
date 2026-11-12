import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { actionEnumValues, cloneToolGraph } from "../../src/surface/category-tool.js";
import {
  applyCategoryState,
  disabledActionRefusal,
  editDisableEntries,
  isEpicAction,
  isScopeOff,
  parseDisableEntry,
  protectedScopeRefusal,
  readCategoryState,
  snapshotGraph,
} from "../../src/surface/category-state.js";

describe("disable entries", () => {
  it("reads a bare name as the whole category and a suffix as one scope", () => {
    expect(parseDisableEntry("gas")).toEqual({ category: "gas", scope: "all" });
    expect(parseDisableEntry(" Animation:epic ")).toEqual({ category: "animation", scope: "epic" });
    expect(parseDisableEntry("level:native")).toEqual({ category: "level", scope: "native" });
    expect(parseDisableEntry("level:everything")).toBeNull();
    expect(parseDisableEntry("")).toBeNull();
  });

  it("reads nativeTools.exclude as the epic scope and remembers it was the deprecated spelling", () => {
    const s = readCategoryState({ disable: ["pcg"], nativeTools: { exclude: ["gas"] } });
    expect(isScopeOff(s, "gas", "epic")).toBe(true);
    expect(isScopeOff(s, "gas", "native")).toBe(false);
    expect(isScopeOff(s, "pcg", "native")).toBe(true);
    expect(s.deprecatedExclude).toEqual(["gas"]);
  });

  it("reads nativeTools.enabled false as every category's epic scope", () => {
    const s = readCategoryState({ nativeTools: { enabled: false } });
    expect(isScopeOff(s, "animation", "epic")).toBe(true);
    expect(isScopeOff(s, "animation", "native")).toBe(false);
  });

  it("switches one scope and writes at most one entry per category, in place", () => {
    expect(editDisableEntries(["pcg", "gas"], "level", "epic", false)).toEqual(["pcg", "gas", "level:epic"]);
    expect(editDisableEntries(["level:epic", "pcg"], "level", "native", false)).toEqual(["level", "pcg"]);
    expect(editDisableEntries(["pcg", "level"], "level", "epic", true)).toEqual(["pcg", "level:native"]);
    expect(editDisableEntries(["level:native"], "level", "native", true)).toEqual([]);
    expect(editDisableEntries(["level:native", "pcg"], "level", "all", false)).toEqual(["level", "pcg"]);
    expect(editDisableEntries(["level"], "level", "all", true)).toEqual([]);
  });

  it("refuses to turn off the categories that turn things back on, except their Epic actions", () => {
    expect(protectedScopeRefusal("project", "all")).toMatch(/set_category_enabled/);
    expect(protectedScopeRefusal("project", "native")).toMatch(/set_category_enabled/);
    expect(protectedScopeRefusal("epic", "all")).toMatch(/epic\(status\)/);
    expect(protectedScopeRefusal("project", "epic")).toBeNull();
    expect(protectedScopeRefusal("epic", "epic")).toBeNull();
    expect(protectedScopeRefusal("gas", "all")).toBeNull();
  });
});

describe("applying a category state to the real surface", () => {
  it("takes the epic scope out, and puts it back exactly when the state turns it on", () => {
    const tools = cloneToolGraph(ALL_TOOLS);
    const pristine = snapshotGraph(tools);
    const animation = tools.find((t) => t.name === "animation")!;
    const before = { actions: Object.keys(animation.actions), description: animation.description, enumValues: actionEnumValues(animation.schema.action) };
    const epicCount = before.actions.filter((a) => isEpicAction(a, animation.actions[a])).length;
    expect(epicCount).toBeGreaterThan(0);

    const off = applyCategoryState(tools, pristine, readCategoryState({ disable: ["animation:epic"] }));
    expect(off.removed.animation).toEqual({ native: 0, epic: epicCount });
    expect(Object.keys(animation.actions).some((a) => a.startsWith("epic_"))).toBe(false);
    expect(actionEnumValues(animation.schema.action)).toEqual(Object.keys(animation.actions));
    expect(animation.description).not.toContain("- epic_");

    applyCategoryState(tools, pristine, readCategoryState({}));
    expect(Object.keys(animation.actions)).toEqual(before.actions);
    expect(animation.description).toBe(before.description);
    expect(actionEnumValues(animation.schema.action)).toEqual(before.enumValues);
  });

  it("takes only ue-mcp's own actions out with the native scope, and reports a category left empty", () => {
    const tools = cloneToolGraph(ALL_TOOLS);
    const pristine = snapshotGraph(tools);
    const state = readCategoryState({ disable: ["level:native", "dataflow:epic"] });
    const result = applyCategoryState(tools, pristine, state);
    const level = tools.find((t) => t.name === "level")!;
    expect(Object.keys(level.actions).length).toBeGreaterThan(0);
    expect(Object.entries(level.actions).every(([a, s]) => isEpicAction(a, s))).toBe(true);
    expect(result.empty).toContain("dataflow");
  });

  it("leaves a whole category intact for the surface to withhold, as disable always did", () => {
    const tools = cloneToolGraph(ALL_TOOLS);
    const gas = tools.find((t) => t.name === "gas")!;
    const count = Object.keys(gas.actions).length;
    applyCategoryState(tools, snapshotGraph(tools), readCategoryState({ disable: ["gas"] }));
    expect(Object.keys(gas.actions)).toHaveLength(count);
  });

  it("names the command that turns a refused action back on", () => {
    const tools = cloneToolGraph(ALL_TOOLS);
    const pristine = snapshotGraph(tools);
    const animation = tools.find((t) => t.name === "animation")!;
    const epicAction = Object.keys(animation.actions).find((a) => isEpicAction(a, animation.actions[a]))!;
    const nativeAction = Object.keys(animation.actions).find((a) => !isEpicAction(a, animation.actions[a]))!;

    const scoped = readCategoryState({ disable: ["animation:epic"] });
    expect(disabledActionRefusal(pristine, scoped, `animation.${epicAction}`))
      .toContain("set_category_enabled', category='animation', enabled=true, scope='epic'");
    expect(disabledActionRefusal(pristine, scoped, `animation.${epicAction}`)).toContain("ue-mcp tools enable animation --epic");
    expect(disabledActionRefusal(pristine, scoped, `animation.${nativeAction}`)).toBeNull();

    const whole = readCategoryState({ disable: ["animation"] });
    expect(disabledActionRefusal(pristine, whole, `animation.${nativeAction}`)).toContain("ue-mcp tools enable animation`");

    const legacy = readCategoryState({ nativeTools: { exclude: ["animation"] } });
    expect(disabledActionRefusal(pristine, legacy, `animation.${epicAction}`)).toContain("nativeTools.exclude");

    expect(disabledActionRefusal(pristine, readCategoryState({}), `animation.${epicAction}`)).toBeNull();
    expect(disabledActionRefusal(pristine, whole, "animation.not_an_action")).toBeNull();
  });
});

describe("init writing the category choices", () => {
  it("writes Epic exclusions as <category>:epic, keeps scoped entries, and drops nativeTools.exclude", async () => {
    const fs = await import("node:fs");
    const os = await import("node:os");
    const path = await import("node:path");
    const yaml = (await import("js-yaml")).default;
    const { writeProjectConfig } = await import("../../src/cli/init-core.js");
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "init-disable-"));
    try {
      fs.writeFileSync(path.join(dir, "ue-mcp.yml"), yaml.dump({ "ue-mcp": { disable: ["pcg", "level:native"], nativeTools: { exclude: ["gas"] } } }));
      writeProjectConfig(dir, ["pcg"], { enabled: true, exclude: ["animation"] });
      const block = (yaml.load(fs.readFileSync(path.join(dir, "ue-mcp.yml"), "utf-8")) as { "ue-mcp": Record<string, unknown> })["ue-mcp"];
      expect(block.disable).toEqual(["pcg", "level:native", "animation:epic"]);
      expect(block.nativeTools).toBeUndefined();

      writeProjectConfig(dir, [], { enabled: false, exclude: [] });
      const off = (yaml.load(fs.readFileSync(path.join(dir, "ue-mcp.yml"), "utf-8")) as { "ue-mcp": Record<string, unknown> })["ue-mcp"];
      expect(off.disable).toEqual(["level:native"]);
      expect(off.nativeTools).toEqual({ enabled: false });
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});
