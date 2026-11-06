import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import type { ToolContext } from "../../src/core/types.js";

describe("asset append_array_elements", () => {
  it("maps structured elements to the generic bridge handler", async () => {
    const call = vi.fn(async () => ({ success: true }));
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const elements = [
      { Offset: { x: 1, y: 2, z: 3 } },
      { Offset: { x: 4, y: 5, z: 6 } },
    ];

    await assetTool.handler(ctx, {
      action: "append_array_elements",
      assetPath: "/Game/AI/SOD_Test",
      propertyName: "Slots",
      elements,
    });

    expect(call).toHaveBeenCalledWith("append_asset_array_elements", {
      assetPath: "/Game/AI/SOD_Test",
      propertyName: "Slots",
      elements,
    }, undefined);
  });

  it("passes unique through to the append handler", async () => {
    const call = vi.fn(async () => ({ success: true }));
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await assetTool.handler(ctx, {
      action: "append_array_elements",
      assetPath: "/Game/Data/DA_BuildList",
      propertyName: "OnlyInMaps",
      elements: ["/Game/Maps/SurvivalArena"],
      unique: true,
    });
    expect(call).toHaveBeenCalledWith("append_asset_array_elements", {
      assetPath: "/Game/Data/DA_BuildList",
      propertyName: "OnlyInMaps",
      elements: ["/Game/Maps/SurvivalArena"],
      unique: true,
    }, undefined);
  });

  it.each([
    { selector: { indices: [0, 2] } },
    { selector: { elements: ["/Game/Maps/SurvivalArena"] } },
  ])("maps remove_array_elements with $selector to the bridge handler", async ({ selector }) => {
    const call = vi.fn(async () => ({ success: true }));
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await assetTool.handler(ctx, {
      action: "remove_array_elements",
      assetPath: "/Game/Data/DA_BuildList",
      propertyName: "OnlyInMaps",
      ...selector,
    });
    expect(call).toHaveBeenCalledWith("remove_asset_array_elements", {
      assetPath: "/Game/Data/DA_BuildList",
      propertyName: "OnlyInMaps",
      ...selector,
    }, undefined);
  });

  it("refuses a non-integer index before dispatch", () => {
    expect(assetTool.schema.indices.safeParse([1.5]).success).toBe(false);
    expect(assetTool.schema.indices.safeParse([0, 3]).success).toBe(true);
  });

  it("rejects an empty element batch before dispatch", () => {
    expect(assetTool.schema.elements.safeParse([]).success).toBe(false);
    expect(assetTool.schema.elements.safeParse([{ Enabled: true }]).success).toBe(true);
  });

  it.each([
    { action: "set_property", method: "set_asset_property", args: { propertyName: "Profiles[2].Presentation.PrimaryUse", value: "/Game/Animation/A_Clip" }, previous: "None" },
    { action: "append_array_elements", method: "append_asset_array_elements", args: { propertyName: "Profiles", elements: [{ Id: "Appended" }] }, previous: "((Id=Original,Presentation=(CameraRelativeTransform=(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=1,Y=2,Z=3),Scale3D=(X=2,Y=3,Z=4)))))" },
  ])("keeps $action unsaved when its returned inverse is replayed", async ({ action, method, args, previous }) => {
    const assetPath = "/Game/Data/DA_Profiles";
    const payload = { assetPath, propertyName: args.propertyName, value: previous, save: false };
    const response = { success: true, persisted: false, rollback: { method: "set_asset_property", payload } };
    const call = vi.fn().mockResolvedValueOnce(response).mockResolvedValueOnce({ success: true, persisted: false });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const result = await assetTool.handler(ctx, { action, assetPath, ...args, save: false }) as typeof response;
    expect(call).toHaveBeenNthCalledWith(1, method, { assetPath, ...args, save: false }, undefined);
    expect(result).toEqual(response);
    await assetTool.handler(ctx, { action: "set_property", ...result.rollback.payload });
    expect(call).toHaveBeenNthCalledWith(2, "set_asset_property", payload, undefined);
  });
});
