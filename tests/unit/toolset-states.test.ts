/**
 * Reading epic_get_toolset_states for the category toggles (#1827), against
 * the answer the bridge handler actually builds (EpicHandlers_Toolsets.cpp).
 */
import { describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { GET_TOOLSETS_METHOD, liveToolsetStates, parseToolsetStates } from "../../src/tools/project/categories.js";

/** The handler's answer: per-toolset objects plus the registry's lists. */
const handlerAnswer = {
  success: true,
  toolsetCount: 2,
  enabledCount: 1,
  toolsets: [
    { name: "EditorToolset.MaterialToolset", enabled: true, blockedBy: [], allowedBy: [], outsideAllowList: false },
    {
      name: "GasToolset.Abilities",
      enabled: false,
      blockedBy: ["/^\\QGasToolset.Abilities\\E$/"],
      allowedBy: [],
      outsideAllowList: false,
      disabledBy: "blockList",
      toggledHere: true,
    },
  ],
  blockedNames: ["/^\\QGasToolset.Abilities\\E$/"],
  allowedNames: [],
};

function ctxAnswering(answer: unknown, epicToolsets: string[] | null = null): Pick<ToolContext, "bridge"> {
  const bridge = {
    isConnected: true,
    epicToolsets,
    call: async (method: string) => {
      expect(method).toBe(GET_TOOLSETS_METHOD);
      return answer;
    },
  };
  return { bridge } as unknown as Pick<ToolContext, "bridge">;
}

describe("epic_get_toolset_states as the toggles read it", () => {
  it("reads every toolset's name and enabled state from the handler's answer", () => {
    expect(parseToolsetStates(handlerAnswer)).toEqual([
      { name: "EditorToolset.MaterialToolset", enabled: true },
      { name: "GasToolset.Abilities", enabled: false },
    ]);
  });

  it("returns the states from a live bridge", async () => {
    const live = await liveToolsetStates(ctxAnswering(handlerAnswer));
    expect(live.error).toBeUndefined();
    expect(live.states?.map((s) => s.name)).toEqual(["EditorToolset.MaterialToolset", "GasToolset.Abilities"]);
  });

  it("takes a refusal as an error, not as an editor with no toolsets", async () => {
    const refusal = { success: false, code: "unsupported_engine_version", error: "Toolset toggles require Unreal Engine 5.8 or newer" };
    const live = await liveToolsetStates(ctxAnswering(refusal, ["EditorToolset.MaterialToolset"]));
    expect(live.error).toMatch(/5\.8 or newer/);
    // The toolsets the bridge already knows about still show, as enabled.
    expect(live.states).toEqual([{ name: "EditorToolset.MaterialToolset", enabled: true }]);
  });

  it("reports no states, with the reason, when a refusing bridge knows no toolsets either", async () => {
    const live = await liveToolsetStates(ctxAnswering({ success: false, error: "Epic ToolsetRegistry not available." }));
    expect(live.states).toBeNull();
    expect(live.error).toMatch(/not available/);
  });
});
