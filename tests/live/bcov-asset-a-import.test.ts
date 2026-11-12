// @live-editor render
/**
 * Imports and texture assets, read back through a second action: generic,
 * static mesh, skeletal mesh and animation imports from files the editor
 * exports first, texture info, export and compare, a render target and an
 * Interchange pipeline. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { deflateSync } from "node:zlib";
import { callBridge, closeEditorTabWindows, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetA/Import";
const TUTORIAL = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

/** callBridge, or a direct call with a longer client budget for a slow handler. */
async function dispatch(method: string, params: Json, timeoutMs?: number): Promise<TestResult> {
  if (!timeoutMs) return callBridge(bridge, method, params);
  try {
    return { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
  } catch (e) {
    return { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
  }
}

async function call(method: string, params: Json, timeoutMs?: number): Promise<Json> {
  const r = await dispatch(method, params, timeoutMs);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function listNames(directory: string): Promise<string[]> {
  const r = await call("list_assets", { directory, recursive: false, limit: 500 });
  return ((r.assets as Array<{ name: string }>) ?? []).map((a) => a.name);
}

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

function crc32(buf: Buffer): number {
  let c = 0xffffffff;
  for (const byte of buf) c = CRC_TABLE[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Buffer): Buffer {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const typed = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(typed));
  return Buffer.concat([len, typed, crc]);
}

/** A solid-colour RGBA PNG. */
function png(width: number, height: number, rgba: [number, number, number, number]): Buffer {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 6;
  const row = Buffer.concat([Buffer.from([0]), Buffer.from(Array.from({ length: width }, () => rgba).flat())]);
  const raw = Buffer.concat(Array.from({ length: height }, () => row));
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", header),
    chunk("IDAT", deflateSync(raw)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = mkdtempSync(join(tmpdir(), "ue-mcp-bcov-import-"));
  writeFileSync(join(scratch, "T_BCovRed.png"), png(8, 4, [255, 0, 0, 255]));
  writeFileSync(join(scratch, "T_BCovBlue.png"), png(16, 16, [0, 0, 255, 255]));
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
}, 120_000);

afterAll(async () => {
  if (bridge) await closeEditorTabWindows(bridge);
  if (scratch) rmSync(scratch, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 180_000);

describe("textures", () => {
  it("import_file imports PNGs that get_texture_info, compare_textures and export_texture read", async () => {
    const red = await call("import_file", { filePath: join(scratch, "T_BCovRed.png"), packagePath: DIR, name: "T_BCovRed" }, 120_000);
    expect(red.importedCount).toBe(1);
    expect((red.imported as Array<{ class: string }>)[0].class).toBe("Texture2D");
    await call("import_file", { filePath: join(scratch, "T_BCovRed.png"), packagePath: DIR, name: "T_BCovRedCopy" }, 120_000);
    await call("import_file", { filePath: join(scratch, "T_BCovBlue.png"), packagePath: DIR, name: "T_BCovBlue" }, 120_000);
    expect(await listNames(DIR)).toEqual(expect.arrayContaining(["T_BCovRed", "T_BCovRedCopy", "T_BCovBlue"]));

    const info = await call("get_texture_info", { assetPath: `${DIR}/T_BCovRed` });
    expect(info.sizeX).toBe(8);
    expect(info.sizeY).toBe(4);

    const same = await call("compare_textures", { assetPathA: `${DIR}/T_BCovRed`, assetPathB: `${DIR}/T_BCovRedCopy` });
    expect(same.sameDimensions).toBe(true);
    expect(same.sameSourceContent).toBe(true);
    const different = await call("compare_textures", { assetPathA: `${DIR}/T_BCovRed`, assetPathB: `${DIR}/T_BCovBlue` });
    expect(different.identical).toBe(false);
    expect(different.sameDimensions).toBe(false);
    expect(different.widthB).toBe(16);

    const outFile = join(scratch, "exported", "blue.png");
    const exported = await call("export_texture", { assetPath: `${DIR}/T_BCovBlue`, outputPath: outFile });
    expect(exported.width).toBe(16);
    expect(existsSync(outFile)).toBe(true);
    const bytes = readFileSync(outFile);
    expect(bytes.subarray(1, 4).toString("ascii")).toBe("PNG");
    expect(bytes.readUInt32BE(16)).toBe(16);
    expect(bytes.readUInt32BE(20)).toBe(16);

    // A size cap shrinks the platform data but not the PNG, which is written
    // from the source. The reported size is the written image's (#1550).
    await call("set_texture_settings", { assetPath: `${DIR}/T_BCovBlue`, maxTextureSize: 4 });
    const cappedFile = join(scratch, "exported", "blue-capped.png");
    const capped = await call("export_texture", { assetPath: `${DIR}/T_BCovBlue`, outputPath: cappedFile });
    const cappedBytes = readFileSync(cappedFile);
    expect(capped.width).toBe(cappedBytes.readUInt32BE(16));
    expect(capped.height).toBe(cappedBytes.readUInt32BE(20));
    expect(capped.width).toBe(16);
  }, 300_000);

  it("create_render_target_2d creates a render target with the requested size", async () => {
    const created = await call("create_render_target_2d", { name: "RT_BCov", packagePath: DIR, width: 320, height: 180, format: "RGBA16F" });
    expect(created.created).toBe(true);
    const sizeX = await call("get_asset_properties", { assetPath: `${DIR}/RT_BCov`, propertyName: "SizeX", valueFormat: "json" });
    const sizeY = await call("get_asset_properties", { assetPath: `${DIR}/RT_BCov`, propertyName: "SizeY", valueFormat: "json" });
    expect(sizeX.value).toBe(320);
    expect(sizeY.value).toBe(180);
  }, 120_000);
});

describe("mesh and animation imports", () => {
  it("import_static_mesh imports an FBX the editor exported", async () => {
    const fbx = join(scratch, "SM_BCovCubeSource.fbx");
    await call("export_asset", { assetPath: "/Engine/BasicShapes/Cube", outputPath: fbx }, 120_000);
    expect(existsSync(fbx)).toBe(true);

    const imported = await call("import_static_mesh", {
      filePath: fbx, packagePath: DIR, name: "SM_BCovImported", importMaterials: false, importTextures: false,
    }, 300_000);
    expect(imported.importedCount).toBeGreaterThan(0);
    const geo = await call("get_mesh_geometry", { assetPath: `${DIR}/SM_BCovImported`, include: ["triangles"] });
    expect(geo.assetType).toBe("StaticMesh");
    // Same 2x2-per-face engine cube as the source: 48 triangles.
    expect(geo.triangleCount).toBe(48);
  }, 420_000);

  it("import_skeletal_mesh and import_animation rebuild a character and its animation from FBX", async () => {
    const meshFbx = join(scratch, "SK_BCovSource.fbx");
    const animFbx = join(scratch, "AS_BCovSource.fbx");
    await call("export_asset", { assetPath: `${TUTORIAL}/TutorialTPP`, outputPath: meshFbx }, 300_000);
    await call("export_asset", { assetPath: `${TUTORIAL}/Tutorial_Idle`, outputPath: animFbx }, 300_000);

    const source = await call("list_skeleton_bones", { assetPath: `${TUTORIAL}/TutorialTPP`, includeBones: false });
    const mesh = await call("import_skeletal_mesh", {
      filePath: meshFbx, packagePath: DIR, name: "SK_BCovImported", importMaterials: false, importTextures: false,
    }, 300_000);
    expect(mesh.importedCount).toBeGreaterThan(0);
    const skeleton = String(mesh.skeleton ?? "");
    expect(skeleton).toContain(DIR);
    const bones = await call("list_skeleton_bones", { assetPath: `${DIR}/SK_BCovImported`, includeBones: false });
    expect(bones.boneCount).toBe(source.boneCount);

    const anim = await call("import_animation", {
      filePath: animFbx, packagePath: DIR, name: "AS_BCovImported", skeletonPath: skeleton.split(".")[0],
    }, 300_000);
    const animations = anim.animations as Array<{ path: string; skeleton: string; numberOfFrames: number; boneTrackCount: number }>;
    expect(animations).toHaveLength(1);
    expect(animations[0].skeleton).toBe(skeleton);
    expect(animations[0].numberOfFrames).toBeGreaterThan(1);
    expect(animations[0].boneTrackCount).toBeGreaterThan(0);
    expect(await listNames(DIR)).toContain("AS_BCovImported");
  }, 900_000);
});

describe("interchange pipeline", () => {
  it("create_interchange_pipeline writes its option overrides onto the asset", async () => {
    const created = await call("create_interchange_pipeline", {
      name: "PL_BCov", packagePath: DIR, meshType: "static", options: { "MeshPipeline.bBuildNanite": true },
    });
    expect(created.overridesApplied).toBe(1);
    expect(created.meshType).toBe("static");
    const nanite = await call("get_asset_properties", { assetPath: `${DIR}/PL_BCov`, propertyName: "MeshPipeline.bBuildNanite", valueFormat: "json" });
    expect(nanite.value).toBe(true);
    const staticMeshes = await call("get_asset_properties", { assetPath: `${DIR}/PL_BCov`, propertyName: "MeshPipeline.bImportStaticMeshes", valueFormat: "json" });
    expect(staticMeshes.value).toBe(true);
  }, 120_000);
});
