import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { editorTool } from "../../src/tools/editor.js";
import { assetTool } from "../../src/tools/asset.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

// {instanceClass, properties} is read by the bridge (SetInstancedSubobject);
// the server must forward it as an object, unflattened (#1292).
const VALUE = { instanceClass: "/Script/MotionWarping.RootMotionModifier_Scale", properties: { Scale: { x: 0, y: 0, z: 0 } } };

async function route(tool: typeof editorTool, input: Record<string, unknown>) {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  await tool.handler(ctx, z.object(tool.schema).parse(input));
  return call.mock.calls[0] as [string, Record<string, unknown>];
}

describe("instanced subobject values (#1292)", () => {
  it("editor set_property forwards an instanceClass value", async () => {
    const [method, params] = await route(editorTool, {
      action: "set_property", objectPath: "/Game/M.M:AnimNotifyState_MotionWarping_1", propertyName: "RootMotionModifier", value: VALUE,
    });
    expect(method).toBe("set_property");
    expect(params.value).toEqual(VALUE);
  });

  it("asset set_property forwards an instanceClass value", async () => {
    const [method, params] = await route(assetTool, {
      action: "set_property", assetPath: "/Game/DA", propertyName: "Modifier", value: VALUE,
    });
    expect(method).toBe("set_asset_property");
    expect(params.value).toEqual(VALUE);
  });

  it("documents the shape on both setters", () => {
    expect(editorTool.actions.set_property.description).toMatch(/instanceClass/);
    expect(assetTool.actions.set_property.description).toMatch(/instanceClass/);
  });
});
