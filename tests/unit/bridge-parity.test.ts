/**
 * Advertised versus deployed (#1021).
 *
 * A report against 1.2.4 said a session read `pluginBuildStale: false` while
 * an advertised method was absent, and that every discovery of a gap came one
 * failed call at a time. Both halves are true and neither is a bug in the
 * staleness check: it compares build timestamps, which is a different question
 * from whether a handler is registered.
 *
 * The handshake has carried the running binary's own method list all along.
 * These cover comparing the two, and the one thing that must not happen: an
 * old plugin that publishes no list being reported as having nothing missing.
 */
import { describe, it, expect } from "vitest";
import { checkBridgeParity, deployedPlugin, unadvertisedMethods } from "../../src/bridge-parity.js";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import type { BridgeCapabilities } from "../../src/bridge.js";

function graph(): ToolDef[] {
  return [
    categoryTool("alpha", "Alpha", {
      list: bp("read", "List things. Params: none", "alpha_list"),
      save: bp("mutate", "Save a thing. Params: none", "alpha_save"),
    }),
    categoryTool("beta", "Beta", {
      read: bp("read", "Read a thing. Params: none", "beta_read"),
      // A handler action reaches the bridge from its own body, so it declares
      // no method here and is not part of the advertised method set.
      local: { kind: "handler", effect: "read", description: "Params: none", handler: async () => ({}) },
    }),
  ];
}

const caps = (actions: string[] | undefined): BridgeCapabilities =>
  ({ actions, actionCount: actions?.length } as unknown as BridgeCapabilities);

describe("comparing the surface against the plugin that answered", () => {
  it("says nothing when the plugin has everything", () => {
    const parity = checkBridgeParity(graph(), caps(["alpha_list", "alpha_save", "beta_read"]));
    expect(parity.checked).toBe(true);
    expect(parity.advertised).toBe(3);
    expect(parity.missing).toEqual([]);
    expect(parity.message).toBeNull();
  });

  it("names the advertised methods the plugin does not register", () => {
    const parity = checkBridgeParity(graph(), caps(["alpha_list"]));
    expect(parity.missing).toEqual(["alpha_save", "beta_read"]);
    expect(parity.message).toContain("alpha_save");
    expect(parity.message).toContain("beta_read");
    expect(parity.message).toContain("Unknown method");
  });

  it("counts a method once however many actions dispatch through it", () => {
    // The real case: 830 wrapped engine tools share `epic_call_tool`, and a
    // report saying 830 methods were missing because one is absent would be
    // useless.
    const tools = [
      categoryTool("gas", "GAS", {
        epic_a: bp("read", "A. Params: none", "epic_call_tool"),
        epic_b: bp("mutate", "B. Params: none", "epic_call_tool"),
      }),
    ];
    // A plugin with handlers, just not that one. An EMPTY list means the
    // plugin published nothing, which is the case below, not a plugin that
    // registered zero handlers.
    const parity = checkBridgeParity(tools, caps(["something_else"]));
    expect(parity.advertised).toBe(1);
    expect(parity.missing).toEqual(["epic_call_tool"]);
  });

  it("reports nothing rather than everything when the plugin publishes no list", () => {
    // The direction that matters. A plugin too old to send its handler list
    // must not read as a plugin missing every method, and must not read as one
    // that has them all either: nothing was compared, so nothing is claimed.
    for (const c of [caps(undefined), caps([]), null, undefined]) {
      const parity = checkBridgeParity(graph(), c);
      expect(parity.checked).toBe(false);
      expect(parity.missing).toEqual([]);
      expect(parity.message).toBeNull();
    }
  });

  it("ignores handler-backed actions, which declare no method to compare", () => {
    const parity = checkBridgeParity(graph(), caps(["alpha_list", "alpha_save", "beta_read"]));
    expect(parity.advertised, "beta.local declares no bridge method").toBe(3);
  });

  it("reads the graph it is handed, so a disabled category is not reported missing", () => {
    const full = checkBridgeParity(graph(), caps(["alpha_list", "alpha_save"]));
    expect(full.missing).toEqual(["beta_read"]);
    // The same plugin, with beta disabled for this session: nothing is missing.
    const narrowed = checkBridgeParity(graph().slice(0, 1), caps(["alpha_list", "alpha_save"]));
    expect(narrowed.missing).toEqual([]);
  });

  it("stays absent on a healthy session, so status only grows when there is something to say", () => {
    const parity = checkBridgeParity(graph(), caps(["alpha_list", "alpha_save", "beta_read"]));
    // No build time published and nothing missing: nothing to report.
    expect(deployedPlugin(caps(["alpha_list", "alpha_save", "beta_read"]), parity)).toBeUndefined();
    // Nothing answered at all.
    expect(deployedPlugin(null, parity)).toBeUndefined();
  });

  it("reports the running binary's own account in one field", () => {
    const c = { actions: ["alpha_list"], builtAt: "Aug 28 2026 11:04:12" } as unknown as BridgeCapabilities;
    const reported = deployedPlugin(c, checkBridgeParity(graph(), c));
    expect(reported?.builtAt).toBe("Aug 28 2026 11:04:12");
    expect(reported?.missingActions).toBe(2);
    expect(reported?.warning).toContain("alpha_save");
  });

  it("reports a build time even when every method is present", () => {
    // The half parity cannot see: a handler that is there and old. #1002 was
    // a fix that shipped three days before it was reported as broken, by a
    // session whose deployed plugin predated it.
    const c = {
      actions: ["alpha_list", "alpha_save", "beta_read"],
      builtAt: "Aug 01 2026 09:00:00",
    } as unknown as BridgeCapabilities;
    const reported = deployedPlugin(c, checkBridgeParity(graph(), c));
    expect(reported?.builtAt).toBe("Aug 01 2026 09:00:00");
    expect(reported?.missingActions).toBeUndefined();
  });

  it("can also report what the plugin has that nothing advertises", () => {
    // Not a defect: an alternate spelling, or a handler shipped ahead of the
    // TypeScript that will expose it. Available, and not part of the warning.
    expect(unadvertisedMethods(graph(), caps(["alpha_list", "alpha_save", "beta_read", "spare_handler"])))
      .toEqual(["spare_handler"]);
  });
});
