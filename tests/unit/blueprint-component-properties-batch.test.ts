import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/core/types.js";
import { actionSchema } from "../../src/surface/action-schema.js";
import { ROUTING_PARAM_NAMES } from "../../src/surface/routing-params.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import { liftRollback } from "../../src/flow/rollback.js";
import { resolveBridgeTimeout } from "../../src/bridge/bridge-timeouts.js";

const METHOD = "set_blueprint_component_properties_batch";
const ACTION = "set_component_properties_batch";

function context(response: unknown = { success: true }) {
  const call = vi.fn().mockResolvedValue(response);
  return { call, ctx: { bridge: { call } } as unknown as ToolContext };
}

describe("blueprint.set_component_properties_batch", () => {
  it("forwards nested property maps, including references, nulls and arrays, in one call", async () => {
    const { call, ctx } = context();
    const params = {
      items: [
        { assetPath: "/Game/AI/BP_One", componentName: "Combat", properties: {
          AttackMontage: "/Game/Animation/AM_Attack.AM_Attack",
          BlockMontage: null,
          Tags: ["Melee", "Enemy"],
          "Config.Weight": 2.5,
        } },
        { assetPath: "/Game/AI/BP_Two", componentName: "/Script/Engine.CharacterMovementComponent",
          properties: { MaxWalkSpeed: 450, bOrientRotationToMovement: true } },
      ],
      dryRun: true,
      maxBlueprints: 18,
    };
    await blueprintTool.handler(ctx, { action: ACTION, ...params });
    expect(call).toHaveBeenCalledTimes(1);
    expect(call).toHaveBeenCalledWith(METHOD, params, 600_000);
  });

  it("forwards directory selection and leaves defaults to the native handler", async () => {
    const { call, ctx } = context();
    const params = {
      directory: "/Game/AI/Enemies",
      classFilter: "/Script/Engine.Character",
      componentName: "CharacterMovement",
      properties: { MaxWalkSpeed: 500 },
      recursive: false,
    };
    await blueprintTool.handler(ctx, { action: ACTION, ...params });
    expect(call).toHaveBeenCalledWith(METHOD, params, 600_000);
    expect(call.mock.calls[0][1]).not.toHaveProperty("items");
    expect(call.mock.calls[0][1]).not.toHaveProperty("dryRun");
  });

  it("preserves per-item failures and the rollback payload", async () => {
    const payload = { items: [{ assetPath: "/Game/BP_One", componentName: "Combat",
      properties: { AttackMontage: "None", Weight: 0 } }] };
    const response = {
      success: false,
      results: [
        { index: 0, success: true, stage: "complete", saved: true, values: { Weight: 1 } },
        { index: 1, success: false, stage: "write_guard", saved: false, error: "Package is read-only", values: {} },
      ],
      rollback: { method: METHOD, payload },
    };
    const { ctx } = context(response);
    const result = await blueprintTool.handler(ctx, { action: ACTION, ...payload });
    expect(result).toEqual(response);
    expect(liftRollback((result as typeof response).rollback)).toEqual({
      taskName: "ue-mcp.bridge", payload: { ...payload, method: METHOD },
    });
  });

  it("advertises the recorded parameters and nested item types", () => {
    const schema = actionSchema(blueprintTool, ACTION);
    expect(schema.bridge).toBe(METHOD);
    expect(schema.params.map((param) => param.name).filter((name) => !ROUTING_PARAM_NAMES.includes(name)).sort()).toEqual([
      "classFilter", "componentName", "directory", "dryRun", "items", "maxBlueprints", "properties", "recursive",
    ]);
    expect(blueprintTool.schema.items.safeParse([{ assetPath: "/Game/BP", componentName: "Combat",
      properties: { Montage: null } }]).success).toBe(true);
    expect(blueprintTool.schema.items.safeParse("/Game/BP").success).toBe(false);
    expect(blueprintTool.schema.properties.safeParse({ Montage: null }).success).toBe(true);
    expect(blueprintTool.schema.dryRun.safeParse("true").success).toBe(false);
    expect(declaredActionEffect("blueprint", ACTION)).toBe("mutate");
  });

  it("allows enough time for the native batch to finish", () => {
    expect(blueprintTool.actions[ACTION].timeoutMs).toBe(600_000);
    expect(resolveBridgeTimeout(METHOD, undefined, {})).toBeGreaterThanOrEqual(600_000);
  });
});
