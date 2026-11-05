import { describe, expect, it, vi } from "vitest";
import { pieTool } from "../../src/tools/pie.js";
import { handlerSpecs, paramsClauses } from "../../src/tools/specs/pie.generated.js";
import type { ToolContext } from "../../src/core/types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const WITH_MAP = ["pie_script_create", "pie_script_update", "pie_script_validate", "pie_script_run"] as const;

describe("a test script names the level it plays in", () => {
  it.each(WITH_MAP)("%s takes an optional string map", (method) => {
    const map = handlerSpecs[method].params.find((p) => p.name === "map");
    expect(map).toMatchObject({ type: "string", required: false });
    expect(paramsClauses[method]).toContain("map?");
  });

  it("registers map in C++ under the same name for every action that takes it", () => {
    const source = readHandlerFile("PIEHandlers_Script.cpp");
    expect(source.match(/MCPParam::Optional\(TEXT\("map"\)/g)).toHaveLength(WITH_MAP.length);
    expect(source).toContain('TryGetStringParam(Params, TEXT("map"), Map)');
    expect(source).toContain('R->SetStringField(TEXT("map"), Asset->Map)');
  });

  it("forwards map to the bridge as sent", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = {
      name: "S_Meadow",
      steps: [{ instruction: "Walk to the gate", judge: "manual" }],
      manual_only: true,
      map: "/Game/Maps/L_WG_Meadow",
    };
    await pieTool.handler(ctx, { action: "script_create", ...params });
    expect(call).toHaveBeenCalledWith("pie_script_create", expect.objectContaining({ map: "/Game/Maps/L_WG_Meadow" }), undefined);
  });

  it("opens the script's level with the loader the UAT runner uses", () => {
    expect(readHandlerFile("PIEUatRunner.cpp")).toContain("MCPLoadMapIfNeeded(Map, OutError)");
    expect(readHandlerFile("PIEUatRunner.cpp")).not.toContain("bool LoadMapIfNeeded(");
    const runner = readHandlerFile("PIEScriptRunner.cpp");
    expect(runner).toContain("MCPLoadMapIfNeeded(Cfg.Script.Map, OutError)");
    expect(runner).toContain("MCPLoadMapIfNeeded(Source.Map, OutError)");
    // The level opens before the observer arms, so a refusal leaves nothing armed.
    expect(runner.indexOf("MCPLoadMapIfNeeded(Cfg.Script.Map")).toBeLessThan(runner.indexOf("FPIEObserver::Get().Arm(ObsCfg"));
  });
});
