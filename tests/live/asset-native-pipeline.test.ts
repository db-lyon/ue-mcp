// @live-editor render
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { getBridge, disconnectBridge, callBridge, closeEditorTabWindows, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";

let bridge: EditorBridge;
const id = `AssetPipeline_${Date.now()}`;
const folder = `${TEST_PREFIX}/${id}`;
const savedDir = path.join(TEST_PROJECT_DIR, "Saved");
const disk = path.join(savedDir, id);
const created = new Set<string>();
const textures = ["T_Color", "T_Normal", "T_RMA"].map((name) => `${folder}/${name}.${name}`);

async function run(method: string, params: Record<string, unknown>) {
  const response = await callBridge(bridge, method, params);
  expect(response.ok, response.error).toBe(true);
  const result = response.result as Record<string, any>;
  for (const asset of result.importedAssets ?? []) {
    if (typeof asset === "string" && asset.startsWith(`${folder}/`)) created.add(asset);
  }
  expect(result.success, JSON.stringify(result)).not.toBe(false);
  return result;
}

beforeAll(async () => {
  bridge = await getBridge();
  fs.mkdirSync(disk, { recursive: true });
});

afterAll(async () => {
  if (bridge) await closeEditorTabWindows(bridge);
  if (created.size) await callBridge(bridge, "delete_asset_batch", { assetPaths: [...created], force: true });
  disconnectBridge();
  if (path.dirname(disk) !== savedDir) throw new Error("Unexpected fixture directory");
  fs.rmSync(disk, { recursive: true, force: true });
});

describe.sequential("native asset pipeline", () => {
  it("imports per-file presets, previews batch settings and reads back every texture", async () => {
    const source = path.join(disk, "source.png");
    fs.writeFileSync(source, Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=", "base64"));
    const items = ["color", "normal", "masks"].map((preset, index) => ({
      filePath: source, name: ["T_Color", "T_Normal", "T_RMA"][index], preset,
      settings: { lodGroup: ["Weapon", "WeaponNormalMap", "WeaponSpecular"][index], maxTextureSize: 512 },
    }));
    const preview = await run("import_texture_batch", { packagePath: folder, items, dryRun: true });
    expect(preview.imported).toBe(0);
    for (const texture of textures) created.add(texture);
    const imported = await run("import_texture_batch", { packagePath: folder, items, save: true });
    expect(imported.imported).toBe(3);
    expect(imported.items.map((item: any) => item.readBack[0].compressionSettings)).toEqual(["Default", "Normalmap", "Masks"]);
    expect(imported.items.map((item: any) => item.readBack[0].sRGB)).toEqual([true, false, false]);
    expect(imported.items[1].readBack[0].flipGreenChannel).toBe(false);
    expect(imported.items.every((item: any) => item.saved)).toBe(true);

    const dry = await run("set_texture_settings", { assetPaths: textures, maxTextureSize: 256, neverStream: true, dryRun: true });
    expect(dry.items.every((item: any) => item.readBack.maxTextureSize === 512)).toBe(true);
    const invalidBatch = await callBridge(bridge, "set_texture_settings", {
      assetPaths: [textures[0], `${folder}/MissingTexture`], maxTextureSize: 64,
    });
    expect(invalidBatch.ok, invalidBatch.error).toBe(true);
    expect((invalidBatch.result as any).success).toBe(false);
    expect((await run("get_texture_info", { assetPath: textures[0] })).maxTextureSize).toBe(512);
    const changed = await run("set_texture_settings", { assetPaths: textures, maxTextureSize: 256, neverStream: true, save: true });
    expect(changed.saved).toBe(true);
    const read = await run("get_texture_info", { assetPaths: textures });
    expect(read.items.every((item: any) => item.maxTextureSize === 256 && item.neverStream)).toBe(true);
    const unsaved = await run("set_texture_settings", { assetPaths: textures, maxTextureSize: 128 });
    expect(unsaved.saved).toBe(false);
    expect(unsaved.rollback.payload.save).toBe(false);
    expect(Object.keys(unsaved.rollback.payload.items[0].properties)).toEqual(["MaxTextureSize"]);
    await run("set_texture_settings", { assetPaths: textures, neverStream: false });
    await run(unsaved.rollback.method, unsaved.rollback.payload);
    const restored = await run("get_texture_info", { assetPaths: textures });
    expect(restored.items.every((item: any) => item.maxTextureSize === 256 && !item.neverStream)).toBe(true);
    const bad = await callBridge(bridge, "set_texture_settings", { assetPaths: textures, compressionSettings: "NoSuchCompression" });
    expect(bad.ok, bad.error).toBe(true);
    expect((bad.result as any).success).toBe(false);
    const typo = await callBridge(bridge, "set_texture_settings", { assetPaths: textures, settings: { maxTextureSzie: 64 } });
    expect(typo.ok, typo.error).toBe(true);
    expect((typo.result as any).success).toBe(false);
    const after = await run("get_texture_info", { assetPath: textures[2] });
    expect(after.compressionSettings).toBe("Masks");
    await run("export_texture", { assetPath: textures[0], outputPath: path.join(disk, "texture.png") });
    expect(fs.statSync(path.join(disk, "texture.png")).size).toBeGreaterThan(0);
  });

  it("reads static mesh sockets, regex metadata and collision with reference details", async () => {
    const mesh = "/Engine/BasicShapes/Cube.Cube";
    const summary = await run("get_mesh_info", { assetPath: mesh });
    expect(summary.meshKind).toBe("StaticMesh");
    expect(summary.materialSlots.length).toBeGreaterThan(0);
    expect(Array.isArray(summary.sockets)).toBe(true);
    expect(summary.boundsExtent.x).toBeGreaterThan(0);
    const query = await run("list_assets", {
      directory: "/Engine/BasicShapes", classPaths: ["/Script/Engine.StaticMesh"], nameRegex: "^cube$", includeMetadata: true,
    });
    expect(query.assets.map((asset: any) => asset.name)).toContain("Cube");
    expect(query.assets[0].metadata.playLength).toBeNull();
    const ownedMesh = `${folder}/SM_Referenced`;
    const parent = `${folder}/BP_UsesMesh`;
    const child = `${folder}/BP_Child`;
    for (const asset of [ownedMesh, parent, child]) created.add(asset);
    await run("duplicate_asset", { sourcePath: mesh, destinationPath: ownedMesh });
    await run("create_blueprint", { assetPath: parent, parentClass: "Actor" });
    await run("add_component", { assetPath: parent, componentClass: "StaticMeshComponent", componentName: "Mesh" });
    await run("set_blueprint_component_property", { assetPath: parent, componentName: "Mesh", propertyName: "StaticMesh", value: ownedMesh });
    await run("compile_blueprint", { assetPath: parent });
    await run("create_blueprint", { assetPath: child, parentClass: `${parent}.BP_UsesMesh_C` });
    // Package dependency queries require saved references; creation only compiles the child.
    await run("save_asset", { assetPaths: [ownedMesh, parent, child], force: true });
    const refs = await run("get_asset_referencers", { packages: [ownedMesh], includeDetails: true, includeMeshInfo: true, maxDepth: 2 });
    const details = refs.detailsByPackage[ownedMesh];
    expect(Array.isArray(details.maps)).toBe(true);
    expect(details.collision.success).toBe(true);
    expect(details.blueprints).toEqual(expect.arrayContaining([
      expect.objectContaining({ packagePath: parent, depth: 1 }),
      expect.objectContaining({ packagePath: child }),
    ]));
    const outputPath = path.join(disk, "cube.fbx");
    await run("export_asset", { assetPath: mesh, outputPath, dryRun: true });
    expect(fs.existsSync(outputPath)).toBe(false);
    await run("export_asset", { assetPath: mesh, outputPath });
    expect(fs.statSync(outputPath).size).toBeGreaterThan(0);
  });

  it("round-trips a skeletal FBX with Interchange scale and skeleton controls", async () => {
    const candidates = await run("list_assets", { directory: "/Game", classPaths: ["/Script/Engine.SkeletalMesh"], limit: 1 });
    let sourceMesh = candidates.assets[0]?.path;
    if (!sourceMesh) {
      const seedFile = path.join(disk, "seed.fbx");
      await run("export_asset", { assetPath: "/Engine/BasicShapes/Cube.Cube", outputPath: seedFile });
      const seed = await run("import_skeletal_mesh", {
        filePath: seedFile, packagePath: folder, name: "SK_Seed", importer: "interchange",
        importMaterials: false, importTextures: false, save: true,
      });
      sourceMesh = seed.meshes[0].assetPath;
    }
    const sourceFile = path.join(disk, "skeletal.fbx");
    await run("export_asset", { assetPath: sourceMesh, outputPath: sourceFile });
    const params = {
      filePath: sourceFile, packagePath: folder, name: "SK_RoundTrip", importer: "interchange",
      importMaterials: false, importTextures: false, importMorphTargets: false, save: true,
    };
    await run("import_skeletal_mesh", { ...params, dryRun: true, importUniformScale: 1 });
    const imported = await run("import_skeletal_mesh", { ...params, importUniformScale: 1, replaceExisting: false });
    for (const asset of imported.importedAssets) created.add(asset);
    expect(imported.meshes.length).toBeGreaterThan(0);
    const mesh = imported.meshes[0];
    expect(mesh.skeletonPath).toBeTruthy();
    expect(Array.isArray(mesh.sockets)).toBe(true);
    expect(Array.isArray(mesh.materialSlots)).toBe(true);
    const same = await run("list_assets", { directory: folder, classPaths: ["/Script/Engine.SkeletalMesh"], skeletonPath: mesh.skeletonPath, includeMetadata: true });
    expect(same.assets.length).toBeGreaterThan(0);
    const replaced = await run("import_skeletal_mesh", { ...params, replaceExisting: true, importUniformScale: 2, skeletonPath: mesh.skeletonPath });
    for (const asset of replaced.importedAssets) created.add(asset);
    expect(replaced.meshes[0].skeletonPath).toBe(mesh.skeletonPath);
    const firstExtent = Math.hypot(mesh.boundsExtent.x, mesh.boundsExtent.y, mesh.boundsExtent.z);
    const extent = replaced.meshes[0].boundsExtent;
    expect(Math.hypot(extent.x, extent.y, extent.z) / firstExtent).toBeCloseTo(2, 1);
    const conflict = await callBridge(bridge, "import_skeletal_mesh", { ...params, replaceExisting: false });
    expect(conflict.ok, conflict.error).toBe(true);
    expect((conflict.result as any).success).toBe(false);
    const newSkeleton = `${folder}/ReplacementSkeleton.ReplacementSkeleton`;
    created.add(newSkeleton);
    await run("duplicate_asset", { sourcePath: mesh.skeletonPath, destinationPath: `${folder}/ReplacementSkeleton` });
    await run("set_asset_property", { assetPath: mesh.assetPath, propertyName: "Skeleton", value: newSkeleton, save: true });
    const reassigned = await run("get_mesh_info", { assetPath: mesh.assetPath });
    expect(reassigned.skeletonPath).toBe(newSkeleton);
    const sequence = `${folder}/A_Pickup.A_Pickup`;
    created.add(sequence);
    await run("create_sequence", { name: "A_Pickup", packagePath: folder, skeletonPath: newSkeleton, numFrames: 31, frameRate: 30 });
    const animations = await run("list_assets", {
      directory: folder, classPaths: ["/Script/Engine.AnimSequence"], nameRegex: "pickup",
      skeletonPath: newSkeleton, includeMetadata: true,
    });
    expect(animations.assets).toHaveLength(1);
    expect(animations.assets[0].metadata.skeletonPath).toBe(newSkeleton);
    expect(animations.assets[0].metadata.playLength).toBeGreaterThan(0);
  }, 180_000);

  it("authors texture parameters, tint and UV scale, creates an instance, and exports parameter JSON", async () => {
    const materialPath = `${folder}/M_Master`;
    const instancePath = `${folder}/MI_Test`;
    created.add(materialPath);
    created.add(instancePath);
    await run("create_material", { name: "M_Master", packagePath: folder });
    const nodes = new Map<string, any>();
    for (const [name, expressionType, defaultValue] of [
      ["BaseColor", "TextureSampleParameter2D"], ["Normal", "TextureSampleParameter2D"], ["RMA", "TextureSampleParameter2D"],
      ["Tint", "VectorParameter", { r: 1, g: 1, b: 1, a: 1 }], ["UVScale", "ScalarParameter", 1],
      ["RoughnessScale", "ScalarParameter", 1], ["MetallicScale", "ScalarParameter", 1],
      ["UV", "TextureCoordinate"], ["ScaledUV", "Multiply"], ["TintedColor", "Multiply"],
      ["ScaledRoughness", "Multiply"], ["ScaledMetallic", "Multiply"],
    ] as const) {
      nodes.set(name, await run("add_material_expression", { materialPath, name, expressionType, parameterName: name, ...(defaultValue === undefined ? {} : { defaultValue }) }));
    }
    // The reflection setter takes enum identifiers, including the SAMPLERTYPE_ prefix.
    for (const [index, name, sampler] of [[0, "BaseColor", "SAMPLERTYPE_Color"], [1, "Normal", "SAMPLERTYPE_Normal"], [2, "RMA", "SAMPLERTYPE_Masks"]] as const) {
      const expressionIndex = nodes.get(name).expressionIndex;
      await run("set_expression_value", { materialPath, expressionIndex, texturePath: textures[index] });
      await run("set_expression_value", { materialPath, expressionIndex, propertyName: "SamplerType", value: sampler });
    }
    for (const [sourceExpression, targetExpression, targetInput] of [
      ["UV", "ScaledUV", "A"], ["UVScale", "ScaledUV", "B"], ["BaseColor", "TintedColor", "A"], ["Tint", "TintedColor", "B"],
      ["ScaledUV", "BaseColor", "Coordinates"], ["ScaledUV", "Normal", "Coordinates"], ["ScaledUV", "RMA", "Coordinates"],
    ]) await run("connect_material_expressions", { materialPath, sourceExpression, targetExpression, targetInput });
    for (const [sourceExpression, sourceOutput, targetExpression, targetInput] of [
      ["RMA", "R", "ScaledRoughness", "A"], ["RoughnessScale", "0", "ScaledRoughness", "B"],
      ["RMA", "G", "ScaledMetallic", "A"], ["MetallicScale", "0", "ScaledMetallic", "B"],
    ]) await run("connect_material_expressions", { materialPath, sourceExpression, sourceOutput, targetExpression, targetInput });
    for (const [expressionName, property, outputName] of [
      ["TintedColor", "BaseColor", "0"], ["Normal", "Normal", "RGB"],
      ["ScaledRoughness", "Roughness", "0"], ["ScaledMetallic", "Metallic", "0"], ["RMA", "AmbientOcclusion", "B"],
    ]) await run("connect_to_material_property", { materialPath, expressionName, property, outputName });
    await run("recompile_material", { materialPath });
    await run("create_material_instance", { parentPath: materialPath, name: "MI_Test", packagePath: folder });
    const parameters = ["BaseColor", "Normal", "RMA"].map((name, index) => ({ name, type: "texture", value: textures[index] }));
    await run("batch_set_material_instances", { instances: [{ assetPath: instancePath, parameters }] });
    const outputPath = path.join(disk, "parameters.json");
    const exported = await run("export_asset", { assetPath: instancePath, outputPath, format: "json" });
    expect(exported.bytes).toBeGreaterThan(0);
    const contents = JSON.parse(fs.readFileSync(outputPath, "utf8"));
    expect(contents.textureParameters.map((entry: any) => entry.name).sort()).toEqual(["BaseColor", "Normal", "RMA"]);
    expect(contents.scalarParameters.map((entry: any) => entry.name)).toContain("UVScale");
    expect(contents.scalarParameters.map((entry: any) => entry.name)).toContain("RoughnessScale");
    expect(contents.scalarParameters.map((entry: any) => entry.name)).toContain("MetallicScale");
    expect(contents.vectorParameters.map((entry: any) => entry.name)).toContain("Tint");
    expect(contents.textureOverrides.length).toBe(3);
    const before = fs.readFileSync(outputPath, "utf8");
    const refused = await callBridge(bridge, "export_asset", { assetPath: instancePath, outputPath, replaceExisting: false });
    expect(refused.ok, refused.error).toBe(true);
    expect((refused.result as any).success).toBe(false);
    expect(fs.readFileSync(outputPath, "utf8")).toBe(before);
    const previewPath = path.join(disk, "preview.json");
    await run("export_asset", { assetPath: instancePath, outputPath: previewPath, dryRun: true });
    expect(fs.existsSync(previewPath)).toBe(false);
  }, 180_000);
});
