// @live-editor render
/** Captured camera provenance, using only the verified disposable test project. */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import zlib from "node:zlib";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;
const folder = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-spatial-"));

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
  fs.rmSync(folder, { recursive: true, force: true });
});

/** Mean 0-255 luma of an 8-bit RGB or RGBA, non-interlaced PNG. */
function meanLuma(png: Buffer): number {
  let offset = 8;
  let width = 0, height = 0, channels = 0;
  const idat: Buffer[] = [];
  while (offset < png.length) {
    const length = png.readUInt32BE(offset);
    const type = png.toString("ascii", offset + 4, offset + 8);
    const data = png.subarray(offset + 8, offset + 8 + length);
    if (type === "IHDR") {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      expect(data[8]).toBe(8);
      channels = data[9] === 6 ? 4 : 3;
    } else if (type === "IDAT") idat.push(data);
    offset += 12 + length;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const pred = [0, a, b, (a + b) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? b : c][filter];
      out[y * stride + x] = (v + pred) & 0xff;
    }
  }
  let sum = 0;
  for (let i = 0; i < out.length; i += channels) sum += 0.2126 * out[i] + 0.7152 * out[i + 1] + 0.0722 * out[i + 2];
  return sum / (width * height);
}

describe("capture_scene_png spatial evidence", () => {
  it("pairs a PNG with the actual scene-capture camera rather than the editor viewport", async () => {
    const location = { x: 123, y: -456, z: 789 };
    const filename = path.join(folder, "capture.png");
    const body = resultJson<any>(await server.call("editor", {
      action: "capture_scene_png", outputPath: filename, location,
      rotation: { pitch: 0, yaw: 0, roll: 0 }, fov: 60, width: 64, height: 48,
      world: "editor", fullyLoadTextures: false, timeoutMs: 120_000,
    }));
    expect(body.success).toBe(true);
    expect(fs.readFileSync(filename).subarray(0, 8)).toEqual(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    expect(body.captureActorRemoved).toBe(true);
    expect(body.captureMetadata).toMatchObject({
      location, fovDegrees: 60, width: 64, height: 48, projection: "perspective",
      world: "editor", fullyLoadTextures: false,
      forward: { x: 1, y: 0, z: 0 }, right: { x: 0, y: 1, z: 0 }, up: { x: 0, y: 0, z: 1 },
    });
    expect(body.captureMetadata.worldPath).toBeTruthy();
    expect(body.captureMetadata.focus).toBeUndefined();
  });

  it("takes the camera pose from captureTransform and refuses it given twice (#1458)", async () => {
    const location = { x: -321, y: 654, z: 987 };
    const filename = path.join(folder, "capture-transform.png");
    const body = resultJson<any>(await server.call("editor", {
      action: "capture_scene_png", outputPath: filename,
      captureTransform: { location, rotation: { pitch: 0, yaw: 90, roll: 0 } },
      width: 64, height: 48, world: "editor", fullyLoadTextures: false, timeoutMs: 120_000,
    }));
    expect(body.success, JSON.stringify(body)).toBe(true);
    expect(body.paramsNotRead).toBeUndefined();
    expect(body.captureMetadata).toMatchObject({ location });
    expect(body.captureMetadata.forward.x).toBeCloseTo(0, 5);
    expect(body.captureMetadata.forward.y).toBeCloseTo(1, 5);

    const bridge = await liveBridge();
    const twice = path.join(folder, "capture-transform-twice.png");
    const refused = await bridge.call("capture_scene_png", {
      outputPath: twice, location, captureTransform: { location }, fullyLoadTextures: false,
    }) as any;
    expect(refused.success).toBe(false);
    expect(refused.error).toMatch(/captureTransform\.location/);
    expect(fs.existsSync(twice)).toBe(false);
  });

  it("exposes an enclosed, lamp-lit interior instead of rendering it near black (#1549)", async () => {
    const bridge = await liveBridge();
    const prefix = "SpatialCaptureRoom_";
    const centre = { x: 0, y: -60000, z: 2000 };
    const at = (dx: number, dy: number, dz: number) => ({ x: centre.x + dx, y: centre.y + dy, z: centre.z + dz });
    // Six 100 uu cubes scaled into a closed 1000 uu room, so no sky or sun reaches inside.
    const walls = [
      { name: "Floor", location: at(0, 0, -550), scale: { x: 12, y: 12, z: 1 } },
      { name: "Ceiling", location: at(0, 0, 550), scale: { x: 12, y: 12, z: 1 } },
      { name: "Front", location: at(550, 0, 0), scale: { x: 1, y: 12, z: 12 } },
      { name: "Back", location: at(-550, 0, 0), scale: { x: 1, y: 12, z: 12 } },
      { name: "Left", location: at(0, -550, 0), scale: { x: 12, y: 1, z: 12 } },
      { name: "Right", location: at(0, 550, 0), scale: { x: 12, y: 1, z: 12 } },
    ];
    await bridge.call("delete_actors", { labelPrefix: prefix });
    try {
      for (const wall of walls) {
        const placed = await bridge.call("place_actor", {
          actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube",
          label: prefix + wall.name, location: wall.location, scale: wall.scale,
        }) as any;
        expect(placed.success, JSON.stringify(placed)).not.toBe(false);
      }
      const lamp = await bridge.call("spawn_light", { lightType: "point", label: prefix + "Lamp", location: centre, intensity: 5000, attenuationRadius: 2000, mobility: "movable" }) as any;
      expect(lamp.success, JSON.stringify(lamp)).not.toBe(false);

      const filename = path.join(folder, "interior.png");
      const body = await bridge.call("capture_scene_png", {
        outputPath: filename, location: at(-300, 0, 0), rotation: { pitch: 0, yaw: 0, roll: 0 },
        fov: 70, width: 160, height: 90, world: "editor", fullyLoadTextures: false,
      }, 120_000) as any;
      expect(body.success, JSON.stringify(body)).toBe(true);
      expect(body.captureMetadata.exposure).toMatchObject({ source: "captureAutoExposure" });
      expect(body.captureMetadata.exposure.settlePasses).toBeGreaterThan(0);
      // Before #1549 the capture had no view state, so it used a fixed exposure from the
      // middle of the auto exposure range and this frame averaged close to 0.
      expect(meanLuma(fs.readFileSync(filename))).toBeGreaterThan(25);
    } finally {
      await bridge.call("delete_actors", { labelPrefix: prefix });
    }
  }, 180_000);

  it("rejects a degenerate camera projection before writing a file", async () => {
    const bridge = await liveBridge();
    for (const fov of [180, 1e-300, 179.99999999]) {
      const filename = path.join(folder, `invalid-fov-${fov}.png`);
      const body = await bridge.call("capture_scene_png", { outputPath: filename, fov }) as any;
      expect(body.success).toBe(false);
      expect(body.error).toMatch(/fov/);
      expect(fs.existsSync(filename)).toBe(false);
    }
  });

  it("rejects reversed or overflowing focus framing before capture", async () => {
    const bridge = await liveBridge();
    const identity = await bridge.call("execute_python", {
      code: "import unreal; print(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings().get_path_name())",
    }) as any;
    const focusActorPath = identity.output.trim();
    expect(focusActorPath).toContain(":PersistentLevel.");
    for (const focusMargin of [-1, 1e308]) {
      const filename = path.join(folder, `invalid-margin-${focusMargin}.png`);
      const body = await bridge.call("capture_scene_png", { outputPath: filename, focusActorPath, focusMargin }) as any;
      expect(body.success).toBe(false);
      expect(body.error).toMatch(/focusMargin|finite coordinates/);
      expect(fs.existsSync(filename)).toBe(false);
    }
  });
});
