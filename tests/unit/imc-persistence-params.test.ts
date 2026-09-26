import { describe, expect, it } from "vitest";
import { z } from "zod";
import { assetTool } from "../../src/tools/asset.js";
import { gameplayTool } from "../../src/tools/gameplay.js";
import type { ToolDef } from "../../src/core/types.js";

type RouteCase = {
  tool: ToolDef;
  input: Record<string, unknown>;
  bridge: string;
  expected: Record<string, unknown>;
};

const IMC = "/Game/Input/IMC_Default";
const IA = "/Game/Input/IA_Jump";

const cases: RouteCase[] = [
  { tool: gameplayTool, input: { action: "add_imc_mapping", imcPath: IMC, inputActionPath: IA, key: "SpaceBar" }, bridge: "add_imc_mapping", expected: { imcPath: IMC, inputActionPath: IA, key: "SpaceBar" } },
  { tool: gameplayTool, input: { action: "remove_imc_mapping", imcPath: IMC, mappingIndex: 1 }, bridge: "remove_imc_mapping", expected: { imcPath: IMC, mappingIndex: 1 } },
  { tool: gameplayTool, input: { action: "set_imc_mapping_key", imcPath: IMC, mappingIndex: 1, newKey: "Enter" }, bridge: "set_imc_mapping_key", expected: { imcPath: IMC, mappingIndex: 1, newKey: "Enter" } },
  { tool: gameplayTool, input: { action: "set_imc_mapping_action", imcPath: IMC, mappingIndex: 1, newInputActionPath: IA }, bridge: "set_imc_mapping_action", expected: { imcPath: IMC, mappingIndex: 1, newInputActionPath: IA } },
  { tool: gameplayTool, input: { action: "set_mapping_modifiers", imcPath: IMC, mappingIndex: 1, modifiers: [] }, bridge: "set_mapping_modifiers", expected: { imcPath: IMC, mappingIndex: 1, modifiers: [] } },
  // The asset spellings reach the editor as sent; the spec aliases resolve them there.
  { tool: assetTool, input: { action: "add_input_mapping", mappingContext: IMC, inputAction: IA, key: "SpaceBar" }, bridge: "add_imc_mapping", expected: { mappingContext: IMC, inputAction: IA, key: "SpaceBar" } },
  { tool: assetTool, input: { action: "remove_input_mapping", mappingContext: IMC, mappingIndex: 1 }, bridge: "remove_imc_mapping", expected: { mappingContext: IMC, mappingIndex: 1 } },
];

describe("IMC persistence parameters", () => {
  it("validates and routes save=true, save=false, and an omitted save for every IMC mutator", async () => {
    for (const route of cases) {
      for (const save of [true, false, undefined]) {
        const input = save === undefined ? route.input : { ...route.input, save };
        const parsed = z.object(route.tool.schema).parse(input);
        let call: { method: string; params: Record<string, unknown> } | undefined;
        const ctx = {
          bridge: {
            call: async (method: string, params: Record<string, unknown>) => {
              call = { method, params };
              return { success: true };
            },
          },
        } as never;

        await route.tool.handler(ctx, parsed);

        expect(call?.method).toBe(route.bridge);
        expect(call?.params).toMatchObject({ ...route.expected, ...(save === undefined ? {} : { save }) });
        expect(call?.params.save).toBe(save);
      }

      expect(z.object(route.tool.schema).safeParse({ ...route.input, save: "true" }).success).toBe(false);
    }
  });
});
