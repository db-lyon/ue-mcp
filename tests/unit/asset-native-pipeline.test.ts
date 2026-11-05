import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { assetTool } from "../../src/tools/asset.js";
import { materialTool } from "../../src/tools/material.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import { paramZod, zodSignature } from "../../src/surface/handler-spec.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import type { ToolContext } from "../../src/core/types.js";

async function forwards(action: string, method: string, params: Record<string, unknown>) {
  const result = { success: true, saved: false, readBack: { skeletonPath: "/Game/Skeleton" } };
  const call = vi.fn().mockResolvedValue(result);
  const ctx = { bridge: { call } } as unknown as ToolContext;
  expect(await assetTool.handler(ctx, { action, ...params })).toEqual(result);
  expect(call).toHaveBeenCalledWith(method, params, undefined);
}

describe("texture batch controls", () => {
  it("forwards per-file presets and overrides without discarding false or zero", async () => {
    await forwards("import_texture_batch", "import_texture_batch", {
      items: [
        { filePath: "C:/Art/N.png", preset: "normal", lodGroup: "WeaponNormalMap", flipGreenChannel: false },
        { filePath: "C:/Art/RMA.png", preset: "masks", settings: { maxTextureSize: 0, sRGB: false } },
        { filePath: "C:/Art/C.png", preset: "color", lodGroup: "Weapon" },
      ], packagePath: "/Game/Textures", save: false, dryRun: true,
    });
  });

  it("accepts texture presets and bounds the size", () => {
    for (const preset of ["normal", "masks", "color"]) expect(assetTool.schema.preset.safeParse(preset).success).toBe(true);
    expect(assetTool.schema.preset.safeParse("colourish").success).toBe(false);
    for (const size of [0, 512, 32768]) expect(assetTool.schema.maxTextureSize.safeParse(size).success).toBe(true);
    for (const size of [-1, 1.5, 32769]) expect(assetTool.schema.maxTextureSize.safeParse(size).success).toBe(false);
    expect(assetTool.schema.flipGreenChannel.safeParse("false").success).toBe(false);
  });

  it("retains the single texture selector and exposes the batch alternative", async () => {
    for (const method of ["get_texture_info", "set_texture_settings"]) {
      expect(handlerSpecs[method].choices).toEqual([{ mode: "exactlyOne", branches: [["assetPath"], ["assetPaths"]] }]);
    }
    await forwards("set_texture_settings", "set_texture_settings", {
      assetPaths: ["/Game/T_A", "/Game/T_B"], preset: "masks", maxTextureSize: 1024, neverStream: true, save: false, dryRun: true,
    });
    await forwards("get_texture_info", "get_texture_info", { assetPaths: ["/Game/T_A", "/Game/T_B"] });
    await forwards("get_texture_info", "get_texture_info", { assetPath: "/Game/T_A" });
  });

  it("locks all textures in a settings batch", () => {
    const classified = classifyAction("asset.set_texture_settings", { assetPaths: ["/Game/T_A", "/Game/T_B"], maxTextureSize: 512 });
    expect(classified.mutates).toBe(true);
    expect(classified.paths.sort()).toEqual(["/Game/T_A", "/Game/T_B"]);
  });
});

describe("skeletal import and summaries", () => {
  it("forwards the Interchange controls and target skeleton", async () => {
    await forwards("import_skeletal_mesh", "import_skeletal_mesh", {
      filePath: "C:/Art/SK_Prop.fbx", packagePath: "/Game/Props", importer: "interchange",
      skeletonPath: "/Game/Props/Skeleton", importUniformScale: 100, replaceExisting: false,
      importMaterials: false, importTextures: false, save: true, dryRun: false,
    });
  });

  it("rejects unknown importer choices and describes socket read-back", () => {
    expect(assetTool.schema.importer.safeParse("interchange").success).toBe(true);
    expect(assetTool.schema.importer.safeParse("automatic").success).toBe(false);
    expect(assetTool.actions.get_mesh_info.description).toContain("sockets");
    expect(assetTool.actions.set_property.description).toContain("propertyName=Skeleton");
  });
});

describe("DCC exports", () => {
  it("forwards material JSON export and file overwrite controls", async () => {
    await forwards("export", "export_asset", {
      assetPath: "/Game/MI_Prop", outputPath: "Saved/Export/parameters.json", format: "json", replaceExisting: false, dryRun: true,
    });
    expect(assetTool.actions.export.description).toContain("skeletal meshes to FBX");
    expect(assetTool.actions.export_texture.bridge).toBe("export_texture");
  });
});

describe("registry queries", () => {
  it("validates a name regex before dispatch", () => {
    expect(assetTool.schema.nameRegex.safeParse("pick|grab|loot").success).toBe(true);
    expect(assetTool.schema.nameRegex.safeParse("[").success).toBe(false);
    expect(assetTool.schema.nameRegex.safeParse("a".repeat(513)).success).toBe(false);
  });

  it("compares refinements by their input type without hiding transforms", () => {
    const declared = handlerSpecs.list_assets.params.find((param) => param.name === "nameRegex")!;
    expect(zodSignature(assetTool.schema.nameRegex)).toBe(zodSignature(paramZod(declared).optional()));
    const integer = z.number().int().min(1).max(8);
    expect(zodSignature(integer.refine((value) => value !== 3), { rules: true }))
      .toBe(zodSignature(integer, { rules: true }));
    expect(zodSignature(z.string().transform(Number))).not.toBe(zodSignature(z.string()));
  });

  it("forwards registry filters and cursor as supplied", async () => {
    await forwards("list", "list_assets", {
      directory: "/Game", classPaths: ["/Script/Engine.AnimSequence", "/Script/Engine.AnimMontage"],
      recursiveClasses: true, nameRegex: "pick|grab", regexIgnoreCase: false,
      skeletonPath: "/Game/Characters/Skeleton.Skeleton", includeMetadata: true, limit: 20, cursor: "resume",
    });
  });

  it("forwards batched map references and mesh collision requests", async () => {
    await forwards("get_referencers", "get_asset_referencers", {
      packages: ["/Game/SM_Stair", "/Game/SM_Ramp"], includeDetails: true, includeMeshInfo: true, maxDepth: 3,
    });
    const depth = paramZod(handlerSpecs.get_asset_referencers.params.find((p) => p.name === "maxDepth")!);
    expect(depth.safeParse(8).success).toBe(true);
    expect(depth.safeParse(9).success).toBe(false);
    expect(depth.safeParse(0).success).toBe(false);
  });
});

describe("master material discovery", () => {
  it.each(["SAMPLERTYPE_Color", "SAMPLERTYPE_Normal", "SAMPLERTYPE_Masks"])("forwards reflected sampler value %s unchanged", async (value) => {
    const params = { materialPath: "/Game/M_Master", expressionIndex: 0, propertyName: "SamplerType", value };
    const result = { success: true, importedValue: value };
    const call = vi.fn().mockResolvedValue(result);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    expect(await materialTool.handler(ctx, { action: "set_expression_value", ...params })).toEqual(result);
    expect(call).toHaveBeenCalledWith("set_expression_value", params, undefined);
  });

  it("describes texture parameters and instance texture assignment on existing actions", () => {
    expect(materialTool.actions.add_expression.description).toContain("TextureSampleParameter2D");
    expect(materialTool.actions.add_expression.description).toContain("packed RMA");
    expect(materialTool.actions.create_instance.description).toContain("batch_set_instances");
    expect(materialTool.actions.build_material.description).toContain("textureParameters");
  });
});
