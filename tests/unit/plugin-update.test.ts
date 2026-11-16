/** #1276: plugin update runs install's full path for every plugin not at its target. */
import { describe, expect, it } from "vitest";
import { planPluginUpdates, type PluginUpdateFacts } from "../../src/extensions/plugin-install.js";

function facts(installed: Record<string, string>, latest: Record<string, string | null>, native: Record<string, string> = {}): PluginUpdateFacts {
  return {
    installed: (n) => installed[n],
    latest: (n) => (n in latest ? latest[n] : null),
    deployedNative: (n) => native[n],
  };
}

describe("planPluginUpdates", () => {
  it("moves a plugin to the newest published version, across a 0.x minor", () => {
    const plan = planPluginUpdates([{ name: "voxel-plugin-tools" }], undefined, undefined,
      facts({ "voxel-plugin-tools": "0.6.1" }, { "voxel-plugin-tools": "0.7.2" }));
    expect(plan.steps).toEqual([{ name: "voxel-plugin-tools", to: "0.7.2", pin: undefined, reason: "0.6.1 -> 0.7.2" }]);
    expect(plan.errors).toEqual([]);
  });

  it("moves a pinned entry's pin with it, and keeps an unpinned entry unpinned", () => {
    const plan = planPluginUpdates([{ name: "a", version: "1.0.0" }, { name: "b" }], undefined, undefined,
      facts({ a: "1.0.0", b: "2.0.0" }, { a: "1.1.0", b: "2.1.0" }));
    expect(plan.steps.map((s) => [s.name, s.pin])).toEqual([["a", "1.1.0"], ["b", undefined]]);
  });

  it("installs the given --version for the named plugin, matched by slug", () => {
    const plan = planPluginUpdates([{ name: "ue-mcp-meshy" }], "meshy", "0.3.0", facts({ "ue-mcp-meshy": "0.4.0" }, {}));
    expect(plan.steps).toEqual([{ name: "ue-mcp-meshy", to: "0.3.0", pin: undefined, reason: "0.4.0 -> 0.3.0" }]);
  });

  it("redeploys when the package is current but the native module or the pin is not", () => {
    const plan = planPluginUpdates([{ name: "a" }, { name: "b", version: "0.6.1" }], undefined, undefined,
      facts({ a: "0.7.0", b: "0.7.0" }, { a: "0.7.0", b: "0.7.0" }, { a: "0.6.2" }));
    expect(plan.steps.map((s) => s.reason)).toEqual(["native module deployed from 0.6.2", "ue-mcp.yml pins 0.6.1"]);
  });

  it("reports a plugin already at its target as current", () => {
    const plan = planPluginUpdates([{ name: "a" }], undefined, undefined, facts({ a: "1.0.0" }, { a: "1.0.0" }, { a: "1.0.0" }));
    expect(plan.steps).toEqual([]);
    expect(plan.current).toEqual(["a@1.0.0"]);
  });

  it("fails instead of guessing", () => {
    expect(planPluginUpdates([{ name: "a" }], undefined, "1.0.0", facts({}, {})).errors).toEqual(["--version needs a plugin name"]);
    expect(planPluginUpdates([{ name: "a" }], "zzz", undefined, facts({}, {})).errors[0]).toMatch(/zzz is not in ue-mcp.yml/);
    expect(planPluginUpdates([{ name: "a" }], undefined, undefined, facts({ a: "1.0.0" }, {})).errors[0]).toMatch(/latest version of a/);
  });
});
