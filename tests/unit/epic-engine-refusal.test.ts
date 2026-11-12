/**
 * A wrapped Epic tool called on a recorded engine whose registry lacks it is
 * refused before it is sent, with unsupported_engine_version naming the
 * engines that have it. The tools here are fixtures; "5.8" is the recorded
 * engine the real catalog carries.
 */
import { describe, it, expect, vi } from "vitest";
import { engineMinor, epicEngineRefusal, epicEngineRefusalFor } from "../../src/surface/epic-engines.js";
import { actionPreparation, runAction } from "../../src/flow/run-action.js";
import { EPIC_CATALOG_ENGINES } from "../../src/tools/epic/catalog-engines.js";
import type { ActionSpec, ToolDef } from "../../src/core/types.js";
import type { FlowContext } from "../../src/flow/context.js";

const TOOLSET = "aaa_toolset.toolsets.blueprint.NewBlueprintTools";
const only59 = { toolset: TOOLSET, name: `${TOOLSET}.compile_blueprint`, engines: ["5.9"] };
const recorded = ["5.8", "5.9"];

const spec = {
  kind: "bridge",
  bridge: "epic_call_tool",
  effect: "mutate",
  description: "fixture",
  epicTool: only59,
  epicSchema: { properties: { blueprint: { type: "object", properties: { refPath: {} } } }, required: ["blueprint"] },
} as unknown as ActionSpec;

describe("engine versions", () => {
  it("reads major.minor out of every spelling an editor reports", () => {
    expect(engineMinor("5.9")).toBe("5.9");
    expect(engineMinor("5.8.1-123+++UE5+Release-5.8")).toBe("5.8");
    expect(engineMinor("5.10.0")).toBe("5.10");
    expect(engineMinor("{GUID}")).toBeNull();
    expect(engineMinor(undefined)).toBeNull();
  });
});

describe("a call on an engine without the tool", () => {
  it("is refused with unsupported_engine_version naming the engines that have it", () => {
    expect(epicEngineRefusal("blueprint.epic_x", only59, "5.8.1-123+++UE5+Release-5.8", recorded)).toEqual({
      success: false,
      errorCode: "unsupported_engine_version",
      error: expect.stringContaining("It is available on UE 5.9."),
      engineVersion: "5.8",
      availableOn: ["5.9"],
    });
  });

  it("is sent on an engine that has it, on an unrecorded engine, and when the version is unknown", () => {
    expect(epicEngineRefusal("t", only59, "5.9.0", recorded)).toBeNull();
    expect(epicEngineRefusal("t", only59, "5.10.0", recorded)).toBeNull();
    expect(epicEngineRefusal("t", only59, undefined, recorded)).toBeNull();
    expect(epicEngineRefusal("t", { toolset: TOOLSET, name: "x" }, "5.8.0", recorded)).toBeNull();
  });

  it("is found by task name in a tool graph, which is how the MCP route asks", () => {
    expect(EPIC_CATALOG_ENGINES).toContain("5.8");
    const graph = [{ name: "blueprint", actions: { epic_x: spec } }] as unknown as ToolDef[];
    expect(epicEngineRefusalFor(graph, "blueprint.epic_x", "5.8.0")?.availableOn).toEqual(["5.9"]);
    expect(epicEngineRefusalFor(graph, "blueprint.epic_x", "5.9.0")).toBeNull();
    expect(epicEngineRefusalFor(graph, "blueprint.other", "5.8.0")).toBeNull();
  });

  it("never reaches the bridge when dispatched against a 5.8 editor", async () => {
    const call = vi.fn(async () => ({ success: true }));
    const ctx = { bridge: { capabilities: { engineVersion: "5.8.0-1+++UE5+Release-5.8" }, call } } as unknown as FlowContext;
    const prep = actionPreparation(undefined, "epic_x", spec);
    const result = await runAction(ctx, "blueprint.epic_x", spec, { blueprint: "/Game/BP" }, prep);
    expect(call).not.toHaveBeenCalled();
    expect(result.data).toMatchObject({ success: false, errorCode: "unsupported_engine_version", availableOn: ["5.9"] });
  });

  it("is sent to a 5.9 editor", async () => {
    const call = vi.fn(async () => ({ success: true, result: {} }));
    const ctx = { bridge: { capabilities: { engineVersion: "5.9.0" }, call } } as unknown as FlowContext;
    const prep = actionPreparation(undefined, "epic_x", spec);
    await runAction(ctx, "blueprint.epic_x", spec, { blueprint: "/Game/BP" }, prep);
    expect(call).toHaveBeenCalledTimes(1);
    expect((call.mock.calls[0] as unknown[])[0]).toBe("epic_call_tool");
  });
});
