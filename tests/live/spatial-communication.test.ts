/** Captured camera provenance, using only the verified disposable test project. */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
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
