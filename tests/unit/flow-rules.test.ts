import { describe, it, expect } from "vitest";
// @ts-expect-error - plain ESM script, no types
import { flowNames, namespacedPaths, strayCleanupFlows, DEMO_FLOWS, DEMO_PATHS } from "../../scripts/check-flows.mjs";
// @ts-expect-error - plain ESM script, no types
import { lintText } from "../../scripts/lint-prose.mjs";

const paths = (src: string) =>
  (namespacedPaths(src) as Array<{ path: string }>).map((h) => h.path);

/**
 * A flow creates content somebody keeps. Two things make one look like
 * throwaway bridge output instead, and both are visible in the definition.
 */
describe("a flow default names a real asset domain", () => {
  it("refuses a path that names the tool", () => {
    expect(paths('const PKG = "/Game/Flows/Starter";')).toEqual(["/Game/Flows/Starter"]);
    expect(paths('packagePath: "/Game/MCP/Materials"')).toEqual(["/Game/MCP/Materials"]);
  });

  it("allows a real production domain", () => {
    expect(paths('const PKG = "/Game/Materials/PBR";')).toEqual([]);
    expect(paths('const FIRE_PKG = "/Game/VFX/Fire";')).toEqual([]);
  });

  it("exempts only the exact paths the demos already use", () => {
    expect([...(DEMO_PATHS as Set<string>)]).toEqual(["/Game/Flows/Beacon", "/Game/MCP_Home"]);
  });

  it("ignores a path inside a comment", () => {
    expect(paths('  // was "/Game/Flows/Old" before')).toEqual([]);
  });

  it("catches a template literal, which is how a parameterised default is written", () => {
    expect(paths("outPath: `/Game/Flows/${name}`,")).toEqual(["/Game/Flows/${name}"]);
    expect(paths("outPath: `/Game/MCP/Thing`,")).toEqual(["/Game/MCP/Thing"]);
  });

  it("catches it whatever the case, since the content root is not case sensitive", () => {
    expect(paths('outPath: "/game/Flows/Thing",')).toEqual(["/game/Flows/Thing"]);
  });
});

describe("cleanup twins belong to demos only", () => {
  const names = ["niagara_fire", "beacon", "neon_shrine", "neon_shrine_cleanup"];

  it("allows a demo's cleanup", () => {
    expect(strayCleanupFlows(names)).toEqual([]);
  });

  it("refuses one paired with an ordinary flow", () => {
    expect(strayCleanupFlows([...names, "texture_bomb_cleanup"])).toEqual(["texture_bomb_cleanup"]);
  });

  it("refuses it by what the name means, not by one suffix", () => {
    // Checking only for a trailing _cleanup invited the rename rather than
    // the rethink.
    for (const n of ["cleanup_water", "water_teardown", "waterSetupCleanup", "wipe_props"]) {
      expect(strayCleanupFlows([n]), n).toEqual([n]);
    }
  });

  it("does not mistake an ordinary flow for one", () => {
    for (const n of ["niagara_fire", "texture_bomb", "material_starter_pack"]) {
      expect(strayCleanupFlows([n]), n).toEqual([]);
    }
  });

  it("knows which flows are demos", () => {
    expect(DEMO_FLOWS as Set<string>).toContain("neon_shrine");
    expect(DEMO_FLOWS as Set<string>).not.toContain("niagara_fire");
  });
});

describe("reading flow names out of the loader", () => {
  it("finds a flow by its description key", () => {
    const src = [
      "  return {",
      "    my_flow: {",
      "      description:",
      '        "does a thing",',
      "      steps: {},",
      "    },",
      "  };",
    ].join("\n");
    expect(flowNames(src)).toEqual(["my_flow"]);
  });
});

/**
 * The range is stated and nothing more. Somebody on an unmentioned version who
 * hits a problem files an issue, which is the system working.
 */
describe("the engine range is never hedged", () => {
  const rules = (text: string): string[] =>
    (lintText(text, { rel: "docs/x.md" }) as Array<{ rule: string }>).map((f) => f.rule);

  it("refuses an annotation about what was compiled", () => {
    // lint-prose-allow: range-hedge  the fixture has to be a violation
    expect(rules("Unreal Engine 5.4 to 5.8, compiled and verified on 5.7 and 5.8.")).toContain("range-hedge");
  });

  it("refuses a note about what is gated but unbuilt", () => {
    // lint-prose-allow: range-hedge  the fixture has to be a violation
    expect(rules("5.4 to 5.6 are version-gated but not built here.")).toContain("range-hedge");
  });

  it("allows the range stated plainly", () => {
    expect(rules("Unreal Engine 5.4 to 5.8.")).toEqual([]);
  });

  it("says nothing about a line with no version in it", () => {
    expect(rules("Compiled and verified against the handler suite.")).toEqual([]);
  });
});
